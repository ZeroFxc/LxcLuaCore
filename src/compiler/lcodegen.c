/*
** $Id: lcodegen.c $
** AST to Proto Code Generator
** See Copyright Notice in lua.h
*/

#define lcodegen_c
#define LUA_CORE

#include "lprefix.h"

#include <string.h>
#include <stdio.h>
#include <stdarg.h>

#include "lua.h"
#include "lauxlib.h"

#include "lcode.h"
#include "ldebug.h"
#include "ldo.h"
#include "lfunc.h"
#include "llex.h"
#include "lmem.h"
#include "lobject.h"
#include "lopcodes.h"
#include "lopnames.h"
#include "lparser.h"
#include "lstate.h"
#include "lstring.h"
#include "ltable.h"
#include "lcodegen.h"
#include "lclass.h"
#include "lasm.h"


#define LOGD(...) ((void)0)



/* 最大局部变量数 */
#define CODEGEN_MAXVARS		200

/* 初始label/goto数组大小 */
#define LABEL_INIT_SIZE		4


/* 前向声明 */
static void codegen_expr(CodegenState *cg, AstExpr *e, expdesc *v);
static void codegen_stmt(CodegenState *cg, AstStmt *s);
static void codegen_block(CodegenState *cg, AstBlock *blk);
static Proto *codegen_func(CodegenState *cg, AstFunc *f);
static void codegen_match_pattern(CodegenState *cg, AstMatchPat *pat, int ctrl_reg,
                                   int *next_check_jump, int *success_jump);
static void codegen_match_body(CodegenState *cg, AstStmt *s, expdesc *v);


/* ========== BinOp/UnOp映射表 ========== */

static const BinOpr binop_map[] = {
  [AST_BIN_ADD] = OPR_ADD,
  [AST_BIN_SUB] = OPR_SUB,
  [AST_BIN_MUL] = OPR_MUL,
  [AST_BIN_DIV] = OPR_DIV,
  [AST_BIN_IDIV] = OPR_IDIV,
  [AST_BIN_MOD] = OPR_MOD,
  [AST_BIN_POW] = OPR_POW,
  [AST_BIN_BAND] = OPR_BAND,
  [AST_BIN_BOR] = OPR_BOR,
  [AST_BIN_BXOR] = OPR_BXOR,
  [AST_BIN_SHL] = OPR_SHL,
  [AST_BIN_SHR] = OPR_SHR,
  [AST_BIN_CONCAT] = OPR_CONCAT,
  [AST_BIN_PIPE] = OPR_PIPE,
  [AST_BIN_REVPIPE] = OPR_PIPE,
  [AST_BIN_SAFEPIPE] = OPR_PIPE,
  [AST_BIN_EQ] = OPR_EQ,
  [AST_BIN_NE] = OPR_NE,
  [AST_BIN_LT] = OPR_LT,
  [AST_BIN_LE] = OPR_LE,
  [AST_BIN_GT] = OPR_GT,
  [AST_BIN_GE] = OPR_GE,
  [AST_BIN_SPACESHIP] = OPR_SPACESHIP,
  [AST_BIN_IS] = OPR_IS,
  [AST_BIN_IN] = OPR_IN,
  [AST_BIN_AND] = OPR_AND,
  [AST_BIN_OR] = OPR_OR,
  [AST_BIN_NULLCOAL] = OPR_NULLCOAL,
  [AST_BIN_CASE] = OPR_EQ,
  [AST_BIN_INFIX] = OPR_PIPE,
  [AST_BIN_MERGE] = OPR_MERGE,
};

static const UnOpr unop_map[] = {
  [AST_UN_MINUS] = OPR_MINUS,
  [AST_UN_BNOT] = OPR_BNOT,
  [AST_UN_NOT] = OPR_NOT,
  [AST_UN_LEN] = OPR_LEN,
  [AST_UN_AWAIT] = OPR_AWAIT,
};


/* ========== 静态辅助函数 ========== */

/*
** 初始化expdesc
*/
static void init_exp(expdesc *e, expkind k, int i) {
  e->f = e->t = NO_JUMP;
  e->k = k;
  e->u.info = i;
  e->nodiscard = 0;
  e->is_pipe_self = 0;
}


/*
** 初始化代码生成状态
** 参数：
**   cg - CodegenState指针
**   L - Lua状态机
**   pool - AST内存池
**   dyd - 动态数据
*/
static void codegen_init(CodegenState *cg, lua_State *L, AstPool *pool, Dyndata *dyd) {
  memset(cg, 0, sizeof(*cg));
  cg->L = L;
  cg->pool = pool;
  cg->dyd = dyd;
  cg->nerr = 0;
  /* loop_depth 已由 memset 初始化为 0 */
  cg->labels.names = NULL;
  cg->labels.pcs = NULL;
  cg->labels.n = 0;
  cg->labels.size = 0;
  cg->gotos.names = NULL;
  cg->gotos.pcs = NULL;
  cg->gotos.n = 0;
  cg->gotos.size = 0;

  /* 初始化最小LexState */
  memset(&cg->ls, 0, sizeof(cg->ls));
  cg->ls.L = L;
  cg->ls.dyd = dyd;
  cg->ls.h = luaH_new(L);
  sethvalue2s(L, L->top.p, cg->ls.h);
  luaD_inctop(L);
  cg->ls.envn = luaS_new(L, LUA_ENV);
  cg->ls.linenumber = 1;
  cg->ls.lastline = 1;
}


/*
** 错误报告
** 参数：
**   cg - CodegenState指针
**   fmt - 格式化字符串
*/
static void cg_error(CodegenState *cg, const char *fmt, ...) {
  va_list argp;
  const char *msg;
  cg->nerr++;
  va_start(argp, fmt);
  msg = luaO_pushvfstring(cg->L, fmt, argp);
  va_end(argp);
  luaO_pushfstring(cg->L, "%s (codegen)", msg);
  /* 使用luaD_throw抛出错误 */
  luaD_throw(cg->L, LUA_ERRSYNTAX);
}


/*
** 获取当前FuncState*（通过cg->ls.fs）
*/
static FuncState *cg_fs(CodegenState *cg) {
  return cg->ls.fs;
}


/*
** 在当前函数actvar中查找局部变量，返回vidx，-1表示未找到
** 参数：
**   cg - CodegenState指针
**   name - 变量名
** 返回值：变量索引（vidx），未找到返回-1
*/
static int find_local(CodegenState *cg, TString *name) {
  FuncState *fs = cg_fs(cg);
  Dyndata *dyd = cg->dyd;
  int i;
  for (i = cast_int(fs->nactvar) - 1; i >= 0; i--) {
    Vardesc *vd = &dyd->actvar.arr[fs->firstlocal + i];
    if (eqstr(name, vd->vd.name)) {
      return i;
    }
  }
  return -1;
}


/*
** 查找upvalue，返回索引，-1表示未找到
** 参数：
**   cg - CodegenState指针
**   name - upvalue名
** 返回值：upvalue索引，未找到返回-1
*/
static int find_upval(CodegenState *cg, TString *name) {
  FuncState *fs = cg_fs(cg);
  int i;
  Upvaldesc *up = fs->f->upvalues;
  for (i = 0; i < fs->nups; i++) {
    if (eqstr(up[i].name, name)) return i;
  }
  return -1;
}


/*
** 添加局部变量（在actvar数组末尾添加，但不激活）
** 参数：
**   cg - CodegenState指针
**   name - 变量名
**   attr - 属性（VDKREG/RDKCONST等）
** 返回值：新变量的vidx
*/
static int add_local(CodegenState *cg, TString *name, int attr) {
  FuncState *fs = cg_fs(cg);
  lua_State *L = cg->L;
  Dyndata *dyd = cg->dyd;
  Vardesc *var;
  /* 检查变量数限制 */
  if (dyd->actvar.n + 1 - fs->firstlocal > CODEGEN_MAXVARS) {
    cg_error(cg, "too many local variables (limit is %d)", CODEGEN_MAXVARS);
  }
  luaM_growvector(L, dyd->actvar.arr, dyd->actvar.n + 1,
                  dyd->actvar.size, Vardesc, USHRT_MAX, "local variables");
  var = &dyd->actvar.arr[dyd->actvar.n++];
  memset(var, 0, sizeof(*var));
  var->vd.kind = attr;
  var->vd.name = name;
  var->vd.used = 0;
  var->vd.hint = NULL;
  var->vd.nodiscard = 0;
  return dyd->actvar.n - 1 - fs->firstlocal;
}


/*
** 注册局部变量到Proto的locvars数组（调试信息）
** 参数：
**   cg - CodegenState指针
**   name - 变量名
*/
static void register_localvar(CodegenState *cg, TString *name) {
  FuncState *fs = cg_fs(cg);
  Proto *f = fs->f;
  lua_State *L = cg->L;
  int oldsize = f->sizelocvars;
  luaM_growvector(L, f->locvars, fs->ndebugvars, f->sizelocvars,
                  LocVar, SHRT_MAX, "local variables");
  while (oldsize < f->sizelocvars)
    f->locvars[oldsize++].varname = NULL;
  f->locvars[fs->ndebugvars].varname = name;
  f->locvars[fs->ndebugvars].startpc = fs->pc;
  luaC_objbarrier(L, f, name);
  fs->ndebugvars++;
}


/*
** 激活最近nvars个局部变量（设置ridx和pidx）
** 参数：
**   cg - CodegenState指针
**   nvars - 要激活的变量数
*/
static void activate_locals(CodegenState *cg, int nvars) {
  FuncState *fs = cg_fs(cg);
  Dyndata *dyd = cg->dyd;
  int reg = luaY_nvarstack(fs);
  int i;
  for (i = 0; i < nvars; i++) {
    int vidx = fs->nactvar++;
    Vardesc *var = &dyd->actvar.arr[fs->firstlocal + vidx];
    var->vd.ridx = reg++;
    var->vd.pidx = fs->ndebugvars;
    register_localvar(cg, var->vd.name);
  }
  if (fs->freereg < reg)
    fs->freereg = reg;
}


/*
** 登记标签位置
** 参数：
**   cg - CodegenState指针
**   name - 标签名
**   pc - 标签pc位置
*/
static void register_label(CodegenState *cg, TString *name, int pc) {
  lua_State *L = cg->L;
  if (cg->labels.n >= cg->labels.size) {
    if (cg->labels.size == 0) cg->labels.size = LABEL_INIT_SIZE;
    else cg->labels.size *= 2;
    cg->labels.names = luaM_reallocvector(L, cg->labels.names,
                          cg->labels.n, cg->labels.size, TString*);
    cg->labels.pcs = luaM_reallocvector(L, cg->labels.pcs,
                         cg->labels.n, cg->labels.size, int);
  }
  cg->labels.names[cg->labels.n] = name;
  cg->labels.pcs[cg->labels.n] = pc;
  cg->labels.n++;
}


/*
** 添加goto
** 参数：
**   cg - CodegenState指针
**   name - 目标标签名
**   pc - goto指令的pc位置（用于patch）
*/
static void add_goto(CodegenState *cg, TString *name, int pc) {
  lua_State *L = cg->L;
  if (cg->gotos.n >= cg->gotos.size) {
    if (cg->gotos.size == 0) cg->gotos.size = LABEL_INIT_SIZE;
    else cg->gotos.size *= 2;
    cg->gotos.names = luaM_reallocvector(L, cg->gotos.names,
                         cg->gotos.n, cg->gotos.size, TString*);
    cg->gotos.pcs = luaM_reallocvector(L, cg->gotos.pcs,
                        cg->gotos.n, cg->gotos.size, int);
  }
  cg->gotos.names[cg->gotos.n] = name;
  cg->gotos.pcs[cg->gotos.n] = pc;
  cg->gotos.n++;
}


/*
** 查找标签pc
** 参数：
**   cg - CodegenState指针
**   name - 标签名
** 返回值：标签pc位置，-1表示未找到
*/
static int find_label(CodegenState *cg, TString *name) {
  int i;
  for (i = 0; i < cg->labels.n; i++) {
    if (eqstr(cg->labels.names[i], name)) {
      return cg->labels.pcs[i];
    }
  }
  return -1;
}


/*
** 第二遍patch所有goto到标签
** 参数：
**   cg - CodegenState指针
*/
static void patch_gotos(CodegenState *cg) {
  FuncState *fs = cg_fs(cg);
  int i;
  for (i = 0; i < cg->gotos.n; i++) {
    int pc = find_label(cg, cg->gotos.names[i]);
    if (pc < 0) {
      cg_error(cg, "no visible label '%s' for <goto>", getstr(cg->gotos.names[i]));
    }
    luaK_patchlist(fs, cg->gotos.pcs[i], pc);
  }
}


/*
** 释放label/goto数组
*/
static void free_label_goto(CodegenState *cg) {
  lua_State *L = cg->L;
  if (cg->labels.names) {
    luaM_freearray(L, cg->labels.names, cg->labels.size);
    cg->labels.names = NULL;
  }
  if (cg->labels.pcs) {
    luaM_freearray(L, cg->labels.pcs, cg->labels.size);
    cg->labels.pcs = NULL;
  }
  if (cg->gotos.names) {
    luaM_freearray(L, cg->gotos.names, cg->gotos.size);
    cg->gotos.names = NULL;
  }
  if (cg->gotos.pcs) {
    luaM_freearray(L, cg->gotos.pcs, cg->gotos.size);
    cg->gotos.pcs = NULL;
  }
  cg->labels.n = cg->labels.size = 0;
  cg->gotos.n = cg->gotos.size = 0;
}


/*
** 从TString*创建expdesc（字符串常量）
*/
static void cg_codestring(expdesc *e, TString *s) {
  e->f = e->t = NO_JUMP;
  e->k = VKSTR;
  e->u.strval = s;
}


/*
** 初始化局部变量expdesc
*/
static void cg_init_local(CodegenState *cg, expdesc *e, int vidx) {
  FuncState *fs = cg_fs(cg);
  Dyndata *dyd = cg->dyd;
  Vardesc *vd;
  e->f = e->t = NO_JUMP;
  e->k = VLOCAL;
  e->u.var.vidx = vidx;
  vd = &dyd->actvar.arr[fs->firstlocal + vidx];
  e->u.var.ridx = vd->vd.ridx;
  e->nodiscard = 0;
}


/*
** 修复for跳转指令的Bx参数
** 参数：
**   fs - FuncState指针
**   pc - 要修复的指令pc
**   dest - 目标pc
**   back - 是否为回跳（1=回跳，0=前跳）
*/
static void cg_fixforjump(FuncState *fs, int pc, int dest, int back) {
  Instruction *jmp = &fs->f->code[pc];
  int offset = dest - (pc + 1);
  if (back)
    offset = -offset;
  SETARG_Bx(*jmp, offset);
}


/*
** 标记当前块的最后一个变量需要关闭（to-be-closed）
** 参数：
**   fs - FuncState指针
*/
static void cg_marktobeclosed(FuncState *fs) {
  BlockCnt *bl = fs->bl;
  bl->upval = 1;
  bl->insidetbc = 1;
  fs->needclose = 1;
}


/*
** 标记某个层级的局部变量将被作为upvalue引用（参考原markupval）
** 找到该变量所在的block并标记upval，同时设置fs->needclose
** 参数：
**   fs - FuncState指针
**   level - 变量的vidx
*/
static void cg_markupval(FuncState *fs, int level) {
  BlockCnt *bl = fs->bl;
  while (bl->nactvar > level)
    bl = bl->previous;
  bl->upval = 1;
  fs->needclose = 1;
}


/*
** 调整赋值结果数量（参考原adjust_assign）
** 参数：
**   fs - FuncState指针
**   nvars - 需要的变量数量
**   nexps - 已提供的表达式数量
**   e - 最后一个表达式（当nexps>0时有效）
*/
static void cg_adjust_assign(FuncState *fs, int nvars, int nexps, expdesc *e) {
  int needed = nvars - nexps;
  luaK_checkstack(fs, needed);
  if (nexps > 0 && hasmultret(e->k)) {
    int extra = needed + 1;
    if (extra < 0) extra = 0;
    luaK_setreturns(fs, e, extra);
  } else {
    if (nexps > 0 && e->k != VVOID)
      luaK_exp2nextreg(fs, e);
    if (needed > 0)
      luaK_nil(fs, fs->freereg, needed);
  }
  if (needed > 0)
    luaK_reserveregs(fs, needed);
  else
    fs->freereg = cast_byte(fs->freereg + needed);
}


/*
** 分配并创建新upvalue
** 参数：
**   cg - CodegenState指针
**   name - upvalue名
**   v - 父函数中的变量描述（VLOCAL或VUPVAL）
** 返回值：新upvalue的索引
*/
static int cg_newupvalue(CodegenState *cg, TString *name, expdesc *v) {
  FuncState *fs = cg_fs(cg);
  Proto *f = fs->f;
  lua_State *L = cg->L;
  Upvaldesc *up;
  int oldsize;
  oldsize = f->sizeupvalues;
  luaM_growvector(L, f->upvalues, fs->nups + 1, f->sizeupvalues,
                  Upvaldesc, MAXUPVAL, "upvalues");
  while (oldsize < f->sizeupvalues)
    f->upvalues[oldsize++].name = NULL;
  up = &f->upvalues[fs->nups++];
  if (v->k == VLOCAL) {
    up->instack = 1;
    up->idx = v->u.var.ridx;
    up->kind = VDKREG;
  } else {
    up->instack = 0;
    up->idx = cast_byte(v->u.info);
    up->kind = fs->prev->f->upvalues[v->u.info].kind;
  }
  up->name = name;
  luaC_objbarrier(L, f, name);
  return fs->nups - 1;
}


/*
** 递归查找变量（参考原singlevaraux）
** 参数：
**   cg - CodegenState指针
**   fs - 当前查找的FuncState
**   name - 变量名
**   var - 输出expdesc
**   base - 是否为原始调用层（1=是）
*/
static void cg_singlevaraux(CodegenState *cg, FuncState *fs, TString *name,
                            expdesc *var, int base) {
  if (fs == NULL) {
    init_exp(var, VVOID, 0);
    return;
  }
  /* 在当前函数查找局部变量 */
  {
    Dyndata *dyd = cg->dyd;
    int i;
    for (i = cast_int(fs->nactvar) - 1; i >= 0; i--) {
      Vardesc *vd = &dyd->actvar.arr[fs->firstlocal + i];
      if (eqstr(name, vd->vd.name)) {
        init_exp(var, VLOCAL, 0);
        var->u.var.vidx = i;
        var->u.var.ridx = vd->vd.ridx;
        var->nodiscard = vd->vd.nodiscard;  /* 传播 nodiscard 标志 */
        if (!base && vd->vd.kind != RDKCTC)
          cg_markupval(fs, i);
        return;
      }
    }
  }
  /* 查找当前函数已有upvalue */
  {
    int i;
    Upvaldesc *up = fs->f->upvalues;
    for (i = 0; i < fs->nups; i++) {
      if (eqstr(up[i].name, name)) {
        init_exp(var, VUPVAL, i);
        return;
      }
    }
  }
  /* 递归到父函数查找 */
  cg_singlevaraux(cg, fs->prev, name, var, 0);
  if (var->k == VLOCAL || var->k == VUPVAL) {
    /* 在当前函数创建新upvalue */
    int saved_nodiscard = var->nodiscard;  /* 保存 nodiscard 标志 */
    FuncState *saved_fs = cg->ls.fs;
    cg->ls.fs = fs;
    int idx = cg_newupvalue(cg, name, var);
    cg->ls.fs = saved_fs;
    init_exp(var, VUPVAL, idx);
    var->nodiscard = saved_nodiscard;  /* 恢复 nodiscard 标志 */
  }
}


/*
** 确保_ENV作为upvalue存在并返回其索引
** 参数：
**   cg - CodegenState指针
** 返回值：_ENV upvalue的索引
*/
static int cg_get_env_upval(CodegenState *cg) {
  FuncState *fs = cg_fs(cg);
  TString *envname = cg->ls.envn;
  expdesc env;
  int idx;
  /* 先查找现有upvalue */
  idx = find_upval(cg, envname);
  if (idx >= 0) return idx;
  /* 递归从父函数捕获_ENV */
  cg_singlevaraux(cg, fs->prev, envname, &env, 1);
  if (env.k == VLOCAL || env.k == VUPVAL) {
    idx = cg_newupvalue(cg, envname, &env);
    return idx;
  }
  /* main函数应该已有_ENV */
  return 0;
}


/*
** 单变量引用解析：局部变量 -> 当前函数upvalue -> 递归父函数 -> 全局
*/
static void cg_singlevar(CodegenState *cg, TString *name, expdesc *var) {
  FuncState *fs = cg_fs(cg);
  cg_singlevaraux(cg, fs, name, var, 1);
  if (var->k == VVOID) {
    /* 全局变量：通过_ENV[name]访问 */
    expdesc env, key;
    int envidx = cg_get_env_upval(cg);
    init_exp(&env, VUPVAL, envidx);
    cg_codestring(&key, name);
    luaK_indexed(fs, &env, &key);
    *var = env;
  }
}


/* ========== 表达式代码生成 ========== */

/*
** 将AstExpr编译为expdesc
** 参数：
**   cg - CodegenState指针
**   e - AST表达式节点
**   v - 输出expdesc
*/
static void codegen_expr(CodegenState *cg, AstExpr *e, expdesc *v) {
  FuncState *fs = cg_fs(cg);
  int line = e->node.line;
  cg->ls.lastline = line;  /* 同步AST节点行号到codegen，确保运行时错误报告正确行号 */

  switch (e->kind) {
    case AST_EXPR_NIL: {
      init_exp(v, VNIL, 0);
      break;
    }
    case AST_EXPR_TRUE: {
      init_exp(v, VTRUE, 0);
      break;
    }
    case AST_EXPR_FALSE: {
      init_exp(v, VFALSE, 0);
      break;
    }
    case AST_EXPR_INT: {
      v->f = v->t = NO_JUMP;
      v->k = VKINT;
      v->u.ival = e->u.ival;
      break;
    }
    case AST_EXPR_FLT: {
      v->f = v->t = NO_JUMP;
      v->k = VKFLT;
      v->u.nval = e->u.nval;
      break;
    }
    case AST_EXPR_STRING:
    case AST_EXPR_INTERPSTRING: {
      /* 字符串插值处理: $"Hello, {name}" -> 生成多个片段然后用 string.concat 连接 */
      TString *interp_str = e->u.strval;
      const char *str = getstr(interp_str);
      size_t len = tsslen(interp_str);
      FuncState *fs = cg_fs(cg);
      lua_State *L = cg->L;

      /* 检查是否有插值标记 */
      int has_interpolation = 0;
      size_t check_i;
      for (check_i = 0; check_i < len; check_i++) {
        if (str[check_i] == '$' && check_i + 1 < len && str[check_i + 1] == '{') {
          has_interpolation = 1;
          break;
        }
      }

      if (!has_interpolation) {
        /* 普通字符串，直接返回 */
        cg_codestring(v, interp_str);
        break;
      }

      /* 有插值，收集所有片段到连续寄存器 */
      int base_reg = fs->freereg;
      int part_count = 0;
      size_t i = 0;
      size_t last_end = 0;

      #define MAX_INTERP_VARS 32

      while (i < len) {
        if (str[i] == '$' && i + 1 < len && str[i + 1] == '{') {
          /* 处理 ${...} 前面的字符串部分 */
          if (i > last_end) {
            TString *part_str = luaS_newlstr(L, str + last_end, i - last_end);
            cg_codestring(v, part_str);
            luaK_exp2nextreg(fs, v);
            part_count++;
          }

          i += 2;  /* 跳过 ${ */

          /* 检查是否是表达式模式 ${[expr]} */
          int is_expr_mode = (i < len && str[i] == '[');

          if (is_expr_mode) {
            i++;  /* 跳过 [ */
            size_t expr_start = i;
            int depth = 1;
            int brace_depth = 0;

            /* 找到匹配的 ]} */
            while (i < len && depth > 0) {
              if (str[i] == '[') depth++;
              else if (str[i] == ']') {
                depth--;
                if (depth == 0 && brace_depth == 0) break;
              }
              else if (str[i] == '{') brace_depth++;
              else if (str[i] == '}') brace_depth--;
              i++;
            }

            size_t expr_len = i - expr_start;
            i++;  /* 跳过 ] */
            if (i < len && str[i] == '}') i++;  /* 跳过 } */
            last_end = i;

            if (expr_len > 0) {
              /* 检查是否是简单标识符 */
              int is_simple_id = 1;
              size_t check_j;
              for (check_j = 0; check_j < expr_len; check_j++) {
                char c = str[expr_start + check_j];
                if (check_j == 0) {
                  if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_')) {
                    is_simple_id = 0;
                    break;
                  }
                } else {
                  if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                        (c >= '0' && c <= '9') || c == '_')) {
                    is_simple_id = 0;
                    break;
                  }
                }
              }

              if (is_simple_id) {
                /* 简单标识符处理 */
                TString *varname = luaS_newlstr(L, str + expr_start, expr_len);
                expdesc var_exp;

                int varkind = find_local(cg, varname);
                if (varkind >= 0) {
                  cg_init_local(cg, &var_exp, varkind);
                } else {
                  int uv = find_upval(cg, varname);
                  if (uv >= 0) {
                    init_exp(&var_exp, VUPVAL, uv);
                  } else {
                    /* 全局变量 */
                    cg_singlevaraux(cg, cg_fs(cg), luaS_newliteral(L, "_ENV"), &var_exp, 1);
                    expdesc key;
                    cg_codestring(&key, varname);
                    luaK_indexed(fs, &var_exp, &key);
                  }
                }

                /* 调用 tostring */
                expdesc tostring_func;
                cg_singlevaraux(cg, cg_fs(cg), luaS_newliteral(L, "tostring"), &tostring_func, 1);
                if (tostring_func.k == VVOID) {
                  expdesc env_v;
                  cg_singlevaraux(cg, cg_fs(cg), luaS_newliteral(L, "_ENV"), &env_v, 1);
                  expdesc key;
                  cg_codestring(&key, luaS_newliteral(L, "tostring"));
                  luaK_indexed(fs, &env_v, &key);
                  tostring_func = env_v;
                }

                luaK_exp2nextreg(fs, &tostring_func);
                int call_reg = fs->freereg - 1;
                luaK_exp2nextreg(fs, &var_exp);
                luaK_codeABC(fs, OP_CALL, call_reg, 2, 2);
                fs->freereg = call_reg + 1;
                part_count++;
              }
              else {
                /* 复杂表达式处理 - 在编译期预编译闭包，避免运行时调用 load() */
                /* 收集表达式中的标识符 */
                TString *used_vars[MAX_INTERP_VARS];
                int nused = 0;

                /* 扫描表达式，提取所有标识符 */
                size_t scan_i = 0;
                while (scan_i < expr_len && nused < MAX_INTERP_VARS) {
                  char c = str[expr_start + scan_i];
                  /* 跳过字符串字面量 */
                  if (c == '"' || c == '\'') {
                    char quote = c;
                    scan_i++;
                    while (scan_i < expr_len && str[expr_start + scan_i] != quote) {
                      if (str[expr_start + scan_i] == '\\') scan_i++;
                      scan_i++;
                    }
                    scan_i++;
                    continue;
                  }
                  /* 检查是否是标识符开始 */
                  if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_') {
                    size_t id_start = scan_i;
                    while (scan_i < expr_len) {
                      c = str[expr_start + scan_i];
                      if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                            (c >= '0' && c <= '9') || c == '_')) break;
                      scan_i++;
                    }
                    size_t id_len = scan_i - id_start;

                    /* 跳过关键字 */
                    int is_keyword = 0;
                    const char *id = str + expr_start + id_start;
                    if (id_len == 3 && (strncmp(id, "and", 3) == 0 ||
                                        strncmp(id, "for", 3) == 0 ||
                                        strncmp(id, "not", 3) == 0 ||
                                        strncmp(id, "nil", 3) == 0 ||
                                        strncmp(id, "end", 3) == 0)) is_keyword = 1;
                    else if (id_len == 2 && (strncmp(id, "do", 2) == 0 ||
                                             strncmp(id, "if", 2) == 0 ||
                                             strncmp(id, "in", 2) == 0 ||
                                             strncmp(id, "or", 2) == 0)) is_keyword = 1;
                    if (!is_keyword) {
                      TString *varname = luaS_newlstr(L, id, id_len);
                      int idx = find_local(cg, varname);
                      if (idx >= 0) {
                        int already_added = 0;
                        int k;
                        for (k = 0; k < nused; k++) {
                          if (eqstr(used_vars[k], varname)) {
                            already_added = 1;
                            break;
                          }
                        }
                        if (!already_added && nused < MAX_INTERP_VARS) {
                          used_vars[nused++] = varname;
                        }
                      }
                    }
                  }
                  else {
                    scan_i++;
                  }
                }

                /* 生成代码字符串: return function(...) return tostring(expr) end */
                size_t total_len = 16;  /* "return function(" */
                for (int k = 0; k < nused; k++) {
                  total_len += tsslen(used_vars[k]);
                  if (k < nused - 1) total_len += 2;
                }
                total_len += 18;  /* ") return tostring(" */
                total_len += expr_len;
                total_len += 5;   /* ") end" */

                char *code_str = luaM_newblock(L, total_len + 1);
                size_t pos = 0;
                memcpy(code_str + pos, "return function(", 16); pos += 16;
                for (int k = 0; k < nused; k++) {
                  size_t l = tsslen(used_vars[k]);
                  memcpy(code_str + pos, getstr(used_vars[k]), l); pos += l;
                  if (k < nused - 1) {
                    memcpy(code_str + pos, ", ", 2); pos += 2;
                  }
                }
                memcpy(code_str + pos, ") return tostring(", 18); pos += 18;
                memcpy(code_str + pos, str + expr_start, expr_len); pos += expr_len;
                memcpy(code_str + pos, ") end", 5); pos += 5;
                code_str[pos] = '\0';

                /*
                ** 在编译期使用 luaL_loadbuffer 编译代码字符串，
                ** 然后调用得到闭包，存入常量表中。
                ** 运行时直接加载常量并调用，避免依赖 load() 全局函数。
                */
                int status = luaL_loadbuffer(L, code_str, pos, "=interp");
                if (status != LUA_OK) {
                  const char *err = lua_tostring(L, -1);
                  cg_error(cg, "interpolation: failed to compile expression '%s': %s",
                           code_str, err ? err : "unknown error");
                  lua_pop(L, 1);
                  luaM_freearray(L, code_str, total_len + 1);
                  break;
                }

                /* 执行 chunk 得到闭包函数 */
                status = lua_pcall(L, 0, 1, 0);
                if (status != LUA_OK) {
                  const char *err = lua_tostring(L, -1);
                  cg_error(cg, "interpolation: failed to evaluate expression '%s': %s",
                           code_str, err ? err : "unknown error");
                  lua_pop(L, 1);
                  luaM_freearray(L, code_str, total_len + 1);
                  break;
                }

                /*
                ** 栈顶是闭包函数 function(var1, var2, ...) return tostring(expr) end
                ** 将其存入常量表，运行时直接加载并调用
                */
                TValue closure_val;
                setobj(L, &closure_val, s2v(L->top.p - 1));
                int closure_kidx = luaK_closureK(fs, &closure_val);
                L->top.p--;  /* 弹出闭包 */

                luaM_freearray(L, code_str, total_len + 1);

                /* 运行时：加载预编译闭包常量，推入参数，调用 */
                expdesc closure_exp;
                init_exp(&closure_exp, VK, closure_kidx);
                luaK_exp2nextreg(fs, &closure_exp);
                int closure_reg = fs->freereg - 1;

                for (int k = 0; k < nused; k++) {
                  expdesc var_exp;
                  int vk = find_local(cg, used_vars[k]);
                  if (vk >= 0) {
                    cg_init_local(cg, &var_exp, vk);
                  } else {
                    int uv = find_upval(cg, used_vars[k]);
                    if (uv >= 0) {
                      init_exp(&var_exp, VUPVAL, uv);
                    } else {
                      expdesc key;
                      cg_singlevaraux(cg, cg_fs(cg), luaS_newliteral(L, "_ENV"), &var_exp, 1);
                      cg_codestring(&key, used_vars[k]);
                      luaK_indexed(fs, &var_exp, &key);
                    }
                  }
                  luaK_exp2nextreg(fs, &var_exp);
                }
                luaK_codeABC(fs, OP_CALL, closure_reg, nused + 1, 2);
                fs->freereg = closure_reg + 1;

                /* 移动结果 */
                if (closure_reg != base_reg + part_count) {
                  luaK_codeABC(fs, OP_MOVE, base_reg + part_count, closure_reg, 0);
                  fs->freereg = base_reg + part_count + 1;
                }

                part_count++;
              }
            }
          }
          else {
            /* 简单变量模式 ${name} */
            size_t expr_start = i;
            int depth = 1;
            while (i < len && depth > 0) {
              if (str[i] == '{') depth++;
              else if (str[i] == '}') depth--;
              if (depth > 0) i++;
            }
            size_t expr_len = i - expr_start;
            if (i < len && str[i] == '}') i++;
            last_end = i;

            if (expr_len > 0) {
              TString *varname = luaS_newlstr(L, str + expr_start, expr_len);
              expdesc var_exp;

              int varkind = find_local(cg, varname);
              if (varkind >= 0) {
                cg_init_local(cg, &var_exp, varkind);
              } else {
                int uv = find_upval(cg, varname);
                if (uv >= 0) {
                  init_exp(&var_exp, VUPVAL, uv);
                } else {
                  /* 全局变量 */
                  expdesc key;
                  cg_singlevaraux(cg, cg_fs(cg), luaS_newliteral(L, "_ENV"), &var_exp, 1);
                  cg_codestring(&key, varname);
                  luaK_indexed(fs, &var_exp, &key);
                }
              }

              /* 调用 tostring */
              expdesc tostring_func;
              cg_singlevaraux(cg, cg_fs(cg), luaS_newliteral(L, "tostring"), &tostring_func, 1);
              if (tostring_func.k == VVOID) {
                expdesc env_v;
                cg_singlevaraux(cg, cg_fs(cg), luaS_newliteral(L, "_ENV"), &env_v, 1);
                expdesc key;
                cg_codestring(&key, luaS_newliteral(L, "tostring"));
                luaK_indexed(fs, &env_v, &key);
                tostring_func = env_v;
              }

              luaK_exp2nextreg(fs, &tostring_func);
              int call_reg = fs->freereg - 1;
              luaK_exp2nextreg(fs, &var_exp);
              luaK_codeABC(fs, OP_CALL, call_reg, 2, 2);
              fs->freereg = call_reg + 1;
              part_count++;
            }
          }
        }
        else {
          i++;
        }
      }

      /* 处理最后一段字符串 */
      if (len > last_end) {
        TString *part_str = luaS_newlstr(L, str + last_end, len - last_end);
        cg_codestring(v, part_str);
        luaK_exp2nextreg(fs, v);
        part_count++;
      }

      if (part_count == 1) {
        /* 只有一个片段，直接返回 */
        init_exp(v, VNONRELOC, base_reg);
      } else {
        /* 使用 OP_CONCAT 连接所有片段 */
        luaK_codeABC(fs, OP_CONCAT, base_reg, part_count, 0);
        fs->freereg = base_reg + 1;
        init_exp(v, VNONRELOC, base_reg);
        v->t = NO_JUMP;
        v->f = NO_JUMP;
      }
      break;
    }
#undef MAX_INTERP_VARS
    case AST_EXPR_REGEX: {
      /* 正则字面量 /pattern/flags：发射 OP_REGEX 指令 */
      TString *ts = e->u.strval;
      int kidx = luaK_stringK(fs, ts);
      init_exp(v, VRELOC, luaK_codeABx(fs, OP_REGEX, 0, kidx));
      break;
    }
    case AST_EXPR_VARARG: {
      int pc;
      if (!fs->f->is_vararg) {
        cg_error(cg, "cannot use '...' outside a vararg function");
      }
      pc = luaK_codeABC(fs, OP_VARARG, 0, 0, 1);
      init_exp(v, VVARARG, pc);
      break;
    }
    case AST_EXPR_IDENT: {
      /* 检查是否是 $keyword 调用：在 keyword 编译时注册表中查找同名 Proto */
      {
        global_State *g = G(cg->L);
        int i;
        for (i = 0; i < g->kwreg_count; i++) {
          if (g->keyword_registry[i].name == e->u.strval) {
            /* 找到 keyword，直接生成 OP_CLOSURE */
            Proto *kwproto = g->keyword_registry[i].p;
            Proto *f = fs->f;
            if (fs->np >= f->sizep) {
              int oldsize = f->sizep;
              luaM_growvector(cg->L, f->p, fs->np, f->sizep, Proto *, MAXARG_Bx, "functions");
              while (oldsize < f->sizep)
                f->p[oldsize++] = NULL;
            }
            int proto_idx = fs->np++;
            f->p[proto_idx] = kwproto;
            luaC_objbarrier(cg->L, f, kwproto);
            int reg = fs->freereg;
            luaK_codeABx(fs, OP_CLOSURE, reg, proto_idx);
            init_exp(v, VNONRELOC, reg);
            fs->freereg = reg + 1;
            return;  /* 不再走正常变量查找 */
          }
        }
      }
      /* 非 keyword，走正常变量查找路径 */
      cg_singlevar(cg, e->u.strval, v);
      break;
    }
    case AST_EXPR_BINOP: {
      BinOpr op = binop_map[e->u.binop.op];

      /* 链式比较检测：a < b < c 等价于 (a < b) and (b < c)
       * 策略：3个寄存器 (R0/R1/R2)，比较结果在 R0/R1 交替，R2 存 AND 累积 */
      if ((op == OPR_LT || op == OPR_LE || op == OPR_GT || op == OPR_GE ||
           op == OPR_EQ || op == OPR_NE) &&
          e->u.binop.lhs->kind == AST_EXPR_BINOP) {
        BinOpr lhs_op = binop_map[e->u.binop.lhs->u.binop.op];
        if (lhs_op == OPR_LT || lhs_op == OPR_LE || lhs_op == OPR_GT ||
            lhs_op == OPR_GE || lhs_op == OPR_EQ || lhs_op == OPR_NE) {
          int nops = 0;
          BinOpr ops[32];
          AstExpr *operands[33];
          AstExpr *cur = e;

          while (cur->kind == AST_EXPR_BINOP && nops < 32) {
            BinOpr cur_op = binop_map[cur->u.binop.op];
            if (!(cur_op == OPR_LT || cur_op == OPR_LE || cur_op == OPR_GT ||
                  cur_op == OPR_GE || cur_op == OPR_EQ || cur_op == OPR_NE))
              break;
            ops[nops] = cur_op;
            operands[nops + 1] = cur->u.binop.rhs;
            cur = cur->u.binop.lhs;
            nops++;
          }
          operands[0] = cur;

          /* 反转 ops 和 operands：while 循环从外层向内层收集，顺序是反的 */
          if (nops >= 2) {
            int i;
            /* 反转 ops[0..nops-1] */
            for (i = 0; i < nops / 2; i++) {
              BinOpr tmp = ops[i];
              ops[i] = ops[nops - 1 - i];
              ops[nops - 1 - i] = tmp;
            }
            /* 反转 operands[1..nops] */
            for (i = 1; i <= nops / 2; i++) {
              AstExpr *tmp = operands[i];
              operands[i] = operands[nops + 1 - i];
              operands[nops + 1 - i] = tmp;
            }

            int reg0, reg1, reg2;

            /* 评估 operands[0] 到 R0, operands[1] 到 R1 */
            {
              expdesc v0, v1;
              codegen_expr(cg, operands[0], &v0);
              luaK_exp2nextreg(fs, &v0);
              reg0 = v0.u.info;
              codegen_expr(cg, operands[1], &v1);
              luaK_exp2nextreg(fs, &v1);
              reg1 = v1.u.info;
            }

            /* 第一个比较: R0 < R1 → 布尔值在 R2 */
            {
              expdesc cmp, r;
              init_exp(&cmp, VNONRELOC, reg0);
              init_exp(&r, VNONRELOC, reg1);
              luaK_infix(fs, ops[0], &cmp);
              luaK_posfix(fs, ops[0], &cmp, &r, line);
              /* cmp 现在是 VJMP，luaK_exp2reg 转换为布尔值并存入寄存器 */
              reg2 = reg1 + 1;
              luaK_exp2reg(fs, &cmp, reg2);
            }
            fs->freereg = reg2 + 1;

            /* 后续比较：R1 保存前一个操作数，R0 载入新操作数，R2 保存 AND 累积结果 */
            for (i = 1; i < nops; i++) {
              /* 载入 operands[i+1] 到 R0 */
              {
                expdesc next;
                codegen_expr(cg, operands[i + 1], &next);
                luaK_exp2reg(fs, &next, reg0);
              }
              /* 比较: R1 (前一个操作数) < R0 (新操作数) → 布尔值在 R0 */
              {
                expdesc cmp, r;
                init_exp(&cmp, VNONRELOC, reg1);
                init_exp(&r, VNONRELOC, reg0);
                luaK_infix(fs, ops[i], &cmp);
                luaK_posfix(fs, ops[i], &cmp, &r, line);
                /* 保存 operands[i+1] 到 R1 供下一次迭代使用（R0 将被布尔值覆盖） */
                luaK_codeABC(fs, OP_MOVE, reg1, reg0, 0);
                /* cmp 是 VJMP，转换为布尔值存入 reg0 */
                luaK_exp2reg(fs, &cmp, reg0);
              }
              /* AND: R2 AND R0 → 布尔值在 R2 */
              {
                expdesc cmp, r;
                init_exp(&cmp, VNONRELOC, reg2);
                init_exp(&r, VNONRELOC, reg0);
                luaK_infix(fs, OPR_AND, &cmp);
                luaK_posfix(fs, OPR_AND, &cmp, &r, line);
                /* cmp 是 VJMP，转换为布尔值存入 reg2 */
                luaK_exp2reg(fs, &cmp, reg2);
              }
              fs->freereg = reg2 + 1;
            }

            init_exp(v, VNONRELOC, reg2);
            break;
          }
        }
      }

      /* 正常二元运算 */
      expdesc rhs;
      codegen_expr(cg, e->u.binop.lhs, v);
      luaK_infix(fs, op, v);
      codegen_expr(cg, e->u.binop.rhs, &rhs);
      luaK_posfix(fs, op, v, &rhs, line);
      break;
    }
    /* 范围表达式：1..5 生成 range(1, 5) 表 */
    case AST_EXPR_RANGE: {
      /* 提取起始值：支持 AST_EXPR_INT 和 AST_EXPR_UNOP(AST_UN_MINUS, AST_EXPR_INT) */
      lua_Integer start = 0, end = 0;
      AstExpr *s = e->u.range.start;
      AstExpr *ed = e->u.range.end;
      if (s->kind == AST_EXPR_INT) {
        start = s->u.ival;
      } else if (s->kind == AST_EXPR_UNOP && s->u.unop.op == AST_UN_MINUS
                 && s->u.unop.operand->kind == AST_EXPR_INT) {
        start = -s->u.unop.operand->u.ival;
      }
      if (ed->kind == AST_EXPR_INT) {
        end = ed->u.ival;
      } else if (ed->kind == AST_EXPR_UNOP && ed->u.unop.op == AST_UN_MINUS
                 && ed->u.unop.operand->kind == AST_EXPR_INT) {
        end = -ed->u.unop.operand->u.ival;
      }
      luaK_range(fs, v, start, end, line);
      break;
    }
    case AST_EXPR_UNOP: {
      AstUnOp ast_op = e->u.unop.op;

      /* 测试操作符特殊处理：[-z/-n/-nil/-bool/-func expr] */
      if (ast_op >= AST_UN_TEST_Z && ast_op <= AST_UN_TEST_FUNC) {
        codegen_expr(cg, e->u.unop.operand, v);
        int r = luaK_exp2anyreg(fs, v);

        if (ast_op == AST_UN_TEST_Z || ast_op == AST_UN_TEST_N) {
          /* 字符串长度测试: [-z expr] → #expr == 0, [-n expr] → #expr ~= 0 */
          luaK_prefix(fs, OPR_LEN, v, line);
          luaK_exp2anyreg(fs, v);
          r = v->u.info;
          expdesc zero;
          init_exp(&zero, VKINT, 0);
          zero.u.ival = 0;
          BinOpr cmp_op = (ast_op == AST_UN_TEST_Z) ? OPR_EQ : OPR_NE;
          luaK_infix(fs, cmp_op, v);
          luaK_posfix(fs, cmp_op, v, &zero, line);
          luaK_exp2reg(fs, v, r);  /* VJMP → 布尔值寄存器 */
          fs->freereg = cast_byte(r + 1);  /* 寄存器 r 现在被占用 */
        } else {
          /* 类型测试: [-nil/-bool/-func expr] → type(expr) == "typename" */
          const char *typename_full;
          if (ast_op == AST_UN_TEST_BOOL) typename_full = "boolean";
          else if (ast_op == AST_UN_TEST_FUNC) typename_full = "function";
          else typename_full = "nil";  /* AST_UN_TEST_NIL */
          int type_k = luaK_stringK(fs, luaS_new(cg->L, typename_full));
          /* OP_IS A B C k: if (type(R[A]) == K[B]) ~= k then pc++ */
          luaK_codeABCk(fs, OP_IS, r, type_k, 0, 0);
          int jmp_false = luaK_jump(fs);
          luaK_codeABC(fs, OP_LOADTRUE, r, 0, 0);
          int jmp_end = luaK_jump(fs);
          luaK_patchtohere(fs, jmp_false);
          luaK_codeABC(fs, OP_LOADFALSE, r, 0, 0);
          luaK_patchtohere(fs, jmp_end);
          init_exp(v, VNONRELOC, r);
        }
      } else if (ast_op == AST_UN_AWAIT) {
        /* await 表达式：发射 OP_AWAIT 指令（纯语法级，不依赖 coroutine.yield）
         * OP_AWAIT A B: R[A] = await(R[B]) */
        int r = fs->freereg;  /* 结果寄存器 */
        codegen_expr(cg, e->u.unop.operand, v);
        luaK_exp2reg(fs, v, r + 1);  /* Promise 参数放到 r+1 */
        luaK_codeABC(fs, OP_AWAIT, r, r + 1, 0);
        init_exp(v, VNONRELOC, r);  /* 结果在 r */
        fs->freereg = r + 1;  /* 只有结果存活 */
        luaK_fixline(fs, line);
      } else {
        UnOpr op = unop_map[ast_op];
        codegen_expr(cg, e->u.unop.operand, v);
        luaK_prefix(fs, op, v, line);
      }
      break;
    }
    case AST_EXPR_CALL: {
      int nargs;
      int base;
      int i;
      int pc;
      expdesc func;
      int has_vararg = 0;
      codegen_expr(cg, e->u.call.callee, &func);
      luaK_exp2nextreg(fs, &func);
      base = func.u.info;
      nargs = e->u.call.nargs;
      for (i = 0; i < nargs; i++) {
        expdesc arg;
        codegen_expr(cg, e->u.call.args[i], &arg);
        /* 最后一个参数是 ... 时，保持多返回值传递 */
        if (i == nargs - 1 && hasmultret(arg.k)) {
          has_vararg = 1;
          luaK_setmultret(fs, &arg);
        } else {
          luaK_exp2nextreg(fs, &arg);
        }
      }
      if (has_vararg)
        pc = luaK_codeABC(fs, OP_CALL, base, 0, 2);
      else
        pc = luaK_codeABC(fs, OP_CALL, base, nargs + 1, 2);
      {
        int saved_nodiscard = func.nodiscard;  /* 保存 callee 的 nodiscard 标志 */
        init_exp(v, VCALL, pc);
        v->nodiscard = saved_nodiscard;  /* 恢复 nodiscard 标志 */
      }
      fs->freereg = cast_byte(base + 1);
      break;
    }
    case AST_EXPR_METHOD_CALL: {
      int base;
      int i;
      int pc;
      expdesc func;
      expdesc mkey;
      int has_vararg = 0;
      int nargs = e->u.mcall.nargs;
      codegen_expr(cg, e->u.mcall.recv, &func);
      cg_codestring(&mkey, e->u.mcall.method);
      luaK_self(fs, &func, &mkey);
      base = func.u.info;
      for (i = 0; i < nargs; i++) {
        expdesc arg;
        codegen_expr(cg, e->u.mcall.args[i], &arg);
        /* 最后一个参数是 ... 时，保持多返回值传递 */
        if (i == nargs - 1 && hasmultret(arg.k)) {
          has_vararg = 1;
          luaK_setmultret(fs, &arg);
        } else {
          luaK_exp2nextreg(fs, &arg);
        }
      }
      if (has_vararg)
        pc = luaK_codeABC(fs, OP_CALL, base, 0, 2);
      else {
        int nparams = fs->freereg - (base + 1);
        pc = luaK_codeABC(fs, OP_CALL, base, nparams + 1, 2);
      }
      init_exp(v, VCALL, pc);
      fs->freereg = cast_byte(base + 1);
      break;
    }
    case AST_EXPR_METHOD_REF: {
      /* 方法引用 obj:method（无括号）：绑定 self 到接收者
       * 用于管道运算符右侧：x |> obj:method -> obj.method(obj, x)
       * luaK_self 分配两个寄存器：func_reg(obj.method) 和 func_reg+1(obj)
       * is_pipe_self=1 告诉 luaK_pipe 管道参数放在 func_reg+2
       */
      expdesc recv, key;
      codegen_expr(cg, e->u.method_ref.recv, &recv);
      cg_codestring(&key, e->u.method_ref.method);
      luaK_self(fs, &recv, &key);
      *v = recv;
      /* 设置 is_pipe_self=1：luaK_pipe 会传递 self 和管道值两个参数
       * 与 lparser.c 的 suffixedexp 中 luaK_self 后自动设置标志一致 */
      v->is_pipe_self = 1;
      break;
    }
    case AST_EXPR_INDEX: {
      expdesc tbl, key;
      codegen_expr(cg, e->u.index.table, &tbl);
      if (e->u.index.is_opt) {
        /* 可选链 ?. 或 ?[expr]：生成 OP_TESTNIL + OP_JMP 短路逻辑
         * 当表为 nil 时跳过字段访问，保持 nil 值在寄存器中 */
        int reg, jmp_skip;
        codegen_expr(cg, e->u.index.key, &key);
        luaK_dischargevars(fs, &tbl);
        luaK_exp2nextreg(fs, &tbl);
        reg = tbl.u.info;
        /* TESTNIL reg, reg, 0, 1 — k=1 表示非nil时跳过下一条JMP */
        luaK_codeABCk(fs, OP_TESTNIL, reg, reg, 0, 1);
        jmp_skip = luaK_jump(fs);
        /* 正常索引访问 */
        luaK_indexed(fs, &tbl, &key);
        /* 根据索引类型发射对应的访问指令 */
        if (tbl.k == VINDEXSTR) {
          luaK_codeABC(fs, OP_GETFIELD, reg, reg, tbl.u.ind.idx);
        } else {
          /* VINDEXED: 键在寄存器中 */
          int rkey = tbl.u.ind.idx;
          luaK_codeABC(fs, OP_GETTABLE, reg, reg, rkey);
          /* 释放键寄存器 */
          if (rkey == fs->freereg - 1) fs->freereg--;
        }
        luaK_patchtohere(fs, jmp_skip);
        init_exp(v, VNONRELOC, reg);
      } else {
        luaK_exp2anyregup(fs, &tbl);  /* 放表表达式到寄存器或upvalue，确保luaK_indexed接收合法输入 */
        codegen_expr(cg, e->u.index.key, &key);
        luaK_indexed(fs, &tbl, &key);
        *v = tbl;
      }
      break;
    }
    case AST_EXPR_SLICE: {
      /* 切片语法: t[start:end:step]
       * 生成 OP_SLICE 指令
       * 寄存器布局: base=表, base+1=start, base+2=end, base+3=step */
      expdesc tbl, startexp, endexp, stepexp;
      int base, has_step;
      /* 将源表 codegen 后放入寄存器 */
      codegen_expr(cg, e->u.slice.table, &tbl);
      luaK_exp2nextreg(fs, &tbl);
      base = tbl.u.info;
      /* codegen start 并放入 base+1 */
      if (e->u.slice.start) {
        codegen_expr(cg, e->u.slice.start, &startexp);
        luaK_exp2nextreg(fs, &startexp);
      } else {
        init_exp(&startexp, VNIL, 0);
        luaK_exp2nextreg(fs, &startexp);
      }
      /* codegen end 并放入 base+2 */
      if (e->u.slice.end) {
        codegen_expr(cg, e->u.slice.end, &endexp);
        luaK_exp2nextreg(fs, &endexp);
      } else {
        init_exp(&endexp, VNIL, 0);
        luaK_exp2nextreg(fs, &endexp);
      }
      /* codegen step 并放入 base+3 */
      has_step = (e->u.slice.step != NULL);
      if (e->u.slice.step) {
        codegen_expr(cg, e->u.slice.step, &stepexp);
        luaK_exp2nextreg(fs, &stepexp);
      } else {
        init_exp(&stepexp, VNIL, 0);
        luaK_exp2nextreg(fs, &stepexp);
      }
      luaK_codeABC(fs, OP_SLICE, base, base, has_step);
      fs->freereg = base + 1;
      v->k = VNONRELOC;
      v->u.info = base;
      break;
    }
    case AST_EXPR_TABLE_CTOR: {
      int pc;
      int reg = fs->freereg;
      int narr = e->u.table.narr;
      int nrec = e->u.table.nrec;
      int i;
      int posidx = 0;
      int na = 0;
      LOGD("[codegen] TABLE_CTOR: reg=%d, nentries=%d, narr=%d, nrec=%d", reg, e->u.table.nentries, narr, nrec);
      /* 只记录有混合条目或 narr>0 的表 */
      if (narr > 0 && nrec > 0) {
        LOGD("[codegen] >>> MIXED TABLE: nentries=%d, narr=%d, nrec=%d", e->u.table.nentries, narr, nrec);
      }
      pc = luaK_codeABC(fs, OP_NEWTABLE, reg, 0, 0);
      luaK_code(fs, 0);  /* extra arg */
      luaK_reserveregs(fs, 1);
      init_exp(v, VNONRELOC, reg);
      for (i = 0; i < e->u.table.nentries; i++) {
        AstTableEntry *entry = &e->u.table.entries[i];
        if (entry->kind == AST_TENTRY_POS) {
          /* 最后一个位置条目是 ... 时，生成 OP_VARARG c=0 捕获所有可变参数 */
          int is_last_vararg = (entry->value->kind == AST_EXPR_VARARG &&
                                i == e->u.table.nentries - 1);
          if (is_last_vararg) {
            int pc = luaK_codeABC(fs, OP_VARARG, fs->freereg, 0, 0);
            luaK_reserveregs(fs, 1);
            posidx++;
            luaK_setlist(fs, reg, na, LUA_MULTRET);
            na--;  /* 不统计最后一个表达式（未知元素数量） */
            posidx = 0;
            break;
          }
          expdesc val;
          codegen_expr(cg, entry->value, &val);
          luaK_exp2nextreg(fs, &val);
          posidx++;
          if (posidx >= LFIELDS_PER_FLUSH || i == e->u.table.nentries - 1) {
            luaK_setlist(fs, reg, na, posidx);
            na += posidx;
            posidx = 0;
          }
        } else {
          expdesc key, val;
          codegen_expr(cg, entry->key, &key);
          codegen_expr(cg, entry->value, &val);
          luaK_exp2anyreg(fs, &val);
          expdesc tbl = *v;
          luaK_indexed(fs, &tbl, &key);
          luaK_storevar(fs, &tbl, &val);
        }
      }
      /* 循环结束后刷新未写入的数组元素（与旧解析器 lastlistfield 行为一致） */
      LOGD("[codegen] TABLE_CTOR after loop: posidx=%d, na=%d", posidx, na);
      if (posidx > 0) {
        LOGD("[codegen] TABLE_CTOR flushing: posidx=%d, na=%d, reg=%d", posidx, na, reg);
        luaK_setlist(fs, reg, na, posidx);
        na += posidx;
        posidx = 0;
      }
      LOGD("[codegen] TABLE_CTOR settablesize: reg=%d, narr=%d, nrec=%d", reg, narr, nrec);
      luaK_settablesize(fs, pc, reg, narr, nrec);
      break;
    }
    case AST_EXPR_MAP_CTOR: {
      int i;
      int map_reg = fs->freereg;
      /* 生成 OP_NEWMAP，创建新的空 map */
      luaK_codeABC(fs, OP_NEWMAP, map_reg, 0, 0);
      luaK_code(fs, 0);  /* extra arg */
      luaK_reserveregs(fs, 1);
      init_exp(v, VNONRELOC, map_reg);

      /* 逐个处理每个 entry */
      for (i = 0; i < e->u.map.nentries; i++) {
        AstMapEntry *entry = &e->u.map.entries[i];
        expdesc key, val;
        /* 生成 key 和 value 的代码 */
        codegen_expr(cg, entry->key, &key);
        codegen_expr(cg, entry->value, &val);
        /* 确保 key 和 val 在寄存器中 */
        luaK_exp2nextreg(fs, &key);
        int key_reg = key.u.info;
        luaK_exp2nextreg(fs, &val);
        int val_reg = val.u.info;
        /* 生成 OP_MAPSET: map_reg[key_reg] = val_reg */
        luaK_codeABC(fs, OP_MAPSET, map_reg, key_reg, val_reg);
        /* 释放临时寄存器，回收 key 和 val 分配的两个寄存器
         * key_reg 是分配 key 之前的 freereg，将 freereg 重置到这里才能完全回收
         */
        fs->freereg = key_reg;
      }
      break;
    }
    case AST_EXPR_FUNC_EXPR:
    case AST_EXPR_ARROW_FUNC: {
      Proto *p = codegen_func(cg, e->u.func.func);
      int bx = fs->np++;
      int oldsize;
      if (bx >= fs->f->sizep) {
        oldsize = fs->f->sizep;
        luaM_growvector(cg->L, fs->f->p, bx + 1, fs->f->sizep,
                        Proto *, MAXARG_Bx, "functions");
        while (oldsize < fs->f->sizep)
          fs->f->p[oldsize++] = NULL;
      }
      fs->f->p[bx] = p;
      luaC_objbarrier(cg->L, fs->f, p);
      init_exp(v, VRELOC, luaK_codeABx(fs, OP_CLOSURE, 0, bx));
      luaK_exp2nextreg(fs, v);
      /* async 函数表达式：发射 OP_ASYNCWRAP 包装 */
      if (e->u.func.func->is_async) {
        luaK_codeABC(fs, OP_ASYNCWRAP, 0, v->u.info, 0);
      }
      break;
    }
    case AST_EXPR_ASTPARSER: {
      /* astparser 编译期代码块：创建 C 闭包包装预编译的 Proto */
      FuncState *fs = cg_fs(cg);
      lua_State *L = cg->L;
      Proto *p = e->u.astparser.proto;
      AstChunk *chunk = e->u.astparser.chunk;

      /* 将 Proto 添加到当前函数的子函数列表 */
      int bx = fs->np++;
      int oldsize;
      if (bx >= fs->f->sizep) {
        oldsize = fs->f->sizep;
        luaM_growvector(L, fs->f->p, bx + 1, fs->f->sizep,
                        Proto *, MAXARG_Bx, "functions");
        while (oldsize < fs->f->sizep)
          fs->f->p[oldsize++] = NULL;
      }
      fs->f->p[bx] = p;
      luaC_objbarrier(L, fs->f, p);

      /* 在 Lua 栈上创建 C 闭包：upvalue 1=Proto*, upvalue 2=AstChunk* */
      lua_pushlightuserdata(L, p);
      lua_pushlightuserdata(L, chunk);
      lua_pushcclosure(L, astparser_runner, 2);

      /* 将栈顶的 C 闭包添加到常量表 */
      int kidx = luaK_closureK(fs, s2v(L->top.p - 1));
      L->top.p--;  /* 弹出 C 闭包（常量表已持有引用） */

      /* 生成 OP_LOADK 将 C 闭包加载到寄存器 */
      int pc = luaK_codeABx(fs, OP_LOADK, 0, kidx);
      init_exp(v, VRELOC, pc);
      luaK_exp2nextreg(fs, v);
      break;
    }
    case AST_EXPR_DICT_COMP:
    case AST_EXPR_LIST_COMP: {
      /* 推导式：生成子函数字节码，创建闭包并立即调用，返回结果表 */
      int line = e->node.line;
      Proto *p = codegen_func(cg, e->u.func.func);
      int bx = fs->np++;
      int oldsize;
      if (bx >= fs->f->sizep) {
        oldsize = fs->f->sizep;
        luaM_growvector(cg->L, fs->f->p, bx + 1, fs->f->sizep,
                        Proto *, MAXARG_Bx, "functions");
        while (oldsize < fs->f->sizep)
          fs->f->p[oldsize++] = NULL;
      }
      fs->f->p[bx] = p;
      luaC_objbarrier(cg->L, fs->f, p);
      init_exp(v, VRELOC, luaK_codeABx(fs, OP_CLOSURE, 0, bx));
      luaK_exp2nextreg(fs, v);
      /* 立即调用闭包：OP_CALL func_reg, 1, 2 (1个参数self, 2个返回值) */
      int func_reg = v->u.info;
      init_exp(v, VCALL, luaK_codeABC(fs, OP_CALL, func_reg, 1, 2));
      luaK_fixline(fs, line);
      fs->freereg = func_reg + 1;
      break;
    }
    case AST_EXPR_CONDEXPR: {
      /* 条件表达式: cond ? then : else
       * 需要按正确顺序生成代码: 条件 -> 跳转 -> true分支 -> 跳转 -> false分支 */
      int escape;
      int condition;
      int reg;

      /* 生成条件代码 */
      codegen_expr(cg, e->u.condexpr.e1, v);
      if (v->k == VNIL) v->k = VFALSE;  /* nil 视为 false */
      luaK_goiftrue(fs, v);
      condition = v->f;  /* 保存 false 跳转列表 */

      /* 生成 true 分支代码 */
      {
        expdesc v1;
        codegen_expr(cg, e->u.condexpr.e2, &v1);
        luaK_exp2nextreg(fs, &v1);
        reg = v1.u.info;
        escape = luaK_jump(fs);  /* 跳过 false 分支 */
      }

      luaK_patchtohere(fs, condition);  /* false 跳转目标 */

      /* 生成 false 分支代码，结果存入同一寄存器 */
      {
        expdesc v2;
        codegen_expr(cg, e->u.condexpr.e3, &v2);
        luaK_exp2reg(fs, &v2, reg);
      }

      luaK_patchtohere(fs, escape);  /* 退出跳转目标 */

      v->k = VNONRELOC;
      v->u.info = reg;
      v->t = NO_JUMP;
      v->f = NO_JUMP;
      break;
    }
    case AST_EXPR_PAREN: {
      codegen_expr(cg, e->u.paren.expr, v);
      luaK_setoneret(fs, v);
      break;
    }
    case AST_EXPR_SWITCH_EXPR: {
      /* switch 表达式代码生成
       * 语法: switch expr {do|then|:|{ } case val1 => expr1, case val2 => expr2, ... }
       * 将匹配到的分支表达式结果存入结果寄存器
       */
      FuncState *fs = cg_fs(cg);
      AstExpr *control = e->u.switchx.cond;
      int narms = e->u.switchx.narms;
      AstCaseArm *arms = e->u.switchx.arms;
      AstExpr *def = e->u.switchx.def;

      if (control == NULL || (narms == 0 && def == NULL)) {
        init_exp(v, VNIL, 0);
        break;
      }

      /* 生成控制表达式到寄存器 */
      expdesc ctrl;
      codegen_expr(cg, control, &ctrl);
      luaK_exp2nextreg(fs, &ctrl);
      int ctrl_reg = ctrl.u.info;

      /* 分配结果寄存器（复用控制值寄存器，控制值在比较后不再需要） */
      int result_reg = ctrl_reg;

      /* 跳转到第一个 case 比较 */
      int jump_to_check = luaK_jump(fs);
      int finish_jump = NO_JUMP;

      int i;
      for (i = 0; i < narms; i++) {
        AstCaseArm *arm = &arms[i];
        int next_check_jump = NO_JUMP;
        int success_jump = NO_JUMP;

        /* 修补上一个检查跳转 */
        luaK_patchtohere(fs, jump_to_check);

        /* 对每个模式值生成相等比较: 控制值 == arm 模式值 */
        for (int p = 0; p < arm->npatterns; p++) {
          expdesc val, cmp;
          codegen_expr(cg, arm->patterns[p], &val);
          init_exp(&cmp, VNONRELOC, ctrl_reg);
          luaK_infix(fs, OPR_EQ, &cmp);
          luaK_posfix(fs, OPR_EQ, &cmp, &val, 0);
          luaK_goiftrue(fs, &cmp);
          luaK_concat(fs, &success_jump, luaK_jump(fs));  /* 匹配成功，跳转到 body */
          /* 不匹配时修补到下一个检查位置 */
          luaK_patchtohere(fs, cmp.f);
        }
        /* 所有模式都不匹配时跳到下一个 case */
        next_check_jump = luaK_jump(fs);

        /* 修补成功跳转，生成 body 表达式到结果寄存器 */
        luaK_patchtohere(fs, success_jump);
        expdesc body_val;
        codegen_expr(cg, arm->body, &body_val);
        luaK_exp2reg(fs, &body_val, result_reg);

        /* 跳转到 switch 结束 */
        luaK_concat(fs, &finish_jump, luaK_jump(fs));

        jump_to_check = next_check_jump;
      }

      /* 处理 default 分支 */
      if (def != NULL) {
        luaK_patchtohere(fs, jump_to_check);
        expdesc def_val;
        codegen_expr(cg, def, &def_val);
        luaK_exp2reg(fs, &def_val, result_reg);
        jump_to_check = NO_JUMP;
      }

      /* 修补所有剩余的检查跳转 */
      if (jump_to_check != NO_JUMP) {
        luaK_patchtohere(fs, jump_to_check);
      }

      /* 没有匹配且没有 default 时返回 nil */
      if (def == NULL) {
        luaK_codeABC(fs, OP_LOADNIL, result_reg, result_reg, 0);
      }

      /* 修补所有 finish 跳转 */
      if (finish_jump != NO_JUMP) {
        luaK_patchtohere(fs, finish_jump);
      }

      /* 设置返回值 */
      init_exp(v, VNONRELOC, result_reg);
      /* 确保 freereg 指向结果之后，调用者能正确分配后续寄存器 */
      fs->freereg = cast_byte(result_reg + 1);
      break;
    }
    case AST_EXPR_NEW: {
      /* new 表达式：使用 OP_NEWOBJ 创建对象
       * 参考 lparser.c:newexpr
       */
      int i;
      int nargs = e->u.newexpr.nargs;
      expdesc class_exp;

      /* 评估类表达式 */
      codegen_expr(cg, e->u.newexpr.class_expr, &class_exp);
      luaK_exp2nextreg(fs, &class_exp);
      int class_reg = class_exp.u.info;

      /* 评估构造函数参数 */
      for (i = 0; i < nargs; i++) {
        expdesc arg;
        codegen_expr(cg, e->u.newexpr.args[i], &arg);
        luaK_exp2nextreg(fs, &arg);
      }

      /* 生成 OP_NEWOBJ: result = newobj(class, nargs+1) */
      int result_reg = class_reg;
      luaK_codeABC(fs, OP_NEWOBJ, result_reg, class_reg, nargs + 1);

      init_exp(v, VNONRELOC, result_reg);
      fs->freereg = cast_byte(result_reg + 1);
      break;
    }

    case AST_EXPR_MATCH: {
      /* match 表达式：委托给 codegen_match_body，设置 v */
      AstStmt *stmt = e->u.match.stmt;
      stmt->u.matchstmt.is_expr = 1;
      codegen_match_body(cg, stmt, v);
      break;
    }

    case AST_EXPR_SUPER: {
      /* super 表达式：编译为 self.__super（父类表）
       * 通过 self 局部变量查找 __super 字段访问父类
       * 后续的 .method 或 :method(args) 通过 AST_EXPR_INDEX/AST_EXPR_METHOD_CALL 处理
       */
      TString *self_name = luaS_newliteral(cg->L, "self");
      expdesc self_exp;
      cg_singlevaraux(cg, fs, self_name, &self_exp, 1);
      if (self_exp.k == VVOID) {
        cg_error(cg, "'super' can only be used inside class methods");
      }
      /* 将 self 放入寄存器 */
      luaK_exp2anyreg(fs, &self_exp);
      int self_reg = self_exp.u.info;
      /* 分配结果寄存器 */
      int result_reg = fs->freereg;
      luaK_reserveregs(fs, 1);
      /* 生成 self.__super 访问：OP_GETFIELD result_reg, self_reg, __super_key */
      TString *super_key = luaS_newliteral(cg->L, "__super");
      int super_k = luaK_stringK(fs, super_key);
      luaK_codeABC(fs, OP_GETFIELD, result_reg, self_reg, super_k);
      init_exp(v, VNONRELOC, result_reg);
      break;
    }

    case AST_EXPR_TEST_TYPE: {
      /* [-type expr "typename"] → type(expr) == "typename" */
      codegen_expr(cg, e->u.test_type.operand, v);
      int r = luaK_exp2anyreg(fs, v);
      int type_k = luaK_stringK(fs, e->u.test_type.type_name);
      /* OP_IS A B C k: if (type(R[A]) == K[B]) ~= k then pc++ */
      luaK_codeABCk(fs, OP_IS, r, type_k, 0, 0);
      int jmp_false = luaK_jump(fs);
      luaK_codeABC(fs, OP_LOADTRUE, r, 0, 0);
      int jmp_end = luaK_jump(fs);
      luaK_patchtohere(fs, jmp_false);
      luaK_codeABC(fs, OP_LOADFALSE, r, 0, 0);
      luaK_patchtohere(fs, jmp_end);
      init_exp(v, VNONRELOC, r);
      break;
    }

    case AST_EXPR_EMBED: {
      /* $embed "filename"：将文件内容作为字符串常量 */
      cg_codestring(v, e->u.embed.filename);
      break;
    }

    case AST_EXPR_OBJECT: {
      /* $object { ... }：将表构造器内联展开 */
      codegen_expr(cg, e->u.object.ctor, v);
      break;
    }

    case AST_EXPR_SPREAD: {
      /* ...expr 展开：转换为 table.unpack(expr) 调用
       * 参考 lparser.c simpleexp TK_DOTS 分支 */
      expdesc table_var;
      TString *table_name = luaS_newliteral(cg->L, "table");
      cg_singlevaraux(cg, fs, table_name, &table_var, 1);
      if (table_var.k == VVOID) {
        /* 全局 table：通过 _ENV["table"] 访问 */
        expdesc env;
        int envidx = cg_get_env_upval(cg);
        init_exp(&env, VUPVAL, envidx);
        expdesc k;
        cg_codestring(&k, table_name);
        luaK_indexed(fs, &env, &k);
        table_var = env;
      }
      luaK_exp2anyregup(fs, &table_var);

      expdesc unpack_key;
      cg_codestring(&unpack_key, luaS_newliteral(cg->L, "unpack"));
      luaK_indexed(fs, &table_var, &unpack_key);
      luaK_exp2nextreg(fs, &table_var);
      int func_reg = table_var.u.info;

      /* 生成要展开的表达式 */
      codegen_expr(cg, e->u.spread.expr, v);
      luaK_exp2nextreg(fs, v);

      init_exp(v, VCALL, luaK_codeABC(fs, OP_CALL, func_reg, 2, 0));
      fs->freereg = func_reg + 1;
      break;
    }

    case AST_EXPR_WALRUS: {
      /* (name := expr) 海象操作符：赋值并返回值
       * 参考 lparser.c parlist 中 walrus 分支 */
      expdesc var_exp;
      TString *varname = e->u.walrus.name;
      /* 查找变量：局部 -> upvalue -> 全局 */
      cg_singlevaraux(cg, fs, varname, &var_exp, 0);
      if (var_exp.k == VVOID) {
        /* 全局变量：通过 _ENV[name] 访问 */
        expdesc env;
        int envidx = cg_get_env_upval(cg);
        init_exp(&env, VUPVAL, envidx);
        expdesc key;
        cg_codestring(&key, varname);
        luaK_indexed(fs, &env, &key);
        var_exp = env;
      }
      /* 生成右侧表达式 */
      expdesc rhs;
      codegen_expr(cg, e->u.walrus.expr, &rhs);
      /* 存储到变量 */
      luaK_storevar(fs, &var_exp, &rhs);
      /* 返回值保持为表达式结果 */
      luaK_exp2nextreg(fs, &rhs);
      init_exp(v, VNONRELOC, rhs.u.info);
      break;
    }

    default: {
      cg_error(cg, "expression kind %d not yet implemented", e->kind);
      break;
    }
  }
}


/* ========== 语句代码生成 ========== */

/*
** 生成语句块
** 参数：
**   cg - CodegenState指针
**   blk - AST语句块
*/
static void codegen_block(CodegenState *cg, AstBlock *blk) {
  FuncState *fs = cg_fs(cg);
  int i;
  for (i = 0; i < blk->count; i++) {
    /* 每条语句开始前，freereg对齐到当前活动变量的栈顶 */
    fs->freereg = luaY_nvarstack(fs);
    codegen_stmt(cg, blk->items[i]);
    lua_assert(fs->f->maxstacksize >= fs->freereg && fs->freereg >= luaY_nvarstack(fs));
  }
}


/*
** 赋值目标：编译为可存储的expdesc
*/
static void codegen_assigntarget(CodegenState *cg, AstAssignTarget *tgt, expdesc *var) {
  FuncState *fs = cg_fs(cg);
  if (tgt->kind == AST_TGT_VAR) {
    cg_singlevar(cg, tgt->as.var.name, var);
  } else {
    expdesc tbl, key;
    codegen_expr(cg, tgt->as.index.table, &tbl);
    codegen_expr(cg, tgt->as.index.key, &key);
    luaK_exp2anyregup(fs, &tbl);  /* 放表表达式到寄存器或upvalue，确保luaK_indexed接收合法输入 */
    luaK_indexed(fs, &tbl, &key);
    *var = tbl;
  }
}


/**
 * @brief 生成匹配模式的条件检查和跳转代码
 * @param cg 代码生成状态
 * @param pat 匹配模式
 * @param ctrl_reg 控制值寄存器
 * @param next_check_jump 输出：失败跳转链（跳到下一个 case）
 * @param success_jump 输出：成功跳转链（跳到 body）
 */
static void codegen_match_pattern(CodegenState *cg, AstMatchPat *pat, int ctrl_reg,
                                   int *next_check_jump, int *success_jump) {
  FuncState *fs = cg_fs(cg);

  if (pat == NULL) {
    /* 空模式：总是失败 */
    luaK_concat(fs, next_check_jump, luaK_jump(fs));
    return;
  }

  switch (pat->kind) {
    case AST_PAT_WILDCARD:
      /* 通配符总是匹配 */
      luaK_concat(fs, success_jump, luaK_jump(fs));
      break;

    case AST_PAT_VARIABLE: {
      /* 变量绑定：总是匹配，创建局部变量并将控制值复制到变量寄存器 */
      add_local(cg, pat->u.var_name, VDKREG);
      activate_locals(cg, 1);
      int var_reg = luaY_nvarstack(fs) - 1;
      if (var_reg != ctrl_reg) {
        luaK_codeABC(fs, OP_MOVE, var_reg, ctrl_reg, 0);
      }
      luaK_concat(fs, success_jump, luaK_jump(fs));
      break;
    }

    case AST_PAT_TYPE: {
      /* 类型检查：OP_IS */
      int type_k = luaK_stringK(fs, pat->u.type_name);
      luaK_codeABC(fs, OP_IS, ctrl_reg, type_k, 0);
      luaK_concat(fs, next_check_jump, luaK_jump(fs));
      luaK_concat(fs, success_jump, luaK_jump(fs));
      break;
    }

    case AST_PAT_RANGE: {
      /* 范围检查：ctrl >= low and ctrl <= high */
      expdesc low, high, c1, c2;
      codegen_expr(cg, pat->u.range.low, &low);
      codegen_expr(cg, pat->u.range.high, &high);

      /* ctrl >= low */
      init_exp(&c1, VNONRELOC, ctrl_reg);
      luaK_infix(fs, OPR_GE, &c1);
      luaK_posfix(fs, OPR_GE, &c1, &low, 0);
      luaK_goiftrue(fs, &c1);
      int ge_false = c1.f;

      /* ctrl <= high */
      init_exp(&c2, VNONRELOC, ctrl_reg);
      luaK_infix(fs, OPR_LE, &c2);
      luaK_posfix(fs, OPR_LE, &c2, &high, 0);
      luaK_goiftrue(fs, &c2);

      luaK_concat(fs, next_check_jump, ge_false);
      luaK_concat(fs, next_check_jump, c2.f);
      luaK_concat(fs, success_jump, luaK_jump(fs));
      break;
    }

    case AST_PAT_LITERAL: {
      /* 字面量比较：ctrl == literal */
      expdesc e, c;
      codegen_expr(cg, pat->u.literal, &e);
      init_exp(&c, VNONRELOC, ctrl_reg);
      luaK_infix(fs, OPR_EQ, &c);
      luaK_posfix(fs, OPR_EQ, &c, &e, 0);
      luaK_goiftrue(fs, &c);
      luaK_concat(fs, next_check_jump, c.f);
      luaK_concat(fs, success_jump, luaK_jump(fs));
      break;
    }

    case AST_PAT_OR: {
      /* OR 多值匹配：对每个子模式尝试匹配，任一成功即可 */
      int *local_success = success_jump;
      for (int i = 0; i < pat->u.or_pat.npat; i++) {
        int sub_next = NO_JUMP;
        int sub_success = NO_JUMP;
        codegen_match_pattern(cg, pat->u.or_pat.pats[i], ctrl_reg, &sub_next, &sub_success);
        luaK_concat(fs, local_success, sub_success);
        luaK_concat(fs, next_check_jump, sub_next);
      }
      break;
    }

    case AST_PAT_TABLE: {
      /* 表解构匹配：先检查值是否为 table 类型 */
      if (pat->u.table_pat.nfields == 0) {
        /* 空表模式：只检查类型，不提取字段 */
        luaK_concat(fs, success_jump, luaK_jump(fs));
        break;
      }
      /* 有字段：检查类型后匹配每个字段 */
      int table_k = luaK_stringK(fs, luaS_new(cg->L, "table"));
      luaK_codeABC(fs, OP_IS, ctrl_reg, table_k, 0);
      luaK_concat(fs, next_check_jump, luaK_jump(fs));

      for (int i = 0; i < pat->u.table_pat.nfields; i++) {
        /* 获取表元素 R[ctrl_reg][i+1] */
        expdesc key, field_val;
        init_exp(&key, VKINT, 0);
        key.u.ival = i + 1;

        init_exp(&field_val, VNONRELOC, ctrl_reg);
        luaK_indexed(fs, &field_val, &key);
        luaK_exp2nextreg(fs, &field_val);
        int field_reg = fs->freereg - 1;

        int sub_next = NO_JUMP;
        int sub_success = NO_JUMP;
        codegen_match_pattern(cg, pat->u.table_pat.fields[i], field_reg, &sub_next, &sub_success);
        luaK_concat(fs, success_jump, sub_success);
        luaK_concat(fs, next_check_jump, sub_next);
      }
      break;
    }

    default:
      luaX_syntaxerror(&cg->ls, "internal error: unknown match pattern type");
      break;
  }
}


/*
** 生成单条语句
** 参数：
**   cg - CodegenState指针
**   s - AST语句节点
*/

/**
 * @brief 生成 match 语句/表达式的代码
 * @param cg 代码生成状态
 * @param s AST match 语句节点
 * @param v 输出表达式描述（NULL表示语句模式，非NULL表示表达式模式）
 */
static void codegen_match_body(CodegenState *cg, AstStmt *s, expdesc *v) {
  FuncState *fs = cg_fs(cg);
  AstExpr *control = s->u.matchstmt.control;
  AstMatchArm *arms = s->u.matchstmt.arms;
  int narms = s->u.matchstmt.narms;
  int is_expr = s->u.matchstmt.is_expr;

  if (control == NULL || narms == 0) {
    /* 空 match 语句：无控制表达式或无分支，返回 nil */
    if (v) init_exp(v, VNIL, 0);
    return;
  }

  int result_reg = -1;
  expdesc ctrl;

  /* 生成控制表达式代码 */
  codegen_expr(cg, control, &ctrl);
  luaK_exp2nextreg(fs, &ctrl);
  int ctrl_reg = ctrl.u.info;

  /* 表达式模式：分配结果寄存器 */
  if (is_expr) {
    result_reg = fs->freereg;
    luaK_reserveregs(fs, 1);
  }

  /* 跳转到第一个检查 */
  int jump_to_check = luaK_jump(fs);
  int finish_jump = NO_JUMP;

  for (int i = 0; i < narms; i++) {
    AstMatchArm *arm = &arms[i];
    int next_check_jump = NO_JUMP;
    int success_jump = NO_JUMP;

    /* 修补上一个检查的跳转到这里 */
    luaK_patchtohere(fs, jump_to_check);

    /* 生成模式匹配代码 */
    codegen_match_pattern(cg, arm->pattern, ctrl_reg, &next_check_jump, &success_jump);

    /* 修补成功跳转 */
    luaK_patchtohere(fs, success_jump);

    /* 可选守卫条件 */
    if (arm->guard) {
      expdesc cond;
      codegen_expr(cg, arm->guard, &cond);
      luaK_goiftrue(fs, &cond);
      luaK_concat(fs, &next_check_jump, cond.f);
    }

    /* 臂体代码生成 */
    if (arm->is_arrow) {
      /* 箭头表达式体 */
      expdesc body_val;
      codegen_expr(cg, arm->body_expr, &body_val);
      if (is_expr) {
        luaK_exp2reg(fs, &body_val, result_reg);
      } else {
        luaK_exp2nextreg(fs, &body_val);
      }
    } else {
      /* 语句块体 */
      codegen_block(cg, &arm->body_block);
    }

    /* 跳转到 match 结束 */
    luaK_concat(fs, &finish_jump, luaK_jump(fs));

    jump_to_check = next_check_jump;
  }

  /* 修补最后的检查跳转（没有匹配的 case） */
  luaK_patchtohere(fs, jump_to_check);

  /* 没有匹配时返回 nil（表达式模式） */
  if (is_expr) {
    luaK_codeABC(fs, OP_LOADNIL, result_reg, result_reg, 0);
  }

  /* 修补所有 finish 跳转 */
  if (finish_jump != NO_JUMP) {
    luaK_patchtohere(fs, finish_jump);
  }

  /* 表达式模式：设置返回值 */
  if (v && is_expr) {
    int target_reg = fs->nactvar;
    if (result_reg != target_reg) {
      luaK_codeABC(fs, OP_MOVE, target_reg, result_reg, 0);
    }
    init_exp(v, VNONRELOC, target_reg);
    fs->freereg = cast_byte(target_reg + 1);
  }
}

/**
 * @brief 生成单条语句的代码
 * @param cg 代码生成状态
 * @param s AST 语句节点
 */
static void codegen_stmt(CodegenState *cg, AstStmt *s) {
  FuncState *fs = cg_fs(cg);
  BlockCnt bl;
  cg->ls.lastline = s->node.line;  /* 同步AST节点行号到codegen，确保运行时错误报告正确行号 */

  switch (s->kind) {
    case AST_STMT_EMPTY: {
      break;
    }
    case AST_STMT_BLOCK:
    case AST_STMT_DO: {
      enterblock(fs, &bl, 0);
      codegen_block(cg, &s->u.block.block);
      leaveblock(fs);
      break;
    }
    case AST_STMT_LOCAL: {
      int nnames = s->u.local.nnames;
      int nvalues = s->u.local.nvalues;
      int i;
      expdesc v;
      int base = fs->freereg;
      /* 先计算前n-1个值，每个放入寄存器 */
      for (i = 0; i < nvalues - 1; i++) {
        expdesc e;
        codegen_expr(cg, s->u.local.values[i], &e);
        luaK_exp2nextreg(fs, &e);
      }
      /* 计算最后一个值 */
      if (nvalues > 0) {
        codegen_expr(cg, s->u.local.values[nvalues - 1], &v);
      } else {
        init_exp(&v, VVOID, 0);
      }
      /* 调整赋值结果数量 */
      cg_adjust_assign(fs, nnames, nvalues, &v);
      (void)base;
      /* 添加局部变量并激活 */
      for (i = 0; i < nnames; i++) {
        int attr = VDKREG;
        if (s->u.local.attrs && s->u.local.attrs[i] == AST_ATTR_CONST)
          attr = RDKCONST;
        else if (s->u.local.attrs && s->u.local.attrs[i] == AST_ATTR_CLOSE)
          attr = RDKTOCLOSE;
        add_local(cg, s->u.local.names[i], attr);
      }
      activate_locals(cg, nnames);
      break;
    }
    case AST_STMT_ASSIGN: {
      int ntargets = s->u.assign.ntargets;
      int nvalues = s->u.assign.nvalues;
      int i;
      expdesc *vars;
      expdesc v;
      int base;
      int ndec = s->ndecorators;
      int dec_base = 0;

      /* 先评估装饰器表达式到寄存器（如果有的话） */
      if (ndec > 0) {
        dec_base = fs->freereg;
        for (i = 0; i < ndec; i++) {
          expdesc dec;
          codegen_expr(cg, s->decorators[i], &dec);
          luaK_exp2nextreg(fs, &dec);
        }
      }

      base = fs->freereg;
      /* 先编译前n-1个值 */
      for (i = 0; i < nvalues - 1; i++) {
        expdesc e;
        codegen_expr(cg, s->u.assign.values[i], &e);
        luaK_exp2nextreg(fs, &e);
      }
      /* 最后一个值 */
      if (nvalues > 0) {
        codegen_expr(cg, s->u.assign.values[nvalues - 1], &v);
      } else {
        init_exp(&v, VVOID, 0);
      }
      /* 调整赋值结果 */
      cg_adjust_assign(fs, ntargets, nvalues, &v);

      /* 应用装饰器到最后一个值 */
      if (ndec > 0 && nvalues > 0) {
        luaK_exp2nextreg(fs, &v);
        int target_reg = v.u.info;
        for (i = ndec - 1; i >= 0; i--) {
          int d_reg = dec_base + i;
          int call_base = fs->freereg;
          luaK_reserveregs(fs, 2);
          luaK_codeABC(fs, OP_MOVE, call_base, d_reg, 0);
          luaK_codeABC(fs, OP_MOVE, call_base + 1, target_reg, 0);
          luaK_codeABC(fs, OP_CALL, call_base, 2, 2);
          luaK_codeABC(fs, OP_MOVE, target_reg, call_base, 0);
          fs->freereg -= 2;
        }
        /* 将结果移回 base（确保 store 时使用正确的寄存器） */
        if (target_reg != base) {
          luaK_codeABC(fs, OP_MOVE, base, target_reg, 0);
          target_reg = base;
        }
        init_exp(&v, VNONRELOC, target_reg);
      }

      /* 编译目标并存储 */
      vars = luaM_newvector(cg->L, ntargets, expdesc);
      for (i = 0; i < ntargets; i++) {
        codegen_assigntarget(cg, &s->u.assign.targets[i], &vars[i]);
      }
      for (i = 0; i < ntargets; i++) {
        expdesc val;
        int reg = base + i;
        init_exp(&val, VNONRELOC, reg);
        luaK_storevar(fs, &vars[i], &val);
      }
      luaM_freearray(cg->L, vars, ntargets);
      break;
    }
    case AST_STMT_EXPR: {
      expdesc v;
      codegen_expr(cg, s->u.expr.expr, &v);
      if (v.k == VCALL) {
        /* 函数调用语句：设置C=1表示0个返回值，并回收寄存器 */
        Instruction *inst = &getinstruction(fs, &v);
        int base = GETARG_A(*inst);
        SETARG_C(*inst, 1);
        fs->freereg = cast_byte(base + 1);
        /* 检查 <nodiscard> 函数，丢弃返回值时发出警告 */
        if (v.nodiscard) {
          luaX_warning(&cg->ls,
            "discarding return value of function declared '<nodiscard>'",
            WT_DISCARDED_RETURN);
        }
      } else {
        /* 其他表达式：discharge到寄存器然后丢弃（Lua标准中只有函数调用可以作为语句） */
        luaK_exp2nextreg(fs, &v);
      }
      break;
    }
    case AST_STMT_IF: {
      int narms = s->u.ifstmt.narms;
      int i;
      int escapelist = NO_JUMP;
      /* 遍历if/elseif分支 */
      for (i = 0; i < narms; i++) {
        AstIfArm *arm = &s->u.ifstmt.arms[i];
        expdesc cond;
        int jf;  /* 条件为假时的跳转 */

        if (arm->let_var != NULL) {
          /* if let name = expr: 先赋值给局部变量，再用该变量作为条件 */
          expdesc v;
          /* 进入 let 变量的作用域 */
          enterblock(fs, &bl, 0);
          codegen_expr(cg, arm->cond, &v);
          cg_adjust_assign(fs, 1, 1, &v);
          add_local(cg, arm->let_var, VDKREG);
          activate_locals(cg, 1);
          /* 用局部变量作为条件 */
          init_exp(&cond, VLOCAL, fs->nactvar - 1);
        } else {
          codegen_expr(cg, arm->cond, &cond);
        }

        /* 条件为假时跳过此分支 */
        luaK_goiftrue(fs, &cond);
        jf = cond.f;
        /* 分支体 */
        if (arm->let_var == NULL) {
          enterblock(fs, &bl, 0);
        }
        codegen_block(cg, &arm->body);
        leaveblock(fs);
        /* 分支结束，跳转到if结尾 */
        luaK_concat(fs, &escapelist, luaK_jump(fs));
        /* patch jf到此处（下一个分支或else开始） */
        luaK_patchtohere(fs, jf);
      }
      /* else块 */
      if (s->u.ifstmt.has_else) {
        enterblock(fs, &bl, 0);
        codegen_block(cg, &s->u.ifstmt.else_body);
        leaveblock(fs);
      }
      /* patch所有分支结束跳转到这里 */
      luaK_patchtohere(fs, escapelist);
      break;
    }
    case AST_STMT_GUARD: {
      /* guard 语句：guard cond else { ... } 或 guard let name = expr else { ... } */
      if (s->u.guard.let_var != NULL) {
        /* guard let name = expr else { ... } */
        expdesc v;
        /* 计算值表达式 */
        codegen_expr(cg, s->u.guard.let_value, &v);
        /* 调整为单个结果 */
        cg_adjust_assign(fs, 1, 1, &v);
        /* 添加局部变量并激活 */
        add_local(cg, s->u.guard.let_var, VDKREG);
        activate_locals(cg, 1);
        /* 检查是否为 nil：nil 时执行 else 块 */
        luaK_goifnil(fs, &v);
        /* 解析 else 块（nil 时执行） */
        enterblock(fs, &bl, 0);
        codegen_block(cg, &s->u.guard.else_block);
        leaveblock(fs);
        /* 非 nil 时跳转到这里（跳过 else 块） */
        luaK_patchtohere(fs, v.t);
      } else {
        /* guard cond else { ... } */
        expdesc cond;
        /* 计算条件表达式 */
        codegen_expr(cg, s->u.guard.cond, &cond);
        /* 条件为假时执行 else 块（goiffalse: f=当前, t=跳转） */
        luaK_goiffalse(fs, &cond);
        /* 解析 else 块（条件为假时执行） */
        enterblock(fs, &bl, 0);
        codegen_block(cg, &s->u.guard.else_block);
        leaveblock(fs);
        /* 条件为真时跳转到这里（跳过 else 块） */
        luaK_patchtohere(fs, cond.t);
      }
      break;
    }
    case AST_STMT_WHILE: {
      int while_init = luaK_getlabel(fs);
      int condexit;
      expdesc cond;
      BlockCnt bl_while;
      /* 进入循环层级，初始化当前层的break/continue跳转链 */
      cg->loop_depth++;
      cg->loop_stack[cg->loop_depth].break_list = NO_JUMP;
      cg->loop_stack[cg->loop_depth].continue_list = NO_JUMP;
      codegen_expr(cg, s->u.whilestmt.cond, &cond);
      if (cond.k == VNIL) cond.k = VFALSE;
      luaK_goiftrue(fs, &cond);
      condexit = cond.f;
      enterblock(fs, &bl_while, 1);
      codegen_block(cg, &s->u.whilestmt.body);
      /* continue标签在循环体末尾、跳转回开头之前 */
      luaK_patchtohere(fs, cg->loop_stack[cg->loop_depth].continue_list);
      luaK_jumpto(fs, while_init);
      /* 条件为假时跳转到此处（循环结束） */
      luaK_patchtohere(fs, condexit);
      leaveblock(fs);
      /* 生成 while...else 的 else 块 */
      if (s->u.whilestmt.has_else) {
        int else_jump = luaK_jump(fs);  /* break 跳过 else 块 */
        luaK_patchtohere(fs, cg->loop_stack[cg->loop_depth].break_list);
        codegen_block(cg, &s->u.whilestmt.else_body);
        luaK_patchtohere(fs, else_jump);
      } else {
        luaK_patchtohere(fs, cg->loop_stack[cg->loop_depth].break_list);
      }
      /* 离开循环层级 */
      cg->loop_depth--;
      break;
    }
    case AST_STMT_WHILE_LET: {
      /* while let name {, name} = expr do body end
       * 语义：每轮循环重新计算expr并赋值给局部变量，当第一个变量为nil/false时退出
       */
      int while_init = luaK_getlabel(fs);
      int condexit;
      int nvars = s->u.whilelet.nnames;
      int i;
      expdesc v;
      BlockCnt bl_while;

      /* 进入循环层级，初始化当前层的break/continue跳转链 */
      cg->loop_depth++;
      cg->loop_stack[cg->loop_depth].break_list = NO_JUMP;
      cg->loop_stack[cg->loop_depth].continue_list = NO_JUMP;

      /* 进入循环块（包含表达式计算+循环体），局部变量作用域在此块内 */
      enterblock(fs, &bl_while, 1);

      /* 计算表达式并赋值给局部变量 */
      codegen_expr(cg, s->u.whilelet.expr, &v);
      cg_adjust_assign(fs, nvars, 1, &v);

      /* 添加并激活局部变量 */
      for (i = 0; i < nvars; i++) {
        add_local(cg, s->u.whilelet.names[i], VDKREG);
      }
      activate_locals(cg, nvars);

      /* 用第一个局部变量作为循环条件 */
      {
        expdesc cond_v;
        init_exp(&cond_v, VLOCAL, fs->nactvar - nvars);
        luaK_goiftrue(fs, &cond_v);
        condexit = cond_v.f;
      }

      /* 生成循环体 */
      codegen_block(cg, &s->u.whilelet.body);

      /* continue标签在循环体末尾、跳转回开头之前 */
      luaK_patchtohere(fs, cg->loop_stack[cg->loop_depth].continue_list);
      luaK_jumpto(fs, while_init);

      /* 条件为假时跳转到此处（循环结束） */
      luaK_patchtohere(fs, condexit);
      leaveblock(fs);

      /* 生成 while...else 的 else 块 */
      if (s->u.whilelet.has_else) {
        int else_jump = luaK_jump(fs);  /* 正常退出跳过 else 块 */
        luaK_patchtohere(fs, cg->loop_stack[cg->loop_depth].break_list);
        codegen_block(cg, &s->u.whilelet.else_body);
        luaK_patchtohere(fs, else_jump);
      } else {
        luaK_patchtohere(fs, cg->loop_stack[cg->loop_depth].break_list);
      }

      /* 离开循环层级 */
      cg->loop_depth--;
      break;
    }
    case AST_STMT_REPEAT: {
      int repeat_init = luaK_getlabel(fs);
      int condexit;
      expdesc cond;
      BlockCnt bl1, bl2;
      /* 进入循环层级，初始化当前层的break/continue跳转链 */
      cg->loop_depth++;
      cg->loop_stack[cg->loop_depth].break_list = NO_JUMP;
      cg->loop_stack[cg->loop_depth].continue_list = NO_JUMP;
      enterblock(fs, &bl1, 1);  /* loop block */
      enterblock(fs, &bl2, 0);  /* scope block */
      codegen_block(cg, &s->u.whilestmt.body);
      /* continue标签在循环体末尾、条件检查之前 */
      luaK_patchtohere(fs, cg->loop_stack[cg->loop_depth].continue_list);
      codegen_expr(cg, s->u.whilestmt.cond, &cond);
      if (cond.k == VNIL) cond.k = VFALSE;
      luaK_goiftrue(fs, &cond);
      condexit = cond.f;
      leaveblock(fs);  /* finish scope (bl2) */
      if (bl2.upval) {
        int exit = luaK_jump(fs);
        luaK_patchtohere(fs, condexit);
        luaK_codeABC(fs, OP_CLOSE, bl2.nactvar, 0, 0);
        condexit = luaK_jump(fs);
        luaK_patchtohere(fs, exit);
      }
      luaK_patchlist(fs, condexit, repeat_init);
      /* break跳转到循环结束 */
      luaK_patchtohere(fs, cg->loop_stack[cg->loop_depth].break_list);
      leaveblock(fs);  /* finish loop (bl1) */
      /* 离开循环层级 */
      cg->loop_depth--;
      break;
    }
    case AST_STMT_RETURN: {
      int nvalues = s->u.retstmt.nvalues;
      int first = fs->nactvar;
      int i;
      expdesc v;
      int nret;
      if (nvalues == 0) {
        nret = 0;
      } else {
        /* 生成前n-1个值 */
        for (i = 0; i < nvalues - 1; i++) {
          expdesc e;
          codegen_expr(cg, s->u.retstmt.values[i], &e);
          luaK_exp2nextreg(fs, &e);
        }
        /* 生成最后一个值 */
        codegen_expr(cg, s->u.retstmt.values[nvalues - 1], &v);
        if (hasmultret(v.k)) {
          luaK_setmultret(fs, &v);
          nret = LUA_MULTRET;
        } else {
          if (nvalues == 1) {
            first = luaK_exp2anyreg(fs, &v);
          } else {
            luaK_exp2nextreg(fs, &v);
            lua_assert(nvalues == fs->freereg - first);
          }
          nret = nvalues;
        }
      }
      luaK_ret(fs, first, nret);
      break;
    }
    case AST_STMT_BREAK: {
      /* 生成break跳转，支持多层级 break N */
      {
        int level = s->u.contbrk.level;
        int pc;
        if (level > cg->loop_depth) {
          cg_error(cg, "break level %d exceeds loop depth %d", level, cg->loop_depth);
        }
        pc = luaK_jump(fs);
        /* loop_stack 使用 1-based 索引，level=1 对应栈顶 */
        luaK_concat(fs, &cg->loop_stack[cg->loop_depth - level + 1].break_list, pc);
      }
      break;
    }
    case AST_STMT_CONTINUE: {
      /* 生成continue跳转，支持多层级 continue N */
      {
        int level = s->u.contbrk.level;
        int pc;
        if (level > cg->loop_depth) {
          cg_error(cg, "continue level %d exceeds loop depth %d", level, cg->loop_depth);
        }
        pc = luaK_jump(fs);
        luaK_concat(fs, &cg->loop_stack[cg->loop_depth - level + 1].continue_list, pc);
      }
      break;
    }
    case AST_STMT_LABEL: {
      TString *name = s->u.label.name;
      int pc = luaK_getlabel(fs);
      register_label(cg, name, pc);
      break;
    }
    case AST_STMT_GOTO: {
      TString *name = s->u.label.name;
      int pc = luaK_jump(fs);
      add_goto(cg, name, pc);
      break;
    }
    case AST_STMT_LOCAL_FUNC: {
      TString *name = s->u.localfunc.name;
      AstFunc *f = s->u.localfunc.func;
      expdesc v;
      int ndec = s->ndecorators;
      int dec_base = 0;

      /* 先评估装饰器表达式到寄存器（如果有的话） */
      if (ndec > 0) {
        dec_base = fs->freereg;
        for (int i = 0; i < ndec; i++) {
          expdesc dec;
          codegen_expr(cg, s->decorators[i], &dec);
          luaK_exp2nextreg(fs, &dec);
        }
      }

      /* 先添加局部变量占位（支持递归） */
      add_local(cg, name, VDKREG);
      activate_locals(cg, 1);
      /* 生成子函数 */
      {
        Proto *p = codegen_func(cg, f);
        int bx = fs->np++;
        int oldsize;
        if (bx >= fs->f->sizep) {
          oldsize = fs->f->sizep;
          luaM_growvector(cg->L, fs->f->p, bx + 1, fs->f->sizep,
                          Proto *, MAXARG_Bx, "functions");
          while (oldsize < fs->f->sizep)
            fs->f->p[oldsize++] = NULL;
        }
        fs->f->p[bx] = p;
        luaC_objbarrier(cg->L, fs->f, p);
        init_exp(&v, VRELOC, luaK_codeABx(fs, OP_CLOSURE, 0, bx));
        luaK_exp2nextreg(fs, &v);
        /* async 函数：发射 OP_ASYNCWRAP 包装 */
        if (f->is_async) {
          luaK_codeABC(fs, OP_ASYNCWRAP, 0, v.u.info, 0);
        }
      }

      /* 应用装饰器 */
      if (ndec > 0) {
        int target_reg = v.u.info;
        for (int i = ndec - 1; i >= 0; i--) {
          int d_reg = dec_base + i;
          int call_base = fs->freereg;
          luaK_reserveregs(fs, 2);
          luaK_codeABC(fs, OP_MOVE, call_base, d_reg, 0);
          luaK_codeABC(fs, OP_MOVE, call_base + 1, target_reg, 0);
          luaK_codeABC(fs, OP_CALL, call_base, 2, 2);
          luaK_codeABC(fs, OP_MOVE, target_reg, call_base, 0);
          fs->freereg -= 2;
        }
        init_exp(&v, VNONRELOC, target_reg);
      }

      /* 存储到局部变量 */
      {
        expdesc var;
        cg_init_local(cg, &var, fs->nactvar - 1);
        luaK_storevar(fs, &var, &v);
      }
      break;
    }
    case AST_STMT_COMPOUND_ASSIGN: {
      /* 复合赋值：a op= b 等价于 a = a op b */
      AstBinOp ast_op = s->u.compound.op;
      BinOpr op = binop_map[ast_op];
      expdesc var, rhs, tgt;
      int line = s->node.line;
      /* 编译目标并读取当前值 */
      codegen_assigntarget(cg, &s->u.compound.targets[0], &var);
      luaK_dischargevars(fs, &var);
      luaK_exp2anyreg(fs, &var);
      /* 二元运算：var = var op rhs */
      luaK_infix(fs, op, &var);
      codegen_expr(cg, s->u.compound.value, &rhs);
      luaK_posfix(fs, op, &var, &rhs, line);
      /* 存回目标 */
      codegen_assigntarget(cg, &s->u.compound.targets[0], &tgt);
      luaK_storevar(fs, &tgt, &var);
      break;
    }
    case AST_STMT_INCR_DECR: {
      /* 自增/自减：++x / --x / x++ / x--，作为语句时均等价于 x = x +/- 1 */
      AstIncrKind kind = s->u.incr.kind;
      BinOpr op = (kind == AST_INCR_PRE_INC || kind == AST_INCR_POST_INC)
                  ? OPR_ADD : OPR_SUB;
      expdesc var, one, tgt;
      int line = s->node.line;
      /* 编译目标并读取当前值 */
      codegen_assigntarget(cg, s->u.incr.target, &var);
      luaK_dischargevars(fs, &var);
      luaK_exp2anyreg(fs, &var);
      /* var = var op 1 */
      luaK_infix(fs, op, &var);
      init_exp(&one, VKINT, 1);
      one.u.ival = 1;
      luaK_posfix(fs, op, &var, &one, line);
      /* 存回目标 */
      codegen_assigntarget(cg, s->u.incr.target, &tgt);
      luaK_storevar(fs, &tgt, &var);
      break;
    }
    case AST_STMT_FOR_NUM: {
      /* 数值for循环：for var = start, stop [, step] do body end */
      int base;
      int prep, endfor;
      TString *for_state_name;
      BlockCnt bl;
      /* 进入循环层级，初始化当前层的break/continue跳转链 */
      cg->loop_depth++;
      cg->loop_stack[cg->loop_depth].break_list = NO_JUMP;
      cg->loop_stack[cg->loop_depth].continue_list = NO_JUMP;
      /* 进入外块（loop scope，控制变量+用户变量+body，break跳转到leaveblock之后） */
      enterblock(fs, &bl, 1);
      for_state_name = luaS_new(cg->L, "(for state)");
      /* 添加3个内部控制变量和1个用户变量到局部变量表（尚未激活） */
      add_local(cg, for_state_name, VDKREG);       /* 控制变量0: index */
      add_local(cg, for_state_name, VDKREG);       /* 控制变量1: limit/counter */
      add_local(cg, for_state_name, VDKREG);       /* 控制变量2: step */
      add_local(cg, s->u.fornum.var, RDKCONST);    /* 用户循环变量 */
      base = fs->freereg;
      luaK_checkstack(fs, 3);
      /* 生成start */
      {
        expdesc e;
        codegen_expr(cg, s->u.fornum.start, &e);
        luaK_exp2nextreg(fs, &e);
      }
      /* 生成limit */
      {
        expdesc e;
        codegen_expr(cg, s->u.fornum.stop, &e);
        luaK_exp2nextreg(fs, &e);
      }
      /* 生成step，缺省为1 */
      if (s->u.fornum.step) {
        expdesc e;
        codegen_expr(cg, s->u.fornum.step, &e);
        luaK_exp2nextreg(fs, &e);
      } else {
        luaK_int(fs, fs->freereg, 1);
        luaK_reserveregs(fs, 1);
      }
      /* 激活3个控制变量（ridx = base, base+1, base+2） */
      activate_locals(cg, 3);
      /* 生成FORPREP */
      prep = luaK_codeABx(fs, OP_FORPREP, base, 0);
      /* 进入内块（用户变量作用域，参考原forbody） */
      {
        BlockCnt bl2;
        enterblock(fs, &bl2, 0);
        /* 激活用户循环变量（ridx = base+3） */
        activate_locals(cg, 1);
        luaK_reserveregs(fs, 1);
        /* 生成循环体 */
        codegen_block(cg, &s->u.fornum.body);
        /* patch continue到此处（循环体结束，回到FORLOOP） */
        luaK_patchtohere(fs, cg->loop_stack[cg->loop_depth].continue_list);
        leaveblock(fs);  /* 离开内块（用户变量作用域） */
      }
      /* patch FORPREP前跳到FORLOOP之后 */
      cg_fixforjump(fs, prep, fs->pc, 0);
      /* 生成FORLOOP */
      endfor = luaK_codeABx(fs, OP_FORLOOP, base, 0);
      cg_fixforjump(fs, endfor, prep + 1, 1);
      luaK_fixline(fs, s->node.line);
      /* 离开外块（loop scope） */
      leaveblock(fs);
      /* 生成 for...else 的 else 块 */
      if (s->u.fornum.has_else) {
        int else_jump = luaK_jump(fs);  /* break 跳过 else 块 */
        luaK_patchtohere(fs, cg->loop_stack[cg->loop_depth].break_list);
        codegen_block(cg, &s->u.fornum.else_body);
        luaK_patchtohere(fs, else_jump);
      } else {
        luaK_patchtohere(fs, cg->loop_stack[cg->loop_depth].break_list);
      }
      /* 离开循环层级 */
      cg->loop_depth--;
      break;
    }
    case AST_STMT_FOR_GEN: {
      /* 泛型for循环：for namelist in explist do body end */
      int base;
      int prep, endfor;
      int nnames = s->u.forgen.nnames;
      int nexprs = s->u.forgen.nexprs;
      int i;
      TString *for_state_name;
      BlockCnt bl;
      /* 进入循环层级，初始化当前层的break/continue跳转链 */
      cg->loop_depth++;
      cg->loop_stack[cg->loop_depth].break_list = NO_JUMP;
      cg->loop_stack[cg->loop_depth].continue_list = NO_JUMP;
      /* 进入外块（loop scope，参考原forstat+forlist结构） */
      enterblock(fs, &bl, 1);
      for_state_name = luaS_new(cg->L, "(for state)");
      /* 添加4个内部控制变量 + nnames个用户变量（尚未激活） */
      add_local(cg, for_state_name, VDKREG);       /* 控制变量0: gen */
      add_local(cg, for_state_name, VDKREG);       /* 控制变量1: state */
      add_local(cg, for_state_name, VDKREG);       /* 控制变量2: control */
      add_local(cg, for_state_name, RDKTOCLOSE);   /* 控制变量3: toclose */
      for (i = 0; i < nnames; i++) {
        add_local(cg, s->u.forgen.names[i], RDKCONST);
      }
      base = fs->freereg;
      luaK_checkstack(fs, 3);
      /* 生成explist到base开始的寄存器（需要恰好3个值：gen, state, control） */
      for (i = 0; i < nexprs - 1; i++) {
        expdesc e;
        codegen_expr(cg, s->u.forgen.exprs[i], &e);
        luaK_exp2nextreg(fs, &e);
      }
      if (nexprs > 0) {
        expdesc e;
        codegen_expr(cg, s->u.forgen.exprs[nexprs - 1], &e);
        cg_adjust_assign(fs, 4, nexprs, &e);
      } else {
        luaK_nil(fs, fs->freereg, 4);
        luaK_reserveregs(fs, 4);
      }
      /* 激活4个控制变量 */
      activate_locals(cg, 4);
      /* 标记to-be-closed变量 */
      cg_marktobeclosed(fs);
      /* 生成TFORPREP */
      prep = luaK_codeABx(fs, OP_TFORPREP, base, 0);
      /* 进入内块（用户变量作用域，参考原forbody） */
      {
        BlockCnt bl2;
        enterblock(fs, &bl2, 0);
        /* 激活nnames个用户循环变量 */
        activate_locals(cg, nnames);
        luaK_reserveregs(fs, nnames);
        /* 生成循环体 */
        codegen_block(cg, &s->u.forgen.body);
        /* patch continue到此处 */
        luaK_patchtohere(fs, cg->loop_stack[cg->loop_depth].continue_list);
        leaveblock(fs);  /* 离开内块 */
      }
      /* patch TFORPREP跳到TFORCALL位置 */
      cg_fixforjump(fs, prep, fs->pc, 0);
      /* 生成TFORCALL */
      luaK_codeABC(fs, OP_TFORCALL, base, 0, nnames);
      luaK_fixline(fs, s->node.line);
      /* 生成TFORLOOP */
      endfor = luaK_codeABx(fs, OP_TFORLOOP, base, 0);
      cg_fixforjump(fs, endfor, prep + 1, 1);
      luaK_fixline(fs, s->node.line);
      /* 离开外块（loop scope） */
      leaveblock(fs);
      /* 生成 for...else 的 else 块 */
      if (s->u.forgen.has_else) {
        int else_jump = luaK_jump(fs);  /* break 跳过 else 块 */
        luaK_patchtohere(fs, cg->loop_stack[cg->loop_depth].break_list);
        codegen_block(cg, &s->u.forgen.else_body);
        luaK_patchtohere(fs, else_jump);
      } else {
        luaK_patchtohere(fs, cg->loop_stack[cg->loop_depth].break_list);
      }
      /* 离开循环层级 */
      cg->loop_depth--;
      break;
    }
    case AST_STMT_TRY: {
      /* try-catch-finally 完整实现（pcall 包装）
       * 参考 lparser.c:trystat 的逻辑：
       * 1. 创建局部变量 __try_ok__ 和 __try_err__
       * 2. 获取 pcall 函数
       * 3. 创建闭包包装 try 体
       * 4. 调用 pcall(closure)，返回 ok, err
       * 5. 如果 ok 为假，执行 catch 块
       * 6. finally 块无条件执行
       */
      int has_catch = (s->u.trycatch.catch_var != NULL);
      int has_finally = (s->u.trycatch.finally_body.count > 0);
      int base;
      TString *ok_name, *err_name;
      int ok_vidx, err_vidx;
      expdesc pcall_func, closure_exp, ok_var, err_var;
      BlockCnt bl;

      /* 进入外层 block */
      enterblock(fs, &bl, 0);

      /* 创建两个局部变量 __try_ok__ 和 __try_err__ */
      ok_name = luaS_newliteral(cg->L, "__try_ok__");
      err_name = luaS_newliteral(cg->L, "__try_err__");
      ok_vidx = add_local(cg, ok_name, VDKREG);
      err_vidx = add_local(cg, err_name, VDKREG);
      activate_locals(cg, 2);

      /* 获取 pcall 全局函数 */
      cg_singlevar(cg, luaS_newliteral(cg->L, "pcall"), &pcall_func);
      luaK_exp2nextreg(fs, &pcall_func);
      base = pcall_func.u.info;

      /* 创建闭包：function() try_body end */
      {
        FuncState new_fs;
        BlockCnt new_bl;
        Proto *try_proto;

        try_proto = addprototype(&cg->ls);
        try_proto->linedefined = s->node.line;

        memset(&new_fs, 0, sizeof(new_fs));
        new_fs.f = try_proto;
        open_func(&cg->ls, &new_fs, &new_bl);

        new_fs.f->numparams = 0;
        new_fs.f->is_vararg = 0;

        /* 生成 try 体代码 */
        codegen_block(cg, &s->u.trycatch.body);

        new_fs.f->lastlinedefined = s->node.line;
        codeclosure(&cg->ls, &closure_exp);
        close_func(&cg->ls);
      }

      /* 将闭包放入下一个寄存器（pcall 的参数） */
      luaK_exp2nextreg(fs, &closure_exp);

      /* 调用 pcall(closure)，返回 ok, err */
      luaK_codeABC(fs, OP_CALL, base, 2, 3);  /* 1个函数 + 1个参数 = 2, 2个返回值 */
      fs->freereg = base + 2;

      /* 将结果存储到局部变量 ok_var <- R(base), err_var <- R(base+1) */
      {
        Vardesc *ok_vd = &cg->dyd->actvar.arr[fs->firstlocal + ok_vidx];
        Vardesc *err_vd = &cg->dyd->actvar.arr[fs->firstlocal + err_vidx];
        expdesc result;

        /* 初始化 ok_var 和 err_var 为 VLOCAL，使用正确的寄存器索引 */
        ok_var.k = VLOCAL;
        ok_var.f = ok_var.t = NO_JUMP;
        ok_var.u.var.vidx = ok_vidx;
        ok_var.u.var.ridx = ok_vd->vd.ridx;

        err_var.k = VLOCAL;
        err_var.f = err_var.t = NO_JUMP;
        err_var.u.var.vidx = err_vidx;
        err_var.u.var.ridx = err_vd->vd.ridx;

        init_exp(&result, VNONRELOC, base);
        luaK_storevar(fs, &ok_var, &result);
        init_exp(&result, VNONRELOC, base + 1);
        luaK_storevar(fs, &err_var, &result);
      }

      /* 处理 catch 块 */
      if (has_catch) {
        expdesc cond;
        BlockCnt catch_bl;
        int jt;

        /* 生成条件跳转：如果 __try_ok__ 为假则执行 catch */
        init_exp(&cond, VLOCAL, ok_vidx);
        /* 手动设置 ridx（因为 init_exp 不设置 u.var 字段） */
        {
          Vardesc *ok_vd = &cg->dyd->actvar.arr[fs->firstlocal + ok_vidx];
          cond.u.var.ridx = ok_vd->vd.ridx;
        }
        luaK_exp2anyreg(fs, &cond);
        luaK_goiffalse(fs, &cond);  /* 假 → fallthrough 执行 catch；真 → 跳转跳过 catch */
        jt = cond.t;

        /* 进入 catch 块 */
        enterblock(fs, &catch_bl, 0);

        /* 创建局部变量 catch_var = __try_err__ */
        {
          TString *catchname;
          /* 从 catch_var 表达式中提取变量名 */
          if (s->u.trycatch.catch_var->kind == AST_EXPR_IDENT) {
            catchname = s->u.trycatch.catch_var->u.strval;
          } else {
            catchname = luaS_newliteral(cg->L, "__catch_var__");
          }
          add_local(cg, catchname, VDKREG);
          activate_locals(cg, 1);
          /* 将 __try_err__ 的值赋给 catch 变量 */
          {
            expdesc err_val;
            Vardesc *err_vd = &cg->dyd->actvar.arr[fs->firstlocal + err_vidx];
            init_exp(&err_val, VLOCAL, err_vidx);
            err_val.u.var.ridx = err_vd->vd.ridx;
            luaK_exp2nextreg(fs, &err_val);
          }
        }

        /* 生成 catch 体代码 */
        codegen_block(cg, &s->u.trycatch.catch_body);

        leaveblock(fs);
        luaK_patchtohere(fs, jt);  /* 真跳转跳到这里（跳过 catch） */
      }

      /* 处理 finally 块 */
      if (has_finally) {
        /* finally 块无条件执行 */
        codegen_block(cg, &s->u.trycatch.finally_body);
      }

      leaveblock(fs);
      break;
    }
    case AST_STMT_DEFER: {
      /* defer 完整实现（闭包 + to-be-closed 局部变量）
       * 参考 lparser.c:deferstat 的逻辑：
       * 1. 创建匿名函数闭包包装 defer 体
       * 2. 创建 to-be-closed 局部变量 (defer) 绑定到闭包
       * 3. 设置 RDKTOCLOSE 并调用 checktoclose
       */
      {
        Proto *defer_proto;
        FuncState new_fs;
        BlockCnt new_bl;
        expdesc b;
        int vidx;
        Vardesc *vd;

        /* 创建闭包包装 defer 体 */
        defer_proto = addprototype(&cg->ls);
        defer_proto->linedefined = s->node.line;

        memset(&new_fs, 0, sizeof(new_fs));
        new_fs.f = defer_proto;
        open_func(&cg->ls, &new_fs, &new_bl);

        new_fs.f->numparams = 0;
        new_fs.f->is_vararg = 0;

        /* 生成 defer 体代码 */
        codegen_block(cg, &s->u.deferstmt.body);

        new_fs.f->lastlinedefined = s->node.line;
        codeclosure(&cg->ls, &b);
        close_func(&cg->ls);

        /* 创建 to-be-closed 局部变量 (defer) */
        {
          TString *defer_name = luaS_newliteral(cg->L, "(defer)");
          vidx = add_local(cg, defer_name, RDKTOCLOSE);
          vd = &cg->dyd->actvar.arr[fs->firstlocal + vidx];
          vd->vd.kind = RDKTOCLOSE;
        }
        activate_locals(cg, 1);

        /* 将闭包赋值给 (defer) 变量 */
        {
          expdesc v;
          v.k = VLOCAL;
          v.f = v.t = NO_JUMP;
          v.u.var.vidx = (unsigned short)vidx;
          v.u.var.ridx = vd->vd.ridx;
          luaK_storevar(fs, &v, &b);
        }

        /* 标记 to-be-closed：确保变量在作用域退出时被关闭 */
        {
          BlockCnt *blk = fs->bl;
          blk->upval = 1;
          blk->insidetbc = 1;
          fs->needclose = 1;
          luaK_codeABC(fs, OP_TBC, vd->vd.ridx, 0, 0);
        }
      }
      break;
    }
    case AST_STMT_NAMESPACE: {
      /**
       * @brief namespace 完整实现
       * 
       * 语法: namespace Name { body }
       * 代码生成逻辑：
       * 1. 调用 OP_NEWNAMESPACE 创建或获取命名空间表
       * 2. 将命名空间表存储到全局变量 Name
       * 3. 创建局部 _ENV 并设置为命名空间表
       * 4. 在新环境中生成 body 代码
       * 5. 退出块时自动恢复原始 _ENV（通过局部变量作用域）
       */
      FuncState *fs = cg_fs(cg);
      BlockCnt bl;
      TString *name = s->u.nsstruct.name;
      expdesc v, ns;
      
      /* 1. 调用 OP_NEWNAMESPACE 创建或获取命名空间 */
      int name_k = luaK_stringK(fs, name);
      init_exp(&ns, VRELOC, luaK_codeABx(fs, OP_NEWNAMESPACE, 0, name_k));
      luaK_exp2nextreg(fs, &ns);
      
      /* 2. 构建全局变量并存储命名空间表 */
      cg_singlevar(cg, name, &v);
      luaK_storevar(fs, &v, &ns);
      
      /* 3. 进入新块 */
      enterblock(fs, &bl, 0);
      
      /* 4. 创建局部 _ENV = ns */
      int vidx = add_local(cg, cg->ls.envn, VDKREG);
      activate_locals(cg, 1);
      fs->freereg = luaY_nvarstack(fs);
      
      /* 5. 赋值 _ENV = ns */
      expdesc env_var;
      cg_init_local(cg, &env_var, vidx);
      luaK_storevar(fs, &env_var, &ns);
      
      /* 6. 在新环境中生成 body 代码 */
      codegen_block(cg, &s->u.nsstruct.body);
      
      /* 7. 退出块（自动恢复原始局部作用域，_ENV 恢复） */
      leaveblock(fs);
      break;
    }
    case AST_STMT_STRUCT: {
      /* struct 完整实现
       * 参考 lparser.c:structstat
       * 语法: struct Name { field1 = val1, field2, ... }
       * 1. 查找 struct.define 函数
       * 2. 构建字段表 {name1, val1, name2, val2, ...}
       * 3. 调用 struct.define("Name", fields_table)
       * 4. 将结果存储到全局变量
       */
      expdesc v;
      TString *name = s->u.nsstruct.name;
      int nentries = s->u.nsstruct.nentries;

      if (nentries == 0) {
        /* 无字段的 struct 定义：执行 body 中的初始化代码 */
        codegen_block(cg, &s->u.nsstruct.body);
        break;
      }

      AstKVPair *entries = s->u.nsstruct.entries;

      /* 1. 查找 struct.define */
      /* 先查找 struct 全局变量 */
      expdesc struct_func;
      {
        TString *struct_name = luaS_newliteral(cg->L, "struct");
        int envidx = cg_get_env_upval(cg);
        init_exp(&struct_func, VUPVAL, envidx);
        expdesc k;
        cg_codestring(&k, struct_name);
        luaK_indexed(fs, &struct_func, &k);
      }
      luaK_exp2nextreg(fs, &struct_func);

      /* 获取 define 字段 */
      {
        expdesc k;
        cg_codestring(&k, luaS_newliteral(cg->L, "define"));
        luaK_indexed(fs, &struct_func, &k);
      }
      luaK_exp2nextreg(fs, &struct_func);
      int func_reg = struct_func.u.info;

      /* 2. 参数1: 名称字符串 */
      {
        expdesc name_arg;
        cg_codestring(&name_arg, name);
        luaK_exp2nextreg(fs, &name_arg);
      }

      /* 3. 参数2: 字段表 */
      int table_reg = fs->freereg;
      int pc = luaK_codeABC(fs, OP_NEWTABLE, table_reg, 0, 0);
      luaK_code(fs, 0);
      luaK_reserveregs(fs, 1);

      int i;
      int tidx = 1; /* 1-based array index */
      for (i = 0; i < nentries; i++) {
        /* 存储字段名 */
        expdesc t_exp;
        init_exp(&t_exp, VNONRELOC, table_reg);
        expdesc idx;
        init_exp(&idx, VKINT, 0);
        idx.u.ival = tidx;
        luaK_indexed(fs, &t_exp, &idx);

        expdesc fname_exp;
        cg_codestring(&fname_exp, entries[i].key->u.strval);
        luaK_storevar(fs, &t_exp, &fname_exp);

        /* 存储字段值 */
        init_exp(&t_exp, VNONRELOC, table_reg);
        idx.u.ival = tidx + 1;
        luaK_indexed(fs, &t_exp, &idx);

        if (entries[i].value) {
          expdesc val_exp;
          codegen_expr(cg, entries[i].value, &val_exp);
          luaK_exp2nextreg(fs, &val_exp);
          luaK_storevar(fs, &t_exp, &val_exp);
        } else {
          /* 无默认值，使用 nil */
          expdesc nil_exp;
          init_exp(&nil_exp, VNIL, 0);
          luaK_exp2nextreg(fs, &nil_exp);
          luaK_storevar(fs, &t_exp, &nil_exp);
        }

        tidx += 2;
      }

      luaK_settablesize(fs, pc, table_reg, tidx - 1, 0);

      /* 4. 调用 struct.define(name, fields_table) */
      init_exp(&v, VCALL, luaK_codeABC(fs, OP_CALL, func_reg, 3, 2));
      fs->freereg = func_reg + 1;

      /* 5. 存储到全局变量 */
      {
        expdesc var;
        int envidx = cg_get_env_upval(cg);
        init_exp(&var, VUPVAL, envidx);
        expdesc k;
        cg_codestring(&k, name);
        luaK_indexed(fs, &var, &k);

        luaK_storevar(fs, &var, &v);
      }

      luaK_fixline(fs, s->node.line);
      break;
    }
    case AST_STMT_SUPERSTRUCT: {
      /* superstruct 完整实现
       * 参考 lparser.c:superstructstat
       * 语法: superstruct Name [ key: val, ... ]
       * 1. 使用 OP_NEWSUPER 创建 superstruct
       * 2. 对每个键值对使用 OP_SETSUPER
       * 3. 将结果存储到全局变量
       */
      expdesc v;
      TString *name = s->u.nsstruct.name;
      int name_k = luaK_stringK(fs, name);

      /* 创建 superstruct */
      init_exp(&v, VRELOC, luaK_codeABx(fs, OP_NEWSUPER, 0, name_k));
      luaK_exp2nextreg(fs, &v);
      int ss_reg = v.u.info;

      /* 遍历键值对，使用 OP_SETSUPER */
      int i;
      for (i = 0; i < s->u.nsstruct.nentries; i++) {
        expdesc key, val;
        codegen_expr(cg, s->u.nsstruct.entries[i].key, &key);
        luaK_exp2nextreg(fs, &key);
        codegen_expr(cg, s->u.nsstruct.entries[i].value, &val);
        luaK_exp2nextreg(fs, &val);
        luaK_codeABC(fs, OP_SETSUPER, ss_reg, key.u.info, val.u.info);
        fs->freereg = ss_reg + 1;
      }

      /* 存储到全局变量 */
      {
        expdesc var;
        int envidx = cg_get_env_upval(cg);
        init_exp(&var, VUPVAL, envidx);
        expdesc key;
        cg_codestring(&key, name);
        luaK_indexed(fs, &var, &key);

        expdesc result;
        init_exp(&result, VNONRELOC, ss_reg);
        luaK_storevar(fs, &var, &result);
      }

      luaK_fixline(fs, s->node.line);
      break;
    }
    case AST_STMT_ENUM: {
      /**
       * @brief enum 完整实现
       * 
       * 语法: enum Name { A, B = 10, C }
       * 代码生成逻辑：
       * 1. 创建新表（OP_NEWTABLE）
       * 2. 遍历每个枚举成员，解析成员名和可选值
       * 3. 自动递增：未赋值的成员值 = 前一个值 + 1
       * 4. 显式赋值：A = expr 设置值
       * 5. 使用 luaK_indexed + luaK_storevar 将成员添加到表中
       * 6. luaK_settablesize 设置表大小
       * 7. 将枚举表存储到全局变量
       * 
       * 参考: lparser.c:enumstat
       */
      FuncState *fs = cg_fs(cg);
      int line = s->node.line;
      expdesc enum_exp, key, val;
      lua_Integer auto_value = 1;  /* 自动递增的枚举值，从1开始 */
      int enum_reg;
      int nh = s->u.enumstmt.nentries;  /* 枚举成员数量 */
      
      /* 1. 创建枚举表（即使为空也创建） */
      enum_reg = fs->freereg;
      int pc = luaK_codeABC(fs, OP_NEWTABLE, enum_reg, 0, 0);
      luaK_code(fs, 0);  /* 为额外参数预留空间 */
      luaK_reserveregs(fs, 1);
      init_exp(&enum_exp, VNONRELOC, enum_reg);
      
      /* 2. 遍历每个枚举成员，同时记录每个成员的值 */
      int i;
      lua_Integer *enum_values = luaM_newvector(cg->L, nh, lua_Integer);
      for (i = 0; i < nh; i++) {
        AstEnumEntry *entry = &s->u.enumstmt.entries[i];
        
        /* 设置键为成员名 */
        cg_codestring(&key, entry->name);
        
        if (entry->value_expr != NULL) {
          /* 显式赋值 */
          expdesc value_exp;
          codegen_expr(cg, entry->value_expr, &value_exp);
          
          /* 尝试获取常量值用于自动递增 */
          if (value_exp.k == VKINT) {
            enum_values[i] = value_exp.u.ival;
            auto_value = value_exp.u.ival + 1;
          } else if (value_exp.k == VKFLT) {
            enum_values[i] = (lua_Integer)value_exp.u.nval;
            auto_value = (lua_Integer)value_exp.u.nval + 1;
          } else {
            /* 非常量表达式，无法确定下一个自动值 */
            enum_values[i] = auto_value;
            auto_value++;
          }
          
          /* 将值放入表中 */
          expdesc tab = enum_exp;
          luaK_indexed(fs, &tab, &key);
          luaK_storevar(fs, &tab, &value_exp);

          /* 非 class 枚举：注册全局变量别名 */
          if (!s->u.enumstmt.is_enum_class) {
            expdesc gvar;
            cg_singlevar(cg, entry->name, &gvar);
            luaK_storevar(fs, &gvar, &value_exp);
          }
        } else {
          /* 自动赋值 */
          enum_values[i] = auto_value;
          init_exp(&val, VKINT, 0);
          val.u.ival = auto_value++;
          
          /* 将值放入表中 */
          expdesc tab = enum_exp;
          luaK_indexed(fs, &tab, &key);
          luaK_storevar(fs, &tab, &val);

          /* 非 class 枚举：注册全局变量别名 */
          if (!s->u.enumstmt.is_enum_class) {
            expdesc gvar;
            cg_singlevar(cg, entry->name, &gvar);
            luaK_storevar(fs, &gvar, &val);
          }
        }
        
        /* 释放临时寄存器 */
        fs->freereg = enum_reg + 1;
      }
      
      /* 3. 设置表大小 */
      luaK_settablesize(fs, pc, enum_reg, 0, nh);
      
      /* 4. 创建反射方法: names, values, kvmap, vkmap 和 _nmembers */
      {
        /* 4.1 创建 _names 数组（注册 enum_reg+1） */
        int names_reg = enum_reg + 1;
        int names_pc = luaK_codeABC(fs, OP_NEWTABLE, names_reg, 0, 0);
        luaK_code(fs, 0);
        luaK_reserveregs(fs, 1);
        expdesc names_exp;
        init_exp(&names_exp, VNONRELOC, names_reg);
        
        /* 4.2 创建 _values 数组（注册 enum_reg+2） */
        int values_reg = enum_reg + 2;
        int values_pc = luaK_codeABC(fs, OP_NEWTABLE, values_reg, 0, 0);
        luaK_code(fs, 0);
        luaK_reserveregs(fs, 1);
        expdesc values_exp;
        init_exp(&values_exp, VNONRELOC, values_reg);
        
        /* 4.3 创建 _vkmap 表（注册 enum_reg+3） */
        int vkmap_reg = enum_reg + 3;
        int vkmap_pc = luaK_codeABC(fs, OP_NEWTABLE, vkmap_reg, 0, 0);
        luaK_code(fs, 0);
        luaK_reserveregs(fs, 1);
        expdesc vkmap_exp;
        init_exp(&vkmap_exp, VNONRELOC, vkmap_reg);
        
        /* 4.4 填充 _names, _values, _vkmap */
        for (i = 0; i < nh; i++) {
          AstEnumEntry *entry = &s->u.enumstmt.entries[i];
          lua_Integer val_i = enum_values[i];
          
          /* _names[i+1] = entry->name */
          {
            expdesc key_idx, name_val;
            init_exp(&key_idx, VKINT, 0);
            key_idx.u.ival = i + 1;
            cg_codestring(&name_val, entry->name);
            expdesc t = names_exp;
            luaK_indexed(fs, &t, &key_idx);
            luaK_storevar(fs, &t, &name_val);
            fs->freereg = enum_reg + 4;
          }
          
          /* _values[i+1] = val_i */
          {
            expdesc key_idx, int_val;
            init_exp(&key_idx, VKINT, 0);
            key_idx.u.ival = i + 1;
            init_exp(&int_val, VKINT, 0);
            int_val.u.ival = val_i;
            expdesc t = values_exp;
            luaK_indexed(fs, &t, &key_idx);
            luaK_storevar(fs, &t, &int_val);
            fs->freereg = enum_reg + 4;
          }
          
          /* _vkmap[val_i] = entry->name */
          {
            expdesc vk_key, vk_val;
            init_exp(&vk_key, VKINT, 0);
            vk_key.u.ival = val_i;
            cg_codestring(&vk_val, entry->name);
            expdesc t = vkmap_exp;
            luaK_indexed(fs, &t, &vk_key);
            luaK_storevar(fs, &t, &vk_val);
            fs->freereg = enum_reg + 4;
          }
        }
        
        /* 4.5 将 _names, _values, _vkmap 存到枚举表上 */
        {
          expdesc key_names, key_values, key_vkmap;
          cg_codestring(&key_names, luaS_new(cg->L, "_names"));
          cg_codestring(&key_values, luaS_new(cg->L, "_values"));
          cg_codestring(&key_vkmap, luaS_new(cg->L, "_vkmap"));
          
          expdesc et = enum_exp;
          luaK_indexed(fs, &et, &key_names);
          luaK_storevar(fs, &et, &names_exp);
          
          et = enum_exp;
          luaK_indexed(fs, &et, &key_values);
          luaK_storevar(fs, &et, &values_exp);
          
          et = enum_exp;
          luaK_indexed(fs, &et, &key_vkmap);
          luaK_storevar(fs, &et, &vkmap_exp);
        }
        
        /* 4.6 存储 _nmembers */
        {
          expdesc key_nm, val_nm;
          cg_codestring(&key_nm, luaS_new(cg->L, "_nmembers"));
          init_exp(&val_nm, VKINT, 0);
          val_nm.u.ival = nh;
          expdesc et = enum_exp;
          luaK_indexed(fs, &et, &key_nm);
          luaK_storevar(fs, &et, &val_nm);
        }
        
        /* 4.7 创建方法闭包: names, values, kvmap, vkmap */
        {
          const char *method_names[] = {"names", "values", "kvmap", "vkmap"};
          const char *field_names[] = {"_names", "_values", NULL, "_vkmap"};
          int j;
          for (j = 0; j < 4; j++) {
            Proto *mp = luaF_newproto(cg->L);
            mp->numparams = 1;
            mp->is_vararg = 0;
            mp->source = fs->f->source;
            mp->linedefined = line;
            mp->lastlinedefined = line;
            
            if (field_names[j] == NULL) {
              /* kvmap: function(self) return self end */
              mp->maxstacksize = 1;
              mp->sizecode = 1;
              mp->code = luaM_newvector(cg->L, 1, Instruction);
              mp->code[0] = CREATE_ABCk(OP_RETURN, 0, 2, 0, 0);
            } else {
              /* names/values/vkmap: function(self) return self._XXX end */
              TString *fname = luaS_new(cg->L, field_names[j]);
              mp->maxstacksize = 2;
              mp->sizecode = 2;
              mp->code = luaM_newvector(cg->L, 2, Instruction);
              mp->code[0] = CREATE_ABCk(OP_GETFIELD, 1, 0, 0, 1);
              mp->code[1] = CREATE_ABCk(OP_RETURN, 1, 2, 0, 0);
              mp->sizek = 1;
              mp->k = luaM_newvector(cg->L, 1, TValue);
              setsvalue(cg->L, &mp->k[0], fname);
            }
            
            /* 添加到父函数的 proto 数组 */
            int bx = fs->np++;
            luaM_growvector(cg->L, fs->f->p, bx, fs->f->sizep, Proto *, MAX_INT, "functions");
            fs->f->p[bx] = mp;
            
            /* 创建闭包并存储到枚举表 */
            expdesc closure;
            init_exp(&closure, VRELOC, luaK_codeABx(fs, OP_CLOSURE, enum_reg + 4, bx));
            luaK_exp2nextreg(fs, &closure);
            
            /* 存储到枚举表: enum[method_name] = closure */
            expdesc mkey;
            TString *mname = luaS_new(cg->L, method_names[j]);
            cg_codestring(&mkey, mname);
            expdesc et = enum_exp;
            luaK_indexed(fs, &et, &mkey);
            luaK_storevar(fs, &et, &closure);
            
            fs->freereg = enum_reg + 4;
          }
        }
        
        /* 释放临时寄存器 */
        fs->freereg = enum_reg + 1;
      }
      
      /* 5. 将枚举表存储到全局变量 */
      if (s->u.enumstmt.name != NULL) {
        expdesc var;
        cg_singlevar(cg, s->u.enumstmt.name, &var);
        init_exp(&enum_exp, VNONRELOC, enum_reg);
        luaK_storevar(fs, &var, &enum_exp);
      }
      
      luaK_fixline(fs, line);
      break;
    }
    case AST_STMT_USING: {
      /**
       * @brief using 完整实现
       * 
       * 语法：
       *   using namespace Name[::Member::...]  -- 导入命名空间
       *   using Name[::Member::...]            -- 链接模块成员
       * 
       * 代码生成逻辑（using namespace）：
       * 1. 解析命名空间引用（通过全局变量或 _ENV）
       * 2. 处理 :: 链路径
       * 3. 调用 OP_LINKNAMESPACE 将命名空间链接到当前 _ENV
       * 
       * 代码生成逻辑（using Name）：
       * 1. 解析名称引用
       * 2. 处理 :: 链路径
       * 3. 创建局部变量并赋值
       */
      FuncState *fs = cg_fs(cg);
      int is_ns = s->u.usingstmt.is_namespace;
      TString *name = s->u.usingstmt.name;
      TString *last_member = s->u.usingstmt.last_member;
      expdesc e;
      
      if (is_ns) {
        /* using namespace Name[::Member::...] */
        /* 解析命名空间引用 */
        cg_singlevar(cg, name, &e);
        
        /* 处理 :: 链 */
        if (last_member != NULL) {
          /* 索引 last_member：AST 中 first 通过 name 解析，last_member 索引访问 */
          luaK_exp2anyregup(fs, &e);
          expdesc key;
          cg_codestring(&key, last_member);
          luaK_indexed(fs, &e, &key);
        }
        
        /* 解析 _ENV */
        expdesc env;
        int envidx = cg_get_env_upval(cg);
        init_exp(&env, VUPVAL, envidx);
        
        /* 将命名空间和 _ENV 放入寄存器 */
        luaK_exp2nextreg(fs, &env);
        luaK_exp2nextreg(fs, &e);
        
        /* OP_LINKNAMESPACE A B: R[A]->using_next = R[B] */
        luaK_codeABC(fs, OP_LINKNAMESPACE, env.u.info, e.u.info, 0);
      } else {
        /* using Name[::Member::...] */
        /* 解析名称引用 */
        cg_singlevar(cg, name, &e);
        
        /* 处理 :: 链 */
        if (last_member != NULL && last_member != name) {
          luaK_exp2anyregup(fs, &e);
          expdesc key;
          cg_codestring(&key, last_member);
          luaK_indexed(fs, &e, &key);
        }
        
        /* 创建局部变量并赋值 */
        int vidx = add_local(cg, last_member ? last_member : name, VDKREG);
        activate_locals(cg, 1);
        fs->freereg = luaY_nvarstack(fs);
        
        expdesc var;
        cg_init_local(cg, &var, vidx);
        luaK_storevar(fs, &var, &e);
      }
      break;
    }
    case AST_STMT_THROW: {
      /* throw 实现（通过调用 error() 抛出异常）
       * 语法：throw expr;
       * 实现：调用 error(expr) 抛出运行时错误
       */
      {
        expdesc error_func, err_expr;

        /* 获取 error 全局函数 */
        cg_singlevar(cg, luaS_newliteral(cg->L, "error"), &error_func);
        luaK_exp2nextreg(fs, &error_func);
        int base = error_func.u.info;

        if (s->u.throwstmt.expr != NULL) {
          /* 计算错误表达式并放入下一个寄存器 */
          codegen_expr(cg, s->u.throwstmt.expr, &err_expr);
          luaK_exp2nextreg(fs, &err_expr);
        } else {
          /* 无表达式：传入 nil */
          luaK_nil(fs, fs->freereg, 1);
          luaK_reserveregs(fs, 1);
        }

        /* 调用 error(expr) */
        luaK_codeABC(fs, OP_CALL, base, 2, 1);
        fs->freereg = cast_byte(base + 1);
      }
      break;
    }
    case AST_STMT_CLASS: {
      /* class 完整代码生成
       * 参考 lparser.c:classstat 逻辑：
       *   1. 评估装饰器表达式（如果有）
       *   2. OP_NEWCLASS 创建类表
       *   3. 处理类修饰符（abstract/final/sealed）→ 设置 __flags 字段
       *   4. 处理继承（extends）→ OP_INHERIT
       *   5. 处理接口实现（implements）→ OP_IMPLEMENT
       *   6. 处理 trait 混入（use）→ OP_USETRAIT
       *   7. 类体成员通过 members 数组生成
       *   8. 应用装饰器到类（如果有）
       *   9. 将类存储到全局变量
       */
      TString *classname = s->u.classstmt.name;
      int class_flags = s->u.classstmt.class_flags;
      int class_reg;
      int i;
      int ndec = s->ndecorators;
      int dec_base = 0;

      /* 0. 评估装饰器表达式到寄存器（如果有的话） */
      if (ndec > 0) {
        dec_base = fs->freereg;
        for (i = 0; i < ndec; i++) {
          expdesc dec;
          codegen_expr(cg, s->decorators[i], &dec);
          luaK_exp2nextreg(fs, &dec);
        }
      }

      /* 1. 创建类表 - OP_NEWCLASS: R[class_reg] = newclass(K[classname]) */
      class_reg = fs->freereg;
      luaK_reserveregs(fs, 1);
      {
        int classname_k = luaK_stringK(fs, classname);
        luaK_codeABx(fs, OP_NEWCLASS, class_reg, classname_k);
      }

      /* 2. 处理类修饰符（abstract/final/sealed）→ 设置 __flags 字段 */
      if (class_flags != 0) {
        TString *flags_ts = luaS_newliteral(cg->L, "__flags");
        int flags_k = luaK_stringK(fs, flags_ts);
        int flags_reg = fs->freereg;
        luaK_reserveregs(fs, 1);
        /* 读取当前 flags: R[flags_reg] = R[class_reg].__flags */
        luaK_codeABC(fs, OP_GETFIELD, flags_reg, class_reg, flags_k);
        /* flags |= class_flags */
        luaK_codeABx(fs, OP_LOADI, fs->freereg, class_flags);
        luaK_reserveregs(fs, 1);
        luaK_codeABC(fs, OP_BOR, flags_reg, flags_reg, fs->freereg - 1);
        /* 写回 flags: R[class_reg].__flags = R[flags_reg] */
        luaK_codeABC(fs, OP_SETFIELD, class_reg, flags_k, flags_reg);
        fs->freereg = class_reg + 1;  /* 释放临时寄存器 */
      }

      /* 3. 处理继承（extends）→ OP_INHERIT */
      if (s->u.classstmt.extends_name != NULL) {
        expdesc parent_exp;
        cg_singlevar(cg, s->u.classstmt.extends_name, &parent_exp);
        luaK_exp2nextreg(fs, &parent_exp);
        luaK_codeABC(fs, OP_INHERIT, class_reg, parent_exp.u.info, 0);
        fs->freereg--;  /* 释放父类寄存器 */
      }

      /* 4. 处理接口实现（implements）→ OP_IMPLEMENT */
      for (i = 0; i < s->u.classstmt.nimplements; i++) {
        expdesc iface_exp;
        cg_singlevar(cg, s->u.classstmt.implements[i], &iface_exp);
        luaK_exp2nextreg(fs, &iface_exp);
        luaK_codeABC(fs, OP_IMPLEMENT, class_reg, iface_exp.u.info, 0);
        fs->freereg--;
      }

      /* 5. 处理 trait 混入（use）→ OP_USETRAIT */
      for (i = 0; i < s->u.classstmt.nuse_traits; i++) {
        expdesc trait_exp;
        cg_singlevar(cg, s->u.classstmt.use_traits[i], &trait_exp);
        luaK_exp2nextreg(fs, &trait_exp);
        luaK_codeABC(fs, OP_USETRAIT, class_reg, trait_exp.u.info, 0);
        fs->freereg--;
      }

      /* 6. 生成类体代码：处理结构化成员 */
      if (s->u.classstmt.members != NULL) {
        int j;
        for (j = 0; j < s->u.classstmt.nmembers; j++) {
          AstClassMember *m = &s->u.classstmt.members[j];
          int name_k = luaK_stringK(fs, m->name);

          switch (m->kind) {
            case AST_MEMBER_METHOD:
            case AST_MEMBER_FINAL:
            case AST_MEMBER_ABSTRACT:
            case AST_MEMBER_GETTER:
            case AST_MEMBER_SETTER: {
              /* 生成方法闭包 */
              if (m->u.method_func != NULL) {
                Proto *p = codegen_func(cg, m->u.method_func);
                int bx = fs->np++;
                int oldsize;
                if (bx >= fs->f->sizep) {
                  oldsize = fs->f->sizep;
                  luaM_growvector(cg->L, fs->f->p, bx + 1, fs->f->sizep,
                                  Proto *, MAXARG_Bx, "functions");
                  while (oldsize < fs->f->sizep)
                    fs->f->p[oldsize++] = NULL;
                }
                fs->f->p[bx] = p;
                luaC_objbarrier(cg->L, fs->f, p);
                expdesc v;
                init_exp(&v, VRELOC, luaK_codeABx(fs, OP_CLOSURE, 0, bx));
                luaK_exp2nextreg(fs, &v);
                /* 存储到类表 */
                luaK_codeABC(fs, OP_SETFIELD, class_reg, name_k, v.u.info);
                fs->freereg--;
              }
              break;
            }

            case AST_MEMBER_PROPERTY: {
              /* 属性初始化 */
              if (m->u.property_value != NULL) {
                expdesc val;
                codegen_expr(cg, m->u.property_value, &val);
                luaK_exp2nextreg(fs, &val);
                luaK_codeABC(fs, OP_SETFIELD, class_reg, name_k, val.u.info);
                fs->freereg--;
              }
              break;
            }
          }
        }
      } else if (s->u.classstmt.body.items != NULL) {
        /* 兼容旧格式：从 body 中提取方法 */
        int i;
        for (i = 0; i < s->u.classstmt.body.count; i++) {
          AstStmt *stmt = s->u.classstmt.body.items[i];
          /* 提取方法名和函数体（支持 LOCAL_FUNC 和 ASSIGN 两种形式） */
          TString *method_name = NULL;
          AstFunc *method_func = NULL;
          if (stmt->kind == AST_STMT_LOCAL_FUNC) {
            method_name = stmt->u.localfunc.name;
            method_func = stmt->u.localfunc.func;
          } else if (stmt->kind == AST_STMT_ASSIGN
                     && stmt->u.assign.ntargets == 1
                     && stmt->u.assign.nvalues == 1
                     && stmt->u.assign.targets[0].kind == AST_TGT_VAR
                     && stmt->u.assign.values[0]->kind == AST_EXPR_FUNC_EXPR) {
            method_name = stmt->u.assign.targets[0].as.var.name;
            method_func = stmt->u.assign.values[0]->u.func.func;
          }
          if (method_name != NULL) {
            /* 类方法：生成函数闭包并存储到类表 */
            int name_k = luaK_stringK(fs, method_name);
            expdesc v;
            /* 生成子函数闭包 */
            {
              Proto *p = codegen_func(cg, method_func);
              int bx = fs->np++;
              int oldsize;
              if (bx >= fs->f->sizep) {
                oldsize = fs->f->sizep;
                luaM_growvector(cg->L, fs->f->p, bx + 1, fs->f->sizep,
                                Proto *, MAXARG_Bx, "functions");
                while (oldsize < fs->f->sizep)
                  fs->f->p[oldsize++] = NULL;
              }
              fs->f->p[bx] = p;
              luaC_objbarrier(cg->L, fs->f, p);
              init_exp(&v, VRELOC, luaK_codeABx(fs, OP_CLOSURE, 0, bx));
              luaK_exp2nextreg(fs, &v);
            }
            /* 存储到类表：R[class_reg].method_name = closure */
            luaK_codeABC(fs, OP_SETFIELD, class_reg, name_k, v.u.info);
            fs->freereg--;  /* 释放闭包寄存器 */
          } else {
            /* 其他语句：正常生成 */
            codegen_stmt(cg, stmt);
          }
        }
      }

      /* 7. 应用装饰器到类（如果有的话） */
      /* 装饰器调用顺序：dec1(dec2(class))，从最后一个装饰器开始反向调用 */
      if (ndec > 0) {
        int target_reg = class_reg;
        for (i = ndec - 1; i >= 0; i--) {
          int d_reg = dec_base + i;
          int call_base = fs->freereg;
          luaK_reserveregs(fs, 2);
          luaK_codeABC(fs, OP_MOVE, call_base, d_reg, 0);
          luaK_codeABC(fs, OP_MOVE, call_base + 1, target_reg, 0);
          luaK_codeABC(fs, OP_CALL, call_base, 2, 2);
          luaK_codeABC(fs, OP_MOVE, target_reg, call_base, 0);
          fs->freereg -= 2;
        }
      }

      /* 8. 将类存储到全局变量 */
      {
        expdesc v, class_exp;
        cg_singlevar(cg, classname, &v);
        init_exp(&class_exp, VNONRELOC, class_reg);
        luaK_storevar(fs, &v, &class_exp);
      }

      luaK_fixline(fs, s->node.line);
      break;
    }
    case AST_STMT_TRAIT: {
      /* trait 代码生成
       * 创建 trait 表，生成方法体，然后存储到全局变量
       * 参考: lparser.c:traitstat
       */
      {
        int trait_reg = fs->freereg;
        /* 创建 trait 表 */
        luaK_codeABC(fs, OP_NEWTABLE, trait_reg, 0, 0);
        luaK_code(fs, 0);
        luaK_reserveregs(fs, 1);

        /* 设置 trait 标志 */
        luaK_codeABC(fs, OP_SETTRAITFLAG, trait_reg, 0, 0);

        /* 生成 trait 体代码：方法定义存储为 trait 表字段 */
        if (s->u.nsstruct.body.items != NULL) {
          int i;
          for (i = 0; i < s->u.nsstruct.body.count; i++) {
            AstStmt *stmt = s->u.nsstruct.body.items[i];
            TString *method_name = NULL;
            AstFunc *method_func = NULL;
            if (stmt->kind == AST_STMT_LOCAL_FUNC) {
              method_name = stmt->u.localfunc.name;
              method_func = stmt->u.localfunc.func;
            } else if (stmt->kind == AST_STMT_ASSIGN
                       && stmt->u.assign.ntargets == 1
                       && stmt->u.assign.nvalues == 1
                       && stmt->u.assign.targets[0].kind == AST_TGT_VAR
                       && stmt->u.assign.values[0]->kind == AST_EXPR_FUNC_EXPR) {
              method_name = stmt->u.assign.targets[0].as.var.name;
              method_func = stmt->u.assign.values[0]->u.func.func;
            }
            if (method_name != NULL) {
              /* trait 方法：生成函数闭包并存储到 trait 表 */
              int name_k = luaK_stringK(fs, method_name);
              expdesc v;
              {
                Proto *p = codegen_func(cg, method_func);
                int bx = fs->np++;
                int oldsize;
                if (bx >= fs->f->sizep) {
                  oldsize = fs->f->sizep;
                  luaM_growvector(cg->L, fs->f->p, bx + 1, fs->f->sizep,
                                  Proto *, MAXARG_Bx, "functions");
                  while (oldsize < fs->f->sizep)
                    fs->f->p[oldsize++] = NULL;
                }
                fs->f->p[bx] = p;
                luaC_objbarrier(cg->L, fs->f, p);
                init_exp(&v, VRELOC, luaK_codeABx(fs, OP_CLOSURE, 0, bx));
                luaK_exp2nextreg(fs, &v);
              }
              luaK_codeABC(fs, OP_SETFIELD, trait_reg, name_k, v.u.info);
              fs->freereg--;
            } else {
              codegen_stmt(cg, stmt);
            }
          }
        }

        /* 存储到全局变量 */
        {
          expdesc var, trait_exp;
          cg_singlevar(cg, s->u.nsstruct.name, &var);
          init_exp(&trait_exp, VNONRELOC, trait_reg);
          luaK_storevar(fs, &var, &trait_exp);
        }
      }
      break;
    }
    case AST_STMT_INTERFACE: {
      /* interface 代码生成
       * 创建接口表，生成方法体，然后存储到全局变量
       * 参考: lparser.c:interfacestat
       */
      {
        int iface_reg = fs->freereg;
        /* 创建接口表 */
        luaK_codeABC(fs, OP_NEWTABLE, iface_reg, 0, 0);
        luaK_code(fs, 0);
        luaK_reserveregs(fs, 1);

        /* 设置接口标志 */
        luaK_codeABC(fs, OP_SETIFACEFLAG, iface_reg, 0, 0);

        /* 生成接口体代码：方法定义存储为接口表字段 */
        if (s->u.nsstruct.body.items != NULL) {
          int i;
          for (i = 0; i < s->u.nsstruct.body.count; i++) {
            AstStmt *stmt = s->u.nsstruct.body.items[i];
            TString *method_name = NULL;
            AstFunc *method_func = NULL;
            if (stmt->kind == AST_STMT_LOCAL_FUNC) {
              method_name = stmt->u.localfunc.name;
              method_func = stmt->u.localfunc.func;
            } else if (stmt->kind == AST_STMT_ASSIGN
                       && stmt->u.assign.ntargets == 1
                       && stmt->u.assign.nvalues == 1
                       && stmt->u.assign.targets[0].kind == AST_TGT_VAR
                       && stmt->u.assign.values[0]->kind == AST_EXPR_FUNC_EXPR) {
              method_name = stmt->u.assign.targets[0].as.var.name;
              method_func = stmt->u.assign.values[0]->u.func.func;
            }
            if (method_name != NULL) {
              /* 接口方法：生成函数闭包并存储到接口表 */
              int name_k = luaK_stringK(fs, method_name);
              expdesc v;
              {
                Proto *p = codegen_func(cg, method_func);
                int bx = fs->np++;
                int oldsize;
                if (bx >= fs->f->sizep) {
                  oldsize = fs->f->sizep;
                  luaM_growvector(cg->L, fs->f->p, bx + 1, fs->f->sizep,
                                  Proto *, MAXARG_Bx, "functions");
                  while (oldsize < fs->f->sizep)
                    fs->f->p[oldsize++] = NULL;
                }
                fs->f->p[bx] = p;
                luaC_objbarrier(cg->L, fs->f, p);
                init_exp(&v, VRELOC, luaK_codeABx(fs, OP_CLOSURE, 0, bx));
                luaK_exp2nextreg(fs, &v);
              }
              luaK_codeABC(fs, OP_SETFIELD, iface_reg, name_k, v.u.info);
              fs->freereg--;
            } else {
              codegen_stmt(cg, stmt);
            }
          }
        }

        /* 存储到全局变量 */
        {
          expdesc var, iface_exp;
          cg_singlevar(cg, s->u.nsstruct.name, &var);
          init_exp(&iface_exp, VNONRELOC, iface_reg);
          luaK_storevar(fs, &var, &iface_exp);
        }
      }
      break;
    }
    case AST_STMT_MATCH: {
      codegen_match_body(cg, s, NULL);
      break;
    }
    case AST_STMT_WITH: {
      /* with 语句代码生成
       * 创建 __with_target__ 和 __with_saved_env__ 局部变量，
       * 调用 __with_create_env__ 创建新环境，设置 _ENV，
       * 执行体代码后恢复 _ENV
       * 参考: lparser.c:withstat
       */
      {
        int target_reg, saved_env_reg, base;
        expdesc target_exp, env_var, func_exp, result;

        /* 生成目标表达式代码 */
        codegen_expr(cg, s->u.withstmt.target, &target_exp);
        luaK_exp2nextreg(fs, &target_exp);
        target_reg = fs->freereg - 1;

        /* 创建 __with_saved_env__：保存当前 _ENV */
        cg_singlevar(cg, cg->ls.envn, &env_var);
        luaK_exp2nextreg(fs, &env_var);
        saved_env_reg = fs->freereg - 1;

        /* 获取 __with_create_env__ 函数 */
        cg_singlevaraux(cg, fs, luaS_newliteral(cg->L, "__with_create_env__"), &func_exp, 1);
        if (func_exp.k == VVOID) {
          /* 从 _ENV 中获取 */
          expdesc key;
          expdesc env;
          cg_singlevaraux(cg, fs, cg->ls.envn, &env, 1);
          cg_codestring(&key, luaS_newliteral(cg->L, "__with_create_env__"));
          luaK_indexed(fs, &env, &key);
          func_exp = env;
        }
        luaK_exp2nextreg(fs, &func_exp);
        base = func_exp.u.info;

        /* 参数1: __with_target__ */
        /* 使用 MOVE 指令直接复制到参数寄存器，避免 VNONRELOC 的 freeexp 副作用 */
        luaK_codeABC(fs, OP_MOVE, fs->freereg, target_reg, 0);
        fs->freereg++;

        /* 参数2: __with_saved_env__ */
        luaK_codeABC(fs, OP_MOVE, fs->freereg, saved_env_reg, 0);
        fs->freereg++;

        /* 调用 __with_create_env__(target, saved_env) */
        luaK_codeABC(fs, OP_CALL, base, 3, 2);
        fs->freereg = base + 1;

        /* 将返回值赋给 _ENV */
        {
          expdesc env_dst;
          init_exp(&result, VNONRELOC, base);
          cg_singlevar(cg, cg->ls.envn, &env_dst);
          luaK_storevar(fs, &env_dst, &result);
        }

        /* 生成体代码 */
        codegen_block(cg, &s->u.withstmt.body);

        /* 恢复 _ENV = __with_saved_env__ */
        {
          expdesc env_dst, saved_val;
          init_exp(&saved_val, VNONRELOC, saved_env_reg);
          luaK_exp2anyreg(fs, &saved_val);
          cg_singlevar(cg, cg->ls.envn, &env_dst);
          luaK_storevar(fs, &env_dst, &saved_val);
        }
      }
      break;
    }
    case AST_STMT_ASM: {
      /* asm 内联汇编：使用原始文本创建临时 LexState 并发射字节码 */
      TString *raw_body = s->u.asmstmt.raw_body;
      if (raw_body && tsslen(raw_body) > 0) {
        const char *body_text = getstr(raw_body);
        size_t body_len = tsslen(raw_body);

        /* 创建临时 LexState */
        struct lexer_state {
          LexState ls;
          ZIO z;
          Mbuffer buff;
          struct Dyndata dyd;
        };
        struct lexer_state *state = cast(struct lexer_state *,
          luaM_new(cg->L, struct lexer_state));
        memset(state, 0, sizeof(*state));

        LexState *asm_ls = &state->ls;
        asm_ls->L = cg->L;
        asm_ls->fs = cg->fs;
        asm_ls->source = cg->ls.source;
        asm_ls->envn = cg->ls.envn;
        asm_ls->linenumber = s->node.line;

        /* 设置 ZIO 直接从内存读取 */
        luaZ_init(cg->L, &state->z, NULL, NULL);
        state->z.p = body_text;
        state->z.n = body_len;
        asm_ls->z = &state->z;

        /* 初始化 buffer */
        luaZ_initbuffer(cg->L, &state->buff);
        asm_ls->buff = &state->buff;

        /* 初始化 dyndata */
        asm_ls->dyd = &state->dyd;
        memset(&state->dyd, 0, sizeof(state->dyd));

        /* 初始化词法分析器 */
        luaX_next(asm_ls);

        /* 调用 asm 执行引擎 */
        lasm_execute(asm_ls, cg->fs, s->node.line);

        /* 清理 */
        luaZ_freebuffer(cg->L, &state->buff);
        luaM_free(cg->L, state);
      }
      break;
    }
    case AST_STMT_CONCEPT: {
      /* concept 代码生成：编译 body 并注册 concept
       * 参考 lparser.c:conceptstat 第 10682-10729 行 */
      int prev_np = cg_fs(cg)->np;  /* 记录 body 生成前的 proto 数量 */
      codegen_block(cg, &s->u.nsstruct.body);

      /* 注册 concept：生成 OP_NEWCONCEPT 指令 */
      if (s->u.nsstruct.name) {
        FuncState *fs = cg_fs(cg);
        int new_np = fs->np;
        if (new_np > prev_np) {
          /* 取 body 中最后一个函数的 Proto，注册 concept */
          int bx = new_np - 1;
          expdesc concept_val;
          init_exp(&concept_val, VRELOC, luaK_codeABx(fs, OP_NEWCONCEPT, fs->freereg, bx));
          luaK_exp2nextreg(fs, &concept_val);
        }
      }
      break;
    }
    case AST_STMT_COMMAND: {
      /* command 代码生成：生成 body 代码并注册到 _CMDS 表
       * 参考 lparser.c:commandstat 第 10489-10504 行 */
      codegen_block(cg, &s->u.nsstruct.body);

      /* 将命令名注册到 _CMDS 运行时表: _CMDS[命令名] = true */
      if (s->u.nsstruct.name) {
        FuncState *fs = cg_fs(cg);
        expdesc cmds_table, key_exp, val_exp;
        init_exp(&cmds_table, VNONRELOC, fs->freereg);
        luaK_codeABC(fs, OP_GETCMDS, fs->freereg, 0, 0);
        luaK_reserveregs(fs, 1);
        luaK_exp2anyregup(fs, &cmds_table);
        cg_codestring(&key_exp, s->u.nsstruct.name);
        init_exp(&val_exp, VTRUE, 0);
        luaK_indexed(fs, &cmds_table, &key_exp);
        luaK_storevar(fs, &cmds_table, &val_exp);
      }
      break;
    }
    case AST_STMT_KEYWORD: {
      /* keyword 代码生成：生成 body 代码并注册到编译时 keyword 注册表
       * 参考 lparser.c:keywordstat 第 10521-10573 行 */
      int prev_np = cg_fs(cg)->np;  /* 记录 body 生成前的 proto 数量 */
      codegen_block(cg, &s->u.nsstruct.body);

      /* 将 keyword 函数 Proto 注册到编译时注册表 */
      if (s->u.nsstruct.name) {
        FuncState *fs = cg_fs(cg);
        int new_np = fs->np;
        if (new_np > prev_np) {
          /* 取 body 中最后一个函数（即 keyword 函数）的 Proto */
          Proto *kwproto = fs->f->p[new_np - 1];

          /* keyword 不能捕获 upvalue */
          if (kwproto->sizeupvalues > 0) {
            luaX_syntaxerror(&cg->ls,
              "keyword cannot capture upvalues (use parameters instead of outer variables)");
          }

          /* 注册到全局 keyword 编译时注册表 */
          global_State *g = G(cg->L);
          int i;
          for (i = 0; i < g->kwreg_count; i++) {
            if (g->keyword_registry[i].name == s->u.nsstruct.name) {
              g->keyword_registry[i].p = kwproto;
              break;
            }
          }
          if (i >= g->kwreg_count) {
            /* 动态扩容 */
            if (g->kwreg_count >= g->kwreg_size) {
              int newsize = (g->kwreg_size == 0) ? 8 : g->kwreg_size * 2;
              g->keyword_registry = luaM_reallocvector(
                  cg->L, g->keyword_registry, g->kwreg_size, newsize, KeywordRegEntry);
              g->kwreg_size = newsize;
            }
            g->keyword_registry[g->kwreg_count].name = s->u.nsstruct.name;
            g->keyword_registry[g->kwreg_count].p = kwproto;
            g->kwreg_count++;
          }
        }
      }
      break;
    }
    case AST_STMT_OPERATOR: {
      /* operator 代码生成：生成 body 代码并注册到 _OPERATORS 表
       * 参考 lparser.c:operatorstat 第 10588-10679 行 */
      int prev_np = cg_fs(cg)->np;  /* 记录 body 生成前的 proto 数量 */
      codegen_block(cg, &s->u.nsstruct.body);

      /* 将运算符函数注册到 _OPERATORS 运行时表 */
      if (s->u.nsstruct.name) {
        FuncState *fs = cg_fs(cg);
        int new_np = fs->np;
        if (new_np > prev_np) {
          /* 取 body 中最后一个函数的 Proto，创建闭包 */
          int bx = new_np - 1;
          expdesc closure;
          init_exp(&closure, VRELOC, luaK_codeABx(fs, OP_CLOSURE, fs->freereg, bx));
          luaK_exp2nextreg(fs, &closure);

          /* 存储到 _OPERATORS[运算符名] = 闭包 */
          expdesc ops_table, key_exp;
          init_exp(&ops_table, VNONRELOC, fs->freereg);
          luaK_codeABC(fs, OP_GETOPS, fs->freereg, 0, 0);
          luaK_reserveregs(fs, 1);
          luaK_exp2anyregup(fs, &ops_table);
          cg_codestring(&key_exp, s->u.nsstruct.name);
          luaK_indexed(fs, &ops_table, &key_exp);
          luaK_storevar(fs, &ops_table, &closure);
        }
      }
      break;
    }
    case AST_STMT_GLOBAL: {
      /* global 变量声明：将值存储到 _ENV[name]
       * 参考 lparser.c:globalstat + globalnames + initglobal
       */
      int i;
      int nnames = s->u.global.nnames;
      int nvalues = s->u.global.nvalues;

      if (s->u.global.has_wildcard) {
        /* global * — 通配符全局声明，创建无名称变量描述符 */
        TString *wc_name = luaS_new(cg->L, "(global wildcard)");
        add_local(cg, wc_name, VDKREG);
        activate_locals(cg, 1);
        break;
      }

      if (nnames == 0) break;

      /* 先评估所有值到寄存器 */
      expdesc *vals = NULL;
      if (nvalues > 0) {
        vals = cast(expdesc *, luaM_newvector(cg->L, nvalues, expdesc));
        for (i = 0; i < nvalues; i++) {
          codegen_expr(cg, s->u.global.values[i], &vals[i]);
          luaK_exp2nextreg(fs, &vals[i]);
        }
      }

      /* 从最后一个名称开始，逐个将值存储到 _ENV[name]
       * 与 lparser.c 的 initglobal 递归模式一致：值从栈顶弹出 */
      for (i = nnames - 1; i >= 0; i--) {
        TString *name = s->u.global.names[i];

        /* 构建 _ENV[name] 表达式 */
        expdesc var, key;
        int envidx = cg_get_env_upval(cg);
        init_exp(&var, VUPVAL, envidx);
        cg_codestring(&key, name);
        luaK_indexed(fs, &var, &key);

        /* 存储值：如果值不够，忽略 */
        if (i < nvalues) {
          /* 值在栈顶（freereg - 1），使用 storevartop 模式 */
          expdesc val;
          init_exp(&val, VNONRELOC, fs->freereg - 1);
          luaK_storevar(fs, &var, &val);
        }
      }

      if (vals) luaM_freearray(cg->L, vals, nvalues);
      break;
    }
    case AST_STMT_TAKE: {
      /* 解构赋值代码生成
       * 支持表解构 local {a, b} = t 和数组解构 local [a, b] = t
       * 支持默认值 local {a, b = default} = t
       */
      int nvars = s->u.take.nvars;
      int is_array = s->u.take.is_array;
      AstExpr **defaults = s->u.take.defaults;
      int i;
      expdesc src;

      /* 将值生成到连续寄存器，从第一个空闲局部变量寄存器开始 */
      int base = luaY_nvarstack(fs);
      /* 源表放在值区域上方，避免被覆盖 */
      fs->freereg = base + nvars;

      /* 生成源表达式到寄存器 */
      if (s->u.take.source) {
        codegen_expr(cg, s->u.take.source, &src);
        luaK_exp2nextreg(fs, &src);
      } else {
        init_exp(&src, VNIL, 0);
        luaK_exp2nextreg(fs, &src);
      }
      int src_reg = src.u.info;

      /* 为每个变量从表中提取值，直接生成 GETFIELD/GETI 指令 */
      for (i = 0; i < nvars; i++) {
        int target_reg = base + i;

        if (is_array) {
          /* 数组解构：使用整数索引 OP_GETI */
          luaK_codeABC(fs, OP_GETI, target_reg, src_reg, i + 1);
        } else {
          /* 表解构：使用字符串键 OP_GETFIELD */
          TString *name = s->u.take.varnames[i];
          int key_idx = luaK_stringK(fs, name);
          luaK_codeABC(fs, OP_GETFIELD, target_reg, src_reg, key_idx);
        }

        if (defaults && defaults[i]) {
          expdesc val;
          init_exp(&val, VNONRELOC, target_reg);
          /* 检查值是否为 nil，nil 时使用默认值 */
          luaK_goifnil(fs, &val);
          /* nil 路径：生成默认值并写入同一寄存器 */
          {
            expdesc def;
            codegen_expr(cg, defaults[i], &def);
            luaK_exp2reg(fs, &def, target_reg);
          }
          luaK_patchtohere(fs, val.t);
        }

        add_local(cg, s->u.take.varnames[i], VDKREG);
      }
      activate_locals(cg, nvars);
      break;
    }
    case AST_STMT_CONSTEXPR: {
      /* constexpr 预处理在解析阶段已完成，codegen 中无需处理 */
      break;
    }
    case AST_STMT_SWITCH: {
      /* switch 语句代码生成
       * 语法: switch expr {do|then|:|{ } case val1 => body, case val2 => body, ... }
       * 对每个 case 值生成相等比较，匹配成功则跳转到对应 body
       */
      FuncState *fs = cg_fs(cg);
      AstExpr *control = s->u.switchstmt.cond;
      int ncases = s->u.switchstmt.ncases;
      AstSwitchCase *cases = s->u.switchstmt.cases;
      int has_default = s->u.switchstmt.has_default;

      if (control == NULL || (ncases == 0 && !has_default)) break;

      /* 生成控制表达式到寄存器 */
      expdesc ctrl;
      codegen_expr(cg, control, &ctrl);
      luaK_exp2nextreg(fs, &ctrl);
      int ctrl_reg = ctrl.u.info;

      /* 跳转到第一个 case 比较 */
      int jump_to_check = luaK_jump(fs);
      int finish_jump = NO_JUMP;

      int i;
      for (i = 0; i < ncases; i++) {
        AstSwitchCase *ac = &cases[i];
        int next_check_jump = NO_JUMP;
        int success_jump = NO_JUMP;

        /* 修补上一个检查跳转 */
        luaK_patchtohere(fs, jump_to_check);

        /* 对每个模式值生成相等比较: 控制值 == case 模式值 */
        for (int p = 0; p < ac->npatterns; p++) {
          expdesc val, cmp;
          codegen_expr(cg, ac->patterns[p], &val);
          init_exp(&cmp, VNONRELOC, ctrl_reg);
          luaK_infix(fs, OPR_EQ, &cmp);
          luaK_posfix(fs, OPR_EQ, &cmp, &val, 0);
          luaK_goiftrue(fs, &cmp);
          luaK_concat(fs, &success_jump, luaK_jump(fs));  /* 匹配成功，跳转到 body */
          /* 不匹配时修补到下一个检查位置 */
          luaK_patchtohere(fs, cmp.f);
        }
        /* 所有模式都不匹配时跳到下一个 case */
        next_check_jump = luaK_jump(fs);

        /* 修补成功跳转，生成 body 代码 */
        luaK_patchtohere(fs, success_jump);
        codegen_block(cg, &ac->body);

        /* 跳转到 switch 结束 */
        luaK_concat(fs, &finish_jump, luaK_jump(fs));

        jump_to_check = next_check_jump;
      }

      /* 处理 default 分支 */
      if (has_default) {
        luaK_patchtohere(fs, jump_to_check);
        codegen_block(cg, &s->u.switchstmt.default_body);
        jump_to_check = NO_JUMP;
      }

      /* 修补所有剩余的检查跳转 */
      if (jump_to_check != NO_JUMP) {
        luaK_patchtohere(fs, jump_to_check);
      }

      /* 修补所有 finish 跳转 */
      if (finish_jump != NO_JUMP) {
        luaK_patchtohere(fs, finish_jump);
      }
      break;
    }
    default: {
      cg_error(cg, "statement kind %d not yet implemented", s->kind);
      break;
    }
  }
}


/* ========== 函数代码生成 ========== */

/*
** 为当前函数创建_ENV upvalue
*/
static void cg_create_env_upval(CodegenState *cg) {
  FuncState *fs = cg_fs(cg);
  Proto *f = fs->f;
  lua_State *L = cg->L;
  Upvaldesc *up;
  int oldsize;
  if (fs->nups >= 1) return;  /* 已有_ENV */
  /* 为main函数创建_ENV upvalue（与原mainfunc行为一致：instack=1, idx=0） */
  oldsize = f->sizeupvalues;
  luaM_growvector(L, f->upvalues, fs->nups + 1, f->sizeupvalues,
                  Upvaldesc, MAXUPVAL, "upvalues");
  while (oldsize < f->sizeupvalues)
    f->upvalues[oldsize++].name = NULL;
  up = &f->upvalues[fs->nups++];
  up->instack = 1;
  up->idx = 0;
  up->kind = VDKREG;
  up->name = luaS_new(L, LUA_ENV);
  luaC_objbarrier(L, f, up->name);
}


/*
** 递归生成函数Proto
** 参数：
**   cg - CodegenState指针
**   f - AST函数节点
** 返回值：生成的Proto
*/
static Proto *codegen_func(CodegenState *cg, AstFunc *f) {
  Proto *proto;
  FuncState fsrec, *prevfs = cg->ls.fs;
  BlockCnt blrec;
  int saved_firstlocal, saved_nactvar, saved_firstlabel;
  int saved_label_n;
  TString **saved_label_names = NULL;
  int *saved_label_pcs = NULL;
  int saved_loop_depth = cg->loop_depth;
  int i;

  /* 创建新Proto */
  proto = luaF_newproto(cg->L);
  proto->linedefined = f->line_defined;
  proto->source = f->source;
  if (f->source) {
    luaC_objbarrier(cg->L, proto, f->source);
  }

  /* 初始化FuncState */
  memset(&fsrec, 0, sizeof(fsrec));
  fsrec.f = proto;

  /* 保存并重置label/goto状态 */
  saved_label_n = cg->labels.n;
  saved_label_names = cg->labels.names;
  saved_label_pcs = cg->labels.pcs;
  cg->labels.names = NULL;
  cg->labels.pcs = NULL;
  cg->labels.n = 0;
  cg->labels.size = 0;
  cg->gotos.names = NULL;
  cg->gotos.pcs = NULL;
  cg->gotos.n = 0;
  cg->gotos.size = 0;
  cg->loop_depth = 0;

  /* 保存actvar状态 */
  saved_firstlocal = cg->dyd->actvar.n;
  saved_nactvar = prevfs ? prevfs->nactvar : 0;
  saved_firstlabel = cg->dyd->label.n;

  /* 调用open_func：ls->fs应保持为父函数，open_func内部会设置fs->prev和ls->fs */
  cg->ls.source = f->source;
  open_func(&cg->ls, &fsrec, &blrec);

  if (!prevfs) {
    /* main函数：按照mainfunc顺序：setvararg -> 创建_ENV upvalue */
    proto->is_vararg = 1;
    luaK_codeABC(&fsrec, OP_VARARGPREP, 0, 0, 0);
    cg_create_env_upval(cg);
    cg->ls.envn = proto->upvalues[0].name;
    proto->numparams = f->nparams;
  } else {
    /* 嵌套函数：先添加参数，再处理vararg */
    for (i = 0; i < f->nparams; i++) {
      TString *pname = f->params[i].name;
      int attr = VDKREG;
      if (f->params[i].attr == AST_ATTR_CONST)
        attr = RDKCONST;
      else if (f->params[i].attr == AST_ATTR_CLOSE)
        attr = RDKTOCLOSE;
      add_local(cg, pname, attr);
    }
    /* 激活参数 */
    if (f->nparams > 0) {
      activate_locals(cg, f->nparams);
    }
    proto->numparams = f->nparams;
    /* 可变参数：生成OP_VARARGPREP指令 */
    if (f->is_vararg) {
      proto->is_vararg = 1;
      luaK_codeABC(&fsrec, OP_VARARGPREP, f->nparams, 0, 0);
    }
  }

  /* 生成函数体 */
  codegen_block(cg, &f->body);

  /* 如果顶层块有导出名称，生成导出表并返回 */
  if (f->body.nexports > 0) {
    FuncState *fs = &fsrec;
    int reg = fs->freereg;
    int pc = luaK_codeABC(fs, OP_NEWTABLE, reg, 0, 0);
    expdesc t;
    int i;
    luaK_code(fs, 0); /* Extra arg for NEWTABLE */
    init_exp(&t, VNONRELOC, reg);
    luaK_reserveregs(fs, 1);

    for (i = 0; i < f->body.nexports; i++) {
      expdesc k, v;
      TString *name = f->body.exports[i];
      expdesc t_copy = t;
      cg_codestring(&k, name);
      cg_singlevaraux(cg, fs, name, &v, 1);
      luaK_exp2anyreg(fs, &v);
      luaK_indexed(fs, &t_copy, &k);
      luaK_storevar(fs, &t_copy, &v);
    }
    luaK_settablesize(fs, pc, reg, 0, f->body.nexports);
    luaK_ret(fs, reg, 1);
  }

  /* patch goto */
  patch_gotos(cg);

  /* 关闭函数 */
  close_func(&cg->ls);

  /* 恢复状态 */
  cg->ls.fs = prevfs;
  cg->dyd->actvar.n = saved_firstlocal;
  cg->dyd->label.n = saved_firstlabel;
  if (prevfs) {
    prevfs->nactvar = saved_nactvar;
  }
  free_label_goto(cg);
  cg->labels.names = saved_label_names;
  cg->labels.pcs = saved_label_pcs;
  cg->labels.n = saved_label_n;
  cg->loop_depth = saved_loop_depth;

  return proto;
}


/* ========== 公开API ========== */

/*
** 从AstFunc生成Proto
** 参数：
**   L - Lua状态机
**   func - AST函数节点
**   pool - AST内存池
**   dyd - 动态数据
** 返回值：生成的Proto
*/
Proto *luaY_codegen_func(lua_State *L, AstFunc *func, AstPool *pool, Dyndata *dyd) {
  CodegenState cg;
  Proto *p;
  int old_actvar_n = dyd->actvar.n;
  int old_label_n = dyd->label.n;
  int old_gt_n = dyd->gt.n;
  int old_gc_state = lua_gc(L, LUA_GCISRUNNING, 0);
  lua_gc(L, LUA_GCSTOP, 0);  /* 暂停GC，防止AST池中的TString被回收 */
  codegen_init(&cg, L, pool, dyd);
  cg.ls.source = func->source;
  /* 重置dyd的label/gt列表（供codegen使用） */
  dyd->label.n = 0;
  dyd->gt.n = 0;
  p = codegen_func(&cg, func);
  /* 恢复dyd状态 */
  dyd->actvar.n = old_actvar_n;
  dyd->label.n = old_label_n;
  dyd->gt.n = old_gt_n;
  free_label_goto(&cg);
  L->top.p--;
  if (old_gc_state) lua_gc(L, LUA_GCRESTART, 0);  /* 恢复GC */
  return p;
}


/*
** 从AstChunk生成Proto（chunk是main func）
** 参数：
**   L - Lua状态机
**   chunk - AST编译单元
**   dyd - 动态数据
** 返回值：生成的Proto
*/
Proto *luaY_codegen_chunk(lua_State *L, AstChunk *chunk, Dyndata *dyd) {
  return luaY_codegen_func(L, chunk->main_func, chunk->pool, dyd);
}
