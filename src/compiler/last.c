/*
** $Id: last.c $
** Abstract Syntax Tree (AST) - Arena Memory Pool Implementation
** See Copyright Notice in lua.h
*/

#define last_c
#define LUA_CORE

#include "lprefix.h"


#include <string.h>
#include <stdio.h>

#include "lua.h"

#include "last.h"
#include "lmem.h"


/**
 * @brief 将大小向上对齐到8字节边界
 *
 * @param n 原始大小
 * @return 对齐后的大小
 */
#define align8(n)  (((n) + AST_POOL_ALIGN_MASK) & ~AST_POOL_ALIGN_MASK)


/**
 * @brief 初始化AST内存池
 *
 * @param L Lua状态机指针，用于后续内存分配
 * @param p 要初始化的内存池结构指针
 */
void ast_pool_init(lua_State *L, AstPool *p) {
  p->L = L;
  p->chunks = NULL;
}


/**
 * @brief 释放AST内存池中所有已分配的内存块
 *
 * 遍历块链表，逐个释放每个AstChunk及其数据。
 *
 * @param p 要释放的内存池指针
 */
void ast_pool_free(AstPool *p) {
  AstPoolChunk *c = p->chunks;
  while (c != NULL) {
    AstPoolChunk *next = c->next;
    size_t total_size = sizeof(AstPoolChunk) + c->cap;
    luaM_free_(p->L, c, total_size);
    c = next;
  }
  p->chunks = NULL;
}


/**
 * @brief 从内存池分配指定字节数的内存
 *
 * 分配的内存自动按8字节对齐，并用memset清零。
 * 如果当前块剩余空间不足，则分配新块：
 * - 小对象（<=8192字节）使用默认块大小
 * - 大对象（>8192字节）单独分配刚好够用的块
 * 块从不realloc，保证已返回指针的稳定性。
 *
 * @param p 内存池指针
 * @param bytes 需要分配的字节数
 * @return 指向已清零内存的指针
 */
void *ast_pool_alloc(AstPool *p, size_t bytes) {
  size_t aligned = align8(bytes);
  AstPoolChunk *c = p->chunks;

  /* 当前块空间不足，需要分配新块 */
  if (c == NULL || c->used + aligned > c->cap) {
    size_t chunk_cap = (aligned > AST_POOL_CHUNK_SIZE) ? aligned : AST_POOL_CHUNK_SIZE;
    size_t total_size = sizeof(AstPoolChunk) + chunk_cap;
    c = cast(AstPoolChunk *, luaM_realloc_(p->L, NULL, 0, total_size));
    c->next = p->chunks;
    c->used = 0;
    c->cap = chunk_cap;
    p->chunks = c;
  }

  /* 从当前块切分内存 */
  void *ptr = c->data + c->used;
  c->used += aligned;
  memset(ptr, 0, aligned);
  return ptr;
}


/**
 * @brief 分配并初始化一个AST节点
 *
 * @param p 内存池指针
 * @param size 节点结构体大小
 * @param kind 节点类型标签
 * @param line 源代码行号
 * @return 初始化后的节点指针
 */
AstNode *ast_node_new(AstPool *p, size_t size, AstNodeKind kind, int line) {
  AstNode *node = cast(AstNode *, ast_pool_alloc(p, size));
  node->type = kind;
  node->line = line;
  node->next = NULL;
  return node;
}


/**
 * @brief 创建nil字面量表达式节点
 * @param p 内存池
 * @param line 源代码行号
 * @return 初始化好的表达式节点
 */
AstExpr *ast_new_expr_nil(AstPool *p, int line) {
  AstExpr *e = ast_new_node(p, AstExpr, AST_EXPR, line);
  e->kind = AST_EXPR_NIL;
  return e;
}


/**
 * @brief 创建布尔字面量表达式节点
 * @param p 内存池
 * @param is_true true则为AST_EXPR_TRUE，false则为AST_EXPR_FALSE
 * @param line 源代码行号
 * @return 初始化好的表达式节点
 */
AstExpr *ast_new_expr_bool(AstPool *p, int is_true, int line) {
  AstExpr *e = ast_new_node(p, AstExpr, AST_EXPR, line);
  e->kind = is_true ? AST_EXPR_TRUE : AST_EXPR_FALSE;
  return e;
}


/**
 * @brief 创建整数字面量表达式节点
 * @param p 内存池
 * @param v 整数值
 * @param line 源代码行号
 * @return 初始化好的表达式节点
 */
AstExpr *ast_new_expr_int(AstPool *p, lua_Integer v, int line) {
  AstExpr *e = ast_new_node(p, AstExpr, AST_EXPR, line);
  e->kind = AST_EXPR_INT;
  e->u.ival = v;
  return e;
}


/**
 * @brief 创建浮点数字面量表达式节点
 * @param p 内存池
 * @param v 浮点数值
 * @param line 源代码行号
 * @return 初始化好的表达式节点
 */
AstExpr *ast_new_expr_flt(AstPool *p, lua_Number v, int line) {
  AstExpr *e = ast_new_node(p, AstExpr, AST_EXPR, line);
  e->kind = AST_EXPR_FLT;
  e->u.nval = v;
  return e;
}


/**
 * @brief 创建字符串相关字面量表达式节点（STRING/INTERPSTRING/REGEX）
 * @param p 内存池
 * @param s TString字符串指针
 * @param kind 表达式子类型（AST_EXPR_STRING/AST_EXPR_INTERPSTRING/AST_EXPR_REGEX）
 * @param line 源代码行号
 * @return 初始化好的表达式节点
 */
AstExpr *ast_new_expr_str(AstPool *p, TString *s, AstExprKind kind, int line) {
  AstExpr *e = ast_new_node(p, AstExpr, AST_EXPR, line);
  e->kind = kind;
  e->u.strval = s;
  return e;
}


/**
 * @brief 创建可变参数表达式节点(...)
 * @param p 内存池
 * @param line 源代码行号
 * @return 初始化好的表达式节点
 */
AstExpr *ast_new_expr_vararg(AstPool *p, int line) {
  AstExpr *e = ast_new_node(p, AstExpr, AST_EXPR, line);
  e->kind = AST_EXPR_VARARG;
  return e;
}


/**
 * @brief 创建标识符表达式节点
 * @param p 内存池
 * @param name 标识符名称（TString指针）
 * @param line 源代码行号
 * @return 初始化好的表达式节点
 */
AstExpr *ast_new_expr_ident(AstPool *p, TString *name, int line) {
  AstExpr *e = ast_new_node(p, AstExpr, AST_EXPR, line);
  e->kind = AST_EXPR_IDENT;
  e->u.strval = name;
  return e;
}


/**
 * @brief 创建二元运算表达式节点
 * @param p 内存池
 * @param op 二元运算符
 * @param lhs 左操作数
 * @param rhs 右操作数
 * @param line 源代码行号
 * @return 初始化好的表达式节点
 */
AstExpr *ast_new_expr_binop(AstPool *p, AstBinOp op, AstExpr *lhs, AstExpr *rhs, int line) {
  AstExpr *e = ast_new_node(p, AstExpr, AST_EXPR, line);
  e->kind = AST_EXPR_BINOP;
  e->u.binop.op = op;
  e->u.binop.lhs = lhs;
  e->u.binop.rhs = rhs;
  return e;
}


/**
 * @brief 创建一元运算表达式节点
 * @param p 内存池
 * @param op 一元运算符
 * @param operand 操作数
 * @param line 源代码行号
 * @return 初始化好的表达式节点
 */
AstExpr *ast_new_expr_unop(AstPool *p, AstUnOp op, AstExpr *operand, int line) {
  AstExpr *e = ast_new_node(p, AstExpr, AST_EXPR, line);
  e->kind = AST_EXPR_UNOP;
  e->u.unop.op = op;
  e->u.unop.operand = operand;
  return e;
}


/**
 * @brief 创建函数调用表达式节点
 * @param p 内存池
 * @param callee 被调用函数表达式
 * @param args 参数数组（NULL则只分配空间不复制）
 * @param nargs 参数个数
 * @param line 源代码行号
 * @return 初始化好的表达式节点
 */
AstExpr *ast_new_expr_call(AstPool *p, AstExpr *callee, AstExpr **args, int nargs, int line) {
  AstExpr *e = ast_new_node(p, AstExpr, AST_EXPR, line);
  int i;
  e->kind = AST_EXPR_CALL;
  e->u.call.callee = callee;
  e->u.call.nargs = nargs;
  if (nargs > 0) {
    e->u.call.args = cast(AstExpr **, ast_pool_alloc(p, sizeof(AstExpr *) * nargs));
    if (args != NULL) {
      for (i = 0; i < nargs; i++) {
        e->u.call.args[i] = args[i];
      }
    }
  } else {
    e->u.call.args = NULL;
  }
  return e;
}


/**
 * @brief 创建方法调用表达式节点（recv:method(args)）
 * @param p 内存池
 * @param recv 接收者表达式
 * @param method 方法名（TString指针）
 * @param args 参数数组（NULL则只分配空间不复制）
 * @param nargs 参数个数
 * @param line 源代码行号
 * @return 初始化好的表达式节点
 */
AstExpr *ast_new_expr_methodcall(AstPool *p, AstExpr *recv, TString *method, AstExpr **args, int nargs, int line) {
  AstExpr *e = ast_new_node(p, AstExpr, AST_EXPR, line);
  int i;
  e->kind = AST_EXPR_METHOD_CALL;
  e->u.mcall.recv = recv;
  e->u.mcall.method = method;
  e->u.mcall.nargs = nargs;
  if (nargs > 0) {
    e->u.mcall.args = cast(AstExpr **, ast_pool_alloc(p, sizeof(AstExpr *) * nargs));
    if (args != NULL) {
      for (i = 0; i < nargs; i++) {
        e->u.mcall.args[i] = args[i];
      }
    }
  } else {
    e->u.mcall.args = NULL;
  }
  return e;
}


/**
 * @brief 创建索引访问表达式节点（t[k]或t?.[k]）
 * @param p 内存池
 * @param table 表表达式
 * @param key 键表达式
 * @param is_opt 是否为可选链索引（?.[]）
 * @param line 源代码行号
 * @return 初始化好的表达式节点
 */
AstExpr *ast_new_expr_index(AstPool *p, AstExpr *table, AstExpr *key, int is_opt, int line) {
  AstExpr *e = ast_new_node(p, AstExpr, AST_EXPR, line);
  e->kind = AST_EXPR_INDEX;
  e->u.index.table = table;
  e->u.index.key = key;
  e->u.index.keystr = 0;
  e->u.index.is_opt = is_opt;
  return e;
}


/**
 * @brief 创建表构造器表达式节点
 * @param p 内存池
 * @param entries 条目数组（NULL则只分配空间不复制）
 * @param nentries 条目个数
 * @param line 源代码行号
 * @return 初始化好的表达式节点
 */
AstExpr *ast_new_expr_table(AstPool *p, AstTableEntry *entries, int nentries, int line) {
  AstExpr *e = ast_new_node(p, AstExpr, AST_EXPR, line);
  int i;
  int narr = 0;
  int nrec = 0;
  e->kind = AST_EXPR_TABLE_CTOR;
  e->u.table.nentries = nentries;
  /* 统计数组元素和散列表元素数量 */
  if (entries != NULL) {
    for (i = 0; i < nentries; i++) {
      if (entries[i].kind == AST_TENTRY_POS)
        narr++;
      else
        nrec++;
    }
  }
  e->u.table.narr = narr;
  e->u.table.nrec = nrec;
  if (nentries > 0) {
    e->u.table.entries = cast(AstTableEntry *, ast_pool_alloc(p, sizeof(AstTableEntry) * nentries));
    if (entries != NULL) {
      for (i = 0; i < nentries; i++) {
        e->u.table.entries[i] = entries[i];
      }
    }
  } else {
    e->u.table.entries = NULL;
  }
  return e;
}


/**
 * @brief 创建map构造器表达式节点
 * @param p 内存池
 * @param entries 条目数组（NULL则只分配空间不复制）
 * @param nentries 条目个数
 * @param line 源代码行号
 * @return 初始化好的表达式节点
 */
AstExpr *ast_new_expr_map(AstPool *p, AstMapEntry *entries, int nentries, int line) {
  AstExpr *e = ast_new_node(p, AstExpr, AST_EXPR, line);
  int i;
  e->kind = AST_EXPR_MAP_CTOR;
  e->u.map.nentries = nentries;
  if (nentries > 0) {
    e->u.map.entries = cast(AstMapEntry *, ast_pool_alloc(p, sizeof(AstMapEntry) * nentries));
    if (entries != NULL) {
      for (i = 0; i < nentries; i++) {
        e->u.map.entries[i] = entries[i];
      }
    }
  } else {
    e->u.map.entries = NULL;
  }
  return e;
}


/**
 * @brief 创建函数表达式节点（普通函数或箭头函数）
 * @param p 内存池
 * @param func AstFunc指针
 * @param is_arrow 是否为箭头函数
 * @param line 源代码行号
 * @return 初始化好的表达式节点
 */
AstExpr *ast_new_expr_func(AstPool *p, AstFunc *func, int is_arrow, int line) {
  AstExpr *e = ast_new_node(p, AstExpr, AST_EXPR, line);
  e->kind = is_arrow ? AST_EXPR_ARROW_FUNC : AST_EXPR_FUNC_EXPR;
  e->u.func.func = func;
  return e;
}


/**
 * @brief 创建三元条件表达式节点（a ? b : c）
 * @param p 内存池
 * @param cond 条件表达式
 * @param thn 条件为真时的表达式
 * @param els 条件为假时的表达式
 * @param line 源代码行号
 * @return 初始化好的表达式节点
 */
AstExpr *ast_new_expr_condexpr(AstPool *p, AstExpr *cond, AstExpr *thn, AstExpr *els, int line) {
  AstExpr *e = ast_new_node(p, AstExpr, AST_EXPR, line);
  e->kind = AST_EXPR_CONDEXPR;
  e->u.condexpr.e1 = cond;
  e->u.condexpr.e2 = thn;
  e->u.condexpr.e3 = els;
  return e;
}


/**
 * @brief 创建括号包裹表达式节点
 * @param p 内存池
 * @param expr 被包裹的表达式
 * @param line 源代码行号
 * @return 初始化好的表达式节点
 */
AstExpr *ast_new_expr_paren(AstPool *p, AstExpr *expr, int line) {
  AstExpr *e = ast_new_node(p, AstExpr, AST_EXPR, line);
  e->kind = AST_EXPR_PAREN;
  e->u.paren.expr = expr;
  return e;
}


/**
 * @brief 创建范围表达式节点（start..end）
 * @param p 内存池
 * @param start 起始表达式
 * @param end 结束表达式
 * @param line 源代码行号
 * @return 初始化好的表达式节点
 */
AstExpr *ast_new_expr_range(AstPool *p, AstExpr *start, AstExpr *end, int line) {
  AstExpr *e = ast_new_node(p, AstExpr, AST_EXPR, line);
  e->kind = AST_EXPR_RANGE;
  e->u.range.start = start;
  e->u.range.end = end;
  return e;
}


/**
 * @brief 创建管道类表达式节点（|>, <|, ?|>等）
 * @param p 内存池
 * @param optype 管道操作类型（AST_BIN_PIPE/AST_BIN_REVPIPE/AST_BIN_SAFEPIPE）
 * @param e1 左侧表达式
 * @param e2 右侧表达式
 * @param line 源代码行号
 * @return 初始化好的表达式节点
 */
AstExpr *ast_new_expr_pipe(AstPool *p, AstBinOp optype, AstExpr *e1, AstExpr *e2, int line) {
  AstExpr *e = ast_new_node(p, AstExpr, AST_EXPR, line);
  switch (optype) {
    case AST_BIN_PIPE: e->kind = AST_EXPR_PIPE; break;
    case AST_BIN_REVPIPE: e->kind = AST_EXPR_REVPIPE; break;
    case AST_BIN_SAFEPIPE: e->kind = AST_EXPR_SAFEPIPE; break;
    default: e->kind = AST_EXPR_PIPE; break;
  }
  e->u.binop.op = optype;
  e->u.binop.lhs = e1;
  e->u.binop.rhs = e2;
  return e;
}


/**
 * @brief 创建方法引用表达式节点（obj:method 不带括号）
 * @param p 内存池
 * @param recv 接收者表达式
 * @param method 方法名
 * @param line 源代码行号
 * @return 初始化好的表达式节点
 */
AstExpr *ast_new_expr_methodref(AstPool *p, AstExpr *recv, TString *method, int line) {
  AstExpr *e = ast_new_node(p, AstExpr, AST_EXPR, line);
  e->kind = AST_EXPR_METHOD_REF;
  e->is_pipe_self = 0;  /* 默认值，代码生成时会覆盖为 1 */
  e->u.method_ref.recv = recv;
  e->u.method_ref.method = method;
  return e;
}


/**
 * @brief 创建类型测试表达式节点 [-type expr "typename"]
 * @param p 内存池
 * @param operand 被测试的表达式
 * @param type_name 类型名称字符串
 * @param line 源代码行号
 * @return 初始化好的表达式节点
 */
AstExpr *ast_new_expr_test_type(AstPool *p, AstExpr *operand, TString *type_name, int line) {
  AstExpr *e = ast_new_node(p, AstExpr, AST_EXPR, line);
  e->kind = AST_EXPR_TEST_TYPE;
  e->u.test_type.operand = operand;
  e->u.test_type.type_name = type_name;
  return e;
}


/**
 * @brief 创建 $embed 嵌入文件表达式节点
 * @param p 内存池
 * @param filename 嵌入文件的完整内容（TString）
 * @param line 源代码行号
 * @return 初始化好的表达式节点
 */
AstExpr *ast_new_expr_embed(AstPool *p, TString *filename, int line) {
  AstExpr *e = ast_new_node(p, AstExpr, AST_EXPR, line);
  e->kind = AST_EXPR_EMBED;
  e->u.embed.filename = filename;
  return e;
}


/**
 * @brief 创建 $object 对象表表达式节点
 * @param p 内存池
 * @param table 表构造器AST节点
 * @param line 源代码行号
 * @return 初始化好的表达式节点
 */
AstExpr *ast_new_expr_object(AstPool *p, AstExpr *table, int line) {
  AstExpr *e = ast_new_node(p, AstExpr, AST_EXPR, line);
  e->kind = AST_EXPR_OBJECT;
  e->u.object.ctor = table;
  return e;
}


/**
 * @brief 创建切片表达式节点
 * @param p 内存池
 * @param table 源表表达式
 * @param start 起始索引表达式（NULL 表示省略）
 * @param end 结束索引表达式（NULL 表示省略）
 * @param step 步长表达式（NULL 表示省略）
 * @param line 源代码行号
 * @return 初始化好的表达式节点
 */
AstExpr *ast_new_expr_slice(AstPool *p, AstExpr *table, AstExpr *start, AstExpr *end, AstExpr *step, int line) {
  AstExpr *e = ast_new_node(p, AstExpr, AST_EXPR, line);
  e->kind = AST_EXPR_SLICE;
  e->u.slice.table = table;
  e->u.slice.start = start;
  e->u.slice.end = end;
  e->u.slice.step = step;
  return e;
}


/**
 * @brief 创建 spread 展开运算符表达式节点
 * @param p 内存池
 * @param expr 被展开的表达式
 * @param line 源代码行号
 * @return 初始化好的表达式节点
 */
AstExpr *ast_new_expr_spread(AstPool *p, AstExpr *expr, int line) {
  AstExpr *e = ast_new_node(p, AstExpr, AST_EXPR, line);
  e->kind = AST_EXPR_SPREAD;
  e->u.spread.expr = expr;
  return e;
}


/**
 * @brief 创建 new 表达式节点
 * @param p 内存池
 * @param class_expr 类名表达式
 * @param args 参数数组
 * @param nargs 参数个数
 * @param line 源代码行号
 * @return 初始化好的表达式节点
 */
AstExpr *ast_new_expr_new(AstPool *p, AstExpr *class_expr, AstExpr **args, int nargs, int line) {
  AstExpr *e = ast_new_node(p, AstExpr, AST_EXPR, line);
  e->kind = AST_EXPR_NEW;
  e->u.newexpr.class_expr = class_expr;
  e->u.newexpr.args = args;
  e->u.newexpr.nargs = nargs;
  return e;
}

/**
 * @brief 创建 match 表达式节点
 * @param p 内存池
 * @param stmt match 语句（is_expr=1）
 * @param line 源代码行号
 * @return 初始化好的表达式节点
 */
AstExpr *ast_new_expr_match(AstPool *p, AstStmt *stmt, int line) {
  AstExpr *e = ast_new_node(p, AstExpr, AST_EXPR, line);
  e->kind = AST_EXPR_MATCH;
  e->u.match.stmt = stmt;
  return e;
}

/**
 * @brief 创建 super 表达式节点
 * super 用于在类方法中访问父类成员，编译为 self.__super
 * @param p 内存池
 * @param line 源代码行号
 * @return 初始化好的表达式节点
 */
AstExpr *ast_new_expr_super(AstPool *p, int line) {
  AstExpr *e = ast_new_node(p, AstExpr, AST_EXPR, line);
  e->kind = AST_EXPR_SUPER;
  e->u.super.obj = NULL;
  e->u.super.method = NULL;
  return e;
}

/**
 * @brief 创建海象操作符表达式节点
 * @param p 内存池
 * @param name 变量名
 * @param expr 右侧表达式
 * @param line 行号
 * @return 表达式节点
 */
AstExpr *ast_new_expr_walrus(AstPool *p, TString *name, AstExpr *expr, int line) {
  AstExpr *e = ast_new_node(p, AstExpr, AST_EXPR, line);
  e->kind = AST_EXPR_WALRUS;
  e->u.walrus.name = name;
  e->u.walrus.expr = expr;
  return e;
}


/**
 * @brief 创建 astparser 编译期代码块表达式节点
 * @param p 内存池
 * @param proto 预编译的 Proto
 * @param chunk 预编译的 AstChunk（可为 NULL）
 * @param line 源代码行号
 * @return 初始化好的 astparser 表达式节点
 */
AstExpr *ast_new_expr_astparser(AstPool *p, struct Proto *proto, struct AstChunk *chunk, int line) {
  AstExpr *e = ast_new_node(p, AstExpr, AST_EXPR, line);
  e->kind = AST_EXPR_ASTPARSER;
  e->u.astparser.proto = proto;
  e->u.astparser.chunk = chunk;
  return e;
}


/**
 * @brief 创建类成员节点
 * @param p 内存池
 * @param kind 成员类型（方法/属性/getter/setter等）
 * @param access 访问级别（private/protected/public/default）
 * @param is_static 是否为静态成员
 * @param name 成员名
 * @param line 源代码行号
 * @return 初始化好的类成员节点
 */
AstClassMember *ast_new_class_member(AstPool *p, AstMemberKind kind, AstAccessLevel access,
                                     int is_static, TString *name, int line) {
  AstClassMember *m = (AstClassMember *)ast_pool_alloc(p, sizeof(AstClassMember));
  m->kind = kind;
  m->access = access;
  m->is_static = is_static;
  m->name = name;
  m->u.method_func = NULL;
  m->line = line;
  return m;
}


/* AstBlock初始容量 */
#define AST_BLOCK_INIT_CAP  4


/**
 * @brief 创建空语句块节点
 * @param p 内存池
 * @param line 源代码行号
 * @return 初始化好的语句块节点
 */
AstStmt *ast_new_stmt_block(AstPool *p, int line) {
  AstStmt *s = ast_new_node(p, AstStmt, AST_STMT, line);
  s->kind = AST_STMT_BLOCK;
  s->u.block.block.count = 0;
  s->u.block.block.capacity = AST_BLOCK_INIT_CAP;
  s->u.block.block.items = cast(AstStmt **, ast_pool_alloc(p, sizeof(AstStmt *) * AST_BLOCK_INIT_CAP));
  return s;
}


/**
 * @brief 向语句块中添加语句（自动扩容，x2增长）
 * @param p 内存池
 * @param blk 目标语句块
 * @param s 要添加的语句
 */
void ast_block_add_stmt(AstPool *p, AstBlock *blk, AstStmt *s) {
  if (blk->count >= blk->capacity) {
    int newcap = blk->capacity * 2;
    AstStmt **newitems = cast(AstStmt **, ast_pool_alloc(p, sizeof(AstStmt *) * newcap));
    int i;
    for (i = 0; i < blk->count; i++) {
      newitems[i] = blk->items[i];
    }
    blk->items = newitems;
    blk->capacity = newcap;
  }
  blk->items[blk->count++] = s;
}


/**
 * @brief 向语句块中添加导出名称（自动扩容）
 * @param p 内存池
 * @param blk 目标语句块
 * @param name 要导出的名称
 */
void ast_block_add_export(AstPool *p, AstBlock *blk, TString *name) {
  if (blk->nexports >= blk->exports_cap) {
    int newcap = (blk->exports_cap == 0) ? 4 : blk->exports_cap * 2;
    TString **newarr = cast(TString **, ast_pool_alloc(p, sizeof(TString *) * newcap));
    int i;
    for (i = 0; i < blk->nexports; i++) {
      newarr[i] = blk->exports[i];
    }
    blk->exports = newarr;
    blk->exports_cap = newcap;
  }
  blk->exports[blk->nexports++] = name;
}


/**
 * @brief 创建局部变量声明语句
 * @param p 内存池
 * @param nnames 变量名数量
 * @param names 变量名数组（NULL则只分配空间不复制）
 * @param nvalues 初始化值数量
 * @param line 源代码行号
 * @return 初始化好的语句节点
 */
AstStmt *ast_new_stmt_local(AstPool *p, int nnames, TString **names, int nvalues, int line) {
  AstStmt *s = ast_new_node(p, AstStmt, AST_STMT, line);
  int i;
  s->kind = AST_STMT_LOCAL;
  s->u.local.nnames = nnames;
  s->u.local.nvalues = nvalues;
  if (nnames > 0) {
    s->u.local.names = cast(TString **, ast_pool_alloc(p, sizeof(TString *) * nnames));
    s->u.local.attrs = cast(int *, ast_pool_alloc(p, sizeof(int) * nnames));
    s->u.local.type_hints = cast(TypeHint **, ast_pool_alloc(p, sizeof(TypeHint *) * nnames));
    for (i = 0; i < nnames; i++) {
      s->u.local.attrs[i] = AST_ATTR_NONE;
      s->u.local.type_hints[i] = NULL;
    }
    if (names != NULL) {
      for (i = 0; i < nnames; i++) {
        s->u.local.names[i] = names[i];
      }
    }
  } else {
    s->u.local.names = NULL;
    s->u.local.attrs = NULL;
    s->u.local.type_hints = NULL;
  }
  if (nvalues > 0) {
    s->u.local.values = cast(AstExpr **, ast_pool_alloc(p, sizeof(AstExpr *) * nvalues));
  } else {
    s->u.local.values = NULL;
  }
  return s;
}


/**
 * @brief 创建赋值语句
 * @param p 内存池
 * @param ntargets 赋值目标数量
 * @param nvalues 值数量
 * @param line 源代码行号
 * @return 初始化好的语句节点
 */
AstStmt *ast_new_stmt_assign(AstPool *p, int ntargets, int nvalues, int line) {
  AstStmt *s = ast_new_node(p, AstStmt, AST_STMT, line);
  s->kind = AST_STMT_ASSIGN;
  s->u.assign.ntargets = ntargets;
  s->u.assign.nvalues = nvalues;
  if (ntargets > 0) {
    s->u.assign.targets = cast(AstAssignTarget *, ast_pool_alloc(p, sizeof(AstAssignTarget) * ntargets));
  } else {
    s->u.assign.targets = NULL;
  }
  if (nvalues > 0) {
    s->u.assign.values = cast(AstExpr **, ast_pool_alloc(p, sizeof(AstExpr *) * nvalues));
  } else {
    s->u.assign.values = NULL;
  }
  return s;
}


/**
 * @brief 创建表达式语句
 * @param p 内存池
 * @param e 表达式
 * @param line 源代码行号
 * @return 初始化好的语句节点
 */
AstStmt *ast_new_stmt_expr(AstPool *p, AstExpr *e, int line) {
  AstStmt *s = ast_new_node(p, AstStmt, AST_STMT, line);
  s->kind = AST_STMT_EXPR;
  s->u.expr.expr = e;
  return s;
}


/**
 * @brief 创建if语句
 * @param p 内存池
 * @param line 源代码行号
 * @return 初始化好的语句节点
 */
AstStmt *ast_new_stmt_if(AstPool *p, int line) {
  AstStmt *s = ast_new_node(p, AstStmt, AST_STMT, line);
  s->kind = AST_STMT_IF;
  s->u.ifstmt.arms = NULL;
  s->u.ifstmt.narms = 0;
  s->u.ifstmt.has_else = 0;
  s->u.ifstmt.else_body.count = 0;
  s->u.ifstmt.else_body.capacity = 0;
  s->u.ifstmt.else_body.items = NULL;
  return s;
}


/**
 * @brief 创建while语句
 * @param p 内存池
 * @param cond 条件表达式
 * @param line 源代码行号
 * @return 初始化好的语句节点
 */
AstStmt *ast_new_stmt_while(AstPool *p, AstExpr *cond, int line) {
  AstStmt *s = ast_new_node(p, AstStmt, AST_STMT, line);
  s->kind = AST_STMT_WHILE;
  s->u.whilestmt.cond = cond;
  s->u.whilestmt.body.count = 0;
  s->u.whilestmt.body.capacity = 0;
  s->u.whilestmt.body.items = NULL;
  s->u.whilestmt.else_body.count = 0;
  s->u.whilestmt.else_body.capacity = 0;
  s->u.whilestmt.else_body.items = NULL;
  s->u.whilestmt.has_else = 0;
  return s;
}


/**
 * @brief 创建while let语句
 * @param p 内存池
 * @param nnames 变量数量
 * @param names 变量名数组
 * @param expr 赋值表达式
 * @param line 源代码行号
 * @return 初始化好的语句节点
 */
AstStmt *ast_new_stmt_while_let(AstPool *p, int nnames, TString **names, AstExpr *expr, int line) {
  AstStmt *s = ast_new_node(p, AstStmt, AST_STMT, line);
  s->kind = AST_STMT_WHILE_LET;
  s->u.whilelet.nnames = nnames;
  s->u.whilelet.names = names;
  s->u.whilelet.expr = expr;
  s->u.whilelet.body.count = 0;
  s->u.whilelet.body.capacity = 0;
  s->u.whilelet.body.items = NULL;
  s->u.whilelet.else_body.count = 0;
  s->u.whilelet.else_body.capacity = 0;
  s->u.whilelet.else_body.items = NULL;
  s->u.whilelet.has_else = 0;
  return s;
}


/**
 * @brief 创建repeat语句
 * @param p 内存池
 * @param line 源代码行号
 * @return 初始化好的语句节点
 */
AstStmt *ast_new_stmt_repeat(AstPool *p, int line) {
  AstStmt *s = ast_new_node(p, AstStmt, AST_STMT, line);
  s->kind = AST_STMT_REPEAT;
  s->u.whilestmt.cond = NULL;
  s->u.whilestmt.body.count = 0;
  s->u.whilestmt.body.capacity = 0;
  s->u.whilestmt.body.items = NULL;
  return s;
}


/**
 * @brief 创建数值for语句
 * @param p 内存池
 * @param var 循环变量名
 * @param start 起始值表达式
 * @param stop 终止值表达式
 * @param step 步长表达式（NULL表示步长为1）
 * @param line 源代码行号
 * @return 初始化好的语句节点
 */
AstStmt *ast_new_stmt_fornum(AstPool *p, TString *var, AstExpr *start, AstExpr *stop, AstExpr *step, int line) {
  AstStmt *s = ast_new_node(p, AstStmt, AST_STMT, line);
  s->kind = AST_STMT_FOR_NUM;
  s->u.fornum.var = var;
  s->u.fornum.start = start;
  s->u.fornum.stop = stop;
  s->u.fornum.step = step;
  s->u.fornum.body.count = 0;
  s->u.fornum.body.capacity = 0;
  s->u.fornum.body.items = NULL;
  s->u.fornum.else_body.count = 0;
  s->u.fornum.else_body.capacity = 0;
  s->u.fornum.else_body.items = NULL;
  s->u.fornum.has_else = 0;
  return s;
}


/**
 * @brief 创建泛型for语句
 * @param p 内存池
 * @param nnames 循环变量名数量
 * @param nexprs 迭代器表达式数量
 * @param line 源代码行号
 * @return 初始化好的语句节点
 */
AstStmt *ast_new_stmt_forgen(AstPool *p, int nnames, int nexprs, int line) {
  AstStmt *s = ast_new_node(p, AstStmt, AST_STMT, line);
  s->kind = AST_STMT_FOR_GEN;
  s->u.forgen.nnames = nnames;
  s->u.forgen.nexprs = nexprs;
  if (nnames > 0) {
    s->u.forgen.names = cast(TString **, ast_pool_alloc(p, sizeof(TString *) * nnames));
  } else {
    s->u.forgen.names = NULL;
  }
  if (nexprs > 0) {
    s->u.forgen.exprs = cast(AstExpr **, ast_pool_alloc(p, sizeof(AstExpr *) * nexprs));
  } else {
    s->u.forgen.exprs = NULL;
  }
  s->u.forgen.body.count = 0;
  s->u.forgen.body.capacity = 0;
  s->u.forgen.body.items = NULL;
  s->u.forgen.else_body.count = 0;
  s->u.forgen.else_body.capacity = 0;
  s->u.forgen.else_body.items = NULL;
  s->u.forgen.has_else = 0;
  return s;
}


/**
 * @brief 创建return语句
 * @param p 内存池
 * @param nvalues 返回值数量
 * @param line 源代码行号
 * @return 初始化好的语句节点
 */
AstStmt *ast_new_stmt_return(AstPool *p, int nvalues, int line) {
  AstStmt *s = ast_new_node(p, AstStmt, AST_STMT, line);
  s->kind = AST_STMT_RETURN;
  s->u.retstmt.nvalues = nvalues;
  if (nvalues > 0) {
    s->u.retstmt.values = cast(AstExpr **, ast_pool_alloc(p, sizeof(AstExpr *) * nvalues));
  } else {
    s->u.retstmt.values = NULL;
  }
  return s;
}


/**
 * @brief 创建break语句
 * @param p 内存池
 * @param level break层级
 * @param line 源代码行号
 * @return 初始化好的语句节点
 */
AstStmt *ast_new_stmt_break(AstPool *p, int level, int line) {
  AstStmt *s = ast_new_node(p, AstStmt, AST_STMT, line);
  s->kind = AST_STMT_BREAK;
  s->u.contbrk.level = level;
  return s;
}


/**
 * @brief 创建continue语句
 * @param p 内存池
 * @param level continue层级
 * @param line 源代码行号
 * @return 初始化好的语句节点
 */
AstStmt *ast_new_stmt_continue(AstPool *p, int level, int line) {
  AstStmt *s = ast_new_node(p, AstStmt, AST_STMT, line);
  s->kind = AST_STMT_CONTINUE;
  s->u.contbrk.level = level;
  return s;
}


/**
 * @brief 创建goto语句
 * @param p 内存池
 * @param name 目标标签名
 * @param line 源代码行号
 * @return 初始化好的语句节点
 */
AstStmt *ast_new_stmt_goto(AstPool *p, TString *name, int line) {
  AstStmt *s = ast_new_node(p, AstStmt, AST_STMT, line);
  s->kind = AST_STMT_GOTO;
  s->u.label.name = name;
  s->u.label.label_id = -1;
  s->u.label.patch_pc = -1;
  return s;
}


/**
 * @brief 创建label语句
 * @param p 内存池
 * @param name 标签名
 * @param line 源代码行号
 * @return 初始化好的语句节点
 */
AstStmt *ast_new_stmt_label(AstPool *p, TString *name, int line) {
  AstStmt *s = ast_new_node(p, AstStmt, AST_STMT, line);
  s->kind = AST_STMT_LABEL;
  s->u.label.name = name;
  s->u.label.label_id = -1;
  s->u.label.patch_pc = -1;
  return s;
}


/**
 * @brief 创建空语句
 * @param p 内存池
 * @param line 源代码行号
 * @return 初始化好的语句节点
 */
AstStmt *ast_new_stmt_empty(AstPool *p, int line) {
  AstStmt *s = ast_new_node(p, AstStmt, AST_STMT, line);
  s->kind = AST_STMT_EMPTY;
  return s;
}


/**
 * @brief 创建复合赋值语句（+=, -=等）
 * @param p 内存池
 * @param op 二元运算符类型
 * @param ntargets 赋值目标数量
 * @param value 值表达式
 * @param line 源代码行号
 * @return 初始化好的语句节点
 */
AstStmt *ast_new_stmt_compound(AstPool *p, AstBinOp op, int ntargets, AstExpr *value, int line) {
  AstStmt *s = ast_new_node(p, AstStmt, AST_STMT, line);
  s->kind = AST_STMT_COMPOUND_ASSIGN;
  s->u.compound.op = op;
  s->u.compound.ntargets = ntargets;
  s->u.compound.value = value;
  if (ntargets > 0) {
    s->u.compound.targets = cast(AstAssignTarget *, ast_pool_alloc(p, sizeof(AstAssignTarget) * ntargets));
  } else {
    s->u.compound.targets = NULL;
  }
  return s;
}


/**
 * @brief 创建自增/自减语句（++, --）
 * @param p 内存池
 * @param kind 自增/自减类型（前置/后置）
 * @param line 源代码行号
 * @return 初始化好的语句节点
 */
AstStmt *ast_new_stmt_incr(AstPool *p, AstIncrKind kind, int line) {
  AstStmt *s = ast_new_node(p, AstStmt, AST_STMT, line);
  s->kind = AST_STMT_INCR_DECR;
  s->u.incr.kind = kind;
  s->u.incr.target = NULL;
  return s;
}


/**
 * @brief 创建 guard 语句（guard cond else { ... } / guard let name = expr else { ... }）
 * @param p 内存池
 * @param cond guard 条件表达式（guard let 时为 NULL）
 * @param let_var guard let 变量名（普通 guard 时为 NULL）
 * @param let_value guard let 值表达式
 * @param else_block else 代码块
 * @param line 源代码行号
 * @return 初始化好的语句节点
 */
AstStmt *ast_new_stmt_guard(AstPool *p, AstExpr *cond, TString *let_var, AstExpr *let_value, AstBlock *else_block, int line) {
  AstStmt *s = ast_new_node(p, AstStmt, AST_STMT, line);
  s->kind = AST_STMT_GUARD;
  s->u.guard.cond = cond;
  s->u.guard.let_var = let_var;
  s->u.guard.let_value = let_value;
  if (else_block != NULL) {
    s->u.guard.else_block = *else_block;
  }
  return s;
}


/**
 * @brief 创建 try 语句
 * @param p 内存池
 * @param body try 块语句
 * @param catch_var catch 变量名（NULL 表示无 catch）
 * @param catch_body catch 块语句
 * @param finally_body finally 块语句
 * @param line 源代码行号
 * @return 初始化好的语句节点
 */
AstStmt *ast_new_stmt_try(AstPool *p, AstBlock *body, AstExpr *catch_var,
                          AstBlock *catch_body, AstBlock *finally_body, int line) {
  AstStmt *s = ast_new_node(p, AstStmt, AST_STMT, line);
  s->kind = AST_STMT_TRY;
  if (body != NULL) s->u.trycatch.body = *body;
  s->u.trycatch.catch_var = catch_var;
  if (catch_body != NULL) s->u.trycatch.catch_body = *catch_body;
  if (finally_body != NULL) s->u.trycatch.finally_body = *finally_body;
  return s;
}


/**
 * @brief 创建 defer 语句
 * @param p 内存池
 * @param body 延迟执行的语句块
 * @param line 源代码行号
 * @return 初始化好的语句节点
 */
AstStmt *ast_new_stmt_defer(AstPool *p, AstBlock *body, int line) {
  AstStmt *s = ast_new_node(p, AstStmt, AST_STMT, line);
  s->kind = AST_STMT_DEFER;
  if (body != NULL) s->u.deferstmt.body = *body;
  return s;
}


/**
 * @brief 创建 namespace 语句
 * @param p 内存池
 * @param name 命名空间名
 * @param body 命名空间体
 * @param line 源代码行号
 * @return 初始化好的语句节点
 */
AstStmt *ast_new_stmt_namespace(AstPool *p, TString *name, AstBlock *body, int line) {
  AstStmt *s = ast_new_node(p, AstStmt, AST_STMT, line);
  s->kind = AST_STMT_NAMESPACE;
  s->u.nsstruct.name = name;
  if (body != NULL) s->u.nsstruct.body = *body;
  return s;
}


/**
 * @brief 创建指定类型的命名空间风格语句（class/trait/interface/enum/struct等）
 * @param p 内存池
 * @param kind 语句类型
 * @param name 语句名
 * @param body 语句体（可为 NULL）
 * @param line 源代码行号
 * @return 初始化好的语句节点
 */
AstStmt *ast_new_stmt_typed(AstPool *p, AstStmtKind kind, TString *name, AstBlock *body, int line) {
  AstStmt *s = ast_new_node(p, AstStmt, AST_STMT, line);
  s->kind = kind;
  s->u.nsstruct.name = name;
  if (body != NULL) s->u.nsstruct.body = *body;
  s->u.nsstruct.entries = NULL;
  s->u.nsstruct.nentries = 0;
  return s;
}


/**
 * @brief 创建带键值对列表的语句节点（用于 struct/superstruct）
 * @param p 内存池
 * @param kind 语句类型
 * @param name 语句名
 * @param pairs 键值对数组
 * @param npairs 键值对数量
 * @param line 源代码行号
 * @return 初始化好的语句节点
 */
AstStmt *ast_new_stmt_typed_pairs(AstPool *p, AstStmtKind kind, TString *name, AstKVPair *pairs, int npairs, int line) {
  AstStmt *s = ast_new_node(p, AstStmt, AST_STMT, line);
  int i;
  s->kind = kind;
  s->u.nsstruct.name = name;
  s->u.nsstruct.body.count = 0;
  s->u.nsstruct.body.capacity = 0;
  s->u.nsstruct.body.items = NULL;
  s->u.nsstruct.nentries = npairs;
  if (npairs > 0) {
    s->u.nsstruct.entries = cast(AstKVPair *, ast_pool_alloc(p, sizeof(AstKVPair) * npairs));
    for (i = 0; i < npairs; i++) {
      s->u.nsstruct.entries[i] = pairs[i];
    }
  } else {
    s->u.nsstruct.entries = NULL;
  }
  return s;
}


/**
 * @brief 创建 enum 语句
 * @param p 内存池
 * @param name 枚举名（NULL表示匿名枚举）
 * @param entries 枚举成员数组
 * @param nentries 枚举成员数量
 * @param is_enum_class 是否为enum class
 * @param line 源代码行号
 * @return 初始化好的语句节点
 */
AstStmt *ast_new_stmt_enum(AstPool *p, TString *name, AstEnumEntry *entries,
                           int nentries, int is_enum_class, int line) {
  AstStmt *s = ast_new_node(p, AstStmt, AST_STMT, line);
  s->kind = AST_STMT_ENUM;
  s->u.enumstmt.name = name;
  s->u.enumstmt.entries = entries;
  s->u.enumstmt.nentries = nentries;
  s->u.enumstmt.is_enum_class = is_enum_class;
  return s;
}


/**
 * @brief 创建 using 语句
 * @param p 内存池
 * @param is_namespace 1=using namespace, 0=using Name::Member
 * @param name 命名空间名或成员名
 * @param last_member ::链的最后一个成员名
 * @param line 源代码行号
 * @return 初始化好的语句节点
 */
AstStmt *ast_new_stmt_using(AstPool *p, int is_namespace, TString *name,
                            TString *last_member, int line) {
  AstStmt *s = ast_new_node(p, AstStmt, AST_STMT, line);
  s->kind = AST_STMT_USING;
  s->u.usingstmt.is_namespace = is_namespace;
  s->u.usingstmt.name = name;
  s->u.usingstmt.last_member = last_member;
  return s;
}


/**
 * @brief 创建 throw 语句
 * @param p 内存池
 * @param expr 抛出的表达式
 * @param line 源代码行号
 * @return 初始化好的语句节点
 */
AstStmt *ast_new_stmt_throw(AstPool *p, AstExpr *expr, int line) {
  AstStmt *s = ast_new_node(p, AstStmt, AST_STMT, line);
  s->kind = AST_STMT_THROW;
  s->u.throwstmt.expr = expr;
  return s;
}


/**
 * @brief 创建局部函数声明语句
 * @param p 内存池
 * @param name 函数名
 * @param func AstFunc指针
 * @param line 源代码行号
 * @return 初始化好的语句节点
 */
AstStmt *ast_new_stmt_localfunc(AstPool *p, TString *name, AstFunc *func, int line) {
  AstStmt *s = ast_new_node(p, AstStmt, AST_STMT, line);
  s->kind = AST_STMT_LOCAL_FUNC;
  s->u.localfunc.name = name;
  s->u.localfunc.func = func;
  s->u.localfunc.local_idx = -1;
  return s;
}


/**
 * @brief 创建全局变量声明语句
 * @param p 内存池
 * @param nnames 变量名数量
 * @param nvalues 初始化值数量
 * @param line 源代码行号
 * @return 初始化好的语句节点
 */
AstStmt *ast_new_stmt_global(AstPool *p, int nnames, int nvalues, int line) {
  AstStmt *s = ast_new_node(p, AstStmt, AST_STMT, line);
  s->kind = AST_STMT_GLOBAL;
  s->u.global.nnames = nnames;
  s->u.global.nvalues = nvalues;
  if (nnames > 0) {
    s->u.global.names = cast(TString **, ast_pool_alloc(p, sizeof(TString *) * nnames));
  } else {
    s->u.global.names = NULL;
  }
  if (nvalues > 0) {
    s->u.global.values = cast(AstExpr **, ast_pool_alloc(p, sizeof(AstExpr *) * nvalues));
  } else {
    s->u.global.values = NULL;
  }
  s->u.global.has_wildcard = 0;
  return s;
}


/**
 * @brief 创建 take 解构赋值语句节点
 * @param p 内存池
 * @param nvars 变量数量
 * @param varnames 变量名数组
 * @param defaults 默认值表达式数组（与varnames对应，NULL表示无默认值）
 * @param source 源表达式
 * @param is_array 是否为数组解构 [a, b]（0=表解构 {a, b}）
 * @param line 源代码行号
 * @return 初始化好的语句节点
 */
AstStmt *ast_new_stmt_take(AstPool *p, int nvars, TString **varnames, AstExpr **defaults, AstExpr *source, int is_array, int line) {
  AstStmt *s = ast_new_node(p, AstStmt, AST_STMT, line);
  s->kind = AST_STMT_TAKE;
  s->u.take.nvars = nvars;
  s->u.take.varnames = varnames;
  s->u.take.defaults = defaults;
  s->u.take.source = source;
  s->u.take.is_array = is_array;
  return s;
}


/**
 * @brief 创建 constexpr 预处理语句节点
 * @param p 内存池
 * @param directive 指令名（如 "if"）
 * @param cond 条件表达式
 * @param body 语句体
 * @param line 源代码行号
 * @return 初始化好的语句节点
 */
AstStmt *ast_new_stmt_constexpr(AstPool *p, TString *directive, AstExpr *cond, AstBlock *body, int line) {
  AstStmt *s = ast_new_node(p, AstStmt, AST_STMT, line);
  s->kind = AST_STMT_CONSTEXPR;
  s->u.constexpr_stmt.directive = directive;
  s->u.constexpr_stmt.cond = cond;
  if (body) {
    s->u.constexpr_stmt.body = *body;
  } else {
    s->u.constexpr_stmt.body.count = 0;
    s->u.constexpr_stmt.body.capacity = 0;
    s->u.constexpr_stmt.body.items = NULL;
  }
  return s;
}

/**
 * @brief 创建 with 语句: with(expr) do ... end
 * @param p 内存池
 * @param target with 目标表达式
 * @param body with 体
 * @param line 源代码行号
 * @return 初始化好的语句节点
 */
AstStmt *ast_new_stmt_with(AstPool *p, AstExpr *target, AstBlock *body, int line) {
  AstStmt *s = ast_new_node(p, AstStmt, AST_STMT, line);
  s->kind = AST_STMT_WITH;
  s->u.withstmt.target = target;
  if (body) {
    s->u.withstmt.body = *body;
  } else {
    s->u.withstmt.body.count = 0;
    s->u.withstmt.body.capacity = 0;
    s->u.withstmt.body.items = NULL;
  }
  return s;
}


/**
 * @brief 创建 asm 内联汇编语句节点
 * @param p 内存池
 * @param raw_body 原始汇编文本（括号内的内容）
 * @param line 源代码行号
 * @return 初始化好的语句节点
 */
AstStmt *ast_new_stmt_asm(AstPool *p, TString *raw_body, int line) {
  AstStmt *s = ast_new_node(p, AstStmt, AST_STMT, line);
  s->kind = AST_STMT_ASM;
  s->u.asmstmt.raw_body = raw_body;
  return s;
}


/**
 * @brief 创建通配符匹配模式 (_)
 * @param p 内存池
 * @param line 源代码行号
 * @return 初始化好的匹配模式节点
 */
AstMatchPat *ast_new_pat_wildcard(AstPool *p, int line) {
  AstMatchPat *pat = cast(AstMatchPat *, ast_pool_alloc(p, sizeof(AstMatchPat)));
  pat->kind = AST_PAT_WILDCARD;
  pat->line = line;
  return pat;
}


/**
 * @brief 创建字面量匹配模式
 * @param p 内存池
 * @param literal 字面量表达式
 * @param line 源代码行号
 * @return 初始化好的匹配模式节点
 */
AstMatchPat *ast_new_pat_literal(AstPool *p, AstExpr *literal, int line) {
  AstMatchPat *pat = cast(AstMatchPat *, ast_pool_alloc(p, sizeof(AstMatchPat)));
  pat->kind = AST_PAT_LITERAL;
  pat->line = line;
  pat->u.literal = literal;
  return pat;
}


/**
 * @brief 创建变量绑定匹配模式
 * @param p 内存池
 * @param name 变量名
 * @param line 源代码行号
 * @return 初始化好的匹配模式节点
 */
AstMatchPat *ast_new_pat_variable(AstPool *p, TString *name, int line) {
  AstMatchPat *pat = cast(AstMatchPat *, ast_pool_alloc(p, sizeof(AstMatchPat)));
  pat->kind = AST_PAT_VARIABLE;
  pat->line = line;
  pat->u.var_name = name;
  return pat;
}


/**
 * @brief 创建范围匹配模式 (low..high)
 * @param p 内存池
 * @param low 下界表达式
 * @param high 上界表达式
 * @param line 源代码行号
 * @return 初始化好的匹配模式节点
 */
AstMatchPat *ast_new_pat_range(AstPool *p, AstExpr *low, AstExpr *high, int line) {
  AstMatchPat *pat = cast(AstMatchPat *, ast_pool_alloc(p, sizeof(AstMatchPat)));
  pat->kind = AST_PAT_RANGE;
  pat->line = line;
  pat->u.range.low = low;
  pat->u.range.high = high;
  return pat;
}


/**
 * @brief 创建类型匹配模式 (is TypeName)
 * @param p 内存池
 * @param type_name 类型名
 * @param line 源代码行号
 * @return 初始化好的匹配模式节点
 */
AstMatchPat *ast_new_pat_type(AstPool *p, TString *type_name, int line) {
  AstMatchPat *pat = cast(AstMatchPat *, ast_pool_alloc(p, sizeof(AstMatchPat)));
  pat->kind = AST_PAT_TYPE;
  pat->line = line;
  pat->u.type_name = type_name;
  return pat;
}


/**
 * @brief 创建 OR 多值匹配模式节点 (逗号分隔)
 * @param p 内存池
 * @param pats 子模式数组
 * @param npat 子模式数量
 * @param line 源代码行号
 * @return 初始化好的匹配模式节点
 */
AstMatchPat *ast_new_pat_or(AstPool *p, AstMatchPat **pats, int npat, int line) {
  AstMatchPat *pat = cast(AstMatchPat *, ast_pool_alloc(p, sizeof(AstMatchPat)));
  pat->kind = AST_PAT_OR;
  pat->line = line;
  pat->u.or_pat.pats = pats;
  pat->u.or_pat.npat = npat;
  return pat;
}


/**
 * @brief 创建表解构匹配模式节点 { field1, field2, ... }
 * @param p 内存池
 * @param fields 字段子模式数组
 * @param nfields 字段数量
 * @param line 源代码行号
 * @return 初始化好的匹配模式节点
 */
AstMatchPat *ast_new_pat_table(AstPool *p, AstMatchPat **fields, int nfields, int line) {
  AstMatchPat *pat = cast(AstMatchPat *, ast_pool_alloc(p, sizeof(AstMatchPat)));
  pat->kind = AST_PAT_TABLE;
  pat->line = line;
  pat->u.table_pat.fields = fields;
  pat->u.table_pat.nfields = nfields;
  return pat;
}


/**
 * @brief 创建 match 语句节点
 * @param p 内存池
 * @param control 控制表达式
 * @param arms 匹配臂数组
 * @param narms 匹配臂数量
 * @param is_expr 是否为表达式模式
 * @param line 源代码行号
 * @return 初始化好的语句节点
 */
AstStmt *ast_new_stmt_match(AstPool *p, AstExpr *control, AstMatchArm *arms, int narms, int is_expr, int line) {
  AstStmt *s = ast_new_node(p, AstStmt, AST_STMT, line);
  s->kind = AST_STMT_MATCH;
  s->u.matchstmt.control = control;
  s->u.matchstmt.arms = arms;
  s->u.matchstmt.narms = narms;
  s->u.matchstmt.is_expr = is_expr;
  return s;
}


/**
 * @brief 创建if分支臂
 * @param p 内存池
 * @param cond 条件表达式（NULL表示else分支）
 * @param line 源代码行号
 * @return 初始化好的IfArm结构
 */
AstIfArm *ast_new_ifarm(AstPool *p, AstExpr *cond, int line) {
  AstIfArm *arm = cast(AstIfArm *, ast_pool_alloc(p, sizeof(AstIfArm)));
  (void)line;
  arm->cond = cond;
  arm->body.count = 0;
  arm->body.capacity = 0;
  arm->body.items = NULL;
  return arm;
}


/**
 * @brief 创建switch case分支
 * @param p 内存池
 * @param pattern case匹配表达式
 * @param is_default 是否为default分支
 * @param line 源代码行号
 * @return 初始化好的SwitchCase结构
 */
AstSwitchCase *ast_new_switchcase(AstPool *p, AstExpr **patterns, int npatterns, int is_default, int line) {
  AstSwitchCase *c = cast(AstSwitchCase *, ast_pool_alloc(p, sizeof(AstSwitchCase)));
  (void)line;
  c->patterns = patterns;
  c->npatterns = npatterns;
  c->is_default = is_default;
  c->body.count = 0;
  c->body.capacity = 0;
  c->body.items = NULL;
  return c;
}


/**
 * @brief 创建新的函数定义节点
 *
 * 初始化函数的各个字段：参数、upvalue、子函数数组初始容量为4，
 * 函数体初始容量为4，计数器初始化为0。
 *
 * @param p 内存池
 * @param func_idx 唯一函数ID
 * @param parent_idx 父函数ID，主chunk为-1
 * @param line 函数定义起始行号
 * @return 初始化好的AstFunc指针
 */
AstFunc *ast_new_func(AstPool *p, int func_idx, int parent_idx, int line) {
  AstFunc *f = ast_new_node(p, AstFunc, AST_FUNC, line);
  f->func_idx = func_idx;
  f->parent_idx = parent_idx;
  f->nparams = 0;
  f->params = NULL;
  f->is_vararg = 0;
  f->vararg_name = NULL;
  f->is_async = 0;
  f->nlocals = 0;
  f->nups = 0;
  f->line_defined = line;
  f->source = NULL;
  f->return_type_hint = NULL;
  f->generic_params = NULL;
  f->ngeneric_params = 0;
  f->generic_constraints = NULL;
  f->nodiscard = 0;

  /* 初始化函数体，初始容量4 */
  f->body.count = 0;
  f->body.capacity = AST_BLOCK_INIT_CAP;
  f->body.items = cast(AstStmt **, ast_pool_alloc(p, sizeof(AstStmt *) * AST_BLOCK_INIT_CAP));

  /* 初始化upvalue数组，初始容量4 */
  f->nupvalues = 0;
  f->upval_cap = AST_FUNC_UPVAL_INIT_CAP;
  f->upvalues = cast(AstUpvalueDesc *, ast_pool_alloc(p, sizeof(AstUpvalueDesc) * AST_FUNC_UPVAL_INIT_CAP));

  /* 初始化子函数数组，初始容量4 */
  f->nchild_funcs = 0;
  f->child_cap = AST_FUNC_CHILD_INIT_CAP;
  f->child_funcs = cast(AstFunc **, ast_pool_alloc(p, sizeof(AstFunc *) * AST_FUNC_CHILD_INIT_CAP));

  return f;
}


/**
 * @brief 创建函数参数
 * @param p 内存池
 * @param name 参数名TString
 * @param attr 参数属性（AST_ATTR_NONE/AST_ATTR_CONST/AST_ATTR_CLOSE）
 * @return 初始化好的AstFuncParam指针
 */
AstFuncParam *ast_new_param(AstPool *p, TString *name, int attr) {
  AstFuncParam *param = cast(AstFuncParam *, ast_pool_alloc(p, sizeof(AstFuncParam)));
  param->name = name;
  param->default_value = NULL;
  param->attr = attr;
  param->type_hint = NULL;
  return param;
}


/**
 * @brief 向函数添加upvalue描述符（自动扩容，x2增长）
 * @param p 内存池
 * @param f 目标函数
 * @param src upvalue来源（AST_UPVAL_LOCAL/AST_UPVAL_UPVAL）
 * @param idx 父局部变量索引或父upvalue索引
 * @param name upvalue名称
 */
void ast_func_add_upvalue(AstPool *p, AstFunc *f, AstUpvalSrc src, int idx, TString *name) {
  if (f->nupvalues >= f->upval_cap) {
    int newcap = f->upval_cap * 2;
    AstUpvalueDesc *newups = cast(AstUpvalueDesc *, ast_pool_alloc(p, sizeof(AstUpvalueDesc) * newcap));
    int i;
    for (i = 0; i < f->nupvalues; i++) {
      newups[i] = f->upvalues[i];
    }
    f->upvalues = newups;
    f->upval_cap = newcap;
  }
  f->upvalues[f->nupvalues].src = src;
  f->upvalues[f->nupvalues].idx = idx;
  f->upvalues[f->nupvalues].name = name;
  f->nupvalues++;
}


/**
 * @brief 创建新的编译单元（chunk）
 *
 * 自动创建主函数（func_idx=0, parent_idx=-1），并初始化函数列表。
 *
 * @param p 内存池
 * @param source 源码文件名TString
 * @return 初始化好的AstChunk指针
 */
AstChunk *ast_new_chunk(AstPool *p, TString *source) {
  AstChunk *chunk = ast_new_node(p, AstChunk, AST_CHUNK, 0);
  chunk->source = source;
  chunk->pool = p;

  /* 创建主函数 */
  chunk->main_func = ast_new_func(p, 0, -1, 0);
  chunk->main_func->source = source;

  /* 初始化函数平铺列表，初始容量4，先加入main_func */
  chunk->nfuncs = 0;
  chunk->funcs_cap = AST_CHUNK_FUNCS_INIT_CAP;
  chunk->all_funcs = cast(AstFunc **, ast_pool_alloc(p, sizeof(AstFunc *) * AST_CHUNK_FUNCS_INIT_CAP));

  /* 将main_func加入列表 */
  chunk->all_funcs[chunk->nfuncs++] = chunk->main_func;

  return chunk;
}


/**
 * @brief 将函数添加到chunk的函数列表和父函数的子函数列表
 *
 * 该函数同时更新chunk的all_funcs平铺列表，以及父函数的child_funcs列表。
 * 两个动态数组都会在容量不足时自动x2扩容。
 *
 * @param chunk 目标编译单元
 * @param f 要添加的函数（其parent_idx必须已正确设置）
 */
void ast_chunk_add_func(AstChunk *chunk, AstFunc *f) {
  AstPool *p = chunk->pool;

  /* 添加到chunk的all_funcs平铺列表 */
  if (chunk->nfuncs >= chunk->funcs_cap) {
    int newcap = chunk->funcs_cap * 2;
    AstFunc **newfuncs = cast(AstFunc **, ast_pool_alloc(p, sizeof(AstFunc *) * newcap));
    int i;
    for (i = 0; i < chunk->nfuncs; i++) {
      newfuncs[i] = chunk->all_funcs[i];
    }
    chunk->all_funcs = newfuncs;
    chunk->funcs_cap = newcap;
  }
  chunk->all_funcs[chunk->nfuncs++] = f;

  /* 如果有父函数，添加到父函数的child_funcs列表 */
  if (f->parent_idx >= 0) {
    AstFunc *parent = chunk->all_funcs[f->parent_idx];
    if (parent->nchild_funcs >= parent->child_cap) {
      int newcap = parent->child_cap * 2;
      AstFunc **newchildren = cast(AstFunc **, ast_pool_alloc(p, sizeof(AstFunc *) * newcap));
      int i;
      for (i = 0; i < parent->nchild_funcs; i++) {
        newchildren[i] = parent->child_funcs[i];
      }
      parent->child_funcs = newchildren;
      parent->child_cap = newcap;
    }
    parent->child_funcs[parent->nchild_funcs++] = f;
  }
}


/* ====================================================================== */
/* AST S表达式调试打印实现 */

/**
 * @brief 打印指定层级的缩进（每层2空格）
 * @param out 输出文件
 * @param indent 缩进层级
 */
static void dump_indent(FILE *out, int indent) {
  int i;
  for (i = 0; i < indent; i++) {
    fputs("  ", out);
  }
}

/**
 * @brief 二元运算符转字符串
 * @param op 二元运算符枚举值
 * @return 运算符字符串
 */
static const char *binop_name(AstBinOp op) {
  switch (op) {
    case AST_BIN_ADD: return "+";
    case AST_BIN_SUB: return "-";
    case AST_BIN_MUL: return "*";
    case AST_BIN_DIV: return "/";
    case AST_BIN_IDIV: return "//";
    case AST_BIN_MOD: return "%";
    case AST_BIN_POW: return "^";
    case AST_BIN_BAND: return "&";
    case AST_BIN_BOR: return "|";
    case AST_BIN_BXOR: return "~";
    case AST_BIN_SHL: return "<<";
    case AST_BIN_SHR: return ">>";
    case AST_BIN_CONCAT: return "..";
    case AST_BIN_PIPE: return "|>";
    case AST_BIN_REVPIPE: return "<|";
    case AST_BIN_SAFEPIPE: return "?>";
    case AST_BIN_EQ: return "==";
    case AST_BIN_NE: return "~=";
    case AST_BIN_LT: return "<";
    case AST_BIN_LE: return "<=";
    case AST_BIN_GT: return ">";
    case AST_BIN_GE: return ">=";
    case AST_BIN_SPACESHIP: return "<=>";
    case AST_BIN_IS: return "is";
    case AST_BIN_IN: return "in";
    case AST_BIN_AND: return "and";
    case AST_BIN_OR: return "or";
    case AST_BIN_NULLCOAL: return "??";
    case AST_BIN_CASE: return "<>";
    case AST_BIN_INFIX: return "infix";
    case AST_BIN_MERGE: return "merge";
    default: return "?";
  }
}

/**
 * @brief 一元运算符转字符串
 * @param op 一元运算符枚举值
 * @return 运算符字符串
 */
static const char *unop_name(AstUnOp op) {
  switch (op) {
    case AST_UN_MINUS: return "-";
    case AST_UN_BNOT: return "~";
    case AST_UN_NOT: return "not";
    case AST_UN_LEN: return "#";
    case AST_UN_AWAIT: return "await";
    case AST_UN_TEST_Z: return "test-z";
    case AST_UN_TEST_N: return "test-n";
    case AST_UN_TEST_NIL: return "test-nil";
    case AST_UN_TEST_BOOL: return "test-bool";
    case AST_UN_TEST_FUNC: return "test-func";
    default: return "?";
  }
}

/**
 * @brief 自增/自减类型转字符串
 * @param kind 自增/自减类型
 * @return 类型字符串
 */
static const char *incr_kind_name(AstIncrKind kind) {
  switch (kind) {
    case AST_INCR_PRE_INC: return "pre-inc";
    case AST_INCR_PRE_DEC: return "pre-dec";
    case AST_INCR_POST_INC: return "post-inc";
    case AST_INCR_POST_DEC: return "post-dec";
    default: return "?";
  }
}

/**
 * @brief 打印转义后的字符串（简单处理，截断过长内容）
 * @param out 输出文件
 * @param s TString指针
 */
static void dump_print_str(FILE *out, TString *s) {
  const char *str = getstr(s);
  int i;
  int len = 0;
  const char *p;
  if (str == NULL) {
    fputs("null", out);
    return;
  }
  fputc('"', out);
  for (p = str; *p && len < 40; p++, len++) {
    char c = *p;
    switch (c) {
      case '"': fputs("\\\"", out); break;
      case '\\': fputs("\\\\", out); break;
      case '\n': fputs("\\n", out); break;
      case '\r': fputs("\\r", out); break;
      case '\t': fputs("\\t", out); break;
      default:
        if ((unsigned char)c < 0x20) {
          fprintf(out, "\\x%02x", (unsigned char)c);
        } else {
          fputc(c, out);
        }
    }
  }
  if (*p) fputs("...", out);
  fputc('"', out);
}

/**
 * @brief 打印赋值目标
 * @param out 输出文件
 * @param tgt 赋值目标
 * @param indent 缩进层级
 */
static void dump_assignment_target(FILE *out, AstAssignTarget *tgt, int indent) {
  (void)indent;
  switch (tgt->kind) {
    case AST_TGT_VAR:
      if (tgt->as.var.name) {
        fprintf(out, "%s", getstr(tgt->as.var.name));
      } else {
        fprintf(out, "var#%d", tgt->as.var.idx);
      }
      break;
    case AST_TGT_INDEX:
      fputs("(index ", out);
      ast_dump_expr(out, tgt->as.index.table, 0);
      fputc(' ', out);
      ast_dump_expr(out, tgt->as.index.key, 0);
      fputc(')', out);
      break;
  }
}


/* 前向声明 */
static void dump_if_arms(FILE *out, AstIfArm *arms, int narms, int indent);
static void dump_block_inline(FILE *out, AstBlock *blk, int indent);
static void dump_match_pat(FILE *out, AstMatchPat *pat, int indent);


/**
 * @brief 打印匹配模式节点
 * @param out 输出文件
 * @param pat 匹配模式节点
 * @param indent 缩进层级
 */
static void dump_match_pat(FILE *out, AstMatchPat *pat, int indent) {
  int i;
  if (pat == NULL) {
    fputs("(null-pat)", out);
    return;
  }
  switch (pat->kind) {
    case AST_PAT_WILDCARD:
      fputs("_", out);
      break;
    case AST_PAT_LITERAL:
      ast_dump_expr(out, pat->u.literal, 0);
      break;
    case AST_PAT_VARIABLE:
      fprintf(out, "%s", getstr(pat->u.var_name));
      break;
    case AST_PAT_RANGE:
      ast_dump_expr(out, pat->u.range.low, 0);
      fputs("..", out);
      ast_dump_expr(out, pat->u.range.high, 0);
      break;
    case AST_PAT_TYPE:
      fprintf(out, "is %s", getstr(pat->u.type_name));
      break;
    case AST_PAT_OR:
      for (i = 0; i < pat->u.or_pat.npat; i++) {
        if (i > 0) fputs(", ", out);
        dump_match_pat(out, pat->u.or_pat.pats[i], indent);
      }
      break;
    case AST_PAT_TABLE:
      fputs("{", out);
      for (i = 0; i < pat->u.table_pat.nfields; i++) {
        if (i > 0) fputs(", ", out);
        dump_match_pat(out, pat->u.table_pat.fields[i], indent);
      }
      fputs("}", out);
      break;
  }
}


/**
 * @brief 打印表达式节点
 * @param out 输出文件指针
 * @param e 表达式节点指针
 * @param indent 当前缩进层级
 */
void ast_dump_expr(FILE *out, AstExpr *e, int indent) {
  int i;
  if (e == NULL) {
    fputs("(null)", out);
    return;
  }
  switch (e->kind) {
    case AST_EXPR_NIL:
      fputs("(nil)", out);
      break;
    case AST_EXPR_TRUE:
      fputs("(true)", out);
      break;
    case AST_EXPR_FALSE:
      fputs("(false)", out);
      break;
    case AST_EXPR_INT:
      fprintf(out, "(int " LUA_INTEGER_FMT ")", e->u.ival);
      break;
    case AST_EXPR_FLT:
      fprintf(out, "(flt %g)", (double)e->u.nval);
      break;
    case AST_EXPR_STRING:
      fputs("(str ", out);
      dump_print_str(out, e->u.strval);
      fputc(')', out);
      break;
    case AST_EXPR_INTERPSTRING:
      fputs("(interp ", out);
      dump_print_str(out, e->u.strval);
      fputc(')', out);
      break;
    case AST_EXPR_REGEX:
      fputs("(regex ", out);
      dump_print_str(out, e->u.strval);
      fputc(')', out);
      break;
    case AST_EXPR_VARARG:
      fputs("(...)", out);
      break;
    case AST_EXPR_IDENT:
      fprintf(out, "(ident %s)", getstr(e->u.strval));
      break;
    case AST_EXPR_BINOP:
      fprintf(out, "(binop %s ", binop_name(e->u.binop.op));
      ast_dump_expr(out, e->u.binop.lhs, 0);
      fputc(' ', out);
      ast_dump_expr(out, e->u.binop.rhs, 0);
      fputc(')', out);
      break;
    case AST_EXPR_UNOP:
      fprintf(out, "(unop %s ", unop_name(e->u.unop.op));
      ast_dump_expr(out, e->u.unop.operand, 0);
      fputc(')', out);
      break;
    case AST_EXPR_CALL:
      fputs("(call ", out);
      ast_dump_expr(out, e->u.call.callee, 0);
      for (i = 0; i < e->u.call.nargs; i++) {
        fputc(' ', out);
        ast_dump_expr(out, e->u.call.args[i], 0);
      }
      fputc(')', out);
      break;
    case AST_EXPR_METHOD_CALL:
      fputs("(mcall ", out);
      ast_dump_expr(out, e->u.mcall.recv, 0);
      fprintf(out, " :%s", getstr(e->u.mcall.method));
      for (i = 0; i < e->u.mcall.nargs; i++) {
        fputc(' ', out);
        ast_dump_expr(out, e->u.mcall.args[i], 0);
      }
      fputc(')', out);
      break;
    case AST_EXPR_INDEX:
      if (e->u.index.is_opt) {
        fputs("(index? ", out);
      } else {
        fputs("(index ", out);
      }
      ast_dump_expr(out, e->u.index.table, 0);
      fputc(' ', out);
      ast_dump_expr(out, e->u.index.key, 0);
      fputc(')', out);
      break;
    case AST_EXPR_TABLE_CTOR:
      fputs("(table", out);
      for (i = 0; i < e->u.table.nentries; i++) {
        AstTableEntry *entry = &e->u.table.entries[i];
        fputc(' ', out);
        if (entry->key != NULL) {
          fputc('[', out);
          ast_dump_expr(out, entry->key, 0);
          fputs("]=", out);
        }
        ast_dump_expr(out, entry->value, 0);
      }
      fputc(')', out);
      break;
    case AST_EXPR_MAP_CTOR:
      fputs("(map", out);
      for (i = 0; i < e->u.map.nentries; i++) {
        AstMapEntry *entry = &e->u.map.entries[i];
        fputc(' ', out);
        fputc('[', out);
        ast_dump_expr(out, entry->key, 0);
        fputs("]=", out);
        ast_dump_expr(out, entry->value, 0);
      }
      fputc(')', out);
      break;
    case AST_EXPR_FUNC_EXPR:
      fprintf(out, "(func %d", e->u.func.func->func_idx);
      fputc(')', out);
      break;
    case AST_EXPR_ARROW_FUNC:
      fprintf(out, "(arrow %d", e->u.func.func->func_idx);
      fputc(')', out);
      break;
    case AST_EXPR_DICT_COMP:
      fprintf(out, "(dictcomp %d", e->u.func.func->func_idx);
      fputc(')', out);
      break;
    case AST_EXPR_LIST_COMP:
      fprintf(out, "(listcomp %d", e->u.func.func->func_idx);
      fputc(')', out);
      break;
    case AST_EXPR_AWAIT:
      fputs("(await ", out);
      ast_dump_expr(out, e->u.unop.operand, 0);
      fputc(')', out);
      break;
    case AST_EXPR_PIPE:
      fputs("(pipe |> ", out);
      ast_dump_expr(out, e->u.binop.lhs, 0);
      fputc(' ', out);
      ast_dump_expr(out, e->u.binop.rhs, 0);
      fputc(')', out);
      break;
    case AST_EXPR_REVPIPE:
      fputs("(revpipe <| ", out);
      ast_dump_expr(out, e->u.binop.lhs, 0);
      fputc(' ', out);
      ast_dump_expr(out, e->u.binop.rhs, 0);
      fputc(')', out);
      break;
    case AST_EXPR_SAFEPIPE:
      fputs("(safepipe ?> ", out);
      ast_dump_expr(out, e->u.binop.lhs, 0);
      fputc(' ', out);
      ast_dump_expr(out, e->u.binop.rhs, 0);
      fputc(')', out);
      break;
    case AST_EXPR_NULLCOAL:
      fputs("(nullcoal ?? ", out);
      ast_dump_expr(out, e->u.binop.lhs, 0);
      fputc(' ', out);
      ast_dump_expr(out, e->u.binop.rhs, 0);
      fputc(')', out);
      break;
    case AST_EXPR_SPACESHIP:
      fputs("(spaceship <=> ", out);
      ast_dump_expr(out, e->u.binop.lhs, 0);
      fputc(' ', out);
      ast_dump_expr(out, e->u.binop.rhs, 0);
      fputc(')', out);
      break;
    case AST_EXPR_IS:
      fputs("(is ", out);
      ast_dump_expr(out, e->u.binop.lhs, 0);
      fputc(' ', out);
      ast_dump_expr(out, e->u.binop.rhs, 0);
      fputc(')', out);
      break;
    case AST_EXPR_IN:
      fputs("(in ", out);
      ast_dump_expr(out, e->u.binop.lhs, 0);
      fputc(' ', out);
      ast_dump_expr(out, e->u.binop.rhs, 0);
      fputc(')', out);
      break;
    case AST_EXPR_MERGE:
      fputs("(merge <> ", out);
      ast_dump_expr(out, e->u.binop.lhs, 0);
      fputc(' ', out);
      ast_dump_expr(out, e->u.binop.rhs, 0);
      fputc(')', out);
      break;
    case AST_EXPR_CONDEXPR:
      fputs("(cond ", out);
      ast_dump_expr(out, e->u.condexpr.e1, 0);
      fputc(' ', out);
      ast_dump_expr(out, e->u.condexpr.e2, 0);
      fputc(' ', out);
      ast_dump_expr(out, e->u.condexpr.e3, 0);
      fputc(')', out);
      break;
    case AST_EXPR_PAREN:
      fputs("(paren ", out);
      ast_dump_expr(out, e->u.paren.expr, 0);
      fputc(')', out);
      break;
    case AST_EXPR_OPTCHAIN:
      fputs("(optchain)", out);
      break;
    case AST_EXPR_RANGE:
      fputs("(range ", out);
      ast_dump_expr(out, e->u.range.start, 0);
      fputc(' ', out);
      ast_dump_expr(out, e->u.range.end, 0);
      fputc(')', out);
      break;
    case AST_EXPR_SUPER:
      if (e->u.super.method) {
        fprintf(out, "(super :%s)", getstr(e->u.super.method));
      } else {
        fputs("(super)", out);
      }
      break;
    case AST_EXPR_SWITCH_EXPR: {
      int j, k;
      fputs("(switch ", out);
      ast_dump_expr(out, e->u.switchx.cond, 0);
      for (j = 0; j < e->u.switchx.narms; j++) {
        fputc(' ', out);
        fputc('(', out);
        fputs("case ", out);
        for (k = 0; k < e->u.switchx.arms[j].npatterns; k++) {
          if (k > 0) fputs(", ", out);
          ast_dump_expr(out, e->u.switchx.arms[j].patterns[k], 0);
        }
        fputc(' ', out);
        ast_dump_expr(out, e->u.switchx.arms[j].body, 0);
        fputc(')', out);
      }
      if (e->u.switchx.def) {
        fputs(" (default ", out);
        ast_dump_expr(out, e->u.switchx.def, 0);
        fputc(')', out);
      }
      fputc(')', out);
      break;
    }
    case AST_EXPR_SELECT_CASE:
      fputs("(select-case)", out);
      break;
    case AST_EXPR_NEW:
      fputs("(new ", out);
      ast_dump_expr(out, e->u.newexpr.class_expr, 0);
      for (i = 0; i < e->u.newexpr.nargs; i++) {
        fputc(' ', out);
        ast_dump_expr(out, e->u.newexpr.args[i], 0);
      }
      fputc(')', out);
      break;
    case AST_EXPR_MATCH:
      fputs("(match-expr ", out);
      ast_dump_stmt(out, e->u.match.stmt, 0);
      fputc(')', out);
      break;
    case AST_EXPR_TEST_TYPE:
      fprintf(out, "(test-type \"%s\" ", getstr(e->u.test_type.type_name));
      ast_dump_expr(out, e->u.test_type.operand, 0);
      fputc(')', out);
      break;
    case AST_EXPR_EMBED:
      fprintf(out, "(embed \"%s\")", getstr(e->u.embed.filename));
      break;
    case AST_EXPR_OBJECT:
      fputs("(object ", out);
      ast_dump_expr(out, e->u.object.ctor, 0);
      fputc(')', out);
      break;
    case AST_EXPR_SLICE:
      fputs("(slice ", out);
      ast_dump_expr(out, e->u.slice.table, 0);
      fputc(' ', out);
      if (e->u.slice.start) ast_dump_expr(out, e->u.slice.start, 0); else fputs("nil", out);
      fputc(' ', out);
      if (e->u.slice.end) ast_dump_expr(out, e->u.slice.end, 0); else fputs("nil", out);
      fputc(' ', out);
      if (e->u.slice.step) ast_dump_expr(out, e->u.slice.step, 0); else fputs("nil", out);
      fputc(')', out);
      break;
    case AST_EXPR_SPREAD:
      fputs("(spread ", out);
      ast_dump_expr(out, e->u.spread.expr, 0);
      fputc(')', out);
      break;
    default:
      fprintf(out, "(expr-kind-%d)", e->kind);
      break;
  }
}


/**
 * @brief 内联打印语句块（不换行，简单节点直接内联）
 * @param out 输出文件
 * @param blk 语句块
 * @param indent 缩进层级
 */
static void dump_block_inline(FILE *out, AstBlock *blk, int indent) {
  if (blk->count == 0) {
    fputs("()", out);
    return;
  }
  if (blk->count == 1) {
    ast_dump_stmt(out, blk->items[0], indent);
    return;
  }
  fputs("(block", out);
  ast_dump_block(out, blk, indent + 1);
  fputc(')', out);
}


/**
 * @brief 打印if分支臂
 * @param out 输出文件
 * @param arms 分支臂数组
 * @param narms 分支臂数量
 * @param indent 缩进层级
 */
static void dump_if_arms(FILE *out, AstIfArm *arms, int narms, int indent) {
  int i;
  for (i = 0; i < narms; i++) {
    fputc('\n', out);
    dump_indent(out, indent + 1);
    fputc('(', out);
    if (i == 0) {
      fputs("if ", out);
    } else {
      fputs("elseif ", out);
    }
    ast_dump_expr(out, arms[i].cond, 0);
    if (arms[i].body.count > 0) {
      ast_dump_block(out, &arms[i].body, indent + 1);
    }
    fputc(')', out);
  }
}


/**
 * @brief 打印语句块
 * @param out 输出文件指针
 * @param blk 语句块指针
 * @param indent 当前缩进层级
 */
void ast_dump_block(FILE *out, AstBlock *blk, int indent) {
  int i;
  for (i = 0; i < blk->count; i++) {
    fputc('\n', out);
    dump_indent(out, indent);
    ast_dump_stmt(out, blk->items[i], indent);
  }
}


/**
 * @brief 打印语句节点
 * @param out 输出文件指针
 * @param s 语句节点指针
 * @param indent 当前缩进层级
 */
void ast_dump_stmt(FILE *out, AstStmt *s, int indent) {
  int i;
  if (s == NULL) {
    fputs("(null-stmt)", out);
    return;
  }
  switch (s->kind) {
    case AST_STMT_BLOCK:
      fputs("(block", out);
      ast_dump_block(out, &s->u.block.block, indent + 1);
      fputc(')', out);
      break;
    case AST_STMT_LOCAL:
      fputs("(local (", out);
      for (i = 0; i < s->u.local.nnames; i++) {
        if (i > 0) fputc(' ', out);
        fputs(getstr(s->u.local.names[i]), out);
      }
      fputc(')', out);
      if (s->u.local.nvalues > 0) {
        fputs(" = (", out);
        for (i = 0; i < s->u.local.nvalues; i++) {
          if (i > 0) fputc(' ', out);
          ast_dump_expr(out, s->u.local.values[i], 0);
        }
        fputc(')', out);
      }
      fputc(')', out);
      break;
    case AST_STMT_ASSIGN:
      fputs("(assign (", out);
      for (i = 0; i < s->u.assign.ntargets; i++) {
        if (i > 0) fputc(' ', out);
        dump_assignment_target(out, &s->u.assign.targets[i], 0);
      }
      fputs(") = (", out);
      for (i = 0; i < s->u.assign.nvalues; i++) {
        if (i > 0) fputc(' ', out);
        ast_dump_expr(out, s->u.assign.values[i], 0);
      }
      fputs("))", out);
      break;
    case AST_STMT_EXPR:
      fputs("(exprstmt ", out);
      ast_dump_expr(out, s->u.expr.expr, 0);
      fputc(')', out);
      break;
    case AST_STMT_IF:
      fputs("(if", out);
      dump_if_arms(out, s->u.ifstmt.arms, s->u.ifstmt.narms, indent);
      if (s->u.ifstmt.has_else) {
        fputc('\n', out);
        dump_indent(out, indent + 1);
        fputs("(else", out);
        if (s->u.ifstmt.else_body.count > 0) {
          ast_dump_block(out, &s->u.ifstmt.else_body, indent + 1);
        }
        fputc(')', out);
      }
      fputc('\n', out);
      dump_indent(out, indent);
      fputc(')', out);
      break;
    case AST_STMT_WHILE:
      fputs("(while ", out);
      ast_dump_expr(out, s->u.whilestmt.cond, 0);
      if (s->u.whilestmt.body.count > 0) {
        ast_dump_block(out, &s->u.whilestmt.body, indent + 1);
      }
      if (s->u.whilestmt.has_else) {
        fputc('\n', out);
        dump_indent(out, indent + 1);
        fputs("else", out);
        ast_dump_block(out, &s->u.whilestmt.else_body, indent + 1);
      }
      fputc(')', out);
      break;
    case AST_STMT_WHILE_LET:
      fputs("(while_let (", out);
      {
        int i;
        for (i = 0; i < s->u.whilelet.nnames; i++) {
          if (i > 0) fputc(' ', out);
          fputs(getstr(s->u.whilelet.names[i]), out);
        }
      }
      fputs(") = ", out);
      ast_dump_expr(out, s->u.whilelet.expr, 0);
      if (s->u.whilelet.body.count > 0) {
        ast_dump_block(out, &s->u.whilelet.body, indent + 1);
      }
      if (s->u.whilelet.has_else) {
        fputc('\n', out);
        dump_indent(out, indent + 1);
        fputs("else", out);
        ast_dump_block(out, &s->u.whilelet.else_body, indent + 1);
      }
      fputc(')', out);
      break;
    case AST_STMT_REPEAT:
      fputs("(repeat", out);
      if (s->u.whilestmt.body.count > 0) {
        ast_dump_block(out, &s->u.whilestmt.body, indent + 1);
      }
      fputc('\n', out);
      dump_indent(out, indent + 1);
      ast_dump_expr(out, s->u.whilestmt.cond, 0);
      fputc(')', out);
      break;
    case AST_STMT_FOR_NUM:
      fprintf(out, "(for %s = ", getstr(s->u.fornum.var));
      ast_dump_expr(out, s->u.fornum.start, 0);
      fputs(", ", out);
      ast_dump_expr(out, s->u.fornum.stop, 0);
      if (s->u.fornum.step != NULL) {
        fputs(", ", out);
        ast_dump_expr(out, s->u.fornum.step, 0);
      }
      if (s->u.fornum.body.count > 0) {
        ast_dump_block(out, &s->u.fornum.body, indent + 1);
      }
      if (s->u.fornum.has_else) {
        fputc('\n', out);
        dump_indent(out, indent + 1);
        fputs("else", out);
        ast_dump_block(out, &s->u.fornum.else_body, indent + 1);
      }
      fputc(')', out);
      break;
    case AST_STMT_FOR_GEN:
      fputs("(for (", out);
      for (i = 0; i < s->u.forgen.nnames; i++) {
        if (i > 0) fputc(' ', out);
        fputs(getstr(s->u.forgen.names[i]), out);
      }
      fputs(") in (", out);
      for (i = 0; i < s->u.forgen.nexprs; i++) {
        if (i > 0) fputc(' ', out);
        ast_dump_expr(out, s->u.forgen.exprs[i], 0);
      }
      fputc(')', out);
      if (s->u.forgen.body.count > 0) {
        ast_dump_block(out, &s->u.forgen.body, indent + 1);
      }
      if (s->u.forgen.has_else) {
        fputc('\n', out);
        dump_indent(out, indent + 1);
        fputs("else", out);
        ast_dump_block(out, &s->u.forgen.else_body, indent + 1);
      }
      fputc(')', out);
      break;
    case AST_STMT_DO:
      fputs("(do", out);
      ast_dump_block(out, &s->u.block.block, indent + 1);
      fputc(')', out);
      break;
    case AST_STMT_RETURN:
      fputs("(return", out);
      for (i = 0; i < s->u.retstmt.nvalues; i++) {
        fputc(' ', out);
        ast_dump_expr(out, s->u.retstmt.values[i], 0);
      }
      fputc(')', out);
      break;
    case AST_STMT_BREAK:
      if (s->u.contbrk.level > 1) {
        fprintf(out, "(break %d)", s->u.contbrk.level);
      } else {
        fputs("(break)", out);
      }
      break;
    case AST_STMT_CONTINUE:
      fprintf(out, "(continue %d)", s->u.contbrk.level);
      break;
    case AST_STMT_GOTO:
      fprintf(out, "(goto %s)", getstr(s->u.label.name));
      break;
    case AST_STMT_LABEL:
      fprintf(out, "(label %s)", getstr(s->u.label.name));
      break;
    case AST_STMT_SWITCH: {
      int j, k;
      fputs("(switch ", out);
      ast_dump_expr(out, s->u.switchstmt.cond, 0);
      for (j = 0; j < s->u.switchstmt.ncases; j++) {
        fputc('\n', out);
        dump_indent(out, indent + 1);
        fputs("(case ", out);
        for (k = 0; k < s->u.switchstmt.cases[j].npatterns; k++) {
          if (k > 0) fputs(", ", out);
          ast_dump_expr(out, s->u.switchstmt.cases[j].patterns[k], 0);
        }
        if (s->u.switchstmt.cases[j].body.count > 0) {
          ast_dump_block(out, &s->u.switchstmt.cases[j].body, indent + 2);
        }
        fputc(')', out);
      }
      if (s->u.switchstmt.has_default) {
        fputc('\n', out);
        dump_indent(out, indent + 1);
        fputs("(default", out);
        if (s->u.switchstmt.default_body.count > 0) {
          ast_dump_block(out, &s->u.switchstmt.default_body, indent + 2);
        }
        fputc(')', out);
      }
      fputc('\n', out);
      dump_indent(out, indent);
      fputc(')', out);
      break;
    }
    case AST_STMT_LOCAL_FUNC:
      fputs("(localfunc ", out);
      fputs(getstr(s->u.localfunc.name), out);
      fputc(' ', out);
      ast_dump_func(out, s->u.localfunc.func, indent + 1);
      fputc(')', out);
      break;
    case AST_STMT_GLOBAL:
      if (s->u.global.has_wildcard) {
        fputs("(global *)", out);
      } else {
        fputs("(global (", out);
        for (i = 0; i < s->u.global.nnames; i++) {
          if (i > 0) fputc(' ', out);
          fputs(getstr(s->u.global.names[i]), out);
        }
        fputc(')', out);
        if (s->u.global.nvalues > 0) {
          fputs(" = (", out);
          for (i = 0; i < s->u.global.nvalues; i++) {
            if (i > 0) fputc(' ', out);
            ast_dump_expr(out, s->u.global.values[i], 0);
          }
          fputc(')', out);
        }
        fputc(')', out);
      }
      break;
    case AST_STMT_COMPOUND_ASSIGN:
      fprintf(out, "(compound %s (", binop_name(s->u.compound.op));
      for (i = 0; i < s->u.compound.ntargets; i++) {
        if (i > 0) fputc(' ', out);
        dump_assignment_target(out, &s->u.compound.targets[i], 0);
      }
      fputs(") ", out);
      ast_dump_expr(out, s->u.compound.value, 0);
      fputc(')', out);
      break;
    case AST_STMT_INCR_DECR:
      if (s->u.incr.kind == AST_INCR_PRE_INC || s->u.incr.kind == AST_INCR_POST_INC) {
        fprintf(out, "(incr %s ", incr_kind_name(s->u.incr.kind));
      } else {
        fprintf(out, "(decr %s ", incr_kind_name(s->u.incr.kind));
      }
      if (s->u.incr.target) {
        dump_assignment_target(out, s->u.incr.target, 0);
      } else {
        fputs("null", out);
      }
      fputc(')', out);
      break;
    case AST_STMT_TRY:
      fputs("(try", out);
      ast_dump_block(out, &s->u.trycatch.body, indent + 1);
      if (s->u.trycatch.catch_var || s->u.trycatch.catch_body.count > 0) {
        fputc('\n', out);
        dump_indent(out, indent + 1);
        fputs("(catch ", out);
        if (s->u.trycatch.catch_var) {
          ast_dump_expr(out, s->u.trycatch.catch_var, 0);
          fputc(' ', out);
        }
        if (s->u.trycatch.catch_body.count > 0) {
          ast_dump_block(out, &s->u.trycatch.catch_body, indent + 2);
        }
        fputc(')', out);
      }
      if (s->u.trycatch.finally_body.count > 0) {
        fputc('\n', out);
        dump_indent(out, indent + 1);
        fputs("(finally", out);
        ast_dump_block(out, &s->u.trycatch.finally_body, indent + 2);
        fputc(')', out);
      }
      fputc('\n', out);
      dump_indent(out, indent);
      fputc(')', out);
      break;
    case AST_STMT_CATCH:
      fputs("(catch)", out);
      break;
    case AST_STMT_FINALLY:
      fputs("(finally)", out);
      break;
    case AST_STMT_THROW:
      fputs("(throw ", out);
      if (s->u.throwstmt.expr) {
        ast_dump_expr(out, s->u.throwstmt.expr, 0);
      }
      fputc(')', out);
      break;
    case AST_STMT_DEFER:
      fputs("(defer ", out);
      ast_dump_block(out, &s->u.deferstmt.body, 0);
      fputc(')', out);
      break;
    case AST_STMT_USING:
      fputs("(using)", out);
      break;
    case AST_STMT_NAMESPACE:
      fprintf(out, "(namespace %s", getstr(s->u.nsstruct.name));
      ast_dump_block(out, &s->u.nsstruct.body, indent + 1);
      fputc(')', out);
      break;
    case AST_STMT_STRUCT:
      fprintf(out, "(struct %s", getstr(s->u.nsstruct.name));
      if (s->u.nsstruct.nentries > 0) {
        int j;
        for (j = 0; j < s->u.nsstruct.nentries; j++) {
          AstKVPair *p = &s->u.nsstruct.entries[j];
          fputc('\n', out);
          dump_indent(out, indent + 1);
          fprintf(out, "(%s", getstr(p->key->u.strval));
          if (p->value != NULL) {
            fputs(" = ", out);
            ast_dump_expr(out, p->value, 0);
          }
          fputc(')', out);
        }
      }
      fputc(')', out);
      break;
    case AST_STMT_CLASS:
      fprintf(out, "(class %s", getstr(s->u.classstmt.name));
      if (s->u.classstmt.extends_name) {
        fprintf(out, " extends %s", getstr(s->u.classstmt.extends_name));
      }
      /* 打印结构化成员 */
      if (s->u.classstmt.members != NULL) {
        int j;
        for (j = 0; j < s->u.classstmt.nmembers; j++) {
          AstClassMember *m = &s->u.classstmt.members[j];
          fputc('\n', out);
          dump_indent(out, indent + 1);
          fputc('(', out);
          switch (m->access) {
            case AST_ACCESS_PRIVATE: fputs("private ", out); break;
            case AST_ACCESS_PROTECTED: fputs("protected ", out); break;
            case AST_ACCESS_PUBLIC: fputs("public ", out); break;
            default: break;
          }
          if (m->is_static) fputs("static ", out);
          switch (m->kind) {
            case AST_MEMBER_METHOD: fputs("method ", out); break;
            case AST_MEMBER_ABSTRACT: fputs("abstract ", out); break;
            case AST_MEMBER_FINAL: fputs("final ", out); break;
            case AST_MEMBER_PROPERTY: fputs("property ", out); break;
            case AST_MEMBER_GETTER: fputs("getter ", out); break;
            case AST_MEMBER_SETTER: fputs("setter ", out); break;
          }
          fprintf(out, "%s", getstr(m->name));
          if (m->kind != AST_MEMBER_PROPERTY && m->u.method_func != NULL) {
            fprintf(out, " func=%d", m->u.method_func->func_idx);
          }
          fputc(')', out);
        }
      } else if (s->u.classstmt.body.count > 0) {
        /* 兼容旧格式：从 body 中打印 */
        ast_dump_block(out, &s->u.classstmt.body, indent + 1);
      }
      fputc(')', out);
      break;
    case AST_STMT_TRAIT:
      fprintf(out, "(trait %s", getstr(s->u.nsstruct.name));
      ast_dump_block(out, &s->u.nsstruct.body, indent + 1);
      fputc(')', out);
      break;
    case AST_STMT_INTERFACE:
      fprintf(out, "(interface %s", getstr(s->u.nsstruct.name));
      ast_dump_block(out, &s->u.nsstruct.body, indent + 1);
      fputc(')', out);
      break;
    case AST_STMT_CONCEPT:
      fprintf(out, "(concept %s", getstr(s->u.nsstruct.name));
      ast_dump_block(out, &s->u.nsstruct.body, indent + 1);
      fputc(')', out);
      break;
    case AST_STMT_SUPERSTRUCT:
      fprintf(out, "(superstruct %s", getstr(s->u.nsstruct.name));
      if (s->u.nsstruct.nentries > 0) {
        int j;
        for (j = 0; j < s->u.nsstruct.nentries; j++) {
          AstKVPair *p = &s->u.nsstruct.entries[j];
          fputc('\n', out);
          dump_indent(out, indent + 1);
          fputc('(', out);
          ast_dump_expr(out, p->key, 0);
          fputs(" : ", out);
          ast_dump_expr(out, p->value, 0);
          fputc(')', out);
        }
      }
      fputc(')', out);
      break;
    case AST_STMT_ENUM: {
      fprintf(out, "(enum");
      if (s->u.enumstmt.is_enum_class) fprintf(out, " class");
      if (s->u.enumstmt.name) fprintf(out, " %s", getstr(s->u.enumstmt.name));
      fputc('\n', out);
      for (int i = 0; i < s->u.enumstmt.nentries; i++) {
        AstEnumEntry *entry = &s->u.enumstmt.entries[i];
        dump_indent(out, indent + 1);
        fprintf(out, "%s", getstr(entry->name));
        if (entry->value_expr) {
          fprintf(out, " = ");
          ast_dump_expr(out, entry->value_expr, 0);
        }
        fputc('\n', out);
      }
      dump_indent(out, indent);
      fputc(')', out);
      break;
    }
    case AST_STMT_MATCH: {
      int j;
      fputs("(match", out);
      if (s->u.matchstmt.is_expr) fputs("-expr", out);
      fputc(' ', out);
      ast_dump_expr(out, s->u.matchstmt.control, 0);
      for (j = 0; j < s->u.matchstmt.narms; j++) {
        AstMatchArm *arm = &s->u.matchstmt.arms[j];
        fputc('\n', out);
        dump_indent(out, indent + 1);
        fputs("(arm ", out);
        dump_match_pat(out, arm->pattern, indent + 1);
        if (arm->guard) {
          fputs(" if ", out);
          ast_dump_expr(out, arm->guard, 0);
        }
        if (arm->is_arrow) {
          fputs(" => ", out);
          ast_dump_expr(out, arm->body_expr, 0);
        } else {
          if (arm->body_block.count > 0) {
            ast_dump_block(out, &arm->body_block, indent + 2);
          }
        }
        fputc(')', out);
      }
      fputc('\n', out);
      dump_indent(out, indent);
      fputc(')', out);
      break;
    }
    case AST_STMT_WITH:
      fprintf(out, "(with");
      ast_dump_block(out, &s->u.nsstruct.body, indent + 1);
      fputc(')', out);
      break;
    case AST_STMT_ASM:
      fprintf(out, "(asm \"%s\")", getstr(s->u.asmstmt.raw_body));
      break;
    case AST_STMT_EXPORT:
      fputs("(export)", out);
      break;
    case AST_STMT_COMMAND:
      fprintf(out, "(command %s", getstr(s->u.nsstruct.name));
      ast_dump_block(out, &s->u.nsstruct.body, indent + 1);
      fputc(')', out);
      break;
    case AST_STMT_KEYWORD:
      fprintf(out, "(keyword %s", getstr(s->u.nsstruct.name));
      ast_dump_block(out, &s->u.nsstruct.body, indent + 1);
      fputc(')', out);
      break;
    case AST_STMT_OPERATOR:
      fprintf(out, "(operator %s", getstr(s->u.nsstruct.name));
      ast_dump_block(out, &s->u.nsstruct.body, indent + 1);
      fputc(')', out);
      break;
    case AST_STMT_EMPTY:
      fputs("(empty)", out);
      break;
    case AST_STMT_TAKE:
      fputs("(take ", out);
      {
        int j;
        for (j = 0; j < s->u.take.nvars; j++) {
          if (j > 0) fputs(", ", out);
          fprintf(out, "%s", getstr(s->u.take.varnames[j]));
        }
        fputs(") = ", out);
        ast_dump_expr(out, s->u.take.source, 0);
      }
      fputc(')', out);
      break;
    case AST_STMT_CONSTEXPR:
      fprintf(out, "($ %s", getstr(s->u.constexpr_stmt.directive));
      fputc(' ', out);
      ast_dump_expr(out, s->u.constexpr_stmt.cond, 0);
      ast_dump_block(out, &s->u.constexpr_stmt.body, indent + 1);
      fputc(')', out);
      break;
    default:
      fprintf(out, "(stmt-kind-%d)", s->kind);
      break;
  }
}


/**
 * @brief 打印函数节点
 * @param out 输出文件指针
 * @param f 函数节点指针
 * @param indent 当前缩进层级
 */
void ast_dump_func(FILE *out, AstFunc *f, int indent) {
  int i;
  if (f == NULL) {
    fputs("(null-func)", out);
    return;
  }
  fprintf(out, "(func %d parent=%d line=%d", f->func_idx, f->parent_idx, f->line_defined);
  fputs(" params=(", out);
  for (i = 0; i < f->nparams; i++) {
    if (i > 0) fputc(' ', out);
    fputs(getstr(f->params[i].name), out);
    if (f->params[i].default_value) {
      fputc('=', out);
      ast_dump_expr(out, f->params[i].default_value, 0);
    }
  }
  fputc(')', out);
  if (f->ngeneric_params > 0) {
    fputs(" generic=(", out);
    for (i = 0; i < f->ngeneric_params; i++) {
      if (i > 0) fputc(' ', out);
      fputs(getstr(f->generic_params[i]), out);
      if (f->generic_constraints && f->generic_constraints[i]) {
        fputs(":", out);
        /* TypeHint 的简单打印 */
        fputs("type", out);
      }
    }
    fputc(')', out);
  }
  if (f->is_vararg) {
    fputs(" vararg", out);
  }
  if (f->nupvalues > 0) {
    fputs(" upvals=(", out);
    for (i = 0; i < f->nupvalues; i++) {
      if (i > 0) fputc(' ', out);
      fputs(getstr(f->upvalues[i].name), out);
    }
    fputc(')', out);
  }
  if (f->body.count > 0) {
    ast_dump_block(out, &f->body, indent + 1);
  }
  fputc(')', out);
}


/**
 * @brief 打印整个编译单元（chunk）
 * @param out 输出文件指针
 * @param chunk 编译单元指针
 */
void ast_dump_chunk(FILE *out, AstChunk *chunk) {
  int i;
  if (chunk == NULL) {
    fputs("(null-chunk)\n", out);
    return;
  }
  fputs("(chunk ", out);
  if (chunk->source) {
    dump_print_str(out, chunk->source);
  } else {
    fputs("\"?\"", out);
  }
  fputc('\n', out);
  if (chunk->main_func) {
    dump_indent(out, 1);
    ast_dump_func(out, chunk->main_func, 1);
  }
  for (i = 1; i < chunk->nfuncs; i++) {
    fputc('\n', out);
    dump_indent(out, 1);
    ast_dump_func(out, chunk->all_funcs[i], 1);
  }
  fputs(")\n", out);
}
