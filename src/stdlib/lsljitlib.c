/*
** SLJIT (Stack-less JIT) 库 - 在 Lua 中直接编写原生机器码
** 参考 luaSljit 设计，适配 LXCLUA 项目架构和 SLJIT 0.95 API
** 完整支持：整数/浮点运算、函数调用、原子操作、条件选择、运行时代码修改
*/

#define lsljitlib_c
#define LUA_LIB

#include "lprefix.h"

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <assert.h>
#include <string.h>
#include <math.h>

#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"

/* SLJIT 头文件 */
#include "../jit/sljitLir.h"

/* ============================================================
 * 元表名称定义
 * ============================================================ */
#define CODE_METATABLE    "sljit.code"       /* 生成的机器码 */
#define COMP_METATABLE    "sljit.compiler"   /* 编译器 */
#define CONST_METATABLE   "sljit.const"      /* 可重定位常量 */
#define JUMP_METATABLE    "sljit.jump"       /* 跳转点 */
#define LABEL_METATABLE   "sljit.label"      /* 标签 */
#define ARG_METATABLE     "sljit.argument"   /* 参数/操作码/寄存器 */

/* uservalue 表中 compiler 的索引 */
#define COMPILER_UVAL_INDEX  1

/* 错误信息 */
#define ERR_NOCONV(type)  "conversion to " type " failed"

/* ============================================================
 * 常量类型标志
 * ============================================================ */
typedef int constant_flag_t;

#define TYPE_NOTUD   0   /* 非 userdata 常量（纯整数值） */
#define TYPE_REG     1   /* 寄存器 */
#define TYPE_OP0     2   /* 零操作数指令 */
#define TYPE_OP1     3   /* 单操作数指令 */
#define TYPE_OP2     4   /* 双操作数指令 */
#define TYPE_CMP     5   /* 比较类型 */
#define TYPE_FOP1    6   /* 浮点单操作数指令 */
#define TYPE_FOP2    7   /* 浮点双操作数指令 */
#define TYPE_FCMP    8   /* 浮点比较类型 */
#define TYPE_MASK   15   /* 类型掩码 */

/* TYPE_REG 子标志 */
#define REG_IMM    0     /* 立即数寻址 */
#define REG_ONLY  16     /* 仅寄存器 */
#define REG_MASK  16     /* 寄存器标志掩码 */

/* TYPE_OP1 子标志 */
#define OP1_RET   32     /* 可用于 return 指令 */

/* TYPE_CMP 子标志 */
#define CMP_JMP   64     /* 可用于条件跳转 */

/* ============================================================
 * 数据结构定义
 * ============================================================ */

/* SLJIT 参数/常量 userdata */
typedef struct luaSljitArg {
  constant_flag_t flags;   /* 类型标志 */
  sljit_s32 argi;          /* 寄存器编号/操作码/比较类型 */
  sljit_sw argw;           /* 立即数值或偏移量 */
} luaSljitArg;

/* 常量表条目 */
typedef struct constant {
  const char *name;
  luaSljitArg arg;
} constant;

/* constants 数组前向声明（供 parsearg 等前置函数引用） */
static const constant constants[];

/* 编译器 userdata */
typedef struct luaSljitCompiler {
  struct sljit_compiler *compiler;
} luaSljitCompiler;

/* 跳转点 userdata */
typedef struct luaSljitJump {
  struct sljit_jump *jump;
} luaSljitJump;

/* 标签 userdata */
typedef struct luaSljitLabel {
  struct sljit_label *label;
} luaSljitLabel;

/* 常量 userdata */
typedef struct luaSljitConst {
  struct sljit_const *const_;
} luaSljitConst;

/* 生成代码 userdata */
typedef struct luaSljitCode {
  void *code;
} luaSljitCode;

/* ============================================================
 * 常量定义宏
 * ============================================================ */
#define DEFCONST(name, flags)  { #name, { (flags), SLJIT_##name, 0 } }

/* ============================================================
 * 常量表 (SLJIT 0.95)
 * ============================================================ */
static const constant constants[] = {

  /* ===== 寄存器数量信息常量（非 userdata） ===== */
  DEFCONST(NUMBER_OF_REGISTERS,             TYPE_NOTUD),
  DEFCONST(NUMBER_OF_SAVED_REGISTERS,       TYPE_NOTUD),
  DEFCONST(NUMBER_OF_FLOAT_REGISTERS,       TYPE_NOTUD),
  DEFCONST(NUMBER_OF_SAVED_FLOAT_REGISTERS, TYPE_NOTUD),
  DEFCONST(NUMBER_OF_VECTOR_REGISTERS,      TYPE_NOTUD),
  DEFCONST(NUMBER_OF_SAVED_VECTOR_REGISTERS,TYPE_NOTUD),
  DEFCONST(MAX_LOCAL_SIZE,                  TYPE_NOTUD),
  DEFCONST(WORD_SHIFT,                      TYPE_NOTUD),

  /* ===== 通用寄存器 ===== */
  DEFCONST(R0,     TYPE_REG|REG_ONLY),
  DEFCONST(R1,     TYPE_REG|REG_ONLY),
  DEFCONST(R2,     TYPE_REG|REG_ONLY),
  DEFCONST(R3,     TYPE_REG|REG_ONLY),
  DEFCONST(R4,     TYPE_REG|REG_ONLY),
  DEFCONST(R5,     TYPE_REG|REG_ONLY),
  DEFCONST(R6,     TYPE_REG|REG_ONLY),
  DEFCONST(R7,     TYPE_REG|REG_ONLY),
  DEFCONST(R8,     TYPE_REG|REG_ONLY),
  DEFCONST(R9,     TYPE_REG|REG_ONLY),
  DEFCONST(S0,     TYPE_REG|REG_ONLY),
  DEFCONST(S1,     TYPE_REG|REG_ONLY),
  DEFCONST(S2,     TYPE_REG|REG_ONLY),
  DEFCONST(S3,     TYPE_REG|REG_ONLY),
  DEFCONST(S4,     TYPE_REG|REG_ONLY),
  DEFCONST(S5,     TYPE_REG|REG_ONLY),
  DEFCONST(S6,     TYPE_REG|REG_ONLY),
  DEFCONST(S7,     TYPE_REG|REG_ONLY),
  DEFCONST(S8,     TYPE_REG|REG_ONLY),
  DEFCONST(S9,     TYPE_REG|REG_ONLY),
  DEFCONST(SP,     TYPE_REG|REG_ONLY),
  DEFCONST(IMM,    TYPE_REG|REG_IMM),
  DEFCONST(RETURN_REG, TYPE_REG|REG_ONLY),

  /* ===== 浮点寄存器 ===== */
  DEFCONST(FR0,    TYPE_REG|REG_ONLY),
  DEFCONST(FR1,    TYPE_REG|REG_ONLY),
  DEFCONST(FR2,    TYPE_REG|REG_ONLY),
  DEFCONST(FR3,    TYPE_REG|REG_ONLY),
  DEFCONST(FR4,    TYPE_REG|REG_ONLY),
  DEFCONST(FR5,    TYPE_REG|REG_ONLY),
  DEFCONST(FR6,    TYPE_REG|REG_ONLY),
  DEFCONST(FR7,    TYPE_REG|REG_ONLY),
  DEFCONST(FR8,    TYPE_REG|REG_ONLY),
  DEFCONST(FR9,    TYPE_REG|REG_ONLY),
  DEFCONST(FS0,    TYPE_REG|REG_ONLY),
  DEFCONST(FS1,    TYPE_REG|REG_ONLY),
  DEFCONST(FS2,    TYPE_REG|REG_ONLY),
  DEFCONST(FS3,    TYPE_REG|REG_ONLY),
  DEFCONST(FS4,    TYPE_REG|REG_ONLY),
  DEFCONST(FS5,    TYPE_REG|REG_ONLY),
  DEFCONST(FS6,    TYPE_REG|REG_ONLY),
  DEFCONST(FS7,    TYPE_REG|REG_ONLY),
  DEFCONST(FS8,    TYPE_REG|REG_ONLY),
  DEFCONST(FS9,    TYPE_REG|REG_ONLY),
  DEFCONST(RETURN_FREG, TYPE_REG|REG_ONLY),

  /* ===== 零操作数指令 (OP0) ===== */
  DEFCONST(BREAKPOINT,     TYPE_OP0),
  DEFCONST(NOP,            TYPE_OP0),
  DEFCONST(LMUL_UW,        TYPE_OP0),
  DEFCONST(LMUL_SW,        TYPE_OP0),
  DEFCONST(DIVMOD_UW,      TYPE_OP0),
  DEFCONST(DIVMOD_SW,      TYPE_OP0),
  DEFCONST(DIV_UW,         TYPE_OP0),
  DEFCONST(DIV_SW,         TYPE_OP0),
  DEFCONST(MEMORY_BARRIER, TYPE_OP0),
  DEFCONST(ENDBR,          TYPE_OP0),

  /* ===== 单操作数指令 (OP1) - 整数传送 ===== */
  DEFCONST(MOV,       TYPE_OP1|OP1_RET),
  DEFCONST(MOV_U8,    TYPE_OP1|OP1_RET),
  DEFCONST(MOV_S8,    TYPE_OP1|OP1_RET),
  DEFCONST(MOV_U16,   TYPE_OP1|OP1_RET),
  DEFCONST(MOV_S16,   TYPE_OP1|OP1_RET),
  DEFCONST(MOV_U32,   TYPE_OP1|OP1_RET),
  DEFCONST(MOV_S32,   TYPE_OP1|OP1_RET),
  DEFCONST(MOV32,     TYPE_OP1),
  DEFCONST(MOV_P,     TYPE_OP1|OP1_RET),

  /* ===== 单操作数指令 (OP1) - 位操作 ===== */
  DEFCONST(CLZ,       TYPE_OP1),
  DEFCONST(CTZ,       TYPE_OP1),
  DEFCONST(REV,       TYPE_OP1),
  DEFCONST(REV_U16,   TYPE_OP1),
  DEFCONST(REV_S16,   TYPE_OP1),
  DEFCONST(REV_U32,   TYPE_OP1),
  DEFCONST(REV_S32,   TYPE_OP1),

  /* ===== 双操作数指令 (OP2) - 算术与逻辑 ===== */
  DEFCONST(ADD,   TYPE_OP2),
  DEFCONST(ADDC,  TYPE_OP2),
  DEFCONST(SUB,   TYPE_OP2),
  DEFCONST(SUBC,  TYPE_OP2),
  DEFCONST(MUL,   TYPE_OP2),
  DEFCONST(AND,   TYPE_OP2),
  DEFCONST(OR,    TYPE_OP2),
  DEFCONST(XOR,   TYPE_OP2),
  DEFCONST(SHL,   TYPE_OP2),
  DEFCONST(MSHL,  TYPE_OP2),
  DEFCONST(LSHR,  TYPE_OP2),
  DEFCONST(MLSHR, TYPE_OP2),
  DEFCONST(ASHR,  TYPE_OP2),
  DEFCONST(MASHR, TYPE_OP2),
  DEFCONST(ROTL,  TYPE_OP2),
  DEFCONST(ROTR,  TYPE_OP2),

  /* ===== OP2R (三操作数：乘加) ===== */
  DEFCONST(MULADD,    TYPE_OP2),
  DEFCONST(MULADD32,  TYPE_OP2),

  /* ===== 比较类型 (CMP) - 整数比较 ===== */
  DEFCONST(EQUAL,               TYPE_CMP|CMP_JMP),
  DEFCONST(ZERO,                TYPE_CMP|CMP_JMP),
  DEFCONST(NOT_EQUAL,           TYPE_CMP|CMP_JMP),
  DEFCONST(NOT_ZERO,            TYPE_CMP|CMP_JMP),
  DEFCONST(LESS,                TYPE_CMP|CMP_JMP),
  DEFCONST(GREATER_EQUAL,       TYPE_CMP|CMP_JMP),
  DEFCONST(GREATER,             TYPE_CMP|CMP_JMP),
  DEFCONST(LESS_EQUAL,          TYPE_CMP|CMP_JMP),
  DEFCONST(SIG_LESS,            TYPE_CMP|CMP_JMP),
  DEFCONST(SIG_GREATER_EQUAL,   TYPE_CMP|CMP_JMP),
  DEFCONST(SIG_GREATER,         TYPE_CMP|CMP_JMP),
  DEFCONST(SIG_LESS_EQUAL,      TYPE_CMP|CMP_JMP),
  DEFCONST(OVERFLOW,            TYPE_CMP),
  DEFCONST(NOT_OVERFLOW,        TYPE_CMP),
  DEFCONST(CARRY,               TYPE_CMP),
  DEFCONST(NOT_CARRY,           TYPE_CMP),

  /* ===== 跳转/调用类型 ===== */
  DEFCONST(JUMP,                TYPE_CMP|CMP_JMP),
  DEFCONST(CALL,                TYPE_CMP|CMP_JMP),
  DEFCONST(FAST_CALL,           TYPE_CMP|CMP_JMP),

  /* ===== 浮点单操作数指令 (FOP1) ===== */
  DEFCONST(MOV_F64,             TYPE_FOP1),
  DEFCONST(MOV_F32,             TYPE_FOP1),
  DEFCONST(CONV_F64_FROM_F32,   TYPE_FOP1),
  DEFCONST(CONV_F32_FROM_F64,   TYPE_FOP1),
  DEFCONST(CONV_SW_FROM_F64,    TYPE_FOP1),
  DEFCONST(CONV_SW_FROM_F32,    TYPE_FOP1),
  DEFCONST(CONV_S32_FROM_F64,   TYPE_FOP1),
  DEFCONST(CONV_S32_FROM_F32,   TYPE_FOP1),
  DEFCONST(CONV_F64_FROM_SW,    TYPE_FOP1),
  DEFCONST(CONV_F32_FROM_SW,    TYPE_FOP1),
  DEFCONST(CONV_F64_FROM_S32,   TYPE_FOP1),
  DEFCONST(CONV_F32_FROM_S32,   TYPE_FOP1),
  DEFCONST(CONV_F64_FROM_UW,    TYPE_FOP1),
  DEFCONST(CONV_F32_FROM_UW,    TYPE_FOP1),
  DEFCONST(CONV_F64_FROM_U32,   TYPE_FOP1),
  DEFCONST(CONV_F32_FROM_U32,   TYPE_FOP1),
  DEFCONST(NEG_F64,             TYPE_FOP1),
  DEFCONST(NEG_F32,             TYPE_FOP1),
  DEFCONST(ABS_F64,             TYPE_FOP1),
  DEFCONST(ABS_F32,             TYPE_FOP1),

  /* ===== 浮点双操作数指令 (FOP2) ===== */
  DEFCONST(ADD_F64,             TYPE_FOP2),
  DEFCONST(ADD_F32,             TYPE_FOP2),
  DEFCONST(SUB_F64,             TYPE_FOP2),
  DEFCONST(SUB_F32,             TYPE_FOP2),
  DEFCONST(MUL_F64,             TYPE_FOP2),
  DEFCONST(MUL_F32,             TYPE_FOP2),
  DEFCONST(DIV_F64,             TYPE_FOP2),
  DEFCONST(DIV_F32,             TYPE_FOP2),

  /* ===== 浮点比较类型 ===== */
  DEFCONST(F_EQUAL,             TYPE_FCMP|CMP_JMP),
  DEFCONST(F_NOT_EQUAL,         TYPE_FCMP|CMP_JMP),
  DEFCONST(F_LESS,              TYPE_FCMP|CMP_JMP),
  DEFCONST(F_GREATER_EQUAL,     TYPE_FCMP|CMP_JMP),
  DEFCONST(F_GREATER,           TYPE_FCMP|CMP_JMP),
  DEFCONST(F_LESS_EQUAL,        TYPE_FCMP|CMP_JMP),
  DEFCONST(UNORDERED,           TYPE_FCMP|CMP_JMP),
  DEFCONST(ORDERED,             TYPE_FCMP|CMP_JMP),
  DEFCONST(ORDERED_EQUAL,       TYPE_FCMP|CMP_JMP),
  DEFCONST(ORDERED_LESS,        TYPE_FCMP|CMP_JMP),
  DEFCONST(ORDERED_GREATER,     TYPE_FCMP|CMP_JMP),
  DEFCONST(UNORDERED_OR_EQUAL,  TYPE_FCMP|CMP_JMP),
  DEFCONST(UNORDERED_OR_LESS,   TYPE_FCMP|CMP_JMP),
  DEFCONST(UNORDERED_OR_GREATER,TYPE_FCMP|CMP_JMP),

  /* ===== 整数寄存器与浮点寄存器拷贝 ===== */
  DEFCONST(COPY_TO_F64,         TYPE_FOP1),
  DEFCONST(COPY32_TO_F32,       TYPE_FOP1),
  DEFCONST(COPY_FROM_F64,       TYPE_OP1),
  DEFCONST(COPY32_FROM_F32,     TYPE_OP1),

  /* ===== 32位变体常量 ===== */
  DEFCONST(DIVMOD_U32,          TYPE_OP0),
  DEFCONST(DIVMOD_S32,          TYPE_OP0),
  DEFCONST(DIV_U32,             TYPE_OP0),
  DEFCONST(DIV_S32,             TYPE_OP0),
  DEFCONST(CLZ32,               TYPE_OP1),
  DEFCONST(CTZ32,               TYPE_OP1),
  DEFCONST(REV32,               TYPE_OP1),
  DEFCONST(REV32_U16,           TYPE_OP1),
  DEFCONST(REV32_S16,           TYPE_OP1),
  DEFCONST(ADD32,               TYPE_OP2),
  DEFCONST(ADDC32,              TYPE_OP2),
  DEFCONST(SUB32,               TYPE_OP2),
  DEFCONST(SUBC32,              TYPE_OP2),
  DEFCONST(MUL32,               TYPE_OP2),
  DEFCONST(AND32,               TYPE_OP2),
  DEFCONST(OR32,                TYPE_OP2),
  DEFCONST(XOR32,               TYPE_OP2),
  DEFCONST(SHL32,               TYPE_OP2),
  DEFCONST(MSHL32,              TYPE_OP2),
  DEFCONST(LSHR32,              TYPE_OP2),
  DEFCONST(MLSHR32,             TYPE_OP2),
  DEFCONST(ASHR32,              TYPE_OP2),
  DEFCONST(MASHR32,             TYPE_OP2),
  DEFCONST(ROTL32,              TYPE_OP2),
  DEFCONST(ROTR32,              TYPE_OP2),
  DEFCONST(MOV32_U8,            TYPE_OP1),
  DEFCONST(MOV32_S8,            TYPE_OP1),
  DEFCONST(MOV32_U16,           TYPE_OP1),
  DEFCONST(MOV32_S16,           TYPE_OP1),

  /* ===== 标志设置常量 (SET flags) ===== */
  DEFCONST(SET_Z,               TYPE_NOTUD),

  /* ===== 内存操作标志 ===== */
  DEFCONST(MEM_UNALIGNED,       TYPE_NOTUD),
  DEFCONST(MEM_ALIGNED_16,      TYPE_NOTUD),
  DEFCONST(MEM_ALIGNED_32,      TYPE_NOTUD),
  DEFCONST(MEM_PRE,             TYPE_NOTUD),
  DEFCONST(MEM_POST,            TYPE_NOTUD),

  /* ===== 标签对齐常量 ===== */
  DEFCONST(LABEL_ALIGN_1,       TYPE_NOTUD),
  DEFCONST(LABEL_ALIGN_2,       TYPE_NOTUD),
  DEFCONST(LABEL_ALIGN_4,       TYPE_NOTUD),
  DEFCONST(LABEL_ALIGN_8,       TYPE_NOTUD),
  DEFCONST(LABEL_ALIGN_16,      TYPE_NOTUD),
  DEFCONST(LABEL_ALIGN_W,       TYPE_NOTUD),
  DEFCONST(LABEL_ALIGN_P,       TYPE_NOTUD),

  /* ===== 调用/入口选项常量 ===== */
  DEFCONST(REWRITABLE_JUMP,     TYPE_NOTUD),
  DEFCONST(CALL_RETURN,         TYPE_NOTUD),
  DEFCONST(CALL_REG_ARG,        TYPE_NOTUD),
  DEFCONST(ENTER_REG_ARG,       TYPE_NOTUD),

  /* ===== 原子操作常量 ===== */
  DEFCONST(ATOMIC_USE_CAS,      TYPE_NOTUD),
  DEFCONST(ATOMIC_USE_LS,       TYPE_NOTUD),
  DEFCONST(ATOMIC_TEST,         TYPE_NOTUD),

  /* ===== 生成代码选项 ===== */
  DEFCONST(GENERATE_CODE_BUFFER,   TYPE_NOTUD),
  DEFCONST(GENERATE_CODE_NO_CONTEXT, TYPE_NOTUD),
};

/* ============================================================
 * 辅助函数：参数转换
 * ============================================================ */

/*
** 将参数 userdata 压入栈
*/
static void pusharg (lua_State *L, const luaSljitArg *arg) {
  luaSljitArg *udata = (luaSljitArg *)lua_newuserdata(L, sizeof(luaSljitArg));
  *udata = *arg;
  luaL_getmetatable(L, ARG_METATABLE);
  lua_setmetatable(L, -2);
}

/*
** 检查并获取参数 userdata
*/
static luaSljitArg *checkarg (lua_State *L, int narg, const char *type,
                              constant_flag_t flags) {
  luaSljitArg *res = (luaSljitArg *)luaL_checkudata(L, narg, ARG_METATABLE);
  if (flags != 0 && (res->flags & flags) == 0)
    luaL_error(L, "invalid %s", type != NULL ? type : ARG_METATABLE);
  return res;
}

/*
** 从字符串解析参数（支持 "ADD" 或 "ADD+SUB" 形式）
*/
static bool parsearg (lua_State *L, const char *str, luaSljitArg *copyto) {
  luaSljitArg tmp;
  const luaSljitArg *ud;
  const char *end;
  const char delim = '+';

  if (str == NULL)
    return false;

  lua_pushlightuserdata(L, (void *)constants);
  lua_rawget(L, LUA_REGISTRYINDEX);

  tmp.flags = 0;
  tmp.argi = 0;
  tmp.argw = 0;

  end = strchr(str, delim);
  while (end != NULL) {
    lua_pushlstring(L, str, (size_t)(end - str));
    lua_rawget(L, -2);

    if (lua_isnil(L, -1)) {
      lua_pop(L, 2);
      return false;
    }

    ud = (const luaSljitArg *)lua_touserdata(L, -1);
    tmp.flags |= ud->flags;
    tmp.argi |= ud->argi;
    lua_pop(L, 1);

    str = end + 1;
    end = strchr(str, delim);
  }

  lua_pushstring(L, str);
  lua_rawget(L, -2);

  if (lua_isnil(L, -1)) {
    lua_pop(L, 2);
    return false;
  }

  ud = (const luaSljitArg *)lua_touserdata(L, -1);
  tmp.flags |= ud->flags;
  tmp.argi |= ud->argi;

  lua_pop(L, 2);

  if (copyto != NULL)
    *copyto = tmp;
  else
    pusharg(L, &tmp);

  return true;
}

/*
** 将 Lua 值转换为参数（userdata / number / string）
*/
static luaSljitArg *toarg (lua_State *L, int narg, const char *type,
                           int flags, luaSljitArg *copyto) {
  luaSljitArg *arg, tmp;

  switch (lua_type(L, narg)) {
    case LUA_TUSERDATA:
      arg = checkarg(L, narg, type, flags);
      break;
    case LUA_TNUMBER:
      if (flags != 0 && (flags & REG_IMM) == 0)
        luaL_error(L, "invalid %s", type != NULL ? type : ARG_METATABLE);
      arg = &tmp;
      tmp.flags = TYPE_REG | REG_IMM;
      tmp.argi = SLJIT_IMM;
      tmp.argw = (sljit_sw)lua_tointeger(L, narg);
      break;
    default: {
      arg = &tmp;
      if (!parsearg(L, lua_tostring(L, narg), arg))
        luaL_argerror(L, narg, ERR_NOCONV(ARG_METATABLE));
      break;
    }
  }

  if (copyto != NULL) {
    *copyto = *arg;
    return copyto;
  }

  return (arg == &tmp) ? NULL : arg;
}

/*
** 检查并提取寄存器参数
*/
static void checkreg (lua_State *L, int narg, int flags,
                      sljit_s32 *regi, sljit_sw *regw) {
  luaSljitArg arg;
  toarg(L, narg, "register", flags, &arg);
  *regi = arg.argi;
  *regw = arg.argw;
}

/*
** 检查浮点寄存器参数
*/
static void checkfreg (lua_State *L, int narg, sljit_s32 *regi, sljit_sw *regw) {
  luaSljitArg arg;
  toarg(L, narg, "float register", REG_ONLY, &arg);
  *regi = arg.argi;
  *regw = arg.argw;
}

/*
** 从表参数中获取整数字段
*/
static lua_Integer getiarg (lua_State *L, int t, const char *k,
                            lua_Integer dflt) {
  lua_pushstring(L, k);
  lua_rawget(L, t);
  if (lua_type(L, -1) == LUA_TNIL)
    return dflt;
  else
    return lua_tointeger(L, -1);
}

/*
** 将 Lua 值转换为 sljit_sw
*/
static sljit_sw tosw (lua_State *L, int narg) {
  int isnum;
  lua_Integer i = lua_tointegerx(L, narg, &isnum);
  if (!isnum)
    luaL_argerror(L, narg, ERR_NOCONV("sljit_sw"));
  return (sljit_sw)i;
}

/*
** 将 Lua 值转换为 sljit_s32
*/
static sljit_s32 tos32 (lua_State *L, int narg) {
  return (sljit_s32)luaL_checkinteger(L, narg);
}

/* ============================================================
 * 辅助函数：对象检查
 * ============================================================ */

static luaSljitCompiler *checkcompiler (lua_State *L, int narg) {
  luaSljitCompiler *comp = (luaSljitCompiler *)
    luaL_checkudata(L, narg, COMP_METATABLE);
  if (comp->compiler == NULL)
    luaL_error(L, COMP_METATABLE " object is dead");
  return comp;
}

static luaSljitJump *checkjump (lua_State *L, int narg) {
  luaSljitJump *jump = (luaSljitJump *)
    luaL_checkudata(L, narg, JUMP_METATABLE);
  if (jump->jump == NULL)
    luaL_error(L, JUMP_METATABLE " object is dead");
  return jump;
}

static luaSljitLabel *checklabel (lua_State *L, int narg) {
  luaSljitLabel *label = (luaSljitLabel *)
    luaL_checkudata(L, narg, LABEL_METATABLE);
  if (label->label == NULL)
    luaL_error(L, LABEL_METATABLE " object is dead");
  return label;
}

static luaSljitConst *checkconst (lua_State *L, int narg) {
  luaSljitConst *c = (luaSljitConst *)
    luaL_checkudata(L, narg, CONST_METATABLE);
  if (c->const_ == NULL)
    luaL_error(L, CONST_METATABLE " object is dead");
  return c;
}

static int compiler_error (lua_State *L, const char *fname, int status) {
  return luaL_error(L, "%s failed with %d", fname, status);
}

/* ============================================================
 * 模块级函数 (sljit.*)
 * ============================================================ */

static int l_word_width (lua_State *L) {
#if (defined SLJIT_32BIT_ARCHITECTURE && SLJIT_32BIT_ARCHITECTURE)
  lua_pushinteger(L, 4);
#elif (defined SLJIT_64BIT_ARCHITECTURE && SLJIT_64BIT_ARCHITECTURE)
  lua_pushinteger(L, 8);
#else
  return luaL_error(L, "sljit is misconfigured");
#endif
  return 1;
}

static int l_is_fpu_available (lua_State *L) {
#if (defined SLJIT_HAS_FPU && SLJIT_HAS_FPU)
  lua_pushboolean(L, 1);
#else
  lua_pushboolean(L, 0);
#endif
  return 1;
}

static int l_unaligned (lua_State *L) {
#if (defined SLJIT_UNALIGNED && SLJIT_UNALIGNED)
  lua_pushboolean(L, 1);
#else
  lua_pushboolean(L, 0);
#endif
  return 1;
}

static int l_create_compiler (lua_State *L) {
  luaSljitCompiler *udata;
  udata = (luaSljitCompiler *)
    lua_newuserdata(L, sizeof(luaSljitCompiler));
  udata->compiler = NULL;
  luaL_getmetatable(L, COMP_METATABLE);
  lua_setmetatable(L, -2);
  udata->compiler = sljit_create_compiler(NULL);
  if (udata->compiler == NULL)
    return luaL_error(L, "sljit.create_compiler() failed");
  return 1;
}

static int l_imm (lua_State *L) {
  luaSljitArg res;
  res.flags = TYPE_REG | REG_IMM;
  res.argi = SLJIT_IMM;
  res.argw = tosw(L, 1);
  pusharg(L, &res);
  return 1;
}

static int l_mem0 (lua_State *L) {
  luaSljitArg res;
  res.flags = TYPE_REG | REG_IMM;
  res.argi = SLJIT_MEM0();
  res.argw = tosw(L, 1);
  pusharg(L, &res);
  return 1;
}

static int l_mem1 (lua_State *L) {
  luaSljitArg arg;
  toarg(L, 1, "register", REG_ONLY, &arg);
  arg.argi = SLJIT_MEM1(arg.argi);
  arg.argw = tosw(L, 2);
  pusharg(L, &arg);
  return 1;
}

static int l_mem2 (lua_State *L) {
  luaSljitArg arg1, arg2;
  toarg(L, 1, "register", REG_ONLY, &arg1);
  toarg(L, 2, "register", REG_ONLY, &arg2);
  arg1.argi = SLJIT_MEM2(arg1.argi, arg2.argi);
  arg1.argw = tosw(L, 3);
  pusharg(L, &arg1);
  return 1;
}

/*
** sljit.has_cpu_feature(feature) - 检测CPU特性
*/
static int l_has_cpu_feature (lua_State *L) {
  sljit_s32 feature = tos32(L, 1);
  lua_pushboolean(L, sljit_has_cpu_feature(feature));
  return 1;
}

/*
** sljit.get_platform_name() - 获取平台名称
*/
static int l_get_platform_name (lua_State *L) {
  lua_pushstring(L, sljit_get_platform_name());
  return 1;
}

/* ============================================================
 * 垃圾回收元方法
 * ============================================================ */

static int gc_compiler (lua_State *L) {
  luaSljitCompiler *udata = (luaSljitCompiler *)
    luaL_checkudata(L, 1, COMP_METATABLE);
  struct sljit_compiler *compiler = udata->compiler;
  udata->compiler = NULL;
  if (compiler != NULL)
    sljit_free_compiler(compiler);
  lua_pushnil(L);
  lua_setmetatable(L, 1);
  return 0;
}

static int gc_code (lua_State *L) {
  luaSljitCode *udata = (luaSljitCode *)
    luaL_checkudata(L, 1, CODE_METATABLE);
  void *code = udata->code;
  udata->code = NULL;
  if (code != NULL)
    sljit_free_code(code, NULL);
  lua_pushnil(L);
  lua_setmetatable(L, 1);
  return 0;
}

/* ============================================================
 * Compiler 方法 - 基础
 * ============================================================ */

static int l_verbose (lua_State *L) {
  luaSljitCompiler *comp = checkcompiler(L, 1);
  FILE *file = NULL;
  if (lua_toboolean(L, 2)) {
    luaL_Stream *stream = (luaL_Stream *)
      luaL_checkudata(L, 2, LUA_FILEHANDLE);
    file = stream->f;
  }
  if (file != NULL)
    lua_pushvalue(L, 2);
  else
    lua_pushnil(L);
#if (defined SLJIT_VERBOSE && SLJIT_VERBOSE)
  sljit_compiler_verbose(comp->compiler, file);
#else
  (void)comp;
#endif
  lua_setuservalue(L, 1);
  return 0;
}

/*
** compiler:emit_enter(...) - 函数入口
*/
static int l_emit_enter (lua_State *L) {
  luaSljitCompiler *comp = checkcompiler(L, 1);
  sljit_s32 options, arg_types, scratches, saveds, local_size;
  int status;

  if (lua_type(L, 2) != LUA_TTABLE) {
    options    = (sljit_s32)luaL_checkinteger(L, 2);
    arg_types  = (sljit_s32)luaL_checkinteger(L, 3);
    scratches  = (sljit_s32)luaL_checkinteger(L, 4);
    saveds     = (sljit_s32)luaL_checkinteger(L, 5);
    local_size = (sljit_s32)luaL_checkinteger(L, 6);
  } else {
    options    = (sljit_s32)getiarg(L, 2, "options", 0);
    arg_types  = (sljit_s32)getiarg(L, 2, "arg_types", 0);
    scratches  = (sljit_s32)getiarg(L, 2, "scratches", 0);
    saveds     = (sljit_s32)getiarg(L, 2, "saveds", 0);
    local_size = (sljit_s32)getiarg(L, 2, "local_size", 0);
  }

  status = sljit_emit_enter(comp->compiler, options, arg_types,
            scratches, saveds, local_size);
  if (status != SLJIT_SUCCESS)
    return compiler_error(L, "sljit_emit_enter", status);

  lua_pushvalue(L, 1);
  return 1;
}

/*
** compiler:set_context(...) - 设置上下文（不生成代码）
*/
static int l_set_context (lua_State *L) {
  luaSljitCompiler *comp = checkcompiler(L, 1);
  sljit_s32 options, arg_types, scratches, saveds, local_size;
  int status;

  if (lua_type(L, 2) != LUA_TTABLE) {
    options    = (sljit_s32)luaL_checkinteger(L, 2);
    arg_types  = (sljit_s32)luaL_checkinteger(L, 3);
    scratches  = (sljit_s32)luaL_checkinteger(L, 4);
    saveds     = (sljit_s32)luaL_checkinteger(L, 5);
    local_size = (sljit_s32)luaL_checkinteger(L, 6);
  } else {
    options    = (sljit_s32)getiarg(L, 2, "options", 0);
    arg_types  = (sljit_s32)getiarg(L, 2, "arg_types", 0);
    scratches  = (sljit_s32)getiarg(L, 2, "scratches", 0);
    saveds     = (sljit_s32)getiarg(L, 2, "saveds", 0);
    local_size = (sljit_s32)getiarg(L, 2, "local_size", 0);
  }

  status = sljit_set_context(comp->compiler, options, arg_types,
            scratches, saveds, local_size);
  if (status != SLJIT_SUCCESS)
    return compiler_error(L, "sljit_set_context", status);

  lua_pushvalue(L, 1);
  return 1;
}

static int l_emit_op0 (lua_State *L) {
  luaSljitCompiler *comp = checkcompiler(L, 1);
  luaSljitArg op;
  int status;
  toarg(L, 2, "opcode", 0, &op);
  status = sljit_emit_op0(comp->compiler, op.argi);
  if (status != SLJIT_SUCCESS)
    return compiler_error(L, "sljit_emit_op0", status);
  lua_pushvalue(L, 1);
  return 1;
}

static int l_emit_op1 (lua_State *L) {
  luaSljitCompiler *comp = checkcompiler(L, 1);
  luaSljitArg op;
  sljit_sw dstw, srcw;
  sljit_s32 dst, src;
  int status;
  toarg(L, 2, "opcode", 0, &op);
  checkreg(L, 3, REG_ONLY, &dst, &dstw);
  checkreg(L, 4, REG_IMM,  &src, &srcw);
  status = sljit_emit_op1(comp->compiler, op.argi, dst, dstw, src, srcw);
  if (status != SLJIT_SUCCESS)
    return compiler_error(L, "sljit_emit_op1", status);
  lua_pushvalue(L, 1);
  return 1;
}

static int l_emit_op2 (lua_State *L) {
  luaSljitCompiler *comp = checkcompiler(L, 1);
  luaSljitArg op;
  sljit_sw dstw, src1w, src2w;
  sljit_s32 dst, src1, src2;
  int status;
  toarg(L, 2, "opcode", 0, &op);
  checkreg(L, 3, REG_ONLY, &dst, &dstw);
  checkreg(L, 4, REG_IMM,  &src1, &src1w);
  checkreg(L, 5, REG_IMM,  &src2, &src2w);
  status = sljit_emit_op2(comp->compiler, op.argi,
            dst, dstw, src1, src1w, src2, src2w);
  if (status != SLJIT_SUCCESS)
    return compiler_error(L, "sljit_emit_op2", status);
  lua_pushvalue(L, 1);
  return 1;
}

/*
** compiler:emit_op2u(op, src1, src2) - 无目的二元操作（只设标志）
*/
static int l_emit_op2u (lua_State *L) {
  luaSljitCompiler *comp = checkcompiler(L, 1);
  luaSljitArg op;
  sljit_sw src1w, src2w;
  sljit_s32 src1, src2;
  int status;
  toarg(L, 2, "opcode", 0, &op);
  checkreg(L, 3, REG_IMM, &src1, &src1w);
  checkreg(L, 4, REG_IMM, &src2, &src2w);
  status = sljit_emit_op2u(comp->compiler, op.argi, src1, src1w, src2, src2w);
  if (status != SLJIT_SUCCESS)
    return compiler_error(L, "sljit_emit_op2u", status);
  lua_pushvalue(L, 1);
  return 1;
}

/*
** compiler:emit_op2r(op, dst, src1, src2) - 三操作数（乘加等）
*/
static int l_emit_op2r (lua_State *L) {
  luaSljitCompiler *comp = checkcompiler(L, 1);
  luaSljitArg op;
  sljit_sw src1w, src2w;
  sljit_s32 dst_reg, src1, src2;
  int status;
  toarg(L, 2, "opcode", 0, &op);
  dst_reg = tos32(L, 3);
  checkreg(L, 4, REG_IMM, &src1, &src1w);
  checkreg(L, 5, REG_IMM, &src2, &src2w);
  status = sljit_emit_op2r(comp->compiler, op.argi,
            dst_reg, src1, src1w, src2, src2w);
  if (status != SLJIT_SUCCESS)
    return compiler_error(L, "sljit_emit_op2r", status);
  lua_pushvalue(L, 1);
  return 1;
}

static int l_emit_return_void (lua_State *L) {
  luaSljitCompiler *comp = checkcompiler(L, 1);
  int status = sljit_emit_return_void(comp->compiler);
  if (status != SLJIT_SUCCESS)
    return compiler_error(L, "sljit_emit_return_void", status);
  lua_pushvalue(L, 1);
  return 1;
}

static int l_emit_return (lua_State *L) {
  luaSljitCompiler *comp;
  luaSljitArg op;
  sljit_sw srcw;
  sljit_s32 src;
  int status;
  printf("[DBG] l_emit_return enter\n");
  comp = checkcompiler(L, 1);
  printf("[DBG] l_emit_return comp=%p\n", comp);
  toarg(L, 2, "opcode", OP1_RET, &op);
  printf("[DBG] l_emit_return op.argi=%d op.flags=0x%x\n", op.argi, op.flags);
  checkreg(L, 3, REG_IMM, &src, &srcw);
  printf("[DBG] l_emit_return src=%d srcw=%ld\n", src, (long)srcw);
  status = sljit_emit_return(comp->compiler, op.argi, src, srcw);
  printf("[DBG] l_emit_return status=%d\n", status);
  if (status != SLJIT_SUCCESS)
    return compiler_error(L, "sljit_emit_return", status);
  lua_pushvalue(L, 1);
  printf("[DBG] l_emit_return exit\n");
  return 1;
}

/*
** compiler:emit_return_to(src) - 跳转到指定地址（恢复寄存器）
*/
static int l_emit_return_to (lua_State *L) {
  luaSljitCompiler *comp = checkcompiler(L, 1);
  sljit_sw srcw;
  sljit_s32 src;
  int status;
  checkreg(L, 2, REG_ONLY, &src, &srcw);
  status = sljit_emit_return_to(comp->compiler, src, srcw);
  if (status != SLJIT_SUCCESS)
    return compiler_error(L, "sljit_emit_return_to", status);
  lua_pushvalue(L, 1);
  return 1;
}

/*
** compiler:emit_fast_enter(dst) - 快速入口
*/
static int l_emit_fast_enter (lua_State *L) {
  luaSljitCompiler *comp = checkcompiler(L, 1);
  sljit_sw dstw;
  sljit_s32 dst;
  int status;
  checkreg(L, 2, REG_ONLY, &dst, &dstw);
  status = sljit_emit_op_dst(comp->compiler, SLJIT_FAST_ENTER, dst, dstw);
  if (status != SLJIT_SUCCESS)
    return compiler_error(L, "sljit_emit_fast_enter", status);
  lua_pushvalue(L, 1);
  return 1;
}

/*
** compiler:emit_fast_return(src) - 快速返回
*/
static int l_emit_fast_return (lua_State *L) {
  luaSljitCompiler *comp = checkcompiler(L, 1);
  sljit_sw srcw;
  sljit_s32 src;
  int status;
  checkreg(L, 2, REG_ONLY, &src, &srcw);
  status = sljit_emit_op_src(comp->compiler, SLJIT_FAST_RETURN, src, srcw);
  if (status != SLJIT_SUCCESS)
    return compiler_error(L, "sljit_emit_fast_return", status);
  lua_pushvalue(L, 1);
  return 1;
}

/*
** compiler:emit_get_return_address(dst) - 获取返回地址
*/
static int l_emit_get_return_address (lua_State *L) {
  luaSljitCompiler *comp = checkcompiler(L, 1);
  sljit_sw dstw;
  sljit_s32 dst;
  int status;
  checkreg(L, 2, REG_ONLY, &dst, &dstw);
  status = sljit_emit_op_dst(comp->compiler, SLJIT_GET_RETURN_ADDRESS, dst, dstw);
  if (status != SLJIT_SUCCESS)
    return compiler_error(L, "sljit_emit_get_return_address", status);
  lua_pushvalue(L, 1);
  return 1;
}

static int l_get_local_base (lua_State *L) {
  luaSljitCompiler *comp = checkcompiler(L, 1);
  sljit_sw dstw, offset;
  sljit_s32 dst;
  int status;
  checkreg(L, 2, REG_ONLY, &dst, &dstw);
  offset = tosw(L, 3);
  status = sljit_get_local_base(comp->compiler, dst, dstw, offset);
  if (status != SLJIT_SUCCESS)
    return compiler_error(L, "sljit_get_local_base", status);
  return 0;
}

static int l_get_compiler_error (lua_State *L) {
  luaSljitCompiler *comp = checkcompiler(L, 1);
  lua_pushinteger(L, sljit_get_compiler_error(comp->compiler));
  return 1;
}

static int l_get_generated_code_size (lua_State *L) {
  luaSljitCompiler *comp = checkcompiler(L, 1);
  sljit_uw sz = sljit_get_generated_code_size(comp->compiler);
  lua_pushinteger(L, (lua_Integer)sz);
  return 1;
}

/* ============================================================
 * Compiler 方法 - 跳转与标签
 * ============================================================ */

static int l_emit_jump (lua_State *L) {
  luaSljitCompiler *comp = checkcompiler(L, 1);
  luaSljitArg type;
  luaSljitJump *udata;

  toarg(L, 2, "jump", 0, &type);
  udata = (luaSljitJump *)lua_newuserdata(L, sizeof(luaSljitJump));
  udata->jump = NULL;
  luaL_getmetatable(L, JUMP_METATABLE);
  lua_setmetatable(L, -2);

  lua_createtable(L, 1, 0);
  lua_pushvalue(L, 1);
  lua_rawseti(L, -2, COMPILER_UVAL_INDEX);
  lua_setuservalue(L, -2);

  udata->jump = sljit_emit_jump(comp->compiler, type.argi);
  if (udata->jump == NULL)
    return luaL_error(L, "sljit.emit_jump() failed");
  return 1;
}

static int l_emit_cmp (lua_State *L) {
  luaSljitCompiler *comp = checkcompiler(L, 1);
  luaSljitJump *udata;
  luaSljitArg type;
  sljit_sw src1w, src2w;
  sljit_s32 src1, src2;

  toarg(L, 2, "comparison", CMP_JMP, &type);
  checkreg(L, 3, REG_IMM, &src1, &src1w);
  checkreg(L, 4, REG_IMM, &src2, &src2w);

  udata = (luaSljitJump *)lua_newuserdata(L, sizeof(luaSljitJump));
  udata->jump = NULL;
  luaL_getmetatable(L, JUMP_METATABLE);
  lua_setmetatable(L, -2);

  lua_createtable(L, 1, 0);
  lua_pushvalue(L, 1);
  lua_rawseti(L, -2, COMPILER_UVAL_INDEX);
  lua_setuservalue(L, -2);

  udata->jump = sljit_emit_cmp(comp->compiler,
    type.argi, src1, src1w, src2, src2w);
  if (udata->jump == NULL)
    return luaL_error(L, "sljit.emit_cmp() failed");
  return 1;
}

/*
** compiler:emit_ijump(type, src) - 间接跳转
*/
static int l_emit_ijump (lua_State *L) {
  luaSljitCompiler *comp = checkcompiler(L, 1);
  sljit_s32 type = tos32(L, 2);
  sljit_sw srcw;
  sljit_s32 src;
  int status;
  checkreg(L, 3, REG_IMM, &src, &srcw);
  status = sljit_emit_ijump(comp->compiler, type, src, srcw);
  if (status != SLJIT_SUCCESS)
    return compiler_error(L, "sljit_emit_ijump", status);
  lua_pushvalue(L, 1);
  return 1;
}

/*
** compiler:emit_call(type, arg_types) - 直接函数调用
** 返回 jump userdata，可用于重定位调用目标
*/
static int l_emit_call (lua_State *L) {
  luaSljitCompiler *comp = checkcompiler(L, 1);
  sljit_s32 type = tos32(L, 2);
  sljit_s32 arg_types = tos32(L, 3);
  luaSljitJump *udata;

  udata = (luaSljitJump *)lua_newuserdata(L, sizeof(luaSljitJump));
  udata->jump = NULL;
  luaL_getmetatable(L, JUMP_METATABLE);
  lua_setmetatable(L, -2);

  lua_createtable(L, 1, 0);
  lua_pushvalue(L, 1);
  lua_rawseti(L, -2, COMPILER_UVAL_INDEX);
  lua_setuservalue(L, -2);

  udata->jump = sljit_emit_call(comp->compiler, type, arg_types);
  if (udata->jump == NULL)
    return luaL_error(L, "sljit.emit_call() failed");
  return 1;
}

/*
** compiler:emit_icall(type, arg_types, src) - 间接函数调用
*/
static int l_emit_icall (lua_State *L) {
  luaSljitCompiler *comp = checkcompiler(L, 1);
  sljit_s32 type = tos32(L, 2);
  sljit_s32 arg_types = tos32(L, 3);
  sljit_sw srcw;
  sljit_s32 src;
  int status;
  checkreg(L, 4, REG_IMM, &src, &srcw);
  status = sljit_emit_icall(comp->compiler, type, arg_types, src, srcw);
  if (status != SLJIT_SUCCESS)
    return compiler_error(L, "sljit_emit_icall", status);
  lua_pushvalue(L, 1);
  return 1;
}

/*
** compiler:emit_label() - 定义标签
*/
static int l_emit_label (lua_State *L) {
  luaSljitCompiler *comp = checkcompiler(L, 1);
  luaSljitLabel *udata;

  udata = (luaSljitLabel *)lua_newuserdata(L, sizeof(luaSljitLabel));
  udata->label = NULL;
  luaL_getmetatable(L, LABEL_METATABLE);
  lua_setmetatable(L, -2);

  lua_createtable(L, 1, 0);
  lua_pushvalue(L, 1);
  lua_rawseti(L, -2, COMPILER_UVAL_INDEX);
  lua_setuservalue(L, -2);

  udata->label = sljit_emit_label(comp->compiler);
  if (udata->label == NULL)
    return luaL_error(L, "sljit.emit_label() failed");
  return 1;
}

/*
** compiler:emit_aligned_label(alignment) - 发射对齐标签
*/
static int l_emit_aligned_label (lua_State *L) {
  luaSljitCompiler *comp = checkcompiler(L, 1);
  sljit_s32 alignment = tos32(L, 2);
  luaSljitLabel *udata;

  udata = (luaSljitLabel *)lua_newuserdata(L, sizeof(luaSljitLabel));
  udata->label = NULL;
  luaL_getmetatable(L, LABEL_METATABLE);
  lua_setmetatable(L, -2);

  lua_createtable(L, 1, 0);
  lua_pushvalue(L, 1);
  lua_rawseti(L, -2, COMPILER_UVAL_INDEX);
  lua_setuservalue(L, -2);

  udata->label = sljit_emit_aligned_label(comp->compiler, alignment, NULL);
  if (udata->label == NULL)
    return luaL_error(L, "sljit.emit_aligned_label() failed");
  return 1;
}

/*
** compiler:emit_const(dst, initval) - 定义可重定位常量
*/
static int l_emit_const (lua_State *L) {
  luaSljitCompiler *comp = checkcompiler(L, 1);
  luaSljitConst *udata;
  sljit_sw initval, dstw;
  sljit_s32 dst;

  checkreg(L, 2, REG_ONLY, &dst, &dstw);
  initval = tosw(L, 3);

  udata = (luaSljitConst *)lua_newuserdata(L, sizeof(luaSljitConst));
  udata->const_ = NULL;
  luaL_getmetatable(L, CONST_METATABLE);
  lua_setmetatable(L, -2);

  lua_createtable(L, 1, 0);
  lua_pushvalue(L, 1);
  lua_rawseti(L, -2, COMPILER_UVAL_INDEX);
  lua_setuservalue(L, -2);

  udata->const_ = sljit_emit_const(comp->compiler, SLJIT_MOV, dst, dstw, initval);
  if (udata->const_ == NULL)
    return luaL_error(L, "sljit.emit_const() failed");
  return 1;
}

/*
** compiler:emit_select(type, dst, src1, src2) - 条件选择 (cmov)
*/
static int l_emit_select (lua_State *L) {
  luaSljitCompiler *comp = checkcompiler(L, 1);
  sljit_s32 type = tos32(L, 2);
  sljit_sw dstw, src2w;
  sljit_s32 dst, src2;
  int status;
  checkreg(L, 3, REG_ONLY, &dst, &dstw);
  checkreg(L, 4, REG_IMM, &src2, &src2w);
  status = sljit_emit_select(comp->compiler, type, dst, dstw, src2, src2w);
  if (status != SLJIT_SUCCESS)
    return compiler_error(L, "sljit_emit_select", status);
  lua_pushvalue(L, 1);
  return 1;
}

/*
** compiler:generate_code(options) - 生成机器码
*/
static int l_generate_code (lua_State *L) {
  luaSljitCompiler *comp = checkcompiler(L, 1);
  luaSljitCode *udata;
  sljit_s32 options = (sljit_s32)luaL_optinteger(L, 2, 0);

  udata = (luaSljitCode *)lua_newuserdata(L, sizeof(luaSljitCode));
  udata->code = NULL;
  luaL_getmetatable(L, CODE_METATABLE);
  lua_setmetatable(L, -2);

  udata->code = sljit_generate_code(comp->compiler, options, NULL);
  if (udata->code == NULL)
    return luaL_error(L, "sljit.generate_code() failed");
  return 1;
}

/* ============================================================
 * Compiler 方法 - 浮点操作
 * ============================================================ */

/*
** compiler:emit_fop1(op, dst, src) - 单操作数浮点指令
*/
static int l_emit_fop1 (lua_State *L) {
  luaSljitCompiler *comp = checkcompiler(L, 1);
  luaSljitArg op;
  sljit_sw dstw, srcw;
  sljit_s32 dst, src;
  int status;
  toarg(L, 2, "fopcode", 0, &op);
  checkfreg(L, 3, &dst, &dstw);
  checkfreg(L, 4, &src, &srcw);
  status = sljit_emit_fop1(comp->compiler, op.argi, dst, dstw, src, srcw);
  if (status != SLJIT_SUCCESS)
    return compiler_error(L, "sljit_emit_fop1", status);
  lua_pushvalue(L, 1);
  return 1;
}

/*
** compiler:emit_fop2(op, dst, src1, src2) - 双操作数浮点指令
*/
static int l_emit_fop2 (lua_State *L) {
  luaSljitCompiler *comp = checkcompiler(L, 1);
  luaSljitArg op;
  sljit_sw dstw, src1w, src2w;
  sljit_s32 dst, src1, src2;
  int status;
  toarg(L, 2, "fopcode", 0, &op);
  checkfreg(L, 3, &dst, &dstw);
  checkfreg(L, 4, &src1, &src1w);
  checkfreg(L, 5, &src2, &src2w);
  status = sljit_emit_fop2(comp->compiler, op.argi,
            dst, dstw, src1, src1w, src2, src2w);
  if (status != SLJIT_SUCCESS)
    return compiler_error(L, "sljit_emit_fop2", status);
  lua_pushvalue(L, 1);
  return 1;
}

/*
** compiler:emit_fcmp(type, src1, src2) - 浮点比较并跳转
*/
static int l_emit_fcmp (lua_State *L) {
  luaSljitCompiler *comp = checkcompiler(L, 1);
  luaSljitJump *udata;
  sljit_s32 type = tos32(L, 2);
  sljit_sw src1w, src2w;
  sljit_s32 src1, src2;

  checkfreg(L, 3, &src1, &src1w);
  checkfreg(L, 4, &src2, &src2w);

  udata = (luaSljitJump *)lua_newuserdata(L, sizeof(luaSljitJump));
  udata->jump = NULL;
  luaL_getmetatable(L, JUMP_METATABLE);
  lua_setmetatable(L, -2);

  lua_createtable(L, 1, 0);
  lua_pushvalue(L, 1);
  lua_rawseti(L, -2, COMPILER_UVAL_INDEX);
  lua_setuservalue(L, -2);

  udata->jump = sljit_emit_fcmp(comp->compiler,
    type, src1, src1w, src2, src2w);
  if (udata->jump == NULL)
    return luaL_error(L, "sljit.emit_fcmp() failed");
  return 1;
}

/*
** compiler:emit_fselect(type, dst, src2) - 浮点条件选择
*/
static int l_emit_fselect (lua_State *L) {
  luaSljitCompiler *comp = checkcompiler(L, 1);
  sljit_s32 type = tos32(L, 2);
  sljit_sw dstw, src2w;
  sljit_s32 dst, src2;
  int status;
  checkfreg(L, 3, &dst, &dstw);
  checkfreg(L, 4, &src2, &src2w);
  status = sljit_emit_fselect(comp->compiler, type, dst, dstw, src2, src2w);
  if (status != SLJIT_SUCCESS)
    return compiler_error(L, "sljit_emit_fselect", status);
  lua_pushvalue(L, 1);
  return 1;
}

/*
** compiler:emit_fset64(dst, value) - 设置64位浮点立即数
*/
static int l_emit_fset64 (lua_State *L) {
  luaSljitCompiler *comp = checkcompiler(L, 1);
  sljit_s32 dst;
  sljit_sw dstw;
  double value = luaL_checknumber(L, 3);
  int status;
  checkfreg(L, 2, &dst, &dstw);
  status = sljit_emit_fset64(comp->compiler, dst, value);
  if (status != SLJIT_SUCCESS)
    return compiler_error(L, "sljit_emit_fset64", status);
  lua_pushvalue(L, 1);
  return 1;
}

/*
** compiler:emit_fset32(dst, value) - 设置32位浮点立即数
*/
static int l_emit_fset32 (lua_State *L) {
  luaSljitCompiler *comp = checkcompiler(L, 1);
  sljit_s32 dst;
  sljit_sw dstw;
  float value = (float)luaL_checknumber(L, 3);
  int status;
  checkfreg(L, 2, &dst, &dstw);
  status = sljit_emit_fset32(comp->compiler, dst, value);
  if (status != SLJIT_SUCCESS)
    return compiler_error(L, "sljit_emit_fset32", status);
  lua_pushvalue(L, 1);
  return 1;
}

/*
** compiler:emit_fcopy(op, freg, reg) - 整数与浮点寄存器间拷贝
** 参数：op 操作码，freg 浮点寄存器，reg 整数寄存器
*/
static int l_emit_fcopy (lua_State *L) {
  luaSljitCompiler *comp = checkcompiler(L, 1);
  sljit_s32 op = tos32(L, 2);
  sljit_s32 freg;
  sljit_s32 reg;
  sljit_sw fregw, regw;
  int status;
  checkfreg(L, 3, &freg, &fregw);
  checkreg(L, 4, REG_ONLY, &reg, &regw);
  status = sljit_emit_fcopy(comp->compiler, op, freg, reg);
  if (status != SLJIT_SUCCESS)
    return compiler_error(L, "sljit_emit_fcopy", status);
  lua_pushvalue(L, 1);
  return 1;
}

/* ============================================================
 * Compiler 方法 - 内存操作
 * ============================================================ */

/*
** compiler:emit_mem(type, reg, mem) - 通用内存操作
** type: 操作类型（MOV_U8, MOV_S32 等，可或上 MEM_* 标志）
** reg: 寄存器操作数
** mem: 内存基址寄存器
** memw: 内存偏移量（通过 mem 的立即数偏移传入）
*/
static int l_emit_mem (lua_State *L) {
  luaSljitCompiler *comp = checkcompiler(L, 1);
  sljit_s32 type = tos32(L, 2);
  sljit_s32 reg;
  sljit_s32 mem;
  sljit_sw regw, memw;
  int status;
  checkreg(L, 3, REG_ONLY, &reg, &regw);
  checkreg(L, 4, REG_IMM, &mem, &memw);
  status = sljit_emit_mem(comp->compiler, type, reg, mem, memw);
  if (status != SLJIT_SUCCESS)
    return compiler_error(L, "sljit_emit_mem", status);
  lua_pushvalue(L, 1);
  return 1;
}

/*
** compiler:emit_fmem(type, freg, mem) - 浮点内存操作
** type: 操作类型（MOV_F64, MOV_F32 等，可或上 MEM_* 标志）
** freg: 浮点寄存器操作数
** mem: 内存基址寄存器
** memw: 内存偏移量（通过 mem 的立即数偏移传入）
*/
static int l_emit_fmem (lua_State *L) {
  luaSljitCompiler *comp = checkcompiler(L, 1);
  sljit_s32 type = tos32(L, 2);
  sljit_s32 freg;
  sljit_s32 mem;
  sljit_sw fregw, memw;
  int status;
  checkfreg(L, 3, &freg, &fregw);
  checkreg(L, 4, REG_IMM, &mem, &memw);
  status = sljit_emit_fmem(comp->compiler, type, freg, mem, memw);
  if (status != SLJIT_SUCCESS)
    return compiler_error(L, "sljit_emit_fmem", status);
  lua_pushvalue(L, 1);
  return 1;
}

/*
** compiler:emit_atomic_load(op, dst_reg, mem_reg) - 原子加载
** op: 操作类型（MOV, MOV_U8 等）
** dst_reg: 目标寄存器（加载的值存入此寄存器）
** mem_reg: 内存基址寄存器
*/
static int l_emit_atomic_load (lua_State *L) {
  luaSljitCompiler *comp = checkcompiler(L, 1);
  sljit_s32 op = tos32(L, 2);
  sljit_s32 dst_reg;
  sljit_s32 mem_reg;
  sljit_sw dstw, memw;
  int status;
  checkreg(L, 3, REG_ONLY, &dst_reg, &dstw);
  checkreg(L, 4, REG_ONLY, &mem_reg, &memw);
  status = sljit_emit_atomic_load(comp->compiler, op, dst_reg, mem_reg);
  if (status != SLJIT_SUCCESS)
    return compiler_error(L, "sljit_emit_atomic_load", status);
  lua_pushvalue(L, 1);
  return 1;
}

/*
** compiler:emit_atomic_store(op, src_reg, mem_reg, temp_reg) - 原子存储
** op: 操作类型（MOV, MOV_U8 等）
** src_reg: 源寄存器（要存储的值）
** mem_reg: 内存基址寄存器
** temp_reg: 临时寄存器（需先用 atomic_load 初始化）
*/
static int l_emit_atomic_store (lua_State *L) {
  luaSljitCompiler *comp = checkcompiler(L, 1);
  sljit_s32 op = tos32(L, 2);
  sljit_s32 src_reg;
  sljit_s32 mem_reg;
  sljit_s32 temp_reg;
  sljit_sw srcw, memw, tempw;
  int status;
  checkreg(L, 3, REG_ONLY, &src_reg, &srcw);
  checkreg(L, 4, REG_ONLY, &mem_reg, &memw);
  checkreg(L, 5, REG_ONLY, &temp_reg, &tempw);
  status = sljit_emit_atomic_store(comp->compiler, op, src_reg, mem_reg, temp_reg);
  if (status != SLJIT_SUCCESS)
    return compiler_error(L, "sljit_emit_atomic_store", status);
  lua_pushvalue(L, 1);
  return 1;
}

/* ============================================================
 * Jump 方法
 * ============================================================ */

/*
** jump:set_label(label) - 绑定到标签
*/
static int l_set_label (lua_State *L) {
  luaSljitJump *jump = checkjump(L, 1);
  luaSljitLabel *label = checklabel(L, 2);
  sljit_set_label(jump->jump, label->label);
  lua_getuservalue(L, 1);
  lua_rawgeti(L, -1, COMPILER_UVAL_INDEX);
  return 1;
}

/*
** jump:set_target(target_addr) - 设置跳转目标地址
*/
static int l_set_target (lua_State *L) {
  luaSljitJump *jump = checkjump(L, 1);
  sljit_uw target = (sljit_uw)luaL_checkinteger(L, 2);
  sljit_set_target(jump->jump, target);
  return 0;
}

/*
** jump:get_addr() - 获取跳转指令地址
*/
static int l_jump_get_addr (lua_State *L) {
  luaSljitJump *jump = checkjump(L, 1);
  sljit_uw addr = sljit_get_jump_addr(jump->jump);
  lua_pushinteger(L, (lua_Integer)addr);
  return 1;
}

/* ============================================================
 * Label 方法
 * ============================================================ */

/*
** label:get_addr() - 获取标签地址
*/
static int l_label_get_addr (lua_State *L) {
  luaSljitLabel *label = checklabel(L, 1);
  sljit_uw addr = sljit_get_label_addr(label->label);
  lua_pushinteger(L, (lua_Integer)addr);
  return 1;
}

/*
** label:get_abs_addr() - 获取标签绝对地址
*/
static int l_label_get_abs_addr (lua_State *L) {
  luaSljitLabel *label = checklabel(L, 1);
  sljit_uw addr = sljit_get_label_abs_addr(label->label);
  lua_pushinteger(L, (lua_Integer)addr);
  return 1;
}

/* ============================================================
 * Const 方法
 * ============================================================ */

/*
** const:set(new_value) - 运行时修改常量值
** 通过 sljit_get_const_addr 获取地址，再调用 sljit_set_const 设置新值
*/
static int l_const_set (lua_State *L) {
  luaSljitConst *c = checkconst(L, 1);
  sljit_sw new_value = tosw(L, 2);
  sljit_uw addr;
  sljit_sw executable_offset;
  luaSljitCompiler *comp;

  /* 从 uservalue 表中获取 compiler 引用 */
  lua_getuservalue(L, 1);
  lua_rawgeti(L, -1, COMPILER_UVAL_INDEX);
  comp = checkcompiler(L, -1);
  lua_pop(L, 2);

  addr = sljit_get_const_addr(c->const_);
  executable_offset = sljit_get_executable_offset(comp->compiler);
  /* op 必须与 emit_const 时传入的一致，这里始终使用 SLJIT_MOV */
  sljit_set_const(addr, SLJIT_MOV, new_value, executable_offset);
  return 0;
}

/*
** const:get_addr() - 获取常量指令地址
*/
static int l_const_get_addr (lua_State *L) {
  luaSljitConst *c = checkconst(L, 1);
  sljit_uw addr = sljit_get_const_addr(c->const_);
  lua_pushinteger(L, (lua_Integer)addr);
  return 1;
}

/* ============================================================
 * Code 方法
 * ============================================================ */

/*
** code:get_ptr() - 获取代码指针（lightuserdata）
*/
static int l_code_get_ptr (lua_State *L) {
  luaSljitCode *udata = (luaSljitCode *)
    luaL_checkudata(L, 1, CODE_METATABLE);
  lua_pushlightuserdata(L, udata->code);
  return 1;
}

/*
** code:call(...) - 调用生成的机器码（支持0-6个整数参数，返回整数结果）
*/
static int l_code_call (lua_State *L) {
  luaSljitCode *udata = (luaSljitCode *)
    luaL_checkudata(L, 1, CODE_METATABLE);
  void *code = udata->code;
  int nargs = lua_gettop(L) - 1;
  sljit_sw args[6];
  sljit_sw res = 0;
  int i;

  if (nargs > 6) nargs = 6;

  for (i = 0; i < nargs; i++) {
    switch (lua_type(L, i + 2)) {
      case LUA_TSTRING:
        args[i] = (sljit_sw)lua_tostring(L, i + 2);
        break;
      case LUA_TBOOLEAN:
        args[i] = (sljit_sw)lua_toboolean(L, i + 2);
        break;
      case LUA_TLIGHTUSERDATA:
      case LUA_TTHREAD:
      case LUA_TUSERDATA:
        args[i] = (sljit_sw)lua_topointer(L, i + 2);
        break;
      case LUA_TNUMBER:
      default:
        args[i] = (sljit_sw)lua_tointeger(L, i + 2);
        break;
    }
  }

  switch (nargs) {
    case 0: {
      sljit_sw (*f)(void) = code;
      res = f();
      break;
    }
    case 1: {
      sljit_sw (*f)(sljit_sw) = code;
      res = f(args[0]);
      break;
    }
    case 2: {
      sljit_sw (*f)(sljit_sw, sljit_sw) = code;
      res = f(args[0], args[1]);
      break;
    }
    case 3: {
      sljit_sw (*f)(sljit_sw, sljit_sw, sljit_sw) = code;
      res = f(args[0], args[1], args[2]);
      break;
    }
    case 4: {
      sljit_sw (*f)(sljit_sw, sljit_sw, sljit_sw, sljit_sw) = code;
      res = f(args[0], args[1], args[2], args[3]);
      break;
    }
    case 5: {
      sljit_sw (*f)(sljit_sw, sljit_sw, sljit_sw, sljit_sw, sljit_sw) = code;
      res = f(args[0], args[1], args[2], args[3], args[4]);
      break;
    }
    case 6: {
      sljit_sw (*f)(sljit_sw, sljit_sw, sljit_sw, sljit_sw, sljit_sw, sljit_sw) = code;
      res = f(args[0], args[1], args[2], args[3], args[4], args[5]);
      break;
    }
  }

  lua_pushinteger(L, (lua_Integer)res);
  return 1;
}

/* ============================================================
 * 参数元方法
 * ============================================================ */

static bool binop_option_arguments (lua_State *L, luaSljitArg **first,
                                    luaSljitArg **second, luaSljitArg *tmp) {
  luaSljitArg *args[2] = { NULL, NULL };
  int i;
  for (i = 1; i <= 2; i++) {
    switch (lua_type(L, i)) {
      case LUA_TUSERDATA:
        args[i - 1] = checkarg(L, i, NULL, 0);
        break;
      default:
        if (!parsearg(L, lua_tostring(L, i), tmp))
          luaL_argerror(L, i, ERR_NOCONV(ARG_METATABLE));
        break;
    }
  }
  if (args[0] == NULL && args[1] == NULL)
    return false;
  *first = args[0];
  *second = args[1];
  return true;
}

static int l_arg_add (lua_State *L) {
  luaSljitArg tmp, res, *first, *second;
  if (!binop_option_arguments(L, &first, &second, &tmp))
    return luaL_error(L, "no userdata");
  res = *first;
  res.flags |= second->flags;
  res.argi |= second->argi;
  res.argw = 0;
  pusharg(L, &res);
  return 1;
}

static int l_arg_sub (lua_State *L) {
  luaSljitArg tmp, res, *first, *second;
  if (!binop_option_arguments(L, &first, &second, &tmp))
    return luaL_error(L, "no userdata");
  res = *first;
  res.flags &= ~second->flags;
  res.argi &= ~second->argi;
  res.argw = 0;
  pusharg(L, &res);
  return 1;
}

static int l_arg_tostr (lua_State *L) {
  luaSljitArg *arg = checkarg(L, 1, NULL, 0);
  lua_pushfstring(L, "<sljit.argument: %d>", (int)arg->argi);
  return 1;
}

/* ============================================================
 * 方法表定义
 * ============================================================ */

static const luaL_Reg comp_metafunctions[] = {
  { "__gc", gc_compiler },
  { NULL, NULL }
};

static const luaL_Reg code_metafunctions[] = {
  { "__gc", gc_code },
  { NULL, NULL }
};

static const luaL_Reg comp_methods[] = {
  /* 基础 */
  { "emit_enter",              l_emit_enter              },
  { "set_context",             l_set_context             },
  { "emit_return",             l_emit_return             },
  { "emit_return_void",        l_emit_return_void        },
  { "emit_return_to",          l_emit_return_to          },
  { "emit_fast_enter",         l_emit_fast_enter         },
  { "emit_fast_return",        l_emit_fast_return        },
  { "emit_get_return_address", l_emit_get_return_address },
  { "get_local_base",          l_get_local_base          },
  { "get_compiler_error",      l_get_compiler_error      },
  { "get_generated_code_size", l_get_generated_code_size },
  { "verbose",                 l_verbose                 },
  /* 整数运算 */
  { "emit_op0",                l_emit_op0                },
  { "emit_op1",                l_emit_op1                },
  { "emit_op2",                l_emit_op2                },
  { "emit_op2u",               l_emit_op2u               },
  { "emit_op2r",               l_emit_op2r               },
  { "emit_select",             l_emit_select             },
  /* 跳转/标签 */
  { "emit_jump",               l_emit_jump               },
  { "emit_cmp",                l_emit_cmp                },
  { "emit_ijump",              l_emit_ijump              },
  { "emit_label",              l_emit_label              },
  { "emit_aligned_label",      l_emit_aligned_label      },
  { "emit_const",              l_emit_const              },
  { "generate_code",           l_generate_code           },
  /* 函数调用 */
  { "emit_call",               l_emit_call               },
  { "emit_icall",              l_emit_icall              },
  /* 浮点操作 */
  { "emit_fop1",               l_emit_fop1               },
  { "emit_fop2",               l_emit_fop2               },
  { "emit_fcmp",               l_emit_fcmp               },
  { "emit_fselect",            l_emit_fselect            },
  { "emit_fset64",             l_emit_fset64             },
  { "emit_fset32",             l_emit_fset32             },
  { "emit_fcopy",              l_emit_fcopy              },
  /* 内存操作 */
  { "emit_mem",                l_emit_mem                },
  { "emit_fmem",               l_emit_fmem               },
  { "emit_atomic_load",        l_emit_atomic_load        },
  { "emit_atomic_store",       l_emit_atomic_store       },
  { NULL, NULL }
};

static const luaL_Reg arg_metafunctions[] = {
  { "__add",      l_arg_add   },
  { "__sub",      l_arg_sub   },
  { "__tostring", l_arg_tostr },
  { NULL, NULL }
};

static const luaL_Reg code_methods[] = {
  { "get_ptr",  l_code_get_ptr  },
  { "call",     l_code_call     },
  { NULL, NULL }
};

static const luaL_Reg const_methods[] = {
  { "set",       l_const_set       },
  { "get_addr",  l_const_get_addr  },
  { NULL, NULL }
};

static const luaL_Reg jump_methods[] = {
  { "set_label",  l_set_label      },
  { "set_target", l_set_target     },
  { "get_addr",   l_jump_get_addr  },
  { NULL, NULL }
};

static const luaL_Reg label_methods[] = {
  { "get_addr",     l_label_get_addr     },
  { "get_abs_addr", l_label_get_abs_addr },
  { NULL, NULL }
};

static const luaL_Reg sljit_functions[] = {
  { "word_width",         l_word_width         },
  { "create_compiler",    l_create_compiler    },
  { "is_fpu_available",   l_is_fpu_available   },
  { "imm",                l_imm                },
  { "mem0",               l_mem0               },
  { "mem1",               l_mem1               },
  { "mem2",               l_mem2               },
  { "unaligned",          l_unaligned          },
  { "has_cpu_feature",    l_has_cpu_feature    },
  { "get_platform_name",  l_get_platform_name  },
  { NULL, NULL }
};

/* ============================================================
 * 注册辅助函数
 * ============================================================ */

static void register_udata (lua_State *L, int arg, const char *tname,
                            const luaL_Reg *metafunctions,
                            const luaL_Reg *methods) {
  if (arg != -1)
    lua_pushvalue(L, arg);

  if (methods != NULL)
    luaL_setfuncs(L, methods, 0);

  luaL_newmetatable(L, tname);

  if (metafunctions != NULL)
    luaL_setfuncs(L, metafunctions, 0);

  if (methods != NULL) {
    lua_pushstring(L, "__index");
    lua_newtable(L);
    luaL_setfuncs(L, methods, 0);
    lua_rawset(L, -3);
  }

  lua_pop(L, (arg != -1) ? 2 : 1);
}

static void register_constants (lua_State *L, int arg) {
  size_t i, nconstants = sizeof(constants) / sizeof(constants[0]);
  int j, ntables = 3;

  if (arg != -1)
    lua_pushvalue(L, arg);

  lua_pushstring(L, "C");
  lua_createtable(L, 0, (int)nconstants);

  lua_pushlightuserdata(L, (void *)constants);
  lua_createtable(L, 0, (int)nconstants);

  for (i = 0; i < nconstants; i++) {
    lua_pushstring(L, constants[i].name);

    if ((constants[i].arg.flags & TYPE_MASK) == TYPE_NOTUD) {
      lua_pushinteger(L, (lua_Integer)constants[i].arg.argi);
    } else {
      pusharg(L, &constants[i].arg);
    }

    for (j = 0; j < 2 * (ntables - 1); j++)
      lua_pushvalue(L, -2);

    for (j = 0; j < ntables; j++)
      lua_rawset(L, -1 - 2 * ntables);
  }

  lua_rawset(L, LUA_REGISTRYINDEX);
  lua_rawset(L, -3);

  if (arg != -1)
    lua_pop(L, 1);
}

/* ============================================================
 * 公开 C API
 * ============================================================ */

struct sljit_compiler *luaSljit_tocompiler (lua_State *L, int narg) {
  luaSljitCompiler *ud = (luaSljitCompiler *)
    luaL_testudata(L, narg, COMP_METATABLE);
  return (ud != NULL) ? ud->compiler : NULL;
}

void *luaSljit_tocode (lua_State *L, int narg) {
  luaSljitCode *ud = (luaSljitCode *)
    luaL_testudata(L, narg, CODE_METATABLE);
  return (ud != NULL) ? ud->code : NULL;
}

struct sljit_const *luaSljit_toconst (lua_State *L, int narg) {
  luaSljitConst *ud = (luaSljitConst *)
    luaL_testudata(L, narg, CONST_METATABLE);
  return (ud != NULL) ? ud->const_ : NULL;
}

struct sljit_jump *luaSljit_tojump (lua_State *L, int narg) {
  luaSljitJump *ud = (luaSljitJump *)
    luaL_testudata(L, narg, JUMP_METATABLE);
  return (ud != NULL) ? ud->jump : NULL;
}

struct sljit_label *luaSljit_tolabel (lua_State *L, int narg) {
  luaSljitLabel *ud = (luaSljitLabel *)
    luaL_testudata(L, narg, LABEL_METATABLE);
  return (ud != NULL) ? ud->label : NULL;
}

struct sljit_compiler *luaSljit_get_compiler (lua_State *L, int narg) {
  struct sljit_compiler *res;
  int npop = 0;

  if (luaL_testudata(L, narg, JUMP_METATABLE) != NULL ||
      luaL_testudata(L, narg, LABEL_METATABLE) != NULL ||
      luaL_testudata(L, narg, CONST_METATABLE) != NULL) {
    lua_getuservalue(L, narg);
    lua_rawgeti(L, -1, COMPILER_UVAL_INDEX);
    narg = -1;
    npop = 2;
  }

  res = luaSljit_tocompiler(L, narg);
  if (npop > 0)
    lua_pop(L, npop);
  return res;
}

/* ============================================================
 * 模块入口
 * ============================================================ */

LUAMOD_API int luaopen_sljit (lua_State *L) {
  luaL_newlib(L, sljit_functions);

  register_udata(L, -1, ARG_METATABLE, arg_metafunctions, NULL);
  register_constants(L, -1);

  register_udata(L, -1, CONST_METATABLE, NULL, const_methods);
  register_udata(L, -1, JUMP_METATABLE,  NULL, jump_methods);
  register_udata(L, -1, LABEL_METATABLE, NULL, label_methods);

  register_udata(L, -1, CODE_METATABLE, code_metafunctions, code_methods);
  register_udata(L, -1, COMP_METATABLE, comp_metafunctions, comp_methods);

  return 1;
}
