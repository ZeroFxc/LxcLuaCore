/*
** $Id: last_parse.h $
** AST Parser - Recursive descent parser building LAST AST nodes
** See Copyright Notice in lua.h
*/

#ifndef last_parse_h
#define last_parse_h

#include "llimits.h"
#include "lobject.h"
#include "lzio.h"
#include "last.h"
#include "lparser.h"


/* 前置声明 */
struct Dyndata;
struct Mbuffer;


/* 最大局部变量数（LXCLUA:扩展寄存器到512） */
#define MAXVARS_LP	512

/* 作用域局部名初始容量 */
#define SCOPE_NAMES_INIT 8


/* 前置声明 */
typedef struct ParserState ParserState;
typedef struct ParseScope ParseScope;


/**
 * @brief 解析期作用域结构，用于追踪局部变量
 */
struct ParseScope {
  ParseScope *prev;       /**< 外层作用域链 */
  AstFunc *func;          /**< 此作用域所属函数 */
  int nlocals;            /**< 此作用域及外层可见局部变量总数 */
  int firstlocal;         /**< 此作用域第一个局部变量在func中的索引 */
  int is_loop;            /**< 是否是循环作用域（break/continue目标） */
  TString **local_names;  /**< 此作用域声明的局部变量名列表 */
  int nnames;             /**< 当前作用域内变量数 */
  int names_cap;          /**< local_names数组容量 */
};


/**
 * @brief 解析器状态结构
 */
struct ParserState {
  lua_State *L;           /**< Lua状态机 */
  LexState *ls;           /**< 词法分析器状态 */
  AstPool *pool;          /**< AST内存池（动态分配） */
  AstChunk *chunk;        /**< 当前构建的chunk */
  AstFunc *curfunc;       /**< 当前正在解析的函数 */
  int func_idx_counter;   /**< 下一个函数ID */
  int nerr;               /**< 错误计数 */
  ParseScope *scope;      /**< 当前作用域链 */
  Table *defines;         /**< $define 定义的编译期常量表（用于 $if 条件求值） */
};


/**
 * @brief 解析源码并构建AST
 *
 * @param L Lua状态机
 * @param z 输入流
 * @param buff 词法分析用的缓冲区
 * @param dyd 动态数据结构（Dyndata）
 * @param name 源码文件名
 * @param firstchar 第一个预读字符
 * @return 构建好的AstChunk指针
 */
LUAI_FUNC AstChunk *luaY_parse_ast(lua_State *L, ZIO *z, struct Mbuffer *buff,
                                   struct Dyndata *dyd, const char *name, int firstchar);


#endif
