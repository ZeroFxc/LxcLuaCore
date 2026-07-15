/*
** $Id: lasm.h $
** LXCLUA 内联汇编器 - 共享头文件
** 从 lparser.c 的 asm 实现中提取，供 lparser.c 和 lcodegen.c 共用
** See Copyright Notice in lua.h
*/

#ifndef lasm_h
#define lasm_h

#include "llex.h"
#include "lcode.h"
#include "lopcodes.h"
#include "lstring.h"


/* 初始容量 */
#define ASM_INIT_LABELS   8
#define ASM_INIT_PENDING  8
#define ASM_INIT_DEFINES 16

/* 标签定义 */
typedef struct AsmLabel {
  TString *name;
  int pc;
  int line;
} AsmLabel;

/* 待修补的跳转引用 */
typedef struct AsmPending {
  TString *label;
  int pc;        /* 跳转指令的 PC */
  int line;
  int is_jump;   /* 1=跳转指令（JMP/FORLOOP等），0=普通引用 */
} AsmPending;

/* 常量定义 */
typedef struct AsmDefine {
  TString *name;
  lua_Integer value;
} AsmDefine;

/* 汇编上下文 */
typedef struct AsmContext {
  AsmLabel *labels;
  int nlabels;
  int labels_cap;
  AsmPending *pending;
  int npending;
  int pending_cap;
  AsmDefine *defines;
  int ndefines;
  int defines_cap;
  struct AsmContext *parent;
} AsmContext;


/* 初始化 / 释放汇编上下文 */
LUAI_FUNC void lasm_initcontext(lua_State *L, AsmContext *ctx, AsmContext *parent);
LUAI_FUNC void lasm_freecontext(lua_State *L, AsmContext *ctx);

/* 解析 asm 体并发射字节码 */
LUAI_FUNC void lasm_parse_body(LexState *ls, FuncState *fs, AsmContext *ctx, int line);

/* 修补前向引用 */
LUAI_FUNC void lasm_patchpending(LexState *ls, FuncState *fs, AsmContext *ctx);

/* 解析 asm 入口：asm( ... ) 的完整处理 */
LUAI_FUNC void lasm_execute(LexState *ls, FuncState *fs, int line);


#endif