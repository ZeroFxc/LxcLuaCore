/*
** 自定义 opcode 扩展系统 - vm语言 微型解释器
** 允许用户通过 Lua 动态注册自定义 opcode 处理器
** 并提供了一个内置的微型 VM 解释器用于执行自定义 bytecode 序列
**
** 高级特性：
**   - vmctx 句柄：用户 opcode 可读写 MiniVM 寄存器
**   - vm.asm()：助记符汇编器
**   - vm.mcall()：函数式调用，传入参数，获取返回值
**   - 寄存器状态内省
*/

#define lvmustom_c
#define LUA_LIB

#include "lprefix.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>

#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"
#include "lstate.h"
#include "lobject.h"
#include "lopcodes.h"
#include "lvm.h"
#include "ldo.h"
#include "lstring.h"
#include "ltable.h"
#include "lgc.h"


/*
** 自定义 opcode 指令构造辅助宏
** 使用 OP_CUSTOM + Ax 字段编码用户 opcode
*/
static Instruction make_custom_inst(int user_op) {
  return CREATE_Ax(OP_CUSTOM, cast_uint(user_op));
}

static int get_custom_op(Instruction i) {
  return cast_int(GETARG_Ax(i));
}


/*================================================================
  微型 VM 解释器 (vm语言)
================================================================*/

typedef struct MiniVM {
  lua_State *L;
  TValue *regs;
  int nregs;
} MiniVM;

static MiniVM *minivm_create(lua_State *L, int nregs) {
  MiniVM *vm = lua_newuserdata(L, sizeof(MiniVM));
  memset(vm, 0, sizeof(MiniVM));
  vm->L = L;
  vm->nregs = nregs;
  TValue *regs_base = luaM_newvector(L, nregs, TValue);
  for (int i = 0; i < nregs; i++)
    setnilvalue(&regs_base[i]);
  vm->regs = regs_base;
  return vm;
}

static void minivm_destroy(MiniVM *vm) {
  luaM_freearray(vm->L, vm->regs, vm->nregs);
}

static TValue *minivm_getreg(MiniVM *vm, int reg) {
  if (reg >= 1 && reg <= vm->nregs)
    return &vm->regs[reg - 1];
  return NULL;
}

static void minivm_pushreg(MiniVM *vm, int reg) {
  lua_State *L = vm->L;
  if (reg >= 1 && reg <= vm->nregs) {
    setobj2s(L, L->top.p, &vm->regs[reg - 1]);
    L->top.p++;
  } else {
    setnilvalue(s2v(L->top.p));
    L->top.p++;
  }
}


/**
 * @brief 检查 MiniVM 中的值是否为"假"
 * MiniVM 扩展语义：数字 0 也被视为假（与标准 Lua 不同）
 * @param o 要检查的 TValue 指针
 * @return 1=假值, 0=真值
 */
static int minivm_isfalse(const TValue *o) {
  if (o == NULL) return 1;
  if (ttisnil(o)) return 1;
  if (ttisfalse(o)) return 1;
  if (ttisfloat(o) && fltvalue(o) == 0.0) return 1;
  if (ttisinteger(o) && ivalue(o) == 0) return 1;
  return 0;
}

/*
** 微型 VM 内置指令
*/
#define MINIVM_OP_NOP       0
#define MINIVM_OP_MOV       1
#define MINIVM_OP_LOADK     2
#define MINIVM_OP_ADD       3
#define MINIVM_OP_SUB       4
#define MINIVM_OP_MUL       5
#define MINIVM_OP_DIV       6
#define MINIVM_OP_EQ        7
#define MINIVM_OP_LT        8
#define MINIVM_OP_JMP       9
#define MINIVM_OP_JT        10
#define MINIVM_OP_JF        11
#define MINIVM_OP_CALL      12
#define MINIVM_OP_RET       13
#define MINIVM_OP_PRINT     14
#define MINIVM_OP_HALT      15

#define MINIVM_USER_BASE    16
#define MINIVM_MAX_OPS      128

#define MINIVM_REGISTRY_KEY  "_MINIVM_HANDLERS_"

static int get_minivm_handlers(lua_State *L) {
  lua_getfield(L, LUA_REGISTRYINDEX, MINIVM_REGISTRY_KEY);
  if (lua_isnil(L, -1)) {
    lua_pop(L, 1);
    lua_newtable(L);
    lua_pushvalue(L, -1);
    lua_setfield(L, LUA_REGISTRYINDEX, MINIVM_REGISTRY_KEY);
  }
  return lua_gettop(L);
}

/**
 * @brief vmctx:getreg(reg) — 读取 MiniVM 寄存器的值
 */
static int vmctx_getreg(lua_State *L) {
  lua_getfield(L, 1, "__ptr");
  MiniVM *vm = (MiniVM *)lua_touserdata(L, -1);
  lua_pop(L, 1);
  int reg = luaL_checkinteger(L, 2);
  if (vm && reg >= 1 && reg <= vm->nregs) {
    TValue *r = minivm_getreg(vm, reg);
    if (r) {
      setobj2s(L, L->top.p, r);
      L->top.p++;
      return 1;
    }
  }
  lua_pushnil(L);
  return 1;
}

/**
 * @brief vmctx:setreg(reg, value) — 写入 MiniVM 寄存器的值
 */
static int vmctx_setreg(lua_State *L) {
  lua_getfield(L, 1, "__ptr");
  MiniVM *vm = (MiniVM *)lua_touserdata(L, -1);
  lua_pop(L, 1);
  int reg = luaL_checkinteger(L, 2);
  if (vm && reg >= 1 && reg <= vm->nregs) {
    TValue *dst = minivm_getreg(vm, reg);
    if (dst) {
      setobj(L, dst, s2v(L->top.p - 1));
    }
  }
  return 0;
}

/**
 * @brief 创建一个 vmctx 表，提供 MiniVM 寄存器的 getreg/setreg/nregs 方法
 * 用户自定义 opcode 的第一个参数就是这个表
 * @param L Lua 状态
 * @param vm 微型 VM
 */
static void push_vmctx(lua_State *L, MiniVM *vm) {
  lua_newtable(L);
  lua_pushlightuserdata(L, vm);
  lua_setfield(L, -2, "__ptr");

  lua_pushcfunction(L, vmctx_getreg);
  lua_setfield(L, -2, "getreg");

  lua_pushcfunction(L, vmctx_setreg);
  lua_setfield(L, -2, "setreg");

  lua_pushinteger(L, vm->nregs);
  lua_setfield(L, -2, "nregs");
}

/**
 * @brief 执行微型 VM 单条指令
 */
static int minivm_exec_one(MiniVM *vm, int op, int a, int b, int c, int k) {
  lua_State *L = vm->L;
  StkId saved_top;

  switch (op) {
    case MINIVM_OP_NOP:
      return 1;

    case MINIVM_OP_MOV:
      if (b >= 1 && b <= vm->nregs) {
        TValue *src = minivm_getreg(vm, b);
        TValue *dst = minivm_getreg(vm, a);
        if (src && dst) setobj(L, dst, src);
      }
      return 1;

    case MINIVM_OP_LOADK:
      {
        TValue *dst = minivm_getreg(vm, a);
        if (dst) setfltvalue(dst, cast_num(b));
      }
      return 1;

    case MINIVM_OP_ADD:
    case MINIVM_OP_SUB:
    case MINIVM_OP_MUL:
    case MINIVM_OP_DIV:
      {
        TValue *ra = minivm_getreg(vm, a);
        TValue *rb = minivm_getreg(vm, b);
        TValue *rc = minivm_getreg(vm, c);
        if (!ra || !rb || !rc) return 1;
        lua_Number nb = ttisinteger(rb) ? cast_num(ivalue(rb)) : fltvalue(rb);
        lua_Number nc = ttisinteger(rc) ? cast_num(ivalue(rc)) : fltvalue(rc);
        lua_Number result;
        switch (op) {
          case MINIVM_OP_ADD: result = nb + nc; break;
          case MINIVM_OP_SUB: result = nb - nc; break;
          case MINIVM_OP_MUL: result = nb * nc; break;
          case MINIVM_OP_DIV: result = nb / nc; break;
          default: result = 0; break;
        }
        setfltvalue(ra, result);
      }
      return 1;

    case MINIVM_OP_EQ:
    case MINIVM_OP_LT:
      {
        TValue *ra = minivm_getreg(vm, a);
        TValue *rb = minivm_getreg(vm, b);
        TValue *rc = minivm_getreg(vm, c);
        if (!ra || !rb || !rc) return 1;
        int result = (op == MINIVM_OP_EQ)
          ? luaV_equalobj(L, rb, rc)
          : luaV_lessthan(L, rb, rc);
        setbtvalue(ra);
        if (!result) setnilvalue(ra);
      }
      return 1;

    case MINIVM_OP_JMP:
      return k + 1;

    case MINIVM_OP_JT:
      {
        TValue *ra = minivm_getreg(vm, a);
        if (ra && !minivm_isfalse(ra)) return k + 1;
        return 1;
      }

    case MINIVM_OP_JF:
      {
        TValue *ra = minivm_getreg(vm, a);
        if (ra && minivm_isfalse(ra)) return k + 1;
        return 1;
      }

    case MINIVM_OP_CALL:
      {
        TValue *func = minivm_getreg(vm, a);
        if (!func || !ttisfunction(func)) return 1;
        saved_top = L->top.p;
        setobj2s(L, L->top.p, func);
        L->top.p++;
        int nargs = (b > 0) ? b - 1 : 0;
        for (int i = 1; i <= nargs; i++) {
          TValue *arg = minivm_getreg(vm, a + i);
          if (arg) { setobj2s(L, L->top.p, arg); L->top.p++; }
        }
        int status = lua_pcall(L, nargs, LUA_MULTRET, 0);
        if (status == LUA_OK) { L->top.p = saved_top; return 1; }
        L->top.p = saved_top;
        return -1;
      }

    case MINIVM_OP_RET:
      return -1;

    case MINIVM_OP_PRINT:
      {
        TValue *ra = minivm_getreg(vm, a);
        if (ra) {
          StkId saved = L->top.p;
          setobj2s(L, L->top.p, ra); L->top.p++;
          if (luaL_callmeta(L, -1, "__tostring")) {
            const char *s = lua_tostring(L, -1);
            if (s) lua_writestring(s, strlen(s));
            lua_pop(L, 1);
          } else {
            const char *s = luaL_tolstring(L, -1, NULL);
            if (s) lua_writestring(s, strlen(s));
            lua_pop(L, 2);
          }
          lua_writestring("\n", 1);
          L->top.p = saved;
        }
      }
      return 1;

    case MINIVM_OP_HALT:
      return -2;

    default:
      if (op >= MINIVM_USER_BASE && op < MINIVM_MAX_OPS) {
        int tbl = get_minivm_handlers(L);
        lua_rawgeti(L, tbl, op);
        if (lua_isfunction(L, -1)) {
          /* 用户 opcode 签名: function(vmctx, a, b, c, k) */
          push_vmctx(L, vm);       /* arg1: vmctx 句柄 */
          lua_pushinteger(L, a);   /* arg2: a */
          lua_pushinteger(L, b);   /* arg3: b */
          lua_pushinteger(L, c);   /* arg4: c */
          lua_pushinteger(L, k);   /* arg5: k */
          if (lua_pcall(L, 5, 1, 0) == LUA_OK) {
            int jump = lua_tointeger(L, -1);
            lua_pop(L, 2);
            return jump;
          }
          lua_pop(L, 2);
          return -1;
        }
        lua_pop(L, 2);
      }
      return 1;
  }
}


/*================================================================
  解析单条指令的参数（从 table 中提取 op/a/b/c/k）
================================================================*/
static int parse_inst(lua_State *L, int idx, int *op, int *a, int *b, int *c, int *k) {
  if (!lua_istable(L, idx)) {
    lua_pop(L, 1);
    return 0;
  }
  lua_getfield(L, idx, "op"); *op = lua_tointeger(L, -1); lua_pop(L, 1);
  lua_getfield(L, idx, "a");  *a  = luaL_optinteger(L, -1, 0); lua_pop(L, 1);
  lua_getfield(L, idx, "b");  *b  = luaL_optinteger(L, -1, 0); lua_pop(L, 1);
  lua_getfield(L, idx, "c");  *c  = luaL_optinteger(L, -1, 0); lua_pop(L, 1);
  lua_getfield(L, idx, "k");  *k  = luaL_optinteger(L, -1, 0); lua_pop(L, 1);
  lua_pop(L, 1);
  return 1;
}

/**
 * @brief 内部执行函数：解析 bytecode 并执行
 * @param vm 微型 VM
 * @param bc_idx bytecode 表在栈上的索引
 * @return -2=HALT, -1=RET/error, 0=end
 */
static int exec_minivm_inner(MiniVM *vm, int bc_idx) {
  lua_State *L = vm->L;
  int n_instrs = luaL_len(L, bc_idx);
  int pc = 1;

  while (pc >= 1 && pc <= n_instrs) {
    lua_rawgeti(L, bc_idx, pc);
    int op, a, b, c, k;
    if (!parse_inst(L, -1, &op, &a, &b, &c, &k)) break;

    int result = minivm_exec_one(vm, op, a, b, c, k);
    if (result == -1) return -1;
    if (result == -2) return -2;
    if (result == 0) result = 1;
    pc += result;
  }
  return 0;
}


/*================================================================
  Lua API 函数
================================================================*/

/**
 * @brief vm.execmini(bytecode, nregs)
 * 执行微型 VM bytecode 序列
 * bytecode 是一个包含 {op, a, b, c, k} 指令的数组
 */
static int vm_execmini(lua_State *L) {
  luaL_checktype(L, 1, LUA_TTABLE);
  int nregs = luaL_optinteger(L, 2, 16);

  MiniVM *vm = minivm_create(L, nregs);
  (void)lua_touserdata(L, -1);
  lua_pop(L, 1);

  exec_minivm_inner(vm, 1);

  minivm_destroy(vm);
  return 0;
}

/**
 * @brief vm.mcall(bytecode, nregs, ...) -> results
 * 函数式调用 MiniVM：将 Lua 参数写入 R1..Rn，执行后返回 R1..Rn
 * MiniVM 写入 R1 的值会作为第一个返回值，写入 R2 的值作为第二个返回值，以此类推
 */
static int vm_mcall(lua_State *L) {
  luaL_checktype(L, 1, LUA_TTABLE);
  int nregs = luaL_optinteger(L, 2, 16);
  int nargs = lua_gettop(L) - 2;
  if (nargs > nregs) nargs = nregs;

  MiniVM *vm = minivm_create(L, nregs);
  (void)lua_touserdata(L, -1);
  lua_pop(L, 1);

  /* 将 Lua 参数写入 MiniVM 寄存器 R1..Rn */
  for (int i = 0; i < nargs; i++) {
    TValue *dst = minivm_getreg(vm, i + 1);
    if (dst) setobj(L, dst, s2v(L->top.p - nargs + i));
  }

  exec_minivm_inner(vm, 1);

  /* 返回当前寄存器 R1..Rret 的值 */
  int nret = 0;
  for (int i = 1; i <= nregs; i++) {
    TValue *reg = minivm_getreg(vm, i);
    if (!reg || ttisnil(reg)) break;
    setobj2s(L, L->top.p, reg);
    L->top.p++;
    nret++;
  }

  minivm_destroy(vm);
  return nret;
}

/**
 * @brief vm.setuserminiop(op, handler)
 * 为微型 VM 注册用户自定义指令
 */
static int vm_setuserminiop(lua_State *L) {
  int op = luaL_checkinteger(L, 1);
  luaL_checktype(L, 2, LUA_TFUNCTION);
  if (op < MINIVM_USER_BASE || op >= MINIVM_MAX_OPS)
    luaL_error(L, "user opcode %d out of range (%d ~ %d)", op, MINIVM_USER_BASE, MINIVM_MAX_OPS - 1);
  int tbl = get_minivm_handlers(L);
  lua_pushvalue(L, 2);
  lua_rawseti(L, tbl, op);
  lua_pop(L, 1);
  return 0;
}


/*================================================================
  主 VM OP_CUSTOM 处理器系统
================================================================*/
#define CUSTOM_OP_REGKEY  "_CUSTOMOP_REFS_"

static int get_custom_op_registry(lua_State *L) {
  lua_getfield(L, LUA_REGISTRYINDEX, CUSTOM_OP_REGKEY);
  if (lua_isnil(L, -1)) {
    lua_pop(L, 1);
    lua_newtable(L);
    lua_pushvalue(L, -1);
    lua_setfield(L, LUA_REGISTRYINDEX, CUSTOM_OP_REGKEY);
  }
  return lua_gettop(L);
}

static int custom_op_lua_wrapper(lua_State *L) {
  lua_pushvalue(L, lua_upvalueindex(1));
  lua_insert(L, 1);
  lua_call(L, lua_gettop(L) - 1, LUA_MULTRET);
  return lua_gettop(L);
}

static int vm_setop(lua_State *L) {
  int op = luaL_checkinteger(L, 1);
  luaL_checktype(L, 2, LUA_TFUNCTION);
  if (op < 0 || op >= OP_CUSTOM_COUNT)
    luaL_error(L, "custom opcode %d out of range (0 ~ %d)", op, OP_CUSTOM_COUNT - 1);

  int regidx = get_custom_op_registry(L);
  lua_pushvalue(L, 2);
  lua_rawseti(L, regidx, op + 1);

  if (G(L)->custom_op_handlers[op] == NULL)
    G(L)->custom_op_count++;

  if (lua_iscfunction(L, 2)) {
    G(L)->custom_op_handlers[op] = lua_tocfunction(L, 2);
  } else {
    lua_pushvalue(L, 2);
    lua_pushcclosure(L, custom_op_lua_wrapper, 1);
    G(L)->custom_op_handlers[op] = lua_tocfunction(L, -1);
    lua_pop(L, 1);
  }
  lua_pop(L, 1);
  return 0;
}

static int vm_getop(lua_State *L) {
  int op = luaL_checkinteger(L, 1);
  if (op < 0 || op >= OP_CUSTOM_COUNT)
    luaL_error(L, "custom opcode %d out of range (0 ~ %d)", op, OP_CUSTOM_COUNT - 1);
  int regidx = get_custom_op_registry(L);
  lua_rawgeti(L, regidx, op + 1);
  lua_remove(L, regidx);
  return 1;
}

static int vm_delop(lua_State *L) {
  int op = luaL_checkinteger(L, 1);
  if (op < 0 || op >= OP_CUSTOM_COUNT)
    luaL_error(L, "custom opcode %d out of range (0 ~ %d)", op, OP_CUSTOM_COUNT - 1);
  if (G(L)->custom_op_handlers[op] != NULL)
    G(L)->custom_op_count--;
  G(L)->custom_op_handlers[op] = NULL;
  int regidx = get_custom_op_registry(L);
  lua_pushnil(L);
  lua_rawseti(L, regidx, op + 1);
  lua_pop(L, 1);
  return 0;
}

static int vm_listops(lua_State *L) {
  lua_newtable(L);
  int idx = 1;
  for (int i = 0; i < OP_CUSTOM_COUNT; i++) {
    if (G(L)->custom_op_handlers[i] != NULL) {
      lua_pushinteger(L, i);
      lua_rawseti(L, -2, idx++);
    }
  }
  return 1;
}

static int vm_makeinst(lua_State *L) {
  int op = luaL_checkinteger(L, 1);
  if (op < 0 || op >= OP_CUSTOM_COUNT)
    luaL_error(L, "custom opcode %d out of range (0 ~ %d)", op, OP_CUSTOM_COUNT - 1);
  lua_pushinteger(L, cast_Integer(make_custom_inst(op)));
  return 1;
}

static int vm_getinstop(lua_State *L) {
  lua_Integer inst = luaL_checkinteger(L, 1);
  OpCode opc = GET_OPCODE(cast_Inst(inst));
  if (opc != OP_CUSTOM) { lua_pushnil(L); return 1; }
  lua_pushinteger(L, get_custom_op(cast_Inst(inst)));
  return 1;
}

static int vm_opcount(lua_State *L) {
  lua_pushinteger(L, G(L)->custom_op_count);
  return 1;
}


/*================================================================
  vm语言 汇编器 (vm.asm)
  支持助记符语法: "LOADK R1, 10" → {op=2, a=1, b=10}
================================================================*/

/** 助记符 → opcode 映射表 */
static const struct {
  const char *name;
  int op;
} mini_mnemonics[] = {
  {"NOP",   MINIVM_OP_NOP},   {"MOV",   MINIVM_OP_MOV},
  {"LOADK", MINIVM_OP_LOADK}, {"ADD",   MINIVM_OP_ADD},
  {"SUB",   MINIVM_OP_SUB},   {"MUL",   MINIVM_OP_MUL},
  {"DIV",   MINIVM_OP_DIV},   {"EQ",    MINIVM_OP_EQ},
  {"LT",    MINIVM_OP_LT},    {"JMP",   MINIVM_OP_JMP},
  {"JT",    MINIVM_OP_JT},    {"JF",    MINIVM_OP_JF},
  {"CALL",  MINIVM_OP_CALL},  {"RET",   MINIVM_OP_RET},
  {"PRINT", MINIVM_OP_PRINT}, {"HALT",  MINIVM_OP_HALT},
  {NULL, 0}
};

/**
 * @brief 查找助记符对应的 opcode
 */
static int mnemonic_to_op(const char *name) {
  for (int i = 0; mini_mnemonics[i].name; i++) {
    if (strcasecmp(name, mini_mnemonics[i].name) == 0)
      return mini_mnemonics[i].op;
  }
  return -1;
}

/**
 * @brief 跳过空白
 */
static const char *skip_space(const char *p, const char *end) {
  while (p < end && isspace(*p)) p++;
  return p;
}

/**
 * @brief 读取一个 token（identifier 或 number 或 逗号）
 * 将 token 复制到 buf，返回下一个位置
 */
static const char *read_token(const char *p, const char *end, char *buf, int bufsz) {
  p = skip_space(p, end);
  if (p >= end) return p;

  if (*p == ',') {
    buf[0] = ','; buf[1] = '\0';
    return p + 1;
  }

  int i = 0;
  while (p < end && !isspace(*p) && *p != ',' && i < bufsz - 1) {
    buf[i++] = *p++;
  }
  buf[i] = '\0';
  return p;
}

/**
 * @brief 解析寄存器编号: "R1" → 1, "R10" → 10
 */
static int parse_reg(const char *s) {
  if (toupper(s[0]) == 'R')
    return atoi(s + 1);
  return atoi(s);
}


/*================================================================
  标签系统 — 用于 vm.asm 的标签定义与解析
================================================================*/

#define ASM_MAX_LABELS 128

/**
 * @brief 存储一个标签的名称和所在 PC 位置
 */
typedef struct AsmLabel {
  char name[64];
  int pc;
} AsmLabel;

/**
 * @brief 在标签列表中查找指定名称的标签，返回其 PC 位置
 * @param labels 标签数组
 * @param n 标签数量
 * @param name 要查找的标签名（不含冒号前缀）
 * @return 找到返回 PC (>=1)，未找到返回 0
 */
static int asm_find_label(AsmLabel *labels, int n, const char *name) {
  for (int i = 0; i < n; i++) {
    if (strcmp(labels[i].name, name) == 0)
      return labels[i].pc;
  }
  return 0;
}

/**
 * @brief 扫描源代码，跳过当前行剩余内容（到换行符为止）
 * @param p 当前位置指针
 * @param end 源代码结尾
 * @return 下一行起始位置（跳过换行符）
 */
static const char *skip_to_next_line(const char *p, const char *end) {
  while (p < end && *p != '\n' && *p != '\r') p++;
  while (p < end && (*p == '\n' || *p == '\r')) p++;
  return p;
}

/**
 * @brief vm.asm(code_str)
 * 汇编 vm语言 源代码为 bytecode 表（可直接传给 execmini/mcall）
 *
 * 支持语法：
 *   MNEMONIC Ra, Rb, Rc     — 三操作数 (ADD/SUB/MUL/DIV/EQ/LT/CALL)
 *   MNEMONIC Ra, val        — 两操作数 (LOADK/MOV)
 *   MNEMONIC Ra             — 单操作数 (PRINT/INC/DEC 等用户指令)
 *   MNEMONIC                — 零操作数 (NOP/RET/HALT)
 *   :labelname              — 标签定义（单独一行）
 *
 * 跳转指令的标签引用（替代手工计算偏移量）：
 *   JMP :label              — 无条件跳转到标签位置
 *   JT  Ra, :label          — Ra 为真时跳转到标签
 *   JF  Ra, :label          — Ra 为假时跳转到标签
 *
 * 跳转也可使用旧式的数字偏移量语法（向后兼容）：
 *   JMP 2                   — 无条件跳转 2 条指令
 *
 * 例（标签语法）:
 *   LOADK R1, 5
 *   :loop
 *   PRINT R1
 *   LOADK R2, 1
 *   SUB  R1, R1, R2
 *   JT   R1, :loop
 *   HALT
 */
static int vm_asm(lua_State *L) {
  size_t len;
  const char *code = luaL_checklstring(L, 1, &len);

  const char *end = code + len;
  char token[64];
  int values[3];
  int nvals;

  /*============================================================
    第一遍扫描：收集标签定义，确定每条标签对应的 PC 位置
  ============================================================*/
  AsmLabel labels[ASM_MAX_LABELS];
  int nlabels = 0;
  int scan_pc = 1;
  const char *p = code;

  while (p < end) {
    p = skip_space(p, end);
    if (p >= end) break;
    if (*p == '\n' || *p == '\r') { p++; continue; }
    if (*p == '#' || *p == ';' || *p == '/') {
      p = skip_to_next_line(p, end);
      continue;
    }

    /* 检测标签定义 :labelname */
    if (*p == ':') {
      p++;
      if (p >= end || isspace(*p) || *p == '\n' || *p == '\r') {
        luaL_error(L, "vm.asm: empty label name at PC %d", scan_pc);
      }
      const char *name_start = p;
      while (p < end && !isspace(*p) && *p != '\n' && *p != '\r')
        p++;
      int name_len = (int)(p - name_start);
      if (name_len >= 63)
        luaL_error(L, "vm.asm: label name too long (max 63 chars)");
      if (nlabels >= ASM_MAX_LABELS)
        luaL_error(L, "vm.asm: too many labels (max %d)", ASM_MAX_LABELS);

      AsmLabel *lb = &labels[nlabels];
      memcpy(lb->name, name_start, name_len);
      lb->name[name_len] = '\0';

      /* 检查重复标签 */
      for (int i = 0; i < nlabels; i++) {
        if (strcmp(labels[i].name, lb->name) == 0)
          luaL_error(L, "vm.asm: duplicate label ':%s'", lb->name);
      }
      lb->pc = scan_pc;
      nlabels++;
      p = skip_to_next_line(p, end);
      continue;
    }

    /* 读取助记符并推进扫描 PC */
    p = read_token(p, end, token, sizeof(token));
    if (token[0] == '\0') continue;

    int op = mnemonic_to_op(token);
    if (op < 0) op = atoi(token);

    scan_pc++;
    p = skip_to_next_line(p, end);
  }

  /*============================================================
    第二遍扫描：生成 bytecode，解析标签引用
  ============================================================*/
  lua_newtable(L);
  int pc = 1;
  p = code;

  while (p < end) {
    p = skip_space(p, end);
    if (p >= end) break;
    if (*p == '\n' || *p == '\r') { p++; continue; }
    if (*p == '#' || *p == ';' || *p == '/') {
      p = skip_to_next_line(p, end);
      continue;
    }

    /* 跳过标签定义行（第一遍已经处理） */
    if (*p == ':') {
      p = skip_to_next_line(p, end);
      continue;
    }

    /* 读取助记符 */
    p = read_token(p, end, token, sizeof(token));
    if (token[0] == '\0') continue;

    int op = mnemonic_to_op(token);
    if (op < 0) {
      op = atoi(token);
    }

    /* 读取操作数：数字值存入 values[]，标签引用存入 label_val */
    nvals = 0;
    int label_val = 0;   /* 非0表示存在标签引用，存储标签的 PC */

    while (nvals < 3) {
      const char *next = skip_space(p, end);
      if (next >= end || *next == '\n' || *next == '\r' || *next == '#' || *next == ';')
        break;
      {
        const char *chk = p;
        int crossed = 0;
        while (chk < next) {
          if (*chk == '\n' || *chk == '\r') { crossed = 1; break; }
          chk++;
        }
        if (crossed) break;
      }
      p = read_token(p, end, token, sizeof(token));
      if (token[0] == ',' || token[0] == '\0') continue;

      /* 检测标签引用 :labelname */
      if (token[0] == ':') {
        int target = asm_find_label(labels, nlabels, token + 1);
        if (target == 0)
          luaL_error(L, "vm.asm: undefined label '%s' at line near PC %d", token, pc);
        label_val = target;
        continue;
      }
      values[nvals++] = parse_reg(token);
    }

    int a = 0, b = 0, c = 0, k_val = 0;

    if (op == MINIVM_OP_JMP) {
      if (label_val > 0)
        k_val = label_val - pc - 1;
      else
        k_val = (nvals > 0) ? values[0] : 0;
    } else if (op == MINIVM_OP_JT || op == MINIVM_OP_JF) {
      a = (nvals > 0) ? values[0] : 0;
      if (label_val > 0)
        k_val = label_val - pc - 1;
      else
        k_val = (nvals > 1) ? values[1] : 0;
    } else {
      a = (nvals > 0) ? values[0] : 0;
      b = (nvals > 1) ? values[1] : 0;
      c = (nvals > 2) ? values[2] : 0;
    }

    lua_newtable(L);
    lua_pushinteger(L, op);    lua_setfield(L, -2, "op");
    lua_pushinteger(L, a);     lua_setfield(L, -2, "a");
    lua_pushinteger(L, b);     lua_setfield(L, -2, "b");
    lua_pushinteger(L, c);     lua_setfield(L, -2, "c");
    lua_pushinteger(L, k_val); lua_setfield(L, -2, "k");
    lua_rawseti(L, -2, pc++);

    p = skip_to_next_line(p, end);
  }

  return 1;
}


/*================================================================
  vm语言 原始编译器 (vm.compile) — 保留兼容性
================================================================*/
static int vm_compile(lua_State *L) {
  size_t len;
  const char *code = luaL_checklstring(L, 1, &len);

  lua_newtable(L);
  int pc = 1;
  const char *p = code;
  const char *end = code + len;

  while (p < end) {
    while (p < end && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) p++;
    if (p >= end || *p == ';' || *p == '#') {
      while (p < end && *p != '\n') p++;
      continue;
    }
    int op = 0, a = 0, b = 0, c = 0;
    int n = sscanf(p, "%d %d %d %d", &op, &a, &b, &c);
    if (n >= 1) {
      lua_newtable(L);
      lua_pushinteger(L, op); lua_setfield(L, -2, "op");
      lua_pushinteger(L, a);  lua_setfield(L, -2, "a");
      lua_pushinteger(L, b);  lua_setfield(L, -2, "b");
      lua_pushinteger(L, c);  lua_setfield(L, -2, "c");
      lua_pushinteger(L, 0);  lua_setfield(L, -2, "k");
      lua_rawseti(L, -2, pc++);
    }
    while (p < end && *p != '\n') p++;
  }
  return 1;
}


/*================================================================
  库注册
================================================================*/

static const luaL_Reg vmcustom_funcs[] = {
  {"execmini",      vm_execmini},
  {"mcall",         vm_mcall},
  {"setuserminiop", vm_setuserminiop},
  {"setop",         vm_setop},
  {"getop",         vm_getop},
  {"delop",         vm_delop},
  {"listops",       vm_listops},
  {"makeinst",      vm_makeinst},
  {"getinstop",     vm_getinstop},
  {"opcount",       vm_opcount},
  {"compile",       vm_compile},
  {"asm",           vm_asm},
  {NULL, NULL}
};

LUAMOD_API int luaopen_vmcustom(lua_State *L) {
  luaL_newlib(L, vmcustom_funcs);

  lua_pushinteger(L, OP_CUSTOM_COUNT);   lua_setfield(L, -2, "MAX_CUSTOM_OPS");
  lua_pushinteger(L, MINIVM_USER_BASE);  lua_setfield(L, -2, "MINIVM_USER_BASE");
  lua_pushinteger(L, MINIVM_MAX_OPS);    lua_setfield(L, -2, "MINIVM_MAX_OPS");

  lua_pushinteger(L, MINIVM_OP_NOP);   lua_setfield(L, -2, "MINI_NOP");
  lua_pushinteger(L, MINIVM_OP_MOV);   lua_setfield(L, -2, "MINI_MOV");
  lua_pushinteger(L, MINIVM_OP_LOADK); lua_setfield(L, -2, "MINI_LOADK");
  lua_pushinteger(L, MINIVM_OP_ADD);   lua_setfield(L, -2, "MINI_ADD");
  lua_pushinteger(L, MINIVM_OP_SUB);   lua_setfield(L, -2, "MINI_SUB");
  lua_pushinteger(L, MINIVM_OP_MUL);   lua_setfield(L, -2, "MINI_MUL");
  lua_pushinteger(L, MINIVM_OP_DIV);   lua_setfield(L, -2, "MINI_DIV");
  lua_pushinteger(L, MINIVM_OP_EQ);    lua_setfield(L, -2, "MINI_EQ");
  lua_pushinteger(L, MINIVM_OP_LT);    lua_setfield(L, -2, "MINI_LT");
  lua_pushinteger(L, MINIVM_OP_JMP);   lua_setfield(L, -2, "MINI_JMP");
  lua_pushinteger(L, MINIVM_OP_JT);    lua_setfield(L, -2, "MINI_JT");
  lua_pushinteger(L, MINIVM_OP_JF);    lua_setfield(L, -2, "MINI_JF");
  lua_pushinteger(L, MINIVM_OP_CALL);  lua_setfield(L, -2, "MINI_CALL");
  lua_pushinteger(L, MINIVM_OP_RET);   lua_setfield(L, -2, "MINI_RET");
  lua_pushinteger(L, MINIVM_OP_PRINT); lua_setfield(L, -2, "MINI_PRINT");
  lua_pushinteger(L, MINIVM_OP_HALT);  lua_setfield(L, -2, "MINI_HALT");

  return 1;
}