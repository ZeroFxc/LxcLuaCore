/*
** $Id: lnativeparser.c $
** NLang 编译器 — 把 Lua-like 高级语言翻译为 NativeVM 字节码
**
** === NLang 2.0 语言规范 ===
**
** 注释：    -- 行尾 或 --[[ ... ]] 或 --[=[ ... ]=]
** 标识符：  [a-zA-Z_][a-zA-Z0-9_]*
** 整数：    123, 0xFF, 0b101
** 浮点：    3.14, 1.5e-3, .5
** 字符串：  "hello"  或  'hello' （支持转义：\\, \", \', \n, \t, \r, \0）
**           或 [[...]] 或 [=[...]=] （长字符串，不处理转义）
** 表：      {key=val, ...} 或 {val1, val2, ...}（数组式）
**
** 关键字：  local if else elseif then end while do repeat until for in return
**          nil true false function break continue goto and or not
**
** 运算符：  +  -  *  /  //  %  &  |  ^  ~  <<  >>
**          <  <= >  >= ==  ~=
**          ..  (字符串拼接)  #  (长度)
**          and  or  not
**          =  (赋值)
**
** 分隔符：  ( ) { } [ ] , ; . : :: ...
**
** 语句：
**   local x = expr                   局部变量声明
**   local a, b, c = expr1, expr2, expr3  多重局部变量声明
**   x = expr                         赋值
**   a, b, c = expr1, expr2, expr3    多重赋值
**   t.key = expr                     成员赋值
**   t[expr] = expr                   索引赋值
**   if expr then block [elseif expr then block]* [else block] end
**   while expr do block end
**   repeat block until expr
**   for var = a, b [, c] do block end  数值 for
**   for var1, var2 in expr1, expr2, expr3 do block end  泛型 for
**   return [expr]
**   break
**   continue
**   goto label
**   ::label::
**   do block end
**   function name(arg1, arg2, ...) body end  函数定义
**   function obj:method(args) body end   方法定义
**
** 表达式（按优先级从低到高）：
**   or
**   and
**   == ~= < <= > >=
**   |            （按位或）
**   ~            （按位异或 XOR）
**   &            （按位与）
**   << >>
**   ..           （字符串拼接）
**   + -
**   * / // %
**   ^            （幂运算，右结合）
**   not - # ~    (一元：逻辑非、负号、长度、按位取反)
**   name(args)            函数调用
**   expr.key              成员访问
**   expr[expr]            索引访问
**   function(args) body end  闭包（匿名函数）
**   {key=val, ...} / {val, ...}  表字面量
**   ...                   可变参数
**   字面量 / 变量 / (expr)
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
** 与 lnativevm.c 中的枚举值必须保持一致
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

/* 操作码枚举 — 必须与 lnativevm.c 保持一致 */
enum {
  NI_NOP = 0,  NI_LOADK,    NI_LOADKF,   NI_LOADK64,  NI_LOADKHI,
  NI_MOV,      NI_ADD,      NI_SUB,      NI_MUL,      NI_DIV,
  NI_MOD,      NI_ADDF,     NI_SUBF,     NI_MULF,     NI_DIVF,
  NI_AND,      NI_OR,       NI_XOR,      NI_SHL,      NI_SHR,
  NI_EQ,       NI_NE,       NI_LT,       NI_LE,       NI_LTF,
  NI_LEF,      NI_JMP,      NI_JT,       NI_JF,       NI_RET,
  NI_I2F,      NI_F2I,      NI_NEG,      NI_NEGF,     NI_MOVF,
  NI_MOVI,     NI_SETNIL,   NI_ISNIL,    NI_SQRT,     NI_HALT,
  NI_CALL,     /* R[a] = call(R[b], R[c..c+imm-1], imm) — 统一调用 Lua/NLang 函数 */
  /* === 新增 Lua 互操作码 === */
  NI_GETFIELD,  /* R[a] = getfield(R[b], key_str_ref) — 成员访问 t.key，key_str_ref 在 imm 中 */
  NI_GETTABLE,  /* R[a] = gettable(R[b], R[c]) — 索引访问 t[expr] */
  NI_SETFIELD,  /* setfield(R[a], key_str_ref, R[b]) — 成员赋值 t.key = val */
  NI_SETTABLE,  /* settable(R[a], R[b], R[c]) — 索引赋值 t[expr] = val */
  NI_LEN,       /* R[a] = #R[b] — 长度运算符 */
  NI_CONCAT,    /* R[a] = R[b] .. R[c] — 字符串拼接 */
  NI_POW,       /* R[a] = R[b] ^ R[c] — 幂运算 */
  NI_IDIV,      /* R[a] = R[b] // R[c] — 整除 */
  NI_BNOT,      /* R[a] = ~R[b] — 按位取反 */
  NI_FOR_IN_INIT, /* for_in_init(R[a..a+nvars-1], R[b..b+nexprs-1]) — 泛型 for 初始化 */
  NI_FOR_IN_NEXT, /* for_in_next(R[a..a+nvars-1]) — 泛型 for 迭代，结果在 R[a..a+nvars-1] 中 */
  NI_LOADKPTR,   /* R[a] = imm32 as NTYPE_PTR（Lua registry 引用） */
  NI_MAX
};

/* 寄存器类型标签 — 必须与 lnativevm.c 保持一致 */
#define NTYPE_NIL   0
#define NTYPE_INT   1
#define NTYPE_FLOAT 2
#define NTYPE_PTR   3   /* 指针引用：用于字符串、表、Lua 函数 */
#define NTYPE_FUNC  4   /* 函数引用：v.i 存储 func_id */


/* ============================================================
** 词法分析器（Lexer）
** ============================================================ */

enum TokenType {
  TK_EOF, TK_IDENT, TK_INT, TK_FLOAT, TK_KEYWORD, TK_STRING,
  TK_OP, TK_LPAREN, TK_RPAREN, TK_COMMA, TK_SEMI, TK_DOT, TK_COLON,
  TK_LBRACE, TK_RBRACE,
  TK_DOTS,      /* 可变参数 ... */
  TK_CONCAT,    /* 字符串拼接 .. */
  TK_LABEL      /* 标签 ::label:: */
};

enum Keyword {
  KW_LOCAL, KW_IF, KW_ELSE, KW_ELSEIF, KW_THEN, KW_END,
  KW_WHILE, KW_DO, KW_REPEAT, KW_UNTIL,
  KW_FOR, KW_IN,
  KW_RETURN,
  KW_NIL, KW_TRUE, KW_FALSE,
  KW_FUNCTION,
  KW_BREAK, KW_CONTINUE, KW_GOTO,
  KW_AND, KW_OR, KW_NOT,
  KW___COUNT__
};

/**
 * @brief Token 结构体
 * str_buf: 处理后的字符串内容缓冲区（用于 TK_STRING）
 */
typedef struct {
  int type;
  int kw;
  int64_t ival;
  double  fval;
  const char *s;
  int slen;
  int line;
  char str_buf[512];  /* 处理后的字符串字面量内容 */
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
    {"until",   KW_UNTIL},  {"for",     KW_FOR},    {"in",      KW_IN},
    {"return",  KW_RETURN},
    {"nil",     KW_NIL},    {"true",    KW_TRUE},   {"false",   KW_FALSE},
    {"function",KW_FUNCTION},{"break",  KW_BREAK},  {"continue",KW_CONTINUE},
    {"goto",    KW_GOTO},
    {"and",     KW_AND},    {"or",      KW_OR},     {"not",     KW_NOT},
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
    } else if (c == '-' && L->pos + 1 < L->len && L->src[L->pos+1] == '-') {
      /* 注释：-- 行尾 或 --[[ ]] 块注释（支持嵌套层级 [=[ ]=]） */
      L->pos += 2;
      /* 检查是否为块注释：--[[ 或 --[=[ */
      if (L->pos + 1 < L->len && L->src[L->pos] == '[' &&
          (L->src[L->pos+1] == '[' || L->src[L->pos+1] == '=')) {
        /* 块注释 --[[ ... ]] 或 --[=[ ... ]=] */
        L->pos++;  /* 跳过开 '[' */
        /* 计算开定界符中的 '=' 数量 */
        int eq_count = 0;
        while (L->pos < L->len && L->src[L->pos] == '=') { eq_count++; L->pos++; }
        if (L->pos < L->len && L->src[L->pos] == '[') {
          L->pos++;  /* 跳过第二个 '[' */
          /* 查找匹配的闭定界符 ]=*] */
          while (L->pos < L->len) {
            if (L->src[L->pos] == ']') {
              int close_eq = 0;
              int look = L->pos + 1;
              while (look < L->len && L->src[look] == '=') { close_eq++; look++; }
              if (look < L->len && L->src[look] == ']' && close_eq == eq_count) {
                L->pos = look + 1;
                break;
              }
            }
            if (L->src[L->pos] == '\n') L->line++;
            L->pos++;
          }
        }
        /* 如果开定界符不匹配，回退为行尾注释 */
      } else {
        /* 行尾注释 -- ... */
        while (L->pos < L->len && L->src[L->pos] != '\n') L->pos++;
      }
    } else {
      break;
    }
  }
}

/**
 * @brief 处理字符串转义序列
 * 将源字符串中的转义序列转换为实际字符，写入 tk->str_buf
 * @param src  源字符串指针（在引号之间）
 * @param len  源字符串长度
 * @param tk   输出 Token
 */
static void np_process_string(const char *src, int len, Token *tk) {
  char *out = tk->str_buf;
  int out_len = 0;
  int max = (int)sizeof(tk->str_buf) - 1;
  for (int i = 0; i < len && out_len < max; i++) {
    char c = src[i];
    if (c == '\\' && i + 1 < len) {
      i++;
      switch (src[i]) {
        case 'n':  out[out_len++] = '\n'; break;
        case 't':  out[out_len++] = '\t'; break;
        case 'r':  out[out_len++] = '\r'; break;
        case '0':  out[out_len++] = '\0'; break;
        case '\\': out[out_len++] = '\\'; break;
        case '"':  out[out_len++] = '"';  break;
        case '\'': out[out_len++] = '\''; break;
        default:
          /* 不认识的转义，保留原样 */
          out[out_len++] = '\\';
          if (out_len < max) out[out_len++] = src[i];
          break;
      }
    } else {
      out[out_len++] = c;
    }
  }
  out[out_len] = '\0';
  tk->s = tk->str_buf;
  tk->slen = out_len;
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

  /* 标识符和关键字 */
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

  /* 数字字面量 */
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

  /* 字符串字面量：支持 "..." 和 '...' */
  if (c == '"' || c == '\'') {
    char quote = c;
    L->pos++;  /* 跳过开引号 */
    const char *start = L->src + L->pos;
    int raw_len = 0;
    while (L->pos < L->len && L->src[L->pos] != quote) {
      if (L->src[L->pos] == '\\' && L->pos + 1 < L->len) {
        L->pos++;  /* 跳过转义符 */
        raw_len++;
      }
      if (L->pos < L->len) {
        L->pos++;
        raw_len++;
      }
    }
    if (L->pos >= L->len) {
      np_lex_err(L, "line %d: unfinished string", tk->line);
      return 0;
    }
    /* 处理转义序列，写入 tk->str_buf */
    np_process_string(start, raw_len, tk);
    tk->type = TK_STRING;
    L->pos++;  /* 跳过闭引号 */
    return 1;
  }

  /* 长字符串字面量：[[ ... ]] 或 [=[ ... ]=] */
  if (c == '[' && L->pos + 1 < L->len &&
      (L->src[L->pos+1] == '[' || L->src[L->pos+1] == '=')) {
    int eq_count = 0;
    L->pos++;  /* 跳过第一个 '[' */
    while (L->pos < L->len && L->src[L->pos] == '=') { eq_count++; L->pos++; }
    if (L->pos < L->len && L->src[L->pos] == '[') {
      L->pos++;  /* 跳过第二个 '[' */
      const char *str_start = L->src + L->pos;
      int str_len = 0;
      /* 查找匹配的闭定界符 ]=*] */
      while (L->pos < L->len) {
        if (L->src[L->pos] == ']') {
          int close_eq = 0;
          int look = L->pos + 1;
          while (look < L->len && L->src[look] == '=') { close_eq++; look++; }
          if (look < L->len && L->src[look] == ']' && close_eq == eq_count) {
            str_len = (int)(L->src + L->pos - str_start);
            L->pos = look + 1;
            break;
          }
        }
        if (L->src[L->pos] == '\n') L->line++;
        L->pos++;
      }
      /* 长字符串不处理转义，直接存储原始内容 */
      if (str_len < (int)sizeof(tk->str_buf)) {
        memcpy(tk->str_buf, str_start, str_len);
        tk->str_buf[str_len] = '\0';
      } else {
        int copy_len = (int)sizeof(tk->str_buf) - 1;
        memcpy(tk->str_buf, str_start, copy_len);
        tk->str_buf[copy_len] = '\0';
        str_len = copy_len;
      }
      tk->type = TK_STRING;
      tk->s = tk->str_buf;
      tk->slen = str_len;
      return 1;
    }
    /* 不是合法的长字符串，回退 */
    L->pos = (int)(p - L->src);
  }

  /* 双字符运算符 */
  if (p + 1 < end) {
    char c1 = p[0], c2 = p[1];
    if ((c1 == '=' && c2 == '=') || (c1 == '~' && c2 == '=') ||
        (c1 == '<' && c2 == '=') || (c1 == '>' && c2 == '=') ||
        (c1 == '<' && c2 == '<') || (c1 == '>' && c2 == '>') ||
        (c1 == '/' && c2 == '/') || (c1 == '.' && c2 == '.')) {
      if (c1 == '.' && c2 == '.') {
        /* .. 字符串拼接 — 特殊处理为 TK_CONCAT */
        if (p + 2 < end && p[2] == '.') {
          /* ... 可变参数 */
          tk->type = TK_DOTS; tk->s = p; tk->slen = 3;
          L->pos += 3;
          return 1;
        }
        tk->type = TK_CONCAT; tk->s = p; tk->slen = 2;
        L->pos += 2;
        return 1;
      }
      tk->type = TK_OP; tk->s = p; tk->slen = 2;
      L->pos += 2;
      return 1;
    }
  }
  /* 单字符运算符 */
  if (c == '+' || c == '-' || c == '*' || c == '/' || c == '%' ||
      c == '&' || c == '|' || c == '^' || c == '<' || c == '>' ||
      c == '=' || c == '~' || c == '!' || c == '#' || c == '[' || c == ']') {
    tk->type = TK_OP; tk->s = p; tk->slen = 1;
    L->pos += 1;
    return 1;
  }
  /* 分隔符 */
  switch (c) {
    case '(': tk->type = TK_LPAREN; L->pos++; return 1;
    case ')': tk->type = TK_RPAREN; L->pos++; return 1;
    case ',': tk->type = TK_COMMA;  L->pos++; return 1;
    case ';': tk->type = TK_SEMI;   L->pos++; return 1;
    case '.': tk->type = TK_DOT;    L->pos++; return 1;
    case ':':
      /* 检查 :: 标签语法 */
      if (p + 1 < end && p[1] == ':') {
        tk->type = TK_OP; tk->s = p; tk->slen = 2;
        L->pos += 2;
        return 1;
      }
      tk->type = TK_COLON;  L->pos++; return 1;
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
  N_INT, N_FLOAT, N_NIL, N_TRUE, N_FALSE, N_VAR, N_BINOP, N_UNOP,
  /* 新增节点类型 */
  N_STRING,       /* 字符串字面量 */
  N_FUNC_DEF,     /* 函数定义: function name(params) body end */
  N_FUNC_CALL,    /* 函数调用: name(args) */
  N_TABLE,        /* 表字面量: {key=val, ...} 或 {val, ...} */
  N_MULTI_ASSIGN, /* 多重赋值: a, b, c = expr1, expr2, expr3 */
  N_CLOSURE,      /* 闭包: function(params) body end */
  /* === 新增语法特性节点 === */
  N_MEMBER,       /* 成员访问: t.key，语法糖等价于 t["key"] */
  N_INDEX,        /* 索引访问: t[expr] */
  N_GOTO,         /* goto 语句 */
  N_LABEL,        /* 标签 ::label:: */
  N_DO,           /* do ... end 独立块 */
  N_FOR_IN,       /* 泛型 for 循环: for var1, var2 in expr1, expr2, expr3 do body end */
  N_DOTS,         /* 可变参数 ... */
  N_METHOD_DEF,   /* 方法定义: function obj:method(args) body end */
  N_CONTINUE      /* continue 语句 */
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
  Node *expr;          /* N_LOCAL expr / N_ASSIGN expr / N_IF 条件 / N_WHILE 条件 / N_REPEAT until 条件 / N_FOR start / N_RETURN expr / N_FUNC_CALL 函数表达式 / N_MEMBER 表表达式 / N_INDEX 表表达式 */
  Node *expr2;         /* N_FOR end / N_FOR_IN 迭代器函数 */
  Node *expr3;         /* N_FOR step / N_FOR_IN 不变状态 */
  Node *body;          /* N_WHILE/N_FOR body / N_IF then / N_IFELSE body / N_FUNC_DEF body / N_CLOSURE body / N_FOR_IN body / N_DO body */
  Node *body2;         /* N_REPEAT body / N_IFELSE next */
  Node *else_body;     /* N_IF else */
  NodeList *stmts;     /* N_BLOCK */
  NodeList *elseifs;   /* N_IF elseif 链 */
  int op;              /* 二元/一元运算符编码 */
  Node *lhs;
  Node *rhs;
  int64_t ival;
  double  fval;
  /* 新增字段 */
  NodeList *params;    /* N_FUNC_DEF, N_CLOSURE, N_METHOD_DEF: 参数名列表 */
  NodeList *args;      /* N_FUNC_CALL: 实参表达式列表 */
  NodeList *vars;      /* N_MULTI_ASSIGN: 等号左侧变量名列表 / N_FOR_IN: 循环变量名列表 */
  NodeList *vals;      /* N_MULTI_ASSIGN: 等号右侧表达式列表 */
  NodeList *entries;   /* N_TABLE: 表条目列表 */
  NodeList *upvalues;  /* N_CLOSURE: 捕获的上值变量名列表 */
  int nregs;           /* N_FUNC_DEF, N_CLOSURE: 函数需要的寄存器数 */
  /* === 新增语法特性字段 === */
  char *member_name;   /* N_MEMBER: 成员名（字符串） */
  Node *index_expr;    /* N_INDEX: 索引表达式 */
  char *label_name;    /* N_GOTO, N_LABEL: 标签名 */
  int is_continue;     /* N_CONTINUE: 标记（N_BREAK 复用） */
  int has_dots;        /* N_FUNC_DEF, N_METHOD_DEF: 是否接受可变参数 */
  NodeList *for_in_exprs; /* N_FOR_IN: 迭代器表达式列表 */
  Node *self_name;     /* N_METHOD_DEF: self 参数名 */
};

static Node *np_new_node(int kind) {
  Node *n = (Node *)calloc(1, sizeof(Node));
  n->kind = kind;
  return n;
}

/* 前向声明 */
static void np_free(Node *n);

static void np_free_nodelist(NodeList *l) {
  if (!l) return;
  for (int i = 0; i < l->n; i++) np_free(l->items[i]);
  free(l->items);
  free(l);
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
  if (n->stmts) np_free_nodelist(n->stmts);
  if (n->elseifs) np_free_nodelist(n->elseifs);
  if (n->params) np_free_nodelist(n->params);
  if (n->args) np_free_nodelist(n->args);
  if (n->vars) np_free_nodelist(n->vars);
  if (n->vals) np_free_nodelist(n->vals);
  if (n->entries) np_free_nodelist(n->entries);
  if (n->upvalues) np_free_nodelist(n->upvalues);
  if (n->member_name) free(n->member_name);
  if (n->index_expr) np_free(n->index_expr);
  if (n->label_name) free(n->label_name);
  if (n->for_in_exprs) np_free_nodelist(n->for_in_exprs);
  if (n->self_name) np_free(n->self_name);
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
static Node *np_parse_block(Parser *P);

/* === 表达式解析（递归下降，标准优先级分层） === */

/* 优先级从低到高（10 最低）：
   10: or
   20: and
   30: ==  ~=  <  <=  >  >=
   40: |
   50: ^  (按位异或)
   55: ~  (按位与，优先级高于 |)
   60: &
   65: <<  >>
   70: ..  (字符串拼接)
   80: +  -
   90: *  /  //  %
   95: ^  (幂运算，右结合)
*/

static int np_op_prec(const char *s, int n) {
  if (n == 2) {
    if (s[0] == '=' && s[1] == '=') return 30;
    if (s[0] == '~' && s[1] == '=') return 30;
    if (s[0] == '<' && s[1] == '=') return 30;
    if (s[0] == '>' && s[1] == '=') return 30;
    if (s[0] == '<' && s[1] == '<') return 65;
    if (s[0] == '>' && s[1] == '>') return 65;
    if (s[0] == '/' && s[1] == '/') return 90;
    return 0;
  }
  switch (s[0]) {
    case '+': case '-': return 80;
    case '*': case '/': case '%': return 90;
    case '&': return 60; case '|': return 40; case '^': return 50;
    case '~': return 55;  /* 按位异或（在 NLang 中 ^ 是 XOR，~ 也是 XOR） */
    case '<': case '>': return 30;
    default: return 0;
  }
}

/* 字符串拼接 .. 的优先级 */
static int np_concat_prec(void) { return 70; }

/* 幂运算 ^ 的优先级（右结合，所以需要特殊处理） */

static int np_kw_prec(int kw) {
  if (kw == KW_OR) return 10;
  if (kw == KW_AND) return 20;
  return 0;
}

/** 把 token 编码为统一 op：单字符用 ASCII；双字符用 (c0<<8)|c1；关键字用 256+kw；.. 拼接用特殊值 */
static int np_encode_op(Parser *P) {
  if (P->cur.type == TK_CONCAT) return 512;  /* 特殊编码用于 .. */
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
  if (P->cur.type == TK_CONCAT) return np_concat_prec();
  if (P->cur.type == TK_KEYWORD) return np_kw_prec(P->cur.kw);
  return -1;
}

/* 标准的优先级爬升（左结合）；幂运算 ^ 为右结合 */
static Node *np_parse_expr_prec(Parser *P, int min_prec) {
  Node *lhs = np_parse_primary(P);
  while (1) {
    int prec = np_cur_prec(P);
    if (prec < min_prec) break;
    /* 幂运算 ^ 是右结合的 */
    int is_right_assoc = 0;
    if (P->cur.type == TK_OP && P->cur.slen == 1 && P->cur.s[0] == '^') {
      is_right_assoc = 1;
    }
    int next_min = is_right_assoc ? prec : prec + 1;
    int op_enc = np_encode_op(P);
    np_advance(P);
    Node *rhs = np_parse_expr_prec(P, next_min);
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

/**
 * @brief 解析逗号分隔的表达式列表，用于函数调用参数和表值
 * @param P      解析器
 * @param stop   停止 token 类型
 * @return 表达式列表
 */
static NodeList *np_parse_expr_list(Parser *P, int stop) {
  NodeList *l = np_new_nodelist();
  if (P->cur.type == stop) return l;
  np_nl_push(l, np_parse_expr(P));
  while (np_accept(P, TK_COMMA)) {
    if (P->cur.type == stop) break;
    np_nl_push(l, np_parse_expr(P));
  }
  return l;
}

/**
 * @brief 解析标识符列表（用于函数参数和多重赋值左侧）
 * 支持可变参数 "..." 作为最后一个参数
 * @param P          解析器
 * @param stop       停止 token 类型
 * @param has_dots   输出：是否包含可变参数
 * @return 标识符节点列表
 */
static NodeList *np_parse_ident_list(Parser *P, int stop, int *has_dots) {
  NodeList *l = np_new_nodelist();
  if (has_dots) *has_dots = 0;
  if (P->cur.type == stop) return l;
  /* 检查可变参数 ... */
  if (P->cur.type == TK_DOTS) {
    if (has_dots) *has_dots = 1;
    np_advance(P);
    return l;
  }
  if (P->cur.type != TK_IDENT) {
    np_err(P, P->cur.line, "expected identifier");
    return l;
  }
  Node *v = np_new_node(N_VAR);
  v->name = (char *)malloc(P->cur.slen + 1);
  memcpy(v->name, P->cur.s, P->cur.slen);
  v->name[P->cur.slen] = '\0';
  v->line = P->cur.line;
  np_advance(P);
  np_nl_push(l, v);
  while (np_accept(P, TK_COMMA)) {
    if (P->cur.type == stop) break;
    /* 检查可变参数 */
    if (P->cur.type == TK_DOTS) {
      if (has_dots) *has_dots = 1;
      np_advance(P);
      break;
    }
    if (P->cur.type != TK_IDENT) {
      np_err(P, P->cur.line, "expected identifier");
      break;
    }
    v = np_new_node(N_VAR);
    v->name = (char *)malloc(P->cur.slen + 1);
    memcpy(v->name, P->cur.s, P->cur.slen);
    v->name[P->cur.slen] = '\0';
    v->line = P->cur.line;
    np_advance(P);
    np_nl_push(l, v);
  }
  return l;
}

/**
 * @brief 解析函数体（参数列表 + body）
 * 语法: (param1, param2, ...) body end
 * 支持可变参数 ...
 * @param P      解析器
 * @param params 输出参数列表
 * @param body   输出函数体
 * @param has_dots 输出：是否包含可变参数
 */
static void np_parse_func_body_ex(Parser *P, NodeList **params, Node **body, int *has_dots) {
  np_expect(P, TK_LPAREN, "'('");
  *params = np_parse_ident_list(P, TK_RPAREN, has_dots);
  np_expect(P, TK_RPAREN, "')'");
  *body = np_parse_block(P);
  np_expect_kw(P, KW_END, "'end'");
}

static void np_parse_func_body(Parser *P, NodeList **params, Node **body) {
  int dummy;
  np_parse_func_body_ex(P, params, body, &dummy);
}

static Node *np_parse_primary(Parser *P) {
  Node *n = NULL;
  Token *t = &P->cur;
  if (t->type == TK_KEYWORD) {
    if (t->kw == KW_NIL)   { n = np_new_node(N_NIL);   np_advance(P); goto suffix_loop; }
    if (t->kw == KW_TRUE)  { n = np_new_node(N_TRUE);  np_advance(P); goto suffix_loop; }
    if (t->kw == KW_FALSE) { n = np_new_node(N_FALSE); np_advance(P); goto suffix_loop; }
    if (t->kw == KW_NOT) {
      np_advance(P);
      Node *op = np_parse_expr(P);
      n = np_new_node(N_UNOP); n->op = KW_NOT; n->lhs = op; goto suffix_loop;
    }
    if (t->kw == KW_FUNCTION) {
      /* 闭包: function(params) body end  作为表达式 */
      int line = t->line;
      np_advance(P);
      n = np_new_node(N_CLOSURE);
      n->line = line;
      np_parse_func_body(P, &n->params, &n->body);
      goto suffix_loop;
    }
  }
  if (t->type == TK_INT)   { n = np_new_node(N_INT);   n->ival = t->ival; np_advance(P); goto suffix_loop; }
  if (t->type == TK_FLOAT) { n = np_new_node(N_FLOAT); n->fval = t->fval; np_advance(P); goto suffix_loop; }
  if (t->type == TK_STRING) {
    /* 字符串字面量 */
    n = np_new_node(N_STRING);
    n->name = (char *)malloc(t->slen + 1);
    memcpy(n->name, t->s, t->slen);
    n->name[t->slen] = '\0';
    n->ival = t->slen;  /* 存储字符串长度 */
    n->line = t->line;
    np_advance(P);
    goto suffix_loop;
  }
  if (t->type == TK_IDENT) {
    /* 可能为变量引用或函数调用 */
    n = np_new_node(N_VAR);
    n->name = (char *)malloc(t->slen + 1);
    memcpy(n->name, t->s, t->slen); n->name[t->slen] = '\0';
    n->line = t->line;
    np_advance(P);
    goto suffix_loop;
  }
  if (t->type == TK_DOTS) {
    /* 可变参数 ... */
    n = np_new_node(N_DOTS);
    n->line = t->line;
    np_advance(P);
    goto suffix_loop;
  }
  if (t->type == TK_OP && t->slen == 1 && (t->s[0] == '-' || t->s[0] == '+')) {
    char op = t->s[0];
    np_advance(P);
    Node *operand = np_parse_primary(P);
    if (op == '+') { n = operand; goto suffix_loop; }
    n = np_new_node(N_UNOP); n->op = '-'; n->lhs = operand; goto suffix_loop;
  }
  if (t->type == TK_OP && t->slen == 1 && t->s[0] == '#') {
    /* 长度运算符 #expr */
    int line = t->line;
    np_advance(P);
    Node *operand = np_parse_primary(P);
    n = np_new_node(N_UNOP);
    n->op = '#';
    n->lhs = operand;
    n->line = line;
    goto suffix_loop;
  }
  if (t->type == TK_OP && t->slen == 1 && t->s[0] == '~') {
    /* 一元按位取反 ~expr */
    int line = t->line;
    np_advance(P);
    Node *operand = np_parse_primary(P);
    n = np_new_node(N_UNOP);
    n->op = '~';  /* 一元按位取反 */
    n->lhs = operand;
    n->line = line;
    goto suffix_loop;
  }
  if (t->type == TK_LPAREN) {
    np_advance(P);
    n = np_parse_expr(P);
    np_expect(P, TK_RPAREN, "')'");
    goto suffix_loop;
  }
  if (t->type == TK_LBRACE) {
    /* 表字面量: {key=val, ...} 或 {val, val, ...} */
    int line = t->line;
    np_advance(P);
    n = np_new_node(N_TABLE);
    n->line = line;
    n->entries = np_new_nodelist();
    if (P->cur.type != TK_RBRACE) {
      /* 解析第一个条目 */
      Node *first = np_parse_expr(P);
      if (P->cur.type == TK_OP && P->cur.slen == 1 && P->cur.s[0] == '=') {
        /* {key = val} 形式 */
        if (first->kind != N_VAR && first->kind != N_STRING) {
          np_err(P, first->line, "table key must be identifier or string literal");
        }
        np_advance(P);  /* 跳过 '=' */
        Node *entry = np_new_node(N_BINOP);  /* 复用 N_BINOP: lhs=key, rhs=val */
        entry->lhs = first;
        entry->rhs = np_parse_expr(P);
        np_nl_push(n->entries, entry);
      } else {
        /* {val} 数组形式 */
        Node *entry = np_new_node(N_BINOP);
        entry->rhs = first;  /* rhs 存储值 */
        np_nl_push(n->entries, entry);
      }
      /* 解析后续条目 */
      while (np_accept(P, TK_COMMA)) {
        if (P->cur.type == TK_RBRACE) break;
        Node *val = np_parse_expr(P);
        if (P->cur.type == TK_OP && P->cur.slen == 1 && P->cur.s[0] == '=') {
          /* key = val 形式 */
          if (val->kind != N_VAR && val->kind != N_STRING) {
            np_err(P, val->line, "table key must be identifier or string literal");
          }
          np_advance(P);  /* 跳过 '=' */
          Node *entry = np_new_node(N_BINOP);
          entry->lhs = val;
          entry->rhs = np_parse_expr(P);
          np_nl_push(n->entries, entry);
        } else {
          /* 数组形式 */
          Node *entry = np_new_node(N_BINOP);
          entry->rhs = val;
          np_nl_push(n->entries, entry);
        }
      }
    }
    np_expect(P, TK_RBRACE, "'}'");
    goto suffix_loop;
  }
  np_err(P, t->line, "expected expression");
  np_advance(P);
  return np_new_node(N_NIL);

suffix_loop:
  /* 后缀操作符循环：函数调用、成员访问、索引访问 */
  while (1) {
    if (P->cur.type == TK_LPAREN) {
      /* 函数调用后缀 */
      np_advance(P);
      Node *call = np_new_node(N_FUNC_CALL);
      call->line = n->line;
      call->expr = n;
      call->args = np_parse_expr_list(P, TK_RPAREN);
      np_expect(P, TK_RPAREN, "')'");
      n = call;
    } else if (P->cur.type == TK_DOT) {
      /* 成员访问 t.key */
      np_advance(P);
      if (P->cur.type != TK_IDENT) {
        np_err(P, P->cur.line, "expected identifier after '.'");
        break;
      }
      Node *member = np_new_node(N_MEMBER);
      member->line = n->line;
      member->expr = n;
      member->member_name = (char *)malloc(P->cur.slen + 1);
      memcpy(member->member_name, P->cur.s, P->cur.slen);
      member->member_name[P->cur.slen] = '\0';
      np_advance(P);
      n = member;
    } else if (P->cur.type == TK_LBRACE) {
      /* 索引访问 t[expr] — 注意：这里需要区分索引和表字面量 */
      /* 但表字面量已经被 primary 处理了，这里出现的 [ 一定是索引 */
      /* 实际上 TK_LBRACE 是 '{'，索引是 '[' ... 我需要确认 Token 类型 */
      /* 当前 Token 定义中没有 TK_LBRACKET，所以用 '[' 作为 OP 处理 */
      break;
    } else if (P->cur.type == TK_OP && P->cur.slen == 1 && P->cur.s[0] == '[') {
      /* 索引访问 t[expr] */
      np_advance(P);
      Node *idx = np_new_node(N_INDEX);
      idx->line = n->line;
      idx->expr = n;
      idx->index_expr = np_parse_expr(P);
      np_expect(P, TK_OP, "']'");  /* 消费 ']'，它被归类为 TK_OP */
      n = idx;
    } else {
      break;
    }
  }
  return n;
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

    /* === 标签 ::label:: === */
    if (P->cur.type == TK_OP && P->cur.slen == 2 &&
        P->cur.s[0] == ':' && P->cur.s[1] == ':') {
      int line = P->cur.line;
      np_advance(P);
      if (P->cur.type != TK_IDENT) {
        np_err(P, P->cur.line, "expected label name after '::'");
        continue;
      }
      s = np_new_node(N_LABEL);
      s->line = line;
      s->label_name = (char *)malloc(P->cur.slen + 1);
      memcpy(s->label_name, P->cur.s, P->cur.slen);
      s->label_name[P->cur.slen] = '\0';
      np_advance(P);
      /* 期望 :: */
      if (P->cur.type == TK_OP && P->cur.slen == 2 &&
          P->cur.s[0] == ':' && P->cur.s[1] == ':') {
        np_advance(P);
      } else {
        np_err(P, P->cur.line, "expected '::' after label name");
      }
      if (s) np_nl_push(blk->stmts, s);
      while (np_accept(P, TK_SEMI)) {}
      continue;
    }

    /* 解析单条语句 */
    if (P->cur.type == TK_KEYWORD && P->cur.kw == KW_LOCAL) {
      np_advance(P);
      if (P->cur.type == TK_KEYWORD && P->cur.kw == KW_FUNCTION) {
        /* local function name(params) body end */
        np_advance(P);
        if (P->cur.type != TK_IDENT) {
          np_err(P, P->cur.line, "expected function name after 'local function'");
          continue;
        }
        s = np_new_node(N_FUNC_DEF);
        s->line = P->cur.line;
        s->name = (char *)malloc(P->cur.slen + 1);
        memcpy(s->name, P->cur.s, P->cur.slen);
        s->name[P->cur.slen] = '\0';
        np_advance(P);
        int has_dots = 0;
        np_parse_func_body_ex(P, &s->params, &s->body, &has_dots);
        s->has_dots = has_dots;
      } else if (P->cur.type == TK_IDENT) {
        /* local x = expr 或 local x, y, z = expr1, expr2, expr3 */
        /* 先解析第一个标识符 */
        NodeList *vars = np_new_nodelist();
        Node *v = np_new_node(N_VAR);
        v->name = (char *)malloc(P->cur.slen + 1);
        memcpy(v->name, P->cur.s, P->cur.slen);
        v->name[P->cur.slen] = '\0';
        v->line = P->cur.line;
        np_advance(P);
        np_nl_push(vars, v);
        int is_multi = 0;
        while (np_accept(P, TK_COMMA)) {
          if (P->cur.type != TK_IDENT) {
            np_err(P, P->cur.line, "expected identifier after ','");
            break;
          }
          is_multi = 1;
          v = np_new_node(N_VAR);
          v->name = (char *)malloc(P->cur.slen + 1);
          memcpy(v->name, P->cur.s, P->cur.slen);
          v->name[P->cur.slen] = '\0';
          v->line = P->cur.line;
          np_advance(P);
          np_nl_push(vars, v);
        }
        if (is_multi) {
          /* 多重局部变量声明 */
          s = np_new_node(N_MULTI_ASSIGN);
          s->line = P->cur.line;
          s->vars = vars;
          if (np_accept_op(P, "="))
            s->vals = np_parse_expr_list(P, TK_EOF);
          else
            s->vals = np_new_nodelist();
        } else {
          /* 单变量声明 */
          s = np_new_node(N_LOCAL);
          s->line = vars->items[0]->line;
          s->name = strdup(vars->items[0]->name);
          np_free_nodelist(vars);
          if (np_accept_op(P, "=")) s->expr = np_parse_expr(P);
          else { s->expr = np_new_node(N_NIL); s->expr->line = s->line; }
        }
      } else {
        np_err(P, P->cur.line, "expected identifier or 'function' after 'local'");
        np_advance(P);
        continue;
      }
    } else if (P->cur.type == TK_KEYWORD && P->cur.kw == KW_FUNCTION) {
      /* function name(params) body end  或  function obj:method(params) body end */
      int line = P->cur.line;
      np_advance(P);
      if (P->cur.type != TK_IDENT) {
        np_err(P, P->cur.line, "expected function name");
        continue;
      }
      /* 保存函数名 */
      char *fname = (char *)malloc(P->cur.slen + 1);
      memcpy(fname, P->cur.s, P->cur.slen);
      fname[P->cur.slen] = '\0';
      np_advance(P);
      if (P->cur.type == TK_COLON) {
        /* 方法定义: function obj:method(params) body end */
        np_advance(P);
        if (P->cur.type != TK_IDENT) {
          np_err(P, P->cur.line, "expected method name after ':'");
          free(fname);
          continue;
        }
        s = np_new_node(N_METHOD_DEF);
        s->line = line;
        s->name = (char *)malloc(P->cur.slen + 1);
        memcpy(s->name, P->cur.s, P->cur.slen);
        s->name[P->cur.slen] = '\0';
        s->member_name = fname;  /* 对象名 */
        /* self 参数名 */
        s->self_name = np_new_node(N_VAR);
        s->self_name->name = strdup("self");
        np_advance(P);
        int has_dots = 0;
        np_parse_func_body_ex(P, &s->params, &s->body, &has_dots);
        s->has_dots = has_dots;
      } else {
        s = np_new_node(N_FUNC_DEF);
        s->line = line;
        s->name = fname;
        int has_dots = 0;
        np_parse_func_body_ex(P, &s->params, &s->body, &has_dots);
        s->has_dots = has_dots;
      }
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
      /* 保存第一个标识符 */
      char *first_var = (char *)malloc(P->cur.slen + 1);
      memcpy(first_var, P->cur.s, P->cur.slen);
      first_var[P->cur.slen] = '\0';
      np_advance(P);
      /* 检查是 for i = ... 还是 for i, j in ... */
      if (P->cur.type == TK_OP && P->cur.slen == 1 && P->cur.s[0] == '=') {
        /* 数值 for: for var = start, end [, step] do body end */
        np_advance(P);
        s = np_new_node(N_FOR); s->line = line;
        s->name = first_var;
        s->expr = np_parse_expr(P);
        np_expect(P, TK_COMMA, "','");
        s->expr2 = np_parse_expr(P);
        if (np_accept(P, TK_COMMA)) s->expr3 = np_parse_expr(P);
        np_expect_kw(P, KW_DO, "'do'");
        s->body = np_parse_block(P);
        np_expect_kw(P, KW_END, "'end'");
      } else {
        /* 泛型 for: for var1, var2, ... in expr1, expr2, expr3 do body end */
        s = np_new_node(N_FOR_IN); s->line = line;
        s->vars = np_new_nodelist();
        Node *v = np_new_node(N_VAR);
        v->name = first_var;
        v->line = line;
        np_nl_push(s->vars, v);
        while (np_accept(P, TK_COMMA)) {
          if (P->cur.type == TK_IDENT) {
            v = np_new_node(N_VAR);
            v->name = (char *)malloc(P->cur.slen + 1);
            memcpy(v->name, P->cur.s, P->cur.slen);
            v->name[P->cur.slen] = '\0';
            v->line = P->cur.line;
            np_advance(P);
            np_nl_push(s->vars, v);
          } else {
            /* 可能是 'in' 关键字 */
            break;
          }
        }
        np_expect_kw(P, KW_IN, "'in'");
        s->for_in_exprs = np_new_nodelist();
        /* 解析迭代器表达式，直到遇到 'do' 关键字 */
        if (!(P->cur.type == TK_KEYWORD && P->cur.kw == KW_DO)) {
          np_nl_push(s->for_in_exprs, np_parse_expr(P));
          while (np_accept(P, TK_COMMA)) {
            if (P->cur.type == TK_KEYWORD && P->cur.kw == KW_DO) break;
            np_nl_push(s->for_in_exprs, np_parse_expr(P));
          }
        }
        np_expect_kw(P, KW_DO, "'do'");
        s->body = np_parse_block(P);
        np_expect_kw(P, KW_END, "'end'");
      }
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
    } else if (P->cur.type == TK_KEYWORD && P->cur.kw == KW_CONTINUE) {
      /* continue 语句 */
      int line = P->cur.line;
      np_advance(P);
      s = np_new_node(N_CONTINUE); s->line = line;
    } else if (P->cur.type == TK_KEYWORD && P->cur.kw == KW_GOTO) {
      /* goto label 语句 */
      int line = P->cur.line;
      np_advance(P);
      if (P->cur.type != TK_IDENT) {
        np_err(P, P->cur.line, "expected label name after 'goto'");
        continue;
      }
      s = np_new_node(N_GOTO);
      s->line = line;
      s->label_name = (char *)malloc(P->cur.slen + 1);
      memcpy(s->label_name, P->cur.s, P->cur.slen);
      s->label_name[P->cur.slen] = '\0';
      np_advance(P);
    } else if (P->cur.type == TK_KEYWORD && P->cur.kw == KW_DO) {
      /* do ... end 独立块 */
      int line = P->cur.line;
      np_advance(P);
      s = np_new_node(N_DO);
      s->line = line;
      s->body = np_parse_block(P);
      np_expect_kw(P, KW_END, "'end'");
    } else if (P->cur.type == TK_IDENT) {
      /* 可能为 单赋值 或 多重赋值 或 函数调用语句 或 成员赋值 */
      /* 关键：先解析完整左值表达式（含成员/索引访问），再判断类型 */
      /* 简化处理：先 peek 几个 token 判断 */
      np_peek(P);
      if (P->peeked.type == TK_COMMA || P->peeked.type == TK_OP ||
          (P->peeked.type == TK_DOT)) {
        /* 多重赋值或成员赋值: a, b = ... 或 a.b = ... 或 a[expr] = ... */
        /* 先解析左值列表 */
        NodeList *vars = np_new_nodelist();
        /* 使用完整表达式解析左值 */
        Node *first = np_parse_primary(P);
        np_nl_push(vars, first);
        while (np_accept(P, TK_COMMA)) {
          if (P->cur.type == TK_IDENT || P->cur.type == TK_LPAREN) {
            Node *v = np_parse_primary(P);
            np_nl_push(vars, v);
          } else {
            break;
          }
        }
        if (np_accept_op(P, "=")) {
          if (vars->n == 1 && (first->kind == N_MEMBER || first->kind == N_INDEX)) {
            /* 单成员赋值: t.key = expr 或 t[expr] = expr2 */
            Node *assign = first;
            assign->rhs = np_parse_expr(P);
            s = assign;
            /* 释放 vars 列表（但不释放 first） */
            free(vars->items);
            free(vars);
          } else {
            /* 多重赋值 */
            s = np_new_node(N_MULTI_ASSIGN);
            s->line = P->cur.line;
            s->vars = vars;
            s->vals = np_parse_expr_list(P, TK_EOF);
          }
        } else {
          /* 无 '='，可能是函数调用语句 */
          if (vars->n == 1 && first->kind == N_FUNC_CALL) {
            s = first;
            free(vars->items);
            free(vars);
          } else {
            np_err(P, P->cur.line, "expected '=' or function call");
            np_free_nodelist(vars);
            continue;
          }
        }
      } else {
        /* 可能是函数调用语句（如 inc()）或单赋值 */
        Node *primary = np_parse_primary(P);
        if (primary->kind == N_FUNC_CALL) {
          /* 函数调用语句 */
          s = primary;
        } else if (primary->kind == N_VAR) {
          /* 单赋值: x = expr */
          s = np_new_node(N_ASSIGN); s->line = primary->line;
          s->name = strdup(primary->name);
          np_free(primary);
          np_expect_op(P, "=", "'='");
          s->expr = np_parse_expr(P);
        } else {
          np_err(P, P->cur.line, "expected '=' or function call");
          np_free(primary);
          continue;
        }
      }
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
#define NP_TMP_REG_LO  64  /* 临时寄存器从 64 开始，主代码需创建足够 nregs */
#define NP_TMP_REG_HI  255

/** 符号表条目 — 变量的寄存器映射 */
typedef struct {
  char *name;
  int reg;
  int is_float;   /* 变量当前是否为浮点类型 */
  int is_ptr;     /* 变量是否为指针类型（字符串/表引用） */
  int is_func;    /* 变量是否为函数引用 */
  int ptr_ref;    /* Lua registry 引用索引（字符串/表） */
  int func_id;    /* 函数 ID（由 deffunc 分配，编译时未确定） */
} NpSym;

/** 上值条目：闭包捕获的外部变量映射 */
typedef struct {
  int parent_reg;   /* 父 CG 中的寄存器 */
  int closure_reg;  /* 闭包中的寄存器 */
} UpvalueEntry;

/** 待注册的函数定义信息 */
typedef struct {
  char *name;           /* 函数名 */
  lua_Integer *code;    /* 函数体字节码 */
  int ncode;            /* 字节码长度 */
  int nregs;            /* 函数需要的寄存器数 */
  int nparams;          /* 参数个数 */
  int reg;              /* 函数名变量所在的寄存器 */
  /* 上值信息 */
  UpvalueEntry *upvalues;    /* 上值映射列表 */
  int nupvalues;             /* 上值数量 */
} FuncDef;

typedef struct {
  int pc;
  char lname[32];
} Patch;

typedef struct CG_ {
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
  int continue_label_ids[64];   /* continue 跳转目标标签 */
  int n_continue_labels;
  /* goto/label 支持 */
  char goto_labels[64][64];     /* 已定义的标签名 */
  int  goto_label_pc[64];       /* 标签对应的 PC */
  int  n_goto_labels;
  struct { int pc; char lname[64]; } goto_patches[64]; /* 待回填的 goto */
  int  n_goto_patches;
  char err[256];
  int has_err;
  /* 新增：函数定义 */
  lua_State *L;              /* Lua 状态机，用于存储字符串/表/函数字节码 */
  FuncDef  *func_defs;       /* 函数定义列表 */
  int       n_func_defs;     /* 函数定义数量 */
  int       cap_func_defs;   /* 函数定义列表容量 */
  /* 上值跟踪（闭包捕获外部变量） */
  struct CG_ *parent;          /* 父 CG 上下文（闭包内使用） */
  UpvalueEntry *upvalues;    /* 上值映射列表 */
  int       nupvalues;       /* 上值数量 */
  int       capupvalues;     /* 上值列表容量 */
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

/* 前向声明：上值相关函数，在 cg_find_sym 之后定义 */
static NpSym *cg_add_sym(CG *g, const char *name);
static void cg_add_upvalue(CG *g, int parent_reg, int closure_reg);
static void cg_extract_upvalues(CG *sub, UpvalueEntry **out_upvalues, int *out_nupvalues);

static NpSym *cg_find_sym(CG *g, const char *name) {
  for (int i = 0; i < g->n_syms; i++)
    if (strcmp(g->syms[i].name, name) == 0) return &g->syms[i];
  /* 如果当前 CG 有父 CG（闭包内），尝试在父 CG 中查找上值 */
  if (g->parent) {
    NpSym *ps = cg_find_sym(g->parent, name);  /* 递归向上查找 */
    if (ps) {
      /* 找到上值：在闭包中分配新寄存器并记录映射 */
      NpSym *cls_sym = cg_add_sym(g, name);
      if (!cls_sym) return NULL;
      cg_add_upvalue(g, ps->reg, cls_sym->reg);
      cls_sym->is_float = ps->is_float;  /* 继承父符号的类型信息 */
      return cls_sym;
    }
  }
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
  s->is_ptr = 0;
  s->is_func = 0;
  s->ptr_ref = 0;
  s->func_id = -1;
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

/**
 * @brief 添加函数定义到待注册列表
 * 函数体字节码在编译函数体时生成，在此处记录以便后续注册
 */
static void cg_add_func_def(CG *g, const char *name, lua_Integer *code, int ncode, int nregs, int nparams, int reg,
                             UpvalueEntry *upvalues, int nupvalues) {
  if (g->n_func_defs >= g->cap_func_defs) {
    g->cap_func_defs = g->cap_func_defs ? g->cap_func_defs * 2 : 8;
    g->func_defs = (FuncDef *)realloc(g->func_defs, sizeof(FuncDef) * g->cap_func_defs);
  }
  FuncDef *fd = &g->func_defs[g->n_func_defs++];
  fd->name = name ? strdup(name) : NULL;
  fd->code = code;
  fd->ncode = ncode;
  fd->nregs = nregs;
  fd->nparams = nparams;
  fd->reg = reg;
  fd->upvalues = upvalues;
  fd->nupvalues = nupvalues;
}

/* === 表达式代码生成 === */

static int cg_expr(CG *g, Node *e, int dst);

/* 二元运算：lreg/rreg 是源寄存器，lf/rf 是源类型，dst 是目标，输出类型到 *out_is_float */
static void cg_binop(CG *g, int op_enc, int lreg, int lf, int rreg, int rf, int dst, int *out_is_float) {
  int target_float = lf || rf;

  /* === 特殊运算符：通过 lua_State 处理 === */
  /* 字符串拼接 .. */
  if (op_enc == 512) {
    /* 通过 NI_CONCAT 指令处理 */
    cg_emit(g, NI_CONCAT, dst, lreg, rreg, 0);
    *out_is_float = 0;
    return;
  }
  /* 幂运算 ^ */
  if (op_enc == '^') {
    /* 通过 NI_POW 指令处理 */
    cg_emit(g, NI_POW, dst, lreg, rreg, 0);
    if (target_float) *out_is_float = 1; else *out_is_float = 0;
    return;
  }
  /* 整除 // */
  if (op_enc == ('/' << 8 | '/')) {
    /* 通过 NI_IDIV 指令处理 */
    cg_emit(g, NI_IDIV, dst, lreg, rreg, 0);
    *out_is_float = 0;
    return;
  }
  /* 按位异或 ~（二元 XOR） */
  if (op_enc == '~') {
    if (target_float) {
      int li = cg_alloc_tmp(g), ri = cg_alloc_tmp(g);
      cg_emit(g, NI_F2I, li, lreg, 0, 0);
      cg_emit(g, NI_F2I, ri, rreg, 0, 0);
      cg_emit(g, NI_XOR, dst, li, ri, 0);
    } else {
      cg_emit(g, NI_XOR, dst, lreg, rreg, 0);
    }
    *out_is_float = 0;
    return;
  }

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
    else if (op_enc == '&' || op_enc == '|' ||
             op_enc == ('<' << 8 | '<') || op_enc == ('>' << 8 | '>')) {
      /* 位运算：强转 int */
      int li = cg_alloc_tmp(g), ri = cg_alloc_tmp(g);
      cg_emit(g, NI_F2I, li, lt, 0, 0);
      cg_emit(g, NI_F2I, ri, rt, 0, 0);
      int iop = -1;
      if      (op_enc == '&')                  iop = NI_AND;
      else if (op_enc == '|')                  iop = NI_OR;
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
  else if (op_enc == ('<' << 8 | '<'))             op = NI_SHL;
  else if (op_enc == ('>' << 8 | '>'))             op = NI_SHR;
  else if (op_enc == ('<' << 8 | '='))             op = NI_LE;
  else if (op_enc == ('>' << 8 | '='))           { op = NI_LE; swap = 1; }
  else if (op_enc == '<')                          op = NI_LT;
  else if (op_enc == '>')                        { op = NI_LT; swap = 1; }
  else if (op_enc == ('=' << 8 | '='))             op = NI_EQ;
  else if (op_enc == ('~' << 8 | '='))             op = NI_NE;
  else if (op_enc == 256 + KW_AND) {
    /* 逻辑与: 若 a 为 falsy(0) 返回 a，否则返回 b */
    cg_emit(g, NI_MOV, dst, lreg, 0, 0);
    cg_emit(g, NI_JF, dst, 0, 0, 1);   /* 若 dst==0 跳过下一条 MOV */
    cg_emit(g, NI_MOV, dst, rreg, 0, 0);
    *out_is_float = 0;
    return;
  }
  else if (op_enc == 256 + KW_OR) {
    /* 逻辑或: 若 a 为 truthy(非0) 返回 a，否则返回 b */
    cg_emit(g, NI_MOV, dst, lreg, 0, 0);
    cg_emit(g, NI_JT, dst, 0, 0, 1);   /* 若 dst!=0 跳过下一条 MOV */
    cg_emit(g, NI_MOV, dst, rreg, 0, 0);
    *out_is_float = 0;
    return;
  }
  if (op < 0) { if (!g->has_err) { g->has_err = 1; snprintf(g->err, sizeof(g->err), "unsupported operator"); } *out_is_float = 0; return; }
  if (swap) cg_emit(g, op, dst, rreg, lreg, 0);
  else      cg_emit(g, op, dst, lreg, rreg, 0);
  *out_is_float = 0;
}

/* 前向声明 */
static void cg_stmt(CG *g, Node *s);

/**
 * @brief 编译函数体生成独立的字节码
 * 创建一个新的 CG 上下文来编译函数体
 * @param g        主 CG 上下文
 * @param body     函数体 AST
 * @param params   参数列表
 * @param nregs    输出：函数需要的寄存器数
 * @param nparams  输出：参数个数
 * @param out_ncode 输出：字节码长度
 * @return 新生成的字节码数组（调用者负责释放）
 */
/**
 * @brief 编译函数体为独立字节码
 * @param g         父 CG 上下文
 * @param body      函数体 AST
 * @param params    参数列表
 * @param func_name 函数名（用于递归自引用），可为 NULL
 * @param nregs     输出：需要的寄存器数
 * @param nparams   输出：参数个数
 * @param out_ncode 输出：字节码长度
 * @return 函数字节码，调用者负责释放
 */
/**
 * @brief 添加上值映射到当前 CG 上下文
 * @param g         当前 CG 上下文
 * @param parent_reg 父 CG 中的寄存器索引
 * @param closure_reg 当前闭包中的寄存器索引
 */
static void cg_add_upvalue(CG *g, int parent_reg, int closure_reg) {
  if (g->nupvalues >= g->capupvalues) {
    g->capupvalues = g->capupvalues ? g->capupvalues * 2 : 4;
    g->upvalues = (UpvalueEntry *)realloc(g->upvalues, sizeof(UpvalueEntry) * g->capupvalues);
  }
  g->upvalues[g->nupvalues].parent_reg = parent_reg;
  g->upvalues[g->nupvalues].closure_reg = closure_reg;
  g->nupvalues++;
}

static lua_Integer *cg_compile_func_body(CG *g, Node *body, NodeList *params,
                                          const char *func_name,
                                          int *nregs, int *nparams, int *out_ncode,
                                          UpvalueEntry **out_upvalues, int *out_nupvalues) {
  /* 创建子 CG 上下文 */
  CG sub;
  memset(&sub, 0, sizeof(sub));
  sub.next_user_reg = 0;
  sub.next_tmp_reg = NP_TMP_REG_LO;
  sub.L = g->L;
  sub.parent = g;  /* 设置父 CG，用于闭包捕获外部变量 */

  /* 如果函数有名字，先注册到子上下文（占 R0）以支持递归自调用
   * 必须在参数分配之前注册，确保 R0 保留给自引用，参数从 R1 开始 */
  if (func_name && func_name[0]) {
    NpSym *self_sym = cg_add_sym(&sub, func_name);
    if (self_sym) {
      self_sym->is_func = 1;
    }
  } else {
    /* 闭包：无函数名，但仍需保留 R0 给 VM 的自引用（NTYPE_FUNC），参数从 R1 开始 */
    sub.next_user_reg = 1;
  }

  /* 为参数分配寄存器（从 R1 开始，因为 R0 保留给自引用） */
  *nparams = params ? params->n : 0;
  for (int i = 0; i < *nparams; i++) {
    Node *p = params->items[i];
    NpSym *sym = cg_add_sym(&sub, p->name);
    if (!sym) { *nregs = 0; *nparams = 0; *out_ncode = 0; return NULL; }
  }

  /* 编译函数体 */
  cg_stmt(&sub, body);

  if (sub.has_err) {
    snprintf(g->err, sizeof(g->err), "%s", sub.err);
    g->has_err = 1;
    free(sub.code);
    free(sub.upvalues);
    *out_ncode = 0;
    return NULL;
  }

  cg_emit(&sub, NI_HALT, 0, 0, 0, 0);
  cg_resolve_patches(&sub);

  /* 解析子函数中 goto 跳转补丁 */
  for (int i = 0; i < sub.n_goto_patches; i++) {
    int found = -1;
    for (int j = 0; j < sub.n_goto_labels; j++) {
      if (strcmp(sub.goto_labels[j], sub.goto_patches[i].lname) == 0) {
        found = j;
        break;
      }
    }
    if (found < 0) {
      if (!sub.has_err) {
        sub.has_err = 1;
        snprintf(sub.err, sizeof(sub.err), "undefined label '%s'", sub.goto_patches[i].lname);
      }
      continue;
    }
    int target = sub.goto_label_pc[found];
    int32_t offset = (int32_t)(target - sub.goto_patches[i].pc - 1);
    sub.code[sub.goto_patches[i].pc] = np_make_ni(NI_JMP, 0, 0, 0, offset);
  }

  if (sub.has_err) {
    snprintf(g->err, sizeof(g->err), "%s", sub.err);
    g->has_err = 1;
    free(sub.code);
    free(sub.upvalues);
    *out_ncode = 0;
    return NULL;
  }

  *nregs = sub.next_user_reg;
  if (sub.next_tmp_reg > *nregs) *nregs = sub.next_tmp_reg;
  if (*nregs < 16) *nregs = 16;
  *out_ncode = sub.ncode;

  /* 提取上值信息 */
  if (out_upvalues && out_nupvalues) {
    cg_extract_upvalues(&sub, out_upvalues, out_nupvalues);
  }

  return sub.code;  /* 调用者负责释放，sub 是栈变量，code 不会被自动释放 */
}

/* 辅助：从子 CG 中提取上值信息（所有权转移） */
static void cg_extract_upvalues(CG *sub, UpvalueEntry **out_upvalues, int *out_nupvalues) {
  *out_upvalues = sub->upvalues;
  *out_nupvalues = sub->nupvalues;
  sub->upvalues = NULL;
  sub->nupvalues = 0;
  sub->capupvalues = 0;
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
    case N_STRING: {
      /* 字符串存储在 Lua 端，寄存器中存 NTYPE_PTR + registry ref */
      if (!g->L) { if (!g->has_err) { g->has_err = 1; snprintf(g->err, sizeof(g->err), "no lua_State for string"); } return 0; }
      lua_pushlstring(g->L, e->name, (size_t)e->ival);
      int ref = luaL_ref(g->L, LUA_REGISTRYINDEX);
      cg_emit(g, NI_LOADKPTR, dst, 0, 0, (int32_t)ref);
      return 0;
    }
    case N_FUNC_CALL: {
      /* 函数调用: NI_CALL: R[a] = call(R[b], R[c..c+imm-1], imm) */
      int nargs = e->args ? e->args->n : 0;
      /* 先分配参数寄存器区域，预留 nargs 个连续槽位 */
      int args_start = cg_alloc_tmp(g);
      if (nargs > 1) g->next_tmp_reg += nargs - 1;  /* 预留剩余 nargs-1 个槽位 */
      for (int i = 0; i < nargs; i++) {
        int arg_reg = args_start + i;
        if (arg_reg > NP_TMP_REG_HI) arg_reg = cg_alloc_tmp(g);
        cg_expr(g, e->args->items[i], arg_reg);
      }
      /* 计算函数引用到临时寄存器（在参数槽位之后，避免冲突） */
      int func_reg = cg_alloc_tmp(g);
      if (e->expr && e->expr->kind == N_VAR) {
        /* 通过名称查找函数 */
        NpSym *s = cg_find_sym(g, e->expr->name);
        if (!s) { if (!g->has_err) { g->has_err = 1; snprintf(g->err, sizeof(g->err), "undefined function '%s'", e->expr->name); } return 0; }
        cg_emit(g, NI_MOV, func_reg, s->reg, 0, 0);
      } else if (e->expr) {
        /* 复杂表达式求值（如闭包调用） */
        cg_expr(g, e->expr, func_reg);
      } else {
        if (!g->has_err) { g->has_err = 1; snprintf(g->err, sizeof(g->err), "invalid function call"); }
        return 0;
      }
      /* 生成 NI_CALL 指令 */
      cg_emit(g, NI_CALL, dst, func_reg, args_start, (int32_t)nargs);
      return 0;
    }
    case N_CLOSURE: {
      /* 闭包: function(params) body end 作为表达式 */
      /* 编译闭包函数体 */
      int nregs, nparams, fncode;
      UpvalueEntry *upvalues = NULL;
      int nupvalues = 0;
      lua_Integer *func_code = cg_compile_func_body(g, e->body, e->params, NULL, &nregs, &nparams, &fncode, &upvalues, &nupvalues);
      if (!func_code) return 0;

      /* 记录闭包函数定义，使用自动生成的名称 */
      char auto_name[64];
      snprintf(auto_name, sizeof(auto_name), "__closure_%d", g->label_counter);
      cg_add_func_def(g, auto_name, func_code, fncode, nregs, nparams, dst, upvalues, nupvalues);

      /* 暂时在 dst 中存储标记值，后续由 np_compile 解析 func_id 后回填 */
      cg_emit(g, NI_LOADK, dst, 0, 0, (int32_t)(-1));  /* 占位符 */
      return 0;
    }
    case N_TABLE: {
      /* 表字面量：通过 Lua 端创建表，存储引用 */
      if (!g->L) { if (!g->has_err) { g->has_err = 1; snprintf(g->err, sizeof(g->err), "no lua_State for table"); } return 0; }
      lua_newtable(g->L);
      int arr_idx = 1;
      for (int i = 0; i < e->entries->n; i++) {
        Node *entry = e->entries->items[i];
        if (entry->lhs) {
          /* key = val 形式 */
          if (entry->lhs->kind == N_VAR) {
            lua_pushstring(g->L, entry->lhs->name);
          } else if (entry->lhs->kind == N_STRING) {
            lua_pushlstring(g->L, entry->lhs->name, (size_t)entry->lhs->ival);
          } else {
            /* 不支持复杂 key，跳过 */
            continue;
          }
          /* 计算 value 表达式 */
          /* 对于简单值，直接在 Lua 端处理 */
          if (entry->rhs->kind == N_INT) {
            lua_pushinteger(g->L, entry->rhs->ival);
          } else if (entry->rhs->kind == N_FLOAT) {
            lua_pushnumber(g->L, entry->rhs->fval);
          } else if (entry->rhs->kind == N_NIL) {
            lua_pushnil(g->L);
          } else if (entry->rhs->kind == N_TRUE) {
            lua_pushboolean(g->L, 1);
          } else if (entry->rhs->kind == N_FALSE) {
            lua_pushboolean(g->L, 0);
          } else if (entry->rhs->kind == N_STRING) {
            lua_pushlstring(g->L, entry->rhs->name, (size_t)entry->rhs->ival);
          } else {
            /* 复杂表达式：在 VM 中计算后从寄存器取值 */
            /* 这里简化处理：跳过复杂表达式，后续可扩展 */
            lua_pushnil(g->L);
          }
          lua_settable(g->L, -3);
        } else {
          /* 数组形式：整数索引 */
          lua_pushinteger(g->L, arr_idx++);
          if (entry->rhs->kind == N_INT) {
            lua_pushinteger(g->L, entry->rhs->ival);
          } else if (entry->rhs->kind == N_FLOAT) {
            lua_pushnumber(g->L, entry->rhs->fval);
          } else if (entry->rhs->kind == N_NIL) {
            lua_pushnil(g->L);
          } else if (entry->rhs->kind == N_TRUE) {
            lua_pushboolean(g->L, 1);
          } else if (entry->rhs->kind == N_FALSE) {
            lua_pushboolean(g->L, 0);
          } else if (entry->rhs->kind == N_STRING) {
            lua_pushlstring(g->L, entry->rhs->name, (size_t)entry->rhs->ival);
          } else {
            lua_pushnil(g->L);
          }
          lua_settable(g->L, -3);
        }
      }
      int ref = luaL_ref(g->L, LUA_REGISTRYINDEX);
      cg_emit(g, NI_LOADKPTR, dst, 0, 0, (int32_t)ref);
      return 0;
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
      /* 长度运算符 #expr */
      if (e->op == '#') {
        int src = cg_alloc_tmp(g);
        cg_expr(g, e->lhs, src);
        cg_emit(g, NI_LEN, dst, src, 0, 0);
        return 0;
      }
      /* 一元按位取反 ~expr */
      if (e->op == '~') {
        int src = cg_alloc_tmp(g);
        cg_expr(g, e->lhs, src);
        cg_emit(g, NI_BNOT, dst, src, 0, 0);
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
    /* === 新增表达式类型 === */
    case N_MEMBER: {
      /* t.key — 成员访问，等价于 t["key"] */
      /* 先计算表表达式 */
      int tbl_reg = cg_alloc_tmp(g);
      cg_expr(g, e->expr, tbl_reg);
      /* 将 key 字符串存入 Lua 注册表 */
      if (!g->L) { if (!g->has_err) { g->has_err = 1; snprintf(g->err, sizeof(g->err), "no lua_State for member access"); } return 0; }
      lua_pushstring(g->L, e->member_name);
      int key_ref = luaL_ref(g->L, LUA_REGISTRYINDEX);
      /* 通过 NI_GETFIELD 指令获取字段 */
      cg_emit(g, NI_GETFIELD, dst, tbl_reg, 0, (int32_t)key_ref);
      return 0;
    }
    case N_INDEX: {
      /* t[expr] — 索引访问 */
      int tbl_reg = cg_alloc_tmp(g);
      cg_expr(g, e->expr, tbl_reg);
      int idx_reg = cg_alloc_tmp(g);
      cg_expr(g, e->index_expr, idx_reg);
      cg_emit(g, NI_GETTABLE, dst, tbl_reg, idx_reg, 0);
      return 0;
    }
    case N_DOTS: {
      /* 可变参数 ... — 当前暂时返回 nil */
      cg_emit(g, NI_SETNIL, dst, 0, 0, 0);
      return 0;
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
    case N_MULTI_ASSIGN: {
      /* 多重赋值: a, b, c = expr1, expr2, expr3 */
      /* 先计算右侧所有表达式到临时寄存器 */
      int nvals = s->vals ? s->vals->n : 0;
      int *tmp_regs = (int *)malloc(sizeof(int) * nvals);
      for (int i = 0; i < nvals; i++) {
        tmp_regs[i] = cg_alloc_tmp(g);
        cg_expr(g, s->vals->items[i], tmp_regs[i]);
      }
      /* 将右侧值赋给左侧变量 */
      int nvars = s->vars ? s->vars->n : 0;
      for (int i = 0; i < nvars; i++) {
        Node *v = s->vars->items[i];
        NpSym *sym = cg_find_sym(g, v->name);
        if (!sym) {
          /* 未声明的变量：自动作为局部变量声明 */
          sym = cg_add_sym(g, v->name);
          if (!sym) continue;
        }
        if (i < nvals) {
          cg_emit(g, NI_MOV, sym->reg, tmp_regs[i], 0, 0);
          sym->is_float = 0;  /* 简化：多赋值不跟踪浮点状态 */
        } else {
          /* 多余变量设为 nil */
          cg_emit(g, NI_SETNIL, sym->reg, 0, 0, 0);
        }
      }
      free(tmp_regs);
      break;
    }
    case N_FUNC_DEF: {
      /* 函数定义: function name(params) body end */
      /* 为函数名创建符号 */
      NpSym *sym = cg_add_sym(g, s->name);
      if (!sym) break;
      sym->is_func = 1;

      /* 编译函数体 */
      int nregs, nparams, fncode;
      UpvalueEntry *upvalues = NULL;
      int nupvalues = 0;
      lua_Integer *func_code = cg_compile_func_body(g, s->body, s->params, s->name, &nregs, &nparams, &fncode, &upvalues, &nupvalues);
      if (!func_code) break;

      /* 记录函数定义，后续由 np_compile 注册 */
      cg_add_func_def(g, s->name, func_code, fncode, nregs, nparams, sym->reg, upvalues, nupvalues);

      /* 占位：暂时在寄存器中存 -1，后续由 np_compile 解析 func_id 后回填 */
      cg_emit(g, NI_LOADK, sym->reg, 0, 0, (int32_t)(-1));
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
      if (g->n_continue_labels < 64) g->continue_label_ids[g->n_continue_labels++] = L_start;
      cg_stmt(g, s->body);
      g->n_break_labels--;
      g->n_continue_labels--;
      cg_emit_jump_to_label(g, NI_JMP, 0, L_start);
      cg_emit_label(g, L_end);
      break;
    }
    case N_REPEAT: {
      int L_start = cg_new_label(g);
      int L_end   = cg_new_label(g);
      cg_emit_label(g, L_start);
      if (g->n_break_labels < 64) g->break_label_ids[g->n_break_labels++] = L_end;
      if (g->n_continue_labels < 64) g->continue_label_ids[g->n_continue_labels++] = L_start;
      cg_stmt(g, s->body2);
      g->n_break_labels--;
      g->n_continue_labels--;
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
      if (g->n_continue_labels < 64) g->continue_label_ids[g->n_continue_labels++] = L_start;
      cg_stmt(g, s->body);
      g->n_break_labels--;
      g->n_continue_labels--;
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
    /* === 新增语句类型 === */
    case N_CONTINUE:
      /* continue 跳转到当前循环的条件检查开头（while_start 标签） */
      if (g->n_continue_labels > 0) {
        int L = g->continue_label_ids[g->n_continue_labels - 1];
        cg_emit_jump_to_label(g, NI_JMP, 0, L);
      }
      break;
    case N_GOTO:
      /* goto label — 记录待回填的跳转 */
      if (g->n_goto_patches < 64) {
        int idx = cg_emit(g, NI_JMP, 0, 0, 0, 0);
        snprintf(g->goto_patches[g->n_goto_patches].lname, 64, "%s", s->label_name);
        g->goto_patches[g->n_goto_patches].pc = idx;
        g->n_goto_patches++;
      }
      break;
    case N_LABEL:
      /* ::label:: — 记录标签位置 */
      if (g->n_goto_labels < 64) {
        snprintf(g->goto_labels[g->n_goto_labels], 64, "%s", s->label_name);
        g->goto_label_pc[g->n_goto_labels] = g->ncode;
        g->n_goto_labels++;
      }
      break;
    case N_DO:
      /* do ... end 独立块 */
      cg_stmt(g, s->body);
      break;
    case N_FOR_IN: {
      /* 泛型 for 循环: for var1, var2 in expr1, expr2, expr3 do body end */
      /* 为循环变量分配寄存器 */
      int nvars = s->vars ? s->vars->n : 0;
      int var_regs[8];
      int var_start = -1;
      for (int i = 0; i < nvars && i < 8; i++) {
        Node *v = s->vars->items[i];
        NpSym *sym = cg_add_sym(g, v->name);
        if (!sym) break;
        var_regs[i] = sym->reg;
        if (i == 0) var_start = sym->reg;
      }
      /* 计算迭代器表达式 */
      int nexprs = s->for_in_exprs ? s->for_in_exprs->n : 0;
      int expr_start = cg_alloc_tmp(g);
      for (int i = 0; i < nexprs; i++) {
        int reg = expr_start + i;
        if (reg > NP_TMP_REG_HI) reg = cg_alloc_tmp(g);
        cg_expr(g, s->for_in_exprs->items[i], reg);
      }
      /* NI_FOR_IN_INIT: 初始化迭代器 */
      cg_emit(g, NI_FOR_IN_INIT, var_start, expr_start, 0, (int32_t)((nvars << 16) | nexprs));
      int L_start = cg_new_label(g);
      int L_end   = cg_new_label(g);
      cg_emit_label(g, L_start);
      /* NI_FOR_IN_NEXT: 获取下一个迭代值，结果在 var_regs 中 */
      cg_emit(g, NI_FOR_IN_NEXT, var_start, 0, 0, (int32_t)nvars);
      /* 检查是否迭代结束（第一个变量为 nil） */
      int nil_check = cg_alloc_tmp(g);
      cg_emit(g, NI_ISNIL, nil_check, var_regs[0], 0, 0);
      cg_emit_jump_to_label(g, NI_JT, nil_check, L_end);
      /* 设置 break/continue 标签 */
      if (g->n_break_labels < 64) g->break_label_ids[g->n_break_labels++] = L_end;
      if (g->n_continue_labels < 64) g->continue_label_ids[g->n_continue_labels++] = L_start;
      cg_stmt(g, s->body);
      g->n_break_labels--;
      g->n_continue_labels--;
      cg_emit_jump_to_label(g, NI_JMP, 0, L_start);
      cg_emit_label(g, L_end);
      break;
    }
    case N_METHOD_DEF: {
      /* 方法定义: function obj:method(args) body end */
      /* 等价于 obj.method = function(self, args) body end */
      /* 为方法名创建符号（作为闭包存储在变量中） */
      NpSym *sym = cg_add_sym(g, s->name);
      if (!sym) break;
      sym->is_func = 1;
      /* 编译函数体（含 self 参数） */
      int nregs, nparams, fncode;
      UpvalueEntry *upvalues = NULL;
      int nupvalues = 0;
      lua_Integer *func_code = cg_compile_func_body(g, s->body, s->params, s->name, &nregs, &nparams, &fncode, &upvalues, &nupvalues);
      if (!func_code) break;
      nparams++;  /* 加上 self 参数 */
      /* 记录函数定义 */
      cg_add_func_def(g, s->name, func_code, fncode, nregs, nparams, sym->reg, upvalues, nupvalues);
      /* 占位 */
      cg_emit(g, NI_LOADK, sym->reg, 0, 0, (int32_t)(-1));
      break;
    }
    case N_MEMBER: {
      /* 成员赋值语句: t.key = expr （N_MEMBER 节点中 rhs 存储值） */
      if (!s->rhs) break;
      int tbl_reg = cg_alloc_tmp(g);
      cg_expr(g, s->expr, tbl_reg);
      int val_reg = cg_alloc_tmp(g);
      cg_expr(g, s->rhs, val_reg);
      /* 将 key 字符串存入 Lua 注册表 */
      if (!g->L) { if (!g->has_err) { g->has_err = 1; snprintf(g->err, sizeof(g->err), "no lua_State for member set"); } break; }
      lua_pushstring(g->L, s->member_name);
      int key_ref = luaL_ref(g->L, LUA_REGISTRYINDEX);
      cg_emit(g, NI_SETFIELD, tbl_reg, val_reg, 0, (int32_t)key_ref);
      break;
    }
    case N_INDEX: {
      /* 索引赋值语句: t[expr] = val （N_INDEX 节点中 rhs 存储值） */
      if (!s->rhs) break;
      int tbl_reg = cg_alloc_tmp(g);
      cg_expr(g, s->expr, tbl_reg);
      int idx_reg = cg_alloc_tmp(g);
      cg_expr(g, s->index_expr, idx_reg);
      int val_reg = cg_alloc_tmp(g);
      cg_expr(g, s->rhs, val_reg);
      cg_emit(g, NI_SETTABLE, tbl_reg, idx_reg, val_reg, 0);
      break;
    }
    case N_FUNC_CALL: {
      /* 函数调用语句（丢弃返回值） */
      int tmp = cg_alloc_tmp(g);
      cg_expr(g, s, tmp);
      break;
    }
    default:
      if (!g->has_err) { g->has_err = 1; snprintf(g->err, sizeof(g->err), "unsupported stmt kind %d", s->kind); }
      break;
  }
}


/* ============================================================
** 编译入口
** ============================================================ */

/**
 * @brief 编译 NLang 源代码为 NativeVM 字节码
 * 参数: source_string
 * 返回: (1) 主字节码数组 {inst1, inst2, ...}
 *       (2) 函数定义表 {{name, code, nregs, nparams}, ...}
 */
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
  g.L = L;  /* 设置 Lua 状态机，用于存储字符串/表/函数 */
  cg_stmt(&g, program);
  if (g.has_err) {
    char err[300];
    snprintf(err, sizeof(err), "%s", g.err);
    /* 清理函数定义内存 */
    for (int i = 0; i < g.n_func_defs; i++) {
      free(g.func_defs[i].name);
      free(g.func_defs[i].code);
      free(g.func_defs[i].upvalues);
    }
    free(g.func_defs);
    np_free(program);
    return luaL_error(L, "%s", err);
  }
  cg_emit(&g, NI_HALT, 0, 0, 0, 0);
  cg_resolve_patches(&g);

  /* 解析 goto 跳转补丁 */
  for (int i = 0; i < g.n_goto_patches; i++) {
    int found = -1;
    for (int j = 0; j < g.n_goto_labels; j++) {
      if (strcmp(g.goto_labels[j], g.goto_patches[i].lname) == 0) {
        found = j;
        break;
      }
    }
    if (found < 0) {
      if (!g.has_err) {
        g.has_err = 1;
        snprintf(g.err, sizeof(g.err), "undefined label '%s'", g.goto_patches[i].lname);
      }
      continue;
    }
    int target = g.goto_label_pc[found];
    int32_t offset = (int32_t)(target - g.goto_patches[i].pc - 1);
    g.code[g.goto_patches[i].pc] = np_make_ni(NI_JMP, 0, 0, 0, offset);
  }

  /* 回填函数定义占位符：将 LOADK reg, -1 替换为实际的 func_id */
  for (int i = 0; i < g.n_func_defs; i++) {
    int target_reg = g.func_defs[i].reg;
    for (int j = 0; j < g.ncode; j++) {
      if (NI_OP(g.code[j]) == NI_LOADK &&
          NI_A(g.code[j]) == target_reg &&
          NI_IMM(g.code[j]) == (int32_t)(-1)) {
        g.code[j] = np_make_ni(NI_LOADK, target_reg, 0, 0, (int32_t)i);
        break;
      }
    }
  }

  if (g.has_err) {
    char err[300];
    snprintf(err, sizeof(err), "%s", g.err);
    for (int i = 0; i < g.n_func_defs; i++) {
      free(g.func_defs[i].name);
      free(g.func_defs[i].code);
      free(g.func_defs[i].upvalues);
    }
    free(g.func_defs);
    np_free(program);
    return luaL_error(L, "%s", err);
  }
  np_free(program);

  /* 移除栈上的源字符串参数，后续返回值从栈底开始 */
  lua_remove(L, 1);

  /* 计算需要的寄存器数：max(用户寄存器, 临时寄存器) */
  int nregs = g.next_user_reg;
  if (g.next_tmp_reg > nregs) nregs = g.next_tmp_reg;
  if (nregs < 64) nregs = 64;

  /* 返回主字节码 */
  lua_createtable(L, g.ncode, 0);
  for (int i = 0; i < g.ncode; i++) {
    lua_pushinteger(L, g.code[i]);
    lua_rawseti(L, -2, i + 1);
  }

  /* 返回 nregs */
  lua_pushinteger(L, nregs);

  /* 返回函数定义表 */
  lua_createtable(L, g.n_func_defs, 0);
  for (int i = 0; i < g.n_func_defs; i++) {
    FuncDef *fd = &g.func_defs[i];
    lua_createtable(L, 0, 7);
    if (fd->name) {
      lua_pushstring(L, fd->name);
      lua_setfield(L, -2, "name");
    }
    lua_pushinteger(L, fd->reg);
    lua_setfield(L, -2, "reg");
    lua_pushinteger(L, fd->nregs);
    lua_setfield(L, -2, "nregs");
    lua_pushinteger(L, fd->nparams);
    lua_setfield(L, -2, "nparams");
    /* 函数体字节码 */
    lua_createtable(L, fd->ncode, 0);
    for (int j = 0; j < fd->ncode; j++) {
      lua_pushinteger(L, fd->code[j]);
      lua_rawseti(L, -2, j + 1);
    }
    lua_setfield(L, -2, "code");
    /* 上值信息 */
    if (fd->nupvalues > 0) {
      lua_createtable(L, fd->nupvalues, 0);
      for (int j = 0; j < fd->nupvalues; j++) {
        lua_createtable(L, 0, 2);
        lua_pushinteger(L, fd->upvalues[j].parent_reg);
        lua_setfield(L, -2, "src");
        lua_pushinteger(L, fd->upvalues[j].closure_reg);
        lua_setfield(L, -2, "dst");
        lua_rawseti(L, -2, j + 1);
      }
      lua_setfield(L, -2, "upvalues");
    }
    lua_rawseti(L, -2, i + 1);
  }

  /* 清理 */
  free(g.code);
  for (int i = 0; i < g.n_func_defs; i++) {
    free(g.func_defs[i].name);
    free(g.func_defs[i].code);
    free(g.func_defs[i].upvalues);
  }
  free(g.func_defs);
  return 3;  /* 返回三个值：主字节码 + nregs + 函数定义表 */
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