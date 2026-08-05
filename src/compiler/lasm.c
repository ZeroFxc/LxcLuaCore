/*
** $Id: lasm.c $
** LXCLUA 内联汇编器 - 从 lparser.c 端口
** 支持通过 FuncState 和 LexState 直接工作，供 lcodegen.c 调用
** See Copyright Notice in lua.h
*/

#include "lprefix.h"

#include <string.h>
#include <stdio.h>

#include "lua.h"

#include "lmem.h"
#include "llex.h"
#include "lcode.h"
#include "lopcodes.h"
#include "lstring.h"
#include "ltable.h"
#include "lobject.h"
#include "lopnames.h"
#include "lparser.h"
#include "lasm.h"


/*
** 检查当前 token 是否匹配
*/
static void check (LexState *ls, int c) {
  if (ls->t.token != c)
    luaX_syntaxerror(ls,
        luaO_pushfstring(ls->L, "%s expected", luaX_token2str(ls, c)));
}


/* ===================================================================
** 内部辅助函数：在 FuncState 中查找局部变量
** 从 lparser.c 的 searchvar 简化而来，用于 asm 的 $varname 语法
** =================================================================== */
static int lasm_searchvar (FuncState *fs, TString *name) {
  int i;
  for (i = cast_int(fs->nactvar) - 1; i >= 0; i--) {
    Vardesc *vd = &fs->ls->dyd->actvar.arr[fs->firstlocal + i];
    if (eqstr(name, vd->vd.name)) {
      return vd->vd.ridx;  /* 返回寄存器索引 */
    }
  }
  return -1;  /* 未找到 */
}


/* ===================================================================
** 内部辅助函数：在 FuncState 中查找 upvalue
** 从 lparser.c 的 searchupvalue 简化而来
** =================================================================== */
static int lasm_searchupvalue (FuncState *fs, TString *name) {
  int i;
  Upvaldesc *up = fs->f->upvalues;
  for (i = 0; i < fs->nups; i++) {
    if (eqstr(up[i].name, name)) return i;
  }
  return -1;  /* 未找到 */
}


/* ===================================================================
** 获取 _ENV 的 upvalue 索引，用于 getglobal/setglobal
** =================================================================== */
static int lasm_get_env_upvalue (FuncState *fs, LexState *ls) {
  int i;
  for (i = 0; i < fs->f->sizeupvalues; i++) {
    TString *name = fs->f->upvalues[i].name;
    if (name && strcmp(getstr(name), "_ENV") == 0) {
      return i;
    }
  }
  if (fs->f->sizeupvalues > 0) return 0;
  luaK_semerror(ls, "cannot resolve _ENV in asm");
  return 0;
}


/*
** 初始化汇编上下文
*/
void lasm_initcontext (lua_State *L, AsmContext *ctx, AsmContext *parent) {
  ctx->labels = luaM_newvector(L, ASM_INIT_LABELS, AsmLabel);
  ctx->nlabels = 0;
  ctx->labels_cap = ASM_INIT_LABELS;
  ctx->pending = luaM_newvector(L, ASM_INIT_PENDING, AsmPending);
  ctx->npending = 0;
  ctx->pending_cap = ASM_INIT_PENDING;
  ctx->defines = luaM_newvector(L, ASM_INIT_DEFINES, AsmDefine);
  ctx->ndefines = 0;
  ctx->defines_cap = ASM_INIT_DEFINES;
  ctx->parent = parent;
}


/*
** 释放汇编上下文
*/
void lasm_freecontext (lua_State *L, AsmContext *ctx) {
  luaM_freearray(L, ctx->labels, ctx->labels_cap);
  luaM_freearray(L, ctx->pending, ctx->pending_cap);
  luaM_freearray(L, ctx->defines, ctx->defines_cap);
  ctx->labels = NULL;
  ctx->pending = NULL;
  ctx->defines = NULL;
  ctx->nlabels = ctx->npending = ctx->ndefines = 0;
  ctx->labels_cap = ctx->pending_cap = ctx->defines_cap = 0;
}


/*
** 根据操作码名称查找对应的 OpCode
*/
static int find_opcode (const char *name) {
  int i;
  for (i = 0; opnames[i] != NULL; i++) {
    if (strcmp(opnames[i], name) == 0)
      return i;
  }
  return -1;
}


/*
** 在汇编上下文中查找标签
*/
static int asm_findlabel (AsmContext *ctx, TString *name) {
  int i;
  for (i = 0; i < ctx->nlabels; i++) {
    if (ctx->labels[i].name == name)
      return i;
  }
  return -1;
}


/*
** 定义汇编标签
*/
static void asm_deflabel (LexState *ls, AsmContext *ctx, TString *name, int pc, int line) {
  int idx = asm_findlabel(ctx, name);
  if (idx >= 0) {
    if (ctx->labels[idx].pc >= 0) {
      luaK_semerror(ls, "duplicate label '%s' in asm", getstr(name));
    }
    ctx->labels[idx].pc = pc;
    ctx->labels[idx].line = line;
  }
  else {
    if (ctx->nlabels >= ctx->labels_cap) {
      int newcap = ctx->labels_cap * 2;
      ctx->labels = luaM_reallocvector(ls->L, ctx->labels, ctx->labels_cap, newcap, AsmLabel);
      ctx->labels_cap = newcap;
    }
    ctx->labels[ctx->nlabels].name = name;
    ctx->labels[ctx->nlabels].pc = pc;
    ctx->labels[ctx->nlabels].line = line;
    ctx->nlabels++;
  }
}


/*
** 在汇编上下文中查找常量定义（包括父级上下文）
*/
static int asm_finddefine_ex (AsmContext *ctx, TString *name, AsmContext **out_ctx) {
  AsmContext *cur = ctx;
  while (cur != NULL) {
    int i;
    for (i = 0; i < cur->ndefines; i++) {
      if (cur->defines[i].name == name) {
        if (out_ctx) *out_ctx = cur;
        return i;
      }
    }
    cur = cur->parent;
  }
  if (out_ctx) *out_ctx = NULL;
  return -1;
}


/*
** 在汇编上下文中查找常量定义
*/
static int asm_finddefine (AsmContext *ctx, TString *name) {
  return asm_finddefine_ex(ctx, name, NULL);
}


/*
** 添加或更新汇编常量定义
*/
static void asm_adddefine (LexState *ls, AsmContext *ctx, TString *name, lua_Integer value) {
  int i;
  for (i = 0; i < ctx->ndefines; i++) {
    if (ctx->defines[i].name == name) {
      ctx->defines[i].value = value;
      return;
    }
  }
  if (ctx->ndefines >= ctx->defines_cap) {
    int newcap = ctx->defines_cap * 2;
    ctx->defines = luaM_reallocvector(ls->L, ctx->defines, ctx->defines_cap, newcap, AsmDefine);
    ctx->defines_cap = newcap;
  }
  ctx->defines[ctx->ndefines].name = name;
  ctx->defines[ctx->ndefines].value = value;
  ctx->ndefines++;
}


/*
** 引用汇编标签（可能是前向引用）
*/
static int asm_reflabel (LexState *ls, AsmContext *ctx, TString *name) {
  int idx = asm_findlabel(ctx, name);
  if (idx >= 0 && ctx->labels[idx].pc >= 0) {
    return ctx->labels[idx].pc;
  }
  if (idx < 0) {
    if (ctx->nlabels >= ctx->labels_cap) {
      int newcap = ctx->labels_cap * 2;
      ctx->labels = luaM_reallocvector(ls->L, ctx->labels, ctx->labels_cap, newcap, AsmLabel);
      ctx->labels_cap = newcap;
    }
    ctx->labels[ctx->nlabels].name = name;
    ctx->labels[ctx->nlabels].pc = -1;
    ctx->labels[ctx->nlabels].line = ls->linenumber;
    ctx->nlabels++;
  }
  return -1;
}


/*
** 添加待修补的跳转指令
*/
static void asm_addpending (LexState *ls, AsmContext *ctx, TString *label,
                            int pc, int line, int isJump) {
  if (ctx->npending >= ctx->pending_cap) {
    int newcap = ctx->pending_cap * 2;
    ctx->pending = luaM_reallocvector(ls->L, ctx->pending, ctx->pending_cap, newcap, AsmPending);
    ctx->pending_cap = newcap;
  }
  ctx->pending[ctx->npending].label = label;
  ctx->pending[ctx->npending].pc = pc;
  ctx->pending[ctx->npending].line = line;
  ctx->pending[ctx->npending].is_jump = isJump;
  ctx->npending++;
}


/*
** 修补所有待处理的跳转指令
*/
void lasm_patchpending (LexState *ls, FuncState *fs, AsmContext *ctx) {
  int i;
  for (i = 0; i < ctx->npending; i++) {
    AsmPending *p = &ctx->pending[i];
    int idx = asm_findlabel(ctx, p->label);
    if (idx < 0 || ctx->labels[idx].pc < 0) {
      luaK_semerror(ls, "undefined label '%s' in asm", getstr(p->label));
    }
    int target = ctx->labels[idx].pc;
    Instruction *inst = &fs->f->code[p->pc];
    OpCode op = GET_OPCODE(*inst);

    if (p->is_jump) {
      int offset = target - (p->pc + 1);
      if (getOpMode(op) == isJ) {
        SETARG_sJ(*inst, offset);
      }
      else if (getOpMode(op) == iAsBx) {
        SETARG_sBx(*inst, offset);
      }
      else {
        if (op == OP_FORLOOP || op == OP_TFORLOOP) {
          if (offset > 0) {
            luaK_semerror(ls, "jump target for loop instruction must be backward");
          }
          offset = -offset;
          if (offset > MAXARG_Bx) {
            luaK_semerror(ls, "control structure too long");
          }
          SETARG_Bx(*inst, cast_uint(offset));
        }
        else if (op == OP_FORPREP || op == OP_TFORPREP) {
          if (offset < 0) {
            luaK_semerror(ls, "jump target for prep instruction must be forward");
          }
          if (op == OP_FORPREP) offset--;
          if (offset < 0 || offset > MAXARG_Bx) {
             luaK_semerror(ls, "control structure too long or invalid target");
          }
          SETARG_Bx(*inst, cast_uint(offset));
        }
        else {
          SETARG_Bx(*inst, cast_uint(target));
        }
      }
    }
    else {
      enum OpMode mode = getOpMode(op);
      if (mode == iABx || mode == iAsBx) {
        SETARG_Bx(*inst, cast_uint(target));
      }
      else if (mode == iAx) {
        SETARG_Ax(*inst, target);
      }
      else {
        SETARG_B(*inst, target);
      }
    }
  }
}


/*
** 检查参数是否在有效范围内
*/
static void asm_checkrange (LexState *ls, lua_Integer val, lua_Integer max, const char *name) {
  if (val < 0 || val > max) {
    luaK_semerror(ls, "asm %s out of range (got %lld, max %lld)",
                  name, (long long)val, (long long)max);
  }
}


/*
** 检查带符号参数是否在有效范围内
*/
static void asm_checkrange_signed (LexState *ls, lua_Integer val,
                                   lua_Integer min, lua_Integer max, const char *name) {
  if (val < min || val > max) {
    luaK_semerror(ls, "asm %s out of range (got %lld, range %lld to %lld)",
                  name, (long long)val, (long long)min, (long long)max);
  }
}


/* ===================================================================
** 汇编表达式解析器
** =================================================================== */

/* 前向声明 */
static lua_Integer asm_get_expr_ex (LexState *ls, AsmContext *ctx,
    int *pendingPc, TString **pendingLabel, int *isLabelRef);
static lua_Integer asm_get_or (LexState *ls, AsmContext *ctx,
    int *pendingPc, TString **pendingLabel, int *isLabelRef);
static lua_Integer asm_get_xor (LexState *ls, AsmContext *ctx,
    int *pendingPc, TString **pendingLabel, int *isLabelRef);
static lua_Integer asm_get_and (LexState *ls, AsmContext *ctx,
    int *pendingPc, TString **pendingLabel, int *isLabelRef);
static lua_Integer asm_get_shift (LexState *ls, AsmContext *ctx,
    int *pendingPc, TString **pendingLabel, int *isLabelRef);
static lua_Integer asm_get_add (LexState *ls, AsmContext *ctx,
    int *pendingPc, TString **pendingLabel, int *isLabelRef);
static lua_Integer asm_get_mul (LexState *ls, AsmContext *ctx,
    int *pendingPc, TString **pendingLabel, int *isLabelRef);
static lua_Integer asm_get_unary (LexState *ls, AsmContext *ctx,
    int *pendingPc, TString **pendingLabel, int *isLabelRef);
static lua_Integer asm_get_primary (LexState *ls, AsmContext *ctx,
    int *pendingPc, TString **pendingLabel, int *isLabelRef);


static lua_Integer asm_get_primary (LexState *ls, AsmContext *ctx,
                                    int *pendingPc, TString **pendingLabel, int *isLabelRef) {
  lua_Integer val;
  FuncState *fs = ls->fs;

  if (pendingPc) *pendingPc = -1;
  if (pendingLabel) *pendingLabel = NULL;
  if (isLabelRef) *isLabelRef = 0;

  if (ls->t.token == '(') {
    luaX_next(ls);
    val = asm_get_expr_ex(ls, ctx, pendingPc, pendingLabel, isLabelRef);
    checknext(ls, ')');
    return val;
  }
  else if (ls->t.token == TK_INT) {
    val = ls->t.seminfo.i;
    luaX_next(ls);
    return val;
  }
  else if (ls->t.token == TK_DOLLAR) {
    /* $varname 或 $(varname + offset) */
    luaX_next(ls);
    int has_paren = testnext(ls, '(');
    check(ls, TK_NAME);
    TString *varname = ls->t.seminfo.ts;
    luaX_next(ls);

    int ridx = lasm_searchvar(fs, varname);
    if (ridx < 0) {
      luaK_semerror(ls, "undefined local variable '%s' in asm", getstr(varname));
    }
    val = ridx;

    if (has_paren) {
      if (testnext(ls, '+')) {
        val += asm_get_expr_ex(ls, ctx, pendingPc, pendingLabel, isLabelRef);
      }
      else if (testnext(ls, '-')) {
        val -= asm_get_expr_ex(ls, ctx, pendingPc, pendingLabel, isLabelRef);
      }
      checknext(ls, ')');
    }
    return val;
  }
  else if (ls->t.token == '%') {
    /* %n 或 %(expression) */
    luaX_next(ls);
    if (testnext(ls, '(')) {
      val = asm_get_expr_ex(ls, ctx, pendingPc, pendingLabel, isLabelRef);
      checknext(ls, ')');
    } else {
      check(ls, TK_INT);
      val = ls->t.seminfo.i;
      luaX_next(ls);
    }
    if (val < 0 || val > 255) {
      luaK_semerror(ls, "register index out of range (0-255) in asm: %lld", (long long)val);
    }
    return val;
  }
  else if (ls->t.token == TK_NAME) {
    const char *name = getstr(ls->t.seminfo.ts);
    TString *ts = ls->t.seminfo.ts;

    if ((strcmp(name, "R") == 0 || strcmp(name, "r") == 0) && luaX_lookahead(ls) == '(') {
      luaX_next(ls);
      luaX_next(ls);
      val = asm_get_expr_ex(ls, ctx, pendingPc, pendingLabel, isLabelRef);
      checknext(ls, ')');
      if (val < 0 || val > 255) {
        luaK_semerror(ls, "register index out of range (0-255) in asm: R(%lld)", (long long)val);
      }
      return val;
    }
    else if ((name[0] == 'R' || name[0] == 'r') && name[1] >= '0' && name[1] <= '9') {
      val = 0;
      int i = 1;
      while (name[i] >= '0' && name[i] <= '9') {
        val = val * 10 + (name[i] - '0');
        i++;
      }
      if (name[i] == '\0') {
        if (val > 255) {
          luaK_semerror(ls, "register index out of range (0-255) in asm: R%lld", (long long)val);
        }
        luaX_next(ls);
        return val;
      }
    }
    /* 检查是否是通过 def 定义的常量 */
    if (ctx != NULL) {
      AsmContext *found_ctx = NULL;
      int defIdx = asm_finddefine_ex(ctx, ts, &found_ctx);
      if (defIdx >= 0 && found_ctx != NULL) {
        luaX_next(ls);
        return found_ctx->defines[defIdx].value;
      }
    }
    luaX_syntaxerror(ls, "integer or expression expected in asm instruction");
    return 0;
  }
  else if (ls->t.token == '^') {
    /* ^varname - 获取 upvalue 的索引 */
    TString *varname;
    int idx;
    luaX_next(ls);
    check(ls, TK_NAME);
    varname = ls->t.seminfo.ts;
    idx = lasm_searchupvalue(fs, varname);
    if (idx < 0) {
      luaK_semerror(ls, "undefined upvalue '%s' in asm", getstr(varname));
    }
    luaX_next(ls);
    return idx;
  }
  else if (ls->t.token == '#') {
    /* #constant - 常量相关操作 */
    luaX_next(ls);
    if (ls->t.token == TK_STRING || ls->t.token == TK_RAWSTRING) {
      TString *s = ls->t.seminfo.ts;
      val = luaK_stringK(fs, s);
      luaX_next(ls);
      return val;
    }
    else if (ls->t.token == TK_INT) {
      val = ls->t.seminfo.i;
      luaX_next(ls);
      return val;
    }
    else if (ls->t.token == TK_FLT) {
      val = (lua_Integer)ls->t.seminfo.r;
      luaX_next(ls);
      return val;
    }
    else if (ls->t.token == '-') {
      luaX_next(ls);
      if (ls->t.token == TK_INT) {
        val = -ls->t.seminfo.i;
        luaX_next(ls);
        return val;
      }
      else if (ls->t.token == TK_FLT) {
        val = (lua_Integer)(-ls->t.seminfo.r);
        luaX_next(ls);
        return val;
      }
      else {
        luaX_syntaxerror(ls, "number expected after '#-' in asm");
        return 0;
      }
    }
    else if (ls->t.token == TK_NAME) {
      const char *name = getstr(ls->t.seminfo.ts);
      if (name[0] == 'K' || name[0] == 'k') {
        if (name[1] == 'F' || name[1] == 'f') {
          luaX_next(ls);
          if (ls->t.token == TK_FLT) {
            val = luaK_numberK(fs, ls->t.seminfo.r);
            luaX_next(ls);
            return val;
          }
          else if (ls->t.token == TK_INT) {
            val = luaK_numberK(fs, (lua_Number)ls->t.seminfo.i);
            luaX_next(ls);
            return val;
          }
          else if (ls->t.token == '-') {
            luaX_next(ls);
            if (ls->t.token == TK_FLT) {
              val = luaK_numberK(fs, -ls->t.seminfo.r);
              luaX_next(ls);
              return val;
            }
            else if (ls->t.token == TK_INT) {
              val = luaK_numberK(fs, (lua_Number)(-ls->t.seminfo.i));
              luaX_next(ls);
              return val;
            }
          }
          luaX_syntaxerror(ls, "number expected after '#KF' in asm");
          return 0;
        }
        else if (name[1] == 'I' || name[1] == 'i' || name[1] == '\0') {
          luaX_next(ls);
          if (ls->t.token == TK_INT) {
            val = luaK_intK(fs, ls->t.seminfo.i);
            luaX_next(ls);
            return val;
          }
          else if (ls->t.token == '-') {
            luaX_next(ls);
            if (ls->t.token == TK_INT) {
              val = luaK_intK(fs, -ls->t.seminfo.i);
              luaX_next(ls);
              return val;
            }
          }
          luaX_syntaxerror(ls, "integer expected after '#K' in asm");
          return 0;
        }
      }
      luaX_syntaxerror(ls, "invalid constant specifier after '#' in asm");
      return 0;
    }
    else {
      luaX_syntaxerror(ls, "constant expected after '#' in asm");
      return 0;
    }
  }
  else if (ls->t.token == '@') {
    /* @ 或 @label - PC 位置或标签引用 */
    luaX_next(ls);
    if (ls->t.token == TK_NAME && ctx != NULL) {
      TString *labelname = ls->t.seminfo.ts;
      int labelIdx = asm_findlabel(ctx, labelname);
      int defIdx = asm_finddefine(ctx, labelname);
      if (labelIdx >= 0 || defIdx < 0) {
        int labelpc = asm_reflabel(ls, ctx, labelname);
        luaX_next(ls);
        if (labelpc < 0) {
          if (pendingLabel) *pendingLabel = labelname;
          return 0;
        }
        if (isLabelRef) *isLabelRef = 1;
        return labelpc;
      }
    }
    return fs->pc;
  }
  else if (ls->t.token == TK_NOT) {
    /* !specifier - 特殊值 */
    luaX_next(ls);
    check(ls, TK_NAME);
    const char *specname = getstr(ls->t.seminfo.ts);
    luaX_next(ls);

    if (strcmp(specname, "freereg") == 0) {
      return fs->freereg;
    }
    else if (strcmp(specname, "nactvar") == 0) {
      return fs->nactvar;
    }
    else if (strcmp(specname, "pc") == 0) {
      return fs->pc;
    }
    else if (strcmp(specname, "nk") == 0) {
      return fs->nk;
    }
    else if (strcmp(specname, "np") == 0) {
      return fs->np;
    }
    else {
      luaK_semerror(ls, "unknown special value '!%s' in asm", specname);
      return 0;
    }
  }
  else {
    luaX_syntaxerror(ls, "integer or expression expected in asm instruction");
    return 0;
  }
}


static lua_Integer asm_get_unary (LexState *ls, AsmContext *ctx,
    int *pendingPc, TString **pendingLabel, int *isLabelRef) {
  if (testnext(ls, '-')) {
    return -asm_get_unary(ls, ctx, pendingPc, pendingLabel, isLabelRef);
  }
  else if (testnext(ls, '~')) {
    return ~asm_get_unary(ls, ctx, pendingPc, pendingLabel, isLabelRef);
  }
  else {
    return asm_get_primary(ls, ctx, pendingPc, pendingLabel, isLabelRef);
  }
}


static lua_Integer asm_get_mul (LexState *ls, AsmContext *ctx,
    int *pendingPc, TString **pendingLabel, int *isLabelRef) {
  lua_Integer val = asm_get_unary(ls, ctx, pendingPc, pendingLabel, isLabelRef);
  for (;;) {
    if (testnext(ls, '*')) {
      val *= asm_get_unary(ls, ctx, pendingPc, pendingLabel, isLabelRef);
    }
    else if (testnext(ls, '/')) {
      lua_Integer denom = asm_get_unary(ls, ctx, pendingPc, pendingLabel, isLabelRef);
      if (denom == 0) luaK_semerror(ls, "division by zero in asm expression");
      val /= denom;
    }
    else if (testnext(ls, '%')) {
      lua_Integer denom = asm_get_unary(ls, ctx, pendingPc, pendingLabel, isLabelRef);
      if (denom == 0) luaK_semerror(ls, "division by zero in asm expression");
      val %= denom;
    }
    else if (testnext(ls, TK_IDIV)) {
      lua_Integer denom = asm_get_unary(ls, ctx, pendingPc, pendingLabel, isLabelRef);
      if (denom == 0) luaK_semerror(ls, "division by zero in asm expression");
      val /= denom;
    }
    else {
      break;
    }
  }
  return val;
}


static lua_Integer asm_get_add (LexState *ls, AsmContext *ctx,
    int *pendingPc, TString **pendingLabel, int *isLabelRef) {
  lua_Integer val = asm_get_mul(ls, ctx, pendingPc, pendingLabel, isLabelRef);
  for (;;) {
    if (testnext(ls, '+')) {
      val += asm_get_mul(ls, ctx, pendingPc, pendingLabel, isLabelRef);
    }
    else if (testnext(ls, '-')) {
      val -= asm_get_mul(ls, ctx, pendingPc, pendingLabel, isLabelRef);
    }
    else {
      break;
    }
  }
  return val;
}


static lua_Integer asm_get_shift (LexState *ls, AsmContext *ctx,
    int *pendingPc, TString **pendingLabel, int *isLabelRef) {
  lua_Integer val = asm_get_add(ls, ctx, pendingPc, pendingLabel, isLabelRef);
  for (;;) {
    if (testnext(ls, TK_SHL)) {
      val <<= asm_get_add(ls, ctx, pendingPc, pendingLabel, isLabelRef);
    }
    else if (testnext(ls, TK_SHR)) {
      val >>= asm_get_add(ls, ctx, pendingPc, pendingLabel, isLabelRef);
    }
    else {
      break;
    }
  }
  return val;
}


static lua_Integer asm_get_and (LexState *ls, AsmContext *ctx,
    int *pendingPc, TString **pendingLabel, int *isLabelRef) {
  lua_Integer val = asm_get_shift(ls, ctx, pendingPc, pendingLabel, isLabelRef);
  while (testnext(ls, '&')) {
    val &= asm_get_shift(ls, ctx, pendingPc, pendingLabel, isLabelRef);
  }
  return val;
}


static lua_Integer asm_get_xor (LexState *ls, AsmContext *ctx,
    int *pendingPc, TString **pendingLabel, int *isLabelRef) {
  lua_Integer val = asm_get_and(ls, ctx, pendingPc, pendingLabel, isLabelRef);
  while (testnext(ls, '~')) {
    val ^= asm_get_and(ls, ctx, pendingPc, pendingLabel, isLabelRef);
  }
  return val;
}


static lua_Integer asm_get_or (LexState *ls, AsmContext *ctx,
    int *pendingPc, TString **pendingLabel, int *isLabelRef) {
  lua_Integer val = asm_get_xor(ls, ctx, pendingPc, pendingLabel, isLabelRef);
  while (testnext(ls, '|')) {
    val |= asm_get_xor(ls, ctx, pendingPc, pendingLabel, isLabelRef);
  }
  return val;
}


static lua_Integer asm_get_expr_ex (LexState *ls, AsmContext *ctx,
    int *pendingPc, TString **pendingLabel, int *isLabelRef) {
  return asm_get_or(ls, ctx, pendingPc, pendingLabel, isLabelRef);
}


static lua_Integer asm_getint_ex (LexState *ls, AsmContext *ctx,
                                   int *pendingPc, TString **pendingLabel, int *isLabelRef) {
  return asm_get_expr_ex(ls, ctx, pendingPc, pendingLabel, isLabelRef);
}


/*
** 解析汇编指令中的整数参数（简化版，不支持前向标签引用）
*/
static lua_Integer asm_getint (LexState *ls) {
  return asm_getint_ex(ls, NULL, NULL, NULL, NULL);
}


/*
** 尝试解析汇编指令中的可选整数参数
*/
static lua_Integer asm_trygetint (LexState *ls, lua_Integer defval) {
  if (ls->t.token == TK_INT || ls->t.token == '-' || ls->t.token == '~' ||
      ls->t.token == TK_DOLLAR || ls->t.token == '^' ||
      ls->t.token == '#' || ls->t.token == TK_OR ||
      ls->t.token == TK_NOT || ls->t.token == '%' ||
      ls->t.token == '(') {
    return asm_getint(ls);
  }
  if (ls->t.token == TK_NAME) {
    const char *name = getstr(ls->t.seminfo.ts);
    if ((name[0] == 'R' || name[0] == 'r') && name[1] >= '0' && name[1] <= '9') {
      return asm_getint(ls);
    }
    if (strcmp(name, "R") == 0 || strcmp(name, "r") == 0) {
      return asm_getint(ls);
    }
  }
  return defval;
}


/*
** 尝试解析带标签支持的可选整数参数
*/
static lua_Integer asm_trygetint_ex (LexState *ls, AsmContext *ctx,
                                      lua_Integer defval,
                                      int *pendingPc, TString **pendingLabel, int *isLabelRef) {
  if (ls->t.token == TK_INT || ls->t.token == '-' || ls->t.token == '~' ||
      ls->t.token == TK_DOLLAR || ls->t.token == '^' ||
      ls->t.token == '#' || ls->t.token == TK_OR ||
      ls->t.token == TK_NOT || ls->t.token == '%' ||
      ls->t.token == '(') {
    return asm_getint_ex(ls, ctx, pendingPc, pendingLabel, isLabelRef);
  }
  if (ls->t.token == TK_NAME) {
    const char *name = getstr(ls->t.seminfo.ts);
    if ((name[0] == 'R' || name[0] == 'r') && name[1] >= '0' && name[1] <= '9') {
      return asm_getint_ex(ls, ctx, pendingPc, pendingLabel, isLabelRef);
    }
    if (strcmp(name, "R") == 0 || strcmp(name, "r") == 0) {
      return asm_getint_ex(ls, ctx, pendingPc, pendingLabel, isLabelRef);
    }
    if (ctx != NULL) {
       if (asm_finddefine_ex(ctx, ls->t.seminfo.ts, NULL) >= 0) {
          return asm_getint_ex(ls, ctx, pendingPc, pendingLabel, isLabelRef);
       }
    }
  }
  if (pendingPc) *pendingPc = -1;
  if (pendingLabel) *pendingLabel = NULL;
  if (isLabelRef) *isLabelRef = 0;
  return defval;
}


/*
** 发射一条跳转指令，自动处理前向引用和后向引用
*/
static void asm_emit_jmp (LexState *ls, FuncState *fs, AsmContext *ctx, TString *label, int line) {
  int labelIdx = asm_findlabel(ctx, label);
  if (labelIdx >= 0 && ctx->labels[labelIdx].pc >= 0) {
    int target_pc = ctx->labels[labelIdx].pc;
    int current_pc = fs->pc;
    int offset = target_pc - (current_pc + 1);
    Instruction jmp_inst = CREATE_sJ(OP_JMP, offset + OFFSET_sJ, 0);
    luaK_code(fs, jmp_inst);
    luaK_fixline(fs, line);
  }
  else {
    int instpc = fs->pc;
    Instruction jmp_inst = CREATE_sJ(OP_JMP, OFFSET_sJ, 0);
    luaK_code(fs, jmp_inst);
    luaK_fixline(fs, line);
    asm_addpending(ls, ctx, label, instpc, line, 1);
  }
}


/*
** 递归解析 asm 块主体
** 这个函数包含 asm 主循环的完整逻辑，支持所有伪指令和嵌套
*/
void lasm_parse_body (LexState *ls, FuncState *fs, AsmContext *ctx, int line) {
  while (ls->t.token != ')') {
    const char *opname;
    int opcode;
    enum OpMode mode;
    Instruction inst;
    int instpc;
    TString *pendingLabel = NULL;
    int needsPatch = 0;
    int isJumpInst = 0;

    /*
    ** 跳过注释内容
    */
    for (;;) {
      if (ls->t.token == ';') {
        luaX_next(ls);
        if (ls->t.token == TK_STRING || ls->t.token == TK_RAWSTRING) {
          luaX_next(ls);
        }
      }
      else if (ls->t.token == TK_STRING || ls->t.token == TK_RAWSTRING) {
        luaX_next(ls);
      }
      else {
        break;
      }
    }

    if (ls->t.token == ')') break;

    /* 检查是否是标签定义 */
    if (ls->t.token == ':') {
      luaX_next(ls);
      check(ls, TK_NAME);
      TString *labelname = ls->t.seminfo.ts;
      asm_deflabel(ls, ctx, labelname, fs->pc, ls->linenumber);
      luaX_next(ls);
      testnext(ls, ';');
      continue;
    }

    check(ls, TK_NAME);
    opname = getstr(ls->t.seminfo.ts);

    /* comment 伪指令 */
    if (strcmp(opname, "comment") == 0 || strcmp(opname, "rem") == 0 ||
        strcmp(opname, "COMMENT") == 0 || strcmp(opname, "REM") == 0) {
      luaX_next(ls);
      if (ls->t.token == TK_STRING || ls->t.token == TK_RAWSTRING) {
        luaX_next(ls);
      }
      testnext(ls, ';');
      continue;
    }

    /* nop 伪指令 */
    if (strcmp(opname, "nop") == 0) {
      int nop_count = 1;
      luaX_next(ls);
      if (ls->t.token == TK_INT) {
        nop_count = (int)ls->t.seminfo.i;
        luaX_next(ls);
      }
      {
        int j;
        for (j = 0; j < nop_count; j++) {
          Instruction nop_inst = CREATE_ABCk(OP_MOVE, 0, 0, 0, 0);
          luaK_code(fs, nop_inst);
          luaK_fixline(fs, line);
        }
      }
      testnext(ls, ';');
      continue;
    }

    /* raw 伪指令 */
    if (strcmp(opname, "raw") == 0) {
      luaX_next(ls);
      lua_Integer raw_val = asm_getint_ex(ls, ctx, NULL, NULL, NULL);
      Instruction raw_inst = (Instruction)raw_val;
      luaK_code(fs, raw_inst);
      luaK_fixline(fs, line);
      testnext(ls, ';');
      continue;
    }

    /* emit 伪指令 */
    if (strcmp(opname, "emit") == 0) {
      luaX_next(ls);
      do {
        lua_Integer emit_val = asm_getint_ex(ls, ctx, NULL, NULL, NULL);
        Instruction emit_inst = (Instruction)emit_val;
        luaK_code(fs, emit_inst);
        luaK_fixline(fs, line);
      } while (testnext(ls, ','));
      testnext(ls, ';');
      continue;
    }

    /* 嵌套 asm 伪指令 */
    if (strcmp(opname, "asm") == 0) {
      int nested_line = ls->linenumber;
      AsmContext nested_ctx;
      luaX_next(ls);
      checknext(ls, '(');
      lasm_initcontext(ls->L, &nested_ctx, ctx);
      lasm_parse_body(ls, fs, &nested_ctx, nested_line);
      lasm_patchpending(ls, fs, &nested_ctx);
      lasm_freecontext(ls->L, &nested_ctx);
      checknext(ls, ')');
      testnext(ls, ';');
      continue;
    }

    /* jmpx 伪指令 */
    if (strcmp(opname, "jmpx") == 0 || strcmp(opname, "JMPX") == 0) {
      luaX_next(ls);
      if (ls->t.token != TK_OR) {
        luaK_semerror(ls, "jmpx requires @label argument");
      }
      luaX_next(ls);
      check(ls, TK_NAME);
      TString *label = ls->t.seminfo.ts;
      luaX_next(ls);
      asm_emit_jmp(ls, fs, ctx, label, line);
      testnext(ls, ';');
      continue;
    }

    /* align 伪指令 */
    if (strcmp(opname, "align") == 0) {
      luaX_next(ls);
      int align_val = (int)asm_getint_ex(ls, ctx, NULL, NULL, NULL);
      if (align_val < 1) {
        luaK_semerror(ls, "align value must be positive");
      }
      while (fs->pc % align_val != 0) {
        Instruction nop_inst = CREATE_ABCk(OP_MOVE, 0, 0, 0, 0);
        luaK_code(fs, nop_inst);
        luaK_fixline(fs, line);
      }
      testnext(ls, ';');
      continue;
    }

    /* def 伪指令 */
    if (strcmp(opname, "def") == 0 || strcmp(opname, "define") == 0) {
      luaX_next(ls);
      check(ls, TK_NAME);
      TString *def_name = ls->t.seminfo.ts;
      luaX_next(ls);
      lua_Integer def_value = asm_getint_ex(ls, ctx, NULL, NULL, NULL);
      asm_adddefine(ls, ctx, def_name, def_value);
      testnext(ls, ';');
      continue;
    }

    /* newreg 伪指令 */
    if (strcmp(opname, "newreg") == 0) {
      luaX_next(ls);
      check(ls, TK_NAME);
      TString *reg_name = ls->t.seminfo.ts;
      luaX_next(ls);
      int reg = fs->freereg;
      luaK_reserveregs(fs, 1);
      asm_adddefine(ls, ctx, reg_name, reg);
      testnext(ls, ';');
      continue;
    }

    /* getglobal 伪指令 */
    if (strcmp(opname, "getglobal") == 0) {
      luaX_next(ls);
      int reg_dest = (int)asm_getint_ex(ls, ctx, NULL, NULL, NULL);
      TString *key_name;
      if (ls->t.token == TK_STRING || ls->t.token == TK_RAWSTRING) {
        key_name = ls->t.seminfo.ts;
        luaX_next(ls);
      } else {
        check(ls, TK_NAME);
        key_name = ls->t.seminfo.ts;
        luaX_next(ls);
      }
      int env_idx = lasm_get_env_upvalue(fs, ls);
      int k = luaK_stringK(fs, key_name);
      Instruction getglobal_inst = CREATE_ABCk(OP_GETTABUP, reg_dest, env_idx, k, 0);
      luaK_code(fs, getglobal_inst);
      luaK_fixline(fs, line);
      if (reg_dest >= fs->freereg) {
        int needed = reg_dest + 1 - fs->freereg;
        luaK_checkstack(fs, needed);
        fs->freereg = (reg_dest + 1);
      }
      testnext(ls, ';');
      continue;
    }

    /* setglobal 伪指令 */
    if (strcmp(opname, "setglobal") == 0) {
      luaX_next(ls);
      int reg_src = (int)asm_getint_ex(ls, ctx, NULL, NULL, NULL);
      TString *key_name;
      if (ls->t.token == TK_STRING || ls->t.token == TK_RAWSTRING) {
        key_name = ls->t.seminfo.ts;
        luaX_next(ls);
      } else {
        check(ls, TK_NAME);
        key_name = ls->t.seminfo.ts;
        luaX_next(ls);
      }
      int env_idx = lasm_get_env_upvalue(fs, ls);
      int k = luaK_stringK(fs, key_name);
      Instruction setglobal_inst = CREATE_ABCk(OP_SETTABUP, env_idx, k, reg_src, 0);
      luaK_code(fs, setglobal_inst);
      luaK_fixline(fs, line);
      testnext(ls, ';');
      continue;
    }

    /* _print / asmprint */
    if (strcmp(opname, "_print") == 0 || strcmp(opname, "asmprint") == 0) {
      luaX_next(ls);
      if (ls->t.token == TK_STRING || ls->t.token == TK_RAWSTRING) {
        const char *msg = getstr(ls->t.seminfo.ts);
        luaX_next(ls);
        if (ls->t.token == TK_INT || ls->t.token == '-' ||
            ls->t.token == TK_DOLLAR || ls->t.token == '%' ||
            ls->t.token == TK_NOT || ls->t.token == TK_OR ||
            ls->t.token == TK_NAME || ls->t.token == '(' || ls->t.token == '~') {
          lua_Integer val = asm_getint_ex(ls, ctx, NULL, NULL, NULL);
          printf("[ASM] %s: %lld\n", msg, (long long)val);
        } else {
          printf("[ASM] %s\n", msg);
        }
      }
      else if (ls->t.token == TK_INT || ls->t.token == '-' ||
               ls->t.token == TK_DOLLAR || ls->t.token == '%' ||
               ls->t.token == TK_NOT || ls->t.token == TK_OR ||
               ls->t.token == TK_NAME || ls->t.token == '(' || ls->t.token == '~') {
        lua_Integer val = asm_getint_ex(ls, ctx, NULL, NULL, NULL);
        printf("[ASM] value: %lld\n", (long long)val);
      }
      else {
        luaK_semerror(ls, "_print expects string or value");
      }
      testnext(ls, ';');
      continue;
    }

    /* _assert / asmassert */
    if (strcmp(opname, "_assert") == 0 || strcmp(opname, "asmassert") == 0) {
      luaX_next(ls);
      lua_Integer left_val = asm_getint_ex(ls, ctx, NULL, NULL, NULL);
      int cond_result = 0;
      lua_Integer right_val;
      if (ls->t.token == TK_EQ) {
        luaX_next(ls);
        right_val = asm_getint_ex(ls, ctx, NULL, NULL, NULL);
        cond_result = (left_val == right_val);
      }
      else if (ls->t.token == TK_NE) {
        luaX_next(ls);
        right_val = asm_getint_ex(ls, ctx, NULL, NULL, NULL);
        cond_result = (left_val != right_val);
      }
      else if (ls->t.token == '>') {
        luaX_next(ls);
        if (ls->t.token == '=') {
          luaX_next(ls);
          right_val = asm_getint_ex(ls, ctx, NULL, NULL, NULL);
          cond_result = (left_val >= right_val);
        } else {
          right_val = asm_getint_ex(ls, ctx, NULL, NULL, NULL);
          cond_result = (left_val > right_val);
        }
      }
      else if (ls->t.token == '<') {
        luaX_next(ls);
        if (ls->t.token == '=') {
          luaX_next(ls);
          right_val = asm_getint_ex(ls, ctx, NULL, NULL, NULL);
          cond_result = (left_val <= right_val);
        } else {
          right_val = asm_getint_ex(ls, ctx, NULL, NULL, NULL);
          cond_result = (left_val < right_val);
        }
      }
      else if (ls->t.token == TK_GE) {
        luaX_next(ls);
        right_val = asm_getint_ex(ls, ctx, NULL, NULL, NULL);
        cond_result = (left_val >= right_val);
      }
      else if (ls->t.token == TK_LE) {
        luaX_next(ls);
        right_val = asm_getint_ex(ls, ctx, NULL, NULL, NULL);
        cond_result = (left_val <= right_val);
      }
      else {
        cond_result = (left_val != 0);
      }

      if (!cond_result) {
        if (ls->t.token == TK_STRING || ls->t.token == TK_RAWSTRING) {
          const char *msg = getstr(ls->t.seminfo.ts);
          luaX_next(ls);
          luaK_semerror(ls, "asm assertion failed: %s", msg);
        } else {
          luaK_semerror(ls, "asm assertion failed");
        }
      } else {
        if (ls->t.token == TK_STRING || ls->t.token == TK_RAWSTRING) {
          luaX_next(ls);
        }
      }
      testnext(ls, ';');
      continue;
    }

    /* _info / asminfo */
    if (strcmp(opname, "_info") == 0 || strcmp(opname, "asminfo") == 0) {
      luaX_next(ls);
      printf("[ASM INFO] line=%d, pc=%d, freereg=%d, nactvar=%d, nk=%d\n",
             ls->linenumber, fs->pc, fs->freereg, fs->nactvar, fs->nk);
      testnext(ls, ';');
      continue;
    }

    /* db / dw / dd */
    if (strcmp(opname, "db") == 0) {
      unsigned char bytes[4] = {0, 0, 0, 0};
      int byte_count = 0;
      luaX_next(ls);
      do {
        lua_Integer byte_val = asm_getint_ex(ls, ctx, NULL, NULL, NULL);
        if (byte_count < 4) {
          bytes[byte_count++] = (unsigned char)(byte_val & 0xFF);
        }
        if (byte_count == 4) {
          Instruction db_inst = bytes[0] | (bytes[1] << 8) | (bytes[2] << 16) | (bytes[3] << 24);
          luaK_code(fs, db_inst);
          luaK_fixline(fs, line);
          byte_count = 0;
          memset(bytes, 0, 4);
        }
      } while (testnext(ls, ','));
      if (byte_count > 0) {
        Instruction db_inst = bytes[0] | (bytes[1] << 8) | (bytes[2] << 16) | (bytes[3] << 24);
        luaK_code(fs, db_inst);
        luaK_fixline(fs, line);
      }
      testnext(ls, ';');
      continue;
    }
    if (strcmp(opname, "dw") == 0) {
      unsigned short words[2] = {0, 0};
      int word_count = 0;
      luaX_next(ls);
      do {
        lua_Integer word_val = asm_getint_ex(ls, ctx, NULL, NULL, NULL);
        if (word_count < 2) {
          words[word_count++] = (unsigned short)(word_val & 0xFFFF);
        }
        if (word_count == 2) {
          Instruction dw_inst = words[0] | (words[1] << 16);
          luaK_code(fs, dw_inst);
          luaK_fixline(fs, line);
          word_count = 0;
          memset(words, 0, 4);
        }
      } while (testnext(ls, ','));
      if (word_count > 0) {
        Instruction dw_inst = words[0] | (words[1] << 16);
        luaK_code(fs, dw_inst);
        luaK_fixline(fs, line);
      }
      testnext(ls, ';');
      continue;
    }
    if (strcmp(opname, "dd") == 0) {
      luaX_next(ls);
      do {
        lua_Integer dword_val = asm_getint_ex(ls, ctx, NULL, NULL, NULL);
        Instruction dd_inst = (Instruction)(dword_val & 0xFFFFFFFF);
        luaK_code(fs, dd_inst);
        luaK_fixline(fs, line);
      } while (testnext(ls, ','));
      testnext(ls, ';');
      continue;
    }

    /* str "string" */
    if (strcmp(opname, "str") == 0) {
      luaX_next(ls);
      if (ls->t.token == TK_STRING || ls->t.token == TK_RAWSTRING) {
        TString *str_data = ls->t.seminfo.ts;
        int idx = luaK_stringK(fs, str_data);
        (void)idx;
        luaX_next(ls);
      } else {
        luaK_semerror(ls, "str expects a string literal");
      }
      testnext(ls, ';');
      continue;
    }

    /* rep count { ... } */
    if (strcmp(opname, "rep") == 0 || strcmp(opname, "repeat") == 0) {
      luaX_next(ls);
      int rep_count = (int)asm_getint_ex(ls, ctx, NULL, NULL, NULL);
      if (rep_count < 0) {
        luaK_semerror(ls, "rep count must be non-negative");
      }
      checknext(ls, '{');
      int rep_start_pc = fs->pc;

      lasm_parse_body(ls, fs, ctx, line);

      int rep_end_pc = fs->pc;
      int instr_count = rep_end_pc - rep_start_pc;
      checknext(ls, '}');

      {
        int i;
        for (i = 1; i < rep_count; i++) {
          int j;
          for (j = 0; j < instr_count; j++) {
            Instruction copied_inst = fs->f->code[rep_start_pc + j];
            luaK_code(fs, copied_inst);
            luaK_fixline(fs, line);
          }
        }
      }
      testnext(ls, ';');
      continue;
    }

    /* junk "string" / junk count */
    if (strcmp(opname, "junk") == 0 || strcmp(opname, "garbage") == 0) {
      luaX_next(ls);
      if (ls->t.token == TK_STRING || ls->t.token == TK_RAWSTRING) {
        TString *junk_str = ls->t.seminfo.ts;
        const char *str = getstr(junk_str);
        size_t len = tsslen(junk_str);
        {
          Instruction len_inst = CREATE_Ax(OP_EXTRAARG, (int)(len & MAXARG_Ax));
          luaK_code(fs, len_inst);
          luaK_fixline(fs, line);
        }
        {
          size_t i;
          for (i = 0; i < len; i += 3) {
            unsigned int data = 0;
            data |= ((unsigned char)str[i]) << 0;
            if (i + 1 < len) data |= ((unsigned char)str[i + 1]) << 8;
            if (i + 2 < len) data |= ((unsigned char)str[i + 2]) << 16;
            data &= MAXARG_Ax;
            Instruction data_inst = CREATE_Ax(OP_EXTRAARG, (int)data);
            luaK_code(fs, data_inst);
            luaK_fixline(fs, line);
          }
        }
        luaX_next(ls);
      }
      else if (ls->t.token == TK_INT) {
        int junk_count = (int)ls->t.seminfo.i;
        luaX_next(ls);
        if (junk_count < 0) {
          luaK_semerror(ls, "junk count must be non-negative");
        }
        {
          int j;
          for (j = 0; j < junk_count; j++) {
            Instruction nop_inst = CREATE_ABCk(OP_NOP, 0, 0, 0, 0);
            luaK_code(fs, nop_inst);
            luaK_fixline(fs, line);
          }
        }
      }
      else {
        luaK_semerror(ls, "junk expects a string or integer count");
      }
      testnext(ls, ';');
      continue;
    }

    /* _if / _else / _endif */
    if (strcmp(opname, "_if") == 0 || strcmp(opname, "asmif") == 0) {
      luaX_next(ls);
      lua_Integer left_val = asm_getint_ex(ls, ctx, NULL, NULL, NULL);
      int cond_result = 0;
      lua_Integer right_val;
      if (ls->t.token == TK_EQ) {
        luaX_next(ls);
        right_val = asm_getint_ex(ls, ctx, NULL, NULL, NULL);
        cond_result = (left_val == right_val);
      }
      else if (ls->t.token == TK_NE) {
        luaX_next(ls);
        right_val = asm_getint_ex(ls, ctx, NULL, NULL, NULL);
        cond_result = (left_val != right_val);
      }
      else if (ls->t.token == '>') {
        luaX_next(ls);
        if (ls->t.token == '=') {
          luaX_next(ls);
          right_val = asm_getint_ex(ls, ctx, NULL, NULL, NULL);
          cond_result = (left_val >= right_val);
        } else {
          right_val = asm_getint_ex(ls, ctx, NULL, NULL, NULL);
          cond_result = (left_val > right_val);
        }
      }
      else if (ls->t.token == '<') {
        luaX_next(ls);
        if (ls->t.token == '=') {
          luaX_next(ls);
          right_val = asm_getint_ex(ls, ctx, NULL, NULL, NULL);
          cond_result = (left_val <= right_val);
        } else {
          right_val = asm_getint_ex(ls, ctx, NULL, NULL, NULL);
          cond_result = (left_val < right_val);
        }
      }
      else if (ls->t.token == TK_GE) {
        luaX_next(ls);
        right_val = asm_getint_ex(ls, ctx, NULL, NULL, NULL);
        cond_result = (left_val >= right_val);
      }
      else if (ls->t.token == TK_LE) {
        luaX_next(ls);
        right_val = asm_getint_ex(ls, ctx, NULL, NULL, NULL);
        cond_result = (left_val <= right_val);
      }
      else {
        cond_result = (left_val != 0);
      }

      if (!cond_result) {
        int nest_level = 1;
        while (nest_level > 0 && ls->t.token != TK_EOS && ls->t.token != ')') {
          if (ls->t.token == TK_NAME) {
            const char *name = getstr(ls->t.seminfo.ts);
            if (strcmp(name, "_if") == 0 || strcmp(name, "asmif") == 0) {
              nest_level++;
            }
            else if (strcmp(name, "_endif") == 0 || strcmp(name, "asmend") == 0) {
              if (nest_level == 1) {
                luaX_next(ls);
                nest_level = 0;
                break;
              } else { nest_level--; }
            }
            else if (nest_level == 1 && (strcmp(name, "_else") == 0 || strcmp(name, "asmelse") == 0)) {
              luaX_next(ls);
              testnext(ls, ';');
              nest_level = 0;
              break;
            }
          }
          if (nest_level > 0) luaX_next(ls);
        }
      }
      testnext(ls, ';');
      continue;
    }

    if (strcmp(opname, "_else") == 0 || strcmp(opname, "asmelse") == 0) {
      int nest_level = 1;
      luaX_next(ls);
      while (nest_level > 0 && ls->t.token != TK_EOS && ls->t.token != ')') {
        if (ls->t.token == TK_NAME) {
          const char *name = getstr(ls->t.seminfo.ts);
          if (strcmp(name, "_if") == 0 || strcmp(name, "asmif") == 0) {
            nest_level++;
          }
          else if (strcmp(name, "_endif") == 0 || strcmp(name, "asmend") == 0) {
            if (nest_level == 1) {
              luaX_next(ls);
              break;
            } else { nest_level--; }
          }
        }
        luaX_next(ls);
      }
      testnext(ls, ';');
      continue;
    }

    if (strcmp(opname, "_endif") == 0 || strcmp(opname, "asmend") == 0) {
      luaX_next(ls);
      testnext(ls, ';');
      continue;
    }

    /* -------------------------------------------------------------
    ** 优化功能：条件跳转伪指令 je, jne, jl, jle, jg, jge, jtrue, jfalse
    ** -------------------------------------------------------------
    */
    {
      int is_cj = 0;
      int cj_type = 0;
      if (strcmp(opname, "je") == 0 || strcmp(opname, "JE") == 0) { is_cj = 1; cj_type = 1; }
      else if (strcmp(opname, "jne") == 0 || strcmp(opname, "JNE") == 0) { is_cj = 1; cj_type = 2; }
      else if (strcmp(opname, "jl") == 0 || strcmp(opname, "JL") == 0) { is_cj = 1; cj_type = 3; }
      else if (strcmp(opname, "jle") == 0 || strcmp(opname, "JLE") == 0) { is_cj = 1; cj_type = 4; }
      else if (strcmp(opname, "jg") == 0 || strcmp(opname, "JG") == 0) { is_cj = 1; cj_type = 5; }
      else if (strcmp(opname, "jge") == 0 || strcmp(opname, "JGE") == 0) { is_cj = 1; cj_type = 6; }
      else if (strcmp(opname, "jtrue") == 0 || strcmp(opname, "JTRUE") == 0) { is_cj = 1; cj_type = 7; }
      else if (strcmp(opname, "jfalse") == 0 || strcmp(opname, "JFALSE") == 0) { is_cj = 1; cj_type = 8; }

      if (is_cj) {
        luaX_next(ls);

        int a = (int)asm_getint_ex(ls, ctx, NULL, NULL, NULL);

        int b = 0;
        int is_b_const = 0;
        int is_b_imm = 0;

        if (cj_type != 7 && cj_type != 8) {
          if (ls->t.token == '#') {
            int next_tok = luaX_lookahead(ls);
            if (next_tok == TK_NAME) {
              is_b_const = 1;
            } else if (next_tok == TK_STRING || next_tok == TK_RAWSTRING) {
              is_b_const = 1;
            } else {
              is_b_imm = 1;
            }
          }
          else if (ls->t.token == TK_INT || ls->t.token == '-') {
            is_b_imm = 1;
          }

          b = (int)asm_getint_ex(ls, ctx, NULL, NULL, NULL);
        }

        if (ls->t.token != TK_OR) {
          luaK_semerror(ls, "conditional jump requires @label as target");
        }
        luaX_next(ls);
        check(ls, TK_NAME);
        TString *label = ls->t.seminfo.ts;
        luaX_next(ls);

        Instruction comp_inst = 0;
        if (cj_type == 1) {
          if (is_b_const) {
            comp_inst = CREATE_ABCk(OP_EQK, a, b, 0, 0);
          } else if (is_b_imm) {
            asm_checkrange_signed(ls, b, -OFFSET_sC, OFFSET_sC, "sB");
            comp_inst = CREATE_ABCk(OP_EQI, a, int2sC(b), 0, 0);
          } else {
            comp_inst = CREATE_ABCk(OP_EQ, a, b, 0, 0);
          }
        }
        else if (cj_type == 2) {
          if (is_b_const) {
            comp_inst = CREATE_ABCk(OP_EQK, a, b, 1, 0);
          } else if (is_b_imm) {
            asm_checkrange_signed(ls, b, -OFFSET_sC, OFFSET_sC, "sB");
            comp_inst = CREATE_ABCk(OP_EQI, a, int2sC(b), 1, 0);
          } else {
            comp_inst = CREATE_ABCk(OP_EQ, a, b, 1, 0);
          }
        }
        else if (cj_type == 3) {
          if (is_b_const) {
            luaK_semerror(ls, "less-than comparison does not support constant pool operands");
          } else if (is_b_imm) {
            asm_checkrange_signed(ls, b, -OFFSET_sC, OFFSET_sC, "sB");
            comp_inst = CREATE_ABCk(OP_LTI, a, int2sC(b), 0, 0);
          } else {
            comp_inst = CREATE_ABCk(OP_LT, a, b, 0, 0);
          }
        }
        else if (cj_type == 4) {
          if (is_b_const) {
            luaK_semerror(ls, "less-equal comparison does not support constant pool operands");
          } else if (is_b_imm) {
            asm_checkrange_signed(ls, b, -OFFSET_sC, OFFSET_sC, "sB");
            comp_inst = CREATE_ABCk(OP_LEI, a, int2sC(b), 0, 0);
          } else {
            comp_inst = CREATE_ABCk(OP_LE, a, b, 0, 0);
          }
        }
        else if (cj_type == 5) {
          if (is_b_const) {
            luaK_semerror(ls, "greater-than comparison does not support constant pool operands");
          } else if (is_b_imm) {
            asm_checkrange_signed(ls, b, -OFFSET_sC, OFFSET_sC, "sB");
            comp_inst = CREATE_ABCk(OP_GTI, a, int2sC(b), 0, 0);
          } else {
            comp_inst = CREATE_ABCk(OP_LT, b, a, 0, 0);
          }
        }
        else if (cj_type == 6) {
          if (is_b_const) {
            luaK_semerror(ls, "greater-equal comparison does not support constant pool operands");
          } else if (is_b_imm) {
            asm_checkrange_signed(ls, b, -OFFSET_sC, OFFSET_sC, "sB");
            comp_inst = CREATE_ABCk(OP_GEI, a, int2sC(b), 0, 0);
          } else {
            comp_inst = CREATE_ABCk(OP_LE, b, a, 0, 0);
          }
        }
        else if (cj_type == 7) {
          comp_inst = CREATE_ABCk(OP_TEST, a, 0, 0, 0);
        }
        else if (cj_type == 8) {
          comp_inst = CREATE_ABCk(OP_TEST, a, 0, 0, 1);
        }

        luaK_code(fs, comp_inst);
        luaK_fixline(fs, line);

        asm_emit_jmp(ls, fs, ctx, label, line);

        testnext(ls, ';');
        continue;
      }
    }

    /* -------------------------------------------------------------
    ** 正常汇编指令解析
    ** -------------------------------------------------------------
    */
    opcode = find_opcode(opname);
    if (opcode < 0) {
      luaK_semerror(ls, "unknown opcode '%s' in asm", opname);
    }

    luaX_next(ls);
    mode = getOpMode(opcode);
    instpc = fs->pc;

    switch (mode) {
      case iABC: {
        int a = (int)asm_getint_ex(ls, ctx, NULL, NULL, NULL);
        int b = (int)asm_trygetint_ex(ls, ctx, 0, NULL, &pendingLabel, NULL);
        if (pendingLabel) needsPatch = 1;
        int c = (int)asm_trygetint_ex(ls, ctx, 0, NULL, pendingLabel ? NULL : &pendingLabel, NULL);
        if (pendingLabel && !needsPatch) needsPatch = 1;
        int k = (int)asm_trygetint(ls, 0);

        asm_checkrange(ls, a, MAXARG_A, "A");
        asm_checkrange(ls, b, MAXARG_B, "B");
        asm_checkrange(ls, c, MAXARG_C, "C");
        asm_checkrange(ls, k, 1, "k");

        if (opcode == OP_GTI || opcode == OP_GEI ||
            opcode == OP_LTI || opcode == OP_LEI ||
            opcode == OP_EQI || opcode == OP_MMBINI) {
          asm_checkrange_signed(ls, b, -OFFSET_sC, OFFSET_sC, "sB");
          b = int2sC(b);
          inst = CREATE_ABCk(opcode, a, b, c, k);
        }
        else if (opcode == OP_ADDI || opcode == OP_SHLI || opcode == OP_SHRI) {
          asm_checkrange_signed(ls, c, -OFFSET_sC, OFFSET_sC, "sC");
          c = int2sC(c);
          inst = CREATE_ABCk(opcode, a, b, c, k);
        }
        else {
          inst = CREATE_ABCk(opcode, a, b, c, k);
        }
        break;
      }
      case ivABC: {
        int a = (int)asm_getint_ex(ls, ctx, NULL, NULL, NULL);
        int vb = (int)asm_trygetint_ex(ls, ctx, 0, NULL, &pendingLabel, NULL);
        if (pendingLabel) needsPatch = 1;
        int vc = (int)asm_trygetint_ex(ls, ctx, 0, NULL, pendingLabel ? NULL : &pendingLabel, NULL);
        if (pendingLabel && !needsPatch) needsPatch = 1;
        int k = (int)asm_trygetint(ls, 0);

        asm_checkrange(ls, a, MAXARG_A, "A");
        asm_checkrange(ls, vb, MAXARG_vB, "vB");
        asm_checkrange(ls, vc, MAXARG_vC, "vC");
        asm_checkrange(ls, k, 1, "k");

        inst = CREATE_vABCk(opcode, a, vb, vc, k);
        break;
      }
      case iABx: {
        int a = (int)asm_getint_ex(ls, ctx, NULL, NULL, NULL);
        int isLabelRef = 0;
        unsigned int bx = (unsigned int)asm_getint_ex(ls, ctx, NULL, &pendingLabel, &isLabelRef);
        if (pendingLabel) {
          needsPatch = 1;
          if (opcode == OP_FORLOOP || opcode == OP_TFORLOOP ||
              opcode == OP_FORPREP || opcode == OP_TFORPREP) {
            isJumpInst = 1;
          }
        } else if (isLabelRef) {
          int offset;
          int target = (int)bx;
          if (opcode == OP_FORLOOP || opcode == OP_TFORLOOP) {
             offset = (instpc + 1) - target;
             if (offset <= 0) luaK_semerror(ls, "jump target for loop instruction must be backward");
             bx = (unsigned int)offset;
          } else if (opcode == OP_FORPREP || opcode == OP_TFORPREP) {
             offset = target - (instpc + 1);
             if (offset < 0) luaK_semerror(ls, "jump target for prep instruction must be forward");
             if (opcode == OP_FORPREP) offset--;
             bx = (unsigned int)offset;
          }
        }

        asm_checkrange(ls, a, MAXARG_A, "A");
        asm_checkrange(ls, bx, MAXARG_Bx, "Bx");
        inst = CREATE_ABx(opcode, a, bx);
        break;
      }
      case iAsBx: {
        int a = (int)asm_getint_ex(ls, ctx, NULL, NULL, NULL);
        int sbx = (int)asm_getint_ex(ls, ctx, NULL, &pendingLabel, NULL);
        if (pendingLabel) {
          needsPatch = 1;
          isJumpInst = 1;
        }

        asm_checkrange(ls, a, MAXARG_A, "A");
        asm_checkrange_signed(ls, sbx, -OFFSET_sBx, OFFSET_sBx, "sBx");
        inst = CREATE_ABx(opcode, a, cast_uint(sbx + OFFSET_sBx));
        break;
      }
      case iAx: {
        int ax = (int)asm_getint_ex(ls, ctx, NULL, &pendingLabel, NULL);
        if (pendingLabel) needsPatch = 1;

        asm_checkrange(ls, ax, MAXARG_Ax, "Ax");
        inst = CREATE_Ax(opcode, ax);
        break;
      }
      case isJ: {
        int isLabelRef = 0;
        int sj = (int)asm_getint_ex(ls, ctx, NULL, &pendingLabel, &isLabelRef);
        if (pendingLabel) {
          needsPatch = 1;
          isJumpInst = 1;
        } else if (isLabelRef) {
          sj = sj - (instpc + 1);
        }

        asm_checkrange_signed(ls, sj, -OFFSET_sJ, OFFSET_sJ, "sJ");
        inst = CREATE_sJ(opcode, sj + OFFSET_sJ, 0);
        break;
      }
      default: {
        luaK_semerror(ls, "unsupported opcode mode in asm");
        inst = 0;
      }
    }

    luaK_code(fs, inst);
    luaK_fixline(fs, line);

    if (needsPatch && pendingLabel) {
      asm_addpending(ls, ctx, pendingLabel, instpc, ls->linenumber, isJumpInst);
    }

    /* 自动生成 MMBIN 系列指令 */
    if (opcode >= OP_ADD && opcode <= OP_SHR) {
      int b = GETARG_B(inst);
      int c = GETARG_C(inst);
      TMS tm = cast(TMS, (opcode - OP_ADD) + TM_ADD);
      luaK_codeABCk(fs, OP_MMBIN, b, c, cast_int(tm), 0);
      luaK_fixline(fs, line);
    }
    else if (opcode == OP_ADDI) {
      int b = GETARG_B(inst);
      int sc = GETARG_C(inst);
      luaK_codeABCk(fs, OP_MMBINI, b, sc, TM_ADD, 0);
      luaK_fixline(fs, line);
    }
    else if (opcode == OP_SHLI) {
      int b = GETARG_B(inst);
      int sc = GETARG_C(inst);
      luaK_codeABCk(fs, OP_MMBINI, b, sc, TM_SHL, 0);
      luaK_fixline(fs, line);
    }
    else if (opcode == OP_SHRI) {
      int b = GETARG_B(inst);
      int sc = GETARG_C(inst);
      luaK_codeABCk(fs, OP_MMBINI, b, sc, TM_SHR, 0);
      luaK_fixline(fs, line);
    }
    else if (opcode >= OP_ADDK && opcode <= OP_IDIVK) {
      int b = GETARG_B(inst);
      int c = GETARG_C(inst);
      TMS tm = cast(TMS, (opcode - OP_ADDK) + TM_ADD);
      luaK_codeABCk(fs, OP_MMBINK, b, c, cast_int(tm), 0);
      luaK_fixline(fs, line);
    }
    else if (opcode >= OP_BANDK && opcode <= OP_BXORK) {
      int b = GETARG_B(inst);
      int c = GETARG_C(inst);
      TMS tm = cast(TMS, (opcode - OP_BANDK) + TM_BAND);
      luaK_codeABCk(fs, OP_MMBINK, b, c, cast_int(tm), 0);
      luaK_fixline(fs, line);
    }

    if (testAMode(opcode)) {
      int a = GETARG_A(inst);
      if (a >= fs->freereg) {
        int needed = a + 1 - fs->freereg;
        luaK_checkstack(fs, needed);
        fs->freereg = (a + 1);
      }
    }

    testnext(ls, ';');
  }
}


/*
** 解析 asm 入口：asm( ... ) 的完整处理
** 供 lparser.c 和 lcodegen.c 调用
** 参数：
**   ls - 词法状态
**   fs - 函数状态
**   line - 起始行号
*/
void lasm_execute (LexState *ls, FuncState *fs, int line) {
  AsmContext ctx;

  lasm_initcontext(ls->L, &ctx, NULL);

  luaX_next(ls);
  checknext(ls, '(');

  lasm_parse_body(ls, fs, &ctx, line);

  lasm_patchpending(ls, fs, &ctx);

  lasm_freecontext(ls->L, &ctx);

  checknext(ls, ')');
}