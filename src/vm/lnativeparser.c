/*
** $Id: lnativeparser.c $
** NLang 编译器 — 把 Lua-like 高级语言翻译为 NativeVM 字节码
**
** === NLang 1.0 语言规范 ===
**
** 注释：    -- 行尾 或 --[[ ... ]]
** 标识符：  [a-zA-Z_][a-zA-Z0-9_]*
** 整数：    123, 0xFF, 0b101
** 浮点：    3.14, 1.5e-3, .5
** 字符串：  不支持（VM 没有 string 类型）
**
** 关键字：  local if else elseif then end while do repeat until for return
**          nil true false function break and or not
**
** 运算符：  +  -  *  /  %  &  |  ^  <<  >>
**          <  <= >  >= ==  ~=
**          and  or  not
**          =  (赋值)
**
** 语句：
**   local x = expr           局部变量声明
**   x = expr                 赋值
**   if expr then block [elseif expr then block]* [else block] end
**   while expr do block end
**   repeat block until expr
**   for var = a, b [, c] do block end
**   return [expr]
**   break
**
** 表达式（按优先级从低到高）：
**   or
**   and
**   == ~= < <= > >=
**   |            （按位或）
**   ^            （按位异或）
**   &            （按位与）
**   << >>
**   + -
**   * / %
**   not - (一元)
**   字面量 / 变量 / (expr)
**
** 不支持：
**   - 函数定义与调用（VM 没有 CALL 指令）
**   - 字符串
**   - 表
**   - 多重赋值
**   - 闭包
**
** 寄存器分配：
**   R0..R223   用户变量
**   R224..R255 编译器临时寄存器
*/

#define lnativeparser_c
#define LUA_LIB

#include "lprefix.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdarg.h>
#include <stdint.h>

#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"


/* ============================================================
** 复用 NativeVM 的指令编码格式与操作码常量
** ============================================================ */

#define NI_OP(i)   ((int)((i) & 0xFF))
#define NI_A(i)    ((int)(((i) >> 8) & 0xFF))
#define NI_B(i)    ((int)(((i) >> 16) & 0xFF))
#define NI_C(i)    ((int)(((i) >> 24) & 0xFF))
#define NI_IMM(i)  ((int32_t)((int64_t)(i) >> 32))

/**
 * @brief 构造一条 64-bit 指令
 * @param op  操作码
 * @param a   A 字段
 * @param b   B 字段
 * @param c   C 字段
 * @param imm 32 位立即数
 * @return 64-bit 指令整数
 */
static lua_Integer np_make_ni(int op, int a, int b, int c, int32_t imm) {
  return ((lua_Integer)(uint32_t)imm << 32)
       | ((lua_Integer)(c & 0xFF) << 24)
       | ((lua_Integer)(b & 0xFF) << 16)
       | ((lua_Integer)(a & 0xFF) << 8)
       | (lua_Integer)(op & 0xFF);
}

enum {
  NI_NOP = 0,  NI_LOADK,    NI_LOADKF,   NI_LOADK64,  NI_LOADKHI,
  NI_MOV,      NI_ADD,      NI_SUB,      NI_MUL,      NI_DIV,
  NI_MOD,      NI_ADDF,     NI_SUBF,     NI_MULF,     NI_DIVF,
  NI_AND,      NI_OR,       NI_XOR,      NI_SHL,      NI_SHR,
  NI_EQ,       NI_NE,       NI_LT,       NI_LE,       NI_LTF,
  NI_LEF,      NI_JMP,      NI_JT,       NI_JF,       NI_RET,
  NI_I2F,      NI_F2I,      NI_NEG,      NI_NEGF,     NI_MOVF,
  NI_MOVI,     NI_SETNIL,   NI_ISNIL,    NI_SQRT,     NI_HALT,
  NI_MAX
};


/* ============================================================
** 词法分析器（Lexer）
** ============================================================ */

enum TokenType {
  TK_EOF, TK_IDENT, TK_INT, TK_FLOAT, TK_KEYWORD, TK_STRING,
  TK_OP, TK_LPAREN, TK_RPAREN, TK_COMMA, TK_SEMI, TK_DOT, TK_COLON,
  TK_LBRACE, TK_RBRACE
};

enum Keyword {
  KW_LOCAL, KW_IF, KW_ELSE, KW_ELSEIF, KW_THEN, KW_END,
  KW_WHILE, KW_DO, KW_REPEAT, KW_UNTIL,
  KW_FOR,
  KW_RETURN,
  KW_NIL, KW_TRUE, KW_FALSE,
  KW_FUNCTION,
  KW_BREAK,
  KW_AND, KW_OR, KW_NOT,
  KW___COUNT__
};

typedef struct {
  int type;
  int kw;
  int64_t ival;
  double  fval;
  const char *s;
  int slen;
  int line;
} Token;

typedef struct {
  const char *src;
  int pos;
  int len;
  int line;
  char err[256];
  int has_err;
} Lexer;

static int np_keyword(const char *s, int n) {
  static const struct { const char *name; int kw; } kws[] = {
    {"local",   KW_LOCAL},  {"if",      KW_IF},     {"else",    KW_ELSE},
    {"elseif",  KW_ELSEIF}, {"then",    KW_THEN},   {"end",     KW_END},
    {"while",   KW_WHILE},  {"do",      KW_DO},     {"repeat",  KW_REPEAT},
    {"until",   KW_UNTIL},  {"for",     KW_FOR},    {"return",  KW_RETURN},
    {"nil",     KW_NIL},    {"true",    KW_TRUE},   {"false",   KW_FALSE},
    {"function",KW_FUNCTION},{"break",  KW_BREAK},  {"and",     KW_AND},
    {"or",      KW_OR},     {"not",     KW_NOT},
    {NULL, 0}
  };
  for (int i = 0; kws[i].name; i++) {
    int kl = (int)strlen(kws[i].name);
    if (kl == n && memcmp(kws[i].name, s, n) == 0) return kws[i].kw;
  }
  return -1;
}

static void np_lex_err(Lexer *L, const char *fmt, ...) {
  if (L->has_err) return;
  L->has_err = 1;
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(L->err, sizeof(L->err), fmt, ap);
  va_end(ap);
}

static void np_skip_ws(Lexer *L) {
  while (L->pos < L->len) {
    char c = L->src[L->pos];
    if (c == ' ' || c == '\t' || c == '\r') {
      L->pos++;
    } else if (c == '\n') {
      L->line++; L->pos++;
    } else if (c == '-' && L->pos + 1 < L->len && L->src[L->pos+1] == '-' &&
               L->pos + 2 < L->len && L->src[L->pos+2] == '[') {
      /* 块注释 --[[ ]] */
      L->pos += 3;
      while (L->pos + 1 < L->len &&
             !(L->src[L->pos] == ']' && L->src[L->pos+1] == ']')) {
        if (L->src[L->pos] == '\n') L->line++;
        L->pos++;
      }
      if (L->pos + 1 < L->len) L->pos += 2;
    } else if (c == '-' && L->pos + 1 < L->len && L->src[L->pos+1] == '-') {
      L->pos += 2;
      while (L->pos < L->len && L->src[L->pos] != '\n') L->pos++;
    } else {
      break;
    }
  }
}

static int np_next_tok(Lexer *L, Token *tk) {
  np_skip_ws(L);
  if (L->pos >= L->len || L->has_err) {
    tk->type = TK_EOF; tk->line = L->line; return 0;
  }
  tk->line = L->line;
  const char *p = L->src + L->pos;
  const char *end = L->src + L->len;
  char c = *p;

  if (c == '_' || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')) {
    const char *s = p;
    while (p < end && (*p == '_' || isalnum((unsigned char)*p))) p++;
    int n = (int)(p - s);
    int kw = np_keyword(s, n);
    if (kw >= 0) { tk->type = TK_KEYWORD; tk->kw = kw; }
    else          { tk->type = TK_IDENT; tk->s = s; tk->slen = n; }
    L->pos += n;
    return 1;
  }

  if (isdigit((unsigned char)c) ||
      (c == '.' && p + 1 < end && isdigit((unsigned char)p[1]))) {
    const char *s = p;
    int is_float = 0;
    if (*p == '0' && p + 1 < end && (p[1] == 'x' || p[1] == 'X')) {
      p += 2;
      while (p < end && (isxdigit((unsigned char)*p) || *p == '_')) p++;
    } else if (*p == '0' && p + 1 < end && (p[1] == 'b' || p[1] == 'B')) {
      p += 2;
      while (p < end && (*p == '0' || *p == '1' || *p == '_')) p++;
    } else {
      while (p < end && (isdigit((unsigned char)*p) || *p == '_')) p++;
      if (p < end && *p == '.') { is_float = 1; p++; while (p < end && (isdigit((unsigned char)*p) || *p == '_')) p++; }
      if (p < end && (*p == 'e' || *p == 'E')) {
        is_float = 1; p++;
        if (p < end && (*p == '+' || *p == '-')) p++;
        while (p < end && isdigit((unsigned char)*p)) p++;
      }
    }
    int n = (int)(p - s);
    char buf[64];
    if (n >= (int)sizeof(buf)) { np_lex_err(L, "line %d: numeric literal too long", tk->line); return 0; }
    memcpy(buf, s, n); buf[n] = '\0';
    if (is_float) {
      tk->type = TK_FLOAT; tk->fval = strtod(buf, NULL);
    } else {
      int base = 10;
      const char *num = buf;
      if (buf[0] == '0' && (buf[1] == 'x' || buf[1] == 'X')) { base = 16; num = buf + 2; }
      else if (buf[0] == '0' && (buf[1] == 'b' || buf[1] == 'B')) {
        /* strtoll 不识别 0b 前缀，手动解析二进制 */
        int64_t v = 0;
        for (const char *q = buf + 2; *q; q++) {
          if (*q == '_') continue;
          v = (v << 1) | (*q - '0');
        }
        tk->type = TK_INT; tk->ival = v;
        L->pos += n;
        return 1;
      }
      tk->type = TK_INT; tk->ival = strtoll(num, NULL, base);
    }
    L->pos += n;
    return 1;
  }

  if (c == '"' || c == '\'') {
    np_lex_err(L, "line %d: strings are not supported in NLang", tk->line);
    return 0;
  }

  if (p + 1 < end) {
    char c1 = p[0], c2 = p[1];
    if ((c1 == '=' && c2 == '=') || (c1 == '~' && c2 == '=') ||
        (c1 == '<' && c2 == '=') || (c1 == '>' && c2 == '=') ||
        (c1 == '<' && c2 == '<') || (c1 == '>' && c2 == '>')) {
      tk->type = TK_OP; tk->s = p; tk->slen = 2;
      L->pos += 2;
      return 1;
    }
  }
  if (c == '+' || c == '-' || c == '*' || c == '/' || c == '%' ||
      c == '&' || c == '|' || c == '^' || c == '<' || c == '>' ||
      c == '=' || c == '~' || c == '!') {
    tk->type = TK_OP; tk->s = p; tk->slen = 1;
    L->pos += 1;
    return 1;
  }
  switch (c) {
    case '(': tk->type = TK_LPAREN; L->pos++; return 1;
    case ')': tk->type = TK_RPAREN; L->pos++; return 1;
    case ',': tk->type = TK_COMMA;  L->pos++; return 1;
    case ';': tk->type = TK_SEMI;   L->pos++; return 1;
    case '.': tk->type = TK_DOT;    L->pos++; return 1;
    case ':': tk->type = TK_COLON;  L->pos++; return 1;
    case '{': tk->type = TK_LBRACE; L->pos++; return 1;
    case '}': tk->type = TK_RBRACE; L->pos++; return 1;
  }
  np_lex_err(L, "line %d: unexpected character '%c'", tk->line, c);
  return 0;
}


/* ============================================================
** AST 节点
** ============================================================ */

enum NodeKind {
  N_LOCAL, N_ASSIGN, N_IF, N_WHILE, N_REPEAT, N_FOR,
  N_RETURN, N_BLOCK, N_BREAK, N_IFELSE,
  N_INT, N_FLOAT, N_NIL, N_TRUE, N_FALSE, N_VAR, N_BINOP, N_UNOP
};

typedef struct Node Node;
typedef struct {
  Node **items;
  int n, cap;
} NodeList;

struct Node {
  int kind;
  int line;
  char *name;
  Node *expr;          /* N_LOCAL expr / N_ASSIGN expr / N_IF 条件 / N_WHILE 条件 / N_REPEAT until 条件 / N_FOR start / N_RETURN expr */
  Node *expr2;         /* N_FOR end */
  Node *expr3;         /* N_FOR step */
  Node *body;          /* N_WHILE/N_FOR body / N_IF then / N_IFELSE body */
  Node *body2;         /* N_REPEAT body / N_IFELSE next（未用，简化掉） */
  Node *else_body;     /* N_IF else */
  NodeList *stmts;     /* N_BLOCK */
  NodeList *elseifs;   /* N_IF elseif 链 */
  int op;              /* 二元/一元运算符编码 */
  Node *lhs;
  Node *rhs;
  int64_t ival;
  double  fval;
};

static Node *np_new_node(int kind) {
  Node *n = (Node *)calloc(1, sizeof(Node));
  n->kind = kind;
  return n;
}

static void np_free(Node *n) {
  if (!n) return;
  if (n->name) free(n->name);
  if (n->expr)  np_free(n->expr);
  if (n->expr2) np_free(n->expr2);
  if (n->expr3) np_free(n->expr3);
  if (n->body)  np_free(n->body);
  if (n->body2) np_free(n->body2);
  if (n->else_body) np_free(n->else_body);
  if (n->stmts) {
    for (int i = 0; i < n->stmts->n; i++) np_free(n->stmts->items[i]);
    free(n->stmts->items); free(n->stmts);
  }
  if (n->elseifs) {
    for (int i = 0; i < n->elseifs->n; i++) np_free(n->elseifs->items[i]);
    free(n->elseifs->items); free(n->elseifs);
  }
  free(n);
}

static NodeList *np_new_nodelist(void) {
  return (NodeList *)calloc(1, sizeof(NodeList));
}

static void np_nl_push(NodeList *l, Node *n) {
  if (l->n >= l->cap) {
    l->cap = l->cap ? l->cap * 2 : 8;
    l->items = (Node **)realloc(l->items, sizeof(Node *) * l->cap);
  }
  l->items[l->n++] = n;
}


/* ============================================================
** 解析器（Parser）
** ============================================================ */

typedef struct {
  Lexer lex;
  Token cur;
  Token peeked;
  int has_peek;
  NodeList *errors;
} Parser;

static void np_err(Parser *P, int line, const char *fmt, ...) {
  char buf[256];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);
  char full[300];
  snprintf(full, sizeof(full), "line %d: %s", line, buf);
  if (!P->errors) P->errors = np_new_nodelist();
  Node *e = np_new_node(-1);
  e->name = strdup(full);
  e->line = line;
  np_nl_push(P->errors, e);
}

static int np_advance(Parser *P) {
  if (P->has_peek) { P->cur = P->peeked; P->has_peek = 0; }
  else np_next_tok(&P->lex, &P->cur);
  return P->cur.type != TK_EOF;
}

static void np_peek(Parser *P) {
  if (!P->has_peek) { np_next_tok(&P->lex, &P->peeked); P->has_peek = 1; }
}

static int np_accept(Parser *P, int type) {
  if (P->cur.type == type) { np_advance(P); return 1; }
  return 0;
}

static int np_accept_kw(Parser *P, int kw) {
  if (P->cur.type == TK_KEYWORD && P->cur.kw == kw) { np_advance(P); return 1; }
  return 0;
}

static int np_accept_op(Parser *P, const char *op) {
  if (P->cur.type == TK_OP && (int)strlen(op) == P->cur.slen &&
      memcmp(P->cur.s, op, P->cur.slen) == 0) { np_advance(P); return 1; }
  return 0;
}

static int np_expect(Parser *P, int type, const char *what) {
  if (P->cur.type == type) { np_advance(P); return 1; }
  np_err(P, P->cur.line, "expected %s", what);
  return 0;
}

static int np_expect_kw(Parser *P, int kw, const char *what) {
  if (P->cur.type == TK_KEYWORD && P->cur.kw == kw) { np_advance(P); return 1; }
  np_err(P, P->cur.line, "expected '%s'", what);
  return 0;
}

static int np_expect_op(Parser *P, const char *op, const char *what) {
  if (P->cur.type == TK_OP && (int)strlen(op) == P->cur.slen &&
      memcmp(P->cur.s, op, P->cur.slen) == 0) { np_advance(P); return 1; }
  np_err(P, P->cur.line, "expected '%s'", what);
  return 0;
}

static Node *np_parse_expr(Parser *P);
static Node *np_parse_primary(Parser *P);

/* === 表达式解析（递归下降，标准优先级分层） === */

/* 优先级从低到高（10 最低）：
   10: or
   20: and
   30: ==  ~=  <  <=  >  >=
   40: |
   50: ^
   60: &
   70: <<  >>
   80: +  -
   90: *  /  %
*/

static int np_op_prec(const char *s, int n) {
  if (n == 2) {
    if (s[0] == '=' && s[1] == '=') return 30;
    if (s[0] == '~' && s[1] == '=') return 30;
    if (s[0] == '<' && s[1] == '=') return 30;
    if (s[0] == '>' && s[1] == '=') return 30;
    if (s[0] == '<' && s[1] == '<') return 70;
    if (s[0] == '>' && s[1] == '>') return 70;
    return 0;
  }
  switch (s[0]) {
    case '+': case '-': return 80;
    case '*': case '/': case '%': return 90;
    case '&': return 60; case '|': return 40; case '^': return 50;
    case '<': case '>': return 30;
    default: return 0;
  }
}

static int np_kw_prec(int kw) {
  if (kw == KW_OR) return 10;
  if (kw == KW_AND) return 20;
  return 0;
}

/** 把 token 编码为统一 op：单字符用 ASCII；双字符用 (c0<<8)|c1；关键字用 256+kw */
static int np_encode_op(Parser *P) {
  if (P->cur.type == TK_OP) {
    if (P->cur.slen == 2) return ((unsigned char)P->cur.s[0] << 8) | (unsigned char)P->cur.s[1];
    return (unsigned char)P->cur.s[0];
  }
  if (P->cur.type == TK_KEYWORD) return 256 + P->cur.kw;
  return -1;
}

/** 当前 token 的优先级；非运算符返回 -1 */
static int np_cur_prec(Parser *P) {
  if (P->cur.type == TK_OP) return np_op_prec(P->cur.s, P->cur.slen);
  if (P->cur.type == TK_KEYWORD) return np_kw_prec(P->cur.kw);
  return -1;
}

/* 标准的优先级爬升（左结合）：
   parse_expr_prec(min_prec):
     lhs = parse_primary()
     while (cur_prec >= min_prec):
       op = cur_op
       advance
       rhs = parse_expr_prec(op_prec + 1)  # 左结合
       lhs = BinOp(op, lhs, rhs)
     return lhs
*/
static Node *np_parse_expr_prec(Parser *P, int min_prec) {
  Node *lhs = np_parse_primary(P);
  while (1) {
    int prec = np_cur_prec(P);
    if (prec < min_prec) break;
    int op_enc = np_encode_op(P);
    np_advance(P);
    Node *rhs = np_parse_expr_prec(P, prec + 1);
    Node *cur = np_new_node(N_BINOP);
    cur->op = op_enc;
    cur->lhs = lhs;
    cur->rhs = rhs;
    lhs = cur;
  }
  return lhs;
}

static Node *np_parse_expr(Parser *P) {
  return np_parse_expr_prec(P, 1);
}

static Node *np_parse_primary(Parser *P) {
  Node *n = NULL;
  Token *t = &P->cur;
  if (t->type == TK_KEYWORD) {
    if (t->kw == KW_NIL)   { n = np_new_node(N_NIL);   np_advance(P); return n; }
    if (t->kw == KW_TRUE)  { n = np_new_node(N_TRUE);  np_advance(P); return n; }
    if (t->kw == KW_FALSE) { n = np_new_node(N_FALSE); np_advance(P); return n; }
    if (t->kw == KW_NOT) {
      np_advance(P);
      Node *op = np_parse_expr(P);
      n = np_new_node(N_UNOP); n->op = KW_NOT; n->lhs = op; return n;
    }
    if (t->kw == KW_FUNCTION) {
      np_err(P, t->line, "function definition is not supported in NLang");
      np_advance(P);
      return np_new_node(N_NIL);
    }
  }
  if (t->type == TK_INT)   { n = np_new_node(N_INT);   n->ival = t->ival; np_advance(P); return n; }
  if (t->type == TK_FLOAT) { n = np_new_node(N_FLOAT); n->fval = t->fval; np_advance(P); return n; }
  if (t->type == TK_IDENT) {
    n = np_new_node(N_VAR);
    n->name = (char *)malloc(t->slen + 1);
    memcpy(n->name, t->s, t->slen); n->name[t->slen] = '\0';
    np_advance(P);
    return n;
  }
  if (t->type == TK_OP && t->slen == 1 && (t->s[0] == '-' || t->s[0] == '+')) {
    char op = t->s[0];
    np_advance(P);
    Node *operand = np_parse_primary(P);
    if (op == '+') return operand;
    n = np_new_node(N_UNOP); n->op = '-'; n->lhs = operand; return n;
  }
  if (t->type == TK_LPAREN) {
    np_advance(P);
    n = np_parse_expr(P);
    np_expect(P, TK_RPAREN, "')'");
    return n;
  }
  np_err(P, t->line, "expected expression");
  np_advance(P);
  return np_new_node(N_NIL);
}

static Node *np_parse_block(Parser *P) {
  Node *blk = np_new_node(N_BLOCK);
  blk->stmts = np_new_nodelist();
  while (1) {
    if (P->cur.type == TK_EOF) break;
    if (P->cur.type == TK_KEYWORD) {
      int kw = P->cur.kw;
      if (kw == KW_END || kw == KW_ELSE || kw == KW_ELSEIF || kw == KW_UNTIL) break;
    }
    Node *s = NULL;
    /* 解析单条语句 */
    if (P->cur.type == TK_KEYWORD && P->cur.kw == KW_LOCAL) {
      np_advance(P);
      if (P->cur.type != TK_IDENT) { np_err(P, P->cur.line, "expected identifier after 'local'"); np_advance(P); continue; }
      s = np_new_node(N_LOCAL);
      s->line = P->cur.line;
      s->name = (char *)malloc(P->cur.slen + 1);
      memcpy(s->name, P->cur.s, P->cur.slen); s->name[P->cur.slen] = '\0';
      np_advance(P);
      if (np_accept_op(P, "=")) s->expr = np_parse_expr(P);
      else { s->expr = np_new_node(N_NIL); s->expr->line = s->line; }
    } else if (P->cur.type == TK_KEYWORD && P->cur.kw == KW_IF) {
      int line = P->cur.line;
      np_advance(P);
      s = np_new_node(N_IF); s->line = line;
      s->expr = np_parse_expr(P);
      np_expect_kw(P, KW_THEN, "'then'");
      s->body = np_parse_block(P);
      s->elseifs = np_new_nodelist();
      while (np_accept_kw(P, KW_ELSEIF)) {
        Node *ei = np_new_node(N_IFELSE);
        ei->line = P->cur.line;
        ei->expr = np_parse_expr(P);
        np_expect_kw(P, KW_THEN, "'then'");
        ei->body = np_parse_block(P);
        np_nl_push(s->elseifs, ei);
      }
      if (np_accept_kw(P, KW_ELSE)) s->else_body = np_parse_block(P);
      np_expect_kw(P, KW_END, "'end'");
    } else if (P->cur.type == TK_KEYWORD && P->cur.kw == KW_WHILE) {
      int line = P->cur.line;
      np_advance(P);
      s = np_new_node(N_WHILE); s->line = line;
      s->expr = np_parse_expr(P);
      np_expect_kw(P, KW_DO, "'do'");
      s->body = np_parse_block(P);
      np_expect_kw(P, KW_END, "'end'");
    } else if (P->cur.type == TK_KEYWORD && P->cur.kw == KW_REPEAT) {
      int line = P->cur.line;
      np_advance(P);
      s = np_new_node(N_REPEAT); s->line = line;
      s->body2 = np_parse_block(P);
      np_expect_kw(P, KW_UNTIL, "'until'");
      s->expr = np_parse_expr(P);
    } else if (P->cur.type == TK_KEYWORD && P->cur.kw == KW_FOR) {
      int line = P->cur.line;
      np_advance(P);
      if (P->cur.type != TK_IDENT) { np_err(P, P->cur.line, "expected identifier after 'for'"); np_advance(P); continue; }
      s = np_new_node(N_FOR); s->line = line;
      s->name = (char *)malloc(P->cur.slen + 1);
      memcpy(s->name, P->cur.s, P->cur.slen); s->name[P->cur.slen] = '\0';
      np_advance(P);
      np_expect_op(P, "=", "'='");
      s->expr = np_parse_expr(P);
      np_expect(P, TK_COMMA, "','");
      s->expr2 = np_parse_expr(P);
      if (np_accept(P, TK_COMMA)) s->expr3 = np_parse_expr(P);
      np_expect_kw(P, KW_DO, "'do'");
      s->body = np_parse_block(P);
      np_expect_kw(P, KW_END, "'end'");
    } else if (P->cur.type == TK_KEYWORD && P->cur.kw == KW_RETURN) {
      int line = P->cur.line;
      np_advance(P);
      s = np_new_node(N_RETURN); s->line = line;
      if (P->cur.type != TK_EOF &&
          !(P->cur.type == TK_KEYWORD && (P->cur.kw == KW_END || P->cur.kw == KW_ELSE ||
              P->cur.kw == KW_ELSEIF || P->cur.kw == KW_UNTIL))) {
        s->expr = np_parse_expr(P);
      }
    } else if (P->cur.type == TK_KEYWORD && P->cur.kw == KW_BREAK) {
      int line = P->cur.line;
      np_advance(P);
      s = np_new_node(N_BREAK); s->line = line;
    } else if (P->cur.type == TK_IDENT) {
      s = np_new_node(N_ASSIGN); s->line = P->cur.line;
      s->name = (char *)malloc(P->cur.slen + 1);
      memcpy(s->name, P->cur.s, P->cur.slen); s->name[P->cur.slen] = '\0';
      np_advance(P);
      np_expect_op(P, "=", "'='");
      s->expr = np_parse_expr(P);
    } else {
      np_err(P, P->cur.line, "expected statement");
      np_advance(P);
      continue;
    }
    if (s) np_nl_push(blk->stmts, s);
    while (np_accept(P, TK_SEMI)) {}
  }
  return blk;
}


/* ============================================================
** 代码生成器（CodeGen）
** ============================================================ */

#define NP_MAX_INSTS   8192
#define NP_MAX_LABELS  1024
#define NP_MAX_PATCHES 1024
#define NP_MAX_SYMS    256
#define NP_USER_REG_LO 0
#define NP_USER_REG_HI 223
#define NP_TMP_REG_LO  224
#define NP_TMP_REG_HI  255

typedef struct {
  char *name;
  int reg;
  int is_float;
} NpSym;

typedef struct {
  int pc;
  char lname[32];
} Patch;

typedef struct {
  lua_Integer *code;
  int ncode, cap;
  char labels[NP_MAX_LABELS][32];
  int  label_pc[NP_MAX_LABELS];
  int  n_labels;
  Patch patches[NP_MAX_PATCHES];
  int  n_patches;
  NpSym syms[NP_MAX_SYMS];
  int n_syms;
  int next_user_reg;
  int next_tmp_reg;
  int label_counter;
  int break_label_ids[64];
  int n_break_labels;
  char err[256];
  int has_err;
} CG;

static int cg_emit(CG *g, int op, int a, int b, int c, int32_t imm) {
  if (g->ncode >= g->cap) {
    g->cap = g->cap ? g->cap * 2 : 256;
    g->code = (lua_Integer *)realloc(g->code, sizeof(lua_Integer) * g->cap);
  }
  if (g->ncode >= NP_MAX_INSTS) {
    if (!g->has_err) { g->has_err = 1; snprintf(g->err, sizeof(g->err), "instruction limit exceeded"); }
    return -1;
  }
  g->code[g->ncode++] = np_make_ni(op, a, b, c, imm);
  return g->ncode - 1;
}

static int cg_new_label(CG *g) { return ++g->label_counter; }

static void cg_emit_label(CG *g, int label_id) {
  if (g->n_labels >= NP_MAX_LABELS) return;
  snprintf(g->labels[g->n_labels], 32, ".L%d", label_id);
  g->label_pc[g->n_labels] = g->ncode;
  g->n_labels++;
}

static void cg_emit_jump_to_label(CG *g, int op, int reg, int label_id) {
  if (g->n_patches >= NP_MAX_PATCHES) {
    if (!g->has_err) { g->has_err = 1; snprintf(g->err, sizeof(g->err), "patch limit exceeded"); }
    return;
  }
  int idx = cg_emit(g, op, reg, 0, 0, 0);
  if (idx < 0) return;
  Patch *p = &g->patches[g->n_patches++];
  p->pc = idx;
  snprintf(p->lname, 32, ".L%d", label_id);
}

static NpSym *cg_find_sym(CG *g, const char *name) {
  for (int i = 0; i < g->n_syms; i++)
    if (strcmp(g->syms[i].name, name) == 0) return &g->syms[i];
  return NULL;
}

static NpSym *cg_add_sym(CG *g, const char *name) {
  if (g->n_syms >= NP_MAX_SYMS) {
    if (!g->has_err) { g->has_err = 1; snprintf(g->err, sizeof(g->err), "symbol limit exceeded"); }
    return NULL;
  }
  NpSym *s = &g->syms[g->n_syms++];
  s->name = strdup(name);
  s->reg = g->next_user_reg++;
  s->is_float = 0;
  if (s->reg > NP_USER_REG_HI) {
    if (!g->has_err) { g->has_err = 1; snprintf(g->err, sizeof(g->err), "user register overflow"); }
    return NULL;
  }
  return s;
}

static int cg_alloc_tmp(CG *g) {
  int r = g->next_tmp_reg++;
  if (g->next_tmp_reg > NP_TMP_REG_HI) g->next_tmp_reg = NP_TMP_REG_LO;
  return r;
}

static void cg_resolve_patches(CG *g) {
  for (int i = 0; i < g->n_patches; i++) {
    Patch *p = &g->patches[i];
    int found = -1;
    for (int j = 0; j < g->n_labels; j++)
      if (strcmp(g->labels[j], p->lname) == 0) { found = j; break; }
    if (found < 0) {
      if (!g->has_err) { g->has_err = 1; snprintf(g->err, sizeof(g->err), "unresolved label %s", p->lname); }
      continue;
    }
    int target = g->label_pc[found];
    int32_t offset = (int32_t)(target - p->pc - 1);
    lua_Integer inst = g->code[p->pc];
    int op = NI_OP(inst), a = NI_A(inst), b = NI_B(inst), c = NI_C(inst);
    g->code[p->pc] = np_make_ni(op, a, b, c, offset);
  }
}

/* === 表达式代码生成 === */

static int cg_expr(CG *g, Node *e, int dst);

/* 二元运算：lreg/rreg 是源寄存器，lf/rf 是源类型，dst 是目标，输出类型到 *out_is_float */
static void cg_binop(CG *g, int op_enc, int lreg, int lf, int rreg, int rf, int dst, int *out_is_float) {
  int target_float = lf || rf;
  if (target_float) {
    int lt = cg_alloc_tmp(g), rt = cg_alloc_tmp(g);
    if (!lf) cg_emit(g, NI_I2F, lt, lreg, 0, 0); else cg_emit(g, NI_MOV, lt, lreg, 0, 0);
    if (!rf) cg_emit(g, NI_I2F, rt, rreg, 0, 0); else cg_emit(g, NI_MOV, rt, rreg, 0, 0);
    int op = -1, swap = 0;
    if      (op_enc == '+')                          op = NI_ADDF;
    else if (op_enc == '-')                          op = NI_SUBF;
    else if (op_enc == '*')                          op = NI_MULF;
    else if (op_enc == '/')                          op = NI_DIVF;
    else if (op_enc == ('<' << 8 | '='))             op = NI_LEF;
    else if (op_enc == ('>' << 8 | '='))           { op = NI_LEF; swap = 1; }
    else if (op_enc == '<')                          op = NI_LTF;
    else if (op_enc == '>')                        { op = NI_LTF; swap = 1; }
    else if (op_enc == ('=' << 8 | '='))             op = NI_EQ;
    else if (op_enc == ('~' << 8 | '='))             op = NI_NE;
    else if (op_enc == '&' || op_enc == '|' || op_enc == '^' ||
             op_enc == ('<' << 8 | '<') || op_enc == ('>' << 8 | '>')) {
      /* 位运算：强转 int */
      int li = cg_alloc_tmp(g), ri = cg_alloc_tmp(g);
      cg_emit(g, NI_F2I, li, lt, 0, 0);
      cg_emit(g, NI_F2I, ri, rt, 0, 0);
      int iop = -1;
      if      (op_enc == '&')                  iop = NI_AND;
      else if (op_enc == '|')                  iop = NI_OR;
      else if (op_enc == '^')                  iop = NI_XOR;
      else if (op_enc == ('<' << 8 | '<'))     iop = NI_SHL;
      else if (op_enc == ('>' << 8 | '>'))     iop = NI_SHR;
      cg_emit(g, iop, dst, li, ri, 0);
      *out_is_float = 0;
      return;
    } else if (op_enc == '%') {
      /* 浮点取模 VM 不支持 → 退化为整数取模（强转） */
      int li = cg_alloc_tmp(g), ri = cg_alloc_tmp(g);
      cg_emit(g, NI_F2I, li, lt, 0, 0);
      cg_emit(g, NI_F2I, ri, rt, 0, 0);
      cg_emit(g, NI_MOD, dst, li, ri, 0);
      *out_is_float = 0;
      return;
    }
    if (op < 0) { if (!g->has_err) { g->has_err = 1; snprintf(g->err, sizeof(g->err), "unsupported op on float"); } *out_is_float = 1; return; }
    if (swap) cg_emit(g, op, dst, rt, lt, 0);
    else      cg_emit(g, op, dst, lt, rt, 0);
    *out_is_float = (op == NI_ADDF || op == NI_SUBF || op == NI_MULF || op == NI_DIVF) ? 1 : 0;
    return;
  }
  /* 整数 */
  int op = -1, swap = 0;
  if      (op_enc == '+')                          op = NI_ADD;
  else if (op_enc == '-')                          op = NI_SUB;
  else if (op_enc == '*')                          op = NI_MUL;
  else if (op_enc == '/')                          op = NI_DIV;
  else if (op_enc == '%')                          op = NI_MOD;
  else if (op_enc == '&')                          op = NI_AND;
  else if (op_enc == '|')                          op = NI_OR;
  else if (op_enc == '^')                          op = NI_XOR;
  else if (op_enc == ('<' << 8 | '<'))             op = NI_SHL;
  else if (op_enc == ('>' << 8 | '>'))             op = NI_SHR;
  else if (op_enc == ('<' << 8 | '='))             op = NI_LE;
  else if (op_enc == ('>' << 8 | '='))           { op = NI_LE; swap = 1; }
  else if (op_enc == '<')                          op = NI_LT;
  else if (op_enc == '>')                        { op = NI_LT; swap = 1; }
  else if (op_enc == ('=' << 8 | '='))             op = NI_EQ;
  else if (op_enc == ('~' << 8 | '='))             op = NI_NE;
  else if (op_enc == 256 + KW_AND) {
    /* 简化：两边为 0/1 时用 MUL */
    int t = cg_alloc_tmp(g);
    cg_emit(g, NI_MOV, t, lreg, 0, 0);
    cg_emit(g, NI_MUL, dst, t, rreg, 0);
    *out_is_float = 0;
    return;
  }
  else if (op_enc == 256 + KW_OR) {
    int t = cg_alloc_tmp(g);
    cg_emit(g, NI_MOV, t, lreg, 0, 0);
    cg_emit(g, NI_OR, dst, t, rreg, 0);
    *out_is_float = 0;
    return;
  }
  if (op < 0) { if (!g->has_err) { g->has_err = 1; snprintf(g->err, sizeof(g->err), "unsupported operator"); } *out_is_float = 0; return; }
  if (swap) cg_emit(g, op, dst, rreg, lreg, 0);
  else      cg_emit(g, op, dst, lreg, rreg, 0);
  *out_is_float = 0;
}

static int cg_expr(CG *g, Node *e, int dst) {
  if (!e || g->has_err) return 0;
  switch (e->kind) {
    case N_INT: cg_emit(g, NI_LOADK, dst, 0, 0, (int32_t)e->ival); return 0;
    case N_FLOAT: {
      union { float f; int32_t i; } u; u.f = (float)e->fval;
      cg_emit(g, NI_LOADKF, dst, 0, 0, u.i);
      return 1;
    }
    case N_NIL:  cg_emit(g, NI_SETNIL, dst, 0, 0, 0); return 0;
    case N_TRUE: cg_emit(g, NI_LOADK, dst, 0, 0, 1);   return 0;
    case N_FALSE:cg_emit(g, NI_LOADK, dst, 0, 0, 0);   return 0;
    case N_VAR: {
      NpSym *s = cg_find_sym(g, e->name);
      if (!s) { if (!g->has_err) { g->has_err = 1; snprintf(g->err, sizeof(g->err), "undefined variable '%s'", e->name); } return 0; }
      if (s->reg != dst) cg_emit(g, NI_MOV, dst, s->reg, 0, 0);
      return s->is_float;
    }
    case N_UNOP: {
      if (e->op == '-') {
        int src_is_float = cg_expr(g, e->lhs, dst);
        if (src_is_float) cg_emit(g, NI_NEGF, dst, dst, 0, 0);
        else               cg_emit(g, NI_NEG,  dst, dst, 0, 0);
        return src_is_float;
      }
      if (e->op == KW_NOT) {
        int tmp = cg_alloc_tmp(g);
        cg_expr(g, e->lhs, tmp);
        cg_emit(g, NI_LOADK, dst, 0, 0, 0);
        cg_emit(g, NI_EQ, dst, tmp, dst, 0);
        return 0;
      }
      if (!g->has_err) { g->has_err = 1; snprintf(g->err, sizeof(g->err), "unsupported unary op"); }
      return 0;
    }
    case N_BINOP: {
      int ltmp = cg_alloc_tmp(g), rtmp = cg_alloc_tmp(g);
      int lf = cg_expr(g, e->lhs, ltmp);
      int rf = cg_expr(g, e->rhs, rtmp);
      int out_f = 0;
      cg_binop(g, e->op, ltmp, lf, rtmp, rf, dst, &out_f);
      return out_f;
    }
    default:
      if (!g->has_err) { g->has_err = 1; snprintf(g->err, sizeof(g->err), "unsupported expr node %d", e->kind); }
      return 0;
  }
}

/* === 语句代码生成 === */
static void cg_stmt(CG *g, Node *s);

static void cg_block_stmts(CG *g, NodeList *l) {
  for (int i = 0; i < l->n; i++) cg_stmt(g, l->items[i]);
}

static void cg_stmt(CG *g, Node *s) {
  if (!s || g->has_err) return;
  switch (s->kind) {
    case N_BLOCK:
      cg_block_stmts(g, s->stmts);
      break;
    case N_LOCAL: {
      NpSym *sym = cg_add_sym(g, s->name);
      if (!sym) break;
      int is_float = cg_expr(g, s->expr, sym->reg);
      sym->is_float = is_float;
      break;
    }
    case N_ASSIGN: {
      NpSym *sym = cg_find_sym(g, s->name);
      if (!sym) { if (!g->has_err) { g->has_err = 1; snprintf(g->err, sizeof(g->err), "undefined variable '%s'", s->name); } break; }
      int is_float = cg_expr(g, s->expr, sym->reg);
      sym->is_float = is_float;
      break;
    }
    case N_IF: {
      int L_after = cg_new_label(g);
      int L_next  = cg_new_label(g);
      int c = cg_alloc_tmp(g);
      cg_expr(g, s->expr, c);
      cg_emit_jump_to_label(g, NI_JF, c, L_next);
      cg_stmt(g, s->body);
      cg_emit_jump_to_label(g, NI_JMP, 0, L_after);
      cg_emit_label(g, L_next);
      for (int i = 0; i < s->elseifs->n; i++) {
        Node *ei = s->elseifs->items[i];
        int L_next2 = cg_new_label(g);
        int cc = cg_alloc_tmp(g);
        cg_expr(g, ei->expr, cc);
        cg_emit_jump_to_label(g, NI_JF, cc, L_next2);
        cg_stmt(g, ei->body);
        cg_emit_jump_to_label(g, NI_JMP, 0, L_after);
        cg_emit_label(g, L_next2);
      }
      if (s->else_body) cg_stmt(g, s->else_body);
      cg_emit_label(g, L_after);
      break;
    }
    case N_WHILE: {
      int L_start = cg_new_label(g);
      int L_end   = cg_new_label(g);
      cg_emit_label(g, L_start);
      int c = cg_alloc_tmp(g);
      cg_expr(g, s->expr, c);
      cg_emit_jump_to_label(g, NI_JF, c, L_end);
      if (g->n_break_labels < 64) g->break_label_ids[g->n_break_labels++] = L_end;
      cg_stmt(g, s->body);
      g->n_break_labels--;
      cg_emit_jump_to_label(g, NI_JMP, 0, L_start);
      cg_emit_label(g, L_end);
      break;
    }
    case N_REPEAT: {
      int L_start = cg_new_label(g);
      int L_end   = cg_new_label(g);
      cg_emit_label(g, L_start);
      if (g->n_break_labels < 64) g->break_label_ids[g->n_break_labels++] = L_end;
      cg_stmt(g, s->body2);
      g->n_break_labels--;
      int c = cg_alloc_tmp(g);
      cg_expr(g, s->expr, c);
      cg_emit_jump_to_label(g, NI_JF, c, L_start);
      cg_emit_label(g, L_end);
      break;
    }
    case N_FOR: {
      NpSym *xsym = cg_add_sym(g, s->name);
      if (!xsym) break;
      int rx = xsym->reg;
      int rend = cg_alloc_tmp(g);
      int rstep = cg_alloc_tmp(g);
      int rcond = cg_alloc_tmp(g);
      int rtmp  = cg_alloc_tmp(g);
      int a_is_float = cg_expr(g, s->expr, rx);
      xsym->is_float = a_is_float;
      int b_is_float = cg_expr(g, s->expr2, rend);
      int s_is_float;
      if (s->expr3) s_is_float = cg_expr(g, s->expr3, rstep);
      else { cg_emit(g, NI_LOADK, rstep, 0, 0, 1); s_is_float = 0; }
      int all_float = a_is_float || b_is_float || s_is_float;
      if (all_float) {
        if (!a_is_float) cg_emit(g, NI_I2F, rx, rx, 0, 0);
        if (!b_is_float) cg_emit(g, NI_I2F, rend, rend, 0, 0);
        if (!s_is_float) cg_emit(g, NI_I2F, rstep, rstep, 0, 0);
      }
      int L_start = cg_new_label(g);
      int L_end   = cg_new_label(g);
      int L_pos   = cg_new_label(g);
      int L_neg   = cg_new_label(g);
      int L_done  = cg_new_label(g);
      cg_emit_label(g, L_start);
      cg_emit(g, NI_LOADK, rtmp, 0, 0, 0);
      if (all_float) cg_emit(g, NI_LTF, rtmp, rtmp, rstep, 0);
      else           cg_emit(g, NI_LT,  rtmp, rtmp, rstep, 0);
      cg_emit_jump_to_label(g, NI_JT, rtmp, L_pos);
      cg_emit_jump_to_label(g, NI_JMP, 0, L_neg);
      cg_emit_label(g, L_pos);
      if (all_float) cg_emit(g, NI_LEF, rcond, rx, rend, 0);
      else           cg_emit(g, NI_LE,  rcond, rx, rend, 0);
      cg_emit_jump_to_label(g, NI_JMP, 0, L_done);
      cg_emit_label(g, L_neg);
      /* x >= end 等价 end <= x */
      if (all_float) cg_emit(g, NI_LEF, rcond, rend, rx, 0);
      else           cg_emit(g, NI_LE,  rcond, rend, rx, 0);
      cg_emit_label(g, L_done);
      cg_emit_jump_to_label(g, NI_JF, rcond, L_end);
      if (g->n_break_labels < 64) g->break_label_ids[g->n_break_labels++] = L_end;
      cg_stmt(g, s->body);
      g->n_break_labels--;
      if (all_float) cg_emit(g, NI_ADDF, rx, rx, rstep, 0);
      else           cg_emit(g, NI_ADD,  rx, rx, rstep, 0);
      cg_emit_jump_to_label(g, NI_JMP, 0, L_start);
      cg_emit_label(g, L_end);
      break;
    }
    case N_RETURN:
      if (s->expr) {
        int tmp = cg_alloc_tmp(g);
        cg_expr(g, s->expr, tmp);
        cg_emit(g, NI_RET, tmp, 1, 0, 0);
      } else {
        cg_emit(g, NI_RET, 0, 0, 0, 0);
      }
      break;
    case N_BREAK:
      if (g->n_break_labels > 0) {
        int L = g->break_label_ids[g->n_break_labels - 1];
        cg_emit_jump_to_label(g, NI_JMP, 0, L);
      }
      break;
    default:
      if (!g->has_err) { g->has_err = 1; snprintf(g->err, sizeof(g->err), "unsupported stmt kind %d", s->kind); }
      break;
  }
}


/* ============================================================
** 编译入口
** ============================================================ */

static int np_compile(lua_State *L) {
  size_t slen;
  const char *src = luaL_checklstring(L, 1, &slen);
  Lexer lex;
  memset(&lex, 0, sizeof(lex));
  lex.src = src; lex.pos = 0; lex.len = (int)slen; lex.line = 1;
  Token tk;
  np_next_tok(&lex, &tk);
  if (lex.has_err) return luaL_error(L, "%s", lex.err);
  if (tk.type == TK_EOF) return luaL_error(L, "empty source");
  Parser P;
  memset(&P, 0, sizeof(P));
  P.lex = lex;
  P.cur = tk;
  Node *program = np_parse_block(&P);
  if (P.errors && P.errors->n > 0) {
    luaL_Buffer b;
    luaL_buffinit(L, &b);
    for (int i = 0; i < P.errors->n; i++) {
      luaL_addstring(&b, P.errors->items[i]->name);
      luaL_addstring(&b, "\n");
    }
    luaL_pushresult(&b);
    const char *msg = lua_tostring(L, -1);
    size_t mlen = strlen(msg);
    char *copy = (char *)malloc(mlen + 1);
    memcpy(copy, msg, mlen + 1);
    lua_pop(L, 1);
    np_free(program);
    return luaL_error(L, "%s", copy);
  }
  if (P.cur.type != TK_EOF) {
    int line = P.cur.line;
    np_free(program);
    return luaL_error(L, "line %d: unexpected token after program", line);
  }
  CG g;
  memset(&g, 0, sizeof(g));
  g.next_user_reg = 0;
  g.next_tmp_reg = NP_TMP_REG_LO;
  cg_stmt(&g, program);
  if (g.has_err) {
    char err[300];
    snprintf(err, sizeof(err), "%s", g.err);
    np_free(program);
    return luaL_error(L, "%s", err);
  }
  cg_emit(&g, NI_HALT, 0, 0, 0, 0);
  cg_resolve_patches(&g);
  if (g.has_err) {
    char err[300];
    snprintf(err, sizeof(err), "%s", g.err);
    np_free(program);
    return luaL_error(L, "%s", err);
  }
  np_free(program);
  lua_createtable(L, g.ncode, 0);
  for (int i = 0; i < g.ncode; i++) {
    lua_pushinteger(L, g.code[i]);
    lua_rawseti(L, -2, i + 1);
  }
  free(g.code);
  return 1;
}


/* ============================================================
** 模块注册
** ============================================================ */

static const luaL_Reg lnativeparser_funcs[] = {
  {"compile",  np_compile},
  {NULL, NULL}
};

LUAMOD_API int luaopen_nativeparser(lua_State *L) {
  luaL_newlib(L, lnativeparser_funcs);
  return 1;
}
