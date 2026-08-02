/*
** $Id: last_serialize.c $
** AST Serialization/Deserialization to/from Lua tables
** See Copyright Notice in lua.h
*/

#define last_serialize_c
#define LUA_CORE

#include "lprefix.h"

#include <string.h>
#include <stdio.h>

#include "lua.h"

#include "lapi.h"
#include "lauxlib.h"
#include "lmem.h"
#include "lobject.h"
#include "lstate.h"
#include "lstring.h"
#include "ltable.h"

#include "last.h"
#include "last_serialize.h"


/* ========== 辅助函数 ========== */

/* 将 Lua table 推入栈顶 */
#define push_table(L)  lua_newtable(L)

/* 设置 table 的字符串字段 */
static void setstrfield(lua_State *L, const char *k, const char *v) {
  lua_pushstring(L, v);
  lua_setfield(L, -2, k);
}

/* 设置 table 的整数字段 */
static void setintfield(lua_State *L, const char *k, lua_Integer v) {
  lua_pushinteger(L, v);
  lua_setfield(L, -2, k);
}

/* 设置 table 的布尔字段 */
static void setboolfield(lua_State *L, const char *k, int v) {
  lua_pushboolean(L, v);
  lua_setfield(L, -2, k);
}

/* 设置 table 的第 n 个数组元素（从 1 开始） */
static void setarrayelem(lua_State *L, int n) {
  lua_rawseti(L, -2, n);
}


/* ========== 运算符名称映射 ========== */

/* 二元运算符 → 字符串 */
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
    case AST_BIN_SAFEPIPE: return "?|>";
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
    case AST_BIN_CASE: return "case";
    case AST_BIN_INFIX: return "infix";
    case AST_BIN_MERGE: return "merge";
    case AST_BIN_AS: return "as";
    default: return "?";
  }
}

/* 一元运算符 → 字符串 */
static const char *unop_name(AstUnOp op) {
  switch (op) {
    case AST_UN_MINUS: return "-";
    case AST_UN_BNOT: return "~";
    case AST_UN_NOT: return "not";
    case AST_UN_LEN: return "#";
    case AST_UN_AWAIT: return "await";
    case AST_UN_TEST_Z: return "-z";
    case AST_UN_TEST_N: return "-n";
    case AST_UN_TEST_NIL: return "-nil";
    case AST_UN_TEST_BOOL: return "-bool";
    case AST_UN_TEST_FUNC: return "-func";
    default: return "?";
  }
}

/* AstExprKind → 字符串（去掉 AST_EXPR_ 前缀，转小写） */
static const char *expr_kind_name(AstExprKind kind) {
  switch (kind) {
    case AST_EXPR_NIL: return "nil";
    case AST_EXPR_TRUE: return "true";
    case AST_EXPR_FALSE: return "false";
    case AST_EXPR_INT: return "int";
    case AST_EXPR_FLT: return "float";
    case AST_EXPR_STRING: return "string";
    case AST_EXPR_INTERPSTRING: return "interpstring";
    case AST_EXPR_REGEX: return "regex";
    case AST_EXPR_VARARG: return "vararg";
    case AST_EXPR_IDENT: return "ident";
    case AST_EXPR_BINOP: return "binop";
    case AST_EXPR_UNOP: return "unop";
    case AST_EXPR_CALL: return "call";
    case AST_EXPR_METHOD_CALL: return "methodcall";
    case AST_EXPR_INDEX: return "index";
    case AST_EXPR_TABLE_CTOR: return "table";
    case AST_EXPR_MAP_CTOR: return "map";
    case AST_EXPR_FUNC_EXPR: return "function";
    case AST_EXPR_ARROW_FUNC: return "arrowfunc";
    case AST_EXPR_AWAIT: return "await";
    case AST_EXPR_PIPE: return "pipe";
    case AST_EXPR_REVPIPE: return "revpipe";
    case AST_EXPR_SAFEPIPE: return "safepipe";
    case AST_EXPR_NULLCOAL: return "nullcoal";
    case AST_EXPR_SPACESHIP: return "spaceship";
    case AST_EXPR_IS: return "is";
    case AST_EXPR_IN: return "in";
    case AST_EXPR_MERGE: return "merge";
    case AST_EXPR_CONDEXPR: return "condexpr";
    case AST_EXPR_PAREN: return "paren";
    case AST_EXPR_OPTCHAIN: return "optchain";
    case AST_EXPR_RANGE: return "range";
    case AST_EXPR_SUPER: return "super";
    case AST_EXPR_SWITCH_EXPR: return "switch";
    case AST_EXPR_SELECT_CASE: return "selectcase";
    case AST_EXPR_METHOD_REF: return "methodref";
    case AST_EXPR_NEW: return "new";
    case AST_EXPR_MATCH: return "match";
    case AST_EXPR_TEST_TYPE: return "testtype";
    case AST_EXPR_EMBED: return "embed";
    case AST_EXPR_OBJECT: return "object";
    case AST_EXPR_SLICE: return "slice";
    case AST_EXPR_DICT_COMP: return "dictcomp";
    case AST_EXPR_LIST_COMP: return "listcomp";
    case AST_EXPR_SPREAD: return "spread";
    case AST_EXPR_WALRUS: return "walrus";
    case AST_EXPR_ASTPARSER: return "astparser";
    default: return "unknown";
  }
}

/* AstStmtKind → 字符串（去掉 AST_STMT_ 前缀，转小写） */
static const char *stmt_kind_name(AstStmtKind kind) {
  switch (kind) {
    case AST_STMT_BLOCK: return "block";
    case AST_STMT_LOCAL: return "local";
    case AST_STMT_ASSIGN: return "assign";
    case AST_STMT_EXPR: return "expr";
    case AST_STMT_IF: return "if";
    case AST_STMT_WHILE: return "while";
    case AST_STMT_REPEAT: return "repeat";
    case AST_STMT_FOR_NUM: return "fornum";
    case AST_STMT_FOR_GEN: return "forgen";
    case AST_STMT_DO: return "do";
    case AST_STMT_RETURN: return "return";
    case AST_STMT_BREAK: return "break";
    case AST_STMT_CONTINUE: return "continue";
    case AST_STMT_GOTO: return "goto";
    case AST_STMT_LABEL: return "label";
    case AST_STMT_SWITCH: return "switch";
    case AST_STMT_LOCAL_FUNC: return "localfunc";
    case AST_STMT_GLOBAL: return "global";
    case AST_STMT_TRY: return "try";
    case AST_STMT_CATCH: return "catch";
    case AST_STMT_FINALLY: return "finally";
    case AST_STMT_THROW: return "throw";
    case AST_STMT_DEFER: return "defer";
    case AST_STMT_USING: return "using";
    case AST_STMT_NAMESPACE: return "namespace";
    case AST_STMT_STRUCT: return "struct";
    case AST_STMT_SUPERSTRUCT: return "superstruct";
    case AST_STMT_ENUM: return "enum";
    case AST_STMT_CLASS: return "class";
    case AST_STMT_TRAIT: return "trait";
    case AST_STMT_INTERFACE: return "interface";
    case AST_STMT_MATCH: return "match";
    case AST_STMT_WITH: return "with";
    case AST_STMT_ASM: return "asm";
    case AST_STMT_CONCEPT: return "concept";
    case AST_STMT_EXPORT: return "export";
    case AST_STMT_WHILE_LET: return "whilelet";
    case AST_STMT_COMPOUND_ASSIGN: return "compound";
    case AST_STMT_INCR_DECR: return "incr";
    case AST_STMT_GUARD: return "guard";
    case AST_STMT_COMMAND: return "command";
    case AST_STMT_KEYWORD: return "keyword";
    case AST_STMT_OPERATOR: return "operator";
    case AST_STMT_EMPTY: return "empty";
    case AST_STMT_TAKE: return "take";
    case AST_STMT_CONSTEXPR: return "constexpr";
    default: return "unknown";
  }
}


/* ========== 序列化 ========== */

/* 前向声明 */
static void ast_serialize_expr(lua_State *L, AstExpr *e);
static void ast_serialize_stmt(lua_State *L, AstStmt *s);
static void ast_serialize_block(lua_State *L, AstBlock *blk);
static void ast_serialize_match_pat(lua_State *L, AstMatchPat *pat);
static AstMatchPat *ast_deserialize_match_pat(lua_State *L, AstPool *pool, int idx);

/* 创建占位节点 */
static void push_placeholder(lua_State *L, int line) {
  push_table(L);
  setstrfield(L, "kind", "unknown");
  setintfield(L, "line", line);
}

/* 序列化表达式 */
static void ast_serialize_expr(lua_State *L, AstExpr *e) {
  if (e == NULL) {
    push_placeholder(L, 0);
    return;
  }
  int line = e->node.line;
  push_table(L);
  setstrfield(L, "kind", expr_kind_name(e->kind));
  setintfield(L, "line", line);

  switch (e->kind) {
    case AST_EXPR_NIL:
      break;

    case AST_EXPR_TRUE:
      setboolfield(L, "value", 1);
      break;

    case AST_EXPR_FALSE:
      setboolfield(L, "value", 0);
      break;

    case AST_EXPR_INT:
      setintfield(L, "value", e->u.ival);
      break;

    case AST_EXPR_FLT:
      lua_pushnumber(L, e->u.nval);
      lua_setfield(L, -2, "value");
      break;

    case AST_EXPR_STRING:
    case AST_EXPR_REGEX:
    case AST_EXPR_INTERPSTRING:
      if (e->u.strval) {
        lua_pushstring(L, getstr(e->u.strval));
        lua_setfield(L, -2, "value");
      }
      break;

    case AST_EXPR_VARARG:
      break;

    case AST_EXPR_IDENT:
      if (e->u.strval) {
        lua_pushstring(L, getstr(e->u.strval));
        lua_setfield(L, -2, "name");
      }
      break;

    case AST_EXPR_BINOP:
      setstrfield(L, "op", binop_name(e->u.binop.op));
      ast_serialize_expr(L, e->u.binop.lhs);
      lua_setfield(L, -2, "lhs");
      ast_serialize_expr(L, e->u.binop.rhs);
      lua_setfield(L, -2, "rhs");
      break;

    case AST_EXPR_UNOP:
      setstrfield(L, "op", unop_name(e->u.unop.op));
      ast_serialize_expr(L, e->u.unop.operand);
      lua_setfield(L, -2, "operand");
      break;

    case AST_EXPR_CALL:
      ast_serialize_expr(L, e->u.call.callee);
      lua_setfield(L, -2, "callee");
      {
        lua_newtable(L);
        for (int i = 0; i < e->u.call.nargs; i++) {
          ast_serialize_expr(L, e->u.call.args[i]);
          setarrayelem(L, i + 1);
        }
        lua_setfield(L, -2, "args");
      }
      break;

    case AST_EXPR_METHOD_CALL:
      ast_serialize_expr(L, e->u.mcall.recv);
      lua_setfield(L, -2, "recv");
      if (e->u.mcall.method) {
        lua_pushstring(L, getstr(e->u.mcall.method));
        lua_setfield(L, -2, "method");
      }
      {
        lua_newtable(L);
        for (int i = 0; i < e->u.mcall.nargs; i++) {
          ast_serialize_expr(L, e->u.mcall.args[i]);
          setarrayelem(L, i + 1);
        }
        lua_setfield(L, -2, "args");
      }
      break;

    case AST_EXPR_INDEX:
      ast_serialize_expr(L, e->u.index.table);
      lua_setfield(L, -2, "table");
      ast_serialize_expr(L, e->u.index.key);
      lua_setfield(L, -2, "key");
      setboolfield(L, "is_opt", e->u.index.is_opt);
      break;

    case AST_EXPR_TABLE_CTOR: {
      lua_newtable(L);
      for (int i = 0; i < e->u.table.nentries; i++) {
        AstTableEntry *entry = &e->u.table.entries[i];
        push_table(L);
        if (entry->key) {
          ast_serialize_expr(L, entry->key);
          lua_setfield(L, -2, "key");
        }
        ast_serialize_expr(L, entry->value);
        lua_setfield(L, -2, "value");
        setarrayelem(L, i + 1);
      }
      lua_setfield(L, -2, "entries");
      setintfield(L, "narr", e->u.table.narr);
      setintfield(L, "nrec", e->u.table.nrec);
      break;
    }

    case AST_EXPR_FUNC_EXPR:
    case AST_EXPR_ARROW_FUNC: {
      if (e->u.func.func) {
        AstFunc *f = e->u.func.func;
        /* params */
        lua_newtable(L);
        for (int i = 0; i < f->nparams; i++) {
          lua_pushstring(L, getstr(f->params[i].name));
          setarrayelem(L, i + 1);
        }
        lua_setfield(L, -2, "params");
        setboolfield(L, "is_vararg", f->is_vararg);
        ast_serialize_block(L, &f->body);
        lua_setfield(L, -2, "body");
      }
      break;
    }

    case AST_EXPR_CONDEXPR:
      ast_serialize_expr(L, e->u.condexpr.e1);
      lua_setfield(L, -2, "cond");
      ast_serialize_expr(L, e->u.condexpr.e2);
      lua_setfield(L, -2, "then_expr");
      ast_serialize_expr(L, e->u.condexpr.e3);
      lua_setfield(L, -2, "else_expr");
      break;

    case AST_EXPR_PAREN:
      ast_serialize_expr(L, e->u.paren.expr);
      lua_setfield(L, -2, "expr");
      break;

    case AST_EXPR_RANGE:
      ast_serialize_expr(L, e->u.range.start);
      lua_setfield(L, -2, "start");
      ast_serialize_expr(L, e->u.range.end);
      lua_setfield(L, -2, "end");
      break;

    case AST_EXPR_PIPE:
    case AST_EXPR_REVPIPE:
    case AST_EXPR_SAFEPIPE:
      ast_serialize_expr(L, e->u.pipe.recv);
      lua_setfield(L, -2, "recv");
      ast_serialize_expr(L, e->u.pipe.placeholder);
      lua_setfield(L, -2, "placeholder");
      break;

    case AST_EXPR_AWAIT:
      /* await 作为一元操作处理 */
      break;

    case AST_EXPR_NULLCOAL:
      /* 内部使用 binop 结构 */
      break;

    case AST_EXPR_SPACESHIP:
      /* 内部使用 binop 结构 */
      break;

    case AST_EXPR_IS:
    case AST_EXPR_IN:
    case AST_EXPR_MERGE:
      /* 内部使用 binop 结构 */
      break;

    case AST_EXPR_OPTCHAIN:
      /* 可选链表达式，占位 */
      break;

    case AST_EXPR_SUPER:
      if (e->u.super.obj) {
        ast_serialize_expr(L, e->u.super.obj);
        lua_setfield(L, -2, "obj");
      }
      if (e->u.super.method) {
        lua_pushstring(L, getstr(e->u.super.method));
        lua_setfield(L, -2, "method");
      }
      break;

    case AST_EXPR_SWITCH_EXPR:
      ast_serialize_expr(L, e->u.switchx.cond);
      lua_setfield(L, -2, "cond");
      /* 序列化 arms 数组 */
      {
        lua_newtable(L);
        for (int i = 0; i < e->u.switchx.narms; i++) {
          AstCaseArm *arm = &e->u.switchx.arms[i];
          push_table(L);
          /* 序列化多值模式 */
          lua_newtable(L);
          for (int p = 0; p < arm->npatterns; p++) {
            ast_serialize_expr(L, arm->patterns[p]);
            setarrayelem(L, p + 1);
          }
          lua_setfield(L, -2, "patterns");
          /* 序列化 body */
          ast_serialize_expr(L, arm->body);
          lua_setfield(L, -2, "body");
          setarrayelem(L, i + 1);
        }
        lua_setfield(L, -2, "arms");
      }
      /* 序列化 default */
      if (e->u.switchx.def) {
        ast_serialize_expr(L, e->u.switchx.def);
        lua_setfield(L, -2, "def");
      }
      break;

    case AST_EXPR_SELECT_CASE:
      break;

    case AST_EXPR_METHOD_REF:
      if (e->u.method_ref.recv) {
        ast_serialize_expr(L, e->u.method_ref.recv);
        lua_setfield(L, -2, "recv");
      }
      if (e->u.method_ref.method) {
        lua_pushstring(L, getstr(e->u.method_ref.method));
        lua_setfield(L, -2, "method");
      }
      break;

    case AST_EXPR_NEW:
      ast_serialize_expr(L, e->u.newexpr.class_expr);
      lua_setfield(L, -2, "class_expr");
      break;

    case AST_EXPR_MATCH: {
      /* 委托给 match 语句序列化 */
      AstStmt *stmt = e->u.match.stmt;
      stmt->u.matchstmt.is_expr = 1;
      ast_serialize_stmt(L, stmt);
      break;
    }

    case AST_EXPR_TEST_TYPE:
      if (e->u.test_type.operand) {
        ast_serialize_expr(L, e->u.test_type.operand);
        lua_setfield(L, -2, "operand");
      }
      if (e->u.test_type.type_name) {
        lua_pushstring(L, getstr(e->u.test_type.type_name));
        lua_setfield(L, -2, "type_name");
      }
      break;

    case AST_EXPR_EMBED:
      if (e->u.embed.filename) {
        lua_pushstring(L, getstr(e->u.embed.filename));
        lua_setfield(L, -2, "filename");
      }
      break;

    case AST_EXPR_OBJECT:
      ast_serialize_expr(L, e->u.object.ctor);
      lua_setfield(L, -2, "ctor");
      break;

    case AST_EXPR_SLICE:
      ast_serialize_expr(L, e->u.slice.table);
      lua_setfield(L, -2, "table");
      if (e->u.slice.start) {
        ast_serialize_expr(L, e->u.slice.start);
        lua_setfield(L, -2, "start");
      }
      if (e->u.slice.end) {
        ast_serialize_expr(L, e->u.slice.end);
        lua_setfield(L, -2, "end");
      }
      if (e->u.slice.step) {
        ast_serialize_expr(L, e->u.slice.step);
        lua_setfield(L, -2, "step");
      }
      break;

    case AST_EXPR_DICT_COMP:
    case AST_EXPR_LIST_COMP:
      /* 这两个节点在 parser 里包装成一个匿名子函数（e->u.func.func），里面包含 for-gen 循环与 return _t，
         所以输出成 AST_EXPR_FUNC 形式即可，方便 unparse */
      {
        AstFunc *f = e->u.func.func;
        /* 构造一个 localfunc 形式的序列化：params=nil，is_vararg=0，输出 body 块 */
        setboolfield(L, "is_arrow", 0);
        lua_newtable(L);
        setarrayelem(L, 0); /* 占位：空 params 数组 */
        lua_setfield(L, -2, "params");
        setboolfield(L, "is_vararg", (f ? f->is_vararg : 0));
        setboolfield(L, "is_comp", 1);
        setintfield(L, "comp_kind", (int)e->kind);
        if (f) {
          ast_serialize_block(L, &f->body);
          lua_setfield(L, -2, "body");
        }
      }
      break;

    case AST_EXPR_SPREAD:
      ast_serialize_expr(L, e->u.spread.expr);
      lua_setfield(L, -2, "expr");
      break;

    case AST_EXPR_WALRUS:
      if (e->u.walrus.name) {
        lua_pushstring(L, getstr(e->u.walrus.name));
        lua_setfield(L, -2, "name");
      }
      ast_serialize_expr(L, e->u.walrus.expr);
      lua_setfield(L, -2, "expr");
      break;

    case AST_EXPR_ASTPARSER:
      /* C-level Proto and AstChunk pointers plus delay-mode ast_ref are not
       * serializable. Keep only kind="astparser" marker; deserialization
       * substitutes a placeholder ident to avoid wild-pointer access. */
      break;

    default:
      break;
  }
}

/* 序列化语句 */
static void ast_serialize_stmt(lua_State *L, AstStmt *s) {
  if (s == NULL) {
    push_placeholder(L, 0);
    return;
  }
  int line = s->node.line;
  push_table(L);
  setstrfield(L, "kind", stmt_kind_name(s->kind));
  setintfield(L, "line", line);

  switch (s->kind) {
    case AST_STMT_BLOCK:
    case AST_STMT_DO:
      ast_serialize_block(L, &s->u.block.block);
      lua_setfield(L, -2, "body");
      break;

    case AST_STMT_LOCAL: {
      lua_newtable(L);
      for (int i = 0; i < s->u.local.nnames; i++) {
        if (s->u.local.names[i]) {
          lua_pushstring(L, getstr(s->u.local.names[i]));
          setarrayelem(L, i + 1);
        }
      }
      lua_setfield(L, -2, "names");
      lua_newtable(L);
      for (int i = 0; i < s->u.local.nvalues; i++) {
        ast_serialize_expr(L, s->u.local.values[i]);
        setarrayelem(L, i + 1);
      }
      lua_setfield(L, -2, "values");
      break;
    }

    case AST_STMT_ASSIGN: {
      lua_newtable(L);
      for (int i = 0; i < s->u.assign.ntargets; i++) {
        AstAssignTarget *t = &s->u.assign.targets[i];
        push_table(L);
        if (t->kind == AST_TGT_VAR && t->as.var.name) {
          lua_pushstring(L, getstr(t->as.var.name));
          lua_setfield(L, -2, "name");
        } else if (t->kind == AST_TGT_INDEX) {
          setstrfield(L, "kind", "index");
          ast_serialize_expr(L, t->as.index.table);
          lua_setfield(L, -2, "table");
          ast_serialize_expr(L, t->as.index.key);
          lua_setfield(L, -2, "key");
        }
        setarrayelem(L, i + 1);
      }
      lua_setfield(L, -2, "targets");
      lua_newtable(L);
      for (int i = 0; i < s->u.assign.nvalues; i++) {
        ast_serialize_expr(L, s->u.assign.values[i]);
        setarrayelem(L, i + 1);
      }
      lua_setfield(L, -2, "values");
      break;
    }

    case AST_STMT_EXPR:
      ast_serialize_expr(L, s->u.expr.expr);
      lua_setfield(L, -2, "expr");
      break;

    case AST_STMT_IF: {
      lua_newtable(L);
      for (int i = 0; i < s->u.ifstmt.narms; i++) {
        AstIfArm *arm = &s->u.ifstmt.arms[i];
        push_table(L);
        if (arm->cond) {
          ast_serialize_expr(L, arm->cond);
          lua_setfield(L, -2, "cond");
        }
        ast_serialize_block(L, &arm->body);
        lua_setfield(L, -2, "body");
        setarrayelem(L, i + 1);
      }
      lua_setfield(L, -2, "arms");
      if (s->u.ifstmt.has_else) {
        ast_serialize_block(L, &s->u.ifstmt.else_body);
        lua_setfield(L, -2, "else_body");
      }
      break;
    }

    case AST_STMT_WHILE:
      ast_serialize_expr(L, s->u.whilestmt.cond);
      lua_setfield(L, -2, "cond");
      ast_serialize_block(L, &s->u.whilestmt.body);
      lua_setfield(L, -2, "body");
      break;

    case AST_STMT_REPEAT:
      ast_serialize_block(L, &s->u.whilestmt.body);
      lua_setfield(L, -2, "body");
      ast_serialize_expr(L, s->u.whilestmt.cond);
      lua_setfield(L, -2, "cond");
      break;

    case AST_STMT_FOR_NUM:
      if (s->u.fornum.var) {
        lua_pushstring(L, getstr(s->u.fornum.var));
        lua_setfield(L, -2, "var");
      }
      ast_serialize_expr(L, s->u.fornum.start);
      lua_setfield(L, -2, "start");
      ast_serialize_expr(L, s->u.fornum.stop);
      lua_setfield(L, -2, "end");
      if (s->u.fornum.step) {
        ast_serialize_expr(L, s->u.fornum.step);
        lua_setfield(L, -2, "step");
      }
      ast_serialize_block(L, &s->u.fornum.body);
      lua_setfield(L, -2, "body");
      break;

    case AST_STMT_FOR_GEN: {
      lua_newtable(L);
      for (int i = 0; i < s->u.forgen.nnames; i++) {
        if (s->u.forgen.names[i]) {
          lua_pushstring(L, getstr(s->u.forgen.names[i]));
          setarrayelem(L, i + 1);
        }
      }
      lua_setfield(L, -2, "vars");
      lua_newtable(L);
      for (int i = 0; i < s->u.forgen.nexprs; i++) {
        ast_serialize_expr(L, s->u.forgen.exprs[i]);
        setarrayelem(L, i + 1);
      }
      lua_setfield(L, -2, "exprs");
      ast_serialize_block(L, &s->u.forgen.body);
      lua_setfield(L, -2, "body");
      break;
    }

    case AST_STMT_RETURN: {
      lua_newtable(L);
      for (int i = 0; i < s->u.retstmt.nvalues; i++) {
        ast_serialize_expr(L, s->u.retstmt.values[i]);
        setarrayelem(L, i + 1);
      }
      lua_setfield(L, -2, "exprs");
      break;
    }

    case AST_STMT_BREAK:
      setintfield(L, "level", s->u.contbrk.level);
      break;

    case AST_STMT_CONTINUE:
      setintfield(L, "level", s->u.contbrk.level);
      break;

    case AST_STMT_GOTO:
      if (s->u.label.name) {
        lua_pushstring(L, getstr(s->u.label.name));
        lua_setfield(L, -2, "label");
      }
      break;

    case AST_STMT_LABEL:
      if (s->u.label.name) {
        lua_pushstring(L, getstr(s->u.label.name));
        lua_setfield(L, -2, "name");
      }
      break;

    case AST_STMT_LOCAL_FUNC: {
      if (s->u.localfunc.name) {
        lua_pushstring(L, getstr(s->u.localfunc.name));
        lua_setfield(L, -2, "name");
      }
      /* 序列化函数体 */
      if (s->u.localfunc.func) {
        AstFunc *f = s->u.localfunc.func;
        /* params */
        lua_newtable(L);
        for (int i = 0; i < f->nparams; i++) {
          lua_pushstring(L, getstr(f->params[i].name));
          setarrayelem(L, i + 1);
        }
        lua_setfield(L, -2, "params");
        setboolfield(L, "is_vararg", f->is_vararg);
        /* 函数体 */
        ast_serialize_block(L, &f->body);
        lua_setfield(L, -2, "body");
      }
      break;
    }

    case AST_STMT_SWITCH:
      ast_serialize_expr(L, s->u.switchstmt.cond);
      lua_setfield(L, -2, "cond");
      /* 序列化 cases 数组 */
      {
        lua_newtable(L);
        for (int i = 0; i < s->u.switchstmt.ncases; i++) {
          AstSwitchCase *c = &s->u.switchstmt.cases[i];
          if (c->is_default) continue; /* default 分支单独序列化 */
          push_table(L);
          setstrfield(L, "kind", "case");
          /* 序列化多值模式 */
          lua_newtable(L);
          for (int p = 0; p < c->npatterns; p++) {
            ast_serialize_expr(L, c->patterns[p]);
            setarrayelem(L, p + 1);
          }
          lua_setfield(L, -2, "patterns");
          ast_serialize_block(L, &c->body);
          lua_setfield(L, -2, "body");
          setarrayelem(L, i + 1);
        }
        lua_setfield(L, -2, "cases");
      }
      /* 序列化 default_body */
      if (s->u.switchstmt.has_default) {
        ast_serialize_block(L, &s->u.switchstmt.default_body);
        lua_setfield(L, -2, "default_body");
      }
      break;

    case AST_STMT_GLOBAL: {
      lua_newtable(L);
      for (int i = 0; i < s->u.global.nnames; i++) {
        if (s->u.global.names[i]) {
          lua_pushstring(L, getstr(s->u.global.names[i]));
          setarrayelem(L, i + 1);
        }
      }
      lua_setfield(L, -2, "names");
      break;
    }

    case AST_STMT_COMPOUND_ASSIGN:
      setstrfield(L, "op", binop_name(s->u.compound.op));
      /* 序列化 targets 数组 */
      lua_newtable(L);
      for (int i = 0; i < s->u.compound.ntargets; i++) {
        AstAssignTarget *t = &s->u.compound.targets[i];
        push_table(L);
        if (t->kind == AST_TGT_VAR && t->as.var.name) {
          lua_pushstring(L, getstr(t->as.var.name));
          lua_setfield(L, -2, "name");
        } else if (t->kind == AST_TGT_INDEX) {
          setstrfield(L, "kind", "index");
          ast_serialize_expr(L, t->as.index.table);
          lua_setfield(L, -2, "table");
          ast_serialize_expr(L, t->as.index.key);
          lua_setfield(L, -2, "key");
        }
        setarrayelem(L, i + 1);
      }
      lua_setfield(L, -2, "targets");
      /* 序列化 value */
      if (s->u.compound.value) {
        ast_serialize_expr(L, s->u.compound.value);
        lua_setfield(L, -2, "value");
      }
      break;

    case AST_STMT_INCR_DECR: {
      const char *kind_str = NULL;
      if (s->u.incr.kind == AST_INCR_PRE_INC)       kind_str = "pre_incr";
      else if (s->u.incr.kind == AST_INCR_PRE_DEC)  kind_str = "pre_decr";
      else if (s->u.incr.kind == AST_INCR_POST_INC) kind_str = "post_incr";
      else if (s->u.incr.kind == AST_INCR_POST_DEC) kind_str = "post_decr";
      if (kind_str) setstrfield(L, "op", kind_str);
      /* 序列化 target */
      if (s->u.incr.target) {
        push_table(L);
        AstAssignTarget *t = s->u.incr.target;
        if (t->kind == AST_TGT_VAR && t->as.var.name) {
          lua_pushstring(L, getstr(t->as.var.name));
          lua_setfield(L, -2, "name");
        } else if (t->kind == AST_TGT_INDEX) {
          setstrfield(L, "kind", "index");
          ast_serialize_expr(L, t->as.index.table);
          lua_setfield(L, -2, "table");
          ast_serialize_expr(L, t->as.index.key);
          lua_setfield(L, -2, "key");
        }
        lua_setfield(L, -2, "target");
      }
      break;
    }

    case AST_STMT_TRY:
      ast_serialize_block(L, &s->u.trycatch.body);
      lua_setfield(L, -2, "body");
      /* 序列化 catch_var */
      if (s->u.trycatch.catch_var) {
        ast_serialize_expr(L, s->u.trycatch.catch_var);
        lua_setfield(L, -2, "catch_var");
      }
      if (s->u.trycatch.catch_body.count > 0) {
        ast_serialize_block(L, &s->u.trycatch.catch_body);
        lua_setfield(L, -2, "catch_body");
      }
      if (s->u.trycatch.finally_body.count > 0) {
        ast_serialize_block(L, &s->u.trycatch.finally_body);
        lua_setfield(L, -2, "finally_body");
      }
      break;

    case AST_STMT_THROW:
      if (s->u.throwstmt.expr) {
        ast_serialize_expr(L, s->u.throwstmt.expr);
        lua_setfield(L, -2, "expr");
      }
      break;

    case AST_STMT_DEFER:
      ast_serialize_block(L, &s->u.deferstmt.body);
      lua_setfield(L, -2, "body");
      break;

    case AST_STMT_TAKE: {
      lua_newtable(L);
      for (int i = 0; i < s->u.take.nvars; i++) {
        if (s->u.take.varnames[i]) {
          lua_pushstring(L, getstr(s->u.take.varnames[i]));
          setarrayelem(L, i + 1);
        }
      }
      lua_setfield(L, -2, "varnames");
      if (s->u.take.source) {
        ast_serialize_expr(L, s->u.take.source);
        lua_setfield(L, -2, "source");
      }
      setboolfield(L, "is_array", s->u.take.is_array);
      break;
    }

    case AST_STMT_MATCH: {
      ast_serialize_expr(L, s->u.matchstmt.control);
      lua_setfield(L, -2, "control");
      setintfield(L, "is_expr", s->u.matchstmt.is_expr);
      /* 序列化 arms 数组 */
      lua_newtable(L);
      for (int i = 0; i < s->u.matchstmt.narms; i++) {
        AstMatchArm *arm = &s->u.matchstmt.arms[i];
        push_table(L);
        /* 序列化 pattern */
        ast_serialize_match_pat(L, arm->pattern);
        lua_setfield(L, -2, "pattern");
        /* 序列化 guard */
        if (arm->guard) {
          ast_serialize_expr(L, arm->guard);
          lua_setfield(L, -2, "guard");
        }
        /* 序列化 body */
        setintfield(L, "is_arrow", arm->is_arrow);
        if (arm->is_arrow) {
          ast_serialize_expr(L, arm->body_expr);
          lua_setfield(L, -2, "body_expr");
        } else {
          ast_serialize_block(L, &arm->body_block);
          lua_setfield(L, -2, "body_block");
        }
        setarrayelem(L, i + 1);
      }
      lua_setfield(L, -2, "arms");
      break;
    }

    /* 以下复杂类型创建占位节点 */
    case AST_STMT_CATCH:
    case AST_STMT_FINALLY:
      /* 与 try 共用 trycatch 结构；用于单独调试时不丢失字段 */
      if (s->u.trycatch.catch_var) {
        ast_serialize_expr(L, s->u.trycatch.catch_var);
        lua_setfield(L, -2, "catch_var");
      }
      ast_serialize_block(L, &s->u.trycatch.body);
      lua_setfield(L, -2, "body");
      if (s->kind == AST_STMT_CATCH) {
        ast_serialize_block(L, &s->u.trycatch.catch_body);
        lua_setfield(L, -2, "catch_body");
      }
      ast_serialize_block(L, &s->u.trycatch.finally_body);
      lua_setfield(L, -2, "finally_body");
      break;

    case AST_STMT_USING:
      setboolfield(L, "is_namespace", s->u.usingstmt.is_namespace);
      if (s->u.usingstmt.name) {
        lua_pushstring(L, getstr(s->u.usingstmt.name));
        lua_setfield(L, -2, "name");
      }
      if (s->u.usingstmt.last_member) {
        lua_pushstring(L, getstr(s->u.usingstmt.last_member));
        lua_setfield(L, -2, "last_member");
      }
      break;

    case AST_STMT_NAMESPACE:
      if (s->u.nsstruct.name) {
        lua_pushstring(L, getstr(s->u.nsstruct.name));
        lua_setfield(L, -2, "name");
      }
      ast_serialize_block(L, &s->u.nsstruct.body);
      lua_setfield(L, -2, "body");
      break;

    case AST_STMT_STRUCT:
    case AST_STMT_SUPERSTRUCT:
      if (s->u.nsstruct.name) {
        lua_pushstring(L, getstr(s->u.nsstruct.name));
        lua_setfield(L, -2, "name");
      }
      /* 若 entries 字段数组 */
      if (s->u.nsstruct.entries && s->u.nsstruct.nentries > 0) {
        lua_newtable(L);
        for (int i = 0; i < s->u.nsstruct.nentries; i++) {
          AstKVPair *ent = &s->u.nsstruct.entries[i];
          push_table(L);
          if (ent->key)   { ast_serialize_expr(L, ent->key);   lua_setfield(L, -2, "key");   }
          if (ent->value) { ast_serialize_expr(L, ent->value); lua_setfield(L, -2, "value"); }
          setarrayelem(L, i + 1);
        }
        lua_setfield(L, -2, "entries");
        setintfield(L, "nentries", s->u.nsstruct.nentries);
      } else {
        ast_serialize_block(L, &s->u.nsstruct.body);
        lua_setfield(L, -2, "body");
      }
      break;

    case AST_STMT_ENUM: {
      if (s->u.enumstmt.name) {
        lua_pushstring(L, getstr(s->u.enumstmt.name));
        lua_setfield(L, -2, "name");
      }
      setboolfield(L, "is_class", s->u.enumstmt.is_enum_class);
      setintfield(L, "nentries", s->u.enumstmt.nentries);
      if (s->u.enumstmt.entries && s->u.enumstmt.nentries > 0) {
        lua_newtable(L);
        for (int i = 0; i < s->u.enumstmt.nentries; i++) {
          AstEnumEntry *e = &s->u.enumstmt.entries[i];
          push_table(L);
          if (e->name) { lua_pushstring(L, getstr(e->name)); lua_setfield(L, -2, "name"); }
          if (e->value_expr) { ast_serialize_expr(L, e->value_expr); lua_setfield(L, -2, "value_expr"); }
          setarrayelem(L, i + 1);
        }
        lua_setfield(L, -2, "entries");
      }
      break;
    }

    case AST_STMT_CLASS:
      if (s->u.classstmt.name) {
        lua_pushstring(L, getstr(s->u.classstmt.name));
        lua_setfield(L, -2, "name");
      }
      if (s->u.classstmt.extends_names && s->u.classstmt.nextends > 0) {
        lua_newtable(L);
        for (int i = 0; i < s->u.classstmt.nextends; i++) {
          lua_pushstring(L, getstr(s->u.classstmt.extends_names[i]));
          setarrayelem(L, i + 1);
        }
        lua_setfield(L, -2, "extends_names");
        setintfield(L, "nextends", s->u.classstmt.nextends);
      }
      if (s->u.classstmt.implements && s->u.classstmt.nimplements > 0) {
        lua_newtable(L);
        for (int i = 0; i < s->u.classstmt.nimplements; i++) {
          lua_pushstring(L, getstr(s->u.classstmt.implements[i]));
          setarrayelem(L, i + 1);
        }
        lua_setfield(L, -2, "implements");
        setintfield(L, "nimplements", s->u.classstmt.nimplements);
      }
      if (s->u.classstmt.use_traits && s->u.classstmt.nuse_traits > 0) {
        lua_newtable(L);
        for (int i = 0; i < s->u.classstmt.nuse_traits; i++) {
          lua_pushstring(L, getstr(s->u.classstmt.use_traits[i]));
          setarrayelem(L, i + 1);
        }
        lua_setfield(L, -2, "use_traits");
        setintfield(L, "nuse_traits", s->u.classstmt.nuse_traits);
      }
      setintfield(L, "class_flags", s->u.classstmt.class_flags);
      /* 泛型参数 */
      if (s->u.classstmt.generic_params && s->u.classstmt.ngeneric_params > 0) {
        lua_newtable(L);
        for (int i = 0; i < s->u.classstmt.ngeneric_params; i++) {
          lua_pushstring(L, getstr(s->u.classstmt.generic_params[i]));
          setarrayelem(L, i + 1);
        }
        lua_setfield(L, -2, "generic_params");
        setintfield(L, "ngeneric_params", s->u.classstmt.ngeneric_params);
      }
      /* members 优先 */
      if (s->u.classstmt.members && s->u.classstmt.nmembers > 0) {
        lua_newtable(L);
        for (int i = 0; i < s->u.classstmt.nmembers; i++) {
          AstClassMember *m = &s->u.classstmt.members[i];
          push_table(L);
          setintfield(L, "kind", (int)m->kind);
          setintfield(L, "access", (int)m->access);
          setboolfield(L, "is_static", m->is_static);
          if (m->name) { lua_pushstring(L, getstr(m->name)); lua_setfield(L, -2, "name"); }
          if (m->kind == AST_MEMBER_PROPERTY && m->u.property_value) {
            ast_serialize_expr(L, m->u.property_value);
            lua_setfield(L, -2, "property_value");
          } else if (m->kind == AST_MEMBER_METHOD && m->u.method_func) {
            /* 方法：临时构造一个 AstStmt localfunc，复用 stmt 序列化写出 params / body / is_vararg */
            AstStmt tmp_st;
            memset(&tmp_st, 0, sizeof(tmp_st));
            tmp_st.kind = AST_STMT_LOCAL_FUNC;
            tmp_st.u.localfunc.name = m->name;
            tmp_st.u.localfunc.func = m->u.method_func;
            ast_serialize_stmt(L, &tmp_st);  /* 结果包含 name/params/is_vararg/body 等字段 */
            lua_setfield(L, -2, "method_func");
          }
          setarrayelem(L, i + 1);
        }
        lua_setfield(L, -2, "members");
        setintfield(L, "nmembers", s->u.classstmt.nmembers);
      } else {
        ast_serialize_block(L, &s->u.classstmt.body);
        lua_setfield(L, -2, "body");
      }
      break;

    case AST_STMT_WITH:
      if (s->u.withstmt.target) {
        ast_serialize_expr(L, s->u.withstmt.target); lua_setfield(L, -2, "target");
      }
      ast_serialize_block(L, &s->u.withstmt.body);
      lua_setfield(L, -2, "body");
      break;

    case AST_STMT_ASM:
      if (s->u.asmstmt.raw_body) {
        lua_pushlstring(L, getstr(s->u.asmstmt.raw_body), tsslen(s->u.asmstmt.raw_body));
        lua_setfield(L, -2, "raw_body");
      }
      break;

    case AST_STMT_CONCEPT:
      /* concept 与 nsstruct 复用 nsstruct 与 typed 用 typed name+body 包 */
      if (s->u.nsstruct.name) {
        lua_pushstring(L, getstr(s->u.nsstruct.name));
        lua_setfield(L, -2, "name");
      }
      ast_serialize_block(L, &s->u.nsstruct.body);
      lua_setfield(L, -2, "body");
      break;

    case AST_STMT_EXPORT:
      break;

    case AST_STMT_WHILE_LET:
      setintfield(L, "nnames", s->u.whilelet.nnames);
      if (s->u.whilelet.names && s->u.whilelet.nnames > 0) {
        lua_newtable(L);
        for (int i = 0; i < s->u.whilelet.nnames; i++) {
          lua_pushstring(L, getstr(s->u.whilelet.names[i]));
          setarrayelem(L, i + 1);
        }
        lua_setfield(L, -2, "names");
      }
      if (s->u.whilelet.expr) {
        ast_serialize_expr(L, s->u.whilelet.expr);
        lua_setfield(L, -2, "expr");
      }
      ast_serialize_block(L, &s->u.whilelet.body);
      lua_setfield(L, -2, "body");
      setboolfield(L, "has_else", s->u.whilelet.has_else);
      ast_serialize_block(L, &s->u.whilelet.else_body);
      lua_setfield(L, -2, "else_body");
      break;

    case AST_STMT_GUARD:
      if (s->u.guard.cond) {
        ast_serialize_expr(L, s->u.guard.cond);
        lua_setfield(L, -2, "cond");
      }
      if (s->u.guard.let_var) {
        lua_pushstring(L, getstr(s->u.guard.let_var));
        lua_setfield(L, -2, "let_var");
      }
      if (s->u.guard.let_value) {
        ast_serialize_expr(L, s->u.guard.let_value);
        lua_setfield(L, -2, "let_value");
      }
      ast_serialize_block(L, &s->u.guard.else_block);
      lua_setfield(L, -2, "else_block");
      break;

    case AST_STMT_COMMAND:
    case AST_STMT_KEYWORD:
    case AST_STMT_OPERATOR:
      /* 这三个同样复用 nsstruct 的 name + body
      （ast_new_stmt_typed 创建(name body
      name = {name,body块 解析，也AST_STMT_TRAIT/INTERFACE/CONCEPT也是用 ast_new_stmt_typed，所以 nsstruct.name 和 nsstruct.body）*/
      if (s->u.nsstruct.name) {
        lua_pushstring(L, getstr(s->u.nsstruct.name));
        lua_setfield(L, -2, "name");
      }
      ast_serialize_block(L, &s->u.nsstruct.body);
      lua_setfield(L, -2, "body");
      break;

    case AST_STMT_EMPTY:
      break;

    case AST_STMT_CONSTEXPR:
      if (s->u.constexpr_stmt.directive) {
        lua_pushstring(L, getstr(s->u.constexpr_stmt.directive));
        lua_setfield(L, -2, "directive");
      }
      if (s->u.constexpr_stmt.cond) {
        ast_serialize_expr(L, s->u.constexpr_stmt.cond);
        lua_setfield(L, -2, "cond");
      }
      ast_serialize_block(L, &s->u.constexpr_stmt.body);
      lua_setfield(L, -2, "body");
      break;

    default:
      break;
  }
}

/* 序列化匹配模式 */
static void ast_serialize_match_pat(lua_State *L, AstMatchPat *pat) {
  push_table(L);
  switch (pat->kind) {
    case AST_PAT_WILDCARD:
      setstrfield(L, "kind", "wildcard");
      break;
    case AST_PAT_VARIABLE:
      setstrfield(L, "kind", "variable");
      lua_pushstring(L, getstr(pat->u.var_name));
      lua_setfield(L, -2, "name");
      break;
    case AST_PAT_LITERAL:
      setstrfield(L, "kind", "literal");
      ast_serialize_expr(L, pat->u.literal);
      lua_setfield(L, -2, "value");
      break;
    case AST_PAT_RANGE:
      setstrfield(L, "kind", "range");
      ast_serialize_expr(L, pat->u.range.low);
      lua_setfield(L, -2, "low");
      ast_serialize_expr(L, pat->u.range.high);
      lua_setfield(L, -2, "high");
      break;
    case AST_PAT_TYPE:
      setstrfield(L, "kind", "type");
      lua_pushstring(L, getstr(pat->u.type_name));
      lua_setfield(L, -2, "type_name");
      break;
    case AST_PAT_OR:
      setstrfield(L, "kind", "or");
      lua_newtable(L);
      for (int i = 0; i < pat->u.or_pat.npat; i++) {
        ast_serialize_match_pat(L, pat->u.or_pat.pats[i]);
        setarrayelem(L, i + 1);
      }
      lua_setfield(L, -2, "pats");
      break;
    case AST_PAT_TABLE:
      setstrfield(L, "kind", "table");
      lua_newtable(L);
      for (int i = 0; i < pat->u.table_pat.nfields; i++) {
        ast_serialize_match_pat(L, pat->u.table_pat.fields[i]);
        setarrayelem(L, i + 1);
      }
      lua_setfield(L, -2, "fields");
      break;
  }
}

/* 序列化语句块 */
static void ast_serialize_block(lua_State *L, AstBlock *blk) {
  push_table(L);
  setstrfield(L, "kind", "block");
  lua_newtable(L);
  for (int i = 0; i < blk->count; i++) {
    ast_serialize_stmt(L, blk->items[i]);
    setarrayelem(L, i + 1);
  }
  lua_setfield(L, -2, "body");
}

/* 顶层入口：将 AST 序列化为 Lua table */
void ast_serialize_to_lua(lua_State *L, AstChunk *chunk) {
  push_table(L);
  setstrfield(L, "kind", "chunk");
  setintfield(L, "line", chunk->node.line);
  ast_serialize_block(L, &chunk->main_func->body);
  lua_setfield(L, -2, "body");
}


/* ========== 反序列化 ========== */

/* 辅助：从 table 中获取字符串字段 */
static const char *get_field_str(lua_State *L, int idx, const char *k) {
  lua_getfield(L, idx, k);
  const char *s = lua_tostring(L, -1);
  lua_pop(L, 1);
  return s;
}

/* 辅助：从 table 中获取整数字段 */
static lua_Integer get_field_int(lua_State *L, int idx, const char *k) {
  lua_getfield(L, idx, k);
  lua_Integer v = lua_tointeger(L, -1);
  lua_pop(L, 1);
  return v;
}

/* 辅助：从 table 中获取布尔字段 */
static int get_field_bool(lua_State *L, int idx, const char *k) {
  lua_getfield(L, idx, k);
  int v = lua_toboolean(L, -1);
  lua_pop(L, 1);
  return v;
}

static lua_Number get_field_num(lua_State *L, int idx, const char *k) {
  lua_getfield(L, idx, k);
  lua_Number v = lua_tonumber(L, -1);
  lua_pop(L, 1);
  return v;
}

/* 字符串 → 二元运算符 */
static AstBinOp str_to_binop(const char *s) {
  if (s == NULL) return AST_BIN_ADD;
  if (strcmp(s, "+") == 0) return AST_BIN_ADD;
  if (strcmp(s, "-") == 0) return AST_BIN_SUB;
  if (strcmp(s, "*") == 0) return AST_BIN_MUL;
  if (strcmp(s, "/") == 0) return AST_BIN_DIV;
  if (strcmp(s, "//") == 0) return AST_BIN_IDIV;
  if (strcmp(s, "%") == 0) return AST_BIN_MOD;
  if (strcmp(s, "^") == 0) return AST_BIN_POW;
  if (strcmp(s, "&") == 0) return AST_BIN_BAND;
  if (strcmp(s, "|") == 0) return AST_BIN_BOR;
  if (strcmp(s, "~") == 0) return AST_BIN_BXOR;
  if (strcmp(s, "<<") == 0) return AST_BIN_SHL;
  if (strcmp(s, ">>") == 0) return AST_BIN_SHR;
  if (strcmp(s, "..") == 0) return AST_BIN_CONCAT;
  if (strcmp(s, "|>") == 0) return AST_BIN_PIPE;
  if (strcmp(s, "<|") == 0) return AST_BIN_REVPIPE;
  if (strcmp(s, "?|>") == 0) return AST_BIN_SAFEPIPE;
  if (strcmp(s, "==") == 0) return AST_BIN_EQ;
  if (strcmp(s, "~=") == 0) return AST_BIN_NE;
  if (strcmp(s, "<") == 0) return AST_BIN_LT;
  if (strcmp(s, "<=") == 0) return AST_BIN_LE;
  if (strcmp(s, ">") == 0) return AST_BIN_GT;
  if (strcmp(s, ">=") == 0) return AST_BIN_GE;
  if (strcmp(s, "<=>") == 0) return AST_BIN_SPACESHIP;
  if (strcmp(s, "is") == 0) return AST_BIN_IS;
  if (strcmp(s, "as") == 0) return AST_BIN_AS;
  if (strcmp(s, "in") == 0) return AST_BIN_IN;
  if (strcmp(s, "and") == 0) return AST_BIN_AND;
  if (strcmp(s, "or") == 0) return AST_BIN_OR;
  if (strcmp(s, "??") == 0) return AST_BIN_NULLCOAL;
  return AST_BIN_ADD;
}

/* 字符串 → 一元运算符 */
static AstUnOp str_to_unop(const char *s) {
  if (s == NULL) return AST_UN_MINUS;
  if (strcmp(s, "-") == 0) return AST_UN_MINUS;
  if (strcmp(s, "~") == 0) return AST_UN_BNOT;
  if (strcmp(s, "not") == 0) return AST_UN_NOT;
  if (strcmp(s, "#") == 0) return AST_UN_LEN;
  if (strcmp(s, "await") == 0) return AST_UN_AWAIT;
  return AST_UN_MINUS;
}

/* 前向声明 */
static AstExpr *ast_deserialize_expr(lua_State *L, AstPool *pool, int idx);
static AstStmt *ast_deserialize_stmt(lua_State *L, AstPool *pool, int idx);
static void ast_deserialize_block(lua_State *L, AstPool *pool, AstBlock *blk, int idx);

/* 反序列化匹配模式 */
static AstMatchPat *ast_deserialize_match_pat(lua_State *L, AstPool *pool, int idx) {
  if (!lua_istable(L, idx)) return NULL;
  const char *kind = get_field_str(L, idx, "kind");
  int line = 0;

  if (kind == NULL) return NULL;

  if (strcmp(kind, "wildcard") == 0) {
    return ast_new_pat_wildcard(pool, line);
  }
  if (strcmp(kind, "variable") == 0) {
    lua_getfield(L, idx, "name");
    TString *name = luaS_new(L, lua_tostring(L, -1));
    lua_pop(L, 1);
    return ast_new_pat_variable(pool, name, line);
  }
  if (strcmp(kind, "literal") == 0) {
    lua_getfield(L, idx, "value");
    AstExpr *e = ast_deserialize_expr(L, pool, lua_gettop(L));
    lua_pop(L, 1);
    return ast_new_pat_literal(pool, e, line);
  }
  if (strcmp(kind, "range") == 0) {
    lua_getfield(L, idx, "low");
    AstExpr *low = ast_deserialize_expr(L, pool, lua_gettop(L));
    lua_pop(L, 1);
    lua_getfield(L, idx, "high");
    AstExpr *high = ast_deserialize_expr(L, pool, lua_gettop(L));
    lua_pop(L, 1);
    return ast_new_pat_range(pool, low, high, line);
  }
  if (strcmp(kind, "type") == 0) {
    lua_getfield(L, idx, "type_name");
    TString *name = luaS_new(L, lua_tostring(L, -1));
    lua_pop(L, 1);
    return ast_new_pat_type(pool, name, line);
  }
  if (strcmp(kind, "or") == 0) {
    lua_getfield(L, idx, "pats");
    int npat = (int)luaL_len(L, -1);
    AstMatchPat **pats = ast_pool_alloc(pool, sizeof(AstMatchPat *) * npat);
    for (int i = 0; i < npat; i++) {
      lua_rawgeti(L, -1, i + 1);
      pats[i] = ast_deserialize_match_pat(L, pool, lua_gettop(L));
      lua_pop(L, 1);
    }
    lua_pop(L, 1);
    return ast_new_pat_or(pool, pats, npat, line);
  }
  if (strcmp(kind, "table") == 0) {
    lua_getfield(L, idx, "fields");
    int nfields = (int)luaL_len(L, -1);
    AstMatchPat **fields = ast_pool_alloc(pool, sizeof(AstMatchPat *) * nfields);
    for (int i = 0; i < nfields; i++) {
      lua_rawgeti(L, -1, i + 1);
      fields[i] = ast_deserialize_match_pat(L, pool, lua_gettop(L));
      lua_pop(L, 1);
    }
    lua_pop(L, 1);
    return ast_new_pat_table(pool, fields, nfields, line);
  }
  return NULL;
}

/* 反序列化表达式 */
static AstExpr *ast_deserialize_expr(lua_State *L, AstPool *pool, int idx) {
  luaL_checktype(L, idx, LUA_TTABLE);
  const char *kind = get_field_str(L, idx, "kind");
  int line = (int)get_field_int(L, idx, "line");

  if (kind == NULL) return ast_new_expr_nil(pool, line);

  if (strcmp(kind, "nil") == 0) {
    return ast_new_expr_nil(pool, line);
  }
  if (strcmp(kind, "true") == 0) {
    return ast_new_expr_bool(pool, 1, line);
  }
  if (strcmp(kind, "false") == 0) {
    return ast_new_expr_bool(pool, 0, line);
  }
  if (strcmp(kind, "int") == 0) {
    lua_Integer v = get_field_int(L, idx, "value");
    return ast_new_expr_int(pool, v, line);
  }
  if (strcmp(kind, "float") == 0) {
    lua_getfield(L, idx, "value");
    lua_Number v = lua_tonumber(L, -1);
    lua_pop(L, 1);
    return ast_new_expr_flt(pool, v, line);
  }
  if (strcmp(kind, "string") == 0) {
    const char *s = get_field_str(L, idx, "value");
    TString *ts = luaS_new(L, s ? s : "");
    return ast_new_expr_str(pool, ts, AST_EXPR_STRING, line);
  }
  if (strcmp(kind, "vararg") == 0) {
    return ast_new_expr_vararg(pool, line);
  }
  if (strcmp(kind, "ident") == 0) {
    const char *s = get_field_str(L, idx, "name");
    TString *ts = luaS_new(L, s ? s : "");
    return ast_new_expr_ident(pool, ts, line);
  }
  if (strcmp(kind, "binop") == 0) {
    const char *opstr = get_field_str(L, idx, "op");
    AstBinOp op = str_to_binop(opstr);
    lua_getfield(L, idx, "lhs");
    AstExpr *lhs = ast_deserialize_expr(L, pool, lua_gettop(L));
    lua_pop(L, 1);
    lua_getfield(L, idx, "rhs");
    AstExpr *rhs = ast_deserialize_expr(L, pool, lua_gettop(L));
    lua_pop(L, 1);
    return ast_new_expr_binop(pool, op, lhs, rhs, line);
  }
  if (strcmp(kind, "unop") == 0) {
    const char *opstr = get_field_str(L, idx, "op");
    AstUnOp op = str_to_unop(opstr);
    lua_getfield(L, idx, "operand");
    AstExpr *operand = ast_deserialize_expr(L, pool, lua_gettop(L));
    lua_pop(L, 1);
    return ast_new_expr_unop(pool, op, operand, line);
  }
  if (strcmp(kind, "call") == 0) {
    lua_getfield(L, idx, "callee");
    AstExpr *callee = ast_deserialize_expr(L, pool, lua_gettop(L));
    lua_pop(L, 1);
    lua_getfield(L, idx, "args");
    int nargs = (int)luaL_len(L, -1);
    AstExpr **args = NULL;
    if (nargs > 0) {
      args = ast_pool_alloc(pool, sizeof(AstExpr *) * nargs);
      for (int i = 0; i < nargs; i++) {
        lua_rawgeti(L, -1, i + 1);
        args[i] = ast_deserialize_expr(L, pool, lua_gettop(L));
        lua_pop(L, 1);
      }
    }
    lua_pop(L, 1);
    return ast_new_expr_call(pool, callee, args, nargs, line);
  }
  if (strcmp(kind, "methodcall") == 0) {
    lua_getfield(L, idx, "recv");
    AstExpr *recv = ast_deserialize_expr(L, pool, lua_gettop(L));
    lua_pop(L, 1);
    const char *method = get_field_str(L, idx, "method");
    TString *method_ts = luaS_new(L, method ? method : "");
    lua_getfield(L, idx, "args");
    int nargs = (int)luaL_len(L, -1);
    AstExpr **args = NULL;
    if (nargs > 0) {
      args = ast_pool_alloc(pool, sizeof(AstExpr *) * nargs);
      for (int i = 0; i < nargs; i++) {
        lua_rawgeti(L, -1, i + 1);
        args[i] = ast_deserialize_expr(L, pool, lua_gettop(L));
        lua_pop(L, 1);
      }
    }
    lua_pop(L, 1);
    return ast_new_expr_methodcall(pool, recv, method_ts, args, nargs, line);
  }
  if (strcmp(kind, "index") == 0) {
    lua_getfield(L, idx, "table");
    AstExpr *table = ast_deserialize_expr(L, pool, lua_gettop(L));
    lua_pop(L, 1);
    lua_getfield(L, idx, "key");
    AstExpr *key = ast_deserialize_expr(L, pool, lua_gettop(L));
    lua_pop(L, 1);
    int is_opt = (int)get_field_bool(L, idx, "is_opt");
    return ast_new_expr_index(pool, table, key, is_opt, line);
  }
  if (strcmp(kind, "table") == 0) {
    lua_getfield(L, idx, "entries");
    int nentries = (int)luaL_len(L, -1);
    AstTableEntry *entries = NULL;
    if (nentries > 0) {
      entries = ast_pool_alloc(pool, sizeof(AstTableEntry) * nentries);
      for (int i = 0; i < nentries; i++) {
        lua_rawgeti(L, -1, i + 1);
        AstExpr *key = NULL;
        lua_getfield(L, -1, "key");
        if (!lua_isnil(L, -1)) {
          key = ast_deserialize_expr(L, pool, lua_gettop(L));
        }
        lua_pop(L, 1);
        lua_getfield(L, -1, "value");
        AstExpr *value = ast_deserialize_expr(L, pool, lua_gettop(L));
        lua_pop(L, 1);
        entries[i].kind = (key != NULL) ? AST_TENTRY_KEY : AST_TENTRY_POS;
        entries[i].key = key;
        entries[i].value = value;
        lua_pop(L, 1);
      }
    }
    lua_pop(L, 1);
    return ast_new_expr_table(pool, entries, nentries, line);
  }
  if (strcmp(kind, "function") == 0 || strcmp(kind, "arrowfunc") == 0) {
    /* 反序列化函数表达式 */
    lua_getfield(L, idx, "params");
    int nparams = (int)luaL_len(L, -1);
    lua_pop(L, 1);
    int is_vararg = (int)get_field_bool(L, idx, "is_vararg");
    /* 创建 AstFunc */
    AstFunc *f = ast_new_func(pool, 0, -1, line);
    f->is_vararg = is_vararg;
    if (nparams > 0) {
      f->nparams = nparams;
      f->params = ast_pool_alloc(pool, sizeof(AstFuncParam) * nparams);
      lua_getfield(L, idx, "params");
      for (int i = 0; i < nparams; i++) {
        lua_rawgeti(L, -1, i + 1);
        f->params[i].name = luaS_new(L, lua_tostring(L, -1));
        f->params[i].default_value = NULL;
        f->params[i].attr = AST_ATTR_NONE;
        f->params[i].type_hint = NULL;
        lua_pop(L, 1);
      }
      lua_pop(L, 1);
    }
    lua_getfield(L, idx, "body");
    ast_deserialize_block(L, pool, &f->body, lua_gettop(L));
    lua_pop(L, 1);
    return ast_new_expr_func(pool, f, (strcmp(kind, "arrowfunc") == 0), line);
  }
  if (strcmp(kind, "condexpr") == 0) {
    lua_getfield(L, idx, "cond");
    AstExpr *cond = ast_deserialize_expr(L, pool, lua_gettop(L));
    lua_pop(L, 1);
    lua_getfield(L, idx, "then_expr");
    AstExpr *thn = ast_deserialize_expr(L, pool, lua_gettop(L));
    lua_pop(L, 1);
    lua_getfield(L, idx, "else_expr");
    AstExpr *els = ast_deserialize_expr(L, pool, lua_gettop(L));
    lua_pop(L, 1);
    return ast_new_expr_condexpr(pool, cond, thn, els, line);
  }
  if (strcmp(kind, "paren") == 0) {
    lua_getfield(L, idx, "expr");
    AstExpr *e = ast_deserialize_expr(L, pool, lua_gettop(L));
    lua_pop(L, 1);
    return ast_new_expr_paren(pool, e, line);
  }

  if (strcmp(kind, "switch") == 0) {
    /* 反序列化 switch 表达式 */
    AstExpr *e = ast_new_node(pool, AstExpr, AST_EXPR, line);
    e->kind = AST_EXPR_SWITCH_EXPR;
    lua_getfield(L, idx, "cond");
    e->u.switchx.cond = ast_deserialize_expr(L, pool, lua_gettop(L));
    lua_pop(L, 1);
    lua_getfield(L, idx, "arms");
    if (!lua_isnil(L, -1)) {
      int narms = (int)luaL_len(L, -1);
      e->u.switchx.narms = narms;
      e->u.switchx.arms = ast_pool_alloc(pool, sizeof(AstCaseArm) * narms);
      for (int i = 0; i < narms; i++) {
        lua_rawgeti(L, -1, i + 1);
        AstCaseArm *arm = &e->u.switchx.arms[i];
        memset(arm, 0, sizeof(AstCaseArm));
        lua_getfield(L, -1, "patterns");
        if (!lua_isnil(L, -1)) {
          int npat = (int)luaL_len(L, -1);
          arm->npatterns = npat;
          arm->patterns = ast_pool_alloc(pool, sizeof(AstExpr *) * npat);
          for (int p = 0; p < npat; p++) {
            lua_rawgeti(L, -1, p + 1);
            arm->patterns[p] = ast_deserialize_expr(L, pool, lua_gettop(L));
            lua_pop(L, 1);
          }
        }
        lua_pop(L, 1);
        lua_getfield(L, -1, "body");
        arm->body = ast_deserialize_expr(L, pool, lua_gettop(L));
        lua_pop(L, 1);
        lua_pop(L, 1);
      }
    }
    lua_pop(L, 1);
    lua_getfield(L, idx, "def");
    if (!lua_isnil(L, -1)) {
      e->u.switchx.def = ast_deserialize_expr(L, pool, lua_gettop(L));
    }
    lua_pop(L, 1);
    return e;
  }

  if (strcmp(kind, "match") == 0) {
    /* 反序列化 match 表达式 */
    AstStmt *stmt = ast_deserialize_stmt(L, pool, idx);
    stmt->u.matchstmt.is_expr = 1;
    return ast_new_expr_match(pool, stmt, line);
  }

  /* 基础字面量：nil */
  if (strcmp(kind, "nil") == 0) return ast_new_expr_nil(pool, line);
  if (strcmp(kind, "true") == 0) return ast_new_expr_bool(pool, 1, line);
  if (strcmp(kind, "false") == 0) return ast_new_expr_bool(pool, 0, line);
  if (strcmp(kind, "int") == 0) {
    lua_Integer iv = (lua_Integer)get_field_int(L, idx, "value");
    return ast_new_expr_int(pool, iv, line);
  }
  if (strcmp(kind, "float") == 0) {
    lua_Number fv = (lua_Number)get_field_num(L, idx, "value");
    return ast_new_expr_flt(pool, fv, line);
  }
  if (strcmp(kind, "string") == 0 || strcmp(kind, "interpstring") == 0 || strcmp(kind, "regex") == 0) {
    const char *sv = get_field_str(L, idx, "value");
    AstExprKind ek = AST_EXPR_STRING;
    if (strcmp(kind, "interpstring") == 0) ek = AST_EXPR_INTERPSTRING;
    else if (strcmp(kind, "regex") == 0) ek = AST_EXPR_REGEX;
    return ast_new_expr_str(pool, sv ? luaS_new(L, sv) : luaS_newliteral(L, ""), ek, line);
  }
  if (strcmp(kind, "vararg") == 0) return ast_new_expr_vararg(pool, line);
  if (strcmp(kind, "ident") == 0) {
    const char *nm = get_field_str(L, idx, "name");
    return ast_new_expr_ident(pool, nm ? luaS_new(L, nm) : luaS_newliteral(L, "_"), line);
  }
  if (strcmp(kind, "map") == 0) {
    /* AST_EXPR_MAP_CTOR：复用 table 反序列化但用 map 构造器 */
    lua_getfield(L, idx, "entries");
    int n = (int)luaL_len(L, -1);
    AstMapEntry *mes = NULL;
    if (n > 0) {
      mes = ast_pool_alloc(pool, sizeof(AstMapEntry) * n);
      for (int i = 0; i < n; i++) {
        lua_rawgeti(L, -1, i + 1);
        lua_getfield(L, -1, "key");
        mes[i].key = ast_deserialize_expr(L, pool, lua_gettop(L));
        lua_pop(L, 1);
        lua_getfield(L, -1, "value");
        mes[i].value = ast_deserialize_expr(L, pool, lua_gettop(L));
        lua_pop(L, 1);
        lua_pop(L, 1);
      }
    }
    lua_pop(L, 1);
    return ast_new_expr_map(pool, mes, n, line);
  }
  if (strcmp(kind, "arrowfunc") == 0 || strcmp(kind, "function") == 0) {
    AstExprKind ek = (strcmp(kind, "arrowfunc") == 0) ? AST_EXPR_ARROW_FUNC : AST_EXPR_FUNC_EXPR;
    int is_vararg = (int)get_field_bool(L, idx, "is_vararg");
    lua_getfield(L, idx, "params");
    int np = (int)luaL_len(L, -1);
    lua_pop(L, 1);
    AstFunc *f = ast_new_func(pool, 0, -1, line);
    f->is_vararg = is_vararg;
    if (np > 0) {
      f->nparams = np;
      f->params = ast_pool_alloc(pool, sizeof(AstFuncParam) * np);
      lua_getfield(L, idx, "params");
      for (int i = 0; i < np; i++) {
        lua_rawgeti(L, -1, i + 1);
        if (lua_isstring(L, -1)) {
          f->params[i].name = luaS_new(L, lua_tostring(L, -1));
        } else if (lua_istable(L, -1)) {
          const char *nm = get_field_str(L, lua_gettop(L), "name");
          f->params[i].name = nm ? luaS_new(L, nm) : luaS_newliteral(L, "_");
        } else {
          f->params[i].name = luaS_newliteral(L, "_");
        }
        f->params[i].default_value = NULL;
        f->params[i].attr = AST_ATTR_NONE;
        f->params[i].type_hint = NULL;
        lua_pop(L, 1);
      }
      lua_pop(L, 1);
    }
    lua_getfield(L, idx, "body");
    ast_deserialize_block(L, pool, &f->body, lua_gettop(L));
    lua_pop(L, 1);
    return ast_new_expr_func(pool, f, (ek == AST_EXPR_ARROW_FUNC) ? 1 : 0, line);
  }
  /* pipe / nullcoal / spaceship / is / in / merge / revpipe / safepipe：共用 binop 结构 */
  {
    AstBinOp op = AST_BIN_ADD;
    int is_bin = 1;
    if (strcmp(kind, "nullcoal") == 0) op = AST_BIN_NULLCOAL;
    else if (strcmp(kind, "spaceship") == 0) op = AST_BIN_SPACESHIP;
    else if (strcmp(kind, "is") == 0) op = AST_BIN_IS;
    else if (strcmp(kind, "as") == 0) op = AST_BIN_AS;
    else if (strcmp(kind, "in") == 0) op = AST_BIN_IN;
    else if (strcmp(kind, "merge") == 0) op = AST_BIN_MERGE;
    else if (strcmp(kind, "pipe") == 0) op = AST_BIN_PIPE;
    else if (strcmp(kind, "revpipe") == 0) op = AST_BIN_REVPIPE;
    else if (strcmp(kind, "safepipe") == 0) op = AST_BIN_SAFEPIPE;
    else is_bin = 0;
    if (is_bin) {
      lua_getfield(L, idx, "lhs");
      AstExpr *lhs = ast_deserialize_expr(L, pool, lua_gettop(L));
      lua_pop(L, 1);
      lua_getfield(L, idx, "rhs");
      AstExpr *rhs = ast_deserialize_expr(L, pool, lua_gettop(L));
      lua_pop(L, 1);
      return ast_new_expr_binop(pool, op, lhs, rhs, line);
    }
  }
  if (strcmp(kind, "await") == 0) {
    /* await 作为 UN_AWAIT 一元操作 */
    lua_getfield(L, idx, "operand");
    AstExpr *opd = ast_deserialize_expr(L, pool, lua_gettop(L));
    lua_pop(L, 1);
    return ast_new_expr_unop(pool, AST_UN_AWAIT, opd, line);
  }
  if (strcmp(kind, "range") == 0) {
    lua_getfield(L, idx, "start");
    AstExpr *st = ast_deserialize_expr(L, pool, lua_gettop(L));
    lua_pop(L, 1);
    lua_getfield(L, idx, "end");
    AstExpr *ed = ast_deserialize_expr(L, pool, lua_gettop(L));
    lua_pop(L, 1);
    return ast_new_expr_range(pool, st, ed, line);
  }
  if (strcmp(kind, "optchain") == 0) {
    /* 可选链：等价于 index(expr, key, is_opt=1) */
    lua_getfield(L, idx, "table");
    AstExpr *tbl = ast_deserialize_expr(L, pool, lua_gettop(L));
    lua_pop(L, 1);
    lua_getfield(L, idx, "key");
    AstExpr *key = ast_deserialize_expr(L, pool, lua_gettop(L));
    lua_pop(L, 1);
    return ast_new_expr_index(pool, tbl, key, 1, line);
  }
  if (strcmp(kind, "super") == 0) return ast_new_expr_super(pool, line);
  if (strcmp(kind, "selectcase") == 0) {
    /* SELECT_CASE：返回占位 ident（实际代码中不直接解析） */
    return ast_new_expr_ident(pool, luaS_newliteral(L, "__select_case__"), line);
  }
  if (strcmp(kind, "methodref") == 0) {
    AstExpr *recv = NULL;
    lua_getfield(L, idx, "recv");
    if (!lua_isnil(L, -1)) recv = ast_deserialize_expr(L, pool, lua_gettop(L));
    lua_pop(L, 1);
    const char *m = get_field_str(L, idx, "method");
    return ast_new_expr_methodref(pool, recv, m ? luaS_new(L, m) : luaS_newliteral(L, ""), line);
  }
  if (strcmp(kind, "new") == 0) {
    lua_getfield(L, idx, "class_expr");
    AstExpr *cls = ast_deserialize_expr(L, pool, lua_gettop(L));
    lua_pop(L, 1);
    lua_getfield(L, idx, "args");
    int na = (int)luaL_len(L, -1);
    AstExpr **args = NULL;
    if (na > 0) {
      args = ast_pool_alloc(pool, sizeof(AstExpr *) * na);
      for (int i = 0; i < na; i++) {
        lua_rawgeti(L, -1, i + 1);
        args[i] = ast_deserialize_expr(L, pool, lua_gettop(L));
        lua_pop(L, 1);
      }
    }
    lua_pop(L, 1);
    return ast_new_expr_new(pool, cls, args, na, line);
  }
  if (strcmp(kind, "testtype") == 0) {
    lua_getfield(L, idx, "operand");
    AstExpr *opd = ast_deserialize_expr(L, pool, lua_gettop(L));
    lua_pop(L, 1);
    const char *tn = get_field_str(L, idx, "type_name");
    return ast_new_expr_test_type(pool, opd, tn ? luaS_new(L, tn) : luaS_newliteral(L, ""), line);
  }
  if (strcmp(kind, "embed") == 0) {
    const char *fn = get_field_str(L, idx, "filename");
    return ast_new_expr_embed(pool, fn ? luaS_new(L, fn) : luaS_newliteral(L, ""), line);
  }
  if (strcmp(kind, "object") == 0) {
    lua_getfield(L, idx, "ctor");
    AstExpr *ctor = ast_deserialize_expr(L, pool, lua_gettop(L));
    lua_pop(L, 1);
    return ast_new_expr_object(pool, ctor, line);
  }
  if (strcmp(kind, "slice") == 0) {
    lua_getfield(L, idx, "table");
    AstExpr *tbl = ast_deserialize_expr(L, pool, lua_gettop(L));
    lua_pop(L, 1);
    AstExpr *st = NULL, *ed = NULL, *sp = NULL;
    lua_getfield(L, idx, "start");
    if (!lua_isnil(L, -1)) st = ast_deserialize_expr(L, pool, lua_gettop(L));
    lua_pop(L, 1);
    lua_getfield(L, idx, "end");
    if (!lua_isnil(L, -1)) ed = ast_deserialize_expr(L, pool, lua_gettop(L));
    lua_pop(L, 1);
    lua_getfield(L, idx, "step");
    if (!lua_isnil(L, -1)) sp = ast_deserialize_expr(L, pool, lua_gettop(L));
    lua_pop(L, 1);
    return ast_new_expr_slice(pool, tbl, st, ed, sp, line);
  }
  if (strcmp(kind, "dictcomp") == 0 || strcmp(kind, "listcomp") == 0) {
    /* 推导式：序列化写的是 func 子结构，先重建 AstFunc 再包装为 DICT/LIST COMP */
    int is_vararg = (int)get_field_bool(L, idx, "is_vararg");
    lua_getfield(L, idx, "params");
    int np = (int)luaL_len(L, -1);
    lua_pop(L, 1);
    AstFunc *f = ast_new_func(pool, 0, -1, line);
    f->is_vararg = is_vararg;
    if (np > 0) {
      f->nparams = np;
      f->params = ast_pool_alloc(pool, sizeof(AstFuncParam) * np);
      lua_getfield(L, idx, "params");
      for (int i = 0; i < np; i++) {
        lua_rawgeti(L, -1, i + 1);
        if (lua_isstring(L, -1)) f->params[i].name = luaS_new(L, lua_tostring(L, -1));
        else f->params[i].name = luaS_newliteral(L, "_");
        f->params[i].default_value = NULL;
        f->params[i].attr = AST_ATTR_NONE;
        f->params[i].type_hint = NULL;
        lua_pop(L, 1);
      }
      lua_pop(L, 1);
    }
    lua_getfield(L, idx, "body");
    ast_deserialize_block(L, pool, &f->body, lua_gettop(L));
    lua_pop(L, 1);
    AstExpr *e = ast_new_node(pool, AstExpr, AST_EXPR, line);
    e->kind = (strcmp(kind, "dictcomp") == 0) ? AST_EXPR_DICT_COMP : AST_EXPR_LIST_COMP;
    e->u.func.func = f;  /* 复用 func 联合字段 */
    return e;
  }
  if (strcmp(kind, "spread") == 0) {
    lua_getfield(L, idx, "expr");
    AstExpr *ex = ast_deserialize_expr(L, pool, lua_gettop(L));
    lua_pop(L, 1);
    return ast_new_expr_spread(pool, ex, line);
  }
  if (strcmp(kind, "walrus") == 0) {
    const char *nm = get_field_str(L, idx, "name");
    lua_getfield(L, idx, "expr");
    AstExpr *ex = ast_deserialize_expr(L, pool, lua_gettop(L));
    lua_pop(L, 1);
    return ast_new_expr_walrus(pool, nm ? luaS_new(L, nm) : luaS_newliteral(L, "_"), ex, line);
  }
  if (strcmp(kind, "astparser") == 0) {
    /* AST_EXPR_ASTPARSER：Proto/AstChunk 无法从 Lua table 重建，返回 ident 占位（防止后续 segv） */
    return ast_new_expr_ident(pool, luaS_newliteral(L, "__astparser_block__"), line);
  }

  /* 未知类型，返回 nil */
  return ast_new_expr_nil(pool, line);
}

/* 反序列化语句 */
static AstStmt *ast_deserialize_stmt(lua_State *L, AstPool *pool, int idx) {
  luaL_checktype(L, idx, LUA_TTABLE);
  const char *kind = get_field_str(L, idx, "kind");
  int line = (int)get_field_int(L, idx, "line");

  if (kind == NULL) return ast_new_stmt_block(pool, line);

  if (strcmp(kind, "block") == 0 || strcmp(kind, "do") == 0) {
    AstStmt *s = ast_new_stmt_block(pool, line);
    lua_getfield(L, idx, "body");
    ast_deserialize_block(L, pool, &s->u.block.block, lua_gettop(L));
    lua_pop(L, 1);
    return s;
  }

  if (strcmp(kind, "return") == 0) {
    lua_getfield(L, idx, "exprs");
    int nvalues = (int)luaL_len(L, -1);
    AstStmt *s = ast_new_stmt_return(pool, nvalues, line);
    if (nvalues > 0) {
      s->u.retstmt.values = ast_pool_alloc(pool, sizeof(AstExpr *) * nvalues);
      for (int i = 0; i < nvalues; i++) {
        lua_rawgeti(L, -1, i + 1);
        s->u.retstmt.values[i] = ast_deserialize_expr(L, pool, lua_gettop(L));
        lua_pop(L, 1);
      }
    }
    lua_pop(L, 1);
    return s;
  }

  if (strcmp(kind, "assign") == 0) {
    lua_getfield(L, idx, "targets");
    int ntargets = (int)luaL_len(L, -1);
    lua_pop(L, 1);
    lua_getfield(L, idx, "values");
    int nvalues = (int)luaL_len(L, -1);
    lua_pop(L, 1);
    AstStmt *s = ast_new_stmt_assign(pool, ntargets, nvalues, line);
    /* 反序列化 targets */
    if (ntargets > 0) {
      s->u.assign.targets = ast_pool_alloc(pool, sizeof(AstAssignTarget) * ntargets);
      lua_getfield(L, idx, "targets");
      for (int i = 0; i < ntargets; i++) {
        lua_rawgeti(L, -1, i + 1);
        AstAssignTarget *t = &s->u.assign.targets[i];
        memset(t, 0, sizeof(AstAssignTarget));
        const char *tgt_kind = get_field_str(L, -1, "kind");
        if (tgt_kind && strcmp(tgt_kind, "index") == 0) {
          /* 索引目标：先读子节点再解引用 */
          lua_getfield(L, -1, "table");
          AstExpr *tbl = ast_deserialize_expr(L, pool, lua_gettop(L));
          lua_pop(L, 1);
          lua_getfield(L, -1, "key");
          AstExpr *key = ast_deserialize_expr(L, pool, lua_gettop(L));
          lua_pop(L, 1);
          if (tbl && key) {
            t->kind = AST_TGT_INDEX;
            t->as.index.table = tbl;
            t->as.index.key = key;
          }
        } else {
          const char *name = get_field_str(L, -1, "name");
          if (name) {
            t->kind = AST_TGT_VAR;
            t->as.var.name = luaS_new(L, name);
            t->as.var.var_kind = AST_VAR_GLOBAL;
            t->as.var.idx = -1;
          }
        }
        lua_pop(L, 1);
      }
      lua_pop(L, 1);
    }
    /* 反序列化 values */
    if (nvalues > 0) {
      s->u.assign.values = ast_pool_alloc(pool, sizeof(AstExpr *) * nvalues);
      lua_getfield(L, idx, "values");
      for (int i = 0; i < nvalues; i++) {
        lua_rawgeti(L, -1, i + 1);
        s->u.assign.values[i] = ast_deserialize_expr(L, pool, lua_gettop(L));
        lua_pop(L, 1);
      }
      lua_pop(L, 1);
    }
    return s;
  }

  if (strcmp(kind, "compound") == 0) {
    /* 解析复合赋值运算符：注意 op_name 是符号字符串（"+", "-", "*" 等），不是单词名 */
    const char *op_name = get_field_str(L, idx, "op");
    AstBinOp op = AST_BIN_ADD;
    if (op_name) {
      if (strcmp(op_name, "+") == 0) op = AST_BIN_ADD;
      else if (strcmp(op_name, "-") == 0) op = AST_BIN_SUB;
      else if (strcmp(op_name, "*") == 0) op = AST_BIN_MUL;
      else if (strcmp(op_name, "/") == 0) op = AST_BIN_DIV;
      else if (strcmp(op_name, "//") == 0) op = AST_BIN_IDIV;
      else if (strcmp(op_name, "%") == 0) op = AST_BIN_MOD;
      else if (strcmp(op_name, "^") == 0) op = AST_BIN_POW;
      else if (strcmp(op_name, "..") == 0) op = AST_BIN_CONCAT;
      else if (strcmp(op_name, "&") == 0) op = AST_BIN_BAND;
      else if (strcmp(op_name, "|") == 0) op = AST_BIN_BOR;
      else if (strcmp(op_name, "~") == 0) op = AST_BIN_BXOR;
      else if (strcmp(op_name, "<<") == 0) op = AST_BIN_SHL;
      else if (strcmp(op_name, ">>") == 0) op = AST_BIN_SHR;
    }
    lua_getfield(L, idx, "targets");
    int ntargets = (int)luaL_len(L, -1);
    lua_pop(L, 1);
    /* value 先读出来（ntargets 创建时需要传 value 指针，后填充 targets） */
    lua_getfield(L, idx, "value");
    AstExpr *value_expr = !lua_isnil(L, -1) ? ast_deserialize_expr(L, pool, lua_gettop(L)) : NULL;
    lua_pop(L, 1);
    AstStmt *s = ast_new_stmt_compound(pool, op, ntargets, value_expr, line);
    /* 反序列化 targets */
    if (ntargets > 0) {
      s->u.compound.targets = ast_pool_alloc(pool, sizeof(AstAssignTarget) * ntargets);
      lua_getfield(L, idx, "targets");
      for (int i = 0; i < ntargets; i++) {
        lua_rawgeti(L, -1, i + 1);
        AstAssignTarget *t = &s->u.compound.targets[i];
        memset(t, 0, sizeof(AstAssignTarget));
        const char *tgt_kind = get_field_str(L, -1, "kind");
        if (tgt_kind && strcmp(tgt_kind, "index") == 0) {
          lua_getfield(L, -1, "table");
          AstExpr *tbl = ast_deserialize_expr(L, pool, lua_gettop(L));
          lua_pop(L, 1);
          lua_getfield(L, -1, "key");
          AstExpr *key = ast_deserialize_expr(L, pool, lua_gettop(L));
          lua_pop(L, 1);
          if (tbl && key) {
            t->kind = AST_TGT_INDEX;
            t->as.index.table = tbl;
            t->as.index.key = key;
          }
        } else {
          const char *name = get_field_str(L, -1, "name");
          if (name) {
            t->kind = AST_TGT_VAR;
            t->as.var.name = luaS_new(L, name);
            t->as.var.var_kind = AST_VAR_GLOBAL;
            t->as.var.idx = -1;
          }
        }
        lua_pop(L, 1);
      }
      lua_pop(L, 1);
    }
    return s;
  }

  if (strcmp(kind, "incr") == 0) {
    const char *op_name = get_field_str(L, idx, "op");
    AstIncrKind ik = AST_INCR_PRE_INC;
    if (op_name) {
      if (strcmp(op_name, "pre_incr") == 0) ik = AST_INCR_PRE_INC;
      else if (strcmp(op_name, "post_incr") == 0) ik = AST_INCR_POST_INC;
      else if (strcmp(op_name, "pre_decr") == 0) ik = AST_INCR_PRE_DEC;
      else if (strcmp(op_name, "post_decr") == 0) ik = AST_INCR_POST_DEC;
    }
    AstStmt *s = ast_new_stmt_incr(pool, ik, line);
    /* 反序列化 target */
    lua_getfield(L, idx, "target");
    if (!lua_isnil(L, -1) && lua_istable(L, -1)) {
      if (!s->u.incr.target) {
        s->u.incr.target = ast_pool_alloc(pool, sizeof(AstAssignTarget));
      }
      AstAssignTarget *t = s->u.incr.target;
      memset(t, 0, sizeof(AstAssignTarget));
      const char *tgt_kind = get_field_str(L, -1, "kind");
      if (tgt_kind && strcmp(tgt_kind, "index") == 0) {
        lua_getfield(L, -1, "table");
        AstExpr *tbl = ast_deserialize_expr(L, pool, lua_gettop(L));
        lua_pop(L, 1);
        lua_getfield(L, -1, "key");
        AstExpr *key = ast_deserialize_expr(L, pool, lua_gettop(L));
        lua_pop(L, 1);
        if (tbl && key) {
          t->kind = AST_TGT_INDEX;
          t->as.index.table = tbl;
          t->as.index.key = key;
        }
      } else {
        const char *name = get_field_str(L, -1, "name");
        if (name) {
          t->kind = AST_TGT_VAR;
          t->as.var.name = luaS_new(L, name);
          t->as.var.var_kind = AST_VAR_GLOBAL;
          t->as.var.idx = -1;
        }
      }
    }
    lua_pop(L, 1);
    return s;
  }

  if (strcmp(kind, "local") == 0) {
    lua_getfield(L, idx, "names");
    int nnames = (int)luaL_len(L, -1);
    lua_pop(L, 1);
    lua_getfield(L, idx, "values");
    int nvalues = (int)luaL_len(L, -1);
    lua_pop(L, 1);
    TString **names = NULL;
    if (nnames > 0) {
      names = ast_pool_alloc(pool, sizeof(TString *) * nnames);
      lua_getfield(L, idx, "names");
      for (int i = 0; i < nnames; i++) {
        lua_rawgeti(L, -1, i + 1);
        names[i] = luaS_new(L, lua_tostring(L, -1));
        lua_pop(L, 1);
      }
      lua_pop(L, 1);
    }
    AstStmt *s = ast_new_stmt_local(pool, nnames, names, nvalues, line);
    if (nvalues > 0) {
      s->u.local.values = ast_pool_alloc(pool, sizeof(AstExpr *) * nvalues);
      lua_getfield(L, idx, "values");
      for (int i = 0; i < nvalues; i++) {
        lua_rawgeti(L, -1, i + 1);
        s->u.local.values[i] = ast_deserialize_expr(L, pool, lua_gettop(L));
        lua_pop(L, 1);
      }
      lua_pop(L, 1);
    }
    return s;
  }

  if (strcmp(kind, "if") == 0) {
    AstStmt *s = ast_new_stmt_if(pool, line);
    lua_getfield(L, idx, "arms");
    int narms = (int)luaL_len(L, -1);
    if (narms > 0) {
      s->u.ifstmt.arms = ast_pool_alloc(pool, sizeof(AstIfArm) * narms);
      s->u.ifstmt.narms = narms;
      for (int i = 0; i < narms; i++) {
        lua_rawgeti(L, -1, i + 1);
        AstIfArm *arm = &s->u.ifstmt.arms[i];
        memset(arm, 0, sizeof(AstIfArm));
        lua_getfield(L, -1, "cond");
        if (!lua_isnil(L, -1)) {
          arm->cond = ast_deserialize_expr(L, pool, lua_gettop(L));
        }
        lua_pop(L, 1);
        lua_getfield(L, -1, "body");
        ast_deserialize_block(L, pool, &arm->body, lua_gettop(L));
        lua_pop(L, 1);
        lua_pop(L, 1);
      }
    }
    lua_pop(L, 1);
    lua_getfield(L, idx, "else_body");
    if (!lua_isnil(L, -1)) {
      s->u.ifstmt.has_else = 1;
      ast_deserialize_block(L, pool, &s->u.ifstmt.else_body, lua_gettop(L));
    }
    lua_pop(L, 1);
    return s;
  }

  if (strcmp(kind, "while") == 0) {
    lua_getfield(L, idx, "cond");
    AstExpr *cond = ast_deserialize_expr(L, pool, lua_gettop(L));
    lua_pop(L, 1);
    AstStmt *s = ast_new_stmt_while(pool, cond, line);
    lua_getfield(L, idx, "body");
    ast_deserialize_block(L, pool, &s->u.whilestmt.body, lua_gettop(L));
    lua_pop(L, 1);
    return s;
  }

  if (strcmp(kind, "repeat") == 0) {
    AstStmt *s = ast_new_stmt_repeat(pool, line);
    lua_getfield(L, idx, "body");
    ast_deserialize_block(L, pool, &s->u.whilestmt.body, lua_gettop(L));
    lua_pop(L, 1);
    lua_getfield(L, idx, "cond");
    s->u.whilestmt.cond = ast_deserialize_expr(L, pool, lua_gettop(L));
    lua_pop(L, 1);
    return s;
  }

  if (strcmp(kind, "fornum") == 0) {
    const char *var = get_field_str(L, idx, "var");
    lua_getfield(L, idx, "start");
    AstExpr *start = ast_deserialize_expr(L, pool, lua_gettop(L));
    lua_pop(L, 1);
    lua_getfield(L, idx, "end");
    AstExpr *stop = ast_deserialize_expr(L, pool, lua_gettop(L));
    lua_pop(L, 1);
    AstExpr *step = NULL;
    lua_getfield(L, idx, "step");
    if (!lua_isnil(L, -1)) {
      step = ast_deserialize_expr(L, pool, lua_gettop(L));
    }
    lua_pop(L, 1);
    TString *varname = luaS_new(L, var ? var : "i");
    AstStmt *s = ast_new_stmt_fornum(pool, varname, start, stop, step, line);
    lua_getfield(L, idx, "body");
    ast_deserialize_block(L, pool, &s->u.fornum.body, lua_gettop(L));
    lua_pop(L, 1);
    return s;
  }

  if (strcmp(kind, "forgen") == 0) {
    lua_getfield(L, idx, "vars");
    int nnames = (int)luaL_len(L, -1);
    lua_pop(L, 1);
    lua_getfield(L, idx, "exprs");
    int nexprs = (int)luaL_len(L, -1);
    lua_pop(L, 1);
    AstStmt *s = ast_new_stmt_forgen(pool, nnames, nexprs, line);
    if (nnames > 0) {
      s->u.forgen.names = ast_pool_alloc(pool, sizeof(TString *) * nnames);
      lua_getfield(L, idx, "vars");
      for (int i = 0; i < nnames; i++) {
        lua_rawgeti(L, -1, i + 1);
        s->u.forgen.names[i] = luaS_new(L, lua_tostring(L, -1));
        lua_pop(L, 1);
      }
      lua_pop(L, 1);
    }
    if (nexprs > 0) {
      s->u.forgen.exprs = ast_pool_alloc(pool, sizeof(AstExpr *) * nexprs);
      lua_getfield(L, idx, "exprs");
      for (int i = 0; i < nexprs; i++) {
        lua_rawgeti(L, -1, i + 1);
        s->u.forgen.exprs[i] = ast_deserialize_expr(L, pool, lua_gettop(L));
        lua_pop(L, 1);
      }
      lua_pop(L, 1);
    }
    lua_getfield(L, idx, "body");
    ast_deserialize_block(L, pool, &s->u.forgen.body, lua_gettop(L));
    lua_pop(L, 1);
    return s;
  }

  if (strcmp(kind, "break") == 0) {
    int level = (int)get_field_int(L, idx, "level");
    if (level < 1) level = 1;
    return ast_new_stmt_break(pool, level, line);
  }

  if (strcmp(kind, "continue") == 0) {
    int level = (int)get_field_int(L, idx, "level");
    if (level < 1) level = 1;
    return ast_new_stmt_continue(pool, level, line);
  }

  if (strcmp(kind, "goto") == 0) {
    const char *label = get_field_str(L, idx, "label");
    TString *ts = luaS_new(L, label ? label : "");
    return ast_new_stmt_goto(pool, ts, line);
  }

  if (strcmp(kind, "label") == 0) {
    const char *name = get_field_str(L, idx, "name");
    TString *ts = luaS_new(L, name ? name : "");
    return ast_new_stmt_label(pool, ts, line);
  }

  if (strcmp(kind, "expr") == 0) {
    lua_getfield(L, idx, "expr");
    AstExpr *e = ast_deserialize_expr(L, pool, lua_gettop(L));
    lua_pop(L, 1);
    return ast_new_stmt_expr(pool, e, line);
  }

  if (strcmp(kind, "localfunc") == 0) {
    /* 反序列化 local function 语句 */
    const char *name = get_field_str(L, idx, "name");
    TString *name_ts = luaS_new(L, name ? name : "");
    lua_getfield(L, idx, "params");
    int nparams = (int)luaL_len(L, -1);
    lua_pop(L, 1);
    int is_vararg = (int)get_field_bool(L, idx, "is_vararg");
    AstFunc *f = ast_new_func(pool, 0, -1, line);
    f->is_vararg = is_vararg;
    if (nparams > 0) {
      f->nparams = nparams;
      f->params = ast_pool_alloc(pool, sizeof(AstFuncParam) * nparams);
      lua_getfield(L, idx, "params");
      for (int i = 0; i < nparams; i++) {
        lua_rawgeti(L, -1, i + 1);
        f->params[i].name = luaS_new(L, lua_tostring(L, -1));
        f->params[i].default_value = NULL;
        f->params[i].attr = AST_ATTR_NONE;
        f->params[i].type_hint = NULL;
        lua_pop(L, 1);
      }
      lua_pop(L, 1);
    }
    lua_getfield(L, idx, "body");
    ast_deserialize_block(L, pool, &f->body, lua_gettop(L));
    lua_pop(L, 1);
    return ast_new_stmt_localfunc(pool, name_ts, f, line);
  }

  if (strcmp(kind, "function") == 0) {
    /* 反序列化 function 语句（非局部函数声明） */
    lua_getfield(L, idx, "params");
    int nparams = (int)luaL_len(L, -1);
    lua_pop(L, 1);
    int is_vararg = (int)get_field_bool(L, idx, "is_vararg");
    AstFunc *f = ast_new_func(pool, 0, -1, line);
    f->is_vararg = is_vararg;
    if (nparams > 0) {
      f->nparams = nparams;
      f->params = ast_pool_alloc(pool, sizeof(AstFuncParam) * nparams);
      lua_getfield(L, idx, "params");
      for (int i = 0; i < nparams; i++) {
        lua_rawgeti(L, -1, i + 1);
        f->params[i].name = luaS_new(L, lua_tostring(L, -1));
        f->params[i].default_value = NULL;
        f->params[i].attr = AST_ATTR_NONE;
        f->params[i].type_hint = NULL;
        lua_pop(L, 1);
      }
      lua_pop(L, 1);
    }
    lua_getfield(L, idx, "body");
    ast_deserialize_block(L, pool, &f->body, lua_gettop(L));
    lua_pop(L, 1);
    /* 使用 localfunc 结构存储函数定义（name 为 NULL 表示匿名或非局部函数） */
    AstStmt *s = ast_new_node(pool, AstStmt, AST_STMT, line);
    s->kind = AST_STMT_LOCAL_FUNC;
    s->u.localfunc.name = NULL;
    s->u.localfunc.func = f;
    return s;
  }

  if (strcmp(kind, "switch") == 0) {
    /* 反序列化 switch 语句 */
    AstStmt *s = ast_new_node(pool, AstStmt, AST_STMT, line);
    s->kind = AST_STMT_SWITCH;
    lua_getfield(L, idx, "cond");
    s->u.switchstmt.cond = ast_deserialize_expr(L, pool, lua_gettop(L));
    lua_pop(L, 1);
    /* 反序列化 cases */
    lua_getfield(L, idx, "cases");
    if (!lua_isnil(L, -1)) {
      int ncases = (int)luaL_len(L, -1);
      if (ncases > 0) {
        s->u.switchstmt.cases = ast_pool_alloc(pool, sizeof(AstSwitchCase) * ncases);
        s->u.switchstmt.ncases = ncases;
        for (int i = 0; i < ncases; i++) {
          lua_rawgeti(L, -1, i + 1);
          AstSwitchCase *c = &s->u.switchstmt.cases[i];
          memset(c, 0, sizeof(AstSwitchCase));
          /* 反序列化 patterns 数组 */
          lua_getfield(L, -1, "patterns");
          if (!lua_isnil(L, -1)) {
            int npat = (int)luaL_len(L, -1);
            c->npatterns = npat;
            c->patterns = ast_pool_alloc(pool, sizeof(AstExpr *) * npat);
            for (int p = 0; p < npat; p++) {
              lua_rawgeti(L, -1, p + 1);
              c->patterns[p] = ast_deserialize_expr(L, pool, lua_gettop(L));
              lua_pop(L, 1);
            }
          }
          lua_pop(L, 1);
          lua_getfield(L, -1, "body");
          ast_deserialize_block(L, pool, &c->body, lua_gettop(L));
          lua_pop(L, 1);
          lua_pop(L, 1);
        }
      }
    }
    lua_pop(L, 1);
    /* 反序列化 default_body */
    lua_getfield(L, idx, "default_body");
    if (!lua_isnil(L, -1)) {
      s->u.switchstmt.has_default = 1;
      ast_deserialize_block(L, pool, &s->u.switchstmt.default_body, lua_gettop(L));
    }
    lua_pop(L, 1);
    return s;
  }

  if (strcmp(kind, "match") == 0) {
    /* 反序列化 match 语句 */
    AstStmt *s = ast_new_node(pool, AstStmt, AST_STMT, line);
    s->kind = AST_STMT_MATCH;
    lua_getfield(L, idx, "control");
    s->u.matchstmt.control = ast_deserialize_expr(L, pool, lua_gettop(L));
    lua_pop(L, 1);
    s->u.matchstmt.is_expr = (int)get_field_int(L, idx, "is_expr");
    lua_getfield(L, idx, "arms");
    if (!lua_isnil(L, -1)) {
      int narms = (int)luaL_len(L, -1);
      s->u.matchstmt.narms = narms;
      s->u.matchstmt.arms = ast_pool_alloc(pool, sizeof(AstMatchArm) * narms);
      for (int i = 0; i < narms; i++) {
        lua_rawgeti(L, -1, i + 1);
        AstMatchArm *arm = &s->u.matchstmt.arms[i];
        memset(arm, 0, sizeof(AstMatchArm));
        lua_getfield(L, -1, "pattern");
        arm->pattern = ast_deserialize_match_pat(L, pool, lua_gettop(L));
        lua_pop(L, 1);
        lua_getfield(L, -1, "guard");
        if (!lua_isnil(L, -1)) {
          arm->guard = ast_deserialize_expr(L, pool, lua_gettop(L));
        }
        lua_pop(L, 1);
        arm->is_arrow = (int)get_field_int(L, -1, "is_arrow");
        if (arm->is_arrow) {
          lua_getfield(L, -1, "body_expr");
          arm->body_expr = ast_deserialize_expr(L, pool, lua_gettop(L));
          lua_pop(L, 1);
        } else {
          lua_getfield(L, -1, "body_block");
          ast_deserialize_block(L, pool, &arm->body_block, lua_gettop(L));
          lua_pop(L, 1);
        }
        lua_pop(L, 1);
      }
    }
    lua_pop(L, 1);
    return s;
  }

  if (strcmp(kind, "try") == 0) {
    /* 反序列化 try 语句 */
    AstBlock body, catch_body, finally_body;
    memset(&body, 0, sizeof(AstBlock));
    memset(&catch_body, 0, sizeof(AstBlock));
    memset(&finally_body, 0, sizeof(AstBlock));
    lua_getfield(L, idx, "body");
    ast_deserialize_block(L, pool, &body, lua_gettop(L));
    lua_pop(L, 1);
    AstExpr *catch_var = NULL;
    lua_getfield(L, idx, "catch_var");
    if (!lua_isnil(L, -1)) {
      catch_var = ast_deserialize_expr(L, pool, lua_gettop(L));
    }
    lua_pop(L, 1);
    lua_getfield(L, idx, "catch_body");
    if (!lua_isnil(L, -1)) {
      ast_deserialize_block(L, pool, &catch_body, lua_gettop(L));
    }
    lua_pop(L, 1);
    lua_getfield(L, idx, "finally_body");
    if (!lua_isnil(L, -1)) {
      ast_deserialize_block(L, pool, &finally_body, lua_gettop(L));
    }
    lua_pop(L, 1);
    return ast_new_stmt_try(pool, &body, catch_var, &catch_body, &finally_body, line);
  }

  /* global 语句：字段结构与 local 类似 */
  if (strcmp(kind, "global") == 0) {
    lua_getfield(L, idx, "names");
    int nn = (int)luaL_len(L, -1);
    lua_pop(L, 1);
    lua_getfield(L, idx, "values");
    int nv = (int)luaL_len(L, -1);
    lua_pop(L, 1);
    TString **names = NULL;
    AstStmt *s = ast_new_stmt_global(pool, nn, nv, line);
    s->u.global.has_wildcard = (int)get_field_bool(L, idx, "has_wildcard");
    if (nn > 0) {
      names = ast_pool_alloc(pool, sizeof(TString *) * nn);
      lua_getfield(L, idx, "names");
      for (int i = 0; i < nn; i++) {
        lua_rawgeti(L, -1, i + 1);
        names[i] = luaS_new(L, lua_tostring(L, -1) ? lua_tostring(L, -1) : "_");
        lua_pop(L, 1);
      }
      lua_pop(L, 1);
      s->u.global.names = names;
    }
    if (nv > 0) {
      s->u.global.values = ast_pool_alloc(pool, sizeof(AstExpr *) * nv);
      lua_getfield(L, idx, "values");
      for (int i = 0; i < nv; i++) {
        lua_rawgeti(L, -1, i + 1);
        s->u.global.values[i] = ast_deserialize_expr(L, pool, lua_gettop(L));
        lua_pop(L, 1);
      }
      lua_pop(L, 1);
    }
    return s;
  }

  /* throw：抛出异常 */
  if (strcmp(kind, "throw") == 0) {
    AstExpr *e = NULL;
    lua_getfield(L, idx, "expr");
    if (!lua_isnil(L, -1)) e = ast_deserialize_expr(L, pool, lua_gettop(L));
    lua_pop(L, 1);
    return ast_new_stmt_throw(pool, e, line);
  }

  /* defer：延后执行的代码块 */
  if (strcmp(kind, "defer") == 0) {
    AstBlock body;
    memset(&body, 0, sizeof(AstBlock));
    lua_getfield(L, idx, "body");
    ast_deserialize_block(L, pool, &body, lua_gettop(L));
    lua_pop(L, 1);
    return ast_new_stmt_defer(pool, &body, line);
  }

  /* take：解构赋值（take a, b from expr） */
  if (strcmp(kind, "take") == 0) {
    lua_getfield(L, idx, "varnames");
    int nv = (int)luaL_len(L, -1);
    lua_pop(L, 1);
    TString **vns = NULL;
    AstExpr **defs = NULL;
    if (nv > 0) {
      vns = ast_pool_alloc(pool, sizeof(TString *) * nv);
      defs = ast_pool_alloc(pool, sizeof(AstExpr *) * nv);
      memset(defs, 0, sizeof(AstExpr *) * nv);
      lua_getfield(L, idx, "varnames");
      for (int i = 0; i < nv; i++) {
        lua_rawgeti(L, -1, i + 1);
        vns[i] = luaS_new(L, lua_tostring(L, -1) ? lua_tostring(L, -1) : "_");
        lua_pop(L, 1);
      }
      lua_pop(L, 1);
      /* defaults 反序列化（若存在） */
      lua_getfield(L, idx, "defaults");
      if (lua_istable(L, -1)) {
        for (int i = 0; i < nv; i++) {
          lua_rawgeti(L, -1, i + 1);
          if (!lua_isnil(L, -1)) defs[i] = ast_deserialize_expr(L, pool, lua_gettop(L));
          lua_pop(L, 1);
        }
      }
      lua_pop(L, 1);
    }
    AstExpr *src = NULL;
    lua_getfield(L, idx, "source");
    if (!lua_isnil(L, -1)) src = ast_deserialize_expr(L, pool, lua_gettop(L));
    lua_pop(L, 1);
    int is_array = (int)get_field_bool(L, idx, "is_array");
    return ast_new_stmt_take(pool, nv, vns, defs, src, is_array, line);
  }

  /* catch / finally：单独存在时复用 trycatch 结构 */
  if (strcmp(kind, "catch") == 0 || strcmp(kind, "finally") == 0) {
    AstBlock body, cbody, fbody;
    memset(&body, 0, sizeof(AstBlock));
    memset(&cbody, 0, sizeof(AstBlock));
    memset(&fbody, 0, sizeof(AstBlock));
    AstExpr *cv = NULL;
    lua_getfield(L, idx, "catch_var");
    if (!lua_isnil(L, -1)) cv = ast_deserialize_expr(L, pool, lua_gettop(L));
    lua_pop(L, 1);
    lua_getfield(L, idx, "body");
    ast_deserialize_block(L, pool, &body, lua_gettop(L));
    lua_pop(L, 1);
    lua_getfield(L, idx, "catch_body");
    if (!lua_isnil(L, -1)) ast_deserialize_block(L, pool, &cbody, lua_gettop(L));
    lua_pop(L, 1);
    lua_getfield(L, idx, "finally_body");
    if (!lua_isnil(L, -1)) ast_deserialize_block(L, pool, &fbody, lua_gettop(L));
    lua_pop(L, 1);
    AstStmt *s = ast_new_stmt_try(pool, &body, cv, &cbody, &fbody, line);
    s->kind = (strcmp(kind, "catch") == 0) ? AST_STMT_CATCH : AST_STMT_FINALLY;
    return s;
  }

  /* using：using namespace / using Name::Member */
  if (strcmp(kind, "using") == 0) {
    int is_ns = (int)get_field_bool(L, idx, "is_namespace");
    const char *nm = get_field_str(L, idx, "name");
    const char *lm = get_field_str(L, idx, "last_member");
    return ast_new_stmt_using(pool, is_ns,
                              nm ? luaS_new(L, nm) : NULL,
                              lm ? luaS_new(L, lm) : NULL,
                              line);
  }

  /* namespace：命名空间 */
  if (strcmp(kind, "namespace") == 0) {
    const char *nm = get_field_str(L, idx, "name");
    AstBlock body;
    memset(&body, 0, sizeof(AstBlock));
    lua_getfield(L, idx, "body");
    ast_deserialize_block(L, pool, &body, lua_gettop(L));
    lua_pop(L, 1);
    return ast_new_stmt_namespace(pool, nm ? luaS_new(L, nm) : NULL, &body, line);
  }

  /* struct / superstruct：结构体 / 超结构体 */
  if (strcmp(kind, "struct") == 0 || strcmp(kind, "superstruct") == 0) {
    const char *nm = get_field_str(L, idx, "name");
    /* 优先 entries，其次 body */
    lua_getfield(L, idx, "entries");
    int ne = 0;
    AstKVPair *entries = NULL;
    if (lua_istable(L, -1)) {
      ne = (int)luaL_len(L, -1);
      if (ne > 0) {
        entries = ast_pool_alloc(pool, sizeof(AstKVPair) * ne);
        memset(entries, 0, sizeof(AstKVPair) * ne);
        for (int i = 0; i < ne; i++) {
          lua_rawgeti(L, -1, i + 1);
          lua_getfield(L, -1, "key");
          if (!lua_isnil(L, -1)) entries[i].key = ast_deserialize_expr(L, pool, lua_gettop(L));
          lua_pop(L, 1);
          lua_getfield(L, -1, "value");
          if (!lua_isnil(L, -1)) entries[i].value = ast_deserialize_expr(L, pool, lua_gettop(L));
          lua_pop(L, 1);
          lua_pop(L, 1);
        }
      }
    }
    lua_pop(L, 1);
    AstStmt *s;
    if (ne > 0) {
      s = ast_new_stmt_typed_pairs(pool,
          (strcmp(kind, "struct") == 0) ? AST_STMT_STRUCT : AST_STMT_SUPERSTRUCT,
          nm ? luaS_new(L, nm) : NULL, entries, ne, line);
    } else {
      AstBlock body;
      memset(&body, 0, sizeof(AstBlock));
      lua_getfield(L, idx, "body");
      ast_deserialize_block(L, pool, &body, lua_gettop(L));
      lua_pop(L, 1);
      s = ast_new_stmt_typed(pool,
          (strcmp(kind, "struct") == 0) ? AST_STMT_STRUCT : AST_STMT_SUPERSTRUCT,
          nm ? luaS_new(L, nm) : NULL, &body, line);
    }
    return s;
  }

  /* enum：枚举 */
  if (strcmp(kind, "enum") == 0) {
    const char *nm = get_field_str(L, idx, "name");
    int is_cls = (int)get_field_bool(L, idx, "is_class");
    lua_getfield(L, idx, "entries");
    int ne = 0;
    AstEnumEntry *entries = NULL;
    if (lua_istable(L, -1)) {
      ne = (int)luaL_len(L, -1);
      if (ne > 0) {
        entries = ast_pool_alloc(pool, sizeof(AstEnumEntry) * ne);
        memset(entries, 0, sizeof(AstEnumEntry) * ne);
        for (int i = 0; i < ne; i++) {
          lua_rawgeti(L, -1, i + 1);
          const char *ename = get_field_str(L, -1, "name");
          entries[i].name = ename ? luaS_new(L, ename) : NULL;
          lua_getfield(L, -1, "value_expr");
          if (!lua_isnil(L, -1)) entries[i].value_expr = ast_deserialize_expr(L, pool, lua_gettop(L));
          lua_pop(L, 1);
          lua_pop(L, 1);
        }
      }
    }
    lua_pop(L, 1);
    return ast_new_stmt_enum(pool, nm ? luaS_new(L, nm) : NULL, entries, ne, is_cls, line);
  }

  /* class：类 */
  if (strcmp(kind, "class") == 0) {
    AstStmt *s = ast_new_node(pool, AstStmt, AST_STMT, line);
    s->kind = AST_STMT_CLASS;
    const char *nm = get_field_str(L, idx, "name");
    s->u.classstmt.name = nm ? luaS_new(L, nm) : NULL;
    /* 反序列化多父类列表 */
    lua_getfield(L, idx, "extends_names");
    if (lua_istable(L, -1)) {
      int ne = (int)luaL_len(L, -1);
      s->u.classstmt.nextends = ne;
      if (ne > 0) {
        s->u.classstmt.extends_names = ast_pool_alloc(pool, sizeof(TString *) * ne);
        for (int i = 0; i < ne; i++) {
          lua_rawgeti(L, -1, i + 1);
          s->u.classstmt.extends_names[i] = luaS_new(L, lua_tostring(L, -1) ? lua_tostring(L, -1) : "");
          lua_pop(L, 1);
        }
      } else {
        s->u.classstmt.extends_names = NULL;
      }
    } else {
      /* 兼容旧格式：单父类 extends_name 字符串 */
      const char *ext = get_field_str(L, idx, "extends_name");
      if (ext) {
        s->u.classstmt.nextends = 1;
        s->u.classstmt.extends_names = ast_pool_alloc(pool, sizeof(TString *));
        s->u.classstmt.extends_names[0] = luaS_new(L, ext);
      } else {
        s->u.classstmt.nextends = 0;
        s->u.classstmt.extends_names = NULL;
      }
    }
    lua_pop(L, 1);
    lua_getfield(L, idx, "implements");
    if (lua_istable(L, -1)) {
      int ni = (int)luaL_len(L, -1);
      s->u.classstmt.nimplements = ni;
      if (ni > 0) {
        s->u.classstmt.implements = ast_pool_alloc(pool, sizeof(TString *) * ni);
        for (int i = 0; i < ni; i++) {
          lua_rawgeti(L, -1, i + 1);
          s->u.classstmt.implements[i] = luaS_new(L, lua_tostring(L, -1) ? lua_tostring(L, -1) : "");
          lua_pop(L, 1);
        }
      }
    }
    lua_pop(L, 1);
    lua_getfield(L, idx, "use_traits");
    if (lua_istable(L, -1)) {
      int nt = (int)luaL_len(L, -1);
      s->u.classstmt.nuse_traits = nt;
      if (nt > 0) {
        s->u.classstmt.use_traits = ast_pool_alloc(pool, sizeof(TString *) * nt);
        for (int i = 0; i < nt; i++) {
          lua_rawgeti(L, -1, i + 1);
          s->u.classstmt.use_traits[i] = luaS_new(L, lua_tostring(L, -1) ? lua_tostring(L, -1) : "");
          lua_pop(L, 1);
        }
      }
    }
    lua_pop(L, 1);
    s->u.classstmt.class_flags = (int)get_field_int(L, idx, "class_flags");
    /* 反序列化泛型参数 */
    s->u.classstmt.generic_params = NULL;
    s->u.classstmt.ngeneric_params = 0;
    lua_getfield(L, idx, "generic_params");
    if (lua_istable(L, -1)) {
      int ng = (int)luaL_len(L, -1);
      s->u.classstmt.ngeneric_params = ng;
      if (ng > 0) {
        s->u.classstmt.generic_params = ast_pool_alloc(pool, sizeof(TString *) * ng);
        for (int i = 0; i < ng; i++) {
          lua_rawgeti(L, -1, i + 1);
          s->u.classstmt.generic_params[i] = luaS_new(L, lua_tostring(L, -1) ? lua_tostring(L, -1) : "");
          lua_pop(L, 1);
        }
      }
    }
    lua_pop(L, 1);
    s->u.classstmt.members = NULL;
    s->u.classstmt.nmembers = 0;
    memset(&s->u.classstmt.body, 0, sizeof(AstBlock));
    lua_getfield(L, idx, "members");
    if (lua_istable(L, -1)) {
      int nmemb = (int)luaL_len(L, -1);
      if (nmemb > 0) {
        s->u.classstmt.nmembers = nmemb;
        s->u.classstmt.members = ast_pool_alloc(pool, sizeof(AstClassMember) * nmemb);
        memset(s->u.classstmt.members, 0, sizeof(AstClassMember) * nmemb);
        for (int i = 0; i < nmemb; i++) {
          lua_rawgeti(L, -1, i + 1);
          AstClassMember *m = &s->u.classstmt.members[i];
          m->kind = (AstMemberKind)(int)get_field_int(L, -1, "kind");
          m->access = (AstAccessLevel)(int)get_field_int(L, -1, "access");
          m->is_static = (int)get_field_bool(L, -1, "is_static");
          const char *mname = get_field_str(L, -1, "name");
          m->name = mname ? luaS_new(L, mname) : NULL;
          if (m->kind == AST_MEMBER_PROPERTY) {
            lua_getfield(L, -1, "property_value");
            if (!lua_isnil(L, -1)) m->u.property_value = ast_deserialize_expr(L, pool, lua_gettop(L));
            lua_pop(L, 1);
          } else if (m->kind == AST_MEMBER_METHOD) {
            lua_getfield(L, -1, "method_func");
            if (lua_istable(L, -1)) {
              /* 方法：从临时的 localfunc 结构解出 AstFunc */
              AstStmt *tmp_fs = ast_deserialize_stmt(L, pool, lua_gettop(L));
              if (tmp_fs && tmp_fs->kind == AST_STMT_LOCAL_FUNC) {
                m->u.method_func = tmp_fs->u.localfunc.func;
              }
            }
            lua_pop(L, 1);
          }
          lua_pop(L, 1);
        }
      }
    } else {
      lua_pop(L, 1); /* pop members(nil) */
      lua_getfield(L, idx, "body");
      if (!lua_isnil(L, -1)) ast_deserialize_block(L, pool, &s->u.classstmt.body, lua_gettop(L));
    }
    lua_pop(L, 1); /* pop members(table or body) */
    return s;
  }

  /* trait / interface / concept：三者都是 typed 结构 */
  if (strcmp(kind, "trait") == 0 || strcmp(kind, "interface") == 0 || strcmp(kind, "concept") == 0) {
    AstStmtKind sk = AST_STMT_TRAIT;
    if (strcmp(kind, "interface") == 0) sk = AST_STMT_INTERFACE;
    else if (strcmp(kind, "concept") == 0) sk = AST_STMT_CONCEPT;
    const char *nm = get_field_str(L, idx, "name");
    AstBlock body;
    memset(&body, 0, sizeof(AstBlock));
    lua_getfield(L, idx, "body");
    ast_deserialize_block(L, pool, &body, lua_gettop(L));
    lua_pop(L, 1);
    return ast_new_stmt_typed(pool, sk, nm ? luaS_new(L, nm) : NULL, &body, line);
  }

  /* with：环境管理 with(expr) block */
  if (strcmp(kind, "with") == 0) {
    AstExpr *target = NULL;
    lua_getfield(L, idx, "target");
    if (!lua_isnil(L, -1)) target = ast_deserialize_expr(L, pool, lua_gettop(L));
    lua_pop(L, 1);
    AstBlock body;
    memset(&body, 0, sizeof(AstBlock));
    lua_getfield(L, idx, "body");
    ast_deserialize_block(L, pool, &body, lua_gettop(L));
    lua_pop(L, 1);
    return ast_new_stmt_with(pool, target, &body, line);
  }

  /* asm：内联汇编 */
  if (strcmp(kind, "asm") == 0) {
    const char *rb = get_field_str(L, idx, "raw_body");
    return ast_new_stmt_asm(pool, rb ? luaS_new(L, rb) : NULL, line);
  }

  /* export：注解占位（实际 parse 阶段会展开，这里仅占位） */
  if (strcmp(kind, "export") == 0) {
    return ast_new_stmt_empty(pool, line);
  }

  /* while let：while let pattern = expr do ... end */
  if (strcmp(kind, "whilelet") == 0) {
    lua_getfield(L, idx, "names");
    int nn = (int)luaL_len(L, -1);
    lua_pop(L, 1);
    TString **names = NULL;
    if (nn > 0) {
      names = ast_pool_alloc(pool, sizeof(TString *) * nn);
      lua_getfield(L, idx, "names");
      for (int i = 0; i < nn; i++) {
        lua_rawgeti(L, -1, i + 1);
        names[i] = luaS_new(L, lua_tostring(L, -1) ? lua_tostring(L, -1) : "_");
        lua_pop(L, 1);
      }
      lua_pop(L, 1);
    }
    AstExpr *expr = NULL;
    lua_getfield(L, idx, "expr");
    if (!lua_isnil(L, -1)) expr = ast_deserialize_expr(L, pool, lua_gettop(L));
    lua_pop(L, 1);
    AstStmt *s = ast_new_stmt_while_let(pool, nn, names, expr, line);
    lua_getfield(L, idx, "body");
    ast_deserialize_block(L, pool, &s->u.whilelet.body, lua_gettop(L));
    lua_pop(L, 1);
    s->u.whilelet.has_else = (int)get_field_bool(L, idx, "has_else");
    if (s->u.whilelet.has_else) {
      lua_getfield(L, idx, "else_body");
      ast_deserialize_block(L, pool, &s->u.whilelet.else_body, lua_gettop(L));
      lua_pop(L, 1);
    }
    return s;
  }

  /* guard：guard cond else { ... } / guard let v = val else { ... } */
  if (strcmp(kind, "guard") == 0) {
    AstExpr *cond = NULL;
    TString *lv = NULL;
    AstExpr *lval = NULL;
    lua_getfield(L, idx, "cond");
    if (!lua_isnil(L, -1)) cond = ast_deserialize_expr(L, pool, lua_gettop(L));
    lua_pop(L, 1);
    const char *letv = get_field_str(L, idx, "let_var");
    if (letv) lv = luaS_new(L, letv);
    lua_getfield(L, idx, "let_value");
    if (!lua_isnil(L, -1)) lval = ast_deserialize_expr(L, pool, lua_gettop(L));
    lua_pop(L, 1);
    AstBlock else_blk;
    memset(&else_blk, 0, sizeof(AstBlock));
    lua_getfield(L, idx, "else_block");
    ast_deserialize_block(L, pool, &else_blk, lua_gettop(L));
    lua_pop(L, 1);
    return ast_new_stmt_guard(pool, cond, lv, lval, &else_blk, line);
  }

  /* command / keyword / operator：三者都是 typed 结构 */
  if (strcmp(kind, "command") == 0 || strcmp(kind, "keyword") == 0 || strcmp(kind, "operator") == 0) {
    AstStmtKind sk = AST_STMT_COMMAND;
    if (strcmp(kind, "keyword") == 0) sk = AST_STMT_KEYWORD;
    else if (strcmp(kind, "operator") == 0) sk = AST_STMT_OPERATOR;
    const char *nm = get_field_str(L, idx, "name");
    AstBlock body;
    memset(&body, 0, sizeof(AstBlock));
    lua_getfield(L, idx, "body");
    ast_deserialize_block(L, pool, &body, lua_gettop(L));
    lua_pop(L, 1);
    return ast_new_stmt_typed(pool, sk, nm ? luaS_new(L, nm) : NULL, &body, line);
  }

  /* empty：空语句（分号） */
  if (strcmp(kind, "empty") == 0) {
    return ast_new_stmt_empty(pool, line);
  }

  /* constexpr：编译期条件 $if cond then ... $end */
  if (strcmp(kind, "constexpr") == 0) {
    const char *dir = get_field_str(L, idx, "directive");
    AstExpr *cond = NULL;
    lua_getfield(L, idx, "cond");
    if (!lua_isnil(L, -1)) cond = ast_deserialize_expr(L, pool, lua_gettop(L));
    lua_pop(L, 1);
    AstBlock body;
    memset(&body, 0, sizeof(AstBlock));
    lua_getfield(L, idx, "body");
    ast_deserialize_block(L, pool, &body, lua_gettop(L));
    lua_pop(L, 1);
    return ast_new_stmt_constexpr(pool, dir ? luaS_new(L, dir) : NULL, cond, &body, line);
  }

  /* 未知类型，返回空语句块 */
  return ast_new_stmt_block(pool, line);
}

/* 反序列化语句块 */
static void ast_deserialize_block(lua_State *L, AstPool *pool, AstBlock *blk, int idx) {
  if (!lua_istable(L, idx)) return;
  lua_getfield(L, idx, "body");
  if (!lua_istable(L, -1)) {
    lua_pop(L, 1);
    return;
  }
  int n = (int)luaL_len(L, -1);
  for (int i = 0; i < n; i++) {
    lua_rawgeti(L, -1, i + 1);
    AstStmt *s = ast_deserialize_stmt(L, pool, lua_gettop(L));
    ast_block_add_stmt(pool, blk, s);
    lua_pop(L, 1);
  }
  lua_pop(L, 1);
}

/* Lua table → AST 反序列化入口 */
AstChunk *ast_deserialize_from_lua(lua_State *L, int idx) {
  luaL_checktype(L, idx, LUA_TTABLE);

  /* 创建 AstPool */
  AstPool *pool = luaM_new(L, AstPool);
  ast_pool_init(L, pool);

  /* 创建 chunk */
  int line = (int)get_field_int(L, idx, "line");
  (void)line;
  AstChunk *chunk = ast_new_chunk(pool, luaS_newliteral(L, "deserialized"));
  chunk->pool = pool;

  /* 反序列化 body */
  lua_getfield(L, idx, "body");
  ast_deserialize_block(L, pool, &chunk->main_func->body, lua_gettop(L));
  lua_pop(L, 1);

  return chunk;
}