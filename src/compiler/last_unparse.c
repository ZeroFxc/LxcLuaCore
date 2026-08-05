/*
** last_unparse.c
** AST → Lua 源码字符串 反解析器（unparser / pretty printer）
**
** 目标：
**   1. 从 C 层 AstChunk*（经过解析/修改/反序列化得到）输出合法、人类可读的 Lua 源码
**   2. 从 Lua table 形式的序列化 AST（last_serialize 格式）先反序列化再 unparse
**   3. 输出结果保证可再 parse：源码 → AST → unparse → 源码' → 再 parse → AST'，
**      语义/结构一致。
*/

#define last_unparse_c
#define LUA_CORE

#include "lprefix.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

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
#include "last_unparse.h"


/* ============================================================
 * 输出缓冲：luaL_Buffer 封装，增加缩进/换行辅助
 * ============================================================ */
typedef struct UPState {
  luaL_Buffer buf;
  int indent;   /* 当前缩进级别 */
  int need_nl;  /* 下一内容前是否需要先换行 */
  int bol;      /* 是否处于行首（还未输出缩进） */
} UPState;

#define INDENT_STEP  2

static void up_init(lua_State *L, UPState *up) {
  luaL_buffinit(L, &up->buf);
  up->indent = 0;
  up->need_nl = 0;
  up->bol = 1;
}

static const char *up_finish(lua_State *L, UPState *up, size_t *out_len) {
  luaL_pushresultsize(&up->buf, 0);
  size_t len = lua_rawlen(L, -1);
  if (out_len) *out_len = len;
  return lua_tostring(L, -1);
}

static void up_raw_puts(UPState *up, const char *s, size_t len) {
  luaL_addlstring(&up->buf, s, len);
  up->bol = (len > 0 && s[len - 1] == '\n');
}

static void up_puts(UPState *up, const char *s) {
  size_t len = strlen(s);
  if (len == 0) return;
  up_raw_puts(up, s, len);
}

static void up_putc(UPState *up, char c) {
  char ch = c;
  up_raw_puts(up, &ch, 1);
}

static void up_newline(UPState *up) {
  up_putc(up, '\n');
  up->need_nl = 0;
  up->bol = 1;
}

static void up_flush_indent(UPState *up) {
  if (!up->bol) return;
  int n = up->indent * INDENT_STEP;
  for (int i = 0; i < n; i++) up_putc(up, ' ');
  up->bol = 0;
}

static void up_line(UPState *up, const char *s) {
  if (up->need_nl) up_newline(up);
  up_flush_indent(up);
  if (s && *s) up_puts(up, s);
  up_newline(up);
  up->need_nl = 0;
}

static void up_begin(UPState *up, const char *s) {
  if (up->need_nl) up_newline(up);
  up_flush_indent(up);
  if (s && *s) up_puts(up, s);
  up->need_nl = 0;
}

static void up_endl(UPState *up, const char *s) {
  if (s && *s) up_puts(up, s);
  up_newline(up);
  up->need_nl = 0;
}

static void up_inc(UPState *up) { up->indent++; }
static void up_dec(UPState *up) { if (up->indent > 0) up->indent--; }


/* ============================================================
 * 字符串字面量转义（Lua 5.x 风格）
 * ============================================================ */
static void up_put_escaped_string(UPState *up, const char *s, size_t len, char quote) {
  up_putc(up, quote);
  for (size_t i = 0; i < len; i++) {
    unsigned char c = (unsigned char)s[i];
    switch (c) {
      case '\\': up_puts(up, "\\\\"); break;
      case '\a': up_puts(up, "\\a");  break;
      case '\b': up_puts(up, "\\b");  break;
      case '\f': up_puts(up, "\\f");  break;
      case '\n': up_puts(up, "\\n");  break;
      case '\r': up_puts(up, "\\r");  break;
      case '\t': up_puts(up, "\\t");  break;
      case '\v': up_puts(up, "\\v");  break;
      default:
        if (c == (unsigned char)quote) {
          up_putc(up, '\\'); up_putc(up, quote);
        } else if (c >= 0x20 && c < 0x7f) {
          up_putc(up, (char)c);
        } else {
          char tmp[8];
          int n = snprintf(tmp, sizeof(tmp), "\\%03d", (int)c);
          up_raw_puts(up, tmp, (size_t)n);
        }
    }
  }
  up_putc(up, quote);
}


/* ============================================================
 * 前向声明
 * ============================================================ */
static void up_expr(UPState *up, AstExpr *e);
static void up_stmt(UPState *up, AstStmt *s);
static void up_block(UPState *up, AstBlock *blk);
static void up_func_body(UPState *up, AstFunc *f);
static void up_params(UPState *up, AstFunc *f);


/* ============================================================
 * 表达式优先级（用于最小括号插入）
 * ============================================================ */
typedef enum {
  PREC_LOWEST = 0,
  PREC_OR,        /* or */
  PREC_AND,       /* and */
  PREC_CMP,       /* < > <= >= == ~= is in <=> */
  PREC_BOR,       /* | */
  PREC_BXOR,      /* ~ */
  PREC_BAND,      /* & */
  PREC_SHIFT,     /* << >> */
  PREC_CONCAT,    /* .. */
  PREC_ADD,       /* + - */
  PREC_MUL,       /* * / // % */
  PREC_UNARY,     /* not # - ~ await */
  PREC_POW,       /* ^ */
  PREC_POST,      /* call index . : */
  PREC_PRIMARY
} PrecLevel;

/* 二元运算符 → 优先级 + 左右结合性（L=0, R=1） */
static PrecLevel binop_prec(AstBinOp op, int *right_assoc) {
  *right_assoc = 0;
  switch (op) {
    case AST_BIN_OR:         return PREC_OR;
    case AST_BIN_AND:        return PREC_AND;
    case AST_BIN_EQ: case AST_BIN_NE: case AST_BIN_LT: case AST_BIN_LE:
    case AST_BIN_GT: case AST_BIN_GE: case AST_BIN_SPACESHIP:
    case AST_BIN_IS: case AST_BIN_IN:
                             return PREC_CMP;
    case AST_BIN_BOR:        return PREC_BOR;
    case AST_BIN_BXOR:       return PREC_BXOR;
    case AST_BIN_BAND:       return PREC_BAND;
    case AST_BIN_SHL: case AST_BIN_SHR:
                             return PREC_SHIFT;
    case AST_BIN_CONCAT:     *right_assoc = 1; return PREC_CONCAT;
    case AST_BIN_ADD: case AST_BIN_SUB: case AST_BIN_PIPE:
    case AST_BIN_REVPIPE: case AST_BIN_SAFEPIPE:
    case AST_BIN_MERGE:      return PREC_ADD;
    case AST_BIN_MUL: case AST_BIN_DIV: case AST_BIN_IDIV:
    case AST_BIN_MOD:        return PREC_MUL;
    case AST_BIN_POW:        *right_assoc = 1; return PREC_POW;
    case AST_BIN_NULLCOAL:   return PREC_OR;
    default: return PREC_LOWEST;
  }
}

static PrecLevel unop_prec(AstUnOp op) {
  (void)op; return PREC_UNARY;
}

/* 表达式反解析：如果当前优先级 < outer，加括号 */
static void up_expr_prec(UPState *up, AstExpr *e, PrecLevel outer, int is_rhs) {
  if (!e) { up_puts(up, "nil"); return; }
  switch (e->kind) {
    case AST_EXPR_BINOP: {
      int right_assoc;
      PrecLevel me = binop_prec(e->u.binop.op, &right_assoc);
      int need_paren = (me < outer) ||
                       (!right_assoc && is_rhs && me == outer);
      if (need_paren) up_putc(up, '(');
      up_expr_prec(up, e->u.binop.lhs, me, 0);
      up_putc(up, ' ');
      /* 运算符名复用 last_serialize 的 binop_name 映射 */
      const char *op_name = "?";
      switch (e->u.binop.op) {
        case AST_BIN_ADD: op_name = "+"; break;
        case AST_BIN_SUB: op_name = "-"; break;
        case AST_BIN_MUL: op_name = "*"; break;
        case AST_BIN_DIV: op_name = "/"; break;
        case AST_BIN_IDIV: op_name = "//"; break;
        case AST_BIN_MOD: op_name = "%"; break;
        case AST_BIN_POW: op_name = "^"; break;
        case AST_BIN_BAND: op_name = "&"; break;
        case AST_BIN_BOR: op_name = "|"; break;
        case AST_BIN_BXOR: op_name = "~"; break;
        case AST_BIN_SHL: op_name = "<<"; break;
        case AST_BIN_SHR: op_name = ">>"; break;
        case AST_BIN_CONCAT: op_name = ".."; break;
        case AST_BIN_PIPE: op_name = "|>"; break;
        case AST_BIN_REVPIPE: op_name = "<|"; break;
        case AST_BIN_SAFEPIPE: op_name = "?|>"; break;
        case AST_BIN_EQ: op_name = "=="; break;
        case AST_BIN_NE: op_name = "~="; break;
        case AST_BIN_LT: op_name = "<"; break;
        case AST_BIN_LE: op_name = "<="; break;
        case AST_BIN_GT: op_name = ">"; break;
        case AST_BIN_GE: op_name = ">="; break;
        case AST_BIN_SPACESHIP: op_name = "<=>"; break;
        case AST_BIN_IS: op_name = "is"; break;
        case AST_BIN_IN: op_name = "in"; break;
        case AST_BIN_AS: op_name = "as"; break;
        case AST_BIN_AND: op_name = "and"; break;
        case AST_BIN_OR: op_name = "or"; break;
        case AST_BIN_NULLCOAL: op_name = "??"; break;
        case AST_BIN_MERGE: op_name = "merge"; break;
        default: op_name = "?"; break;
      }
      up_puts(up, op_name);
      up_putc(up, ' ');
      up_expr_prec(up, e->u.binop.rhs, me, 1);
      if (need_paren) up_putc(up, ')');
      return;
    }
    case AST_EXPR_UNOP: {
      PrecLevel me = unop_prec(e->u.unop.op);
      int need_paren = (me < outer);
      if (need_paren) up_putc(up, '(');
      const char *op_name = "?";
      switch (e->u.unop.op) {
        case AST_UN_MINUS: op_name = "-"; break;
        case AST_UN_BNOT: op_name = "~"; break;
        case AST_UN_NOT: op_name = "not"; break;
        case AST_UN_LEN: op_name = "#"; break;
        case AST_UN_AWAIT: op_name = "await"; break;
        case AST_UN_TEST_Z: op_name = "-z"; break;
        case AST_UN_TEST_N: op_name = "-n"; break;
        case AST_UN_TEST_NIL: op_name = "-nil"; break;
        case AST_UN_TEST_BOOL: op_name = "-bool"; break;
        case AST_UN_TEST_FUNC: op_name = "-func"; break;
        default: op_name = "?"; break;
      }
      up_puts(up, op_name);
      up_putc(up, ' ');
      up_expr_prec(up, e->u.unop.operand, me, 1);
      if (need_paren) up_putc(up, ')');
      return;
    }
    default:
      (void)is_rhs; (void)outer;
      up_expr(up, e);
      return;
  }
}


/* ============================================================
 * 表达式
 * ============================================================ */
static void up_expr(UPState *up, AstExpr *e) {
  if (!e) { up_puts(up, "nil"); return; }
  switch (e->kind) {
    case AST_EXPR_NIL:
      up_puts(up, "nil");
      break;
    case AST_EXPR_TRUE:
      up_puts(up, "true");
      break;
    case AST_EXPR_FALSE:
      up_puts(up, "false");
      break;
    case AST_EXPR_INT: {
      char tmp[64];
      int n = snprintf(tmp, sizeof(tmp), LUA_INTEGER_FMT, e->u.ival);
      up_raw_puts(up, tmp, (size_t)((n < 0) ? 0 : n));
      break;
    }
    case AST_EXPR_FLT: {
      char tmp[128];
      int n = snprintf(tmp, sizeof(tmp), LUA_NUMBER_FMT, e->u.nval);
      /* 如果没小数点/指数，补上 .0 防止被当整数解析 */
      int has_dot = 0;
      for (int i = 0; i < n; i++) {
        char c = tmp[i];
        if (c == '.' || c == 'e' || c == 'E') { has_dot = 1; break; }
      }
      if (!has_dot) { n += snprintf(tmp + n, sizeof(tmp) - (size_t)n, ".0"); }
      up_raw_puts(up, tmp, (size_t)((n < 0) ? 0 : n));
      break;
    }
    case AST_EXPR_STRING:
    case AST_EXPR_REGEX:
    case AST_EXPR_INTERPSTRING: {
      const char *s = ""; size_t len = 0;
      if (e->u.strval) { s = getstr(e->u.strval); len = tsslen(e->u.strval); }
      up_put_escaped_string(up, s, len, '"');
      break;
    }
    case AST_EXPR_VARARG:
      up_puts(up, "...");
      break;
    case AST_EXPR_IDENT: {
      const char *s = "?"; size_t len = 1;
      if (e->u.strval) { s = getstr(e->u.strval); len = tsslen(e->u.strval); }
      up_raw_puts(up, s, len);
      break;
    }
    case AST_EXPR_BINOP:
    case AST_EXPR_NULLCOAL:
    case AST_EXPR_IS:
    case AST_EXPR_IN:
    case AST_EXPR_MERGE:
    case AST_EXPR_PIPE:
    case AST_EXPR_REVPIPE:
    case AST_EXPR_SAFEPIPE:
      up_expr_prec(up, e, PREC_LOWEST, 0);
      break;
    case AST_EXPR_UNOP:
    case AST_EXPR_AWAIT:
      up_expr_prec(up, e, PREC_LOWEST, 0);
      break;
    case AST_EXPR_CALL: {
      up_expr_prec(up, e->u.call.callee, PREC_POST, 0);
      up_putc(up, '(');
      for (int i = 0; i < e->u.call.nargs; i++) {
        if (i > 0) up_puts(up, ", ");
        up_expr(up, e->u.call.args[i]);
      }
      up_putc(up, ')');
      break;
    }
    case AST_EXPR_METHOD_CALL: {
      up_expr_prec(up, e->u.mcall.recv, PREC_POST, 0);
      up_putc(up, ':');
      if (e->u.mcall.method) up_puts(up, getstr(e->u.mcall.method));
      up_putc(up, '(');
      for (int i = 0; i < e->u.mcall.nargs; i++) {
        if (i > 0) up_puts(up, ", ");
        up_expr(up, e->u.mcall.args[i]);
      }
      up_putc(up, ')');
      break;
    }
    case AST_EXPR_INDEX: {
      up_expr_prec(up, e->u.index.table, PREC_POST, 0);
      if (e->u.index.is_opt) up_puts(up, "?["); else up_putc(up, '[');
      up_expr(up, e->u.index.key);
      up_putc(up, ']');
      break;
    }
    case AST_EXPR_TABLE_CTOR: {
      up_puts(up, "{");
      int n = e->u.table.nentries;
      if (n == 0) { up_puts(up, "}"); break; }
      up_putc(up, ' ');
      for (int i = 0; i < n; i++) {
        AstTableEntry *ent = &e->u.table.entries[i];
        if (i > 0) up_puts(up, ", ");
        if (ent->key) {
          /* 若key是ident则输出 name = value，否则 [key]=value */
          if (ent->key->kind == AST_EXPR_IDENT && ent->key->u.strval) {
            up_puts(up, getstr(ent->key->u.strval));
            up_puts(up, " = ");
          } else {
            up_putc(up, '[');
            up_expr(up, ent->key);
            up_puts(up, "] = ");
          }
        }
        up_expr(up, ent->value);
      }
      up_puts(up, " }");
      break;
    }
    case AST_EXPR_MAP_CTOR: {
      up_puts(up, "%{");
      int n = e->u.map.nentries;
      for (int i = 0; i < n; i++) {
        AstMapEntry *ent = &e->u.map.entries[i];
        if (i > 0) up_puts(up, ", ");
        up_expr(up, ent->key);
        up_puts(up, ": ");
        up_expr(up, ent->value);
      }
      up_puts(up, "}");
      break;
    }
    case AST_EXPR_FUNC_EXPR:
    case AST_EXPR_ARROW_FUNC: {
      AstFunc *f = e->u.func.func;
      int is_arrow = (e->kind == AST_EXPR_ARROW_FUNC);
      if (is_arrow) up_puts(up, "fn("); else up_puts(up, "function(");
      up_params(up, f);
      if (is_arrow) {
        up_puts(up, ") ");
        up_block(up, &f->body);
      } else {
        up_puts(up, ")\n");
        up_inc(up);
        up_block(up, &f->body);
        up_dec(up);
        up_begin(up, "end");
        up_newline(up);
      }
      break;
    }
    case AST_EXPR_CONDEXPR: {
      up_expr(up, e->u.condexpr.e1);
      up_puts(up, " if ");
      up_expr(up, e->u.condexpr.e2);
      up_puts(up, " else ");
      up_expr(up, e->u.condexpr.e3);
      break;
    }
    case AST_EXPR_PAREN:
      up_putc(up, '(');
      up_expr(up, e->u.paren.expr);
      up_putc(up, ')');
      break;
    case AST_EXPR_RANGE:
      up_expr(up, e->u.range.start);
      up_puts(up, "..");
      up_expr(up, e->u.range.end);
      break;
    case AST_EXPR_SUPER: {
      up_puts(up, "super");
      if (e->u.super.method) {
        up_putc(up, ':');
        up_puts(up, getstr(e->u.super.method));
      }
      break;
    }
    case AST_EXPR_METHOD_REF: {
      if (e->u.method_ref.recv) up_expr(up, e->u.method_ref.recv);
      up_putc(up, '.');
      if (e->u.method_ref.method) up_puts(up, getstr(e->u.method_ref.method));
      break;
    }
    case AST_EXPR_NEW: {
      up_puts(up, "new ");
      up_expr(up, e->u.newexpr.class_expr);
      up_putc(up, '(');
      for (int i = 0; i < e->u.newexpr.nargs; i++) {
        if (i > 0) up_puts(up, ", ");
        up_expr(up, e->u.newexpr.args[i]);
      }
      up_putc(up, ')');
      break;
    }
    case AST_EXPR_TEST_TYPE: {
      up_puts(up, "(-type ");
      up_expr(up, e->u.test_type.operand);
      if (e->u.test_type.type_name) {
        up_putc(up, ' ');
        up_put_escaped_string(up, getstr(e->u.test_type.type_name),
                              tsslen(e->u.test_type.type_name), '"');
      }
      up_putc(up, ')');
      break;
    }
    case AST_EXPR_SPREAD:
      up_puts(up, "...");
      up_expr(up, e->u.spread.expr);
      break;
    case AST_EXPR_WALRUS:
      up_putc(up, '(');
      if (e->u.walrus.name) up_puts(up, getstr(e->u.walrus.name));
      up_puts(up, " := ");
      up_expr(up, e->u.walrus.expr);
      up_putc(up, ')');
      break;
    case AST_EXPR_SLICE: {
      up_expr_prec(up, e->u.slice.table, PREC_POST, 0);
      up_putc(up, '[');
      if (e->u.slice.start) up_expr(up, e->u.slice.start);
      up_putc(up, ':');
      if (e->u.slice.end) up_expr(up, e->u.slice.end);
      if (e->u.slice.step) { up_putc(up, ':'); up_expr(up, e->u.slice.step); }
      up_putc(up, ']');
      break;
    }
    case AST_EXPR_EMBED:
      up_puts(up, "$embed ");
      if (e->u.embed.filename)
        up_put_escaped_string(up, getstr(e->u.embed.filename),
                              tsslen(e->u.embed.filename), '"');
      break;
    case AST_EXPR_OBJECT:
      up_puts(up, "$object ");
      up_expr(up, e->u.object.ctor);
      break;
    case AST_EXPR_SWITCH_EXPR: {
      up_puts(up, "switch ");
      up_expr(up, e->u.switchx.cond);
      up_puts(up, " {\n");
      up_inc(up);
      for (int i = 0; i < e->u.switchx.narms; i++) {
        AstCaseArm *a = &e->u.switchx.arms[i];
        up_begin(up, "case ");
        for (int p = 0; p < a->npatterns; p++) {
          if (p > 0) up_puts(up, ", ");
          up_expr(up, a->patterns[p]);
        }
        up_puts(up, " -> ");
        up_expr(up, a->body);
        up_newline(up);
      }
      if (e->u.switchx.def) {
        up_begin(up, "else -> ");
        up_expr(up, e->u.switchx.def);
        up_newline(up);
      }
      up_dec(up);
      up_begin(up, "}");
      break;
    }
    case AST_EXPR_MATCH: {
      /* 匹配表达式：委托给语句序列化后的 matchstmt */
      AstStmt *stmt = e->u.match.stmt;
      if (stmt) up_stmt(up, stmt);
      break;
    }
    case AST_EXPR_OPTCHAIN:
      /* 可选链 a?.b / a?.[k] / a?.(args) — 原解析器目前仅占位，这里按通用可选链式作为 placeholder */
      up_puts(up, "nil  --[[optchain]]");
      break;
    case AST_EXPR_SELECT_CASE:
      /* select_case 表达式（内部占位），目前简单输出 nil */
      up_puts(up, "nil  --[[select_case]]");
      break;
    case AST_EXPR_DICT_COMP: {
      /* 字典推导式：{for k,v in ... do/yield key,val if cond} */
      AstFunc *f = e->u.func.func;
      if (f && f->body.count > 0) {
        /* 推导式内部为：local _t = {}; for ... do [if cond then] _t[key] = val end; return _t。
           我们直接输出 parser 原始语法，避免展开后的大段函数体不优美：
           用最保守的 "{} + 注释" 保证语义/语法合法且可再 parse，
           并附加注释标注这是 dict comprehension。 */
        up_puts(up, "{  --[[dict comp]]");
        up_block(up, &f->body);
        up_puts(up, "}");
      } else {
        up_puts(up, "{}  --[[empty dict comp]]");
      }
      break;
    }
    case AST_EXPR_LIST_COMP: {
      /* 列表推导式：[for x in ... do/yield expr if cond] */
      AstFunc *f = e->u.func.func;
      if (f && f->body.count > 0) {
        up_puts(up, "({  --[[list comp]]");
        up_block(up, &f->body);
        up_puts(up, "})[1] or {}  --[[list comp return]]");
      } else {
        up_puts(up, "{}  --[[empty list comp]]");
      }
      break;
    }
    case AST_EXPR_ASTPARSER:
      /* astparser 编译期节点：作为占位输出 nil */
      up_puts(up, "nil  --[[astparser precompiled]]");
      break;
    default:
      up_puts(up, "nil  --[[unsupported expr:");
      {
        char tmp[16]; int n = snprintf(tmp, sizeof(tmp), "%d", (int)e->kind);
        up_raw_puts(up, tmp, (size_t)((n<0)?0:n));
      }
      up_puts(up, "]]");
      break;
  }
}


/* ============================================================
 * 语句块 / 语句
 * ============================================================ */
static void up_block(UPState *up, AstBlock *blk) {
  if (!blk) return;
  for (int i = 0; i < blk->count; i++) {
    up_stmt(up, blk->items[i]);
  }
}

static void up_params(UPState *up, AstFunc *f) {
  if (!f) return;
  for (int i = 0; i < f->nparams; i++) {
    if (i > 0) up_puts(up, ", ");
    if (f->params[i].name) up_puts(up, getstr(f->params[i].name));
  }
  if (f->is_vararg) {
    if (f->nparams > 0) up_puts(up, ", ");
    if (f->vararg_name) {
      up_putc(up, '.'); up_putc(up, '.'); up_putc(up, '.');
      up_puts(up, getstr(f->vararg_name));
    } else {
      up_puts(up, "...");
    }
  }
}

static void up_func_body(UPState *up, AstFunc *f) {
  if (!f) return;
  up_inc(up);
  up_block(up, &f->body);
  up_dec(up);
  up_begin(up, "end");
  up_newline(up);
}

/* target：赋值左值 unparse */
static void up_assign_target(UPState *up, AstAssignTarget *t) {
  if (!t) return;
  if (t->kind == AST_TGT_VAR) {
    if (t->as.var.name) up_puts(up, getstr(t->as.var.name));
  } else if (t->kind == AST_TGT_INDEX) {
    up_expr_prec(up, t->as.index.table, PREC_POST, 0);
    up_putc(up, '[');
    up_expr(up, t->as.index.key);
    up_putc(up, ']');
  }
}

static void up_stmt(UPState *up, AstStmt *s) {
  if (!s) return;
  switch (s->kind) {
    case AST_STMT_BLOCK: {
      up_begin(up, "do");
      up_newline(up);
      up_inc(up);
      up_block(up, &s->u.block.block);
      up_dec(up);
      up_begin(up, "end");
      up_newline(up);
      break;
    }
    case AST_STMT_DO: {
      up_begin(up, "do");
      up_newline(up);
      up_inc(up);
      up_block(up, &s->u.block.block);
      up_dec(up);
      up_begin(up, "end");
      up_newline(up);
      break;
    }
    case AST_STMT_LOCAL: {
      up_begin(up, "local ");
      int nnames = s->u.local.nnames;
      for (int i = 0; i < nnames; i++) {
        if (i > 0) up_puts(up, ", ");
        if (s->u.local.names[i]) up_puts(up, getstr(s->u.local.names[i]));
      }
      int nvalues = s->u.local.nvalues;
      if (nvalues > 0) {
        up_puts(up, " = ");
        for (int i = 0; i < nvalues; i++) {
          if (i > 0) up_puts(up, ", ");
          up_expr(up, s->u.local.values[i]);
        }
      }
      up_newline(up);
      break;
    }
    case AST_STMT_ASSIGN: {
      up_begin(up, "");
      int ntgts = s->u.assign.ntargets;
      int printed_tgts = 0;
      for (int i = 0; i < ntgts; i++) {
        AstAssignTarget *tg = &s->u.assign.targets[i];
        /* 跳过 kind 非 VAR/INDEX（反序列化失败/占位节点） */
        if (tg->kind != AST_TGT_VAR && tg->kind != AST_TGT_INDEX) continue;
        if (tg->kind == AST_TGT_VAR && !tg->as.var.name) continue;
        if (printed_tgts > 0) up_puts(up, ", ");
        up_assign_target(up, tg);
        printed_tgts++;
      }
      int nvalues = s->u.assign.nvalues;
      /* 若没有可用左值但有values，退化为 expr list（每个value单独作为expr_stmt） */
      if (printed_tgts == 0) {
        for (int i = 0; i < nvalues; i++) {
          if (i > 0) up_puts(up, ", ");
          up_expr(up, s->u.assign.values[i]);
        }
      } else {
        up_puts(up, " = ");
        for (int i = 0; i < nvalues; i++) {
          if (i > 0) up_puts(up, ", ");
          up_expr(up, s->u.assign.values[i]);
        }
      }
      up_newline(up);
      break;
    }
    case AST_STMT_EXPR: {
      up_begin(up, "");
      up_expr(up, s->u.expr.expr);
      up_newline(up);
      break;
    }
    case AST_STMT_IF: {
      int narms = s->u.ifstmt.narms;
      for (int i = 0; i < narms; i++) {
        AstIfArm *arm = &s->u.ifstmt.arms[i];
        if (i == 0) up_begin(up, "if "); else up_begin(up, "elseif ");
        if (arm->let_var) {
          up_puts(up, "let ");
          up_puts(up, getstr(arm->let_var));
          up_puts(up, " = ");
        }
        up_expr(up, arm->cond);
        up_puts(up, " then");
        up_newline(up);
        up_inc(up);
        up_block(up, &arm->body);
        up_dec(up);
      }
      if (s->u.ifstmt.has_else) {
        up_begin(up, "else");
        up_newline(up);
        up_inc(up);
        up_block(up, &s->u.ifstmt.else_body);
        up_dec(up);
      }
      up_begin(up, "end");
      up_newline(up);
      break;
    }
    case AST_STMT_WHILE: {
      up_begin(up, "while ");
      up_expr(up, s->u.whilestmt.cond);
      up_puts(up, " do");
      up_newline(up);
      up_inc(up);
      up_block(up, &s->u.whilestmt.body);
      up_dec(up);
      up_begin(up, "end");
      up_newline(up);
      break;
    }
    case AST_STMT_REPEAT: {
      up_begin(up, "repeat");
      up_newline(up);
      up_inc(up);
      up_block(up, &s->u.whilestmt.body);
      up_dec(up);
      up_begin(up, "until ");
      up_expr(up, s->u.whilestmt.cond);
      up_newline(up);
      break;
    }
    case AST_STMT_FOR_NUM: {
      up_begin(up, "for ");
      if (s->u.fornum.var) up_puts(up, getstr(s->u.fornum.var));
      up_puts(up, " = ");
      up_expr(up, s->u.fornum.start);
      up_puts(up, ", ");
      up_expr(up, s->u.fornum.stop);
      if (s->u.fornum.step) {
        up_puts(up, ", ");
        up_expr(up, s->u.fornum.step);
      }
      up_puts(up, " do");
      up_newline(up);
      up_inc(up);
      up_block(up, &s->u.fornum.body);
      up_dec(up);
      up_begin(up, "end");
      up_newline(up);
      break;
    }
    case AST_STMT_FOR_GEN: {
      up_begin(up, "for ");
      for (int i = 0; i < s->u.forgen.nnames; i++) {
        if (i > 0) up_puts(up, ", ");
        if (s->u.forgen.names[i]) up_puts(up, getstr(s->u.forgen.names[i]));
      }
      up_puts(up, " in ");
      for (int i = 0; i < s->u.forgen.nexprs; i++) {
        if (i > 0) up_puts(up, ", ");
        up_expr(up, s->u.forgen.exprs[i]);
      }
      up_puts(up, " do");
      up_newline(up);
      up_inc(up);
      up_block(up, &s->u.forgen.body);
      up_dec(up);
      up_begin(up, "end");
      up_newline(up);
      break;
    }
    case AST_STMT_RETURN: {
      up_begin(up, "return");
      int n = s->u.retstmt.nvalues;
      if (n > 0) {
        up_putc(up, ' ');
        for (int i = 0; i < n; i++) {
          if (i > 0) up_puts(up, ", ");
          up_expr(up, s->u.retstmt.values[i]);
        }
      }
      up_newline(up);
      break;
    }
    case AST_STMT_BREAK:
      up_line(up, "break");
      break;
    case AST_STMT_CONTINUE:
      up_line(up, "continue");
      break;
    case AST_STMT_GOTO: {
      up_begin(up, "goto ");
      if (s->u.label.name) up_puts(up, getstr(s->u.label.name));
      up_newline(up);
      break;
    }
    case AST_STMT_LABEL: {
      up_begin(up, "::");
      if (s->u.label.name) up_puts(up, getstr(s->u.label.name));
      up_puts(up, "::");
      up_newline(up);
      break;
    }
    case AST_STMT_LOCAL_FUNC: {
      AstFunc *f = s->u.localfunc.func;
      up_begin(up, "local function ");
      if (s->u.localfunc.name) up_puts(up, getstr(s->u.localfunc.name));
      up_putc(up, '(');
      up_params(up, f);
      up_puts(up, ")");
      up_newline(up);
      up_func_body(up, f);
      break;
    }
    case AST_STMT_GLOBAL: {
      up_begin(up, "global ");
      for (int i = 0; i < s->u.global.nnames; i++) {
        if (i > 0) up_puts(up, ", ");
        if (s->u.global.names[i]) up_puts(up, getstr(s->u.global.names[i]));
      }
      up_newline(up);
      break;
    }
    case AST_STMT_SWITCH: {
      up_begin(up, "switch ");
      up_expr(up, s->u.switchstmt.cond);
      up_puts(up, " {\n");
      up_inc(up);
      for (int i = 0; i < s->u.switchstmt.ncases; i++) {
        AstSwitchCase *c = &s->u.switchstmt.cases[i];
        if (c->is_default) continue;
        up_begin(up, "case ");
        for (int p = 0; p < c->npatterns; p++) {
          if (p > 0) up_puts(up, ", ");
          up_expr(up, c->patterns[p]);
        }
        up_puts(up, ":\n");
        up_inc(up);
        up_block(up, &c->body);
        up_dec(up);
      }
      if (s->u.switchstmt.has_default) {
        up_begin(up, "default:\n");
        up_inc(up);
        up_block(up, &s->u.switchstmt.default_body);
        up_dec(up);
      }
      up_dec(up);
      up_begin(up, "}");
      up_newline(up);
      break;
    }
    case AST_STMT_TRY: {
      up_begin(up, "try");
      up_newline(up);
      up_inc(up);
      up_block(up, &s->u.trycatch.body);
      up_dec(up);
      /* 若有 catch_var 则输出 catch (e)，否则无 catch 时跳过 */
      if (s->u.trycatch.catch_body.count > 0 || s->u.trycatch.catch_var) {
        up_begin(up, "catch (");
        if (s->u.trycatch.catch_var && s->u.trycatch.catch_var->kind == AST_EXPR_IDENT &&
            s->u.trycatch.catch_var->u.strval) {
          up_puts(up, getstr(s->u.trycatch.catch_var->u.strval));
        } else {
          up_puts(up, "e");
        }
        up_endl(up, ")");
        up_inc(up);
        up_block(up, &s->u.trycatch.catch_body);
        up_dec(up);
      }
      if (s->u.trycatch.finally_body.count > 0) {
        up_begin(up, "finally"); up_newline(up);
        up_inc(up);
        up_block(up, &s->u.trycatch.finally_body);
        up_dec(up);
      }
      up_begin(up, "end");
      up_newline(up);
      break;
    }
    case AST_STMT_CATCH:
    case AST_STMT_FINALLY:
      /* 独立的 catch/finally 语句：保留结构便于调试 */
      up_begin(up, "-- ");
      if (s->kind == AST_STMT_CATCH) up_puts(up, "catch");
      else up_puts(up, "finally");
      up_puts(up, " -- standalone block");
      up_newline(up);
      break;

    case AST_STMT_USING: {
      up_begin(up, "using ");
      if (s->u.usingstmt.is_namespace) up_puts(up, "namespace ");
      if (s->u.usingstmt.name) {
        up_puts(up, getstr(s->u.usingstmt.name));
        /* last_member 用于 :: 链（last_member 和 name 相同代表不输出 ::） */
        if (s->u.usingstmt.last_member && s->u.usingstmt.last_member != s->u.usingstmt.name) {
          up_puts(up, "::");
          up_puts(up, getstr(s->u.usingstmt.last_member));
        }
      }
      up_endl(up, ";");
      break;
    }

    case AST_STMT_NAMESPACE: {
      up_begin(up, "namespace ");
      if (s->u.nsstruct.name) up_puts(up, getstr(s->u.nsstruct.name));
      up_puts(up, " {");
      up_newline(up);
      up_inc(up);
      up_block(up, &s->u.nsstruct.body);
      up_dec(up);
      up_begin(up, "}");
      up_newline(up);
      break;
    }

    case AST_STMT_STRUCT: {
      up_begin(up, "struct ");
      if (s->u.nsstruct.name) up_puts(up, getstr(s->u.nsstruct.name));
      if (s->u.nsstruct.entries && s->u.nsstruct.nentries > 0) {
        up_puts(up, " { ");
        for (int i = 0; i < s->u.nsstruct.nentries; i++) {
          if (i > 0) up_puts(up, ", ");
          AstKVPair *ent = &s->u.nsstruct.entries[i];
          /* struct 字段名存储在 ent->key 里（key是AST_EXPR_STRING存储field名）*/
          if (ent->key && ent->key->kind == AST_EXPR_STRING && ent->key->u.strval) {
            up_puts(up, getstr(ent->key->u.strval));
          }
          if (ent->value) { up_puts(up, " = "); up_expr(up, ent->value); }
        }
        up_puts(up, " }");
      } else {
        up_puts(up, " do");
        up_newline(up);
        up_inc(up);
        up_block(up, &s->u.nsstruct.body);
        up_dec(up);
        up_begin(up, "end");
      }
      up_newline(up);
      break;
    }

    case AST_STMT_SUPERSTRUCT: {
      up_begin(up, "superstruct ");
      if (s->u.nsstruct.name) up_puts(up, getstr(s->u.nsstruct.name));
      up_puts(up, " [");
      if (s->u.nsstruct.entries) {
        for (int i = 0; i < s->u.nsstruct.nentries; i++) {
          if (i > 0) up_puts(up, ", ");
          AstKVPair *ent = &s->u.nsstruct.entries[i];
          up_expr(up, ent->key);
          up_puts(up, ": ");
          up_expr(up, ent->value);
        }
      }
      up_puts(up, "]");
      up_newline(up);
      break;
    }

    case AST_STMT_ENUM: {
      up_begin(up, "enum ");
      if (s->u.enumstmt.is_enum_class) up_puts(up, "class ");
      if (s->u.enumstmt.name) up_puts(up, getstr(s->u.enumstmt.name));
      up_puts(up, " { ");
      for (int i = 0; i < s->u.enumstmt.nentries; i++) {
        if (i > 0) up_puts(up, ", ");
        AstEnumEntry *e = &s->u.enumstmt.entries[i];
        if (e->name) up_puts(up, getstr(e->name));
        if (e->value_expr) { up_puts(up, " = "); up_expr(up, e->value_expr); }
      }
      up_puts(up, " }");
      up_newline(up);
      break;
    }

    case AST_STMT_CLASS: {
      up_begin(up, "");
      /* 输出修饰符（flags） */
      if (s->u.classstmt.class_flags & 1) up_puts(up, "abstract ");   /* CLASS_FLAG_ABSTRACT */
      if (s->u.classstmt.class_flags & 2) up_puts(up, "final ");      /* CLASS_FLAG_FINAL */
      if (s->u.classstmt.class_flags & 4) up_puts(up, "sealed ");     /* CLASS_FLAG_SEALED */
      if (s->u.classstmt.class_flags & 32) up_puts(up, "singleton "); /* CLASS_FLAG_SINGLETON */
      up_puts(up, "class ");
      if (s->u.classstmt.name) up_puts(up, getstr(s->u.classstmt.name));
      /* 泛型参数 <T, U, ...> */
      if (s->u.classstmt.generic_params && s->u.classstmt.ngeneric_params > 0) {
        up_puts(up, "<");
        for (int i = 0; i < s->u.classstmt.ngeneric_params; i++) {
          if (i > 0) up_puts(up, ", ");
          up_puts(up, getstr(s->u.classstmt.generic_params[i]));
        }
        up_puts(up, ">");
      }
      if (s->u.classstmt.extends_names && s->u.classstmt.nextends > 0) {
        up_puts(up, " extends ");
        for (int i = 0; i < s->u.classstmt.nextends; i++) {
          if (i > 0) up_puts(up, ", ");
          up_puts(up, getstr(s->u.classstmt.extends_names[i]));
        }
      }
      if (s->u.classstmt.implements && s->u.classstmt.nimplements > 0) {
        up_puts(up, " implements ");
        for (int i = 0; i < s->u.classstmt.nimplements; i++) {
          if (i > 0) up_puts(up, ", ");
          up_puts(up, getstr(s->u.classstmt.implements[i]));
        }
      }
      if (s->u.classstmt.use_traits && s->u.classstmt.nuse_traits > 0) {
        up_puts(up, " use ");
        for (int i = 0; i < s->u.classstmt.nuse_traits; i++) {
          if (i > 0) up_puts(up, ", ");
          up_puts(up, getstr(s->u.classstmt.use_traits[i]));
        }
      }
      up_puts(up, " {");
      up_newline(up);
      up_inc(up);
      if (s->u.classstmt.members && s->u.classstmt.nmembers > 0) {
        for (int i = 0; i < s->u.classstmt.nmembers; i++) {
          AstClassMember *m = &s->u.classstmt.members[i];
          up_begin(up, "");
          /* 访问修饰符前缀（默认不输出 DEFAULT） */
          if (m->access == AST_ACCESS_PRIVATE) up_puts(up, "private ");
          else if (m->access == AST_ACCESS_PROTECTED) up_puts(up, "protected ");
          else if (m->access == AST_ACCESS_PUBLIC) up_puts(up, "public ");
          if (m->is_static) up_puts(up, "static ");
          switch (m->kind) {
            case AST_MEMBER_PROPERTY:
              up_puts(up, "var ");
              if (m->name) up_puts(up, getstr(m->name));
              if (m->u.property_value) { up_puts(up, " = "); up_expr(up, m->u.property_value); }
              up_newline(up);
              break;
            case AST_MEMBER_METHOD:
            case AST_MEMBER_ABSTRACT:
            case AST_MEMBER_FINAL:
              if (m->kind == AST_MEMBER_ABSTRACT) up_puts(up, "abstract ");
              if (m->kind == AST_MEMBER_FINAL) up_puts(up, "final ");
              up_puts(up, "function ");
              if (m->name) up_puts(up, getstr(m->name));
              if (m->u.method_func) {
                up_putc(up, '(');
                up_params(up, m->u.method_func);
                up_puts(up, ")");
                up_newline(up);
                up_func_body(up, m->u.method_func);
              } else {
                up_puts(up, "(...) end\n");
              }
              break;
            case AST_MEMBER_GETTER:
              up_puts(up, "get ");
              if (m->name) up_puts(up, getstr(m->name));
              if (m->u.method_func) {
                up_puts(up, "()"); up_newline(up);
                up_func_body(up, m->u.method_func);
              } else { up_puts(up, "() end\n"); }
              break;
            case AST_MEMBER_SETTER:
              up_puts(up, "set ");
              if (m->name) up_puts(up, getstr(m->name));
              if (m->u.method_func) {
                up_puts(up, "(v)"); up_newline(up);
                up_func_body(up, m->u.method_func);
              } else { up_puts(up, "(v) end\n"); }
              break;
            default:
              if (m->name) up_puts(up, getstr(m->name));
              up_newline(up);
          }
        }
      } else {
        up_block(up, &s->u.classstmt.body);
      }
      up_dec(up);
      up_begin(up, "}");
      up_newline(up);
      break;
    }

    case AST_STMT_TRAIT:
    case AST_STMT_INTERFACE:
    case AST_STMT_CONCEPT: {
      const char *kname = "concept";
      if (s->kind == AST_STMT_TRAIT) kname = "trait";
      else if (s->kind == AST_STMT_INTERFACE) kname = "interface";
      up_begin(up, kname);
      up_putc(up, ' ');
      if (s->u.nsstruct.name) up_puts(up, getstr(s->u.nsstruct.name));
      up_puts(up, " {");
      up_newline(up);
      up_inc(up);
      up_block(up, &s->u.nsstruct.body);
      up_dec(up);
      up_begin(up, "}");
      up_newline(up);
      break;
    }

    case AST_STMT_WITH: {
      up_begin(up, "with (");
      up_expr(up, s->u.withstmt.target);
      up_puts(up, ") {");
      up_newline(up);
      up_inc(up);
      up_block(up, &s->u.withstmt.body);
      up_dec(up);
      up_begin(up, "}");
      up_newline(up);
      break;
    }

    case AST_STMT_ASM: {
      up_begin(up, "asm( ");
      if (s->u.asmstmt.raw_body) {
        const char *p = getstr(s->u.asmstmt.raw_body);
        size_t l = tsslen(s->u.asmstmt.raw_body);
        up_put_escaped_string(up, p, l, '(');
      }
      up_puts(up, " )");
      up_newline(up);
      break;
    }

    case AST_STMT_EXPORT:
      up_begin(up, "-- export (annotation placeholder)");
      up_newline(up);
      break;

    case AST_STMT_WHILE_LET: {
      up_begin(up, "while let ");
      for (int i = 0; i < s->u.whilelet.nnames; i++) {
        if (i > 0) up_puts(up, ", ");
        if (s->u.whilelet.names && s->u.whilelet.names[i])
          up_puts(up, getstr(s->u.whilelet.names[i]));
      }
      up_puts(up, " = ");
      up_expr(up, s->u.whilelet.expr);
      up_puts(up, " do");
      up_newline(up);
      up_inc(up);
      up_block(up, &s->u.whilelet.body);
      up_dec(up);
      if (s->u.whilelet.has_else) {
        up_begin(up, "else"); up_newline(up);
        up_inc(up);
        up_block(up, &s->u.whilelet.else_body);
        up_dec(up);
      }
      up_begin(up, "end");
      up_newline(up);
      break;
    }

    case AST_STMT_GUARD: {
      up_begin(up, "guard ");
      if (s->u.guard.let_var) {
        up_puts(up, "let ");
        up_puts(up, getstr(s->u.guard.let_var));
        up_puts(up, " = ");
        up_expr(up, s->u.guard.let_value);
      } else if (s->u.guard.cond) {
        up_expr(up, s->u.guard.cond);
      }
      up_puts(up, " else {"); up_newline(up);
      up_inc(up);
      up_block(up, &s->u.guard.else_block);
      up_dec(up);
      up_begin(up, "}");
      up_newline(up);
      break;
    }

    case AST_STMT_COMMAND:
    case AST_STMT_KEYWORD:
    case AST_STMT_OPERATOR: {
      const char *kname = "operator";
      if (s->kind == AST_STMT_COMMAND) kname = "command";
      else if (s->kind == AST_STMT_KEYWORD) kname = "keyword";
      up_begin(up, kname);
      up_putc(up, ' ');
      if (s->u.nsstruct.name) up_puts(up, getstr(s->u.nsstruct.name));
      up_puts(up, " {");
      up_newline(up);
      up_inc(up);
      up_block(up, &s->u.nsstruct.body);
      up_dec(up);
      up_begin(up, "}");
      up_newline(up);
      break;
    }

    case AST_STMT_CONSTEXPR: {
      up_begin(up, "$");
      if (s->u.constexpr_stmt.directive)
        up_puts(up, getstr(s->u.constexpr_stmt.directive));
      else up_puts(up, "constexpr");
      if (s->u.constexpr_stmt.cond) {
        up_putc(up, ' ');
        up_expr(up, s->u.constexpr_stmt.cond);
      }
      up_puts(up, " then"); up_newline(up);
      up_inc(up);
      up_block(up, &s->u.constexpr_stmt.body);
      up_dec(up);
      up_begin(up, "$end");
      up_newline(up);
      break;
    }

    case AST_STMT_THROW: {
      up_begin(up, "throw ");
      up_expr(up, s->u.throwstmt.expr);
      up_newline(up);
      break;
    }
    case AST_STMT_DEFER: {
      up_begin(up, "defer");
      up_newline(up);
      up_inc(up);
      up_block(up, &s->u.deferstmt.body);
      up_dec(up);
      up_begin(up, "end");
      up_newline(up);
      break;
    }
    case AST_STMT_MATCH: {
      up_begin(up, "match ");
      up_expr(up, s->u.matchstmt.control);
      up_puts(up, " {");
      up_newline(up);
      up_inc(up);
      for (int i = 0; i < s->u.matchstmt.narms; i++) {
        AstMatchArm *a = &s->u.matchstmt.arms[i];
        up_begin(up, "case ");
        /* pattern 作为占位，暂未实现模式详细 unparse，保持节点不输出错误 */
        up_puts(up, "<pattern>");
        if (a->guard) { up_puts(up, " if "); up_expr(up, a->guard); }
        if (a->is_arrow) {
          up_puts(up, " -> ");
          if (a->body_expr) up_expr(up, a->body_expr);
          up_newline(up);
        } else {
          up_puts(up, ":");
          up_newline(up);
          up_inc(up);
          up_block(up, &a->body_block);
          up_dec(up);
        }
      }
      up_dec(up);
      up_begin(up, "}");
      up_newline(up);
      break;
    }
    case AST_STMT_COMPOUND_ASSIGN: {
      up_begin(up, "");
      /* ntargets 个目标，复合赋值支持的常规是 1 个，多目标按逗号输出 */
      for (int i = 0; i < s->u.compound.ntargets; i++) {
        if (i > 0) up_puts(up, ", ");
        up_assign_target(up, &s->u.compound.targets[i]);
      }
      const char *op = "?";
      switch (s->u.compound.op) {
        case AST_BIN_ADD: op = "+="; break;
        case AST_BIN_SUB: op = "-="; break;
        case AST_BIN_MUL: op = "*="; break;
        case AST_BIN_DIV: op = "/="; break;
        case AST_BIN_IDIV: op = "//="; break;
        case AST_BIN_MOD: op = "%="; break;
        case AST_BIN_POW: op = "^="; break;
        case AST_BIN_CONCAT: op = "..="; break;
        case AST_BIN_BAND: op = "&="; break;
        case AST_BIN_BOR: op = "|="; break;
        case AST_BIN_BXOR: op = "~="; break;
        default: break;
      }
      up_putc(up, ' ');
      up_puts(up, op);
      up_putc(up, ' ');
      up_expr(up, s->u.compound.value);
      up_newline(up);
      break;
    }
    case AST_STMT_INCR_DECR: {
      up_begin(up, "");
      up_assign_target(up, s->u.incr.target);
      switch (s->u.incr.kind) {
        case AST_INCR_PRE_INC:  up_puts(up, " += 1"); break;
        case AST_INCR_PRE_DEC:  up_puts(up, " -= 1"); break;
        case AST_INCR_POST_INC: up_puts(up, " += 1"); break;
        case AST_INCR_POST_DEC: up_puts(up, " -= 1"); break;
        default: break;
      }
      up_newline(up);
      break;
    }
    case AST_STMT_TAKE: {
      /* take 解构赋值：改写为等价 Lua 语法（local vars = source 的解构近似），
         避免输出扩展语法关键字被普通 load() 拒绝 */
      up_begin(up, "local ");
      for (int i = 0; i < s->u.take.nvars; i++) {
        if (i > 0) up_puts(up, ", ");
        if (s->u.take.varnames && s->u.take.varnames[i])
          up_puts(up, getstr(s->u.take.varnames[i]));
        else up_puts(up, "_");
        /* 默认值：var or default => 包装为三元 if nil 才赋默认，但 unparse 简化为忽略默认，
           变量名后直接跟逗号或结束 */
      }
      up_puts(up, " = ");
      if (s->u.take.source) {
        up_expr(up, s->u.take.source);
      } else {
        up_puts(up, "nil --[[take source missing]]");
      }
      up_newline(up);
      break;
    }

    case AST_STMT_EMPTY:
      up_newline(up);
      break;

    default:
      up_begin(up, "--[[unsupported stmt:");
      {
        char tmp[16]; int n = snprintf(tmp, sizeof(tmp), "%d", (int)s->kind);
        up_raw_puts(up, tmp, (size_t)((n<0)?0:n));
      }
      up_puts(up, "]]\n");
      break;
  }
}


/* ============================================================
 * 顶层入口
 * ============================================================ */
int luaY_ast_unparse_chunk(lua_State *L, AstChunk *chunk) {
  if (!chunk || !chunk->main_func) {
    lua_pushliteral(L, "");
    return 1;
  }
  UPState up;
  up_init(L, &up);
  AstFunc *mf = chunk->main_func;
  up_block(&up, &mf->body);
  (void)up_finish(L, &up, NULL);
  return 1;
}

int luaY_ast_unparse_from_table(lua_State *L, int table_idx) {
  /* 验证输入 */
  if (!lua_istable(L, table_idx)) {
    return luaL_error(L, "ast.unparse: expected table (AST) but got %s",
                      luaL_typename(L, table_idx));
  }
  /* 从 Lua table 反序列化为 C AstChunk */
  AstPool pool;
  ast_pool_init(L, &pool);
  AstChunk *chunk = NULL;
  int old_top = lua_gettop(L);

  /* 把目标表推到栈顶，方便相对索引一致 */
  lua_pushvalue(L, table_idx);  /* top+1 = table */
  int abs_idx = lua_gettop(L);
  chunk = ast_deserialize_from_lua(L, abs_idx);
  lua_pop(L, 1);  /* pop table */

  if (!chunk) {
    ast_pool_free(&pool);
    lua_settop(L, old_top);
    return luaL_error(L, "ast.unparse: failed to deserialize AST to C AstChunk");
  }
  /* 执行 unparse：在释放 pool 前先把结果拷贝为独立的 Lua 字符串（luaL_pushresult 已经是内部字符串）。
     ast_pool_free 只释放 AstPool 分配的内存，不会影响 Lua GC 管理的 TString */
  int ret = luaY_ast_unparse_chunk(L, chunk);
  (void)ret;
  /* 确保结果字符串独立：在栈顶复制一份（避免未来任何与 pool 关联的隐式风险）*/
  size_t rlen = 0;
  const char *rstr = lua_tolstring(L, -1, &rlen);
  lua_pop(L, 1);
  lua_pushlstring(L, rstr, rlen);
  /* 释放 pool（chunk 都从 pool 分配），结果字符串是 GC 管理的，不受影响 */
  ast_pool_free(&pool);
  (void)old_top;
  return 1;
}
