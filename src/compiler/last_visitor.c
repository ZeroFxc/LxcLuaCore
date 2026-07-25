/*
** $Id: last_visitor.c $
** AST Visitor Framework - Implementation
** See Copyright Notice in lua.h
*/

#define last_visitor_c
#define LUA_CORE

#include "lprefix.h"


#include <string.h>
#include <stdio.h>

#include "lua.h"

#include "last.h"
#include "last_visitor.h"


/**
 * @brief 初始化访问者，将所有回调置为NULL
 * @param v 要初始化的访问者指针
 */
void ast_visitor_init(AstVisitor *v) {
  memset(v, 0, sizeof(AstVisitor));
}


/**
 * @brief 注册表达式类型的访问回调
 * @param v 访问者指针
 * @param k 表达式类型枚举值
 * @param cb 回调函数指针
 */
void ast_visitor_on_expr(AstVisitor *v, AstExprKind k, AstExprVisitor cb) {
  v->expr_visitors[k] = cb;
}


/**
 * @brief 注册语句类型的访问回调
 * @param v 访问者指针
 * @param k 语句类型枚举值
 * @param cb 回调函数指针
 */
void ast_visitor_on_stmt(AstVisitor *v, AstStmtKind k, AstStmtVisitor cb) {
  v->stmt_visitors[k] = cb;
}


/**
 * @brief 注册函数入口的访问回调
 * @param v 访问者指针
 * @param cb 回调函数指针
 */
void ast_visitor_on_func(AstVisitor *v, AstFuncVisitor cb) {
  v->func_visitor = cb;
}


/**
 * @brief 辅助宏：检查子节点遍历结果，TERMINATE则立即返回
 */
#define CHECK_TERMINATE(r)  do { if ((r) == AST_VISIT_TERMINATE) return AST_VISIT_TERMINATE; } while(0)


/**
 * @brief 前序遍历表达式节点
 * @param v 访问者指针
 * @param ctx 用户上下文
 * @param e 表达式节点（可为NULL）
 * @return 遍历结果
 */
AstVisitResult ast_walk_expr(AstVisitor *v, AstVisitorContext ctx, AstExpr *e) {
  int i;
  AstVisitResult r;

  if (e == NULL)
    return AST_VISIT_CONTINUE;

  /* 调用用户回调 */
  if (v->expr_visitors[e->kind] != NULL) {
    r = v->expr_visitors[e->kind](ctx, e);
    if (r == AST_VISIT_SKIP)
      return AST_VISIT_CONTINUE;
    if (r == AST_VISIT_TERMINATE)
      return AST_VISIT_TERMINATE;
  }

  /* 根据节点类型递归访问子节点 */
  switch (e->kind) {
    case AST_EXPR_BINOP:
    case AST_EXPR_NULLCOAL:
    case AST_EXPR_SPACESHIP:
    case AST_EXPR_IS:
    case AST_EXPR_IN:
    case AST_EXPR_MERGE:
      r = ast_walk_expr(v, ctx, e->u.binop.lhs);
      CHECK_TERMINATE(r);
      r = ast_walk_expr(v, ctx, e->u.binop.rhs);
      CHECK_TERMINATE(r);
      break;

    case AST_EXPR_UNOP:
    case AST_EXPR_AWAIT:
      r = ast_walk_expr(v, ctx, e->u.unop.operand);
      CHECK_TERMINATE(r);
      break;

    case AST_EXPR_CALL:
      r = ast_walk_expr(v, ctx, e->u.call.callee);
      CHECK_TERMINATE(r);
      for (i = 0; i < e->u.call.nargs; i++) {
        r = ast_walk_expr(v, ctx, e->u.call.args[i]);
        CHECK_TERMINATE(r);
      }
      break;

    case AST_EXPR_METHOD_CALL:
      r = ast_walk_expr(v, ctx, e->u.mcall.recv);
      CHECK_TERMINATE(r);
      for (i = 0; i < e->u.mcall.nargs; i++) {
        r = ast_walk_expr(v, ctx, e->u.mcall.args[i]);
        CHECK_TERMINATE(r);
      }
      break;

    case AST_EXPR_INDEX:
    case AST_EXPR_OPTCHAIN:
      r = ast_walk_expr(v, ctx, e->u.index.table);
      CHECK_TERMINATE(r);
      r = ast_walk_expr(v, ctx, e->u.index.key);
      CHECK_TERMINATE(r);
      break;

    case AST_EXPR_TABLE_CTOR:
      for (i = 0; i < e->u.table.nentries; i++) {
        AstTableEntry *entry = &e->u.table.entries[i];
        if (entry->kind == AST_TENTRY_KEY) {
          r = ast_walk_expr(v, ctx, entry->key);
          CHECK_TERMINATE(r);
        }
        r = ast_walk_expr(v, ctx, entry->value);
        CHECK_TERMINATE(r);
      }
      break;

    case AST_EXPR_MAP_CTOR:
      for (i = 0; i < e->u.map.nentries; i++) {
        AstMapEntry *entry = &e->u.map.entries[i];
        r = ast_walk_expr(v, ctx, entry->key);
        CHECK_TERMINATE(r);
        r = ast_walk_expr(v, ctx, entry->value);
        CHECK_TERMINATE(r);
      }
      break;

    case AST_EXPR_FUNC_EXPR:
    case AST_EXPR_ARROW_FUNC:
    case AST_EXPR_DICT_COMP:
    case AST_EXPR_LIST_COMP:
      r = ast_walk_func(v, ctx, e->u.func.func);
      CHECK_TERMINATE(r);
      break;

    case AST_EXPR_CONDEXPR:
      r = ast_walk_expr(v, ctx, e->u.condexpr.e1);
      CHECK_TERMINATE(r);
      r = ast_walk_expr(v, ctx, e->u.condexpr.e2);
      CHECK_TERMINATE(r);
      r = ast_walk_expr(v, ctx, e->u.condexpr.e3);
      CHECK_TERMINATE(r);
      break;

    case AST_EXPR_PAREN:
      r = ast_walk_expr(v, ctx, e->u.paren.expr);
      CHECK_TERMINATE(r);
      break;

    case AST_EXPR_RANGE:
      r = ast_walk_expr(v, ctx, e->u.range.start);
      CHECK_TERMINATE(r);
      r = ast_walk_expr(v, ctx, e->u.range.end);
      CHECK_TERMINATE(r);
      break;

    case AST_EXPR_SWITCH_EXPR:
    case AST_EXPR_SELECT_CASE:
      r = ast_walk_expr(v, ctx, e->u.switchx.cond);
      CHECK_TERMINATE(r);
      for (i = 0; i < e->u.switchx.narms; i++) {
        int j;
        for (j = 0; j < e->u.switchx.arms[i].npatterns; j++) {
          r = ast_walk_expr(v, ctx, e->u.switchx.arms[i].patterns[j]);
          CHECK_TERMINATE(r);
        }
        r = ast_walk_expr(v, ctx, e->u.switchx.arms[i].body);
        CHECK_TERMINATE(r);
      }
      r = ast_walk_expr(v, ctx, e->u.switchx.def);
      CHECK_TERMINATE(r);
      break;

    case AST_EXPR_PIPE:
    case AST_EXPR_REVPIPE:
    case AST_EXPR_SAFEPIPE:
      r = ast_walk_expr(v, ctx, e->u.pipe.recv);
      CHECK_TERMINATE(r);
      r = ast_walk_expr(v, ctx, e->u.pipe.placeholder);
      CHECK_TERMINATE(r);
      break;

    case AST_EXPR_NEW: {
      int i;
      r = ast_walk_expr(v, ctx, e->u.newexpr.class_expr);
      CHECK_TERMINATE(r);
      for (i = 0; i < e->u.newexpr.nargs; i++) {
        r = ast_walk_expr(v, ctx, e->u.newexpr.args[i]);
        CHECK_TERMINATE(r);
      }
      break;
    }

    case AST_EXPR_MATCH:
      r = ast_walk_stmt(v, ctx, e->u.match.stmt);
      CHECK_TERMINATE(r);
      break;

    case AST_EXPR_TEST_TYPE:
      r = ast_walk_expr(v, ctx, e->u.test_type.operand);
      CHECK_TERMINATE(r);
      break;

    case AST_EXPR_SLICE:
      r = ast_walk_expr(v, ctx, e->u.slice.table);
      CHECK_TERMINATE(r);
      if (e->u.slice.start) { r = ast_walk_expr(v, ctx, e->u.slice.start); CHECK_TERMINATE(r); }
      if (e->u.slice.end) { r = ast_walk_expr(v, ctx, e->u.slice.end); CHECK_TERMINATE(r); }
      if (e->u.slice.step) { r = ast_walk_expr(v, ctx, e->u.slice.step); CHECK_TERMINATE(r); }
      break;

    /* 叶子节点，无子节点 */
    case AST_EXPR_NIL:
    case AST_EXPR_TRUE:
    case AST_EXPR_FALSE:
    case AST_EXPR_INT:
    case AST_EXPR_FLT:
    case AST_EXPR_STRING:
    case AST_EXPR_INTERPSTRING:
    case AST_EXPR_REGEX:
    case AST_EXPR_VARARG:
    case AST_EXPR_IDENT:
    case AST_EXPR_SUPER:
      break;
  }

  return AST_VISIT_CONTINUE;
}


/**
 * @brief 辅助函数：遍历赋值目标列表
 */
static AstVisitResult walk_assign_targets(AstVisitor *v, AstVisitorContext ctx,
                                           AstAssignTarget *targets, int ntargets) {
  int i;
  AstVisitResult r;
  for (i = 0; i < ntargets; i++) {
    if (targets[i].kind == AST_TGT_INDEX) {
      r = ast_walk_expr(v, ctx, targets[i].as.index.table);
      CHECK_TERMINATE(r);
      r = ast_walk_expr(v, ctx, targets[i].as.index.key);
      CHECK_TERMINATE(r);
    }
  }
  return AST_VISIT_CONTINUE;
}


/**
 * @brief 递归遍历匹配模式节点
 * @param v 访问者指针
 * @param ctx 用户上下文
 * @param pat 匹配模式节点（可为NULL）
 * @return 遍历结果
 */
static AstVisitResult walk_match_pat(AstVisitor *v, AstVisitorContext ctx, AstMatchPat *pat) {
  int i;
  AstVisitResult r;

  if (pat == NULL)
    return AST_VISIT_CONTINUE;

  switch (pat->kind) {
    case AST_PAT_WILDCARD:
      break;
    case AST_PAT_LITERAL:
      r = ast_walk_expr(v, ctx, pat->u.literal);
      CHECK_TERMINATE(r);
      break;
    case AST_PAT_VARIABLE:
      break;
    case AST_PAT_RANGE:
      r = ast_walk_expr(v, ctx, pat->u.range.low);
      CHECK_TERMINATE(r);
      r = ast_walk_expr(v, ctx, pat->u.range.high);
      CHECK_TERMINATE(r);
      break;
    case AST_PAT_TYPE:
      break;
    case AST_PAT_OR:
      for (i = 0; i < pat->u.or_pat.npat; i++) {
        r = walk_match_pat(v, ctx, pat->u.or_pat.pats[i]);
        CHECK_TERMINATE(r);
      }
      break;
    case AST_PAT_TABLE:
      for (i = 0; i < pat->u.table_pat.nfields; i++) {
        r = walk_match_pat(v, ctx, pat->u.table_pat.fields[i]);
        CHECK_TERMINATE(r);
      }
      break;
  }

  return AST_VISIT_CONTINUE;
}


/**
 * @brief 前序遍历语句节点
 * @param v 访问者指针
 * @param ctx 用户上下文
 * @param s 语句节点（可为NULL）
 * @return 遍历结果
 */
AstVisitResult ast_walk_stmt(AstVisitor *v, AstVisitorContext ctx, AstStmt *s) {
  int i;
  AstVisitResult r;

  if (s == NULL)
    return AST_VISIT_CONTINUE;

  /* 调用用户回调 */
  if (v->stmt_visitors[s->kind] != NULL) {
    r = v->stmt_visitors[s->kind](ctx, s);
    if (r == AST_VISIT_SKIP)
      return AST_VISIT_CONTINUE;
    if (r == AST_VISIT_TERMINATE)
      return AST_VISIT_TERMINATE;
  }

  /* 遍历装饰器表达式 */
  for (i = 0; i < s->ndecorators; i++) {
    r = ast_walk_expr(v, ctx, s->decorators[i]);
    CHECK_TERMINATE(r);
  }

  /* 根据节点类型递归访问子节点 */
  switch (s->kind) {
    case AST_STMT_BLOCK:
    case AST_STMT_DO:
      r = ast_walk_block(v, ctx, &s->u.block.block);
      CHECK_TERMINATE(r);
      break;

    case AST_STMT_LOCAL:
      for (i = 0; i < s->u.local.nvalues; i++) {
        r = ast_walk_expr(v, ctx, s->u.local.values[i]);
        CHECK_TERMINATE(r);
      }
      break;

    case AST_STMT_ASSIGN:
      r = walk_assign_targets(v, ctx, s->u.assign.targets, s->u.assign.ntargets);
      CHECK_TERMINATE(r);
      for (i = 0; i < s->u.assign.nvalues; i++) {
        r = ast_walk_expr(v, ctx, s->u.assign.values[i]);
        CHECK_TERMINATE(r);
      }
      break;

    case AST_STMT_COMPOUND_ASSIGN:
      r = walk_assign_targets(v, ctx, s->u.compound.targets, s->u.compound.ntargets);
      CHECK_TERMINATE(r);
      r = ast_walk_expr(v, ctx, s->u.compound.value);
      CHECK_TERMINATE(r);
      break;

    case AST_STMT_EXPR:
      r = ast_walk_expr(v, ctx, s->u.expr.expr);
      CHECK_TERMINATE(r);
      break;

    case AST_STMT_IF:
      for (i = 0; i < s->u.ifstmt.narms; i++) {
        r = ast_walk_expr(v, ctx, s->u.ifstmt.arms[i].cond);
        CHECK_TERMINATE(r);
        r = ast_walk_block(v, ctx, &s->u.ifstmt.arms[i].body);
        CHECK_TERMINATE(r);
      }
      if (s->u.ifstmt.has_else) {
        r = ast_walk_block(v, ctx, &s->u.ifstmt.else_body);
        CHECK_TERMINATE(r);
      }
      break;

    case AST_STMT_WHILE:
      r = ast_walk_expr(v, ctx, s->u.whilestmt.cond);
      CHECK_TERMINATE(r);
      r = ast_walk_block(v, ctx, &s->u.whilestmt.body);
      CHECK_TERMINATE(r);
      if (s->u.whilestmt.has_else) {
        r = ast_walk_block(v, ctx, &s->u.whilestmt.else_body);
        CHECK_TERMINATE(r);
      }
      break;

    case AST_STMT_REPEAT:
      r = ast_walk_block(v, ctx, &s->u.whilestmt.body);
      CHECK_TERMINATE(r);
      r = ast_walk_expr(v, ctx, s->u.whilestmt.cond);
      CHECK_TERMINATE(r);
      break;

    case AST_STMT_FOR_NUM:
      r = ast_walk_expr(v, ctx, s->u.fornum.start);
      CHECK_TERMINATE(r);
      r = ast_walk_expr(v, ctx, s->u.fornum.stop);
      CHECK_TERMINATE(r);
      if (s->u.fornum.step != NULL) {
        r = ast_walk_expr(v, ctx, s->u.fornum.step);
        CHECK_TERMINATE(r);
      }
      r = ast_walk_block(v, ctx, &s->u.fornum.body);
      CHECK_TERMINATE(r);
      if (s->u.fornum.has_else) {
        r = ast_walk_block(v, ctx, &s->u.fornum.else_body);
        CHECK_TERMINATE(r);
      }
      break;

    case AST_STMT_FOR_GEN:
      for (i = 0; i < s->u.forgen.nexprs; i++) {
        r = ast_walk_expr(v, ctx, s->u.forgen.exprs[i]);
        CHECK_TERMINATE(r);
      }
      r = ast_walk_block(v, ctx, &s->u.forgen.body);
      CHECK_TERMINATE(r);
      if (s->u.forgen.has_else) {
        r = ast_walk_block(v, ctx, &s->u.forgen.else_body);
        CHECK_TERMINATE(r);
      }
      break;

    case AST_STMT_RETURN:
      for (i = 0; i < s->u.retstmt.nvalues; i++) {
        r = ast_walk_expr(v, ctx, s->u.retstmt.values[i]);
        CHECK_TERMINATE(r);
      }
      break;

    case AST_STMT_LOCAL_FUNC:
      r = ast_walk_func(v, ctx, s->u.localfunc.func);
      CHECK_TERMINATE(r);
      break;

    case AST_STMT_GLOBAL:
      for (i = 0; i < s->u.global.nvalues; i++) {
        r = ast_walk_expr(v, ctx, s->u.global.values[i]);
        CHECK_TERMINATE(r);
      }
      break;

    case AST_STMT_SWITCH:
      r = ast_walk_expr(v, ctx, s->u.switchstmt.cond);
      CHECK_TERMINATE(r);
      for (i = 0; i < s->u.switchstmt.ncases; i++) {
        int j;
        for (j = 0; j < s->u.switchstmt.cases[i].npatterns; j++) {
          r = ast_walk_expr(v, ctx, s->u.switchstmt.cases[i].patterns[j]);
          CHECK_TERMINATE(r);
        }
        r = ast_walk_block(v, ctx, &s->u.switchstmt.cases[i].body);
        CHECK_TERMINATE(r);
      }
      if (s->u.switchstmt.has_default) {
        r = ast_walk_block(v, ctx, &s->u.switchstmt.default_body);
        CHECK_TERMINATE(r);
      }
      break;

    case AST_STMT_INCR_DECR:
      if (s->u.incr.target->kind == AST_TGT_INDEX) {
        r = ast_walk_expr(v, ctx, s->u.incr.target->as.index.table);
        CHECK_TERMINATE(r);
        r = ast_walk_expr(v, ctx, s->u.incr.target->as.index.key);
        CHECK_TERMINATE(r);
      }
      break;

    case AST_STMT_TRY:
      r = ast_walk_block(v, ctx, &s->u.trycatch.body);
      CHECK_TERMINATE(r);
      r = ast_walk_expr(v, ctx, s->u.trycatch.catch_var);
      CHECK_TERMINATE(r);
      r = ast_walk_block(v, ctx, &s->u.trycatch.catch_body);
      CHECK_TERMINATE(r);
      r = ast_walk_block(v, ctx, &s->u.trycatch.finally_body);
      CHECK_TERMINATE(r);
      break;

    case AST_STMT_CATCH:
      r = ast_walk_expr(v, ctx, s->u.trycatch.catch_var);
      CHECK_TERMINATE(r);
      r = ast_walk_block(v, ctx, &s->u.trycatch.catch_body);
      CHECK_TERMINATE(r);
      break;

    case AST_STMT_FINALLY:
      r = ast_walk_block(v, ctx, &s->u.trycatch.finally_body);
      CHECK_TERMINATE(r);
      break;

    case AST_STMT_THROW:
      r = ast_walk_expr(v, ctx, s->u.throwstmt.expr);
      CHECK_TERMINATE(r);
      break;

    case AST_STMT_DEFER:
      r = ast_walk_block(v, ctx, &s->u.deferstmt.body);
      CHECK_TERMINATE(r);
      break;

    case AST_STMT_NAMESPACE:
    case AST_STMT_STRUCT:
    case AST_STMT_SUPERSTRUCT:
    case AST_STMT_TRAIT:
    case AST_STMT_INTERFACE:
      r = ast_walk_block(v, ctx, &s->u.nsstruct.body);
      CHECK_TERMINATE(r);
      break;

    case AST_STMT_CLASS:
      /* 遍历结构化成员 */
      if (s->u.classstmt.members != NULL) {
        for (i = 0; i < s->u.classstmt.nmembers; i++) {
          AstClassMember *m = &s->u.classstmt.members[i];
          if (m->kind != AST_MEMBER_PROPERTY && m->u.method_func != NULL) {
            r = ast_walk_func(v, ctx, m->u.method_func);
            CHECK_TERMINATE(r);
          } else if (m->kind == AST_MEMBER_PROPERTY && m->u.property_value != NULL) {
            r = ast_walk_expr(v, ctx, m->u.property_value);
            CHECK_TERMINATE(r);
          }
        }
      } else {
        /* 兼容旧格式：遍历 body */
        r = ast_walk_block(v, ctx, &s->u.classstmt.body);
        CHECK_TERMINATE(r);
      }
      break;

    case AST_STMT_MATCH: {
      int j;
      r = ast_walk_expr(v, ctx, s->u.matchstmt.control);
      CHECK_TERMINATE(r);
      for (j = 0; j < s->u.matchstmt.narms; j++) {
        AstMatchArm *arm = &s->u.matchstmt.arms[j];
        r = walk_match_pat(v, ctx, arm->pattern);
        CHECK_TERMINATE(r);
        if (arm->guard) {
          r = ast_walk_expr(v, ctx, arm->guard);
          CHECK_TERMINATE(r);
        }
        if (arm->is_arrow) {
          r = ast_walk_expr(v, ctx, arm->body_expr);
          CHECK_TERMINATE(r);
        } else {
          r = ast_walk_block(v, ctx, &arm->body_block);
          CHECK_TERMINATE(r);
        }
      }
      break;
    }
    case AST_STMT_WITH:
      if (s->u.withstmt.target) {
        r = ast_walk_expr(v, ctx, s->u.withstmt.target);
        CHECK_TERMINATE(r);
      }
      r = ast_walk_block(v, ctx, &s->u.withstmt.body);
      CHECK_TERMINATE(r);
      break;
    case AST_STMT_ASM:
      /* asm 节点只包含原始文本，无子节点可遍历 */
      break;
    case AST_STMT_CONCEPT:
    case AST_STMT_COMMAND:
    case AST_STMT_KEYWORD:
    case AST_STMT_OPERATOR:
      r = ast_walk_block(v, ctx, &s->u.nsstruct.body);
      CHECK_TERMINATE(r);
      break;

    case AST_STMT_ENUM: {
      int i;
      for (i = 0; i < s->u.enumstmt.nentries; i++) {
        AstEnumEntry *entry = &s->u.enumstmt.entries[i];
        if (entry->value_expr) {
          r = ast_walk_expr(v, ctx, entry->value_expr);
          CHECK_TERMINATE(r);
        }
      }
      break;
    }

    case AST_STMT_EXPORT:
      /* export 在 parse 阶段展开为具体语句（local/struct/enum/class 等），
       * 不会生成 AST_STMT_EXPORT 节点，此处保留作为防御性处理 */
      break;
    case AST_STMT_USING:
      /* using 节点仅包含字符串字段（name, last_member），无子节点需要递归 */
      break;

    case AST_STMT_TAKE:
      r = ast_walk_expr(v, ctx, s->u.take.source);
      CHECK_TERMINATE(r);
      if (s->u.take.defaults) {
        int i;
        for (i = 0; i < s->u.take.nvars; i++) {
          if (s->u.take.defaults[i]) {
            r = ast_walk_expr(v, ctx, s->u.take.defaults[i]);
            CHECK_TERMINATE(r);
          }
        }
      }
      break;

    case AST_STMT_CONSTEXPR:
      r = ast_walk_expr(v, ctx, s->u.constexpr_stmt.cond);
      CHECK_TERMINATE(r);
      r = ast_walk_block(v, ctx, &s->u.constexpr_stmt.body);
      CHECK_TERMINATE(r);
      break;

    /* 叶子节点，无子节点 */
    case AST_STMT_BREAK:
    case AST_STMT_CONTINUE:
    case AST_STMT_GOTO:
    case AST_STMT_LABEL:
    case AST_STMT_EMPTY:
      break;
  }

  return AST_VISIT_CONTINUE;
}


/**
 * @brief 按顺序遍历语句块中的所有语句
 * @param v 访问者指针
 * @param ctx 用户上下文
 * @param blk 语句块指针
 * @return 遍历结果
 */
AstVisitResult ast_walk_block(AstVisitor *v, AstVisitorContext ctx, AstBlock *blk) {
  int i;
  AstVisitResult r;

  if (blk == NULL || blk->items == NULL)
    return AST_VISIT_CONTINUE;

  for (i = 0; i < blk->count; i++) {
    r = ast_walk_stmt(v, ctx, blk->items[i]);
    CHECK_TERMINATE(r);
  }

  return AST_VISIT_CONTINUE;
}


/**
 * @brief 遍历函数节点（参数默认值、函数体、子函数）
 * @param v 访问者指针
 * @param ctx 用户上下文
 * @param f 函数节点
 * @return 遍历结果
 */
AstVisitResult ast_walk_func(AstVisitor *v, AstVisitorContext ctx, AstFunc *f) {
  int i;
  AstVisitResult r;
  int skip_body = 0;

  if (f == NULL)
    return AST_VISIT_CONTINUE;

  /* 调用函数入口回调 */
  if (v->func_visitor != NULL) {
    r = v->func_visitor(ctx, f);
    if (r == AST_VISIT_TERMINATE)
      return AST_VISIT_TERMINATE;
    if (r == AST_VISIT_SKIP)
      skip_body = 1;
  }

  if (skip_body)
    return AST_VISIT_CONTINUE;

  /* 遍历参数默认值 */
  for (i = 0; i < f->nparams; i++) {
    if (f->params[i].default_value != NULL) {
      r = ast_walk_expr(v, ctx, f->params[i].default_value);
      CHECK_TERMINATE(r);
    }
  }

  /* 遍历泛型类型约束（TypeHint 不含子表达式，仅遍历引用） */
  if (f->generic_constraints != NULL) {
    for (i = 0; i < f->ngeneric_params; i++) {
      (void)f->generic_constraints[i]; /* 约束已存储，供外部访问 */
    }
  }

  /* 遍历函数体 */
  r = ast_walk_block(v, ctx, &f->body);
  CHECK_TERMINATE(r);

  /* 递归遍历子函数 */
  for (i = 0; i < f->nchild_funcs; i++) {
    r = ast_walk_func(v, ctx, f->child_funcs[i]);
    CHECK_TERMINATE(r);
  }

  return AST_VISIT_CONTINUE;
}


/**
 * @brief 遍历整个编译单元（chunk）
 * @param v 访问者指针
 * @param ctx 用户上下文
 * @param chunk 编译单元指针
 * @return 遍历结果
 */
AstVisitResult ast_walk_chunk(AstVisitor *v, AstVisitorContext ctx, AstChunk *chunk) {
  if (chunk == NULL || chunk->main_func == NULL)
    return AST_VISIT_CONTINUE;

  return ast_walk_func(v, ctx, chunk->main_func);
}
