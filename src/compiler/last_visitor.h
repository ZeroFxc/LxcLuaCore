/*
** $Id: last_visitor.h $
** AST Visitor Framework - Pre-order traversal with callbacks
** See Copyright Notice in lua.h
*/

#ifndef last_visitor_h
#define last_visitor_h

#include "last.h"


/**
 * @brief 遍历控制返回值枚举
 *
 * 访问者回调函数返回此值控制遍历流程
 */
typedef enum {
  AST_VISIT_CONTINUE = 0,  /**< 继续遍历子节点 */
  AST_VISIT_SKIP,          /**< 跳过子节点，继续下一个兄弟节点 */
  AST_VISIT_TERMINATE      /**< 立即终止整个遍历 */
} AstVisitResult;


/**
 * @brief 用户自定义上下文指针类型
 */
typedef void *AstVisitorContext;


/* 前置声明 */
typedef struct AstVisitor AstVisitor;


/**
 * @brief 表达式访问回调函数类型
 * @param ctx 用户自定义上下文
 * @param expr 当前访问的表达式节点
 * @return 遍历控制指令
 */
typedef AstVisitResult (*AstExprVisitor)(AstVisitorContext ctx, AstExpr *expr);


/**
 * @brief 语句访问回调函数类型
 * @param ctx 用户自定义上下文
 * @param stmt 当前访问的语句节点
 * @return 遍历控制指令
 */
typedef AstVisitResult (*AstStmtVisitor)(AstVisitorContext ctx, AstStmt *stmt);


/**
 * @brief 函数入口访问回调函数类型
 * @param ctx 用户自定义上下文
 * @param func 当前访问的函数节点
 * @return 遍历控制指令（SKIP表示跳过函数体）
 */
typedef AstVisitResult (*AstFuncVisitor)(AstVisitorContext ctx, AstFunc *func);


/**
 * @brief AST访问者结构体
 *
 * 存储各类节点的回调函数指针，NULL表示不处理该类型节点
 */
struct AstVisitor {
  AstExprVisitor expr_visitors[AST_EXPR_NEW + 1];         /**< 按AstExprKind索引的表达式回调 */
  AstStmtVisitor stmt_visitors[AST_STMT_CONSTEXPR + 1];  /**< 按AstStmtKind索引的语句回调 */
  AstFuncVisitor func_visitor;                              /**< 函数入口访问回调，可为NULL */
};


/**
 * @brief 初始化访问者，将所有回调置为NULL
 * @param v 要初始化的访问者指针
 */
LUAI_FUNC void ast_visitor_init(AstVisitor *v);


/**
 * @brief 注册表达式类型的访问回调
 * @param v 访问者指针
 * @param k 表达式类型枚举值
 * @param cb 回调函数指针
 */
LUAI_FUNC void ast_visitor_on_expr(AstVisitor *v, AstExprKind k, AstExprVisitor cb);


/**
 * @brief 注册语句类型的访问回调
 * @param v 访问者指针
 * @param k 语句类型枚举值
 * @param cb 回调函数指针
 */
LUAI_FUNC void ast_visitor_on_stmt(AstVisitor *v, AstStmtKind k, AstStmtVisitor cb);


/**
 * @brief 注册函数入口的访问回调
 * @param v 访问者指针
 * @param cb 回调函数指针
 */
LUAI_FUNC void ast_visitor_on_func(AstVisitor *v, AstFuncVisitor cb);


/**
 * @brief 前序遍历表达式节点
 * @param v 访问者指针
 * @param ctx 用户上下文
 * @param e 表达式节点（可为NULL）
 * @return 遍历结果
 */
LUAI_FUNC AstVisitResult ast_walk_expr(AstVisitor *v, AstVisitorContext ctx, AstExpr *e);


/**
 * @brief 前序遍历语句节点
 * @param v 访问者指针
 * @param ctx 用户上下文
 * @param s 语句节点（可为NULL）
 * @return 遍历结果
 */
LUAI_FUNC AstVisitResult ast_walk_stmt(AstVisitor *v, AstVisitorContext ctx, AstStmt *s);


/**
 * @brief 按顺序遍历语句块中的所有语句
 * @param v 访问者指针
 * @param ctx 用户上下文
 * @param blk 语句块指针
 * @return 遍历结果
 */
LUAI_FUNC AstVisitResult ast_walk_block(AstVisitor *v, AstVisitorContext ctx, AstBlock *blk);


/**
 * @brief 遍历函数节点（参数默认值、函数体、子函数）
 * @param v 访问者指针
 * @param ctx 用户上下文
 * @param f 函数节点
 * @return 遍历结果
 */
LUAI_FUNC AstVisitResult ast_walk_func(AstVisitor *v, AstVisitorContext ctx, AstFunc *f);


/**
 * @brief 遍历整个编译单元（chunk）
 * @param v 访问者指针
 * @param ctx 用户上下文
 * @param chunk 编译单元指针
 * @return 遍历结果
 */
LUAI_FUNC AstVisitResult ast_walk_chunk(AstVisitor *v, AstVisitorContext ctx, AstChunk *chunk);


#endif
