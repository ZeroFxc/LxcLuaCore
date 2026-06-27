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
  NI_CALL,        /* R[a] = call(R[b], R[c..c+imm-1], imm) — 统一调用 Lua/NLang 函数 */
  NI_GETFIELD,   /* R[a] = getfield(R[b], key_ref) — 成员访问 t.key */
  NI_GETTABLE,   /* R[a] = gettable(R[b], R[c]) — 索引访问 t[idx] */
  NI_SETFIELD,   /* setfield(R[a], key_ref, R[b]) — 成员赋值 t.key = val */
  NI_SETTABLE,   /* settable(R[a], R[b], R[c]) — 索引赋值 t[idx] = val */
  NI_LEN,        /* R[a] = #R[b] — 长度运算符 */
  NI_CONCAT,     /* R[a] = R[b] .. R[c] — 字符串拼接 */
  NI_POW,        /* R[a] = R[b] ^ R[c] — 幂运算 */
  NI_IDIV,       /* R[a] = R[b] // R[c] — 整除 */
  NI_BNOT,       /* R[a] = ~R[b] — 按位取反 */
  NI_FOR_IN_INIT,/* for_in_init(R[a..a+n-1], R[b..b+m-1]) — 泛型for初始化 */
  NI_FOR_IN_NEXT,/* for_in_next(R[a..a+n-1]) — 泛型for迭代 */
  NI_LOADKPTR,   /* R[a] = imm32 as NTYPE_PTR（Lua registry 引用） */
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
#define NTYPE_NIL   0
#define NTYPE_INT   1
#define NTYPE_FLOAT 2
#define NTYPE_PTR   3
#define NTYPE_FUNC  4   /* 函数引用：v.i 存储 func_id */

typedef struct {
  int type;
  union {
    int64_t i;
    double  f;
    void   *p;
  } v;
} NReg;

/* NLang 函数描述符：存储编译后的字节码 */
typedef struct {
  lua_Integer *code;   /* 函数字节码 */
  int ncode;           /* 字节码长度 */
  int nregs;           /* 函数需要的寄存器数 */
  int nparams;         /* 参数个数 */
  /* 上值（闭包捕获的外部变量） */
  int *upvalue_src;    /* 上值在父作用域中的寄存器索引 */
  int *upvalue_dst;    /* 上值在闭包中的寄存器索引 */
  int nupvalues;       /* 上值数量 */
  NReg *upvalue_data;  /* 上值持久化数据（跨调用保存） */
} NLangFunc;

/* 调用栈帧：保存执行上下文 */
typedef struct {
  int saved_pc;         /* 调用前的 PC */
  int saved_retstart;   /* 调用前的 retstart */
  int saved_retcount;   /* 调用前的 retcount */
  int func_id;          /* 被调用的函数 ID */
} CallFrame;

/* 原生 VM 状态 */
typedef struct NativeVM {
  NReg *regs;
  int   nregs;
  int   halted;
  int   retstart;  /* RET 起始寄存器 */
  int   retcount;  /* RET 返回数量 */
  /* 函数调用支持 */
  lua_State *L;              /* Lua 状态机，用于调用 Lua 函数 */
  NLangFunc *funcs;          /* NLang 函数表 */
  int nfuncs, capfuncs;      /* 函数表大小/容量 */
  CallFrame *call_stack;     /* 调用栈 */
  int call_depth, cap_call;  /* 调用深度/容量 */
  int func_returning;        /* 标记：当前正在从函数返回（跳过 HALT） */
  /* 寄存器保存栈：预分配，避免每次递归调用 malloc/free */
  NReg *reg_save_buf;        /* 寄存器保存缓冲区，按 save_count * call_depth 索引 */
  int   reg_save_cap;        /* 缓冲区容量（以 NReg 个数计） */
} NativeVM;


/* ---- 执行核心 ---- */

/**
 * @brief 将 NLang 寄存器值推入 Lua 栈
 */
static void push_reg_to_lua(NativeVM *nv, int reg) {
  lua_State *L = nv->L;
  if (reg >= nv->nregs) { lua_pushnil(L); return; }
  NReg *r = &nv->regs[reg];
  switch (r->type) {
    case NTYPE_INT:   lua_pushinteger(L, r->v.i); break;
    case NTYPE_FLOAT: lua_pushnumber(L, r->v.f); break;
    case NTYPE_NIL:   lua_pushnil(L); break;
    case NTYPE_PTR: {
      /* 从 Lua registry 取出实际值（字符串、表等对象引用） */
      int ref = (int)(intptr_t)r->v.p;
      lua_rawgeti(L, LUA_REGISTRYINDEX, ref);
      break;
    }
    default:          lua_pushnil(L); break;
  }
}

/**
 * @brief 将 Lua 栈顶值存入 NLang 寄存器
 */
static void pop_lua_to_reg(NativeVM *nv, int reg) {
  lua_State *L = nv->L;
  if (reg >= nv->nregs) { lua_pop(L, 1); return; }
  int ltype = lua_type(L, -1);
  if (ltype == LUA_TNUMBER) {
    if (lua_isinteger(L, -1)) {
      nv->regs[reg].type = NTYPE_INT;
      nv->regs[reg].v.i = lua_tointeger(L, -1);
    } else {
      nv->regs[reg].type = NTYPE_FLOAT;
      nv->regs[reg].v.f = lua_tonumber(L, -1);
    }
  } else if (ltype == LUA_TBOOLEAN) {
    nv->regs[reg].type = NTYPE_INT;
    nv->regs[reg].v.i = lua_toboolean(L, -1) ? 1 : 0;
  } else if (ltype == LUA_TNIL) {
    nv->regs[reg].type = NTYPE_NIL;
    nv->regs[reg].v.i = 0;
  } else if (ltype == LUA_TSTRING || ltype == LUA_TTABLE) {
    /* 将字符串/表存入 Lua registry，ref 存为 NTYPE_PTR */
    lua_pushvalue(L, -1);
    int ref = luaL_ref(L, LUA_REGISTRYINDEX);
    nv->regs[reg].type = NTYPE_PTR;
    nv->regs[reg].v.p = (void *)(intptr_t)ref;
  } else {
    nv->regs[reg].type = NTYPE_NIL;
    nv->regs[reg].v.i = 0;
  }
  lua_pop(L, 1);
}

/**
 * @brief 调用 NLang 函数（内部函数调用）
 * @param nv         VM 状态
 * @param func_id    函数 ID
 * @param args_start 参数起始寄存器
 * @param nargs      参数个数
 * @param ret_reg    返回值目标寄存器
 */
static void native_call_nlang(NativeVM *nv, int func_id, int args_start, int nargs, int ret_reg) {
  if (func_id < 0 || func_id >= nv->nfuncs) return;
  NLangFunc *f = &nv->funcs[func_id];
  /* 扩展寄存器数组以容纳函数体所需的寄存器 */
  if (f->nregs > nv->nregs) {
    nv->regs = (NReg *)realloc(nv->regs, sizeof(NReg) * f->nregs);
    /* 将新增的寄存器清零 */
    for (int i = nv->nregs; i < f->nregs; i++) {
      nv->regs[i].type = NTYPE_NIL;
      nv->regs[i].v.i = 0;
    }
  }
  /* 保存调用者的所有寄存器（按当前实际大小，不是函数体大小） */
  int save_count = nv->nregs;
  /* 使用预分配缓冲区，避免每次递归调用 malloc/free */
  int save_offset = nv->call_depth * save_count;
  int save_needed = save_offset + save_count;
  if (save_count > 0 && save_needed > nv->reg_save_cap) {
    /* 扩展缓冲区：保持索引对齐，每次至少翻倍 */
    int new_cap = nv->reg_save_cap ? nv->reg_save_cap * 2 : save_count * 16;
    if (new_cap < save_needed) new_cap = save_needed;
    nv->reg_save_buf = (NReg *)realloc(nv->reg_save_buf, sizeof(NReg) * new_cap);
    nv->reg_save_cap = new_cap;
  }
  NReg *saved_regs = save_count > 0 ? &nv->reg_save_buf[save_offset] : NULL;
  if (saved_regs) {
    memcpy(saved_regs, nv->regs, sizeof(NReg) * save_count);
  }
  /* 将参数复制到函数的参数寄存器（R1 开始，R0 保留给自引用） */
  for (int i = 0; i < nargs && i < f->nregs && (args_start + i) < nv->nregs; i++) {
    nv->regs[i + 1] = nv->regs[args_start + i];
  }
  /* 初始化上值：必须在设置 R0 自引用之前，因为上值可能引用 R0 */
  if (f->nupvalues > 0) {
    for (int i = 0; i < f->nupvalues; i++) {
      int src = f->upvalue_src[i];
      int dst = f->upvalue_dst[i];
      if (src < nv->nregs && dst < f->nregs) {
        nv->regs[dst] = nv->regs[src];
      }
    }
  }
  /* 设置 R0 为自引用（NTYPE_FUNC），使函数体内可以通过函数名递归调用自己 */
  if (f->nregs > 0) {
    nv->regs[0].type = NTYPE_FUNC;
    nv->regs[0].v.i = func_id;
  }
  /* 执行函数字节码 */
  int saved_ret_dst = nv->func_returning;
  nv->func_returning = ret_reg;
  nv->halted = 0;
  nv->retstart = 0;
  nv->retcount = 0;
  /* 临时切换寄存器数量为函数体的 nregs，确保指令边界检查正确 */
  int saved_nregs = nv->nregs;
  nv->nregs = f->nregs;
  {
    const lua_Integer *fcode = f->code;
    int fncode = f->ncode;
    int fpc = 0;
    while (fpc >= 0 && fpc < fncode && !nv->halted) {
      lua_Integer finst = fcode[fpc];
      int fop = NI_OP(finst);
      int fa = NI_A(finst), fb = NI_B(finst), fc = NI_C(finst);
      int32_t fimm = NI_IMM(finst);
      int fnext = fpc + 1;
      if (fa >= nv->nregs) { fpc++; continue; }
      switch (fop) {
        case NI_NOP: break;
        case NI_LOADK:  nv->regs[fa].type = NTYPE_INT; nv->regs[fa].v.i = (int64_t)fimm; break;
        case NI_LOADKF: { union { int32_t i; float f; } u; u.i = fimm; nv->regs[fa].type = NTYPE_FLOAT; nv->regs[fa].v.f = (double)u.f; } break;
        case NI_LOADK64:
          if (fpc + 1 < fncode && NI_OP(fcode[fpc + 1]) == NI_LOADKHI) {
            int64_t hi = (int64_t)NI_IMM(fcode[fpc + 1]) << 32;
            nv->regs[fa].type = NTYPE_INT;
            nv->regs[fa].v.i = hi | (int64_t)(uint32_t)fimm;
            fpc += 2; continue;
          }
          nv->regs[fa].type = NTYPE_INT; nv->regs[fa].v.i = (int64_t)fimm; break;
        case NI_LOADKHI: break;
        case NI_LOADKPTR: nv->regs[fa].type = NTYPE_PTR; nv->regs[fa].v.p = (void *)(intptr_t)fimm; break;
        case NI_MOV: if (fb < nv->nregs) nv->regs[fa] = nv->regs[fb]; break;
        case NI_ADD: if (fb < nv->nregs && fc < nv->nregs) { nv->regs[fa].type = NTYPE_INT; nv->regs[fa].v.i = nv->regs[fb].v.i + nv->regs[fc].v.i; } break;
        case NI_SUB: if (fb < nv->nregs && fc < nv->nregs) { nv->regs[fa].type = NTYPE_INT; nv->regs[fa].v.i = nv->regs[fb].v.i - nv->regs[fc].v.i; } break;
        case NI_MUL: if (fb < nv->nregs && fc < nv->nregs) { nv->regs[fa].type = NTYPE_INT; nv->regs[fa].v.i = nv->regs[fb].v.i * nv->regs[fc].v.i; } break;
        case NI_DIV: if (fb < nv->nregs && fc < nv->nregs) { int64_t dv = nv->regs[fc].v.i; if (dv) { nv->regs[fa].type = NTYPE_INT; nv->regs[fa].v.i = nv->regs[fb].v.i / dv; } } break;
        case NI_MOD: if (fb < nv->nregs && fc < nv->nregs) { int64_t dv = nv->regs[fc].v.i; if (dv) { nv->regs[fa].type = NTYPE_INT; nv->regs[fa].v.i = nv->regs[fb].v.i % dv; } } break;
        case NI_ADDF: if (fb < nv->nregs && fc < nv->nregs) { nv->regs[fa].type = NTYPE_FLOAT; nv->regs[fa].v.f = nv->regs[fb].v.f + nv->regs[fc].v.f; } break;
        case NI_SUBF: if (fb < nv->nregs && fc < nv->nregs) { nv->regs[fa].type = NTYPE_FLOAT; nv->regs[fa].v.f = nv->regs[fb].v.f - nv->regs[fc].v.f; } break;
        case NI_MULF: if (fb < nv->nregs && fc < nv->nregs) { nv->regs[fa].type = NTYPE_FLOAT; nv->regs[fa].v.f = nv->regs[fb].v.f * nv->regs[fc].v.f; } break;
        case NI_DIVF: if (fb < nv->nregs && fc < nv->nregs) { double dv = nv->regs[fc].v.f; if (dv != 0.0) { nv->regs[fa].type = NTYPE_FLOAT; nv->regs[fa].v.f = nv->regs[fb].v.f / dv; } } break;
        case NI_AND: if (fb < nv->nregs && fc < nv->nregs) { nv->regs[fa].type = NTYPE_INT; nv->regs[fa].v.i = nv->regs[fb].v.i & nv->regs[fc].v.i; } break;
        case NI_OR:  if (fb < nv->nregs && fc < nv->nregs) { nv->regs[fa].type = NTYPE_INT; nv->regs[fa].v.i = nv->regs[fb].v.i | nv->regs[fc].v.i; } break;
        case NI_XOR: if (fb < nv->nregs && fc < nv->nregs) { nv->regs[fa].type = NTYPE_INT; nv->regs[fa].v.i = nv->regs[fb].v.i ^ nv->regs[fc].v.i; } break;
        case NI_SHL: if (fb < nv->nregs && fc < nv->nregs) { nv->regs[fa].type = NTYPE_INT; nv->regs[fa].v.i = nv->regs[fb].v.i << (int)(nv->regs[fc].v.i & 63); } break;
        case NI_SHR: if (fb < nv->nregs && fc < nv->nregs) { nv->regs[fa].type = NTYPE_INT; nv->regs[fa].v.i = nv->regs[fb].v.i >> (int)(nv->regs[fc].v.i & 63); } break;
        case NI_EQ: if (fb < nv->nregs && fc < nv->nregs) { NReg *rb = &nv->regs[fb], *rc = &nv->regs[fc]; nv->regs[fa].type = NTYPE_INT; if (rb->type == NTYPE_FLOAT && rc->type == NTYPE_FLOAT) nv->regs[fa].v.i = (rb->v.f == rc->v.f) ? 1 : 0; else nv->regs[fa].v.i = (rb->v.i == rc->v.i) ? 1 : 0; } break;
        case NI_NE: if (fb < nv->nregs && fc < nv->nregs) { NReg *rb = &nv->regs[fb], *rc = &nv->regs[fc]; nv->regs[fa].type = NTYPE_INT; if (rb->type == NTYPE_FLOAT && rc->type == NTYPE_FLOAT) nv->regs[fa].v.i = (rb->v.f != rc->v.f) ? 1 : 0; else nv->regs[fa].v.i = (rb->v.i != rc->v.i) ? 1 : 0; } break;
        case NI_LT:  if (fb < nv->nregs && fc < nv->nregs) { nv->regs[fa].type = NTYPE_INT; nv->regs[fa].v.i = (nv->regs[fb].v.i < nv->regs[fc].v.i) ? 1 : 0; } break;
        case NI_LE:  if (fb < nv->nregs && fc < nv->nregs) { nv->regs[fa].type = NTYPE_INT; nv->regs[fa].v.i = (nv->regs[fb].v.i <= nv->regs[fc].v.i) ? 1 : 0; } break;
        case NI_LTF: if (fb < nv->nregs && fc < nv->nregs) { nv->regs[fa].type = NTYPE_INT; nv->regs[fa].v.i = (nv->regs[fb].v.f < nv->regs[fc].v.f) ? 1 : 0; } break;
        case NI_LEF: if (fb < nv->nregs && fc < nv->nregs) { nv->regs[fa].type = NTYPE_INT; nv->regs[fa].v.i = (nv->regs[fb].v.f <= nv->regs[fc].v.f) ? 1 : 0; } break;
        case NI_JMP: fnext = fpc + 1 + (int)fimm; break;
        case NI_JT:  if (fa < nv->nregs && nv->regs[fa].v.i != 0) fnext = fpc + 1 + (int)fimm; break;
        case NI_JF:  if (fa < nv->nregs && nv->regs[fa].v.i == 0) fnext = fpc + 1 + (int)fimm; break;
        case NI_RET: {
          int ret_cnt = fb; if (ret_cnt <= 0) ret_cnt = 1;
          int rdst = nv->func_returning;
          /* 始终写入返回值到 rdst，即使 rdst 超出当前函数体的 nregs
           * （因为 nv->regs 数组实际大小是调用者的 nregs，这里只是临时限制了边界） */
          if (rdst >= 0) {
            for (int ri = 0; ri < ret_cnt && (fa + ri) < nv->nregs; ri++) {
              nv->regs[rdst + ri] = nv->regs[fa + ri];
            }
          }
          /* 设置返回值位置，供 native_call_nlang 读取 */
          nv->retstart = rdst >= 0 ? rdst : fa;
          nv->retcount = ret_cnt;
          nv->halted = 1;
          break;
        }
        case NI_HALT: nv->halted = 1; break;
        case NI_I2F: if (fb < nv->nregs) { nv->regs[fa].type = NTYPE_FLOAT; nv->regs[fa].v.f = (double)nv->regs[fb].v.i; } break;
        case NI_F2I: if (fb < nv->nregs) { nv->regs[fa].type = NTYPE_INT; nv->regs[fa].v.i = (int64_t)nv->regs[fb].v.f; } break;
        case NI_NEG: if (fb < nv->nregs) { nv->regs[fa].type = NTYPE_INT; nv->regs[fa].v.i = -nv->regs[fb].v.i; } break;
        case NI_NEGF: if (fb < nv->nregs) { nv->regs[fa].type = NTYPE_FLOAT; nv->regs[fa].v.f = -nv->regs[fb].v.f; } break;
        case NI_MOVF: if (fb < nv->nregs) { nv->regs[fa].type = NTYPE_FLOAT; nv->regs[fa].v.f = (nv->regs[fb].type == NTYPE_INT) ? (double)nv->regs[fb].v.i : nv->regs[fb].v.f; } break;
        case NI_MOVI: if (fb < nv->nregs) { nv->regs[fa].type = NTYPE_INT; nv->regs[fa].v.i = (nv->regs[fb].type == NTYPE_FLOAT) ? (int64_t)nv->regs[fb].v.f : nv->regs[fb].v.i; } break;
        case NI_SETNIL: nv->regs[fa].type = NTYPE_NIL; nv->regs[fa].v.i = 0; break;
        case NI_ISNIL: if (fb < nv->nregs) { nv->regs[fa].type = NTYPE_INT; nv->regs[fa].v.i = (nv->regs[fb].type == NTYPE_NIL) ? 1 : 0; } break;
        case NI_SQRT: if (fb < nv->nregs) { nv->regs[fa].type = NTYPE_FLOAT; double val = (nv->regs[fb].type == NTYPE_INT) ? (double)nv->regs[fb].v.i : nv->regs[fb].v.f; nv->regs[fa].v.f = (val >= 0.0) ? sqrt(val) : 0.0; } break;
        case NI_GETFIELD:
          if (fb < nv->nregs && nv->L) { lua_State *L = nv->L; int kref = (int)fimm; push_reg_to_lua(nv, fb); lua_rawgeti(L, LUA_REGISTRYINDEX, kref); lua_gettable(L, -2); pop_lua_to_reg(nv, fa); lua_pop(L, 1); }
          break;
        case NI_GETTABLE:
          if (fb < nv->nregs && fc < nv->nregs && nv->L) { lua_State *L = nv->L; push_reg_to_lua(nv, fb); push_reg_to_lua(nv, fc); lua_gettable(L, -2); pop_lua_to_reg(nv, fa); lua_pop(L, 1); }
          break;
        case NI_SETFIELD:
          if (fa < nv->nregs && fb < nv->nregs && nv->L) { lua_State *L = nv->L; int kref = (int)fimm; push_reg_to_lua(nv, fa); lua_rawgeti(L, LUA_REGISTRYINDEX, kref); push_reg_to_lua(nv, fb); lua_settable(L, -3); lua_pop(L, 1); }
          break;
        case NI_SETTABLE:
          if (fa < nv->nregs && fb < nv->nregs && fc < nv->nregs && nv->L) { lua_State *L = nv->L; push_reg_to_lua(nv, fa); push_reg_to_lua(nv, fb); push_reg_to_lua(nv, fc); lua_settable(L, -3); lua_pop(L, 1); }
          break;
        case NI_LEN:
          if (fb < nv->nregs && nv->L) { lua_State *L = nv->L; push_reg_to_lua(nv, fb); lua_len(L, -1); pop_lua_to_reg(nv, fa); lua_pop(L, 1); }
          break;
        case NI_CONCAT:
          if (fb < nv->nregs && fc < nv->nregs && nv->L) { lua_State *L = nv->L; push_reg_to_lua(nv, fb); push_reg_to_lua(nv, fc); lua_concat(L, 2); pop_lua_to_reg(nv, fa); }
          break;
        case NI_POW:
          if (fb < nv->nregs && fc < nv->nregs && nv->L) { lua_State *L = nv->L; push_reg_to_lua(nv, fb); push_reg_to_lua(nv, fc); lua_getglobal(L, "math"); if (lua_istable(L, -1)) { lua_getfield(L, -1, "pow"); lua_pushvalue(L, -4); lua_pushvalue(L, -4); lua_call(L, 2, 1); } pop_lua_to_reg(nv, fa); lua_pop(L, 2); }
          break;
        case NI_IDIV:
          if (fb < nv->nregs && fc < nv->nregs) { int64_t dv = nv->regs[fc].v.i; if (dv) { int64_t lh = nv->regs[fb].v.i; int64_t q = lh / dv; if (lh % dv != 0 && ((lh ^ dv) < 0)) q--; nv->regs[fa].type = NTYPE_INT; nv->regs[fa].v.i = q; } }
          break;
        case NI_BNOT:
          if (fb < nv->nregs) { nv->regs[fa].type = NTYPE_INT; nv->regs[fa].v.i = ~nv->regs[fb].v.i; }
          break;
        case NI_FOR_IN_INIT:
          if (fa < nv->nregs) { for (int i = 0; i < (int)fimm && (fa + i) < nv->nregs; i++) { nv->regs[fa + i].type = NTYPE_NIL; nv->regs[fa + i].v.i = 0; } }
          break;
        case NI_FOR_IN_NEXT:
          if (fa < nv->nregs && nv->L) { lua_State *L = nv->L; if (nv->regs[fa].type == NTYPE_PTR) { int lref = (int)(intptr_t)nv->regs[fa].v.p; lua_rawgeti(L, LUA_REGISTRYINDEX, lref); if (lua_isfunction(L, -1)) { push_reg_to_lua(nv, fa + 1); push_reg_to_lua(nv, fa + 2); if (lua_pcall(L, 2, LUA_MULTRET, 0) == LUA_OK) { int nret = lua_gettop(L); if (nret == 0 || lua_isnil(L, 1)) { nv->regs[fa].type = NTYPE_NIL; nv->regs[fa].v.i = 0; } else { for (int i = 0; i < (int)fimm && i < nret && (fa + i) < nv->nregs; i++) { lua_pushvalue(L, i + 1); pop_lua_to_reg(nv, fa + i); } } lua_settop(L, 0); } } else { lua_pop(L, 1); nv->regs[fa].type = NTYPE_NIL; nv->regs[fa].v.i = 0; } } }
          break;
        case NI_CALL:
          /* 递归调用：R[fa] = call(R[fb], R[fc..fc+imm-1], imm) */
          if (fb < nv->nregs) {
            if (nv->regs[fb].type == NTYPE_FUNC ||
                (nv->regs[fb].type == NTYPE_INT && nv->regs[fb].v.i >= 0 && nv->regs[fb].v.i < nv->nfuncs)) {
              /* NLang 函数递归调用 */
              int nested_func_id = (int)nv->regs[fb].v.i;
              int nested_nargs = (int)fimm;
              if (nested_nargs <= 0) nested_nargs = 0;
              /* 保存当前执行位置到调用栈 */
              if (nv->call_depth >= nv->cap_call) {
                nv->cap_call = nv->cap_call ? nv->cap_call * 2 : 16;
                nv->call_stack = (CallFrame *)realloc(nv->call_stack, sizeof(CallFrame) * nv->cap_call);
              }
              CallFrame *frame = &nv->call_stack[nv->call_depth++];
              frame->saved_pc = 0;  /* 由 native_call_nlang 内部管理 */
              frame->saved_retstart = nv->retstart;
              frame->saved_retcount = nv->retcount;
              frame->func_id = nested_func_id;
              native_call_nlang(nv, nested_func_id, fc, nested_nargs, fa);
              nv->call_depth--;
              nv->halted = 0;  /* 递归调用返回后清除 halted，继续执行父函数 */
            } else if (nv->regs[fb].type == NTYPE_PTR && nv->L) {
              /* Lua 函数递归调用 */
              lua_State *L = nv->L;
              int lua_ref = (int)(intptr_t)nv->regs[fb].v.p;
              int nargs = (int)fimm;
              if (nargs < 0) nargs = 0;
              lua_rawgeti(L, LUA_REGISTRYINDEX, lua_ref);
              if (lua_isfunction(L, -1)) {
                for (int i = 0; i < nargs && (fc + i) < nv->nregs; i++) {
                  push_reg_to_lua(nv, fc + i);
                }
                if (lua_pcall(L, nargs, 1, 0) == LUA_OK) {
                  pop_lua_to_reg(nv, fa);
                } else {
                  lua_pop(L, 1);
                  nv->regs[fa].type = NTYPE_NIL;
                  nv->regs[fa].v.i = 0;
                }
              } else {
                lua_pop(L, 1);
                nv->regs[fa].type = NTYPE_NIL;
                nv->regs[fa].v.i = 0;
              }
            } else {
              nv->regs[fa].type = NTYPE_NIL;
              nv->regs[fa].v.i = 0;
            }
          }
          break;
        default: break;
      }
      fpc = fnext;
    }
  }
  nv->nregs = saved_nregs;
  nv->func_returning = saved_ret_dst;
  /* 保存返回值（在恢复寄存器之前，因为返回值在函数体寄存器范围内） */
  int ret_cnt = nv->retcount;
  if (ret_cnt <= 0) ret_cnt = 1;
  NReg saved_ret[8];  /* 栈缓冲区，最多 8 个返回值 */
  int has_ret = 0;
  if (saved_regs && nv->retstart >= 0 && nv->retstart < save_count) {
    for (int i = 0; i < ret_cnt && i < 8 && (nv->retstart + i) < save_count; i++) {
      saved_ret[i] = nv->regs[nv->retstart + i];
    }
    has_ret = 1;
  }
  /* 保存上值修改（在恢复寄存器之前，闭包的上值寄存器在 R[0..f->nregs-1] 内） */
  NReg saved_upvalues[16];  /* 栈缓冲区，最多 16 个上值 */
  int n_saved_uv = 0;
  if (f->nupvalues > 0 && f->nupvalues <= 16) {
    n_saved_uv = f->nupvalues;
    for (int i = 0; i < f->nupvalues; i++) {
      int dst = f->upvalue_dst[i];
      if (dst < f->nregs) {
        saved_upvalues[i] = nv->regs[dst];
      }
    }
  }
  /* 恢复调用者的所有寄存器（预分配缓冲区无需 free） */
  if (saved_regs) {
    memcpy(nv->regs, saved_regs, sizeof(NReg) * save_count);
  }
  /* 将返回值写回 */
  if (has_ret) {
    for (int i = 0; i < ret_cnt && i < 8 && (nv->retstart + i) < save_count; i++) {
      nv->regs[nv->retstart + i] = saved_ret[i];
    }
  }
  /* 上值写回：将闭包修改后的上值写回父作用域寄存器 */
  if (n_saved_uv > 0) {
    for (int i = 0; i < n_saved_uv; i++) {
      int src = f->upvalue_src[i];
      if (src < nv->nregs) {
        nv->regs[src] = saved_upvalues[i];
      }
      /* 同时更新持久化存储 */
      f->upvalue_data[i] = saved_upvalues[i];
    }
  }
}

static void native_exec(NativeVM *nv, const lua_Integer *code, int ncode) {
  int pc = 0;

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

    case NI_LOADKPTR:
      nv->regs[a].type = NTYPE_PTR;
      nv->regs[a].v.p = (void *)(intptr_t)imm;
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

    case NI_CALL:
      /* 统一函数调用: R[a] = call(R[b], R[c..c+imm-1], imm)
       * R[b] 是函数引用: NTYPE_FUNC → NLang 函数, NTYPE_PTR → Lua 函数 */
      if (b < nv->nregs) {
        if (nv->regs[b].type == NTYPE_FUNC) {
          /* NLang 函数调用 */
          int func_id = (int)nv->regs[b].v.i;
          int nargs = (int)imm;
          if (nargs <= 0) nargs = 0;
          /* 保存当前执行上下文 */
          if (nv->call_depth >= nv->cap_call) {
            nv->cap_call = nv->cap_call ? nv->cap_call * 2 : 16;
            nv->call_stack = (CallFrame *)realloc(nv->call_stack, sizeof(CallFrame) * nv->cap_call);
          }
          CallFrame *frame = &nv->call_stack[nv->call_depth++];
          frame->saved_pc = pc + 1;
          frame->func_id = func_id;
          native_call_nlang(nv, func_id, c, nargs, a);
          /* 恢复调用者上下文 */
          nv->call_depth--;
          nv->halted = 0;  /* 函数返回后清除 halted，继续执行后续指令 */
          next = frame->saved_pc;
        } else if (nv->regs[b].type == NTYPE_INT && nv->regs[b].v.i >= 0 && nv->regs[b].v.i < nv->nfuncs) {
          /* 整数 func_id（编译器回填的占位符）→ NLang 函数调用 */
          int func_id = (int)nv->regs[b].v.i;
          int nargs = (int)imm;
          if (nargs <= 0) nargs = 0;
          if (nv->call_depth >= nv->cap_call) {
            nv->cap_call = nv->cap_call ? nv->cap_call * 2 : 16;
            nv->call_stack = (CallFrame *)realloc(nv->call_stack, sizeof(CallFrame) * nv->cap_call);
          }
          CallFrame *frame = &nv->call_stack[nv->call_depth++];
          frame->saved_pc = pc + 1;
          frame->func_id = func_id;
          native_call_nlang(nv, func_id, c, nargs, a);
          nv->call_depth--;
          nv->halted = 0;  /* 函数返回后清除 halted，继续执行后续指令 */
          next = frame->saved_pc;
        } else if (nv->regs[b].type == NTYPE_PTR && nv->L) {
          /* Lua 函数调用: R[b].v.p 存储 registry 引用索引 */
          lua_State *L = nv->L;
          int lua_ref = (int)(intptr_t)nv->regs[b].v.p;
          int nargs = (int)imm;
          if (nargs < 0) nargs = 0;
          /* 从 registry 取出函数 */
          lua_rawgeti(L, LUA_REGISTRYINDEX, lua_ref);
          if (!lua_isfunction(L, -1)) {
            lua_pop(L, 1);
            nv->regs[a].type = NTYPE_NIL;
            nv->regs[a].v.i = 0;
            break;
          }
          /* 推送参数 */
          for (int i = 0; i < nargs && (c + i) < nv->nregs; i++) {
            push_reg_to_lua(nv, c + i);
          }
          /* 调用 Lua 函数 */
          int call_result = lua_pcall(L, nargs, 1, 0);
          if (call_result == LUA_OK) {
            pop_lua_to_reg(nv, a);
          } else {
            /* 出错: 弹出错误消息 */
            lua_pop(L, 1);
            nv->regs[a].type = NTYPE_NIL;
            nv->regs[a].v.i = 0;
          }
        } else {
          nv->regs[a].type = NTYPE_NIL;
          nv->regs[a].v.i = 0;
        }
      }
      break;

    case NI_GETFIELD:
      /* R[a] = getfield(R[b], key_ref): 通过 lua_State 获取字段 */
      if (b < nv->nregs && nv->L) {
        lua_State *L = nv->L;
        int key_ref = (int)imm;
        push_reg_to_lua(nv, b);
        lua_rawgeti(L, LUA_REGISTRYINDEX, key_ref);
        lua_gettable(L, -2);
        pop_lua_to_reg(nv, a);
        lua_pop(L, 1);  /* 弹出表 */
      }
      break;

    case NI_GETTABLE:
      /* R[a] = gettable(R[b], R[c]): 通过 lua_State 索引访问 */
      if (b < nv->nregs && c < nv->nregs && nv->L) {
        lua_State *L = nv->L;
        push_reg_to_lua(nv, b);
        push_reg_to_lua(nv, c);
        lua_gettable(L, -2);
        pop_lua_to_reg(nv, a);
        lua_pop(L, 1);  /* 弹出表 */
      }
      break;

    case NI_SETFIELD:
      /* setfield(R[a], key_ref, R[b]): 成员赋值 t.key = val */
      if (a < nv->nregs && b < nv->nregs && nv->L) {
        lua_State *L = nv->L;
        int key_ref = (int)imm;
        push_reg_to_lua(nv, a);
        lua_rawgeti(L, LUA_REGISTRYINDEX, key_ref);
        push_reg_to_lua(nv, b);
        lua_settable(L, -3);
        lua_pop(L, 1);  /* 弹出表 */
      }
      break;

    case NI_SETTABLE:
      /* settable(R[a], R[b], R[c]): 索引赋值 t[idx] = val */
      if (a < nv->nregs && b < nv->nregs && c < nv->nregs && nv->L) {
        lua_State *L = nv->L;
        push_reg_to_lua(nv, a);
        push_reg_to_lua(nv, b);
        push_reg_to_lua(nv, c);
        lua_settable(L, -3);
        lua_pop(L, 1);  /* 弹出表 */
      }
      break;

    case NI_LEN:
      /* R[a] = #R[b]: 长度运算符 */
      if (b < nv->nregs && nv->L) {
        lua_State *L = nv->L;
        push_reg_to_lua(nv, b);
        lua_len(L, -1);
        pop_lua_to_reg(nv, a);
        lua_pop(L, 1);
      }
      break;

    case NI_CONCAT:
      /* R[a] = R[b] .. R[c]: 字符串拼接 */
      if (b < nv->nregs && c < nv->nregs && nv->L) {
        lua_State *L = nv->L;
        push_reg_to_lua(nv, b);
        push_reg_to_lua(nv, c);
        lua_concat(L, 2);
        pop_lua_to_reg(nv, a);
      }
      break;

    case NI_POW:
      /* R[a] = R[b] ^ R[c]: 幂运算 */
      if (b < nv->nregs && c < nv->nregs && nv->L) {
        lua_State *L = nv->L;
        push_reg_to_lua(nv, b);
        push_reg_to_lua(nv, c);
        /* 调用 math.pow */
        lua_getglobal(L, "math");
        if (lua_istable(L, -1)) {
          lua_getfield(L, -1, "pow");
          lua_pushvalue(L, -4);
          lua_pushvalue(L, -4);
          lua_call(L, 2, 1);
        }
        pop_lua_to_reg(nv, a);
        lua_pop(L, 2);  /* 弹出 math 表和多余值 */
      }
      break;

    case NI_IDIV:
      /* R[a] = R[b] // R[c]: 整除 */
      if (b < nv->nregs && c < nv->nregs) {
        int64_t divisor = nv->regs[c].v.i;
        if (divisor != 0) {
          /* Lua 整除：向负无穷取整 */
          int64_t lhs = nv->regs[b].v.i;
          int64_t q = lhs / divisor;
          if (lhs % divisor != 0 && ((lhs ^ divisor) < 0)) q--;
          nv->regs[a].type = NTYPE_INT;
          nv->regs[a].v.i = q;
        }
      }
      break;

    case NI_BNOT:
      /* R[a] = ~R[b]: 按位取反 */
      if (b < nv->nregs) {
        nv->regs[a].type = NTYPE_INT;
        nv->regs[a].v.i = ~nv->regs[b].v.i;
      }
      break;

    case NI_FOR_IN_INIT:
      /* for_in_init(R[a..a+n-1], R[b..b+m-1]): 泛型for初始化
       * 将循环变量寄存器初始化为 nil，实际迭代由 FOR_IN_NEXT 驱动 */
      {
        int nvars = ((int)imm >> 16) & 0xFF;
        for (int i = 0; i < nvars && (a + i) < nv->nregs; i++) {
          nv->regs[a + i].type = NTYPE_NIL;
          nv->regs[a + i].v.i = 0;
        }
      }
      break;

    case NI_FOR_IN_NEXT:
      /* for_in_next(R[a..a+n-1]): 泛型for迭代，从 R[a] 取迭代器函数，下一值存入 R[a..a+n-1]
       * imm: nvars */
      if (a < nv->nregs && nv->L) {
        int nvars = (int)imm;
        lua_State *L = nv->L;
        /* 调用迭代器函数：R[a] 是函数引用，R[a+1] 是状态，R[a+2] 是初始值 */
        if (nv->regs[a].type == NTYPE_PTR) {
          int lua_ref = (int)(intptr_t)nv->regs[a].v.p;
          lua_rawgeti(L, LUA_REGISTRYINDEX, lua_ref);
          if (lua_isfunction(L, -1)) {
            push_reg_to_lua(nv, a + 1);
            push_reg_to_lua(nv, a + 2);
            if (lua_pcall(L, 2, LUA_MULTRET, 0) == LUA_OK) {
              int nret = lua_gettop(L);
              /* 第一个返回值 == nil 表示循环结束 */
              if (nret == 0 || lua_isnil(L, 1)) {
                nv->regs[a].type = NTYPE_NIL;
                nv->regs[a].v.i = 0;
              } else {
                for (int i = 0; i < nvars && i < nret && (a + i) < nv->nregs; i++) {
                  lua_pushvalue(L, i + 1);
                  pop_lua_to_reg(nv, a + i);
                }
              }
              lua_settop(L, 0);
            }
          } else {
            lua_pop(L, 1);
            nv->regs[a].type = NTYPE_NIL;
            nv->regs[a].v.i = 0;
          }
        }
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
  if (w) {
    if (w->nv.regs) { free(w->nv.regs); w->nv.regs = NULL; }
    if (w->code) { free(w->code); w->code = NULL; }
    /* 释放函数表 */
    if (w->nv.funcs) {
      for (int i = 0; i < w->nv.nfuncs; i++) {
        if (w->nv.funcs[i].code) free(w->nv.funcs[i].code);
        if (w->nv.funcs[i].upvalue_src) free(w->nv.funcs[i].upvalue_src);
        if (w->nv.funcs[i].upvalue_dst) free(w->nv.funcs[i].upvalue_dst);
        if (w->nv.funcs[i].upvalue_data) free(w->nv.funcs[i].upvalue_data);
      }
      free(w->nv.funcs);
      w->nv.funcs = NULL;
    }
    if (w->nv.call_stack) { free(w->nv.call_stack); w->nv.call_stack = NULL; }
    if (w->nv.reg_save_buf) { free(w->nv.reg_save_buf); w->nv.reg_save_buf = NULL; }
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

  nv->L = L;  /* 设置 Lua 状态机，供 NI_CALL 调用 Lua 函数 */
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
    push_reg_to_lua(nv, i);
  }
  return nret;
}

/**
 * @brief native.deffunc(nv, inst_array, nregs, nparams, upvalues) -> func_id
 * 定义一个 NLang 函数，将其字节码注册到 VM 的函数表中
 * 返回函数 ID，用于后续 NI_CALL 调用
 * upvalues: 可选，上值表，每项 {src=parent_reg, dst=closure_reg}
 */
static int nativedeffunc(lua_State *L) {
  NativeVMWrapper *w = (NativeVMWrapper *)luaL_checkudata(L, 1, "nativevm_meta");
  NativeVM *nv = &w->nv;
  luaL_checktype(L, 2, LUA_TTABLE);
  int nregs = (int)luaL_optinteger(L, 3, 16);
  int nparams = (int)luaL_optinteger(L, 4, 0);
  int ncode = (int)luaL_len(L, 2);

  /* 扩展函数表 */
  if (nv->nfuncs >= nv->capfuncs) {
    nv->capfuncs = nv->capfuncs ? nv->capfuncs * 2 : 8;
    nv->funcs = (NLangFunc *)realloc(nv->funcs, sizeof(NLangFunc) * nv->capfuncs);
  }
  int func_id = nv->nfuncs++;
  NLangFunc *f = &nv->funcs[func_id];
  memset(f, 0, sizeof(NLangFunc));

  f->ncode = ncode;
  f->nregs = nregs;
  f->nparams = nparams;
  /* 如果函数体需要的寄存器数超过当前 regs 数组大小，扩展 regs 数组 */
  if (nregs > nv->nregs) {
    nv->regs = (NReg *)realloc(nv->regs, sizeof(NReg) * nregs);
    for (int i = nv->nregs; i < nregs; i++) {
      nv->regs[i].type = NTYPE_NIL;
      nv->regs[i].v.i = 0;
    }
    nv->nregs = nregs;
  }
  f->code = (lua_Integer *)malloc(sizeof(lua_Integer) * ncode);
  for (int i = 0; i < ncode; i++) {
    lua_rawgeti(L, 2, i + 1);
    f->code[i] = lua_tointeger(L, -1);
    lua_pop(L, 1);
  }

  /* 解析上值信息（第5个参数，可选） */
  if (lua_gettop(L) >= 5 && lua_istable(L, 5)) {
    f->nupvalues = (int)luaL_len(L, 5);
    if (f->nupvalues > 0) {
      f->upvalue_src = (int *)malloc(sizeof(int) * f->nupvalues);
      f->upvalue_dst = (int *)malloc(sizeof(int) * f->nupvalues);
      f->upvalue_data = (NReg *)malloc(sizeof(NReg) * f->nupvalues);
      memset(f->upvalue_data, 0, sizeof(NReg) * f->nupvalues);
      for (int i = 0; i < f->nupvalues; i++) {
        lua_rawgeti(L, 5, i + 1);
        if (lua_istable(L, -1)) {
          lua_getfield(L, -1, "src");
          f->upvalue_src[i] = (int)lua_tointeger(L, -1);
          lua_pop(L, 1);
          lua_getfield(L, -1, "dst");
          f->upvalue_dst[i] = (int)lua_tointeger(L, -1);
          lua_pop(L, 1);
        }
        lua_pop(L, 1);
      }
    }
  }

  lua_pushinteger(L, func_id);
  return 1;
}

/**
 * @brief native.loadfunc(nv, func_id) -> 将函数引用存入寄存器
 * 返回用于 NI_CALL 的函数引用值（NTYPE_FUNC）
 */
static int nativeloadfunc(lua_State *L) {
  luaL_checktype(L, 1, LUA_TUSERDATA);
  int func_id = (int)luaL_checkinteger(L, 2);
  /* 返回函数引用：{type=NTYPE_FUNC, value=func_id} */
  lua_newtable(L);
  lua_pushinteger(L, NTYPE_FUNC);
  lua_setfield(L, -2, "type");
  lua_pushinteger(L, func_id);
  lua_setfield(L, -2, "value");
  return 1;
}

/**
 * @brief native.luafunc(nv, lua_func) -> 将 Lua 函数注册到 registry 并返回引用
 * 返回用于 NI_CALL 的函数引用值（NTYPE_PTR）
 */
static int nativeluafunc(lua_State *L) {
  luaL_checktype(L, 1, LUA_TUSERDATA);
  luaL_checktype(L, 2, LUA_TFUNCTION);
  /* 将函数存入 registry */
  lua_pushvalue(L, 2);
  int ref = luaL_ref(L, LUA_REGISTRYINDEX);
  /* 返回指针引用 */
  lua_newtable(L);
  lua_pushinteger(L, NTYPE_PTR);
  lua_setfield(L, -2, "type");
  lua_pushinteger(L, ref);
  lua_setfield(L, -2, "ref");
  return 1;
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
            int tmp = nregs_hint > 0 ? nregs_hint - 1 : 250; /* 最后一个寄存器作为临时寄存器 */
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
            int tmp = nregs_hint > 0 ? nregs_hint - 1 : 250;
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
    "SETNIL","ISNIL","SQRT","HALT","CALL",
    "GETFIELD","GETTABLE","SETFIELD","SETTABLE",
    "LEN","CONCAT","POW","IDIV","BNOT",
    "FOR_IN_INIT","FOR_IN_NEXT"
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
  {"deffunc", nativedeffunc},
  {"loadfunc", nativeloadfunc},
  {"luafunc", nativeluafunc},
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
  lua_pushinteger(L, NI_CALL);    lua_setfield(L, -2, "CALL");

  /* 导出类型常量 */
  lua_pushinteger(L, NTYPE_NIL);   lua_setfield(L, -2, "NTYPE_NIL");
  lua_pushinteger(L, NTYPE_INT);   lua_setfield(L, -2, "NTYPE_INT");
  lua_pushinteger(L, NTYPE_FLOAT); lua_setfield(L, -2, "NTYPE_FLOAT");
  lua_pushinteger(L, NTYPE_PTR);   lua_setfield(L, -2, "NTYPE_PTR");
  lua_pushinteger(L, NTYPE_FUNC);  lua_setfield(L, -2, "NTYPE_FUNC");

  return 1;
}