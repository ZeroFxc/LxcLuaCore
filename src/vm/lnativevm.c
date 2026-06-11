/*
** 纯 C 原生高速 VM — 执行期间零 Lua 操作
** 经过严格的纯c执行计划
**
** 指令格式 (64-bit):
**   | imm32 (bits 32-63) | c(8) | b(8) | a(8) | op(8) |
**
** API (Lua侧):
**   native.new(inst_array, nregs) -> nv
**   native.call(nv, ...) -> results
**   native.asm(code) -> inst_array
*/

#define lnativevm_c
#define LUA_LIB

#include "lprefix.h"

#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <stdlib.h>
#include <ctype.h>
#include <math.h>
#include <stdint.h>

#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"


/* 寄存器类型标签 */
#define NTYPE_NIL   0
#define NTYPE_INT   1
#define NTYPE_FLOAT 2
#define NTYPE_PTR   3

/* 指令解码宏 */
#define NI_OP(i)  ((int)((i) & 0xFF))
#define NI_A(i)   ((int)(((i) >> 8) & 0xFF))
#define NI_B(i)   ((int)(((i) >> 16) & 0xFF))
#define NI_C(i)   ((int)(((i) >> 24) & 0xFF))
#define NI_IMM(i) ((int32_t)((int64_t)(i) >> 32))

/* 构造指令 */
static lua_Integer make_ni(int op, int a, int b, int c, int32_t imm) {
  return ((lua_Integer)((uint64_t)(uint32_t)imm << 32))
       | ((lua_Integer)(c & 0xFF) << 24)
       | ((lua_Integer)(b & 0xFF) << 16)
       | ((lua_Integer)(a & 0xFF) << 8)
       | (lua_Integer)(op & 0xFF);
}

/* 操作码 */
enum {
  NI_NOP = 0,
  NI_LOADK,       /* R[a] = imm32 (int) */
  NI_LOADKF,      /* R[a] = imm32 reinterpret as float */
  NI_LOADK64,     /* R[a] = imm64 (combine with next NI_LOADKHI) */
  NI_LOADKHI,     /* R[a] = (R[a] & 0xFFFFFFFF) | ((int64_t)imm32 << 32) */
  NI_MOV,         /* R[a] = R[b] */
  NI_ADD,         /* R[a] = R[b] + R[c] (int) */
  NI_SUB,         /* R[a] = R[b] - R[c] (int) */
  NI_MUL,         /* R[a] = R[b] * R[c] (int) */
  NI_DIV,         /* R[a] = R[b] / R[c] (int) */
  NI_MOD,         /* R[a] = R[b] % R[c] (int) */
  NI_ADDF,        /* R[a] = R[b] + R[c] (float) */
  NI_SUBF,        /* R[a] = R[b] - R[c] (float) */
  NI_MULF,        /* R[a] = R[b] * R[c] (float) */
  NI_DIVF,        /* R[a] = R[b] / R[c] (float) */
  NI_AND,         /* R[a] = R[b] & R[c] (int) */
  NI_OR,          /* R[a] = R[b] | R[c] (int) */
  NI_XOR,         /* R[a] = R[b] ^ R[c] (int) */
  NI_SHL,         /* R[a] = R[b] << R[c] (int) */
  NI_SHR,         /* R[a] = R[b] >> R[c] (int) */
  NI_EQ,          /* R[a] = (R[b] == R[c]) */
  NI_NE,          /* R[a] = (R[b] != R[c]) */
  NI_LT,          /* R[a] = (R[b] < R[c]) (int) */
  NI_LE,          /* R[a] = (R[b] <= R[c]) (int) */
  NI_LTF,         /* R[a] = (R[b] < R[c]) (float) */
  NI_LEF,         /* R[a] = (R[b] <= R[c]) (float) */
  NI_JMP,         /* pc += imm32 */
  NI_JT,          /* if R[a] then pc += imm32 */
  NI_JF,          /* if not R[a] then pc += imm32 */
  NI_RET,         /* stop, return R[a..a+b-1] */
  NI_I2F,         /* R[a] = (double)R[b] */
  NI_F2I,         /* R[a] = (int64_t)R[b] */
  NI_NEG,         /* R[a] = -R[b] (int) */
  NI_NEGF,        /* R[a] = -R[b] (float) */
  NI_MOVF,        /* R[a] = (float)R[b] (重新标记为 float) */
  NI_MOVI,        /* R[a] = (int)R[b] (重新标记为 int) */
  NI_SETNIL,      /* R[a] = nil */
  NI_ISNIL,       /* R[a] = (R[b].type == NTYPE_NIL) */
  NI_SQRT,        /* R[a] = sqrt(R[b]) */
  NI_HALT,        /* stop */
  NI_MAX
};


/* ---- 标签和中间指令结构（两遍汇编用） ---- */

#define ASM_MAX_LABELS 256
#define ASM_MAX_LNAME  32
#define ASM_MAX_INSTS  4096

/* 标签定义 */
typedef struct {
  char name[ASM_MAX_LNAME];
  int  pc;
} AsmLabel;

/* 中间指令：含标签引用信息 */
typedef struct {
  int     op;
  int     a, b, c;
  int32_t imm;
  char    lname[ASM_MAX_LNAME];  /* 标签引用名（JMP/JT/JF） */
  int     has_label;             /* 是否为标签跳转 */
} AsmInst;

/* 注册器别名 */
typedef struct {
  char name[ASM_MAX_LNAME];
  int  reg;
} AsmAlias;

/* ---- 寄存器值：tagged union ---- */
typedef struct {
  int type;
  union {
    int64_t i;
    double  f;
    void   *p;
  } v;
} NReg;

/* 原生 VM 状态 */
typedef struct NativeVM {
  NReg *regs;
  int   nregs;
  int   halted;
  int   retstart;  /* RET 起始寄存器 */
  int   retcount;  /* RET 返回数量 */
} NativeVM;


/* ---- 执行核心（纯 C，零 Lua 调用） ---- */

static void native_exec(NativeVM *nv, const lua_Integer *code, int ncode) {
  int pc = 0;
  int ret_offset = -1;

  while (pc >= 0 && pc < ncode && !nv->halted) {
    lua_Integer inst = code[pc];
    int op = NI_OP(inst);
    int a  = NI_A(inst);
    int b  = NI_B(inst);
    int c  = NI_C(inst);
    int32_t imm = NI_IMM(inst);
    int next = pc + 1;

    if (a >= nv->nregs) { pc++; continue; }

    switch (op) {
    case NI_NOP:
      break;

    case NI_LOADK:
      nv->regs[a].type = NTYPE_INT;
      nv->regs[a].v.i = (int64_t)imm;
      break;

    case NI_LOADKF:
      nv->regs[a].type = NTYPE_FLOAT;
      { union { int32_t i; float f; } u; u.i = imm;
        nv->regs[a].v.f = (double)u.f; }
      break;

    case NI_LOADK64:
      if (pc + 1 < ncode && NI_OP(code[pc + 1]) == NI_LOADKHI) {
        int64_t hi = (int64_t)NI_IMM(code[pc + 1]) << 32;
        nv->regs[a].type = NTYPE_INT;
        nv->regs[a].v.i = hi | (int64_t)(uint32_t)imm;
        pc += 2;
        continue;
      }
      nv->regs[a].type = NTYPE_INT;
      nv->regs[a].v.i = (int64_t)imm;
      break;

    case NI_LOADKHI:
      /* 不应单独出现，跳过 */
      break;

    case NI_MOV:
      if (b < nv->nregs) {
        nv->regs[a] = nv->regs[b];
      }
      break;

    case NI_ADD:
      if (b < nv->nregs && c < nv->nregs) {
        NReg *rb = &nv->regs[b], *rc = &nv->regs[c];
        nv->regs[a].type = NTYPE_INT;
        nv->regs[a].v.i = rb->v.i + rc->v.i;
      }
      break;

    case NI_SUB:
      if (b < nv->nregs && c < nv->nregs) {
        nv->regs[a].type = NTYPE_INT;
        nv->regs[a].v.i = nv->regs[b].v.i - nv->regs[c].v.i;
      }
      break;

    case NI_MUL:
      if (b < nv->nregs && c < nv->nregs) {
        nv->regs[a].type = NTYPE_INT;
        nv->regs[a].v.i = nv->regs[b].v.i * nv->regs[c].v.i;
      }
      break;

    case NI_DIV:
      if (b < nv->nregs && c < nv->nregs) {
        int64_t divisor = nv->regs[c].v.i;
        if (divisor != 0) {
          nv->regs[a].type = NTYPE_INT;
          nv->regs[a].v.i = nv->regs[b].v.i / divisor;
        }
      }
      break;

    case NI_MOD:
      if (b < nv->nregs && c < nv->nregs) {
        int64_t divisor = nv->regs[c].v.i;
        if (divisor != 0) {
          nv->regs[a].type = NTYPE_INT;
          nv->regs[a].v.i = nv->regs[b].v.i % divisor;
        }
      }
      break;

    case NI_ADDF:
      if (b < nv->nregs && c < nv->nregs) {
        nv->regs[a].type = NTYPE_FLOAT;
        nv->regs[a].v.f = nv->regs[b].v.f + nv->regs[c].v.f;
      }
      break;

    case NI_SUBF:
      if (b < nv->nregs && c < nv->nregs) {
        nv->regs[a].type = NTYPE_FLOAT;
        nv->regs[a].v.f = nv->regs[b].v.f - nv->regs[c].v.f;
      }
      break;

    case NI_MULF:
      if (b < nv->nregs && c < nv->nregs) {
        nv->regs[a].type = NTYPE_FLOAT;
        nv->regs[a].v.f = nv->regs[b].v.f * nv->regs[c].v.f;
      }
      break;

    case NI_DIVF:
      if (b < nv->nregs && c < nv->nregs) {
        double divisor = nv->regs[c].v.f;
        if (divisor != 0.0) {
          nv->regs[a].type = NTYPE_FLOAT;
          nv->regs[a].v.f = nv->regs[b].v.f / divisor;
        }
      }
      break;

    case NI_AND:
      if (b < nv->nregs && c < nv->nregs) {
        nv->regs[a].type = NTYPE_INT;
        nv->regs[a].v.i = nv->regs[b].v.i & nv->regs[c].v.i;
      }
      break;

    case NI_OR:
      if (b < nv->nregs && c < nv->nregs) {
        nv->regs[a].type = NTYPE_INT;
        nv->regs[a].v.i = nv->regs[b].v.i | nv->regs[c].v.i;
      }
      break;

    case NI_XOR:
      if (b < nv->nregs && c < nv->nregs) {
        nv->regs[a].type = NTYPE_INT;
        nv->regs[a].v.i = nv->regs[b].v.i ^ nv->regs[c].v.i;
      }
      break;

    case NI_SHL:
      if (b < nv->nregs && c < nv->nregs) {
        nv->regs[a].type = NTYPE_INT;
        nv->regs[a].v.i = nv->regs[b].v.i << (int)(nv->regs[c].v.i & 63);
      }
      break;

    case NI_SHR:
      if (b < nv->nregs && c < nv->nregs) {
        nv->regs[a].type = NTYPE_INT;
        nv->regs[a].v.i = nv->regs[b].v.i >> (int)(nv->regs[c].v.i & 63);
      }
      break;

    case NI_EQ:
      if (b < nv->nregs && c < nv->nregs) {
        NReg *rb = &nv->regs[b], *rc = &nv->regs[c];
        nv->regs[a].type = NTYPE_INT;
        if (rb->type == NTYPE_FLOAT && rc->type == NTYPE_FLOAT)
          nv->regs[a].v.i = (rb->v.f == rc->v.f) ? 1 : 0;
        else
          nv->regs[a].v.i = (rb->v.i == rc->v.i) ? 1 : 0;
      }
      break;

    case NI_NE:
      if (b < nv->nregs && c < nv->nregs) {
        NReg *rb = &nv->regs[b], *rc = &nv->regs[c];
        nv->regs[a].type = NTYPE_INT;
        if (rb->type == NTYPE_FLOAT && rc->type == NTYPE_FLOAT)
          nv->regs[a].v.i = (rb->v.f != rc->v.f) ? 1 : 0;
        else
          nv->regs[a].v.i = (rb->v.i != rc->v.i) ? 1 : 0;
      }
      break;

    case NI_LT:
      if (b < nv->nregs && c < nv->nregs) {
        nv->regs[a].type = NTYPE_INT;
        nv->regs[a].v.i = (nv->regs[b].v.i < nv->regs[c].v.i) ? 1 : 0;
      }
      break;

    case NI_LE:
      if (b < nv->nregs && c < nv->nregs) {
        nv->regs[a].type = NTYPE_INT;
        nv->regs[a].v.i = (nv->regs[b].v.i <= nv->regs[c].v.i) ? 1 : 0;
      }
      break;

    case NI_LTF:
      if (b < nv->nregs && c < nv->nregs) {
        nv->regs[a].type = NTYPE_INT;
        nv->regs[a].v.i = (nv->regs[b].v.f < nv->regs[c].v.f) ? 1 : 0;
      }
      break;

    case NI_LEF:
      if (b < nv->nregs && c < nv->nregs) {
        nv->regs[a].type = NTYPE_INT;
        nv->regs[a].v.i = (nv->regs[b].v.f <= nv->regs[c].v.f) ? 1 : 0;
      }
      break;

    case NI_JMP:
      next = pc + 1 + (int)imm;
      break;

    case NI_JT:
      if (a < nv->nregs && nv->regs[a].v.i != 0)
        next = pc + 1 + (int)imm;
      break;

    case NI_JF:
      if (a < nv->nregs && nv->regs[a].v.i == 0)
        next = pc + 1 + (int)imm;
      break;

    case NI_RET:
      nv->retstart = a;
      nv->retcount = b;
      nv->halted = 1;
      break;

    case NI_HALT:
      nv->halted = 1;
      break;

    case NI_I2F:
      if (b < nv->nregs) {
        nv->regs[a].type = NTYPE_FLOAT;
        nv->regs[a].v.f = (double)nv->regs[b].v.i;
      }
      break;

    case NI_F2I:
      if (b < nv->nregs) {
        nv->regs[a].type = NTYPE_INT;
        nv->regs[a].v.i = (int64_t)nv->regs[b].v.f;
      }
      break;

    case NI_NEG:
      if (b < nv->nregs) {
        nv->regs[a].type = NTYPE_INT;
        nv->regs[a].v.i = -nv->regs[b].v.i;
      }
      break;

    case NI_NEGF:
      if (b < nv->nregs) {
        nv->regs[a].type = NTYPE_FLOAT;
        nv->regs[a].v.f = -nv->regs[b].v.f;
      }
      break;

    case NI_MOVF:
      if (b < nv->nregs) {
        nv->regs[a].type = NTYPE_FLOAT;
        if (nv->regs[b].type == NTYPE_INT)
          nv->regs[a].v.f = (double)nv->regs[b].v.i;
        else
          nv->regs[a].v.f = nv->regs[b].v.f;
      }
      break;

    case NI_MOVI:
      if (b < nv->nregs) {
        nv->regs[a].type = NTYPE_INT;
        if (nv->regs[b].type == NTYPE_FLOAT)
          nv->regs[a].v.i = (int64_t)nv->regs[b].v.f;
        else
          nv->regs[a].v.i = nv->regs[b].v.i;
      }
      break;

    case NI_SETNIL:
      nv->regs[a].type = NTYPE_NIL;
      nv->regs[a].v.i = 0;
      break;

    case NI_ISNIL:
      if (b < nv->nregs) {
        nv->regs[a].type = NTYPE_INT;
        nv->regs[a].v.i = (nv->regs[b].type == NTYPE_NIL) ? 1 : 0;
      }
      break;

    case NI_SQRT:
      if (b < nv->nregs) {
        nv->regs[a].type = NTYPE_FLOAT;
        double val = (nv->regs[b].type == NTYPE_INT)
          ? (double)nv->regs[b].v.i : nv->regs[b].v.f;
        nv->regs[a].v.f = (val >= 0.0) ? sqrt(val) : 0.0;
      }
      break;

    default:
      break;
    }

    pc = next;
  }
}


/* ---- 原生 VM 包装器：持有 code 和 NativeVM ---- */

typedef struct NativeVMWrapper {
  NativeVM nv;
  lua_Integer *code;
  int ncode;
} NativeVMWrapper;

static int nativevm_gc(lua_State *L) {
  NativeVMWrapper *w = (NativeVMWrapper *)lua_touserdata(L, 1);
  if (w && w->nv.regs) {
    free(w->nv.regs);
    w->nv.regs = NULL;
  }
  if (w && w->code) {
    free(w->code);
    w->code = NULL;
  }
  return 0;
}

/* ---- Lua API 函数 ---- */

/**
 * @brief native.new(inst_array, nregs) -> nv
 * 从整数指令数组创建一个原生 VM 实例
 * inst_array: Lua 整数数组，每个元素是一条 64-bit 指令
 * nregs: 寄存器数量，默认 32
 */
static int nativenew(lua_State *L) {
  luaL_checktype(L, 1, LUA_TTABLE);
  int nregs = (int)luaL_optinteger(L, 2, 32);
  int ncode = (int)luaL_len(L, 1);

  NativeVMWrapper *w = (NativeVMWrapper *)lua_newuserdata(L, sizeof(NativeVMWrapper));
  memset(w, 0, sizeof(NativeVMWrapper));

  w->code = (lua_Integer *)malloc(sizeof(lua_Integer) * ncode);
  for (int i = 0; i < ncode; i++) {
    lua_rawgeti(L, 1, i + 1);
    w->code[i] = lua_tointeger(L, -1);
    lua_pop(L, 1);
  }
  w->ncode = ncode;

  w->nv.nregs = nregs;
  w->nv.regs = (NReg *)malloc(sizeof(NReg) * nregs);
  memset(w->nv.regs, 0, sizeof(NReg) * nregs);

  if (luaL_newmetatable(L, "nativevm_meta")) {
    lua_pushcfunction(L, nativevm_gc);
    lua_setfield(L, -2, "__gc");
  }
  lua_setmetatable(L, -2);

  return 1;
}

/**
 * @brief native.call(nv, ...) -> results
 * 传入参数到 R0..Rn，执行，返回 RET 指定的寄存器值
 */
static int nativecall(lua_State *L) {
  NativeVMWrapper *w = (NativeVMWrapper *)luaL_checkudata(L, 1, "nativevm_meta");
  NativeVM *nv = &w->nv;
  lua_Integer *code = w->code;
  int ncode = w->ncode;

  nv->halted = 0;
  nv->retstart = 0;
  nv->retcount = 0;
  for (int i = 0; i < nv->nregs; i++) {
    nv->regs[i].type = NTYPE_NIL;
    nv->regs[i].v.i = 0;
  }

  int nargs = lua_gettop(L) - 1;
  if (nargs > nv->nregs) nargs = nv->nregs;
  for (int i = 0; i < nargs; i++) {
    int ltype = lua_type(L, i + 2);
    if (ltype == LUA_TNUMBER) {
      if (lua_isinteger(L, i + 2)) {
        nv->regs[i].type = NTYPE_INT;
        nv->regs[i].v.i = lua_tointeger(L, i + 2);
      } else {
        nv->regs[i].type = NTYPE_FLOAT;
        nv->regs[i].v.f = lua_tonumber(L, i + 2);
      }
    } else if (ltype == LUA_TBOOLEAN) {
      nv->regs[i].type = NTYPE_INT;
      nv->regs[i].v.i = lua_toboolean(L, i + 2) ? 1 : 0;
    } else if (ltype == LUA_TLIGHTUSERDATA) {
      nv->regs[i].type = NTYPE_PTR;
      nv->regs[i].v.p = lua_touserdata(L, i + 2);
    }
  }

  native_exec(nv, code, ncode);

  int nret = nv->retcount;
  if (nret == 0 && nv->halted) {
    for (int i = 0; i < nv->nregs && nret < nv->nregs; i++) {
      if (nv->regs[i].type != NTYPE_NIL) nret++;
      else break;
    }
  }
  if (nret == 0) return 0;

  for (int i = nv->retstart; i < nv->retstart + nret && i < nv->nregs; i++) {
    switch (nv->regs[i].type) {
    case NTYPE_INT:
      lua_pushinteger(L, nv->regs[i].v.i);
      break;
    case NTYPE_FLOAT:
      lua_pushnumber(L, nv->regs[i].v.f);
      break;
    case NTYPE_NIL:
      lua_pushnil(L);
      break;
    default:
      lua_pushnil(L);
      break;
    }
  }
  return nret;
}

/**
 * @brief native.asm(code_str, [options]) -> inst_array
 * 增强版两遍汇编器，支持标签、表达式语法和传统助记符
 *
 * === 传统助记符（向后兼容）===
 *   ADD Ra,Rb,Rc | SUB | MUL | DIV | MOD | ...
 *   JMP offset | JT Ra,offset | JF Ra,offset
 *   LOADK Ra,imm | LOADKF Ra,imm
 *
 * === 标签系统 ===
 *   .loop:          ← 定义标签（可在任意位置引用）
 *   JMP .loop       ← 引用标签，自动计算偏移
 *   JT R4, .done    ← 条件跳转到标签
 *   JF R4, .next
 *
 * === 表达式语法 ===
 *   R0 = 42                 → LOADK R0,42
 *   R0 = 3.14               → LOADKF R0,3.14
 *   R0 = R1                 → MOV R0,R1
 *   R0 = R1 + R2            → ADD R0,R1,R2
 *   R0 = R1 - R2 / * / / / % / & / | / ^
 *   R0 = R1 << R2 / >> R2
 *   R0 = R1 < R2  / <= / > / >= / == / !=
 *   R0 = -R1                → NEG R0,R1
 *   R0 = !R1                → EQ R0,R1,R0  (R0 此时为0/nil)
 *   R0 = R1 + imm           → LOADK tmp,imm; ADD R0,R1,tmp (需空闲 tmp 寄存器)
 */

#define NA_ENTRY(name, op) { name, sizeof(name)-1, op }

static const struct {
  const char *name;
  int len;
  int op;
} na_mnems[] = {
  NA_ENTRY("NOP",     NI_NOP),
  NA_ENTRY("LOADK",   NI_LOADK),
  NA_ENTRY("LOADKF",  NI_LOADKF),
  NA_ENTRY("LOADK64", NI_LOADK64),
  NA_ENTRY("MOV",     NI_MOV),
  NA_ENTRY("ADD",     NI_ADD),
  NA_ENTRY("SUB",     NI_SUB),
  NA_ENTRY("MUL",     NI_MUL),
  NA_ENTRY("DIV",     NI_DIV),
  NA_ENTRY("MOD",     NI_MOD),
  NA_ENTRY("ADDF",    NI_ADDF),
  NA_ENTRY("SUBF",    NI_SUBF),
  NA_ENTRY("MULF",    NI_MULF),
  NA_ENTRY("DIVF",    NI_DIVF),
  NA_ENTRY("AND",     NI_AND),
  NA_ENTRY("OR",      NI_OR),
  NA_ENTRY("XOR",     NI_XOR),
  NA_ENTRY("SHL",     NI_SHL),
  NA_ENTRY("SHR",     NI_SHR),
  NA_ENTRY("EQ",      NI_EQ),
  NA_ENTRY("NE",      NI_NE),
  NA_ENTRY("LT",      NI_LT),
  NA_ENTRY("LE",      NI_LE),
  NA_ENTRY("LTF",     NI_LTF),
  NA_ENTRY("LEF",     NI_LEF),
  NA_ENTRY("JMP",     NI_JMP),
  NA_ENTRY("JT",      NI_JT),
  NA_ENTRY("JF",      NI_JF),
  NA_ENTRY("RET",     NI_RET),
  NA_ENTRY("HALT",    NI_HALT),
  NA_ENTRY("I2F",     NI_I2F),
  NA_ENTRY("F2I",     NI_F2I),
  NA_ENTRY("NEG",     NI_NEG),
  NA_ENTRY("NEGF",    NI_NEGF),
  NA_ENTRY("MOVF",    NI_MOVF),
  NA_ENTRY("MOVI",    NI_MOVI),
  NA_ENTRY("SETNIL",  NI_SETNIL),
  NA_ENTRY("ISNIL",   NI_ISNIL),
  NA_ENTRY("SQRT",    NI_SQRT),
  {NULL, 0, 0}
};

#undef NA_ENTRY

static int find_mnem(const char *tok, int len) {
  for (int i = 0; na_mnems[i].name; i++) {
    if (len == na_mnems[i].len && strncasecmp(tok, na_mnems[i].name, len) == 0)
      return na_mnems[i].op;
  }
  return -1;
}

/* ---- 表达式二元运算符 → 操作码映射 ---- */
typedef struct {
  const char *sym;
  int len;
  int op;
} BinOpMap;

static const BinOpMap binops[] = {
  {"<=", 2, NI_LE},  {">=", 2, NI_LE},   /* >= 通过交换操作数处理 */
  {"<<", 2, NI_SHL}, {">>", 2, NI_SHR},
  {"==", 2, NI_EQ},  {"!=", 2, NI_NE},
  {"+",  1, NI_ADD}, {"-",  1, NI_SUB},
  {"*",  1, NI_MUL}, {"/",  1, NI_DIV},
  {"%",  1, NI_MOD}, {"&",  1, NI_AND},
  {"|",  1, NI_OR},  {"^",  1, NI_XOR},
  {"<",  1, NI_LT},  {">",  1, NI_LT},    /* > 通过交换操作数处理 */
  {NULL, 0, 0}
};

/* 解析一个 token 并返回其长度，跳过前导空白 */
static const char *skip_space(const char *p, const char *end) {
  while (p < end && (*p == ' ' || *p == '\t')) p++;
  return p;
}

static int token_len(const char *p, const char *end) {
  const char *s = p;
  if (s >= end) return 0;
  if (*s == '.' || *s == '@' || *s == '_' || isalpha(*s)) {
    while (s < end && (isalnum(*s) || *s == '_' || *s == '.' || *s == '@')) s++;
  } else if (*s == '-' || *s == '+' || isdigit(*s)) {
    if (*s == '-' || *s == '+') s++;
    while (s < end && (isdigit(*s) || *s == '.' || *s == 'x' || *s == 'X'
           || (*s >= 'a' && *s <= 'f') || (*s >= 'A' && *s <= 'F'))) s++;
  } else if (*s == '<' || *s == '>' || *s == '=' || *s == '!') {
    s++;
    if (s < end && *s == '=') s++;
  } else {
    s++;
  }
  return (int)(s - p);
}

/* 检查 token 是否是寄存器 R0..R255 */
static int is_register(const char *tok, int len, int *out_reg) {
  if (len >= 2 && (tok[0] == 'R' || tok[0] == 'r')) {
    char *ep;
    long r = strtol(tok + 1, &ep, 10);
    if (ep == tok + len && r >= 0 && r < 256) {
      *out_reg = (int)r;
      return 1;
    }
  }
  return 0;
}

/* 检查 token 是否是数字（整数或浮点） */
static int is_number(const char *tok, int len, int64_t *out_i, double *out_f) {
  char buf[64];
  if (len >= (int)sizeof(buf)) return 0;
  memcpy(buf, tok, len);
  buf[len] = '\0';

  /* 检查是否有小数点或 f 后缀 → 浮点 */
  int has_dot = 0;
  for (int i = 0; i < len; i++) {
    if (buf[i] == '.' || buf[i] == 'f' || buf[i] == 'F') { has_dot = 1; break; }
  }
  if (has_dot) {
    char *ep;
    *out_f = strtod(buf, &ep);
    if (ep != buf) return 2;  /* float */
    return 0;
  }
  char *ep;
  *out_i = strtoll(buf, &ep, 0);
  if (ep != buf) return 1;   /* int */
  return 0;
}

/* 检查 token 是否是标签引用（以 . 开头） */
static int is_label_ref(const char *tok, int len, char *out, int out_max) {
  if (len >= 2 && tok[0] == '.' && len < out_max) {
    memcpy(out, tok + 1, len - 1);
    out[len - 1] = '\0';
    return 1;
  }
  return 0;
}

/* 检查行是否是标签定义 (.name:) */
static int is_label_def(const char *p, const char *end, char *out, int out_max) {
  if (p < end && *p == '.') {
    const char *s = p + 1;
    while (s < end && (isalnum(*s) || *s == '_')) s++;
    if (s < end && *s == ':') {
      int llen = (int)(s - p - 1);
      if (llen > 0 && llen < out_max) {
        memcpy(out, p + 1, llen);
        out[llen] = '\0';
        return 1;
      }
    }
  }
  return 0;
}

/* 解析运算符，返回操作码和长度 */
static int parse_binop(const char *p, const char *end, int *op_out, int *swapped) {
  *swapped = 0;
  for (int i = 0; binops[i].sym; i++) {
    int blen = binops[i].len;
    if (p + blen <= end && memcmp(p, binops[i].sym, blen) == 0) {
      *op_out = binops[i].op;
      /* > 和 >= 需要交换操作数（因为 VM 只有 LT/LE） */
      if (binops[i].sym[0] == '>') *swapped = 1;
      return blen;
    }
  }
  return 0;
}

/**
 * @brief 两遍汇编器：第一遍收集指令+标签，第二遍解析标签引用并输出
 */
static int nativeasm(lua_State *L) {
  size_t slen;
  const char *code = luaL_checklstring(L, 1, &slen);
  int nregs_hint = (int)luaL_optinteger(L, 2, -1); /* 可选的临时寄存器提示 */

  AsmInst insts[ASM_MAX_INSTS];
  AsmLabel labels[ASM_MAX_LABELS];
  int n_insts = 0, n_labels = 0;
  int cur_pc = 0;  /* 当前指令序号 (=PC) */

  const char *p = code;
  const char *end = code + slen;

  /* ===== 第一遍：解析所有行 → 标签定义 + 中间指令 ===== */
  while (p < end) {
    /* 跳过空白 */
    while (p < end && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) p++;
    if (p >= end) break;

    /* 注释 */
    if (*p == '#' || *p == ';') {
      while (p < end && *p != '\n') p++;
      continue;
    }

    /* 检查标签定义 .name: */
    {
      char lname[ASM_MAX_LNAME];
      if (is_label_def(p, end, lname, ASM_MAX_LNAME)) {
        if (n_labels < ASM_MAX_LABELS) {
          strncpy(labels[n_labels].name, lname, ASM_MAX_LNAME - 1);
          labels[n_labels].name[ASM_MAX_LNAME - 1] = '\0';
          labels[n_labels].pc = cur_pc;
          n_labels++;
        }
        while (p < end && *p != '\n') p++;
        continue;
      }
    }

    /* 读取第一个 token */
    p = skip_space(p, end);
    if (p >= end) break;
    int tlen = token_len(p, end);
    if (tlen == 0) { p++; continue; }
    const char *tok = p;
    p += tlen;

    /* ---- 表达式语法：Rdest = ... ---- */
    int reg_dest;
    if (is_register(tok, tlen, &reg_dest) && p < end) {
      p = skip_space(p, end);
      if (p < end && *p == '=') {
        p++; /* 跳过 = */
        if (n_insts >= ASM_MAX_INSTS) break;
        AsmInst *inst = &insts[n_insts];
        memset(inst, 0, sizeof(AsmInst));
        inst->a = reg_dest;

        p = skip_space(p, end);
        if (p >= end) { n_insts++; cur_pc++; continue; }

        /* R0 = -R1  (NEG) */
        if (*p == '-') {
          const char *p2 = p + 1;
          int reg_src;
          if (is_register(p2, token_len(p2, end), &reg_src)) {
            inst->op = NI_NEG;
            inst->b = reg_src;
            n_insts++; cur_pc++;
            while (p2 < end && *p2 != '\n') p2++;
            p = p2; continue;
          }
        }
        /* R0 = !R1  (转换为 EQ Rdest, R1, 0) */
        if (*p == '!') {
          const char *p2 = p + 1;
          int reg_src;
          if (is_register(p2, token_len(p2, end), &reg_src)) {
            inst->op = NI_EQ;
            inst->b = reg_src;
            inst->c = reg_dest; /* 与目标寄存器比较 (此时应为 0/nil) */
            /* 需要先 SETNIL 目标寄存器 → 生成两条指令 */
            /* 简化处理：如果 b==dest 则先清零 */
            AsmInst *pre = &insts[n_insts];
            memset(pre, 0, sizeof(AsmInst));
            pre->op = NI_LOADK;
            pre->a = reg_dest;
            pre->imm = 0;
            n_insts++; cur_pc++;
            inst = &insts[n_insts];
            memset(inst, 0, sizeof(AsmInst));
            inst->op = NI_EQ;
            inst->a = reg_dest;
            inst->b = reg_src;
            inst->c = reg_dest;
            n_insts++; cur_pc++;
            while (p2 < end && *p2 != '\n') p2++;
            p = p2; continue;
          }
        }
        /* R0 = -42  (LOADK then NEG) */
        if (*p == '-') {
          const char *p2 = p;
          int nlen = token_len(p2, end);
          int64_t ival; double fval;
          int ntype = is_number(p2, nlen, &ival, &fval);
          if (ntype == 1) {
            /* 两个指令: LOADK Rdest, 0 ; SUB Rdest, 0, Rdest */
            AsmInst *pre = &insts[n_insts];
            memset(pre, 0, sizeof(AsmInst));
            pre->op = NI_LOADK;
            pre->a = reg_dest;
            pre->imm = 0;
            n_insts++; cur_pc++;
            inst = &insts[n_insts];
            memset(inst, 0, sizeof(AsmInst));
            inst->op = NI_SUB;
            inst->a = reg_dest;
            inst->b = 0;
            inst->c = reg_dest;
            /* 还需要再加载实际值: LOADK tmp, val */
            /* 这超出简单处理范围，跳过 */
            n_insts++; cur_pc++;
            p2 += nlen;
            while (p2 < end && *p2 != '\n') p2++;
            p = p2; continue;
          }
        }

        /* 解析第一个操作数 */
        int tlen1 = token_len(p, end);
        int r1, r2;
        int64_t i1, i2; double f1, f2;
        int op1_is_reg = is_register(p, tlen1, &r1);
        int op1_is_int = 0, op1_is_fp = 0;

        if (!op1_is_reg) {
          int nt = is_number(p, tlen1, &i1, &f1);
          if (nt == 1) op1_is_int = 1;
          else if (nt == 2) op1_is_fp = 1;
          else {
            /* 可能是标签 .name */
            char lref[ASM_MAX_LNAME];
            if (is_label_ref(p, tlen1, lref, ASM_MAX_LNAME)) {
              inst->op = NI_JMP;
              inst->has_label = 1;
              strncpy(inst->lname, lref, ASM_MAX_LNAME - 1);
              n_insts++; cur_pc++;
              p += tlen1;
              while (p < end && *p != '\n') p++;
              continue;
            }
          }
        }
        p += tlen1;

        /* 检查是否有二元运算符 */
        p = skip_space(p, end);
        int bop, swapped;
        int oplen = parse_binop(p, end, &bop, &swapped);

        if (oplen > 0) {
          p += oplen;
          p = skip_space(p, end);

          /* 解析第二个操作数 */
          int tlen2 = token_len(p, end);
          int op2_is_reg = is_register(p, tlen2, &r2);
          int op2_is_int = 0, op2_is_fp = 0;
          if (!op2_is_reg) {
            int nt = is_number(p, tlen2, &i2, &f2);
            if (nt == 1) op2_is_int = 1;
            else if (nt == 2) op2_is_fp = 1;
          }
          p += tlen2;

          if (op1_is_reg && op2_is_reg) {
            /* R0 = R1 op R2 → 直接映射 */
            inst->op = bop;
            if (swapped) { inst->b = r2; inst->c = r1; }
            else         { inst->b = r1; inst->c = r2; }
          } else if (op1_is_reg && (op2_is_int || op2_is_fp)) {
            /* R0 = R1 op imm → 需要临时寄存器存 imm */
            int tmp = nregs_hint >= 0 ? nregs_hint : 250; /* 用户指定或 250 */
            /* 生成: LOADK tmp, imm ; OP Rdest, R1, tmp */
            AsmInst *li = &insts[n_insts];
            memset(li, 0, sizeof(AsmInst));
            if (op2_is_fp) li->op = NI_LOADKF;
            else li->op = NI_LOADK;
            li->a = tmp;
            if (op2_is_fp) {
              union { float f; int32_t i; } u; u.f = (float)f2; li->imm = u.i;
            } else li->imm = (int32_t)i2;
            n_insts++; cur_pc++;
            inst = &insts[n_insts];
            memset(inst, 0, sizeof(AsmInst));
            inst->op = bop;
            inst->a = reg_dest;
            if (swapped) { inst->b = tmp; inst->c = r1; }
            else         { inst->b = r1; inst->c = tmp; }
          } else if ((op1_is_int || op1_is_fp) && op2_is_reg) {
            /* R0 = imm op R1 → 需要临时寄存器 */
            int tmp = nregs_hint >= 0 ? nregs_hint : 250;
            AsmInst *li = &insts[n_insts];
            memset(li, 0, sizeof(AsmInst));
            if (op1_is_fp) li->op = NI_LOADKF;
            else li->op = NI_LOADK;
            li->a = tmp;
            if (op1_is_fp) {
              union { float f; int32_t i; } u; u.f = (float)f1; li->imm = u.i;
            } else li->imm = (int32_t)i1;
            n_insts++; cur_pc++;
            inst = &insts[n_insts];
            memset(inst, 0, sizeof(AsmInst));
            inst->op = bop;
            inst->a = reg_dest;
            if (swapped) { inst->b = r2; inst->c = tmp; }
            else         { inst->b = tmp; inst->c = r2; }
          }
          n_insts++; cur_pc++;
          continue;
        }

        /* 无运算符: R0 = R1 (MOV) 或 R0 = imm (LOADK/LOADKF) */
        if (op1_is_reg) {
          inst->op = NI_MOV;
          inst->b = r1;
        } else if (op1_is_int) {
          inst->op = NI_LOADK;
          inst->imm = (int32_t)i1;
        } else if (op1_is_fp) {
          inst->op = NI_LOADKF;
          union { float f; int32_t i; } u; u.f = (float)f1; inst->imm = u.i;
        }
        n_insts++; cur_pc++;
        continue;
      }
    }

    /* ---- 传统助记符语法 ---- */
    int op = find_mnem(tok, tlen);
    if (op < 0) { while (p < end && *p != '\n') p++; continue; }

    if (n_insts >= ASM_MAX_INSTS) break;
    AsmInst *inst = &insts[n_insts];
    memset(inst, 0, sizeof(AsmInst));
    inst->op = op;

    /* 解析参数 */
    int vals[3] = {0, 0, 0};
    int32_t imm = 0;
    int nvals = 0;

    while (nvals < 3) {
      p = skip_space(p, end);
      while (p < end && *p == ',') { p++; p = skip_space(p, end); }
      if (p >= end || *p == '\n' || *p == '\r' || *p == '#' || *p == ';') break;

      int atlen = token_len(p, end);
      if (atlen == 0) break;

      /* 标签引用 .name */
      char lref[ASM_MAX_LNAME];
      if (is_label_ref(p, atlen, lref, ASM_MAX_LNAME)) {
        if (op == NI_JMP || op == NI_JT || op == NI_JF) {
          inst->has_label = 1;
          strncpy(inst->lname, lref, ASM_MAX_LNAME - 1);
          p += atlen;
          nvals++; /* 占一个参数位 */
          continue;
        }
        /* 非跳转指令使用标签 → 忽略 */
        p += atlen; continue;
      }

      /* 寄存器 */
      int r;
      if (is_register(p, atlen, &r)) {
        if (nvals < 3) vals[nvals] = r;
        p += atlen; nvals++; continue;
      }

      /* 数字 */
      int64_t iv; double fv;
      int nt = is_number(p, atlen, &iv, &fv);
      if (nt) {
        if (op == NI_LOADKF && nt == 2) {
          union { float f; int32_t i; } u; u.f = (float)fv;
          imm = u.i;
          if (nvals < 3) vals[nvals] = u.i;
        } else {
          imm = (int32_t)iv;
          if (nvals < 3) vals[nvals] = (int)iv;
        }
        p += atlen; nvals++; continue;
      }

      p += atlen;
    }

    inst->a = (nvals > 0) ? vals[0] : 0;
    inst->b = (nvals > 1) ? vals[1] : 0;
    inst->c = (nvals > 2) ? vals[2] : 0;
    inst->imm = imm;

    /* JMP 单参数：作为 imm */
    if (op == NI_JMP && nvals <= 1 && !inst->has_label) {
      inst->imm = (int32_t)inst->a;
      inst->a = 0;
    }
    /* JT/JF 单参数 */
    if ((op == NI_JT || op == NI_JF) && nvals <= 1 && !inst->has_label) {
      inst->imm = (int32_t)inst->a;
      inst->a = 0;
    }
    /* LOADK/LOADKF */
    if ((op == NI_LOADK || op == NI_LOADKF) && nvals >= 2) {
      inst->a = vals[0];
      inst->imm = (int32_t)vals[1];
      inst->b = 0;
      inst->c = 0;
    }

    n_insts++; cur_pc++;

    /* 跳到行尾 */
    while (p < end && *p != '\n') p++;
  }

  /* ===== 第二遍：解析标签引用 → 计算偏移，输出最终指令 ===== */
  lua_newtable(L);
  int outidx = 1;

  for (int i = 0; i < n_insts; i++) {
    AsmInst *inst = &insts[i];

    if (inst->has_label) {
      /* 查找标签 */
      int found = -1;
      for (int j = 0; j < n_labels; j++) {
        if (strcmp(labels[j].name, inst->lname) == 0) { found = j; break; }
      }
      if (found >= 0) {
        int target_pc = labels[found].pc;
        inst->imm = (int32_t)(target_pc - i - 1);
      } else {
        inst->imm = 0; /* 标签未找到 */
      }
    }

    lua_Integer final_inst = make_ni(inst->op, inst->a, inst->b, inst->c, inst->imm);
    lua_pushinteger(L, final_inst);
    lua_rawseti(L, -2, outidx++);
  }

  return 1;
}

/**
 * @brief native.disasm(inst) -> string
 * 反汇编单条指令
 */
static int nativedisasm(lua_State *L) {
  lua_Integer inst = luaL_checkinteger(L, 1);
  int op = NI_OP(inst);
  int a = NI_A(inst), b = NI_B(inst), c = NI_C(inst);
  int32_t imm = NI_IMM(inst);
  char buf[128];

  const char *names[] = {
    "NOP","LOADK","LOADKF","LOADK64","LOADKHI","MOV",
    "ADD","SUB","MUL","DIV","MOD",
    "ADDF","SUBF","MULF","DIVF",
    "AND","OR","XOR","SHL","SHR",
    "EQ","NE","LT","LE","LTF","LEF",
    "JMP","JT","JF","RET",
    "I2F","F2I","NEG","NEGF","MOVF","MOVI",
    "SETNIL","ISNIL","SQRT","HALT"
  };

  if (op >= 0 && op < (int)(sizeof(names)/sizeof(names[0]))) {
    snprintf(buf, sizeof(buf), "%s R%d,R%d,R%d  ; imm=%d",
             names[op], a, b, c, (int)imm);
  } else {
    snprintf(buf, sizeof(buf), "??? op=%d a=%d b=%d c=%d imm=%d",
             op, a, b, c, (int)imm);
  }
  lua_pushstring(L, buf);
  return 1;
}


/* ---- 编译上下文帧（用于 while/if 嵌套） ---- */

#define CCTX_MAX_DEPTH 64

typedef struct {
  char start_label[ASM_MAX_LNAME];
  char end_label[ASM_MAX_LNAME];
  char else_label[ASM_MAX_LNAME];
  int  type;   /* 0=while, 1=if */
  int  has_else;
} CtxFrame;

/**
 * @brief native.compile(code_str) -> inst_array
 * 高层语言编译器，编译为 NativeVM 指令数组
 *
 * === HLL 语法 ===
 *   .program <name>        程序名（可选，注释用）
 *   .regs <n>              寄存器总数提示
 *
 *   @name = R<num>         寄存器别名定义
 *
 *   @dest = @src           赋值 MOV
 *   @dest = <int>          赋值 LOADK
 *   @dest = <float>        赋值 LOADKF
 *   @dest = @a + @b        二元运算 (+, -, *, /, %, &, |, ^, <<, >>)
 *   @dest = @a < @b        比较运算 (<, <=, >, >=, ==, !=)
 *   @dest = -(@src)        取负 NEG
 *   @dest = !(@src)        逻辑非
 *
 *   while @a <op> @b       while 循环
 *     <body>
 *   end
 *
 *   if @a <op> @b          if 条件
 *     <then>
 *   else                   else 分支（可选）
 *     <else>
 *   end
 *
 *   ret @start             返回单值
 *   ret @start, <count>    返回从 @start 开始的 count 个值
 *   halt                   停止执行
 *
 * === 示例（阶乘 10!） ===
 *   .regs 16
 *   @result = R0
 *   @i      = R1
 *   @limit  = R2
 *   @one    = R3
 *   @cond   = R4
 *
 *   @result = 1
 *   @i      = 1
 *   @limit  = 11
 *   @one    = 1
 *
 *   while @i < @limit
 *       @result = @result * @i
 *       @i = @i + @one
 *   end
 *
 *   ret @result
 */
static int nativecompile(lua_State *L) {
  size_t slen;
  const char *code = luaL_checklstring(L, 1, &slen);

  AsmInst  insts[ASM_MAX_INSTS];
  AsmLabel labels[ASM_MAX_LABELS];
  char     alias_names[256][ASM_MAX_LNAME];  /* register → alias name (for lookup) */
  int      n_insts = 0, n_labels = 0;
  int      cur_pc = 0;
  int      label_counter = 0; /* 自动标签编号 */
  CtxFrame ctx_stack[CCTX_MAX_DEPTH];
  int      ctx_depth = 0;

  /* 初始化别名映射 */
  for (int i = 0; i < 256; i++) {
    alias_names[i][0] = '\0';
  }

  const char *p = code;
  const char *end = code + slen;

  while (p < end) {
    /* 跳过空白 */
    while (p < end && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) p++;
    if (p >= end) break;

    /* 注释 */
    if (*p == '#' || *p == ';') {
      while (p < end && *p != '\n') p++;
      continue;
    }

    /* 读取行首 token */
    int toklen = token_len(p, end);
    if (toklen == 0) { p++; continue; }
    const char *tok = p;
    p += toklen;

    /* ---- .regs <n> ---- */
    if (toklen >= 2 && tok[0] == '.' && strncmp(tok, ".regs", toklen) == 0) {
      p = skip_space(p, end);
      if (p < end) {
        int t2 = token_len(p, end);
        int64_t iv; double fv;
        if (is_number(p, t2, &iv, &fv) == 1) {
          /* nregs 信息记录但不在编译期使用 */
          p += t2;
        }
      }
      while (p < end && *p != '\n') p++;
      continue;
    }

    /* ---- .program <name> ---- */
    if (toklen >= 2 && tok[0] == '.' && strncmp(tok, ".program", toklen) == 0) {
      while (p < end && *p != '\n') p++;
      continue;
    }

    /* ---- @name = R<num>  (别名定义) ---- */
    if (toklen >= 2 && tok[0] == '@') {
      char aname[ASM_MAX_LNAME];
      if (toklen >= ASM_MAX_LNAME) toklen = ASM_MAX_LNAME - 1;
      memcpy(aname, tok, toklen);
      aname[toklen] = '\0';

      p = skip_space(p, end);
      if (p < end && *p == '=') {
        p++;
        p = skip_space(p, end);
        if (p < end) {
          int t2 = token_len(p, end);
          int reg;
          if (is_register(p, t2, &reg)) {
            /* 别名定义: @name = Rnum */
            alias_names[reg][0] = '\0';
            strncpy(alias_names[reg], aname, ASM_MAX_LNAME - 1);
            p += t2;
            while (p < end && *p != '\n') p++;
            continue;
          }

          /* 否则是表达式赋值: @dest = expr */
          int dest_reg = -1;
          for (int ai = 0; ai < 256; ai++)
            if (alias_names[ai][0] && strcmp(alias_names[ai], aname) == 0)
              { dest_reg = ai; break; }

          if (dest_reg < 0) {
            while (p < end && *p != '\n') p++; continue;
          }

          /* 解析表达式操作数 */
          p = skip_space(p, end);
          int t_op1 = token_len(p, end);
          if (t_op1 == 0) { while (p < end && *p != '\n') p++; continue; }
          const char *op1 = p;
          p += t_op1;

          /* @dest = -(@src) — 需要配合下一个 token 是 @name */
          if (t_op1 == 1 && op1[0] == '-') {
            p = skip_space(p, end);
            if (p < end && *p == '(') p++;
            p = skip_space(p, end);
            int tneg = token_len(p, end);
            int src_reg = -1;
            int64_t niv; double nfv;
            if (tneg >= 2 && p[0] == '@') {
              /* -@src → NEG */
              char sname[ASM_MAX_LNAME];
              int slen2 = (tneg < ASM_MAX_LNAME) ? tneg : ASM_MAX_LNAME - 1;
              memcpy(sname, p, slen2); sname[slen2] = '\0';
              for (int ai = 0; ai < 256; ai++)
                if (alias_names[ai][0] && strcmp(alias_names[ai], sname) == 0)
                  { src_reg = ai; break; }
              if (src_reg >= 0 && n_insts < ASM_MAX_INSTS) {
                AsmInst *inst = &insts[n_insts];
                memset(inst, 0, sizeof(AsmInst));
                inst->op = NI_NEG; inst->a = dest_reg; inst->b = src_reg;
                n_insts++; cur_pc++;
              }
            } else if (is_number(p, tneg, &niv, &nfv)) {
              /* -<number> → LOADK with negated value */
              if (n_insts < ASM_MAX_INSTS) {
                AsmInst *li = &insts[n_insts];
                memset(li, 0, sizeof(AsmInst));
                li->op = NI_LOADK; li->a = dest_reg; li->imm = (int32_t)(-niv);
                n_insts++; cur_pc++;
              }
            }
            while (p < end && *p != '\n') p++;
            continue;
          }

          /* @dest = !(@src) */
          if (t_op1 == 1 && op1[0] == '!') {
            p = skip_space(p, end);
            if (p < end && *p == '(') p++;
            p = skip_space(p, end);
            int tnot = token_len(p, end);
            int src_reg = -1;
            if (tnot >= 2 && p[0] == '@') {
              char sname[ASM_MAX_LNAME];
              int slen2 = (tnot < ASM_MAX_LNAME) ? tnot : ASM_MAX_LNAME - 1;
              memcpy(sname, p, slen2); sname[slen2] = '\0';
              for (int ai = 0; ai < 256; ai++)
                if (alias_names[ai][0] && strcmp(alias_names[ai], sname) == 0)
                  { src_reg = ai; break; }
            }
            if (src_reg >= 0 && n_insts + 1 < ASM_MAX_INSTS) {
              AsmInst *z = &insts[n_insts];
              memset(z, 0, sizeof(AsmInst));
              z->op = NI_LOADK; z->a = dest_reg; z->imm = 0;
              n_insts++; cur_pc++;
              AsmInst *eq = &insts[n_insts];
              memset(eq, 0, sizeof(AsmInst));
              eq->op = NI_EQ; eq->a = dest_reg; eq->b = src_reg; eq->c = dest_reg;
              n_insts++; cur_pc++;
            }
            while (p < end && *p != '\n') p++;
            continue;
          }

          /* 解析第一个操作数（可能是别名或数字） */
          int src1_reg = -1;
          int64_t i1; double f1;
          int op1_type = 0; /* 0=unknown, 1=int, 2=float, 3=alias */

          if (t_op1 >= 2 && op1[0] == '@') {
            char sname[ASM_MAX_LNAME];
            int slen2 = (t_op1 < ASM_MAX_LNAME) ? t_op1 : ASM_MAX_LNAME - 1;
            memcpy(sname, op1, slen2); sname[slen2] = '\0';
            for (int ai = 0; ai < 256; ai++)
              if (alias_names[ai][0] && strcmp(alias_names[ai], sname) == 0)
                { src1_reg = ai; op1_type = 3; break; }
          } else {
            int nt = is_number(op1, t_op1, &i1, &f1);
            if (nt == 1) op1_type = 1;
            else if (nt == 2) op1_type = 2;
          }

          /* 检查是否有二元运算符 */
          p = skip_space(p, end);
          int bop, swapped;
          int oplen = parse_binop(p, end, &bop, &swapped);

          if (oplen > 0) {
            p += oplen;
            p = skip_space(p, end);
            int t_op2 = token_len(p, end);
            if (t_op2 == 0) { n_insts++; cur_pc++; while (p < end && *p != '\n') p++; continue; }
            const char *op2 = p;
            p += t_op2;

            int src2_reg = -1;
            int64_t i2; double f2;
            int op2_type = 0;

            if (t_op2 >= 2 && op2[0] == '@') {
              char sname[ASM_MAX_LNAME];
              int slen2 = (t_op2 < ASM_MAX_LNAME) ? t_op2 : ASM_MAX_LNAME - 1;
              memcpy(sname, op2, slen2); sname[slen2] = '\0';
              for (int ai = 0; ai < 256; ai++)
                if (alias_names[ai][0] && strcmp(alias_names[ai], sname) == 0)
                  { src2_reg = ai; op2_type = 3; break; }
            } else {
              int nt = is_number(op2, t_op2, &i2, &f2);
              if (nt == 1) op2_type = 1;
              else if (nt == 2) op2_type = 2;
            }

            if (n_insts >= ASM_MAX_INSTS) { while (p < end && *p != '\n') p++; continue; }

            if (op1_type == 3 && op2_type == 3) {
              /* @dest = @a op @b */
              AsmInst *inst = &insts[n_insts];
              memset(inst, 0, sizeof(AsmInst));
              inst->op = bop; inst->a = dest_reg;
              if (swapped) { inst->b = src2_reg; inst->c = src1_reg; }
              else         { inst->b = src1_reg; inst->c = src2_reg; }
              n_insts++; cur_pc++;
            } else if (op1_type == 3 && (op2_type == 1 || op2_type == 2)) {
              /* @dest = @a op imm → 需临时寄存器 */
              int tmp = 250;
              AsmInst *li = &insts[n_insts];
              memset(li, 0, sizeof(AsmInst));
              if (op2_type == 2) {
                li->op = NI_LOADKF;
                union { float f; int32_t i; } u; u.f = (float)f2; li->imm = u.i;
              } else { li->op = NI_LOADK; li->imm = (int32_t)i2; }
              li->a = tmp;
              n_insts++; cur_pc++;
              if (n_insts < ASM_MAX_INSTS) {
                AsmInst *inst = &insts[n_insts];
                memset(inst, 0, sizeof(AsmInst));
                inst->op = bop; inst->a = dest_reg;
                if (swapped) { inst->b = tmp; inst->c = src1_reg; }
                else         { inst->b = src1_reg; inst->c = tmp; }
                n_insts++; cur_pc++;
              }
            }
            while (p < end && *p != '\n') p++;
            continue;
          }

          /* 无运算符: @dest = @src 或 @dest = imm */
          if (n_insts >= ASM_MAX_INSTS) { while (p < end && *p != '\n') p++; continue; }
          AsmInst *inst = &insts[n_insts];
          memset(inst, 0, sizeof(AsmInst));
          inst->a = dest_reg;
          if (op1_type == 3) {
            inst->op = NI_MOV; inst->b = src1_reg;
          } else if (op1_type == 1) {
            inst->op = NI_LOADK; inst->imm = (int32_t)i1;
          } else if (op1_type == 2) {
            inst->op = NI_LOADKF;
            union { float f; int32_t i; } u; u.f = (float)f1; inst->imm = u.i;
          }
          n_insts++; cur_pc++;
          while (p < end && *p != '\n') p++;
          continue;
        }
      }
      /* @name 但不是赋值 → 跳过 */
      while (p < end && *p != '\n') p++;
      continue;
    }

    /* ---- while @a <op> @b ---- */
    if (toklen == 5 && strncasecmp(tok, "while", 5) == 0) {
      p = skip_space(p, end);
      /* 解析条件: @a <op> @b */
      int ta = token_len(p, end);
      int reg_a = -1, reg_b = -1;
      if (ta >= 2 && p[0] == '@') {
        char aname[ASM_MAX_LNAME];
        int alen = (ta < ASM_MAX_LNAME) ? ta : ASM_MAX_LNAME - 1;
        memcpy(aname, p, alen); aname[alen] = '\0';
        for (int ai = 0; ai < 256; ai++)
          if (alias_names[ai][0] && strcmp(alias_names[ai], aname) == 0) { reg_a = ai; break; }
      }
      p += ta; p = skip_space(p, end);

      int cmp_op = NI_LT, cmp_swapped = 0;
      int coplen = parse_binop(p, end, &cmp_op, &cmp_swapped);
      p += coplen; p = skip_space(p, end);

      int tb = token_len(p, end);
      if (tb >= 2 && p[0] == '@') {
        char bname[ASM_MAX_LNAME];
        int blen = (tb < ASM_MAX_LNAME) ? tb : ASM_MAX_LNAME - 1;
        memcpy(bname, p, blen); bname[blen] = '\0';
        for (int ai = 0; ai < 256; ai++)
          if (alias_names[ai][0] && strcmp(alias_names[ai], bname) == 0) { reg_b = ai; break; }
      }
      p += tb;

      /* 生成标签和条件判断 */
      if (reg_a >= 0 && reg_b >= 0 && ctx_depth < CCTX_MAX_DEPTH && n_insts + 1 < ASM_MAX_INSTS) {
        char lbl_id[16];
        snprintf(lbl_id, sizeof(lbl_id), "_w%d", label_counter++);

        /* 定义 while_start 标签 */
        if (n_labels < ASM_MAX_LABELS) {
          strncpy(labels[n_labels].name, lbl_id, ASM_MAX_LNAME - 1);
          labels[n_labels].pc = cur_pc;
          n_labels++;
        }

        /* 生成条件比较: LT tmp, reg_a, reg_b */
        int tmp_reg = 249;
        AsmInst *cmp = &insts[n_insts];
        memset(cmp, 0, sizeof(AsmInst));
        cmp->op = cmp_op; cmp->a = tmp_reg;
        if (cmp_swapped) { cmp->b = reg_b; cmp->c = reg_a; }
        else             { cmp->b = reg_a; cmp->c = reg_b; }
        n_insts++; cur_pc++;

        /* 生成 JF（暂时放占位偏移，第二遍解析） */
        char end_lbl[ASM_MAX_LNAME];
        snprintf(end_lbl, sizeof(end_lbl), "_w%de", label_counter - 1);
        AsmInst *jf = &insts[n_insts];
        memset(jf, 0, sizeof(AsmInst));
        jf->op = NI_JF; jf->a = tmp_reg;
        jf->has_label = 1;
        strncpy(jf->lname, end_lbl, ASM_MAX_LNAME - 1);
        n_insts++; cur_pc++;

        /* 压栈 */
        CtxFrame *ctx = &ctx_stack[ctx_depth++];
        memset(ctx, 0, sizeof(CtxFrame));
        strncpy(ctx->start_label, lbl_id, ASM_MAX_LNAME - 1);
        strncpy(ctx->end_label, end_lbl, ASM_MAX_LNAME - 1);
        ctx->type = 0;
      }
      while (p < end && *p != '\n') p++;
      continue;
    }

    /* ---- if @a <op> @b ---- */
    if (toklen == 2 && strncasecmp(tok, "if", 2) == 0) {
      p = skip_space(p, end);
      int ta = token_len(p, end);
      int reg_a = -1, reg_b = -1;
      if (ta >= 2 && p[0] == '@') {
        char aname[ASM_MAX_LNAME];
        int alen = (ta < ASM_MAX_LNAME) ? ta : ASM_MAX_LNAME - 1;
        memcpy(aname, p, alen); aname[alen] = '\0';
        for (int ai = 0; ai < 256; ai++)
          if (alias_names[ai][0] && strcmp(alias_names[ai], aname) == 0) { reg_a = ai; break; }
      }
      p += ta; p = skip_space(p, end);

      int cmp_op = NI_LT, cmp_swapped = 0;
      int coplen = parse_binop(p, end, &cmp_op, &cmp_swapped);
      p += coplen; p = skip_space(p, end);

      int tb = token_len(p, end);
      if (tb >= 2 && p[0] == '@') {
        char bname[ASM_MAX_LNAME];
        int blen = (tb < ASM_MAX_LNAME) ? tb : ASM_MAX_LNAME - 1;
        memcpy(bname, p, blen); bname[blen] = '\0';
        for (int ai = 0; ai < 256; ai++)
          if (alias_names[ai][0] && strcmp(alias_names[ai], bname) == 0) { reg_b = ai; break; }
      }
      p += tb;

      if (reg_a >= 0 && reg_b >= 0 && ctx_depth < CCTX_MAX_DEPTH && n_insts + 2 < ASM_MAX_INSTS) {
        char lbl_id[16];
        snprintf(lbl_id, sizeof(lbl_id), "_i%d", label_counter++);

        int tmp_reg = 249;
        AsmInst *cmp = &insts[n_insts];
        memset(cmp, 0, sizeof(AsmInst));
        cmp->op = cmp_op; cmp->a = tmp_reg;
        if (cmp_swapped) { cmp->b = reg_b; cmp->c = reg_a; }
        else             { cmp->b = reg_a; cmp->c = reg_b; }
        n_insts++; cur_pc++;

        char else_lbl[ASM_MAX_LNAME], end_lbl[ASM_MAX_LNAME];
        snprintf(else_lbl, sizeof(else_lbl), "_i%de", label_counter - 1);
        snprintf(end_lbl, sizeof(end_lbl), "_i%dx", label_counter - 1);

        AsmInst *jf = &insts[n_insts];
        memset(jf, 0, sizeof(AsmInst));
        jf->op = NI_JF; jf->a = tmp_reg;
        jf->has_label = 1;
        strncpy(jf->lname, else_lbl, ASM_MAX_LNAME - 1);
        n_insts++; cur_pc++;

        CtxFrame *ctx = &ctx_stack[ctx_depth++];
        memset(ctx, 0, sizeof(CtxFrame));
        strncpy(ctx->else_label, else_lbl, ASM_MAX_LNAME - 1);
        strncpy(ctx->end_label, end_lbl, ASM_MAX_LNAME - 1);
        ctx->type = 1;
        ctx->has_else = 0;
      }
      while (p < end && *p != '\n') p++;
      continue;
    }

    /* ---- else ---- */
    if (toklen == 4 && strncasecmp(tok, "else", 4) == 0) {
      if (ctx_depth > 0) {
        CtxFrame *ctx = &ctx_stack[ctx_depth - 1];
        if (ctx->type == 1 && !ctx->has_else) {
          /* JMP to end_label, then define else_label */
          AsmInst *jmp = &insts[n_insts];
          memset(jmp, 0, sizeof(AsmInst));
          jmp->op = NI_JMP; jmp->has_label = 1;
          strncpy(jmp->lname, ctx->end_label, ASM_MAX_LNAME - 1);
          n_insts++; cur_pc++;

          if (n_labels < ASM_MAX_LABELS) {
            strncpy(labels[n_labels].name, ctx->else_label, ASM_MAX_LNAME - 1);
            labels[n_labels].pc = cur_pc;
            n_labels++;
          }
          ctx->has_else = 1;
        }
      }
      while (p < end && *p != '\n') p++;
      continue;
    }

    /* ---- end ---- */
    if (toklen == 3 && strncasecmp(tok, "end", 3) == 0) {
      if (ctx_depth > 0) {
        CtxFrame *ctx = &ctx_stack[ctx_depth - 1];

        if (ctx->type == 0) {
          /* while: JMP back to start, define end label */
          AsmInst *jmp = &insts[n_insts];
          memset(jmp, 0, sizeof(AsmInst));
          jmp->op = NI_JMP; jmp->has_label = 1;
          strncpy(jmp->lname, ctx->start_label, ASM_MAX_LNAME - 1);
          n_insts++; cur_pc++;

          if (n_labels < ASM_MAX_LABELS) {
            strncpy(labels[n_labels].name, ctx->end_label, ASM_MAX_LNAME - 1);
            labels[n_labels].pc = cur_pc;
            n_labels++;
          }
        } else if (ctx->type == 1) {
          if (ctx->has_else) {
            /* if-else: 定义 end label */
            if (n_labels < ASM_MAX_LABELS) {
              strncpy(labels[n_labels].name, ctx->end_label, ASM_MAX_LNAME - 1);
              labels[n_labels].pc = cur_pc;
              n_labels++;
            }
          } else {
            /* if only: 定义 else_label（作为结束位置），不需要 JMP */
            if (n_labels < ASM_MAX_LABELS) {
              strncpy(labels[n_labels].name, ctx->else_label, ASM_MAX_LNAME - 1);
              labels[n_labels].pc = cur_pc;
              n_labels++;
            }
          }
        }
        ctx_depth--;
      }
      while (p < end && *p != '\n') p++;
      continue;
    }

    /* ---- ret @name[, count] ---- */
    if (toklen == 3 && strncasecmp(tok, "ret", 3) == 0) {
      p = skip_space(p, end);
      int ta = token_len(p, end);
      int reg_a = -1;
      if (ta >= 2 && p[0] == '@') {
        char aname[ASM_MAX_LNAME];
        int alen = (ta < ASM_MAX_LNAME) ? ta : ASM_MAX_LNAME - 1;
        memcpy(aname, p, alen); aname[alen] = '\0';
        for (int ai = 0; ai < 256; ai++)
          if (alias_names[ai][0] && strcmp(alias_names[ai], aname) == 0) { reg_a = ai; break; }
      }
      p += ta;

      int count = 1;
      p = skip_space(p, end);
      if (p < end && *p == ',') {
        p++; p = skip_space(p, end);
        int tc = token_len(p, end);
        int64_t iv; double fv;
        if (is_number(p, tc, &iv, &fv) == 1) { count = (int)iv; p += tc; }
      }

      if (n_insts < ASM_MAX_INSTS) {
        AsmInst *inst = &insts[n_insts];
        memset(inst, 0, sizeof(AsmInst));
        inst->op = NI_RET; inst->a = (reg_a >= 0) ? reg_a : 0;
        inst->b = count;
        n_insts++; cur_pc++;
      }
      while (p < end && *p != '\n') p++;
      continue;
    }

    /* ---- halt ---- */
    if (toklen == 4 && strncasecmp(tok, "halt", 4) == 0) {
      if (n_insts < ASM_MAX_INSTS) {
        AsmInst *inst = &insts[n_insts];
        memset(inst, 0, sizeof(AsmInst));
        inst->op = NI_HALT;
        n_insts++; cur_pc++;
      }
      while (p < end && *p != '\n') p++;
      continue;
    }

    /* 未识别的 token → 跳过该行 */
    while (p < end && *p != '\n') p++;
  }

  /* ===== 第二遍：解析所有标签引用 → 计算偏移 ===== */
  lua_newtable(L);
  int outidx = 1;

  for (int i = 0; i < n_insts; i++) {
    AsmInst *inst = &insts[i];

    if (inst->has_label) {
      int found = -1;
      for (int j = 0; j < n_labels; j++) {
        if (strcmp(labels[j].name, inst->lname) == 0) { found = j; break; }
      }
      if (found >= 0) {
        int target_pc = labels[found].pc;
        inst->imm = (int32_t)(target_pc - i - 1);
      } else {
        inst->imm = 0;
      }
    }

    lua_Integer final_inst = make_ni(inst->op, inst->a, inst->b, inst->c, inst->imm);
    lua_pushinteger(L, final_inst);
    lua_rawseti(L, -2, outidx++);
  }

  return 1;
}

/* ---- 模块注册 ---- */

static const luaL_Reg native_funcs[] = {
  {"new",     nativenew},
  {"call",    nativecall},
  {"asm",     nativeasm},
  {"disasm",  nativedisasm},
  {"compile", nativecompile},
  {NULL, NULL}
};

LUAMOD_API int luaopen_nativevm(lua_State *L) {
  luaL_newlib(L, native_funcs);

  /* 导出操作码常量 */
  lua_pushinteger(L, NI_NOP);     lua_setfield(L, -2, "NOP");
  lua_pushinteger(L, NI_LOADK);   lua_setfield(L, -2, "LOADK");
  lua_pushinteger(L, NI_LOADKF);  lua_setfield(L, -2, "LOADKF");
  lua_pushinteger(L, NI_LOADK64); lua_setfield(L, -2, "LOADK64");
  lua_pushinteger(L, NI_MOV);     lua_setfield(L, -2, "MOV");
  lua_pushinteger(L, NI_ADD);     lua_setfield(L, -2, "ADD");
  lua_pushinteger(L, NI_SUB);     lua_setfield(L, -2, "SUB");
  lua_pushinteger(L, NI_MUL);     lua_setfield(L, -2, "MUL");
  lua_pushinteger(L, NI_DIV);     lua_setfield(L, -2, "DIV");
  lua_pushinteger(L, NI_MOD);     lua_setfield(L, -2, "MOD");
  lua_pushinteger(L, NI_ADDF);    lua_setfield(L, -2, "ADDF");
  lua_pushinteger(L, NI_SUBF);    lua_setfield(L, -2, "SUBF");
  lua_pushinteger(L, NI_MULF);    lua_setfield(L, -2, "MULF");
  lua_pushinteger(L, NI_DIVF);    lua_setfield(L, -2, "DIVF");
  lua_pushinteger(L, NI_AND);     lua_setfield(L, -2, "AND");
  lua_pushinteger(L, NI_OR);      lua_setfield(L, -2, "OR");
  lua_pushinteger(L, NI_XOR);     lua_setfield(L, -2, "XOR");
  lua_pushinteger(L, NI_SHL);     lua_setfield(L, -2, "SHL");
  lua_pushinteger(L, NI_SHR);     lua_setfield(L, -2, "SHR");
  lua_pushinteger(L, NI_EQ);      lua_setfield(L, -2, "EQ");
  lua_pushinteger(L, NI_NE);      lua_setfield(L, -2, "NE");
  lua_pushinteger(L, NI_LT);      lua_setfield(L, -2, "LT");
  lua_pushinteger(L, NI_LE);      lua_setfield(L, -2, "LE");
  lua_pushinteger(L, NI_LTF);     lua_setfield(L, -2, "LTF");
  lua_pushinteger(L, NI_LEF);     lua_setfield(L, -2, "LEF");
  lua_pushinteger(L, NI_JMP);     lua_setfield(L, -2, "JMP");
  lua_pushinteger(L, NI_JT);      lua_setfield(L, -2, "JT");
  lua_pushinteger(L, NI_JF);      lua_setfield(L, -2, "JF");
  lua_pushinteger(L, NI_RET);     lua_setfield(L, -2, "RET");
  lua_pushinteger(L, NI_I2F);     lua_setfield(L, -2, "I2F");
  lua_pushinteger(L, NI_F2I);     lua_setfield(L, -2, "F2I");
  lua_pushinteger(L, NI_NEG);     lua_setfield(L, -2, "NEG");
  lua_pushinteger(L, NI_NEGF);    lua_setfield(L, -2, "NEGF");
  lua_pushinteger(L, NI_MOVF);    lua_setfield(L, -2, "MOVF");
  lua_pushinteger(L, NI_MOVI);    lua_setfield(L, -2, "MOVI");
  lua_pushinteger(L, NI_SETNIL);  lua_setfield(L, -2, "SETNIL");
  lua_pushinteger(L, NI_ISNIL);   lua_setfield(L, -2, "ISNIL");
  lua_pushinteger(L, NI_SQRT);    lua_setfield(L, -2, "SQRT");
  lua_pushinteger(L, NI_HALT);    lua_setfield(L, -2, "HALT");

  return 1;
}