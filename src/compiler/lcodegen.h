/*
** $Id: lcodegen.h $
** AST to Proto Code Generator
** See Copyright Notice in lua.h
*/

#ifndef lcodegen_h
#define lcodegen_h

#include "lprefix.h"

#include "lua.h"
#include "lcode.h"
#include "llex.h"
#include "lmem.h"
#include "lobject.h"
#include "lopcodes.h"
#include "lparser.h"
#include "lstate.h"
#include "lstring.h"
#include "lfunc.h"
#include "ldebug.h"
#include "ldo.h"
#include "last.h"


#define MAX_LOOP_DEPTH 256

/*
** 循环层级跳转记录
*/
typedef struct {
  int break_list;    /* break跳转链 */
  int continue_list; /* continue跳转链 */
} LoopJump;

/*
** 代码生成上下文状态
*/
typedef struct CodegenState {
  lua_State *L;
  AstPool *pool;
  FuncState *fs;        /* 当前函数状态 */
  BlockCnt *bl;         /* 当前块链 */
  Dyndata *dyd;         /* 动态数据（actvar/label/goto列表） */
  LexState ls;          /* 最小词法状态（供open_func/new_localvar等使用） */
  int nerr;             /* 错误计数 */
  LoopJump loop_stack[MAX_LOOP_DEPTH]; /* 循环层级栈，loop_stack[1..loop_depth] */
  int loop_depth;       /* 当前循环嵌套深度 */
  /* label/goto处理： */
  struct {
    TString **names;
    int *pcs;
    int n;
    int size;
  } labels;
  struct {
    TString **names;
    int *pcs;
    int n;
    int size;
  } gotos;
} CodegenState;


/*
** 从AstFunc生成Proto。主要入口函数。
*/
LUAI_FUNC Proto *luaY_codegen_func(lua_State *L, AstFunc *func, AstPool *pool, Dyndata *dyd);

/*
** 从AstChunk生成Proto（chunk是main func）。
*/
LUAI_FUNC Proto *luaY_codegen_chunk(lua_State *L, AstChunk *chunk, Dyndata *dyd);


#endif
