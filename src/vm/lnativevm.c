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
  return ((lua_Integer)(uint32_t)imm << 32)
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
 * @brief native.asm(code_str) -> inst_array
 * 汇编器：汇编助记符语法为整数指令数组
 *
 * 支持的助记符:
 *   NOP | LOADK R,imm | LOADKF R,imm | LOADK64 R,imm64
 *   MOV Ra,Rb | MOVF Ra,Rb | MOVI Ra,Rb
 *   ADD Ra,Rb,Rc | SUB | MUL | DIV | MOD
 *   ADDF Ra,Rb,Rc | SUBF | MULF | DIVF
 *   AND Ra,Rb,Rc | OR | XOR | SHL | SHR
 *   EQ Ra,Rb,Rc | NE | LT | LE | LTF | LEF
 *   NEG Ra,Rb | NEGF Ra,Rb | SQRT Ra,Rb
 *   I2F Ra,Rb | F2I Ra,Rb
 *   JMP offset | JT Ra,offset | JF Ra,offset
 *   RET Ra,count | HALT
 *   SETNIL Ra | ISNIL Ra,Rb
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

static int nativeasm(lua_State *L) {
  size_t slen;
  const char *code = luaL_checklstring(L, 1, &slen);

  lua_newtable(L);
  int outidx = 1;

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

    /* 读取助记符 */
    const char *tok_start = p;
    while (p < end && !isspace(*p) && *p != ',') p++;
    int toklen = (int)(p - tok_start);
    if (toklen == 0) continue;

    int op = find_mnem(tok_start, toklen);
    if (op < 0) { while (p < end && *p != '\n') p++; continue; }

    /* 解析参数 */
    int vals[3] = {0, 0, 0};
    int32_t imm = 0;
    int nvals = 0;
    int has_neg = 0;

    while (nvals < 3) {
      while (p < end && (*p == ' ' || *p == '\t' || *p == ',')) p++;
      if (p >= end || *p == '\n' || *p == '\r' || *p == '#' || *p == ';') break;

      if (*p == '-') { has_neg = 1; p++; }
      if (p >= end) break;

      if (isdigit(*p) || *p == '-') {
        char *ep;
        long val = strtol(p, &ep, 0);
        if (has_neg) val = -val;
        imm = (int32_t)val;
        vals[nvals] = (int)val;
        p = ep;
        nvals++;
        has_neg = 0;
      } else if ((*p == 'R' || *p == 'r') && isdigit(*(p+1))) {
        p++;
        char *ep;
        long reg = strtol(p, &ep, 10);
        if (reg < 256 && nvals < 3) vals[nvals] = (int)reg;
        p = ep;
        nvals++;
      } else {
        break;
      }
    }

    int a = (nvals > 0) ? vals[0] : 0;
    int b = (nvals > 1) ? vals[1] : 0;
    int c_val = (nvals > 2) ? vals[2] : 0;

    /* 对于单参数指令 (JMP, JT, JF)，参数是 imm */
    if (op == NI_JMP || ((op == NI_JT || op == NI_JF) && nvals <= 1)) {
      if (nvals == 0) imm = 0;
      else imm = (int32_t)vals[0];
      a = 0;
    }

    /* 对于 LOADK/LOADKF，imm 是立即数 */
    if (op == NI_LOADK || op == NI_LOADKF) {
      if (nvals >= 2) {
        a = vals[0];
        imm = (int32_t)b; /* b 里存的是解析出来的立即数 */
      }
      b = 0;
      c_val = 0;
    }

    /* 构造指令 */
    lua_Integer inst = make_ni(op, a, b, c_val, imm);
    lua_pushinteger(L, inst);
    lua_rawseti(L, -2, outidx++);

    while (p < end && *p != '\n') p++;
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


/* ---- 模块注册 ---- */

static const luaL_Reg native_funcs[] = {
  {"new",    nativenew},
  {"call",   nativecall},
  {"asm",    nativeasm},
  {"disasm", nativedisasm},
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