/*
** $Id: last_parse.c $
** AST Parser - Recursive descent parser building LAST AST nodes
** See Copyright Notice in lua.h
*/

#define last_parse_c
#define LUA_CORE

#include "lprefix.h"


#include <string.h>
#include <stdio.h>
#include <stdarg.h>

#include "lua.h"

#include "llex.h"
#include "last.h"
#include "last_parse.h"
#include "lmem.h"
#include "lstring.h"
#include "ltable.h"
#include "ldo.h"
#include "lparser.h"
#include "lclass.h"
#include "lcodegen.h"

/* Android 调试日志 - 写入文件避免 logcat 截断 */

/* dummy reader for in-memory ZIO: always returns EOF */
static const char *astparser_zreader (lua_State *L, void *data, size_t *size) {
  (void)L; (void)data;
  *size = 0;
  return NULL;
}

#define LOGD(...) ((void)0)


/* 外部声明：$include 文件包含 */
extern void luaX_pushincludefile(LexState *ls, const char *filename);


/* 一元运算符优先级 */
#define UNARY_PRIORITY	12

/* 语句块初始容量 */
#define BLOCK_INIT_CAP 4


/* ---------- 静态辅助函数前向声明 ---------- */
static l_noret lp_error(ParserState *ps, const char *msg);
static int lp_check(ParserState *ps, int c);
static int lp_testnext(ParserState *ps, int c);
static void lp_checknext(ParserState *ps, int c);
static void lp_error_expected(ParserState *ps, int token);
static TString *lp_checkname(ParserState *ps);
static void lp_next(ParserState *ps);
static int is_nametoken(int token);
static ParseScope *scope_push(ParserState *ps, int is_loop);
static void scope_pop(ParserState *ps);
static int scope_find_local(ParserState *ps, TString *name);
static void scope_add_local(ParserState *ps, TString *name, int attr);
static void block_init(ParserState *ps, AstBlock *blk);
static int lp_softkw_is(ParserState *ps, const char *name);


/* ---------- 解析函数前向声明 ---------- */
static AstExpr *parse_expr(ParserState *ps);
static AstStmt *parse_stat(ParserState *ps);
static void parse_block(ParserState *ps, AstBlock *blk);
static AstExpr *parse_simpleexpr(ParserState *ps);
static AstExpr *parse_subexpr(ParserState *ps, int min_prec);
static AstExpr *parse_primary(ParserState *ps);
static AstExpr *parse_suffixedexpr(ParserState *ps, AstExpr *v);
static AstFunc *parse_funcbody(ParserState *ps, int line, int is_arrow, int need_self, int is_async);
static AstExpr **parse_exprlist(ParserState *ps, int *nret);
static AstExpr *parse_switch_expr(ParserState *ps);
static AstStmt *parse_switch_stat(ParserState *ps);
static AstStmt *parse_guard_stat(ParserState *ps);
static AstStmt *parse_try_stat(ParserState *ps);
static AstStmt *parse_defer_stat(ParserState *ps);
static AstStmt *parse_namespace_stat(ParserState *ps);
static AstStmt *parse_using_stat(ParserState *ps);
static AstStmt *parse_struct_stat(ParserState *ps);
static AstStmt *parse_superstruct_stat(ParserState *ps);
static AstStmt *parse_enum_stat(ParserState *ps);
static AstStmt *parse_class_stat(ParserState *ps, int class_flags);
static AstStmt *parse_trait_stat(ParserState *ps);
static AstStmt *parse_interface_stat(ParserState *ps);
static AstMatchPat *parse_match_pattern(ParserState *ps);
static AstStmt *parse_match_stat(ParserState *ps);
static AstStmt *parse_with_stat(ParserState *ps);
static AstStmt *parse_asm_stat(ParserState *ps);
static AstStmt *parse_concept_stat(ParserState *ps);
static AstStmt *parse_command_stat(ParserState *ps);
static AstStmt *parse_keyword_stat(ParserState *ps);
static AstStmt *parse_operator_stat(ParserState *ps);
static AstStmt *parse_global_stat(ParserState *ps);
static AstStmt *parse_constexpr_stat(ParserState *ps);
static AstStmt *lp_try_command_call(ParserState *ps);
static AstExpr *parse_test_or_map(ParserState *ps);
static AstExpr *parse_dict_comprehension(ParserState *ps);
static AstExpr *parse_list_comprehension(ParserState *ps);
static AstExpr *parse_test_value(ParserState *ps, int allow_or);
static AstExpr *parse_do_expr(ParserState *ps);
static AstExpr *parse_if_expr(ParserState *ps);
static int lp_is_generic_factory(ParserState *ps);
static AstStmt *lp_parse_generic_arrow_body(ParserState *ps, int is_async);
static void expr_to_target(ParserState *ps, AstExpr *e, AstAssignTarget *tgt);
static AstStmt *parse_while_let_stat(ParserState *ps, int line);


/* ============================================================
 *                    错误与Token工具函数
 * ============================================================ */

/**
 * @brief 报告语法错误并终止解析
 * @param ps 解析器状态
 * @param msg 错误消息
 */
static l_noret lp_error(ParserState *ps, const char *msg) {
  ps->nerr++;
  luaX_syntaxerror(ps->ls, msg);
}


/**
 * @brief 测试当前token是否为c，不消费
 * @param ps 解析器状态
 * @param c 期望的token
 * @return 1表示匹配，0表示不匹配
 */
static int lp_check(ParserState *ps, int c) {
  return (ps->ls->t.token == c);
}


/**
 * @brief 测试当前token并在匹配时消费
 * @param ps 解析器状态
 * @param c 期望的token
 * @return 1表示匹配并已消费，0表示不匹配
 */
static int lp_testnext(ParserState *ps, int c) {
  if (lp_check(ps, c)) {
    lp_next(ps);
    return 1;
  }
  return 0;
}


/**
 * @brief 断言当前token是c，否则报错
 * @param ps 解析器状态
 * @param c 期望的token
 */
static void lp_checknext(ParserState *ps, int c) {
  if (!lp_check(ps, c)) {
    lp_error_expected(ps, c);
  }
  lp_next(ps);
}


/**
 * @brief 报告期望某个token的语法错误
 * @param ps 解析器状态
 * @param token 期望的token类型
 */
static void lp_error_expected(ParserState *ps, int token) {
  const char *msg = luaO_pushfstring(ps->L, "'%s' expected",
                                     luaX_token2str(ps->ls, token));
  lp_error(ps, msg);
}


/**
 * @brief 判断token是否可作为名字使用（普通名字+类型名+软关键字）
 * @param token 当前token值
 * @return 1表示可作为名字，0表示不可
 */
static int is_nametoken(int token) {
  if (token == TK_NAME) return 1;
  /* 类型关键字可作为标识符 */
  if (token == TK_TYPE_INT || token == TK_TYPE_FLOAT || token == TK_DOUBLE ||
      token == TK_BOOL || token == TK_VOID || token == TK_CHAR ||
      token == TK_LONG) return 1;
  /* 软关键字 */
  if (token == TK_DELETE || token == TK_GUARD || token == TK_LET) return 1;
  return 0;
}


/**
 * @brief 消费当前可作名字的token并返回其TString
 * @param ps 解析器状态
 * @return 标识符的TString指针
 */
static TString *lp_checkname(ParserState *ps) {
  TString *ts;
  if (!is_nametoken(ps->ls->t.token)) {
    lp_error_expected(ps, TK_NAME);
  }
  ts = ps->ls->t.seminfo.ts;
  lp_next(ps);
  return ts;
}


/**
 * @brief 将关键字token转换为对应的TString（用于字段名上下文）
 * @param L Lua状态
 * @param token 关键字token值
 * @return 关键字的TString指针，如果不是关键字则返回NULL
 */
static TString *lp_keyword_ts(lua_State *L, int token) {
  switch (token) {
    case TK_AND: return luaS_newliteral(L, "and");
    case TK_ASM: return luaS_newliteral(L, "asm");
    case TK_ASTPARSER: return luaS_newliteral(L, "astparser");
    case TK_ASYNC: return luaS_newliteral(L, "async");
    case TK_AWAIT: return luaS_newliteral(L, "await");
    case TK_BREAK: return luaS_newliteral(L, "break");
    case TK_CASE: return luaS_newliteral(L, "case");
    case TK_CATCH: return luaS_newliteral(L, "catch");
    case TK_COMMAND: return luaS_newliteral(L, "command");
    case TK_CONST: return luaS_newliteral(L, "const");
    case TK_CONTINUE: return luaS_newliteral(L, "continue");
    case TK_DEFAULT: return luaS_newliteral(L, "default");
    case TK_DEFER: return luaS_newliteral(L, "defer");
    case TK_DELETE: return luaS_newliteral(L, "delete");
    case TK_GUARD: return luaS_newliteral(L, "guard");
    case TK_LET: return luaS_newliteral(L, "let");
    case TK_DO: return luaS_newliteral(L, "do");
    case TK_ELSE: return luaS_newliteral(L, "else");
    case TK_ELSEIF: return luaS_newliteral(L, "elseif");
    case TK_END: return luaS_newliteral(L, "end");
    case TK_ENUM: return luaS_newliteral(L, "enum");
    case TK_FALSE: return luaS_newliteral(L, "false");
    case TK_FINALLY: return luaS_newliteral(L, "finally");
    case TK_FOR: return luaS_newliteral(L, "for");
    case TK_FUNCTION: return luaS_newliteral(L, "function");
    case TK_GOTO: return luaS_newliteral(L, "goto");
    case TK_IF: return luaS_newliteral(L, "if");
    case TK_IN: return luaS_newliteral(L, "in");
    case TK_LOCAL: return luaS_newliteral(L, "local");
    case TK_NIL: return luaS_newliteral(L, "nil");
    case TK_NOT: return luaS_newliteral(L, "not");
    case TK_OR: return luaS_newliteral(L, "or");
    case TK_REPEAT: return luaS_newliteral(L, "repeat");
    case TK_RETURN: return luaS_newliteral(L, "return");
    case TK_SWITCH: return luaS_newliteral(L, "switch");
    case TK_THEN: return luaS_newliteral(L, "then");
    case TK_TRUE: return luaS_newliteral(L, "true");
    case TK_UNTIL: return luaS_newliteral(L, "until");
    case TK_WHILE: return luaS_newliteral(L, "while");
    case TK_BOOL: return luaS_newliteral(L, "bool");
    case TK_CHAR: return luaS_newliteral(L, "char");
    case TK_DOUBLE: return luaS_newliteral(L, "double");
    case TK_TYPE_FLOAT: return luaS_newliteral(L, "float");
    case TK_TYPE_INT: return luaS_newliteral(L, "int");
    case TK_LONG: return luaS_newliteral(L, "long");
    case TK_VOID: return luaS_newliteral(L, "void");
    case TK_EXPORT: return luaS_newliteral(L, "export");
    case TK_CONCEPT: return luaS_newliteral(L, "concept");
    case TK_GLOBAL: return luaS_newliteral(L, "global");
    case TK_IS: return luaS_newliteral(L, "is");
    case TK_INSTANCEOF: return luaS_newliteral(L, "instanceof");
    case TK_LAMBDA: return luaS_newliteral(L, "lambda");
    case TK_STRUCT: return luaS_newliteral(L, "struct");
    case TK_TAKE: return luaS_newliteral(L, "take");
    case TK_TRY: return luaS_newliteral(L, "try");
    case TK_USING: return luaS_newliteral(L, "using");
    case TK_WHEN: return luaS_newliteral(L, "when");
    case TK_WITH: return luaS_newliteral(L, "with");
    case TK_KEYWORD: return luaS_newliteral(L, "keyword");
    case TK_OPERATOR: return luaS_newliteral(L, "operator");
    default: return NULL;
  }
}


/**
 * @brief 消费当前可作字段名的token并返回其TString
 * @param ps 解析器状态
 * @return 字段名的TString指针
 * @details 用于 '.'、':'、'::' 之后的字段名解析，允许关键字作为字段名
 */
static TString *lp_checkfieldname(ParserState *ps) {
  LexState *ls = ps->ls;
  TString *ts;
  if (ls->t.token == TK_NAME) {
    ts = ls->t.seminfo.ts;
  } else {
    ts = lp_keyword_ts(ls->L, ls->t.token);
    if (ts == NULL) {
      lp_error_expected(ps, TK_NAME);
    }
  }
  lp_next(ps);
  return ts;
}


/**
 * @brief 消费当前token，前进到下一个token
 * @param ps 解析器状态
 */
static void lp_next(ParserState *ps) {
  luaX_next(ps->ls);
}


/**
 * @brief 查看下一个token但不消费
 * @param ps 解析器状态
 * @return 下一个token的类型
 */
static int lp_lookahead(ParserState *ps) {
  return luaX_lookahead(ps->ls);
}


/**
 * @brief 查看第二个 lookahead token
 * @param ps 解析器状态
 * @return 第二个 lookahead token 的类型
 */
static int lp_lookahead2(ParserState *ps) {
  return luaX_lookahead2(ps->ls);
}


/**
 * @brief 检查 token 是否可以作为中缀表达式的起始
 * @param token token 类型
 * @return 1 如果可以，0 否则
 */
static int is_expr_start_token(int token) {
  switch (token) {
    case TK_INT: case TK_FLT: case TK_NAME:
    case '(': case TK_STRING: case TK_RAWSTRING: case TK_INTERPSTRING:
    case TK_TRUE: case TK_FALSE: case TK_NIL:
    case '{': case TK_NOT: case '-': case '#': case '~':
    case TK_FUNCTION: case TK_LAMBDA: case TK_IF:
    case TK_AWAIT: case TK_DOTS: case TK_REGEX:
      return 1;
    default:
      return 0;
  }
}


/**
 * @brief 检查 token 是否可以作为命令参数起始
 * @param token 要检查的 token
 * @return 1 如果可以，0 否则
 */
static int is_cmd_arg_start(int token) {
  switch (token) {
    case TK_STRING:
    case TK_INTERPSTRING:
    case TK_RAWSTRING:
    case TK_INT:
    case TK_FLT:
    case TK_NAME:
    case TK_TRUE:
    case TK_FALSE:
    case TK_NIL:
    case '{':
    case '(':
    case '-':  /* 可能是负数或操作符 */
      return 1;
    default:
      return 0;
  }
}


/**
 * @brief 检查 token 是否是语句结束符
 * @param token 要检查的 token
 * @return 1 如果是语句结束符，0 否则
 */
static int is_stmt_terminator(int token) {
  switch (token) {
    case ';':
    case TK_EOS:
    case TK_END:
    case TK_THEN:
    case TK_ELSE:
    case TK_ELSEIF:
    case TK_UNTIL:
    case TK_DO:
    case TK_RETURN:
    case TK_BREAK:
    case TK_CONTINUE:
      return 1;
    default:
      return 0;
  }
}


/**
 * @brief Shell 风格命令调用检测和解析
 * 语法: 命令名 参数1 参数2 ...
 * 等价于: 命令名(参数1, 参数2, ...)
 * 参考 lparser.c 第 12593-12672 行
 * @param ps 解析器状态
 * @return 成功返回 AST 语句节点，否则返回 NULL
 */
static AstStmt *lp_try_command_call(ParserState *ps) {
  LexState *ls = ps->ls;
  int line = ls->linenumber;

  /* 检查是否是 TK_NAME */
  if (ls->t.token != TK_NAME)
    return NULL;

  /* 检查是否是软关键字（new, super, class, interface, abstract, final, sealed） */
  /* new 和 super 是表达式关键字 */
  if (lp_softkw_is(ps, "new") || lp_softkw_is(ps, "super"))
    return NULL;
  if (lp_softkw_is(ps, "class") || lp_softkw_is(ps, "interface") ||
      lp_softkw_is(ps, "abstract") || lp_softkw_is(ps, "final") ||
      lp_softkw_is(ps, "sealed") || lp_softkw_is(ps, "match") ||
      lp_softkw_is(ps, "trait") || lp_softkw_is(ps, "require"))
    return NULL;

  /* 预读下一个 token */
  int lookahead = lp_lookahead(ps);

  /* 如果是普通函数调用/方法调用/字段访问/赋值，不处理 */
  if (lookahead == '(' || lookahead == ':' || lookahead == '.' ||
      lookahead == '=' || lookahead == ',' || lookahead == '[' ||
      lookahead == TK_PLUSPLUS)
    return NULL;

  /* 如果看起来像中缀调用 (Name Name <expr_start>)，不处理 */
  if (lookahead == TK_NAME) {
    int la2 = lp_lookahead2(ps);
    if (is_expr_start_token(la2))
      return NULL;
  }

  /* 如果下一个 token 不能作为命令参数开始，不处理 */
  if (!is_cmd_arg_start(lookahead))
    return NULL;

  /* 字符串或表作为第一个参数时，让 Lua 原生单参数调用语法处理 */
  if (lookahead == TK_STRING || lookahead == TK_INTERPSTRING ||
      lookahead == TK_RAWSTRING || lookahead == '{')
    return NULL;

  /* 解析命令调用 */
  TString *cmdname = ls->t.seminfo.ts;
  lp_next(ps); /* 跳过命令名 */

  /* 解析参数列表（只在同一行内解析，换行即停止） */
  int nargs = 0;
  int cap = 4;
  AstExpr **args = cast(AstExpr **, ast_pool_alloc(ps->pool, cap * sizeof(AstExpr *)));

  while (!is_stmt_terminator(ls->t.token) && ls->t.token != TK_EOS && ls->linenumber == line) {
    if (is_stmt_terminator(ls->t.token))
      break;

    /* 扩容 */
    if (nargs >= cap) {
      int new_cap = cap * 2;
      AstExpr **new_args = cast(AstExpr **,
        ast_pool_alloc(ps->pool, new_cap * sizeof(AstExpr *)));
      memcpy(new_args, args, nargs * sizeof(AstExpr *));
      args = new_args;
      cap = new_cap;
    }

    /* 处理特殊的操作符参数（如 -f, -r 等） */
    if (ls->t.token == '-') {
      int next = lp_lookahead(ps);
      if (next == TK_NAME) {
        lp_next(ps); /* skip '-' */
        TString *op_name = ls->t.seminfo.ts;
        const char *name = getstr(op_name);
        size_t len = strlen(name);
        char *buf = cast(char *, ast_pool_alloc(ps->pool, len + 2));
        buf[0] = '-';
        memcpy(buf + 1, name, len);
        buf[len + 1] = '\0';
        TString *op_str = luaS_new(ps->L, buf);
        args[nargs++] = ast_new_expr_str(ps->pool, op_str, AST_EXPR_STRING, line);
        lp_next(ps); /* skip 操作符名 */
        continue;
      }
      /* 负号后面是数字 */
      if (next == TK_INT || next == TK_FLT) {
        AstExpr *e = parse_expr(ps);
        args[nargs++] = e;
        continue;
      }
    }

    /* 解析普通参数 */
    {
      AstExpr *e = parse_simpleexpr(ps);
      args[nargs++] = e;
    }
  }

  /* 创建函数调用 AST 节点 */
  AstExpr *func_expr = ast_new_expr_ident(ps->pool, cmdname, line);
  AstExpr *call_expr = ast_new_expr_call(ps->pool, func_expr, args, nargs, line);
  return ast_new_stmt_expr(ps->pool, call_expr, line);
}


/* ============================================================
 *                    类型注解解析（AST 版）
 * ============================================================ */

/**
 * @brief 分配并初始化 TypeHint 结构体
 * 使用 AST 内存池分配，无需手动释放
 * @param ps 解析器状态
 * @return 初始化好的 TypeHint 指针
 */
static TypeHint *lp_typehint_new(ParserState *ps) {
  TypeHint *th = cast(TypeHint *, ast_pool_alloc(ps->pool, sizeof(TypeHint)));
  int i;
  for (i = 0; i < MAX_TYPE_DESCS; i++) {
    th->descs[i].type = LVT_NONE;
    th->descs[i].nparam = -1;
    th->descs[i].nret = -1;
    th->descs[i].proto = NULL;
    th->descs[i].nfields = -1;
  }
  th->next = NULL;
  return th;
}


/**
 * @brief 向 TypeHint 中放置一个 TypeDesc（去重）
 * @param th 目标 TypeHint
 * @param td 要放置的 TypeDesc
 */
static void lp_th_emplace_desc(TypeHint *th, TypeDesc td) {
  int i;
  for (i = 0; i < MAX_TYPE_DESCS; i++) {
    if (th->descs[i].type == td.type) return; /* 已存在 */
    if (th->descs[i].type == LVT_NONE) {
      th->descs[i] = td;
      return;
    }
  }
  /* 槽满，退化为 ANY */
  th->descs[0].type = LVT_ANY;
  th->descs[1].type = LVT_NONE;
  th->descs[2].type = LVT_NONE;
}


/**
 * @brief 从命名类型注册表查找类型定义
 * 与 lparser.c 的 get_named_type_opt() 对应
 * @param ps 解析器状态
 * @param name 类型名
 * @return 找到的 TypeHint 指针，未找到返回 NULL
 */
static TypeHint *lp_get_named_type_opt(ParserState *ps, const TString *name) {
  const TValue *o = luaH_getstr(ps->ls->named_types, (TString *)name);
  if (!ttisnil(o)) {
    return (TypeHint *)pvalue(o);
  }
  return NULL;
}


/**
 * @brief 解析类型注解表达式
 * 与 lparser.c 的 checktypehint() 对应，支持命名类型解析。
 *
 * 类型语法: '?'? base_type ('|' base_type)* '?'?
 * base_type ::= name | '{' field ':' type '}' | 'function' '(' params ')' (':' type)?
 *
 * @param ps 解析器状态
 * @param th 要填充的 TypeHint
 */
static void lp_checktypehint(ParserState *ps, TypeHint *th) {
  LexState *ls = ps->ls;

  /* 可选 '?' 前缀（nullable） */
  if (lp_testnext(ps, '?')) {
    TypeDesc td; td.type = LVT_NULL;
    lp_th_emplace_desc(th, td);
  }

  do {
    if (ls->t.token == '{') {
      /* 表类型: { field: type, ... } */
      lp_next(ps);
      {
        TypeDesc td;
        td.type = LVT_TABLE;
        td.nfields = 0;
        while (ls->t.token != '}') {
          TString *ts = lp_checkname(ps);
          lp_checknext(ps, ':');
          {
            TypeHint *fieldth = lp_typehint_new(ps);
            lp_checktypehint(ps, fieldth);
            if (td.nfields < MAX_TYPED_FIELDS) {
              td.names[td.nfields] = ts;
              td.hints[td.nfields] = fieldth;
              td.nfields++;
            }
          }
          if (!lp_testnext(ps, ',') && !lp_testnext(ps, ';')) break;
        }
        lp_checknext(ps, '}');
        lp_th_emplace_desc(th, td);
      }
      continue;
    }

    /* 解析基本类型名 */
    const char *tname;
    TString *ts = NULL;
    if (lp_testnext(ps, TK_FUNCTION)) {
      tname = "function";
      /* 函数类型: function(params): rettype */
      {
        TypeDesc td;
        td.type = LVT_FUNC;
        td.nparam = -1;
        td.nret = -1;
        lp_checknext(ps, '(');
        td.nparam = 0;
        if (ls->t.token != ')') {
          do {
            /* 参数可能是 name: type 或仅为 type */
            if (is_nametoken(ls->t.token)) {
              lp_next(ps);
              if (lp_testnext(ps, ':')) {
                /* 跳过参数类型 */
                TypeHint *ign = lp_typehint_new(ps);
                lp_checktypehint(ps, ign);
              }
            }
            if (td.nparam < MAX_TYPED_PARAMS) {
              td.params[td.nparam] = lp_typehint_new(ps);
              lp_checktypehint(ps, td.params[td.nparam]);
              td.nparam++;
            } else {
              TypeHint *ign = lp_typehint_new(ps);
              lp_checktypehint(ps, ign);
            }
          } while (lp_testnext(ps, ','));
        }
        lp_checknext(ps, ')');
        /* 返回类型 */
        if (lp_testnext(ps, ':')) {
          td.nret = 0;
          if (lp_testnext(ps, '(')) {
            do {
              if (td.nret < MAX_TYPED_RETURNS) {
                td.returns[td.nret] = lp_typehint_new(ps);
                lp_checktypehint(ps, td.returns[td.nret]);
                td.nret++;
              } else {
                TypeHint *ign = lp_typehint_new(ps);
                lp_checktypehint(ps, ign);
              }
            } while (lp_testnext(ps, ','));
            lp_checknext(ps, ')');
          } else {
            if (is_nametoken(ls->t.token) &&
                strcmp(getstr(ls->t.seminfo.ts), "void") == 0) {
              lp_next(ps);
              td.nret = 0;
            } else {
              td.nret = 1;
              td.returns[0] = lp_typehint_new(ps);
              lp_checktypehint(ps, td.returns[0]);
            }
          }
        }
        lp_th_emplace_desc(th, td);
      }
      continue;
    }

    ts = lp_checkname(ps);
    tname = getstr(ts);

    {
      TypeDesc td;
      td.type = LVT_NONE;

      if (strcmp(tname, "number") == 0) td.type = LVT_NUMBER;
      else if (strcmp(tname, "int") == 0 || strcmp(tname, "integer") == 0) td.type = LVT_INT;
      else if (strcmp(tname, "float") == 0) td.type = LVT_FLT;
      else if (strcmp(tname, "table") == 0) td.type = LVT_TABLE;
      else if (strcmp(tname, "string") == 0) td.type = LVT_STR;
      else if (strcmp(tname, "boolean") == 0 || strcmp(tname, "bool") == 0) td.type = LVT_BOOL;
      else if (strcmp(tname, "any") == 0) td.type = LVT_ANY;
      else if (strcmp(tname, "nil") == 0) td.type = LVT_NIL;
      else if (strcmp(tname, "void") == 0) td.type = LVT_NULL;
      else if (strcmp(tname, "userdata") == 0) td.type = LVT_USERDATA;
      else {
        /* 查找命名类型注册表 */
        TypeHint *named = lp_get_named_type_opt(ps, ts);
        if (named) {
          /* 合并命名类型的 TypeDesc 到当前 TypeHint */
          int i;
          for (i = 0; i < MAX_TYPE_DESCS; i++) {
            if (named->descs[i].type != LVT_NONE)
              lp_th_emplace_desc(th, named->descs[i]);
          }
          td.type = LVT_NONE; /* 已处理 */
        } else {
          /* 未注册的自定义类型名 */
          td.type = LVT_NAME;
          td.typename = ts;
        }
      }

      if (td.type != LVT_NONE) lp_th_emplace_desc(th, td);
    }
  } while (lp_testnext(ps, '|'));

  /* 可选的尾随 '?'（nullable） */
  if (lp_testnext(ps, '?')) {
    TypeDesc td; td.type = LVT_NULL;
    lp_th_emplace_desc(th, td);
  }
}


/**
 * @brief 从当前 token 获取类型注解
 * 如果当前 token 是 ':'，则解析后续类型注解；否则返回 NULL
 * @param ps 解析器状态
 * @return TypeHint 指针，无类型注解则为 NULL
 */
static TypeHint *lp_gettypehint(ParserState *ps) {
  if (lp_testnext(ps, ':')) {
    TypeHint *th = lp_typehint_new(ps);
    lp_checktypehint(ps, th);
    return th;
  }
  return NULL;
}

/* ============================================================
 *                       作用域管理
 * ============================================================ */

/**
 * @brief 初始化语句块（如果尚未初始化）
 * @param ps 解析器状态
 * @param blk 语句块指针
 */
static void block_init(ParserState *ps, AstBlock *blk) {
  if (blk->items == NULL) {
    blk->count = 0;
    blk->capacity = BLOCK_INIT_CAP;
    blk->items = cast(AstStmt **,
      ast_pool_alloc(ps->pool, sizeof(AstStmt *) * BLOCK_INIT_CAP));
  } else {
    blk->count = 0;
  }
}


/**
 * @brief 压入一个新的作用域
 * @param ps 解析器状态
 * @param is_loop 是否为循环作用域
 * @return 新创建的作用域指针
 */
static ParseScope *scope_push(ParserState *ps, int is_loop) {
  ParseScope *sc = cast(ParseScope *,
                        ast_pool_alloc(ps->pool, sizeof(ParseScope)));
  sc->prev = ps->scope;
  sc->func = ps->curfunc;
  sc->nlocals = (ps->scope != NULL) ? ps->scope->nlocals : 0;
  sc->firstlocal = ps->curfunc->nlocals;
  sc->is_loop = is_loop;
  sc->local_names = NULL;
  sc->nnames = 0;
  sc->names_cap = 0;
  ps->scope = sc;
  return sc;
}


/**
 * @brief 弹出当前作用域
 * @param ps 解析器状态
 */
static void scope_pop(ParserState *ps) {
  ParseScope *sc = ps->scope;
  lua_assert(sc != NULL);
  ps->curfunc->nlocals = sc->firstlocal;
  ps->scope = sc->prev;
}


/**
 * @brief 在当前作用域链中查找局部变量
 * @param ps 解析器状态
 * @param name 变量名
 * @return 局部变量索引（从0开始），-1表示未找到
 */
static int scope_find_local(ParserState *ps, TString *name) {
  ParseScope *sc;
  for (sc = ps->scope; sc != NULL; sc = sc->prev) {
    int idx;
    for (idx = sc->nnames - 1; idx >= 0; idx--) {
      if (sc->local_names[idx] == name) {
        return sc->firstlocal + idx;
      }
    }
  }
  return -1;
}


/**
 * @brief 向当前作用域添加一个局部变量
 * @param ps 解析器状态
 * @param name 变量名TString
 * @param attr 变量属性（AST_ATTR_*）
 */
static void scope_add_local(ParserState *ps, TString *name, int attr) {
  ParseScope *sc = ps->scope;
  AstFunc *f = ps->curfunc;
  (void)attr;

  if (f->nlocals >= MAXVARS_LP) {
    lp_error(ps, "too many local variables");
  }

  if (sc->nnames >= sc->names_cap) {
    int new_cap = sc->names_cap ? sc->names_cap * 2 : SCOPE_NAMES_INIT;
    TString **new_names = cast(TString **,
      ast_pool_alloc(ps->pool, new_cap * sizeof(TString *)));
    if (sc->local_names != NULL && sc->nnames > 0) {
      memcpy(new_names, sc->local_names, sc->nnames * sizeof(TString *));
    }
    sc->local_names = new_names;
    sc->names_cap = new_cap;
  }

  sc->local_names[sc->nnames++] = name;
  sc->nlocals++;
  f->nlocals++;
}


/* ============================================================
 *                    运算符映射与优先级
 * ============================================================ */

/**
 * @brief 从token获取一元运算符类型
 * @param op token值
 * @return AstUnOp枚举值，非一元运算符返回-1
 */
static int get_unop(int op) {
  switch (op) {
    case TK_NOT: return AST_UN_NOT;
    case '-':    return AST_UN_MINUS;
    case '~':    return AST_UN_BNOT;
    case '#':    return AST_UN_LEN;
    case TK_AWAIT: return AST_UN_AWAIT;
    default:     return -1;
  }
}


/**
 * @brief 从token获取二元运算符类型
 * @param op token值
 * @return AstBinOp枚举值，非二元运算符返回-1
 */
static int get_binop(int op) {
  switch (op) {
    case '+': return AST_BIN_ADD;
    case '-': return AST_BIN_SUB;
    case '*': return AST_BIN_MUL;
    case '%': return AST_BIN_MOD;
    case '^': return AST_BIN_POW;
    case '/': return AST_BIN_DIV;
    case TK_IDIV: return AST_BIN_IDIV;
    case '&': return AST_BIN_BAND;
    case '|': return AST_BIN_BOR;
    case '~': return AST_BIN_BXOR;
    case TK_SHL: return AST_BIN_SHL;
    case TK_SHR: return AST_BIN_SHR;
    case TK_CONCAT: return AST_BIN_CONCAT;
    case TK_PIPE: return AST_BIN_PIPE;
    case TK_REVPIPE: return AST_BIN_REVPIPE;
    case TK_SAFEPIPE: return AST_BIN_SAFEPIPE;
    case TK_NE: return AST_BIN_NE;
    case TK_EQ: return AST_BIN_EQ;
    case '<': return AST_BIN_LT;
    case TK_LE: return AST_BIN_LE;
    case '>': return AST_BIN_GT;
    case TK_GE: return AST_BIN_GE;
    case TK_SPACESHIP: return AST_BIN_SPACESHIP;
    case TK_IS: return AST_BIN_IS;
    case TK_INSTANCEOF: return AST_BIN_IS;
    case TK_IN: return AST_BIN_IN;
    case TK_AND: return AST_BIN_AND;
    case TK_OR: return AST_BIN_OR;
    case TK_NULLCOAL: return AST_BIN_NULLCOAL;
    case TK_MERGE: return AST_BIN_MERGE;
    default: return -1;
  }
}


/**
 * @brief 二元运算符优先级表（左结合/右结合优先级）
 * 顺序与last.h中AstBinOp枚举顺序一致
 */
static const struct {
  lu_byte left;
  lu_byte right;
} binop_priority[] = {
  {10, 10},  /* AST_BIN_ADD '+' */
  {10, 10},  /* AST_BIN_SUB '-' */
  {11, 11},  /* AST_BIN_MUL '*' */
  {11, 11},  /* AST_BIN_DIV '/' */
  {11, 11},  /* AST_BIN_IDIV '//' */
  {11, 11},  /* AST_BIN_MOD '%' */
  {14, 13},  /* AST_BIN_POW '^' (right assoc) */
  {6, 6},    /* AST_BIN_BAND '&' */
  {4, 4},    /* AST_BIN_BOR '|' */
  {5, 5},    /* AST_BIN_BXOR '~' */
  {7, 7},    /* AST_BIN_SHL '<<' */
  {7, 7},    /* AST_BIN_SHR '>>' */
  {9, 8},    /* AST_BIN_CONCAT '..' (right assoc) */
  {8, 8},    /* AST_BIN_PIPE '|>' (left assoc) */
  {8, 8},    /* AST_BIN_REVPIPE '<|' (left assoc) */
  {8, 8},    /* AST_BIN_SAFEPIPE '?>' (left assoc) */
  {3, 3},    /* AST_BIN_EQ '==' */
  {3, 3},    /* AST_BIN_NE '~=' */
  {3, 3},    /* AST_BIN_LT '<' */
  {3, 3},    /* AST_BIN_LE '<=' */
  {3, 3},    /* AST_BIN_GT '>' */
  {3, 3},    /* AST_BIN_GE '>=' */
  {3, 3},    /* AST_BIN_SPACESHIP '<=>' */
  {3, 3},    /* AST_BIN_IS 'is' */
  {3, 3},    /* AST_BIN_IN 'in' */
  {2, 2},    /* AST_BIN_AND 'and' */
  {1, 1},    /* AST_BIN_OR 'or' */
  {1, 1},    /* AST_BIN_NULLCOAL '??' (right assoc) */
  {1, 1},    /* AST_BIN_CASE '=>' */
  {5, 5},    /* AST_BIN_INFIX (infix call) */
  {5, 5}     /* AST_BIN_MERGE '<>' */
};


/* ============================================================
 *                       表达式解析
 * ============================================================ */

/**
 * @brief 解析表达式列表（逗号分隔）
 * @param ps 解析器状态
 * @param nret [out] 返回表达式数量
 * @return 表达式数组（从内存池分配）
 */
static AstExpr **parse_exprlist(ParserState *ps, int *nret) {
  int cap = 4;
  int n = 0;
  AstExpr **exprs = cast(AstExpr **,
    ast_pool_alloc(ps->pool, cap * sizeof(AstExpr *)));

  exprs[n++] = parse_expr(ps);
  LOGD("[parse] EXPRLIST: expr[%d] parsed, next_token=%d\n", n - 1, ps->ls->t.token);
  while (lp_testnext(ps, ',')) {
    if (n >= cap) {
      int new_cap = cap * 2;
      AstExpr **new_exprs = cast(AstExpr **,
        ast_pool_alloc(ps->pool, new_cap * sizeof(AstExpr *)));
      memcpy(new_exprs, exprs, n * sizeof(AstExpr *));
      exprs = new_exprs;
      cap = new_cap;
    }
    exprs[n++] = parse_expr(ps);
  }
  *nret = n;
  return exprs;
}


/**
 * @brief 解析原子表达式（primary）
 * @param ps 解析器状态
 * @return 解析得到的表达式节点
 */
static AstExpr *parse_primary(ParserState *ps) {
  LexState *ls = ps->ls;
  int line = ls->linenumber;
  AstExpr *e;

  switch (ls->t.token) {
    case TK_NIL: {
      lp_next(ps);
      return ast_new_expr_nil(ps->pool, line);
    }
    case TK_TRUE: {
      lp_next(ps);
      return ast_new_expr_bool(ps->pool, 1, line);
    }
    case TK_FALSE: {
      lp_next(ps);
      return ast_new_expr_bool(ps->pool, 0, line);
    }
    case TK_INT: {
      lua_Integer i = ls->t.seminfo.i;
      lp_next(ps);
      return ast_new_expr_int(ps->pool, i, line);
    }
    case TK_FLT: {
      lua_Number r = ls->t.seminfo.r;
      lp_next(ps);
      return ast_new_expr_flt(ps->pool, r, line);
    }
    case TK_STRING:
    case TK_RAWSTRING: {
      TString *ts = ls->t.seminfo.ts;
      lp_next(ps);
      e = ast_new_expr_str(ps->pool, ts, AST_EXPR_STRING, line);
      break;
    }
    case TK_INTERPSTRING: {
      TString *ts = ls->t.seminfo.ts;
      lp_next(ps);
      e = ast_new_expr_str(ps->pool, ts, AST_EXPR_INTERPSTRING, line);
      break;
    }
    case TK_DOTS: {  /* vararg 或 spread 展开运算符 */
      int dots_line = ls->linenumber;
      int la = lp_lookahead(ps);
      /* 展开运算符：同行且下一个 token 是表达式起始 */
      if ((la == TK_NAME || la == '(' || la == '{' || la == TK_STRING ||
           la == TK_RAWSTRING || la == TK_INTERPSTRING || la == TK_INT ||
           la == TK_FLT || la == TK_TRUE || la == TK_FALSE || la == TK_NIL ||
           la == '-' || la == TK_NOT || la == '#' || la == '~' ||
           la == TK_FUNCTION || la == TK_LAMBDA) &&
          ls->linenumber == dots_line) {
        lp_next(ps);  /* 跳过 '...' */
        AstExpr *spread_expr = parse_expr(ps);
        e = ast_new_expr_spread(ps->pool, spread_expr, line);
      } else {
        lp_next(ps);
        e = ast_new_expr_vararg(ps->pool, line);
      }
      break;
    }
    case TK_REGEX: {
      TString *ts = ls->t.seminfo.ts;
      lp_next(ps);
      e = ast_new_expr_str(ps->pool, ts, AST_EXPR_REGEX, line);
      break;
    }
    case TK_NAME:
    case TK_TYPE_INT:
    case TK_TYPE_FLOAT:
    case TK_DOUBLE:
    case TK_BOOL:
    case TK_VOID:
    case TK_CHAR:
    case TK_LONG:
    case TK_DELETE:
    case TK_GUARD:
    case TK_LET: {
      TString *name = ls->t.seminfo.ts;
      /* 软关键字检查：new ClassName(args) */
      if (ls->t.token == TK_NAME && lp_softkw_is(ps, "new")) {
        int line = ls->linenumber;
        lp_next(ps);  /* skip 'new' */
        /* 解析类名（suffixedexpr） */
        AstExpr *class_expr = parse_suffixedexpr(ps, parse_primary(ps));
        /* 解析参数列表 */
        int nargs = 0;
        AstExpr **args = NULL;
        if (lp_testnext(ps, '(')) {
          if (!lp_check(ps, ')')) {
            args = parse_exprlist(ps, &nargs);
          }
          lp_checknext(ps, ')');
        }
        e = ast_new_expr_new(ps->pool, class_expr, args, nargs, line);
        break;
      }
      /* 软关键字检查：match expr { ... } (表达式模式) */
      if (ls->t.token == TK_NAME && lp_softkw_is(ps, "match")) {
        int line = ls->linenumber;
        AstStmt *stmt = parse_match_stat(ps);
        e = ast_new_expr_match(ps->pool, stmt, line);
        break;
      }
      /* 软关键字检查：super 表达式 */
      if (ls->t.token == TK_NAME && lp_softkw_is(ps, "super")) {
        int line = ls->linenumber;
        lp_next(ps);  /* 跳过 super */
        e = ast_new_expr_super(ps->pool, line);
        e = parse_suffixedexpr(ps, e);
        break;
      }
      lp_next(ps);
      e = ast_new_expr_ident(ps->pool, name, line);
      break;
    }
    case TK_FUNCTION: {
      int fline = ls->linenumber;
      lp_next(ps);
      {
        AstFunc *f = parse_funcbody(ps, fline, 0, 0, 0);
        e = ast_new_expr_func(ps->pool, f, 0, fline);
      }
      break;
    }
    case '{': {
      int tline = ls->linenumber;
      LOGD("[parse] PARSE_EXPR '{': line=%d, lookahead=%d, lookahead2=%d\n",
           tline, luaX_lookahead(ls), luaX_lookahead2(ls));
      /* 检测字典推导式: {for k,v in expr do/yield k_expr, v_expr if cond} */
      if (lp_lookahead(ps) == TK_FOR) {
        e = parse_dict_comprehension(ps);
        break;
      }
      int cap = 4;
      int n = 0;
      AstTableEntry *entries = cast(AstTableEntry *,
        ast_pool_alloc(ps->pool, cap * sizeof(AstTableEntry)));
      lp_next(ps);
      LOGD("[parse] TABLE_CTOR start: line=%d, first_token=%d\n", tline, ls->t.token);
      while (!lp_check(ps, '}') && !lp_check(ps, TK_EOS)) {
        AstTableEntry *entry;
        if (n >= cap) {
          int new_cap = cap * 2;
          AstTableEntry *new_entries = cast(AstTableEntry *,
            ast_pool_alloc(ps->pool, new_cap * sizeof(AstTableEntry)));
          memcpy(new_entries, entries, n * sizeof(AstTableEntry));
          entries = new_entries;
          cap = new_cap;
        }
        entry = &entries[n++];
        memset(entry, 0, sizeof(*entry));
        {
          int cur_token = ls->t.token;
          int la = luaX_lookahead(ls);
          LOGD("[parse] TABLE_ENTRY #%d: cur_token=%d(TK_NAME=%d), lookahead=%d('%c'), is_name=%d\n",
               n, cur_token, TK_NAME, la, (la >= 32 && la < 127) ? la : '?', cur_token == TK_NAME);
        }
        if (lp_check(ps, TK_NAME) &&
            (luaX_lookahead(ls) == '=' || luaX_lookahead(ls) == ':')) {
          entry->kind = AST_TENTRY_KEY;
          LOGD("[parse] TABLE_ENTRY #%d: -> KEY (name='%s', sep='%c')\n",
               n, getstr(ls->t.seminfo.ts), luaX_lookahead(ls));
          entry->key = ast_new_expr_str(ps->pool, ls->t.seminfo.ts,
                                        AST_EXPR_STRING, ls->linenumber);
          lp_next(ps);
          if (luaX_lookahead(ls) == ':') {
            lp_next(ps);  /* skip ':' */
          } else {
            lp_next(ps);  /* skip '=' */
          }
          entry->value = parse_expr(ps);
        } else if (lp_check(ps, '[') ) {
          entry->kind = AST_TENTRY_KEY;
          LOGD("[parse] TABLE_ENTRY #%d: -> KEY_BRACKET\n", n);
          lp_next(ps);
          entry->key = parse_expr(ps);
          lp_checknext(ps, ']');
          lp_checknext(ps, '=');
          entry->value = parse_expr(ps);
        } else if (lp_check(ps, TK_FUNCTION)) {
          /* method shorthand: function method(...) end 作为 recfield */
          int f_line = ls->linenumber;
          lp_next(ps); /* skip 'function' */
          if (is_nametoken(ls->t.token)) {
            entry->kind = AST_TENTRY_KEY;
            entry->key = ast_new_expr_str(ps->pool, ls->t.seminfo.ts,
                                          AST_EXPR_STRING, ls->linenumber);
            lp_next(ps); /* skip name */
            AstFunc *f = parse_funcbody(ps, f_line, 0, 1, 0);  /* need_self=1: 隐式self参数 */
            entry->value = ast_new_expr_func(ps->pool, f, 0, f_line);
          } else {
            /* 匿名函数作为数组元素 */
            entry->kind = AST_TENTRY_POS;
            AstFunc *f = parse_funcbody(ps, f_line, 0, 0, 0);
            entry->value = ast_new_expr_func(ps->pool, f, 0, f_line);
          }
        } else {
          entry->kind = AST_TENTRY_POS;
          LOGD("[parse] TABLE_ENTRY #%d: -> POS\n", n);
          entry->value = parse_expr(ps);
        }
        if (!lp_testnext(ps, ',') && !lp_testnext(ps, ';'))
          break;
      }
      lp_checknext(ps, '}');
      LOGD("[parse] TABLE_CTOR end: total_entries=%d\n", n);
      e = ast_new_expr_table(ps->pool, entries, n, tline);
      break;
    }
    case '@': {  /* || -> 无参lambda表达式 */
      int fline = ls->linenumber;
      int func_idx = ps->func_idx_counter++;
      int parent_idx = ps->curfunc ? ps->curfunc->func_idx : -1;
      AstFunc *f;
      AstStmt *ret;
      AstExpr *body_expr;

      lp_next(ps);  /* 跳过 '@' (即 ||) */

      /* 期望 -> */
      if (!lp_testnext(ps, TK_ARROW))
        lp_error(ps, "expected '->' after '||' in lambda expression");

      /* 创建函数（ast_new_expr_func 内部会调用 ast_chunk_add_func） */
      f = ast_new_func(ps->pool, func_idx, parent_idx, fline);
      f->source = ps->ls->source;
      f->nparams = 0;
      f->nlocals = 0;
      f->is_vararg = 0;

      /* 解析表达式体，包装为return语句 */
      body_expr = parse_expr(ps);
      ret = ast_new_stmt_return(ps->pool, 1, fline);
      ret->u.retstmt.values[0] = body_expr;
      ast_block_add_stmt(ps->pool, &f->body, ret);

      e = ast_new_expr_func(ps->pool, f, 1, fline);
      break;
    }
    case '|': {  /* |params| -> lambda表达式 */
      int fline = ls->linenumber;
      int func_idx = ps->func_idx_counter++;
      int parent_idx = ps->curfunc ? ps->curfunc->func_idx : -1;
      AstFunc *f;
      int nparams = 0;
      int param_cap = 4;
      AstFuncParam *params;
      AstStmt *ret;
      AstExpr *body_expr;

      lp_next(ps);  /* 跳过第一个 '|' */

      f = ast_new_func(ps->pool, func_idx, parent_idx, fline);
      f->source = ps->ls->source;
      f->is_vararg = 0;

      params = cast(AstFuncParam *,
        ast_pool_alloc(ps->pool, param_cap * sizeof(AstFuncParam)));

      /* 解析参数列表 */
      if (!lp_check(ps, '|')) {
        for (;;) {
          AstFuncParam *param;
          TString *name;
          if (lp_testnext(ps, TK_DOTS)) {
            f->is_vararg = 1;
            break;
          }
          name = lp_checkname(ps);
          if (nparams >= param_cap) {
            int new_cap = param_cap * 2;
            AstFuncParam *new_params = cast(AstFuncParam *,
              ast_pool_alloc(ps->pool, new_cap * sizeof(AstFuncParam)));
            memcpy(new_params, params, nparams * sizeof(AstFuncParam));
            params = new_params;
            param_cap = new_cap;
          }
          param = &params[nparams++];
          param->name = name;
          param->default_value = NULL;
          param->attr = AST_ATTR_NONE;
          param->type_hint = NULL;
          if (!lp_testnext(ps, ',')) break;
        }
      }

      f->nparams = nparams;
      f->params = params;
      f->nlocals = nparams;

      /* 期望闭合的 '|' */
      lp_checknext(ps, '|');

      /* 期望 -> */
      if (!lp_testnext(ps, TK_ARROW))
        lp_error(ps, "expected '->' after parameter list in lambda expression");

      /* 解析表达式体 */
      body_expr = parse_expr(ps);
      ret = ast_new_stmt_return(ps->pool, 1, fline);
      ret->u.retstmt.values[0] = body_expr;
      ast_block_add_stmt(ps->pool, &f->body, ret);

      e = ast_new_expr_func(ps->pool, f, 1, fline);
      break;
    }
    case TK_LAMBDA: {  /* lambda(params): expr 或 lambda(params) => statement */
      int fline = ls->linenumber;
      int func_idx = ps->func_idx_counter++;
      int parent_idx = ps->curfunc ? ps->curfunc->func_idx : -1;
      AstFunc *f;
      int nparams = 0;
      int param_cap = 4;
      AstFuncParam *params;
      AstStmt *ret;
      AstExpr *body_expr;

      lp_next(ps);  /* 跳过 'lambda' */

      f = ast_new_func(ps->pool, func_idx, parent_idx, fline);
      f->source = ps->ls->source;
      f->is_vararg = 0;

      params = cast(AstFuncParam *,
        ast_pool_alloc(ps->pool, param_cap * sizeof(AstFuncParam)));

      /* 解析参数列表 (params) */
      lp_checknext(ps, '(');
      if (!lp_check(ps, ')')) {
        for (;;) {
          AstFuncParam *param;
          TString *name;
          if (lp_testnext(ps, TK_DOTS)) {
            f->is_vararg = 1;
            break;
          }
          name = lp_checkname(ps);
          if (nparams >= param_cap) {
            int new_cap = param_cap * 2;
            AstFuncParam *new_params = cast(AstFuncParam *,
              ast_pool_alloc(ps->pool, new_cap * sizeof(AstFuncParam)));
            memcpy(new_params, params, nparams * sizeof(AstFuncParam));
            params = new_params;
            param_cap = new_cap;
          }
          param = &params[nparams++];
          param->name = name;
          param->default_value = NULL;
          param->attr = AST_ATTR_NONE;
          param->type_hint = NULL;
          if (!lp_testnext(ps, ',')) break;
        }
      }
      lp_checknext(ps, ')');

      f->nparams = nparams;
      f->params = params;
      f->nlocals = nparams;

      /* 期望 : 或 => */
      if (lp_testnext(ps, ':')) {
        /* lambda(params): expr */
        body_expr = parse_expr(ps);
        ret = ast_new_stmt_return(ps->pool, 1, fline);
        ret->u.retstmt.values[0] = body_expr;
        ast_block_add_stmt(ps->pool, &f->body, ret);
      } else if (lp_testnext(ps, TK_MEAN)) {
        /* lambda(params) => statement/block */
        if (lp_check(ps, '{')) {
          /* 语句块体 */
          AstFunc *oldfunc = ps->curfunc;
          ps->curfunc = f;
          scope_push(ps, 0);
          {
            int i;
            for (i = 0; i < nparams; i++) {
              scope_add_local(ps, params[i].name, params[i].attr);
            }
          }
          parse_block(ps, &f->body);
          scope_pop(ps);
          ps->curfunc = oldfunc;
        } else {
          /* 表达式体 */
          body_expr = parse_expr(ps);
          ret = ast_new_stmt_return(ps->pool, 1, fline);
          ret->u.retstmt.values[0] = body_expr;
          ast_block_add_stmt(ps->pool, &f->body, ret);
        }
      } else {
        /* 块体: lambda(params) body end */
        AstFunc *oldfunc = ps->curfunc;
        ps->curfunc = f;
        scope_push(ps, 0);
        {
          int i;
          for (i = 0; i < nparams; i++) {
            scope_add_local(ps, params[i].name, params[i].attr);
          }
        }
        parse_block(ps, &f->body);
        lp_checknext(ps, TK_END);
        scope_pop(ps);
        ps->curfunc = oldfunc;
      }

      e = ast_new_expr_func(ps->pool, f, 1, fline);
      break;
    }
    case '(': {
      /* 检查是否是箭头函数: (params) => body */
      {
        int is_arrow = 0;
        int la1 = luaX_lookahead(ls);
        if (la1 == ')') {
          if (luaX_lookahead2(ls) == TK_MEAN)
            is_arrow = 1;
        }
        if (is_arrow) {
          /* (params) => expr 箭头函数 */
          int fline = ls->linenumber;
          int func_idx = ps->func_idx_counter++;
          int parent_idx = ps->curfunc ? ps->curfunc->func_idx : -1;
          AstFunc *f;
          int nparams = 0;
          int param_cap = 4;
          AstFuncParam *params;
          AstStmt *ret;
          AstExpr *body_expr;

          lp_next(ps);  /* 跳过 '(' */

          f = ast_new_func(ps->pool, func_idx, parent_idx, fline);
          f->source = ps->ls->source;
          f->is_vararg = 0;

          params = cast(AstFuncParam *,
            ast_pool_alloc(ps->pool, param_cap * sizeof(AstFuncParam)));

          if (!lp_check(ps, ')')) {
            for (;;) {
              AstFuncParam *param;
              TString *name;
              if (lp_testnext(ps, TK_DOTS)) {
                f->is_vararg = 1;
                break;
              }
              name = lp_checkname(ps);
              if (nparams >= param_cap) {
                int new_cap = param_cap * 2;
                AstFuncParam *new_params = cast(AstFuncParam *,
                  ast_pool_alloc(ps->pool, new_cap * sizeof(AstFuncParam)));
                memcpy(new_params, params, nparams * sizeof(AstFuncParam));
                params = new_params;
                param_cap = new_cap;
              }
              param = &params[nparams++];
              param->name = name;
              param->default_value = NULL;
              param->attr = AST_ATTR_NONE;
              param->type_hint = NULL;
              if (!lp_testnext(ps, ',')) break;
            }
          }
          lp_checknext(ps, ')');
          lp_checknext(ps, TK_MEAN);  /* 跳过 => */

          f->nparams = nparams;
          f->params = params;
          f->nlocals = nparams;

          body_expr = parse_expr(ps);
          ret = ast_new_stmt_return(ps->pool, 1, fline);
          ret->u.retstmt.values[0] = body_expr;
          ast_block_add_stmt(ps->pool, &f->body, ret);

          e = ast_new_expr_func(ps->pool, f, 1, fline);
          break;
        }
      }
      /* 检查海象操作符: (name := expr) */
      lp_next(ps);  /* 跳过 '(' */
      if (ls->t.token == TK_NAME && lp_lookahead(ps) == TK_WALRUS) {
        TString *varname = ls->t.seminfo.ts;
        int save = ls->linenumber;
        lp_next(ps);  /* skip NAME */
        lp_next(ps);  /* skip := */
        AstExpr *val_expr = parse_expr(ps);
        lp_checknext(ps, ')');
        /* 创建赋值语句并添加到当前函数体 */
        {
          AstExpr *var_expr = ast_new_expr_ident(ps->pool, varname, save);
          AstStmt *assign_stmt = ast_new_stmt_assign(ps->pool, 1, 1, save);
          expr_to_target(ps, var_expr, &assign_stmt->u.assign.targets[0]);
          assign_stmt->u.assign.values[0] = val_expr;
          if (ps->curfunc) {
            ast_block_add_stmt(ps->pool, &ps->curfunc->body, assign_stmt);
          }
        }
        e = val_expr;
        e->paren = 1;
        break;
      }
      /* 普通括号表达式 */
      e = parse_expr(ps);
      lp_checknext(ps, ')');
      e->paren = 1;
      break;
    }
    case '[': {  /* 条件测试表达式 [ cond ] 或 map 字面量 [key = val] 或列表推导式 [for x in ...] */
      /* 检测列表推导式: [for x in expr do/yield expr if cond] */
      if (lp_lookahead(ps) == TK_FOR) {
        e = parse_list_comprehension(ps);
        break;
      }
      e = parse_test_or_map(ps);
      break;
    }
    case TK_SWITCH: {  /* switch 表达式 */
      e = parse_switch_expr(ps);
      break;
    }
    case TK_DO: {  /* do 表达式 */
      e = parse_do_expr(ps);
      break;
    }
    case TK_IF: {  /* if 条件表达式 if cond then expr else expr */
      e = parse_if_expr(ps);
      break;
    }
    case TK_AWAIT: {  /* await 表达式 */
      int aline = ls->linenumber;
      lp_next(ps);
      e = parse_expr(ps);
      e = ast_new_expr_unop(ps->pool, AST_UN_AWAIT, e, aline);
      break;
    }
    case TK_ASYNC: {  /* async 函数表达式 */
      lp_next(ps);
      if (lp_check(ps, TK_FUNCTION)) {
        int fline = ls->linenumber;
        lp_next(ps);
        AstFunc *f = parse_funcbody(ps, fline, 0, 0, 0);
        e = ast_new_expr_func(ps->pool, f, 0, fline);
      } else {
        lp_error(ps, "expected 'function' after 'async'");
        e = NULL;
      }
      break;
    }
    case TK_DOLLAR: {  /* $embed "filename" 或 $object { ... } */
      TString *kwname;
      lp_next(ps);  /* 跳过 '$' */
      if (!is_nametoken(ls->t.token))
        lp_error_expected(ps, TK_NAME);
      kwname = ls->t.seminfo.ts;

      if (strcmp(getstr(kwname), "embed") == 0) {
        /* $embed "filename"：读取文件内容作为字符串 */
        lp_next(ps);  /* 跳过 embed */
        if (ls->t.token != TK_STRING && ls->t.token != TK_RAWSTRING)
          lp_error(ps, "expected string literal after $embed");
        {
          const char *filename = getstr(ls->t.seminfo.ts);
          FILE *f = fopen(filename, "rb");
          if (!f)
            lp_error(ps, luaO_pushfstring(ps->L, "cannot open file '%s' for $embed", filename));
          fseek(f, 0, SEEK_END);
          long size = ftell(f);
          fseek(f, 0, SEEK_SET);
          char *buf = luaM_newvector(ps->L, size + 1, char);
          if (size > 0 && fread(buf, 1, size, f) != (size_t)size) {
            fclose(f);
            luaM_freearray(ps->L, buf, size + 1);
            lp_error(ps, "failed to read file for $embed");
          }
          fclose(f);
          buf[size] = '\0';
          TString *ts = luaS_newlstr(ps->L, buf, size);
          luaM_freearray(ps->L, buf, size + 1);
          e = ast_new_expr_embed(ps->pool, ts, line);
          lp_next(ps);  /* 跳过字符串字面量 */
        }
        break;
      }

      if (strcmp(getstr(kwname), "object") == 0) {
        /* $object(name1, name2, ...)：创建包含变量名和值的对象表 */
        lp_next(ps);  /* 跳过 'object' */
        lp_checknext(ps, '(');

        /* 解析变量名列表，创建表构造器 AST */
        {
          int tline = ls->linenumber;
          int cap = 4;
          int n = 0;
          AstTableEntry *entries = cast(AstTableEntry *,
            ast_pool_alloc(ps->pool, cap * sizeof(AstTableEntry)));

          while (!lp_check(ps, ')') && !lp_check(ps, TK_EOS)) {
            AstTableEntry *entry;
            TString *varname;
            if (n >= cap) {
              int new_cap = cap * 2;
              AstTableEntry *new_entries = cast(AstTableEntry *,
                ast_pool_alloc(ps->pool, new_cap * sizeof(AstTableEntry)));
              memcpy(new_entries, entries, n * sizeof(AstTableEntry));
              entries = new_entries;
              cap = new_cap;
            }
            entry = &entries[n++];
            memset(entry, 0, sizeof(*entry));

            varname = lp_checkname(ps);
            entry->kind = AST_TENTRY_KEY;
            entry->key = ast_new_expr_str(ps->pool, varname, AST_EXPR_STRING, line);
            /* 值使用变量引用 */
            entry->value = ast_new_expr_ident(ps->pool, varname, line);

            if (!lp_testnext(ps, ',')) break;
          }
          lp_checknext(ps, ')');

          AstExpr *table_expr = ast_new_expr_table(ps->pool, entries, n, tline);
          e = ast_new_expr_object(ps->pool, table_expr, line);
        }
        break;
      }

      /* $name(args) 从 keyword 编译时注册表查找 */
      {
        /* 将 keyword 名作为函数名，创建调用 AST 节点 */
        AstExpr *kw_func = ast_new_expr_ident(ps->pool, kwname, line);
        lp_next(ps);  /* 跳过 keyword 名 */

        /* 解析参数列表 */
        int nargs = 0;
        AstExpr **args = NULL;
        if (lp_testnext(ps, '(')) {
          if (!lp_check(ps, ')')) {
            args = parse_exprlist(ps, &nargs);
          }
          lp_checknext(ps, ')');
        }
        e = ast_new_expr_call(ps->pool, kw_func, args, nargs, line);
      }
      break;
    }
    case TK_DOLLDOLL: {  /* $$<运算符>(args) 运算符调用语法 */
      /*
       * 等价于 _OPERATORS["<运算符>"](args)
       * 从编译时注册表查找运算符对应函数
       */
      int line = ls->linenumber;
      TString *opname = NULL;
      const char *opstr = NULL;

      lp_next(ps);  /* 跳过 '$$' */

      /* 解析运算符符号 */
      int tok = ls->t.token;
      switch (tok) {
        case TK_PLUSPLUS: opstr = "++"; break;
        case TK_CONCAT: opstr = ".."; break;
        case TK_IDIV: opstr = "//"; break;
        case TK_SHL: opstr = "<<"; break;
        case TK_SHR: opstr = ">>"; break;
        case TK_EQ: opstr = "=="; break;
        case TK_NE: opstr = "~="; break;
        case TK_LE: opstr = "<="; break;
        case TK_GE: opstr = ">="; break;
        case TK_PIPE: opstr = "|>"; break;
        case TK_REVPIPE: opstr = "<|"; break;
        case TK_SPACESHIP: opstr = "<=>"; break;
        case TK_NULLCOAL: opstr = "??"; break;
        case TK_NULLCOALEQ: opstr = "?\?="; break;
        case TK_ARROW: opstr = "->"; break;
        case TK_MEAN: opstr = "=>"; break;
        case TK_ADDEQ: opstr = "+="; break;
        case TK_SUBEQ: opstr = "-="; break;
        case TK_MULEQ: opstr = "*="; break;
        case TK_DIVEQ: opstr = "/="; break;
        case TK_MODEQ: opstr = "%="; break;
        case '+': opstr = "+"; break;
        case '-': opstr = "-"; break;
        case '*': opstr = "*"; break;
        case '/': opstr = "/"; break;
        case '%': opstr = "%"; break;
        case '^': opstr = "^"; break;
        case '#': opstr = "#"; break;
        case '&': opstr = "&"; break;
        case '|': opstr = "|"; break;
        case '~': opstr = "~"; break;
        case '<': opstr = "<"; break;
        case '>': opstr = ">"; break;
        case '@': opstr = "@"; break;
        case TK_NAME:
          opname = ls->t.seminfo.ts;
          break;
        case TK_STRING:
          opname = ls->t.seminfo.ts;
          break;
        default:
          lp_error(ps, "expected operator symbol after '$$'");
          e = NULL;
          break;
      }

      if (opstr != NULL) {
        opname = luaS_new(ps->L, opstr);
      }

      if (opname != NULL) {
        lp_next(ps);  /* 跳过运算符符号 */

        /* 生成 _OPERATORS[opname] 表访问 AST 节点 */
        AstExpr *op_table = ast_new_expr_ident(ps->pool,
          luaS_new(ps->L, "_OPERATORS"), line);
        AstExpr *op_key = ast_new_expr_str(ps->pool, opname,
          AST_EXPR_STRING, line);
        e = ast_new_expr_index(ps->pool, op_table, op_key, 0, line);
      } else {
        e = NULL;
      }
      break;
    }
    case TK_ARROW: {  /* 箭头函数语法糖（语句形式）: ->(args) { body } 或 ->{ body } */
      int fline = ls->linenumber;
      int func_idx = ps->func_idx_counter++;
      int parent_idx = ps->curfunc ? ps->curfunc->func_idx : -1;
      AstFunc *f;
      int nparams = 0;
      int param_cap = 4;
      AstFuncParam *params;

      lp_next(ps);  /* 跳过 '->' (TK_ARROW) */

      f = ast_new_func(ps->pool, func_idx, parent_idx, fline);
      f->source = ps->ls->source;
      f->is_vararg = 0;

      params = cast(AstFuncParam *,
        ast_pool_alloc(ps->pool, param_cap * sizeof(AstFuncParam)));

      /* 解析可选参数列表 (args) */
      if (lp_testnext(ps, '(')) {
        if (!lp_check(ps, ')')) {
          for (;;) {
            AstFuncParam *param;
            TString *name;
            if (lp_testnext(ps, TK_DOTS)) {
              f->is_vararg = 1;
              break;
            }
            name = lp_checkname(ps);
            if (nparams >= param_cap) {
              int new_cap = param_cap * 2;
              AstFuncParam *new_params = cast(AstFuncParam *,
                ast_pool_alloc(ps->pool, new_cap * sizeof(AstFuncParam)));
              memcpy(new_params, params, nparams * sizeof(AstFuncParam));
              params = new_params;
              param_cap = new_cap;
            }
            param = &params[nparams++];
            param->name = name;
            param->default_value = NULL;
            param->attr = AST_ATTR_NONE;
            param->type_hint = NULL;
            if (!lp_testnext(ps, ',')) break;
          }
        }
        lp_checknext(ps, ')');
      }

      f->nparams = nparams;
      f->params = params;
      f->nlocals = nparams;

      /* 解析函数体 { body } */
      AstFunc *oldfunc = ps->curfunc;
      ps->curfunc = f;
      scope_push(ps, 0);
      {
        int i;
        for (i = 0; i < nparams; i++) {
          scope_add_local(ps, params[i].name, params[i].attr);
        }
      }
      parse_block(ps, &f->body);
      scope_pop(ps);
      ps->curfunc = oldfunc;

      e = ast_new_expr_func(ps->pool, f, 0, fline);
      break;
    }
    case TK_MEAN: {  /* 箭头函数语法糖（表达式形式）: =>(args) expr 或 => expr */
      int fline = ls->linenumber;
      int func_idx = ps->func_idx_counter++;
      int parent_idx = ps->curfunc ? ps->curfunc->func_idx : -1;
      AstFunc *f;
      int nparams = 0;
      int param_cap = 4;
      AstFuncParam *params;
      AstExpr *body_expr;
      AstStmt *ret;

      lp_next(ps);  /* 跳过 '=>' (TK_MEAN) */

      f = ast_new_func(ps->pool, func_idx, parent_idx, fline);
      f->source = ps->ls->source;
      f->is_vararg = 0;

      params = cast(AstFuncParam *,
        ast_pool_alloc(ps->pool, param_cap * sizeof(AstFuncParam)));

      /* 解析可选参数列表 (args) */
      if (lp_testnext(ps, '(')) {
        if (!lp_check(ps, ')')) {
          for (;;) {
            AstFuncParam *param;
            TString *name;
            if (lp_testnext(ps, TK_DOTS)) {
              f->is_vararg = 1;
              break;
            }
            name = lp_checkname(ps);
            if (nparams >= param_cap) {
              int new_cap = param_cap * 2;
              AstFuncParam *new_params = cast(AstFuncParam *,
                ast_pool_alloc(ps->pool, new_cap * sizeof(AstFuncParam)));
              memcpy(new_params, params, nparams * sizeof(AstFuncParam));
              params = new_params;
              param_cap = new_cap;
            }
            param = &params[nparams++];
            param->name = name;
            param->default_value = NULL;
            param->attr = AST_ATTR_NONE;
            param->type_hint = NULL;
            if (!lp_testnext(ps, ',')) break;
          }
        }
        lp_checknext(ps, ')');
      }

      f->nparams = nparams;
      f->params = params;
      f->nlocals = nparams;

      /* 解析表达式体，包装为 return 语句 */
      body_expr = parse_expr(ps);
      ret = ast_new_stmt_return(ps->pool, 1, fline);
      ret->u.retstmt.values[0] = body_expr;
      ast_block_add_stmt(ps->pool, &f->body, ret);

      e = ast_new_expr_func(ps->pool, f, 1, fline);
      break;
    }
    case TK_ASTPARSER: {
      /* astparser 作为表达式：从 ZIO 直接读取括号内的代码，解析并编译 */
      lua_State *L = ps->L;
      ZIO *z = ls->z;
      int c;

      /* 跳过空格和换行，找到 '(' */
      c = ls->current;
      while (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
        c = zgetc(z);
      }
      if (c != '(') {
        lp_error_expected(ps, '(');
      }
      /* 跳过 '(' */
      c = zgetc(z);

      /* 从 ZIO 读取原始源码，跟踪嵌套括号，跳过字符串和注释 */
      size_t buf_len = 0, buf_cap = 256;
      char *buf = luaM_newvector(L, buf_cap, char);

      int depth = 1;  /* 已经消费了 '('，深度为 1 */
      /* c 已是 '(' 之后的第一个字符，直接使用 */

      while (depth > 0) {
        if (c == EOZ) {
          luaM_free(L, buf);
          lp_error(ps, "unfinished astparser block");
        }

        /* 处理字符串：单引号和双引号 */
        if (c == '\'' || c == '"') {
          int quote = c;
          if (buf_len >= buf_cap) {
            size_t old = buf_cap; buf_cap *= 2;
            buf = luaM_reallocvchar(L, buf, old, buf_cap);
          }
          buf[buf_len++] = (char)c;
          while ((c = zgetc(z)) != EOZ) {
            if (buf_len >= buf_cap) {
              size_t old = buf_cap; buf_cap *= 2;
              buf = luaM_reallocvchar(L, buf, old, buf_cap);
            }
            buf[buf_len++] = (char)c;
            if (c == '\\') {  /* 转义字符 */
              c = zgetc(z);
              if (c != EOZ) {
                if (buf_len >= buf_cap) {
                  size_t old = buf_cap; buf_cap *= 2;
                  buf = luaM_reallocvchar(L, buf, old, buf_cap);
                }
                buf[buf_len++] = (char)c;
              }
              continue;
            }
            if (c == quote) break;
          }
          c = zgetc(z);
          continue;
        }

        /* 处理长括号 [[ 或 [=...[ */
        if (c == '[') {
          int c2 = zgetc(z);
          if (c2 == '[' || c2 == '=') {
            int eq = 0;
            while (c2 == '=') { eq++; c2 = zgetc(z); }
            if (c2 == '[') {
              /* 确认是长括号，写入原始字符 */
              if (buf_len + 2 + eq >= buf_cap) {
                size_t old = buf_cap;
                buf_cap = (buf_len + 2 + eq) * 2;
                buf = luaM_reallocvchar(L, buf, old, buf_cap);
              }
              buf[buf_len++] = '[';
              {
                int i;
                for (i = 0; i < eq; i++) buf[buf_len++] = '=';
              }
              buf[buf_len++] = '[';

              /* 读取直到匹配的 ]=...=] */
              while ((c = zgetc(z)) != EOZ) {
                if (buf_len >= buf_cap) {
                  size_t old = buf_cap; buf_cap *= 2;
                  buf = luaM_reallocvchar(L, buf, old, buf_cap);
                }
                buf[buf_len++] = (char)c;
                if (c == ']') {
                  int match = 1;
                  int i;
                  for (i = 0; i < eq; i++) {
                    c = zgetc(z);
                    if (c != '=') { match = 0; break; }
                    if (buf_len >= buf_cap) {
                      size_t old = buf_cap; buf_cap *= 2;
                      buf = luaM_reallocvchar(L, buf, old, buf_cap);
                    }
                    buf[buf_len++] = (char)c;
                  }
                  if (match) {
                    c = zgetc(z);
                    if (c == ']') {
                      if (buf_len >= buf_cap) {
                        size_t old = buf_cap; buf_cap *= 2;
                        buf = luaM_reallocvchar(L, buf, old, buf_cap);
                      }
                      buf[buf_len++] = ']';
                      break;
                    }
                    if (buf_len >= buf_cap) {
                      size_t old = buf_cap; buf_cap *= 2;
                      buf = luaM_reallocvchar(L, buf, old, buf_cap);
                    }
                    buf[buf_len++] = (char)c;
                  }
                }
              }
              c = zgetc(z);
              continue;
            }
          }
          /* 不是长括号，普通 '[' */
          zungetc(z);
          depth++;
          if (buf_len >= buf_cap) {
            size_t old = buf_cap; buf_cap *= 2;
            buf = luaM_reallocvchar(L, buf, old, buf_cap);
          }
          buf[buf_len++] = '[';
          c = zgetc(z);
          continue;
        }

        /* 处理注释 -- */
        if (c == '-') {
          int c2 = zgetc(z);
          if (c2 == '-') {
            /* 注释开始 */
            int c3 = zgetc(z);
            if (c3 == '[') {
              /* 可能是块注释 --[[ */
              int c4 = zgetc(z);
              if (c4 == '[' || c4 == '=') {
                /* 块注释，跳过直到 ]] */
                if (c4 == '[') {
                  while ((c = zgetc(z)) != EOZ) {
                    if (c == ']') {
                      c = zgetc(z);
                      if (c == ']') break;
                    }
                  }
                } else {
                  int eq = 0;
                  while (c4 == '=') { eq++; c4 = zgetc(z); }
                  while ((c = zgetc(z)) != EOZ) {
                    if (c == ']') {
                      int match = 1;
                      int i;
                      for (i = 0; i < eq; i++) {
                        c = zgetc(z);
                        if (c != '=') { match = 0; break; }
                      }
                      if (match && (c = zgetc(z)) == ']') break;
                    }
                  }
                }
                c = zgetc(z);
                continue;
              } else {
                /* 行注释，跳过直到行尾 */
                zungetc(z);
                while ((c = zgetc(z)) != EOZ && c != '\n' && c != '\r') {}
                if (c == '\r') {
                  c = zgetc(z);
                  if (c != '\n') { zungetc(z); c = '\n'; }
                }
                c = zgetc(z);
                continue;
              }
            } else {
              /* 行注释，跳过直到行尾 */
              zungetc(z);
              while ((c = zgetc(z)) != EOZ && c != '\n' && c != '\r') {}
              if (c == '\r') {
                c = zgetc(z);
                if (c != '\n') { zungetc(z); c = '\n'; }
              }
              c = zgetc(z);
              continue;
            }
          } else {
            /* 普通 '-' */
            zungetc(z);
            if (buf_len >= buf_cap) {
              size_t old = buf_cap; buf_cap *= 2;
              buf = luaM_reallocvchar(L, buf, old, buf_cap);
            }
            buf[buf_len++] = '-';
            c = zgetc(z);
            continue;
          }
        }

        /* 跟踪括号嵌套 */
        if (c == '(') depth++;
        else if (c == ')') depth--;

        if (depth > 0) {
          if (buf_len >= buf_cap) {
            size_t old = buf_cap; buf_cap *= 2;
            buf = luaM_reallocvchar(L, buf, old, buf_cap);
          }
          buf[buf_len++] = (char)c;
        }

        c = zgetc(z);
      }

      /* 更新 ls->current 为 ')' 之后的下一个字符 */
      ls->current = c;

      /* NUL 终止 */
      if (buf_len >= buf_cap) {
        size_t old = buf_cap;
        buf_cap = buf_len + 1;
        buf = luaM_reallocvchar(L, buf, old, buf_cap);
      }
      buf[buf_len] = '\0';

      /* 创建 ZIO 用于 AST 解析 */
      /* firstchar 从 buf[0] 读取，ZIO 从 buf+1 开始，避免重复读取 */
      int firstchar = (buf_len > 0) ? (unsigned char)buf[0] : '\n';
      ZIO ast_z;
      memset(&ast_z, 0, sizeof(ast_z));
      ast_z.L = L;
      ast_z.p = (buf_len > 0) ? buf + 1 : buf;
      ast_z.n = (buf_len > 0) ? buf_len - 1 : 0;
      ast_z.reader = astparser_zreader;  /* 设置 reader，避免 zgetc 耗尽缓冲后空指针崩溃 */

      /* AST 解析用的 Mbuffer */
      Mbuffer ast_buff;
      luaZ_initbuffer(L, &ast_buff);

      /* 创建 Dyndata */
      Dyndata ast_dyd;
      memset(&ast_dyd, 0, sizeof(ast_dyd));

      /* 暂停 GC */
      int old_gc = lua_gc(L, LUA_GCISRUNNING, 0);
      lua_gc(L, LUA_GCSTOP, 0);

      /* AST 解析 + 代码生成 */
      AstChunk *chunk = luaY_parse_ast(L, &ast_z, &ast_buff, &ast_dyd, "astparser",
        firstchar);
      chunk->main_func->is_vararg = 1;
      Proto *p = luaY_codegen_chunk(L, chunk, &ast_dyd);

      /* 恢复 GC */
      if (old_gc) lua_gc(L, LUA_GCRESTART, 0);

      /* 创建 AST_EXPR_ASTPARSER 节点 */
      e = ast_new_expr_astparser(ps->pool, p, chunk, line);

      /* 释放源码缓冲区 */
      luaZ_freebuffer(L, &ast_buff);
      luaM_free(L, buf);

      /* 推进 lexer 到下一个 token（astparser 块已处理完毕） */
      luaX_next(ls);

      break;
    }
    default: {
      lp_error_expected(ps, TK_NAME);
      e = NULL;
      break;
    }
  }
  return parse_suffixedexpr(ps, e);
}


/**
 * @brief 检测当前 token 是否为切片语法
 * 
 * 在 '[' 之后调用，检测第一个 token 是否为 ':'。
 * 如果是 ':'，说明 start 省略，是切片语法。
 * 否则返回 0，由 parse_suffixedexpr 进一步解析后判断。
 * 
 * @param ps 解析器状态
 * @return 1 表示是切片语法，0 表示不是
 */
static int lp_is_slice_syntax(ParserState *ps) {
  return (ps->ls->t.token == ':');
}


/**
 * @brief 解析切片语法: [start:end] 或 [start:end:step]
 * 
 * 调用前已跳过 '['，start 可能已被解析（或为 NULL 表示省略）。
 * 支持的省略形式: [:end], [start:], [:], [::step], [start:end:step]
 * 
 * @param ps 解析器状态
 * @param v 源表表达式
 * @param start 已解析的 start 表达式（NULL 表示省略 start）
 * @param line 源代码行号
 * @return 切片表达式节点
 */
static AstExpr *lp_parse_slice(ParserState *ps, AstExpr *v, AstExpr *start, int line) {
  LexState *ls = ps->ls;
  AstExpr *end = NULL;
  AstExpr *step = NULL;
  
  /* 如果 start 为 NULL，则当前 token 就是 ':' */
  if (start == NULL) {
    /* start 省略，当前 token 是 ':'，跳过 */
    lp_next(ps);
  } else {
    /* start 已解析，当前 token 是 ':' 或 TK_DBCOLON，跳过 */
    if (ls->t.token == TK_DBCOLON) {
      /* TK_DBCOLON (::) 需要拆分成两个 ':' */
      ls->t.token = ':';
      /* 使用 pending 机制塞入第二个 ':' */
      static Token pending_colon;
      pending_colon.token = ':';
      pending_colon.seminfo.ts = NULL;
      ls->pending_tokens = &pending_colon;
      ls->npending = 1;
      ls->pending_idx = 0;
    }
    lp_next(ps);  /* 跳过 ':' */
  }
  
  /* 解析 end 表达式 */
  if (ls->t.token == ']' || ls->t.token == ':') {
    /* 省略 end */
    end = NULL;
  } else {
    end = parse_expr(ps);
  }
  
  /* 检查是否有 step */
  if (ls->t.token == ':') {
    lp_next(ps);  /* 跳过第二个 ':' */
    if (ls->t.token == ']') {
      /* 省略 step */
      step = NULL;
    } else {
      step = parse_expr(ps);
    }
  }
  
  lp_checknext(ps, ']');  /* 必须以 ']' 结束 */
  
  return ast_new_expr_slice(ps->pool, v, start, end, step, line);
}


/**
 * @brief 解析后缀表达式（索引、方法调用、函数调用等）
 * @param ps 解析器状态
 * @param v 已解析的主表达式
 * @return 解析后的表达式
 */
static AstExpr *parse_suffixedexpr(ParserState *ps, AstExpr *v) {
  LexState *ls = ps->ls;
  for (;;) {
    int line = ls->linenumber;
    if (lp_testnext(ps, '.')) {
      TString *key = lp_checkfieldname(ps);
      AstExpr *keyexpr = ast_new_expr_str(ps->pool, key, AST_EXPR_STRING, line);
      v = ast_new_expr_index(ps->pool, v, keyexpr, 0, line);
    }
    else if (lp_testnext(ps, TK_DBCOLON)) {
      /* :: 已被消费，检测 ::name:: 标签模式 */
      if (lp_check(ps, TK_NAME) && lp_lookahead(ps) == TK_DBCOLON) {
        /* 这是标签 ::name::，停止后缀表达式解析循环 */
        break;
      }
      /* 字段访问：::name 等价于 .name */
      TString *key = lp_checkfieldname(ps);
      AstExpr *keyexpr = ast_new_expr_str(ps->pool, key, AST_EXPR_STRING, line);
      v = ast_new_expr_index(ps->pool, v, keyexpr, 0, line);
    }
    else if (lp_testnext(ps, TK_OPTCHAIN)) {
      TString *key = lp_checkfieldname(ps);
      AstExpr *keyexpr = ast_new_expr_str(ps->pool, key, AST_EXPR_STRING, line);
      v = ast_new_expr_index(ps->pool, v, keyexpr, 1, line);
    }
    else if (lp_testnext(ps, '[')) {
      /* 检测切片语法: 当前 token 是 ':' 或表达式后跟 ':' */
      if (ls->t.token == ':') {
        /* 切片语法 [:end] — start 省略 */
        v = lp_parse_slice(ps, v, NULL, line);
      } else {
        int is_opt = lp_testnext(ps, '?');
        AstExpr *key = parse_expr(ps);
        /* 检查 key 后面是否跟 ':'（切片语法 [start:end]） */
        if (ls->t.token == ':') {
          /* 切片语法: key 是 start 表达式 */
          v = lp_parse_slice(ps, v, key, line);
        } else {
          lp_checknext(ps, ']');
          v = ast_new_expr_index(ps->pool, v, key, is_opt, line);
        }
      }
    }
    else if (lp_testnext(ps, ':')) {
      TString *method = lp_checkfieldname(ps);
      if (lp_testnext(ps, '(')) {
        int nargs = 0;
        AstExpr **args = NULL;
        if (!lp_check(ps, ')')) {
          args = parse_exprlist(ps, &nargs);
        }
        lp_checknext(ps, ')');
        v = ast_new_expr_methodcall(ps->pool, v, method, args, nargs, line);
      } else if (lp_check(ps, TK_STRING) || lp_check(ps, TK_RAWSTRING)) {
        AstExpr *s;
        int sl = ls->linenumber;
        TString *ts = ls->t.seminfo.ts;
        lp_next(ps);
        s = ast_new_expr_str(ps->pool, ts, AST_EXPR_STRING, sl);
        {
          AstExpr **args = cast(AstExpr **, ast_pool_alloc(ps->pool, sizeof(AstExpr *)));
          args[0] = s;
          v = ast_new_expr_methodcall(ps->pool, v, method, args, 1, line);
        }
      } else if (lp_check(ps, '{')) {
        AstExpr *t = parse_primary(ps);
        AstExpr **args = cast(AstExpr **, ast_pool_alloc(ps->pool, sizeof(AstExpr *)));
        args[0] = t;
        v = ast_new_expr_methodcall(ps->pool, v, method, args, 1, line);
      } else {
        /* 方法引用（无括号）：obj:method 作为函数值使用 */
        v = ast_new_expr_methodref(ps->pool, v, method, line);
      }
    }
    else if (lp_testnext(ps, '(')) {
      int nargs = 0;
      AstExpr **args = NULL;
      if (!lp_check(ps, ')')) {
        args = parse_exprlist(ps, &nargs);
      }
      lp_checknext(ps, ')');
      v = ast_new_expr_call(ps->pool, v, args, nargs, line);
    }
    else if ((lp_check(ps, TK_STRING) || lp_check(ps, TK_RAWSTRING) || lp_check(ps, TK_INTERPSTRING))) {
      AstExpr *s;
      int sl = ls->linenumber;
      TString *ts = ls->t.seminfo.ts;
      AstExprKind skind = lp_check(ps, TK_INTERPSTRING) ? AST_EXPR_INTERPSTRING : AST_EXPR_STRING;
      lp_next(ps);
      s = ast_new_expr_str(ps->pool, ts, skind, sl);
      {
        AstExpr **args = cast(AstExpr **, ast_pool_alloc(ps->pool, sizeof(AstExpr *)));
        args[0] = s;
        v = ast_new_expr_call(ps->pool, v, args, 1, line);
      }
    }
    else if (lp_check(ps, '{')) {
      AstExpr *t = parse_primary(ps);
      AstExpr **args = cast(AstExpr **, ast_pool_alloc(ps->pool, sizeof(AstExpr *)));
      args[0] = t;
      v = ast_new_expr_call(ps->pool, v, args, 1, line);
    }
    else if (lp_check(ps, TK_WALRUS)) {
      /* walrus := 由调用者处理（parse_stat 的 default_expr 或 parse_primary 的括号分支）*/
      break;
    }
    else if (ls->t.token == TK_NAME) {
      /* 函数表达式 / 调用表达式后不允许中缀方法调用：
       * function() end name(arg) 和 call() name(arg) 是两条独立语句 */
      if (v->kind == AST_EXPR_FUNC_EXPR || v->kind == AST_EXPR_ARROW_FUNC
          || v->kind == AST_EXPR_CALL) {
        break;
      }
      /* 中缀函数调用：receiver method arg
       * 条件：同一行、lookahead 是表达式起始
       * 注意：luaX_lookahead 会缓存 token，但在 suffixedexpr 中，
       * 检测失败后我们会 break 退出，缓存由后续 lp_next 消费 */
      /* 使用 ls->lastline 而非 ls->linenumber：
       * parse_primary 调用 lp_next 后，ls->linenumber 已更新为 method name 的行号，
       * 而 receiver 的行号保存在 ls->lastline 中（luaX_next 在消费 token 前设置） */
      int lineno = ls->lastline;
      int la = luaX_lookahead(ls);
      if (is_expr_start_token(la) && ls->t.linenumber == lineno) {
        TString *method = ls->t.seminfo.ts;
        lp_next(ps); /* 跳过方法名，消费缓存的 lookahead */
        AstExpr *arg = parse_subexpr(ps, 0);
        AstExpr **args = cast(AstExpr **, ast_pool_alloc(ps->pool, sizeof(AstExpr *)));
        args[0] = arg;
        v = ast_new_expr_methodcall(ps->pool, v, method, args, 1, line);
      } else {
        break;  /* 非 infix，缓存由后续 lp_next 消费 */
      }
    }
    else {
      break;
    }
  }
  return v;
}


/**
 * @brief 简单表达式解析（一元运算+primary）
 * @param ps 解析器状态
 * @return 表达式节点
 */
static AstExpr *parse_simpleexpr(ParserState *ps) {
  LexState *ls = ps->ls;
  int line = ls->linenumber;
  int uop = get_unop(ls->t.token);
  if (uop != -1) {
    lp_next(ps);
    {
      AstExpr *operand = parse_subexpr(ps, UNARY_PRIORITY);
      /* 负号作用在范围表达式上的特殊处理：-3..3 应解析为 RANGE(-3, 3) 而非 NEG(RANGE(3, 3)) */
      if (uop == AST_UN_MINUS && operand->kind == AST_EXPR_RANGE) {
        /* 对范围起始值取负，作为新的范围表达式 */
        AstExpr *neg_start = ast_new_expr_unop(ps->pool, AST_UN_MINUS,
                                                operand->u.range.start, line);
        operand->u.range.start = neg_start;
        return operand;
      }
      return ast_new_expr_unop(ps->pool, (AstUnOp)uop, operand, line);
    }
  }
  return parse_primary(ps);
}


/**
 * @brief 使用优先级爬升算法解析二元/一元表达式
 * @param ps 解析器状态
 * @param min_prec 最低优先级
 * @return 表达式节点
 */
static AstExpr *parse_subexpr(ParserState *ps, int min_prec) {
  AstExpr *v = parse_simpleexpr(ps);
  LexState *ls = ps->ls;

  for (;;) {
    int op = get_binop(ls->t.token);
    if (op == -1) {
      break;
    }
    if (binop_priority[op].left <= min_prec) break;

    {
      int op_line = ls->linenumber;
      int prec_right = binop_priority[op].right;
      int concat_nospace = (op == AST_BIN_CONCAT) ? ls->t.nospace : 0;
      lp_next(ps);
      {
        AstExpr *rhs = parse_subexpr(ps, prec_right);
        /* 范围操作符检测：'..' 前无空格且两端为整数常量时生成范围表
         * 注意：仅当 start <= end 时才是合法范围，否则回退到字符串拼接
         * 负数字面量（如 -3）是 AST_EXPR_UNOP(AST_UN_MINUS, AST_EXPR_INT)，也需要识别 */
        lua_Integer start_val = 0, end_val = 0;
        int is_range = concat_nospace;
        if (is_range && v->kind == AST_EXPR_INT) {
          start_val = v->u.ival;
        } else if (is_range && v->kind == AST_EXPR_UNOP
                   && v->u.unop.op == AST_UN_MINUS
                   && v->u.unop.operand->kind == AST_EXPR_INT) {
          start_val = -v->u.unop.operand->u.ival;
        } else {
          is_range = 0;
        }
        if (is_range && rhs->kind == AST_EXPR_INT) {
          end_val = rhs->u.ival;
        } else if (is_range && rhs->kind == AST_EXPR_UNOP
                   && rhs->u.unop.op == AST_UN_MINUS
                   && rhs->u.unop.operand->kind == AST_EXPR_INT) {
          end_val = -rhs->u.unop.operand->u.ival;
        } else {
          is_range = 0;
        }
        if (is_range && start_val <= end_val) {
          v = ast_new_expr_range(ps->pool, v, rhs, op_line);
        } else {
          v = ast_new_expr_binop(ps->pool, (AstBinOp)op, v, rhs, op_line);
        }
      }
    }
  }
  return v;
}


/**
 * @brief 顶层表达式解析
 * @param ps 解析器状态
 * @return 表达式节点
 */
static AstExpr *parse_expr(ParserState *ps) {
  return parse_subexpr(ps, 0);
}


/* ============================================================
 *                       函数体解析
 * ============================================================ */

/**
 * @brief 解析函数体：( params ) body end
 * @param ps 解析器状态
 * @param line 函数定义行号
 * @param is_arrow 是否为箭头函数
 * @return 函数节点
 */
static AstFunc *parse_funcbody(ParserState *ps, int line, int is_arrow, int need_self, int is_async) {
  AstFunc *f;
  int func_idx = ps->func_idx_counter++;
  int parent_idx = ps->curfunc ? ps->curfunc->func_idx : -1;
  int nparams = 0;
  int param_cap = 4;
  AstFuncParam *params;
  (void)is_arrow;

  f = ast_new_func(ps->pool, func_idx, parent_idx, line);
  f->source = ps->ls->source;
  f->is_async = is_async;
  ast_chunk_add_func(ps->chunk, f);

  params = cast(AstFuncParam *,
    ast_pool_alloc(ps->pool, param_cap * sizeof(AstFuncParam)));

  {
    AstFunc *oldfunc = ps->curfunc;
    ps->curfunc = f;

    lp_checknext(ps, '(');
    f->is_vararg = 0;

    /* 如果是方法定义，在参数最前面插入self */
    if (need_self) {
      AstFuncParam *param;
      param = &params[nparams++];
      param->name = luaS_newliteral(ps->L, "self");
      param->default_value = NULL;
      param->attr = AST_ATTR_NONE;
      param->type_hint = NULL;
    }

    if (!lp_check(ps, ')')) {
      for (;;) {
        if (lp_testnext(ps, TK_DOTS)) {
          f->is_vararg = 1;
          /* 命名变参：...name 语法 */
          if (is_nametoken(ps->ls->t.token)) {
            f->vararg_name = lp_checkname(ps);
          }
          break;
        }
        {
          TString *name = lp_checkname(ps);
          int attr = AST_ATTR_NONE;
          AstFuncParam *param;
          if (lp_testnext(ps, TK_CONST)) attr = AST_ATTR_CONST;
          TypeHint *type_hint = lp_gettypehint(ps);  /* 解析参数类型注解 */
          if (nparams >= param_cap) {
            int new_cap = param_cap * 2;
            AstFuncParam *new_params = cast(AstFuncParam *,
              ast_pool_alloc(ps->pool, new_cap * sizeof(AstFuncParam)));
            memcpy(new_params, params, nparams * sizeof(AstFuncParam));
            params = new_params;
            param_cap = new_cap;
          }
          param = &params[nparams++];
          param->name = name;
          param->default_value = NULL;
          /* 默认参数值：param = default 语法 */
          if (lp_testnext(ps, '=')) {
            param->default_value = parse_expr(ps);
          }
          param->attr = attr;
          param->type_hint = type_hint;
        }
        if (!lp_testnext(ps, ',')) break;
      }
    }
    lp_checknext(ps, ')');
    f->return_type_hint = lp_gettypehint(ps);  /* 解析返回值类型注解 */

    /* 检查函数属性 <nodiscard> */
    if (lp_testnext(ps, '<')) {
      if (is_nametoken(ps->ls->t.token)) {
        const char *attr = getstr(ps->ls->t.seminfo.ts);
        if (strcmp(attr, "nodiscard") == 0) {
          f->nodiscard = 1;
        }
        lp_next(ps);
      }
      lp_checknext(ps, '>');
    }

    f->nparams = nparams;
    f->params = params;
    f->nlocals = nparams;

    scope_push(ps, 0);
    {
      int i;
      for (i = 0; i < nparams; i++) {
        scope_add_local(ps, params[i].name, params[i].attr);
      }
    }

    parse_block(ps, &f->body);
    lp_checknext(ps, TK_END);

    scope_pop(ps);
    ps->curfunc = oldfunc;
  }
  return f;
}


/* ============================================================
 *                       语句解析
 * ============================================================ */

/**
 * @brief 判断token是否是块结束标记
 * @param token 当前token
 * @return 1表示是块结束token
 */
static int is_block_end(int token) {
  switch (token) {
    case TK_END: case TK_ELSE: case TK_ELSEIF:
    case TK_UNTIL: case TK_EOS:
    case TK_CASE: /* when 语句的 case 分支 */
    case '}': /* guard/with/using 等 { } 块结束 */
      return 1;
    default:
      return 0;
  }
}


/**
 * @brief 解析语句块
 * @param ps 解析器状态
 * @param blk 要填充的语句块
 */
static void parse_block(ParserState *ps, AstBlock *blk) {
  block_init(ps, blk);
  while (!is_block_end(ps->ls->t.token)) {
    AstStmt *s = parse_stat(ps);
    if (s != NULL) {
      ast_block_add_stmt(ps->pool, blk, s);
    }
    lp_testnext(ps, ';');
  }
}


/**
 * @brief 解析局部变量名列表和初始化表达式（local/const 共用）
 * @param ps 解析器状态
 * @param force_const 是否强制所有变量为 const 属性
 * @return 语句节点
 */
static AstStmt *parse_local_var_list(ParserState *ps, int force_const) {
  LexState *ls = ps->ls;
  int line = ls->linenumber;
  int nnames = 0;
  int nvalues = 0;
  int cap = 4;
  TString **names = cast(TString **, ast_pool_alloc(ps->pool, cap * sizeof(TString *)));
  int *attrs = cast(int *, ast_pool_alloc(ps->pool, cap * sizeof(int)));
  TypeHint **type_hints = cast(TypeHint **, ast_pool_alloc(ps->pool, cap * sizeof(TypeHint *)));
  AstExpr **values = NULL;
  AstStmt *s;

  do {
    TString *name;
    int attr = force_const ? AST_ATTR_CONST : AST_ATTR_NONE;
    if (nnames >= cap) {
      int new_cap = cap * 2;
      TString **new_names = cast(TString **,
        ast_pool_alloc(ps->pool, new_cap * sizeof(TString *)));
      int *new_attrs = cast(int *,
        ast_pool_alloc(ps->pool, new_cap * sizeof(int)));
      TypeHint **new_type_hints = cast(TypeHint **,
        ast_pool_alloc(ps->pool, new_cap * sizeof(TypeHint *)));
      memcpy(new_names, names, nnames * sizeof(TString *));
      memcpy(new_attrs, attrs, nnames * sizeof(int));
      memcpy(new_type_hints, type_hints, nnames * sizeof(TypeHint *));
      names = new_names;
      attrs = new_attrs;
      type_hints = new_type_hints;
      cap = new_cap;
    }
    name = lp_checkname(ps);
    if (!force_const && lp_testnext(ps, TK_CONST)) attr = AST_ATTR_CONST;
    type_hints[nnames] = lp_gettypehint(ps);  /* 解析局部变量类型注解 */
    names[nnames] = name;
    attrs[nnames] = attr;
    nnames++;
  } while (lp_testnext(ps, ','));

  if (lp_testnext(ps, '=')) {
    values = parse_exprlist(ps, &nvalues);
  }

  s = ast_new_stmt_local(ps->pool, nnames, names, nvalues, line);
  {
    int i;
    for (i = 0; i < nnames; i++) {
      s->u.local.names[i] = names[i];
      s->u.local.attrs[i] = attrs[i];
      s->u.local.type_hints[i] = type_hints[i];
      scope_add_local(ps, names[i], attrs[i]);
    }
    if (values != NULL && nvalues > 0) {
      s->u.local.values = values;
    }
  }
  return s;
}


/**
 * @brief 解析局部变量声明语句
 * @param ps 解析器状态
 * @return 语句节点
 */
static AstStmt *parse_local_stat(ParserState *ps) {
  LexState *ls = ps->ls;
  int line = ls->linenumber;
  lp_next(ps); /* skip 'local' */

  /* 处理解构赋值：local {a, b} = t 或 local [a, b] = t */
  if (ls->t.token == '{' || ls->t.token == '[') {
    int is_array = (ls->t.token == '[');
    int end_char = is_array ? ']' : '}';
    int nvars = 0;
    int cap = 4;
    TString **varnames = cast(TString **, ast_pool_alloc(ps->pool, cap * sizeof(TString *)));
    AstExpr **defaults = NULL;
    AstExpr *source = NULL;

    lp_next(ps); /* skip { or [ */
    do {
      TString *name;
      AstExpr *def = NULL;
      if (nvars >= cap) {
        int new_cap = cap * 2;
        TString **new_names = cast(TString **,
          ast_pool_alloc(ps->pool, new_cap * sizeof(TString *)));
        memcpy(new_names, varnames, nvars * sizeof(TString *));
        varnames = new_names;
        if (defaults) {
          AstExpr **new_defaults = cast(AstExpr **,
            ast_pool_alloc(ps->pool, new_cap * sizeof(AstExpr *)));
          memcpy(new_defaults, defaults, nvars * sizeof(AstExpr *));
          defaults = new_defaults;
        }
        cap = new_cap;
      }
      name = lp_checkname(ps);
      varnames[nvars] = name;
      scope_add_local(ps, name, AST_ATTR_NONE);
      lp_gettypehint(ps);  /* 解析解构变量的类型注解 */
      /* 解析默认值 */
      if (lp_testnext(ps, '=')) {
        if (!defaults) {
          defaults = cast(AstExpr **, ast_pool_alloc(ps->pool, cap * sizeof(AstExpr *)));
          memset(defaults, 0, cap * sizeof(AstExpr *));
        }
        def = parse_expr(ps);
        defaults[nvars] = def;
      }
      nvars++;
    } while (lp_testnext(ps, ','));
    lp_checknext(ps, end_char);

    /* 解析 = expr */
    if (lp_testnext(ps, '=')) {
      source = parse_expr(ps);
    }

    return ast_new_stmt_take(ps->pool, nvars, varnames, defaults, source, is_array, line);
  }

  /* 处理 take 解构赋值：local take {a, b, ...} = expr */
  if (lp_testnext(ps, TK_TAKE)) {
    int nvars = 0;
    int cap = 4;
    TString **varnames = cast(TString **, ast_pool_alloc(ps->pool, cap * sizeof(TString *)));
    AstExpr *source = NULL;

    /* 解析 {a, b, ...} */
    if (lp_testnext(ps, '{')) {
      do {
        TString *name;
        if (nvars >= cap) {
          int new_cap = cap * 2;
          TString **new_names = cast(TString **,
            ast_pool_alloc(ps->pool, new_cap * sizeof(TString *)));
          memcpy(new_names, varnames, nvars * sizeof(TString *));
          varnames = new_names;
          cap = new_cap;
        }
        name = lp_checkname(ps);
        varnames[nvars++] = name;
        scope_add_local(ps, name, AST_ATTR_NONE);
        lp_gettypehint(ps);  /* 解析 take 解构变量的类型注解 */
      } while (lp_testnext(ps, ','));
      lp_checknext(ps, '}');
    }

    /* 解析 = expr */
    if (lp_testnext(ps, '=')) {
      source = parse_expr(ps);
    }

    return ast_new_stmt_take(ps->pool, nvars, varnames, NULL, source, 0, line);
  }

  /* 处理 async function */
  int is_async = 0;
  if (lp_testnext(ps, TK_ASYNC)) {
    is_async = 1;
  }

  if (lp_testnext(ps, TK_FUNCTION)) {
    TString *name = lp_checkname(ps);
    {
      int fline = ls->linenumber;
      AstFunc *f = parse_funcbody(ps, fline, 0, 0, is_async);
      AstStmt *s = ast_new_stmt_localfunc(ps->pool, name, f, line);
      s->u.localfunc.local_idx = scope_find_local(ps, name);
      if (s->u.localfunc.local_idx < 0) {
        scope_add_local(ps, name, AST_ATTR_NONE);
        s->u.localfunc.local_idx = ps->curfunc->nlocals - 1;
      }
      return s;
    }
  } else {
    return parse_local_var_list(ps, 0);
  }
}


/**
 * @brief 解析if语句
 * @param ps 解析器状态
 * @return 语句节点
 */
static AstStmt *parse_if_stat(ParserState *ps) {
  LexState *ls = ps->ls;
  int line = ls->linenumber;
  AstStmt *s = ast_new_stmt_if(ps->pool, line);
  int arm_cap = 2;
  int narms = 0;
  AstIfArm *arms = cast(AstIfArm *,
    ast_pool_alloc(ps->pool, arm_cap * sizeof(AstIfArm)));

  lp_next(ps); /* skip 'if' */
  for (;;) {
    AstIfArm *arm;
    if (narms >= arm_cap) {
      int new_cap = arm_cap * 2;
      AstIfArm *new_arms = cast(AstIfArm *,
        ast_pool_alloc(ps->pool, new_cap * sizeof(AstIfArm)));
      memcpy(new_arms, arms, narms * sizeof(AstIfArm));
      arms = new_arms;
      arm_cap = new_cap;
    }
    arm = &arms[narms++];
    memset(arm, 0, sizeof(*arm));

    /* 处理 if let 语法 */
    if (lp_testnext(ps, TK_LET)) {
      TString *name = lp_checkname(ps);
      lp_checknext(ps, '=');
      arm->cond = parse_expr(ps);
      arm->let_var = name;
      scope_add_local(ps, name, AST_ATTR_NONE);
    } else {
      arm->cond = parse_expr(ps);
    }

    lp_checknext(ps, TK_THEN);
    scope_push(ps, 0);
    parse_block(ps, &arm->body);
    scope_pop(ps);

    if (lp_testnext(ps, TK_ELSEIF)) continue;
    if (lp_testnext(ps, TK_ELSE)) {
      s->u.ifstmt.has_else = 1;
      scope_push(ps, 0);
      parse_block(ps, &s->u.ifstmt.else_body);
      scope_pop(ps);
    }
    break;
  }
  lp_checknext(ps, TK_END);

  s->u.ifstmt.arms = arms;
  s->u.ifstmt.narms = narms;
  return s;
}


/**
 * @brief 解析while语句
 * @param ps 解析器状态
 * @return 语句节点
 */
static AstStmt *parse_while_stat(ParserState *ps) {
  LexState *ls = ps->ls;
  int line = ls->linenumber;
  AstExpr *cond;
  AstStmt *s;
  lp_next(ps); /* skip 'while' */

  /* 检查 while let 语法 */
  if (lp_check(ps, TK_LET)) {
    return parse_while_let_stat(ps, line);
  }

  /* 普通 while 循环 */
  cond = parse_expr(ps);
  lp_checknext(ps, TK_DO);
  s = ast_new_stmt_while(ps->pool, cond, line);
  scope_push(ps, 1);
  parse_block(ps, &s->u.whilestmt.body);
  scope_pop(ps);
  /* 解析 while...else 的 else 块 */
  if (lp_testnext(ps, TK_ELSE)) {
    s->u.whilestmt.has_else = 1;
    parse_block(ps, &s->u.whilestmt.else_body);
  }
  lp_checknext(ps, TK_END);
  return s;
}


/**
 * @brief 解析 while let name {, name} = expr do body end 语法
 * 参考 lparser.c whilestat 第6184-6233行
 * 语义：创建局部变量，循环直到所有变量为nil/false
 * @param ps 解析器状态
 * @param line while关键字的行号
 * @return 语句节点
 */
static AstStmt *parse_while_let_stat(ParserState *ps, int line) {
  LexState *ls = ps->ls;
  int nvars = 0;
  TString **names = NULL;
  AstExpr *expr;
  AstStmt *s;

  lp_next(ps); /* skip 'let' */

  /* 解析变量名列表 */
  names = cast(TString **, ast_pool_alloc(ps->pool, 4 * sizeof(TString *)));
  names[nvars++] = lp_checkname(ps);
  while (lp_testnext(ps, ',')) {
    names[nvars++] = lp_checkname(ps);
  }

  /* 解析 = 号和表达式 */
  lp_checknext(ps, '=');
  expr = parse_expr(ps);

  /* 创建 while let 语句节点 */
  s = ast_new_stmt_while_let(ps->pool, nvars, names, expr, line);

  /* 解析 do body end，do 关键字可选 */
  if (lp_testnext(ps, TK_DO)) {
    /* skipped 'do' */
  }

  /* 为局部变量压入作用域 */
  scope_push(ps, 1);
  {
    int i;
    for (i = 0; i < nvars; i++) {
      scope_add_local(ps, names[i], AST_ATTR_NONE);
    }
  }
  parse_block(ps, &s->u.whilelet.body);
  scope_pop(ps);

  /* 解析 while...else 的 else 块 */
  if (lp_testnext(ps, TK_ELSE)) {
    s->u.whilelet.has_else = 1;
    parse_block(ps, &s->u.whilelet.else_body);
  }
  lp_checknext(ps, TK_END);
  return s;
}


/**
 * @brief 解析repeat语句
 * @param ps 解析器状态
 * @return 语句节点
 */
static AstStmt *parse_repeat_stat(ParserState *ps) {
  LexState *ls = ps->ls;
  int line = ls->linenumber;
  AstStmt *s = ast_new_stmt_repeat(ps->pool, line);
  lp_next(ps); /* skip 'repeat' */
  scope_push(ps, 1);
  parse_block(ps, &s->u.whilestmt.body);
  scope_pop(ps);
  lp_checknext(ps, TK_UNTIL);
  s->u.whilestmt.cond = parse_expr(ps);
  return s;
}


/**
 * @brief 解析return语句
 * @param ps 解析器状态
 * @return 语句节点
 */
static AstStmt *parse_return_stat(ParserState *ps) {
  LexState *ls = ps->ls;
  int line = ls->linenumber;
  int nvalues = 0;
  AstExpr **values = NULL;
  lp_next(ps); /* skip 'return' */
  LOGD("[parse] RETURN_STAT: line=%d, next_token=%d, lookahead=%d\n",
       line, ls->t.token, luaX_lookahead(ls));

  if (!lp_check(ps, TK_END) && !lp_check(ps, TK_ELSE) && !lp_check(ps, TK_ELSEIF) &&
      !lp_check(ps, TK_UNTIL) && !lp_check(ps, ';') && !lp_check(ps, TK_EOS)) {
    LOGD("[parse] RETURN_STAT: parsing exprlist, nvalues=%d\n", nvalues);
    values = parse_exprlist(ps, &nvalues);
    LOGD("[parse] RETURN_STAT: exprlist done, nvalues=%d\n", nvalues);
  }
  lp_testnext(ps, ';');
  {
    AstStmt *s = ast_new_stmt_return(ps->pool, nvalues, line);
    s->u.retstmt.values = values;
    return s;
  }
}


/**
 * @brief 从复合赋值token获取对应的二元运算符
 * @param token 当前token值
 * @return AstBinOp枚举值，非复合赋值返回-1
 */
static int get_compound_binop(int token) {
  switch (token) {
    case TK_ADDEQ:    return AST_BIN_ADD;
    case TK_SUBEQ:    return AST_BIN_SUB;
    case TK_MULEQ:    return AST_BIN_MUL;
    case TK_DIVEQ:    return AST_BIN_DIV;
    case TK_IDIVEQ:   return AST_BIN_IDIV;
    case TK_MODEQ:    return AST_BIN_MOD;
    case TK_POWEQ:    return AST_BIN_POW;
    case TK_BANDEQ:   return AST_BIN_BAND;
    case TK_BOREQ:    return AST_BIN_BOR;
    case TK_BXOREQ:   return AST_BIN_BXOR;
    case TK_SHLEQ:    return AST_BIN_SHL;
    case TK_SHREQ:    return AST_BIN_SHR;
    case TK_CONCATEQ: return AST_BIN_CONCAT;
    case TK_NULLCOALEQ: return AST_BIN_NULLCOAL;
    case TK_ANDANDEQ: return AST_BIN_AND;
    case TK_OROREQ:   return AST_BIN_OR;
    case TK_MERGE:    return AST_BIN_MERGE;
    default: return -1;
  }
}


/**
 * @brief 将表达式转换为赋值目标（仅支持变量名和索引表达式）
 * @param ps 解析器状态
 * @param e 要转换的表达式
 * @param tgt [out] 输出的赋值目标结构
 */
static void expr_to_target(ParserState *ps, AstExpr *e, AstAssignTarget *tgt) {
  if (e->kind == AST_EXPR_IDENT) {
    TString *name = e->u.strval;
    int idx = scope_find_local(ps, name);
    tgt->kind = AST_TGT_VAR;
    tgt->as.var.name = name;
    if (idx >= 0) {
      tgt->as.var.var_kind = AST_VAR_LOCAL;
      tgt->as.var.idx = idx;
    } else {
      tgt->as.var.var_kind = AST_VAR_GLOBAL;
      tgt->as.var.idx = -1;
    }
  } else if (e->kind == AST_EXPR_INDEX) {
    tgt->kind = AST_TGT_INDEX;
    tgt->as.index.table = e->u.index.table;
    tgt->as.index.key = e->u.index.key;
  } else {
    lp_error(ps, "syntax error: invalid assignment target");
  }
}


/**
 * @brief 解析for语句（数值for和泛型for）
 * @param ps 解析器状态
 * @return 语句节点
 *
 * 语法：
 *   数值for: for NAME = expr, expr [, expr] do block end
 *   泛型for: for namelist in explist do block end
 */
static AstStmt *parse_for_stat(ParserState *ps) {
  LexState *ls = ps->ls;
  int line = ls->linenumber;
  AstStmt *s;
  TString *varname;

  lp_next(ps); /* skip 'for' */
  varname = lp_checkname(ps);

  if (lp_check(ps, '=')) {
    /* 数值for: for NAME = expr, expr [, expr] do block end */
    AstExpr *start, *stop, *step = NULL;
    lp_next(ps); /* skip '=' */
    start = parse_expr(ps);
    lp_checknext(ps, ',');
    stop = parse_expr(ps);
    if (lp_testnext(ps, ',')) {
      step = parse_expr(ps);
    }
    lp_checknext(ps, TK_DO);
    s = ast_new_stmt_fornum(ps->pool, varname, start, stop, step, line);
    scope_push(ps, 1);
    scope_add_local(ps, varname, AST_ATTR_NONE);
    parse_block(ps, &s->u.fornum.body);
    scope_pop(ps);
    /* 解析 for...else 的 else 块 */
    if (lp_testnext(ps, TK_ELSE)) {
      s->u.fornum.has_else = 1;
      parse_block(ps, &s->u.fornum.else_body);
    }
    lp_checknext(ps, TK_END);
  }
  else if (lp_check(ps, ',') || lp_check(ps, TK_IN)) {
    /* 泛型for: for namelist in explist do block end */
    int nnames = 0;
    int nnames_cap = 4;
    int nexprs = 0;
    TString **names = cast(TString **,
      ast_pool_alloc(ps->pool, nnames_cap * sizeof(TString *)));

    names[nnames++] = varname;
    while (lp_testnext(ps, ',')) {
      if (nnames >= nnames_cap) {
        int new_cap = nnames_cap * 2;
        TString **new_names = cast(TString **,
          ast_pool_alloc(ps->pool, new_cap * sizeof(TString *)));
        memcpy(new_names, names, nnames * sizeof(TString *));
        names = new_names;
        nnames_cap = new_cap;
      }
      names[nnames++] = lp_checkname(ps);
    }

    if (lp_check(ps, TK_IN)) lp_next(ps); /* skip 'in' */

    {
      AstExpr **exprs = parse_exprlist(ps, &nexprs);
      lp_checknext(ps, TK_DO);
      s = ast_new_stmt_forgen(ps->pool, nnames, nexprs, line);
      s->u.forgen.names = names;
      s->u.forgen.exprs = exprs;
      scope_push(ps, 1);
      {
        int i;
        for (i = 0; i < nnames; i++) {
          scope_add_local(ps, names[i], AST_ATTR_NONE);
        }
      }
      parse_block(ps, &s->u.forgen.body);
      scope_pop(ps);
      /* 解析 for...else 的 else 块 */
      if (lp_testnext(ps, TK_ELSE)) {
        s->u.forgen.has_else = 1;
        parse_block(ps, &s->u.forgen.else_body);
      }
      lp_checknext(ps, TK_END);
    }
  }
  else {
    lp_error(ps, "'=' or 'in' expected after 'for' variable");
    s = NULL;
  }

  return s;
}


/* ============================================================
 *                    泛型工厂函数支持
 * ============================================================ */

/**
 * @brief 检测是否为泛型工厂函数语法：function<T> 或 function<T, U>
 * 使用 lookahead 检查：'function' 后跟 '<'，然后是类型名，再是 '>' 或 ',' 或 ':'
 * @param ps 解析器状态
 * @return 1 表示是泛型工厂，0 表示不是
 */
static int lp_is_generic_factory(ParserState *ps) {
  LexState *ls = ps->ls;
  /* 当前 token 必须是 '<' */
  if (ls->t.token != '<') return 0;
  /* lookahead 必须是类型名 */
  {
    int la1 = lp_lookahead(ps);
    if (!is_nametoken(la1)) return 0;
  }
  /* lookahead2 必须是 '>' 或 ',' 或 ':' */
  {
    int la2 = lp_lookahead2(ps);
    if (la2 == '>' || la2 == ',' || la2 == ':') return 1;
  }
  return 0;
}


/**
 * @brief 解析泛型工厂函数体：<T, U>(params) => expression
 * 语法糖形式：function<T>(x: T): T => x  等价于 function(x) return x end
 * 泛型参数 <T, U> 中的类型参数名记录在函数节点的 generic_params 中
 * @param ps 解析器状态
 * @param is_async 是否为 async 函数
 * @return 表达式语句（匿名函数表达式包装为语句）
 */
static AstStmt *lp_parse_generic_arrow_body(ParserState *ps, int is_async) {
  LexState *ls = ps->ls;
  int line = ls->linenumber;
  AstStmt *s;
  AstExpr *func_expr;
  AstExpr *body_expr;

  int func_idx = ps->func_idx_counter++;
  int parent_idx = ps->curfunc ? ps->curfunc->func_idx : -1;
  AstFunc *f;
  int nparams = 0;
  int param_cap = 4;
  AstFuncParam *params;
  int ngeneric = 0;
  int generic_cap = 4;
  TString **generic_names;
  TypeHint **generic_constraints;

  /* 创建函数节点 */
  f = ast_new_func(ps->pool, func_idx, parent_idx, line);
  f->source = ps->ls->source;
  f->is_async = is_async;
  f->is_vararg = 0;
  ast_chunk_add_func(ps->chunk, f);

  generic_names = cast(TString **,
    ast_pool_alloc(ps->pool, generic_cap * sizeof(TString *)));
  generic_constraints = cast(TypeHint **,
    ast_pool_alloc(ps->pool, generic_cap * sizeof(TypeHint *)));

  /* 解析泛型参数 <T, U, ...> */
  lp_next(ps); /* skip '<' */
  do {
    TString *gname = lp_checkname(ps);
    /* 可选的类型约束 : SomeType */
    if (lp_testnext(ps, ':')) {
      TypeHint *th = lp_typehint_new(ps);
      lp_checktypehint(ps, th);
      generic_constraints[ngeneric] = th;
    } else {
      generic_constraints[ngeneric] = NULL;
    }
    if (ngeneric >= generic_cap) {
      int new_cap = generic_cap * 2;
      TString **new_names = cast(TString **,
        ast_pool_alloc(ps->pool, new_cap * sizeof(TString *)));
      TypeHint **new_constraints = cast(TypeHint **,
        ast_pool_alloc(ps->pool, new_cap * sizeof(TypeHint *)));
      memcpy(new_names, generic_names, ngeneric * sizeof(TString *));
      memcpy(new_constraints, generic_constraints, ngeneric * sizeof(TypeHint *));
      generic_names = new_names;
      generic_constraints = new_constraints;
      generic_cap = new_cap;
    }
    generic_names[ngeneric++] = gname;
  } while (lp_testnext(ps, ','));
  lp_checknext(ps, '>');

  /* 存储泛型参数 */
  f->generic_params = generic_names;
  f->ngeneric_params = ngeneric;
  f->generic_constraints = generic_constraints;

  /* 解析函数参数列表 (x: T, y: U) */
  params = cast(AstFuncParam *,
    ast_pool_alloc(ps->pool, param_cap * sizeof(AstFuncParam)));

  lp_checknext(ps, '(');
  if (!lp_check(ps, ')')) {
    for (;;) {
      if (lp_testnext(ps, TK_DOTS)) {
        f->is_vararg = 1;
        if (is_nametoken(ps->ls->t.token)) {
          f->vararg_name = lp_checkname(ps);
        }
        break;
      }
      {
        TString *name = lp_checkname(ps);
        int attr = AST_ATTR_NONE;
        AstFuncParam *param;
        if (lp_testnext(ps, TK_CONST)) attr = AST_ATTR_CONST;
        TypeHint *type_hint = lp_gettypehint(ps);
        if (nparams >= param_cap) {
          int new_cap = param_cap * 2;
          AstFuncParam *new_params = cast(AstFuncParam *,
            ast_pool_alloc(ps->pool, new_cap * sizeof(AstFuncParam)));
          memcpy(new_params, params, nparams * sizeof(AstFuncParam));
          params = new_params;
          param_cap = new_cap;
        }
        param = &params[nparams++];
        param->name = name;
        param->default_value = NULL;
        if (lp_testnext(ps, '=')) {
          param->default_value = parse_expr(ps);
        }
        param->attr = attr;
        param->type_hint = type_hint;
      }
      if (!lp_testnext(ps, ',')) break;
    }
  }
  lp_checknext(ps, ')');

  f->nparams = nparams;
  f->params = params;
  f->nlocals = nparams;
  f->return_type_hint = lp_gettypehint(ps);

  /* 检查函数属性 <nodiscard> */
  if (lp_testnext(ps, '<')) {
    if (is_nametoken(ps->ls->t.token)) {
      const char *attr = getstr(ps->ls->t.seminfo.ts);
      if (strcmp(attr, "nodiscard") == 0) {
        f->nodiscard = 1;
      }
      lp_next(ps);
    }
    lp_checknext(ps, '>');
  }

  /* 解析箭头函数体 */
  {
    AstFunc *oldfunc = ps->curfunc;
    ps->curfunc = f;
    scope_push(ps, 0);
    {
      int i;
      for (i = 0; i < nparams; i++) {
        scope_add_local(ps, params[i].name, params[i].attr);
      }
    }

    if (lp_testnext(ps, TK_MEAN)) {
      /* => expression 或 => { ... } */
      if (lp_check(ps, '{')) {
        /* 语句块体 */
        lp_next(ps); /* skip '{' */
        while (!lp_check(ps, '}') && !lp_check(ps, TK_EOS)) {
          AstStmt *stmt = parse_stat(ps);
          if (stmt != NULL) {
            ast_block_add_stmt(ps->pool, &f->body, stmt);
          }
          lp_testnext(ps, ';');
        }
        lp_checknext(ps, '}');
      } else {
        /* 表达式体 */
        body_expr = parse_expr(ps);
        {
          AstStmt *ret = ast_new_stmt_return(ps->pool, 1, line);
          ret->u.retstmt.values[0] = body_expr;
          ast_block_add_stmt(ps->pool, &f->body, ret);
        }
      }
    } else {
      lp_error(ps, "expected '=>' after generic arrow function parameters");
    }

    scope_pop(ps);
    ps->curfunc = oldfunc;
  }

  f->node.line = line;
  func_expr = ast_new_expr_func(ps->pool, f, 1, line);

  /* 包装为表达式语句 */
  s = ast_new_stmt_expr(ps->pool, func_expr, line);
  return s;
}


/**
 * @brief 解析函数定义语句（非表达式）
 * @param ps 解析器状态
 * @return 语句节点
 *
 * 语法：function funcname funcbody
 *   funcname -> NAME {'.' NAME} [':' NAME]
 */
static AstStmt *parse_func_stat(ParserState *ps, int is_async) {
  LexState *ls = ps->ls;
  int line = ls->linenumber;
  AstStmt *s;
  int ismethod = 0;
  TString *fname;
  AstExpr *v;
  AstFunc *f;
  AstExpr *func_expr;

  lp_next(ps); /* skip 'function' */

  /* 检查是否为泛型工厂函数：function<T> 或 function<T, U> */
  if (lp_is_generic_factory(ps)) {
    return lp_parse_generic_arrow_body(ps, is_async);
  }

  /* 解析 funcname: NAME {. NAME} [: NAME] */
  fname = lp_checkname(ps);
  v = ast_new_expr_ident(ps->pool, fname, line);
  while (lp_testnext(ps, '.')) {
    TString *key = lp_checkfieldname(ps);
    int kline = ls->linenumber;
    AstExpr *keyexpr = ast_new_expr_str(ps->pool, key, AST_EXPR_STRING, kline);
    v = ast_new_expr_index(ps->pool, v, keyexpr, 0, line);
  }
  if (lp_testnext(ps, ':')) {
    TString *method = lp_checkfieldname(ps);
    int mline = ls->linenumber;
    AstExpr *keyexpr = ast_new_expr_str(ps->pool, method, AST_EXPR_STRING, mline);
    v = ast_new_expr_index(ps->pool, v, keyexpr, 0, line);
    ismethod = 1;
  }

  /* 解析函数体 */
  f = parse_funcbody(ps, ls->linenumber, 0, ismethod, is_async);
  func_expr = ast_new_expr_func(ps->pool, f, 0, line);

  /* 构建赋值语句：目标是v，值是func_expr */
  {
    AstAssignTarget tgt;
    AstExpr **values;
    expr_to_target(ps, v, &tgt);
    values = cast(AstExpr **, ast_pool_alloc(ps->pool, sizeof(AstExpr *)));
    values[0] = func_expr;
    s = ast_new_stmt_assign(ps->pool, 1, 1, line);
    s->u.assign.targets = cast(AstAssignTarget *,
      ast_pool_alloc(ps->pool, sizeof(AstAssignTarget)));
    s->u.assign.targets[0] = tgt;
    s->u.assign.values = values;
  }

  return s;
}


/* ============================================================
 *            LXCLUA 特殊语法解析函数
 * ============================================================ */

/**
 * @brief 解析测试表达式的值部分，支持链式操作符
 * 处理: [ expr ], [ expr -op expr ], [ expr = expr ], [ cond1 -a cond2 ], [ cond1 -o cond2 ]
 * -a 优先级高于 -o: -a 的右操作数不包含 -o 链
 * @param ps 解析器状态
 * @param allow_or 是否允许解析 -o 操作符（-a 递归调用时传 0）
 * @return 表达式节点
 */
static AstExpr *parse_test_value(ParserState *ps, int allow_or) {
  LexState *ls = ps->ls;
  int line = ls->linenumber;

  /* 解析第一个值表达式（使用 simpleexpr，不处理二元运算符） */
  AstExpr *v = parse_simpleexpr(ps);

  /* 内层循环: 处理比较操作符和 -a (AND) */
  for (;;) {
    /* 字符串比较: =, ==, != */
    if (ls->t.token == '=') {
      lp_next(ps);
      AstExpr *e2 = parse_simpleexpr(ps);
      v = ast_new_expr_binop(ps->pool, AST_BIN_EQ, v, e2, line);
      continue;
    }
    if (ls->t.token == TK_EQ) {
      lp_next(ps);
      AstExpr *e2 = parse_simpleexpr(ps);
      v = ast_new_expr_binop(ps->pool, AST_BIN_EQ, v, e2, line);
      continue;
    }
    if (ls->t.token == TK_NE) {
      lp_next(ps);
      AstExpr *e2 = parse_simpleexpr(ps);
      v = ast_new_expr_binop(ps->pool, AST_BIN_NE, v, e2, line);
      continue;
    }

    /* 二元比较操作符: -eq, -ne, -gt, -lt, -ge, -le */
    /* 逻辑操作符: -a (AND) */
    if (ls->t.token == '-') {
      int la = lp_lookahead(ps);
      if (la == TK_NAME) {
        const char *opname = getstr(ls->lookahead.seminfo.ts);

        /* -o 始终留给外层循环处理（优先级最低） */
        if (strcmp(opname, "o") == 0) {
          break;
        }

        lp_next(ps); /* skip '-' */
        lp_next(ps); /* skip operator name */

        AstBinOp binop;
        if (strcmp(opname, "eq") == 0) binop = AST_BIN_EQ;
        else if (strcmp(opname, "ne") == 0) binop = AST_BIN_NE;
        else if (strcmp(opname, "gt") == 0) binop = AST_BIN_GT;
        else if (strcmp(opname, "lt") == 0) binop = AST_BIN_LT;
        else if (strcmp(opname, "ge") == 0) binop = AST_BIN_GE;
        else if (strcmp(opname, "le") == 0) binop = AST_BIN_LE;
        else if (strcmp(opname, "a") == 0) binop = AST_BIN_AND;
        else {
          lp_error(ps, "unsupported operator in test expression");
          break;
        }

        AstExpr *e2;
        if (strcmp(opname, "a") == 0) {
          /* -a 右操作数递归解析，不包含 -o */
          e2 = parse_test_value(ps, 0);
        } else {
          e2 = parse_simpleexpr(ps);
        }
        v = ast_new_expr_binop(ps->pool, binop, v, e2, line);
        continue;
      }
      /* 不是操作符名，可能是负数，退出循环 */
      break;
    }
    break;
  }

  /* 外层循环: 处理 -o (OR)，优先级最低 */
  if (allow_or) {
    while (ls->t.token == '-') {
      int la = lp_lookahead(ps);
      if (la == TK_NAME) {
        const char *opname = getstr(ls->lookahead.seminfo.ts);
        if (strcmp(opname, "o") == 0) {
          lp_next(ps); /* skip '-' */
          lp_next(ps); /* skip 'o' */
          AstExpr *e2 = parse_test_value(ps, 0);
          v = ast_new_expr_binop(ps->pool, AST_BIN_OR, v, e2, line);
          continue;
        }
      }
      break;
    }
  }

  return v;
}


/**
 * @brief 解析 [ 条件测试表达式 ] 或 map 字面量 [key = val, ...]
 * @param ps 解析器状态
 * @return 表达式节点
 */
static AstExpr *parse_test_or_map(ParserState *ps) {
  LexState *ls = ps->ls;
  int line = ls->linenumber;
  lp_next(ps); /* skip '[' */

  /* 空 map: [] */
  if (lp_check(ps, ']')) {
    lp_next(ps);
    return ast_new_expr_map(ps->pool, NULL, 0, line);
  }

  /* 检测是否为 map 字面量: [name = val] 或 [[expr] = val] */
  {
    int la = ls->t.token;
    int is_map = 0;

    if (la == '[') {
      /* map with [expr] key: [[expr] = val, ...] */
      is_map = 1;
    }
    else if (la == TK_NAME || is_nametoken(la)) {
      int la2 = lp_lookahead(ps);
      if (la2 == '=') {
        /* map with name sugar: [name = val, ...] */
        is_map = 1;
      }
    }

    if (is_map) {
      /* 解析 map 字面量 */
      int cap = 4;
      int n = 0;
      AstMapEntry *entries = cast(AstMapEntry *,
        ast_pool_alloc(ps->pool, cap * sizeof(AstMapEntry)));

      do {
        AstMapEntry *entry;
        if (n >= cap) {
          int new_cap = cap * 2;
          AstMapEntry *new_entries = cast(AstMapEntry *,
            ast_pool_alloc(ps->pool, new_cap * sizeof(AstMapEntry)));
          memcpy(new_entries, entries, n * sizeof(AstMapEntry));
          entries = new_entries;
          cap = new_cap;
        }
        entry = &entries[n++];
        memset(entry, 0, sizeof(*entry));

        if (lp_check(ps, '[')) {
          /* [expr] = value: 计算键的表达式 */
          lp_next(ps);  /* 跳过 '[' */
          entry->key = parse_expr(ps);
          lp_checknext(ps, ']');
          lp_checknext(ps, '=');
        }
        else {
          /* name = value 语法糖 (等价于 ["name"] = value) */
          TString *name = lp_checkname(ps);
          entry->key = ast_new_expr_str(ps->pool, name, AST_EXPR_STRING, line);
          lp_checknext(ps, '=');
        }
        entry->value = parse_expr(ps);
      } while (lp_testnext(ps, ',') || lp_testnext(ps, ';'));

      lp_checknext(ps, ']');
      return ast_new_expr_map(ps->pool, entries, n, line);
    }
  }

  /* 条件测试表达式 */
  {
    int has_not = 0;
    if (ls->t.token == '!' || ls->t.token == TK_NOT) {
      has_not = 1;
      lp_next(ps);
    }

    AstExpr *e;

    /* 处理 '-' 前缀的一元测试操作符 */
    if (ls->t.token == '-') {
      lp_next(ps);
      if (ls->t.token == TK_NAME || ls->t.token == TK_NIL || ls->t.token == TK_BOOL) {
        /* type/string 测试操作符: -z, -n, -nil, -bool, -func, -type */
        const char *opname = getstr(ls->t.seminfo.ts);
        lp_next(ps);

        /* 字符串测试: [-z expr], [-n expr] */
        if (strcmp(opname, "z") == 0) {
          e = ast_new_expr_unop(ps->pool, AST_UN_TEST_Z, parse_expr(ps), line);
        } else if (strcmp(opname, "n") == 0) {
          e = ast_new_expr_unop(ps->pool, AST_UN_TEST_N, parse_expr(ps), line);
        }
        /* 类型测试: [-nil expr], [-bool expr], [-func expr] */
        else if (strcmp(opname, "nil") == 0) {
          e = ast_new_expr_unop(ps->pool, AST_UN_TEST_NIL, parse_expr(ps), line);
        } else if (strcmp(opname, "bool") == 0) {
          e = ast_new_expr_unop(ps->pool, AST_UN_TEST_BOOL, parse_expr(ps), line);
        } else if (strcmp(opname, "func") == 0) {
          e = ast_new_expr_unop(ps->pool, AST_UN_TEST_FUNC, parse_expr(ps), line);
        }
        /* 类型测试: [-type expr "typename"] */
        else if (strcmp(opname, "type") == 0) {
          AstExpr *operand = parse_expr(ps);
          if (ls->t.token == TK_STRING || ls->t.token == TK_RAWSTRING ||
              ls->t.token == TK_INTERPSTRING) {
            TString *type_name = ls->t.seminfo.ts;
            lp_next(ps);
            e = ast_new_expr_test_type(ps->pool, operand, type_name, line);
          } else {
            lp_error(ps, "expected type name string after -type");
            e = NULL;
          }
        }
        /* 比较操作符缺少左操作数: [-eq expr] 等 */
        else if (strcmp(opname, "eq") == 0 || strcmp(opname, "ne") == 0 ||
                 strcmp(opname, "gt") == 0 || strcmp(opname, "lt") == 0 ||
                 strcmp(opname, "ge") == 0 || strcmp(opname, "le") == 0) {
          lp_error(ps, luaO_pushfstring(ps->L,
            "comparison operator '-%s' requires left operand", opname));
          e = NULL;
        }
        /* 逻辑操作符缺少左操作数: [-a expr], [-o expr] */
        else if (strcmp(opname, "a") == 0 || strcmp(opname, "o") == 0) {
          lp_error(ps, luaO_pushfstring(ps->L,
            "logical operator '-%s' requires left operand", opname));
          e = NULL;
        }
        /* 不支持的扩展操作符 */
        else {
          lp_error(ps, luaO_pushfstring(ps->L,
            "unsupported test operator '-%s' in [ ... ] syntax", opname));
          e = NULL;
        }
      } else {
        lp_error(ps, "expected operator name after '-' in test expression");
        e = NULL;
      }
    } else {
      /* 值表达式: 支持链式操作符 */
      e = parse_test_value(ps, 1);
      /* 转换为布尔值：条件表达式 [ val ] 应返回布尔值 */
      e = ast_new_expr_unop(ps->pool, AST_UN_NOT, e, line);
      e = ast_new_expr_unop(ps->pool, AST_UN_NOT, e, line);
    }

    if (has_not) {
      e = ast_new_expr_unop(ps->pool, AST_UN_NOT, e, line);
    }

    lp_checknext(ps, ']');
    return e;
  }
}


/**
 * @brief 解析字典推导式: {for k,v in expr do/yield k_expr, v_expr if cond}
 *
 * 创建匿名子函数，其内部逻辑等价于：
 *   local _t = {}
 *   for k, v in expr do
 *     if cond then _t[key_expr] = val_expr end
 *   end
 *   return _t
 *
 * @param ps 解析器状态
 * @return 表达式节点（AST_EXPR_DICT_COMP）
 */
static AstExpr *parse_dict_comprehension(ParserState *ps) {
  LexState *ls = ps->ls;
  int line = ls->linenumber;
  int func_idx = ps->func_idx_counter++;
  int parent_idx = ps->curfunc ? ps->curfunc->func_idx : -1;

  lp_next(ps); /* skip '{' */
  lp_checknext(ps, TK_FOR); /* skip 'for' */

  /* 创建匿名子函数 */
  AstFunc *f = ast_new_func(ps->pool, func_idx, parent_idx, line);
  f->source = ps->ls->source;
  f->nparams = 0;
  f->is_vararg = 0;
  ast_chunk_add_func(ps->chunk, f);

  /* 切换到子函数作用域 */
  AstFunc *oldfunc = ps->curfunc;
  ps->curfunc = f;
  scope_push(ps, 0);

  /* 添加 _t 局部变量 */
  TString *t_name = luaS_newliteral(ps->L, "_t");
  scope_add_local(ps, t_name, AST_ATTR_NONE);
  f->nlocals = 1;

  /* 创建 local _t = {} 语句 */
  {
    AstStmt *local_stmt = ast_new_stmt_local(ps->pool, 1, NULL, 1, line);
    local_stmt->u.local.names[0] = t_name;
    local_stmt->u.local.attrs[0] = AST_ATTR_NONE;
    AstExpr *empty_table = ast_new_expr_table(ps->pool, NULL, 0, line);
    local_stmt->u.local.values = cast(AstExpr **,
      ast_pool_alloc(ps->pool, sizeof(AstExpr *)));
    local_stmt->u.local.values[0] = empty_table;
    ast_block_add_stmt(ps->pool, &f->body, local_stmt);
  }

  /* 解析循环变量名列表 */
  int nvars = 0;
  int nvars_cap = 4;
  TString **loop_vars = cast(TString **,
    ast_pool_alloc(ps->pool, nvars_cap * sizeof(TString *)));
  do {
    if (nvars >= nvars_cap) {
      int new_cap = nvars_cap * 2;
      TString **new_vars = cast(TString **,
        ast_pool_alloc(ps->pool, new_cap * sizeof(TString *)));
      memcpy(new_vars, loop_vars, nvars * sizeof(TString *));
      loop_vars = new_vars;
      nvars_cap = new_cap;
    }
    loop_vars[nvars++] = lp_checkname(ps);
  } while (lp_testnext(ps, ','));

  /* 解析 in 关键字和迭代器表达式 */
  lp_checknext(ps, TK_IN);
  int nexprs = 0;
  AstExpr **iter_exprs = parse_exprlist(ps, &nexprs);

  /* 解析 do 或 yield 关键字 */
  if (ls->t.token == TK_DO) {
    lp_next(ps);
  } else if (ls->t.token == TK_NAME && strcmp(getstr(ls->t.seminfo.ts), "yield") == 0) {
    lp_next(ps);
  } else {
    lp_error(ps, "expected 'do' or 'yield' in dict comprehension");
  }

  /* 解析 key 表达式 */
  AstExpr *key_expr = parse_expr(ps);
  lp_checknext(ps, ',');

  /* 解析 value 表达式 */
  AstExpr *val_expr = parse_expr(ps);

  /* 解析可选的 if 条件 */
  AstExpr *if_cond = NULL;
  if (lp_testnext(ps, TK_IF)) {
    if_cond = parse_expr(ps);
  }

  lp_checknext(ps, '}');

  /* 构建 for-generic 循环体 */
  AstStmt *for_stmt = ast_new_stmt_forgen(ps->pool, nvars, nexprs, line);
  for_stmt->u.forgen.names = loop_vars;
  for_stmt->u.forgen.exprs = iter_exprs;

  /* 构建循环体: 可能包含 if 条件和表赋值 */
  AstBlock *loop_body = &for_stmt->u.forgen.body;
  block_init(ps, loop_body);
  f->nlocals += nvars;  /* 循环变量计入局部变量数 */

  /* 构建表赋值: _t[key_expr] = val_expr */
  AstStmt *assign_stmt = ast_new_stmt_assign(ps->pool, 1, 1, line);
  AstAssignTarget *tgt = &assign_stmt->u.assign.targets[0];
  tgt->kind = AST_TGT_INDEX;
  tgt->as.index.table = ast_new_expr_ident(ps->pool, t_name, line);
  tgt->as.index.key = key_expr;
  assign_stmt->u.assign.values[0] = val_expr;

  if (if_cond != NULL) {
    /* 有 if 条件: 创建 if 语句包裹赋值 */
    AstStmt *if_stmt = ast_new_stmt_if(ps->pool, line);
    AstIfArm *arm = ast_new_ifarm(ps->pool, if_cond, line);
    ast_block_add_stmt(ps->pool, &arm->body, assign_stmt);
    if_stmt->u.ifstmt.arms = arm;
    if_stmt->u.ifstmt.narms = 1;
    ast_block_add_stmt(ps->pool, loop_body, if_stmt);
  } else {
    ast_block_add_stmt(ps->pool, loop_body, assign_stmt);
  }

  /* 将 for-generic 语句添加到函数体 */
  ast_block_add_stmt(ps->pool, &f->body, for_stmt);

  /* 创建 return _t 语句 */
  {
    AstStmt *ret_stmt = ast_new_stmt_return(ps->pool, 1, line);
    AstExpr *t_ident = ast_new_expr_ident(ps->pool, t_name, line);
    ret_stmt->u.retstmt.values = cast(AstExpr **,
      ast_pool_alloc(ps->pool, sizeof(AstExpr *)));
    ret_stmt->u.retstmt.values[0] = t_ident;
    ast_block_add_stmt(ps->pool, &f->body, ret_stmt);
  }

  /* 恢复作用域 */
  scope_pop(ps);
  ps->curfunc = oldfunc;

  /* 创建推导式表达式节点 */
  AstExpr *e = ast_new_node(ps->pool, AstExpr, AST_EXPR, line);
  e->kind = AST_EXPR_DICT_COMP;
  e->u.func.func = f;
  return e;
}


/**
 * @brief 解析列表推导式: [for x in expr do/yield expr if cond]
 *
 * 创建匿名子函数，其内部逻辑等价于：
 *   local _t = {}
 *   for x in expr do
 *     if cond then _t[#_t + 1] = expr end
 *   end
 *   return _t
 *
 * @param ps 解析器状态
 * @return 表达式节点（AST_EXPR_LIST_COMP）
 */
static AstExpr *parse_list_comprehension(ParserState *ps) {
  LexState *ls = ps->ls;
  int line = ls->linenumber;
  int func_idx = ps->func_idx_counter++;
  int parent_idx = ps->curfunc ? ps->curfunc->func_idx : -1;

  lp_next(ps); /* skip '[' */
  lp_checknext(ps, TK_FOR); /* skip 'for' */

  /* 创建匿名子函数 */
  AstFunc *f = ast_new_func(ps->pool, func_idx, parent_idx, line);
  f->source = ps->ls->source;
  f->nparams = 0;
  f->is_vararg = 0;
  ast_chunk_add_func(ps->chunk, f);

  /* 切换到子函数作用域 */
  AstFunc *oldfunc = ps->curfunc;
  ps->curfunc = f;
  scope_push(ps, 0);

  /* 添加 _t 局部变量 */
  TString *t_name = luaS_newliteral(ps->L, "_t");
  scope_add_local(ps, t_name, AST_ATTR_NONE);
  f->nlocals = 1;

  /* 创建 local _t = {} 语句 */
  {
    AstStmt *local_stmt = ast_new_stmt_local(ps->pool, 1, NULL, 1, line);
    local_stmt->u.local.names[0] = t_name;
    local_stmt->u.local.attrs[0] = AST_ATTR_NONE;
    AstExpr *empty_table = ast_new_expr_table(ps->pool, NULL, 0, line);
    local_stmt->u.local.values = cast(AstExpr **,
      ast_pool_alloc(ps->pool, sizeof(AstExpr *)));
    local_stmt->u.local.values[0] = empty_table;
    ast_block_add_stmt(ps->pool, &f->body, local_stmt);
  }

  /* 解析循环变量名列表 */
  int nvars = 0;
  int nvars_cap = 4;
  TString **loop_vars = cast(TString **,
    ast_pool_alloc(ps->pool, nvars_cap * sizeof(TString *)));
  do {
    if (nvars >= nvars_cap) {
      int new_cap = nvars_cap * 2;
      TString **new_vars = cast(TString **,
        ast_pool_alloc(ps->pool, new_cap * sizeof(TString *)));
      memcpy(new_vars, loop_vars, nvars * sizeof(TString *));
      loop_vars = new_vars;
      nvars_cap = new_cap;
    }
    loop_vars[nvars++] = lp_checkname(ps);
  } while (lp_testnext(ps, ','));

  /* 解析 in 关键字和迭代器表达式 */
  lp_checknext(ps, TK_IN);
  int nexprs = 0;
  AstExpr **iter_exprs = parse_exprlist(ps, &nexprs);

  /* 解析 do 或 yield 关键字 */
  if (ls->t.token == TK_DO) {
    lp_next(ps);
  } else if (ls->t.token == TK_NAME && strcmp(getstr(ls->t.seminfo.ts), "yield") == 0) {
    lp_next(ps);
  } else {
    lp_error(ps, "expected 'do' or 'yield' in list comprehension");
  }

  /* 解析值表达式 */
  AstExpr *val_expr = parse_expr(ps);

  /* 解析可选的 if 条件 */
  AstExpr *if_cond = NULL;
  if (lp_testnext(ps, TK_IF)) {
    if_cond = parse_expr(ps);
  }

  lp_checknext(ps, ']');

  /* 构建 for-generic 循环体 */
  AstStmt *for_stmt = ast_new_stmt_forgen(ps->pool, nvars, nexprs, line);
  for_stmt->u.forgen.names = loop_vars;
  for_stmt->u.forgen.exprs = iter_exprs;

  AstBlock *loop_body = &for_stmt->u.forgen.body;
  block_init(ps, loop_body);
  f->nlocals += nvars;

  /* 构建表赋值: _t[#_t + 1] = val_expr
   * 键表达式为 #_t + 1（二元加法） */
  AstExpr *len_expr = ast_new_expr_unop(ps->pool, AST_UN_LEN,
    ast_new_expr_ident(ps->pool, t_name, line), line);
  AstExpr *one_expr = ast_new_expr_int(ps->pool, 1, line);
  AstExpr *key_expr = ast_new_expr_binop(ps->pool, AST_BIN_ADD,
    len_expr, one_expr, line);

  AstStmt *assign_stmt = ast_new_stmt_assign(ps->pool, 1, 1, line);
  AstAssignTarget *tgt = &assign_stmt->u.assign.targets[0];
  tgt->kind = AST_TGT_INDEX;
  tgt->as.index.table = ast_new_expr_ident(ps->pool, t_name, line);
  tgt->as.index.key = key_expr;
  assign_stmt->u.assign.values[0] = val_expr;

  if (if_cond != NULL) {
    AstStmt *if_stmt = ast_new_stmt_if(ps->pool, line);
    AstIfArm *arm = ast_new_ifarm(ps->pool, if_cond, line);
    ast_block_add_stmt(ps->pool, &arm->body, assign_stmt);
    if_stmt->u.ifstmt.arms = arm;
    if_stmt->u.ifstmt.narms = 1;
    ast_block_add_stmt(ps->pool, loop_body, if_stmt);
  } else {
    ast_block_add_stmt(ps->pool, loop_body, assign_stmt);
  }

  ast_block_add_stmt(ps->pool, &f->body, for_stmt);

  /* 创建 return _t 语句 */
  {
    AstStmt *ret_stmt = ast_new_stmt_return(ps->pool, 1, line);
    AstExpr *t_ident = ast_new_expr_ident(ps->pool, t_name, line);
    ret_stmt->u.retstmt.values = cast(AstExpr **,
      ast_pool_alloc(ps->pool, sizeof(AstExpr *)));
    ret_stmt->u.retstmt.values[0] = t_ident;
    ast_block_add_stmt(ps->pool, &f->body, ret_stmt);
  }

  /* 恢复作用域 */
  scope_pop(ps);
  ps->curfunc = oldfunc;

  /* 创建推导式表达式节点 */
  AstExpr *e = ast_new_node(ps->pool, AstExpr, AST_EXPR, line);
  e->kind = AST_EXPR_LIST_COMP;
  e->u.func.func = f;
  return e;
}


/**
 * @brief 从 do 语句块中提取最后一个表达式（递归处理嵌套 do）
 * @param blk 语句块
 * @return 最后一个表达式，无表达式时返回 NULL
 */
static AstExpr *extract_last_expr_from_block(AstBlock *blk) {
  int i;
  for (i = blk->count - 1; i >= 0; i--) {
    AstStmt *s = blk->items[i];
    if (s->kind == AST_STMT_EXPR) {
      return s->u.expr.expr;
    }
    if (s->kind == AST_STMT_DO) {
      AstExpr *e = extract_last_expr_from_block(&s->u.block.block);
      if (e != NULL) return e;
    }
  }
  return NULL;
}

/**
 * @brief 解析 do 表达式: do [statements] [expr] end
 * 将非最后表达式的语句添加到函数体，返回最后一个表达式的值
 * @param ps 解析器状态
 * @return 表达式节点
 */
static AstExpr *parse_do_expr(ParserState *ps) {
  LexState *ls = ps->ls;
  int line = ls->linenumber;
  lp_next(ps); /* skip 'do' */

  /* do 表达式: do [statements] [expr] end，返回最后一个表达式的值 */
  AstExpr *last_expr = NULL;
  int nstmts = 0;
  int cap = 4;
  AstStmt **stmts = cast(AstStmt **, ast_pool_alloc(ps->pool, cap * sizeof(AstStmt *)));
  scope_push(ps, 0);

  /* 收集 do 块内所有语句 */
  while (!lp_check(ps, TK_END)) {
    AstStmt *stmt = parse_stat(ps);
    if (stmt != NULL) {
      if (nstmts >= cap) {
        int i;
        cap *= 2;
        AstStmt **newstmts = cast(AstStmt **, ast_pool_alloc(ps->pool, cap * sizeof(AstStmt *)));
        for (i = 0; i < nstmts; i++) newstmts[i] = stmts[i];
        stmts = newstmts;
      }
      stmts[nstmts++] = stmt;
    }
    lp_testnext(ps, ';');
  }

  /* 从后往前查找最后一个表达式语句 */
  {
    int i;
    int last_expr_idx = -1;
    for (i = nstmts - 1; i >= 0; i--) {
      AstStmt *s = stmts[i];
      if (s->kind == AST_STMT_EXPR) {
        last_expr = s->u.expr.expr;
        last_expr_idx = i;
        break;
      }
      /* 处理嵌套 do 块：从 do 语句块中提取最后一个表达式 */
      if (s->kind == AST_STMT_DO) {
        last_expr = extract_last_expr_from_block(&s->u.block.block);
        last_expr_idx = i;
        break;
      }
    }
    /* 将所有非最后表达式语句添加到函数体，确保它们被代码生成 */
    for (i = 0; i < nstmts; i++) {
      if (i == last_expr_idx) continue;
      ast_block_add_stmt(ps->pool, &ps->curfunc->body, stmts[i]);
    }
  }

  scope_pop(ps);
  lp_checknext(ps, TK_END);
  return last_expr ? last_expr : ast_new_expr_nil(ps->pool, line);
}


/**
 * @brief 解析 if 条件表达式: if cond then expr else expr
 * @param ps 解析器状态
 * @return 表达式节点
 */
static AstExpr *parse_if_expr(ParserState *ps) {
  LexState *ls = ps->ls;
  int line = ls->linenumber;
  lp_next(ps); /* skip 'if' */

  AstExpr *cond = parse_expr(ps);
  lp_checknext(ps, TK_THEN);

  AstExpr *thn = parse_expr(ps);

  AstExpr *els = NULL;
  if (lp_testnext(ps, TK_ELSE)) {
    els = parse_expr(ps);
  } else if (lp_testnext(ps, TK_ELSEIF)) {
    /* elseif 递归处理 */
    els = parse_if_expr(ps);
  } else {
    els = ast_new_expr_nil(ps->pool, line);
  }

  return ast_new_expr_condexpr(ps->pool, cond, thn, els, line);
}


/**
 * @brief 解析 switch 表达式: switch expr case pat -> expr end
 * 支持多值模式: case 1, 2, 3 -> expr
 * @param ps 解析器状态
 * @return 表达式节点
 */
static AstExpr *parse_switch_expr(ParserState *ps) {
  LexState *ls = ps->ls;
  int line = ls->linenumber;
  lp_next(ps); /* skip 'switch' */

  AstExpr *cond = parse_expr(ps);

  /* 解析 case 分支 */
  AstCaseArm *arms = NULL;
  int narms = 0;
  int cap = 4;
  AstExpr *def_body = NULL;

  arms = cast(AstCaseArm *, ast_pool_alloc(ps->pool, cap * sizeof(AstCaseArm)));

  while (lp_testnext(ps, TK_CASE)) {
    /* 收集所有逗号分隔的模式值 */
    AstExpr **patterns = NULL;
    int npatterns = 0;
    int pat_cap = 2;

    patterns = cast(AstExpr **, ast_pool_alloc(ps->pool, pat_cap * sizeof(AstExpr *)));
    patterns[npatterns++] = parse_expr(ps);

    while (lp_testnext(ps, ',')) {
      if (npatterns >= pat_cap) {
        pat_cap *= 2;
        AstExpr **new_pats = cast(AstExpr **,
          ast_pool_alloc(ps->pool, pat_cap * sizeof(AstExpr *)));
        memcpy(new_pats, patterns, npatterns * sizeof(AstExpr *));
        patterns = new_pats;
      }
      patterns[npatterns++] = parse_expr(ps);
    }

    lp_checknext(ps, TK_ARROW); /* -> */
    AstExpr *body = parse_expr(ps);

    if (narms >= cap) {
      cap *= 2;
      AstCaseArm *new_arms = cast(AstCaseArm *,
        ast_pool_alloc(ps->pool, cap * sizeof(AstCaseArm)));
      memcpy(new_arms, arms, narms * sizeof(AstCaseArm));
      arms = new_arms;
    }
    arms[narms].patterns = patterns;
    arms[narms].npatterns = npatterns;
    arms[narms].body = body;
    narms++;
  }

  /* 处理 default */
  if (lp_testnext(ps, TK_DEFAULT)) {
    lp_checknext(ps, TK_ARROW);
    def_body = parse_expr(ps);
  }

  lp_checknext(ps, TK_END);

  /* 创建 switch 表达式 */
  AstExpr *e = ast_new_node(ps->pool, AstExpr, AST_EXPR, line);
  e->kind = AST_EXPR_SWITCH_EXPR;
  e->u.switchx.cond = cond;
  e->u.switchx.arms = arms;
  e->u.switchx.narms = narms;
  e->u.switchx.def = def_body;
  return e;
}


/**
 * @brief 解析 switch 语句: switch expr case pat -> body | case pat: body end
 * 支持多值模式（case 1, 2, 3 ->）和块体形式（case val: stmts）
 * @param ps 解析器状态
 * @return 语句节点（AST_STMT_SWITCH）
 */
static AstStmt *parse_switch_stat(ParserState *ps) {
  LexState *ls = ps->ls;
  int line = ls->linenumber;
  lp_next(ps); /* skip 'switch' */

  AstExpr *cond = parse_expr(ps);

  /* 跳过可选分隔符: do, then, :, { */
  if (lp_testnext(ps, TK_DO) || lp_testnext(ps, TK_THEN) || lp_testnext(ps, ':')) {
    /* 已消费 */
  } else {
    lp_testnext(ps, '{'); /* 尝试消费 { */
  }

  /* 解析 case 分支 */
  int case_cap = 4;
  int ncases = 0;
  AstSwitchCase *cases = cast(AstSwitchCase *,
    ast_pool_alloc(ps->pool, case_cap * sizeof(AstSwitchCase)));
  int has_default = 0;
  AstBlock default_body = {NULL, 0, 0};

  while (ls->t.token != TK_END && ls->t.token != TK_EOS && ls->t.token != '}') {
    if (ls->t.token == TK_CASE) {
      lp_next(ps); /* skip 'case' */

      /* 收集所有逗号分隔的模式值 */
      AstExpr **patterns = NULL;
      int npatterns = 0;
      int pat_cap = 2;
      patterns = cast(AstExpr **, ast_pool_alloc(ps->pool, pat_cap * sizeof(AstExpr *)));
      patterns[npatterns++] = parse_expr(ps);

      while (lp_testnext(ps, ',')) {
        if (npatterns >= pat_cap) {
          pat_cap *= 2;
          AstExpr **new_pats = cast(AstExpr **,
            ast_pool_alloc(ps->pool, pat_cap * sizeof(AstExpr *)));
          memcpy(new_pats, patterns, npatterns * sizeof(AstExpr *));
          patterns = new_pats;
        }
        patterns[npatterns++] = parse_expr(ps);
      }

      /* 扩大 cases 数组 */
      if (ncases >= case_cap) {
        case_cap *= 2;
        AstSwitchCase *new_cases = cast(AstSwitchCase *,
          ast_pool_alloc(ps->pool, case_cap * sizeof(AstSwitchCase)));
        memcpy(new_cases, cases, ncases * sizeof(AstSwitchCase));
        cases = new_cases;
      }

      AstSwitchCase *c = &cases[ncases];
      memset(c, 0, sizeof(AstSwitchCase));
      c->patterns = patterns;
      c->npatterns = npatterns;
      c->is_default = 0;

      /* 解析 case 体 */
      if (lp_testnext(ps, TK_ARROW)) {
        /* 箭头形式：解析表达式，包装为单语句块 */
        AstExpr *body_expr = parse_expr(ps);
        c->body = (AstBlock){NULL, 0, 0};
        block_init(ps, &c->body);
        ast_block_add_stmt(ps->pool, &c->body,
          ast_new_stmt_expr(ps->pool, body_expr, ls->linenumber));
      } else {
        /* 块体形式：: do then { 可选分隔符 */
        lp_testnext(ps, ':');
        lp_testnext(ps, TK_DO);
        lp_testnext(ps, TK_THEN);
        c->body = (AstBlock){NULL, 0, 0};
        block_init(ps, &c->body);
        parse_block(ps, &c->body);
      }

      ncases++;
    } else if (ls->t.token == TK_DEFAULT) {
      if (has_default) {
        luaX_syntaxerror(ls, "multiple default blocks in switch");
      }
      has_default = 1;
      lp_next(ps); /* skip 'default' */

      default_body = (AstBlock){NULL, 0, 0};
      block_init(ps, &default_body);

      if (lp_testnext(ps, TK_ARROW)) {
        /* 箭头形式 */
        AstExpr *body_expr = parse_expr(ps);
        ast_block_add_stmt(ps->pool, &default_body,
          ast_new_stmt_expr(ps->pool, body_expr, ls->linenumber));
      } else {
        /* 块体形式 */
        lp_testnext(ps, ':');
        lp_testnext(ps, TK_DO);
        lp_testnext(ps, TK_THEN);
        parse_block(ps, &default_body);
      }
    } else {
      luaX_syntaxerror(ls, "expected 'case' or 'default' in switch block");
    }
  }

  /* 消费结束符 */
  if (ls->t.token == TK_END) {
    lp_next(ps);
  } else if (ls->t.token == '}') {
    lp_next(ps);
  } else {
    luaX_syntaxerror(ls, "expected 'end' or '}' to close switch block");
  }

  /* 创建 switch 语句节点 */
  AstStmt *s = ast_new_node(ps->pool, AstStmt, AST_STMT, line);
  s->kind = AST_STMT_SWITCH;
  s->u.switchstmt.cond = cond;
  s->u.switchstmt.cases = cases;
  s->u.switchstmt.ncases = ncases;
  s->u.switchstmt.has_default = has_default;
  if (has_default) {
    s->u.switchstmt.default_body = default_body;
  }
  return s;
}


/**
 * @brief 解析 guard 语句: guard cond else { block }
 * @param ps 解析器状态
 * @return 语句节点
 */
static AstStmt *parse_guard_stat(ParserState *ps) {
  LexState *ls = ps->ls;
  int line = ls->linenumber;
  lp_next(ps); /* skip 'guard' */
  fprintf(stderr, "[PARSE] guard_stat: after skip 'guard', token=%d\n", ls->t.token);
  fflush(stderr);

  /* guard let name = expr else { ... } */
  fprintf(stderr, "[PARSE] guard_stat: TK_LET=%d, TK_NAME=%d, about to test...\n", TK_LET, TK_NAME);
  fflush(stderr);
  if (lp_testnext(ps, TK_LET)) {
    fprintf(stderr, "[PARSE] guard_stat: TK_LET matched!\n");
    fflush(stderr);
    TString *name = lp_checkname(ps);
    lp_checknext(ps, '=');
    AstExpr *value = parse_expr(ps);
    lp_checknext(ps, TK_ELSE);

    /* 解析 else 块 { ... } */
    lp_checknext(ps, '{');
    AstBlock else_block = {NULL, 0, 0};
    block_init(ps, &else_block);
    parse_block(ps, &else_block);
    lp_checknext(ps, '}');

    /* 创建 guard let 语句 */
    AstStmt *s = ast_new_stmt_guard(ps->pool, NULL, name, value, &else_block, line);
    /* 注册局部变量到作用域 */
    scope_add_local(ps, name, AST_ATTR_NONE);
    return s;
  }

  /* guard cond else { ... } */
  AstExpr *cond = parse_expr(ps);
  lp_checknext(ps, TK_ELSE);

  /* 解析 else 块 { ... } */
  lp_checknext(ps, '{');
  AstBlock else_block = {NULL, 0, 0};
  block_init(ps, &else_block);
  parse_block(ps, &else_block);
  lp_checknext(ps, '}');

  /* 创建 guard 语句 */
  AstStmt *s = ast_new_stmt_guard(ps->pool, cond, NULL, NULL, &else_block, line);
  fprintf(stderr, "[PARSE] guard_stat: done, returning stmt\n");
  fflush(stderr);
  return s;
}


/**
 * @brief 解析 try-catch-finally 语句: try body catch(e) catch_body finally finally_body end
 * @param ps 解析器状态
 * @return 语句节点
 */
static AstStmt *parse_try_stat(ParserState *ps) {
  LexState *ls = ps->ls;
  int line = ls->linenumber;
  lp_next(ps); /* skip 'try' */

  /* 解析 try 块 */
  AstBlock body = {NULL, 0, 0};
  block_init(ps, &body);
  while (ls->t.token != TK_CATCH && ls->t.token != TK_FINALLY &&
         ls->t.token != TK_END && ls->t.token != TK_EOS) {
    AstStmt *s = parse_stat(ps);
    if (s != NULL) ast_block_add_stmt(ps->pool, &body, s);
    lp_testnext(ps, ';');
  }

  /* 解析 catch 块 */
  AstExpr *catch_var = NULL;
  AstBlock catch_body = {NULL, 0, 0};
  if (ls->t.token == TK_CATCH) {
    lp_next(ps); /* skip 'catch' */
    lp_checknext(ps, '(');
    TString *err_name = lp_checkname(ps);
    catch_var = ast_new_expr_ident(ps->pool, err_name, line);
    lp_checknext(ps, ')');
    block_init(ps, &catch_body);
    while (ls->t.token != TK_FINALLY && ls->t.token != TK_END &&
           ls->t.token != TK_EOS) {
      AstStmt *s = parse_stat(ps);
      if (s != NULL) ast_block_add_stmt(ps->pool, &catch_body, s);
      lp_testnext(ps, ';');
    }
  }

  /* 解析 finally 块 */
  AstBlock finally_body = {NULL, 0, 0};
  if (ls->t.token == TK_FINALLY) {
    lp_next(ps); /* skip 'finally' */
    block_init(ps, &finally_body);
    while (ls->t.token != TK_END && ls->t.token != TK_EOS) {
      AstStmt *s = parse_stat(ps);
      if (s != NULL) ast_block_add_stmt(ps->pool, &finally_body, s);
      lp_testnext(ps, ';');
    }
  }

  lp_checknext(ps, TK_END);

  return ast_new_stmt_try(ps->pool, &body, catch_var,
                          catch_var ? &catch_body : NULL,
                          (finally_body.count > 0) ? &finally_body : NULL,
                          line);
}


/**
 * @brief 解析 defer 语句: defer statement
 * @param ps 解析器状态
 * @return 语句节点
 */
static AstStmt *parse_defer_stat(ParserState *ps) {
  int line = ps->ls->linenumber;
  lp_next(ps); /* skip 'defer' */

  /* 解析被延迟的语句 */
  AstBlock body = {NULL, 0, 0};
  block_init(ps, &body);
  AstStmt *s = parse_stat(ps);
  if (s != NULL) ast_block_add_stmt(ps->pool, &body, s);

  return ast_new_stmt_defer(ps->pool, &body, line);
}


/**
 * @brief 解析 namespace 语句: namespace name { body }
 * @param ps 解析器状态
 * @return 语句节点
 */
static AstStmt *parse_namespace_stat(ParserState *ps) {
  LexState *ls = ps->ls;
  int line = ls->linenumber;
  lp_next(ps); /* skip 'namespace' */

  TString *name = lp_checkname(ps);

  /* 解析命名空间体 */
  lp_checknext(ps, '{');
  AstBlock body = {NULL, 0, 0};
  block_init(ps, &body);
  parse_block(ps, &body);
  lp_checknext(ps, '}');

  return ast_new_stmt_namespace(ps->pool, name, &body, line);
}


/**
 * @brief 解析 using 语句: using namespace Name; 或 using Name::Member;
 * @param ps 解析器状态
 * @return 语句节点
 */
static AstStmt *parse_using_stat(ParserState *ps) {
  LexState *ls = ps->ls;
  int line = ls->linenumber;
  lp_next(ps); /* skip 'using' */

  int is_namespace = 0;
  TString *name;
  TString *last_member = NULL;

  if (ls->t.token == TK_NAMESPACE) {
    /* using namespace Name[::Member::...] */
    is_namespace = 1;
    lp_next(ps); /* skip 'namespace' */
    name = lp_checkname(ps);
    /* 处理 :: 链 */
    while (lp_testnext(ps, TK_DBCOLON)) {
      last_member = lp_checkname(ps);
    }
  } else {
    /* using Name[::Member::...] */
    name = lp_checkname(ps);
    last_member = name;
    /* 处理 :: 链 */
    while (lp_testnext(ps, TK_DBCOLON)) {
      last_member = lp_checkname(ps);
    }
  }

  lp_testnext(ps, ';');  /* 分号可选 */

  return ast_new_stmt_using(ps->pool, is_namespace, name, last_member, line);
}


/**
 * @brief 跳过块内容，不解析，仅匹配括号/关键字跳过多余 token
 * @param ps 解析器状态
 * @param end_char 结束符：'}'、']' 或 TK_END（do/begin ... end 块）
 */
static void skip_block(ParserState *ps, int end_char) {
  LexState *ls = ps->ls;
  int depth = 1;

  if (end_char == TK_END) {
    /* do/begin ... end 关键字块：处理嵌套块的关键字和括号 */
    while (depth > 0 && ls->t.token != TK_EOS) {
      if (ls->t.token == '{' || ls->t.token == '['
          || ls->t.token == TK_DO || ls->t.token == TK_IF
          || (ls->t.token == TK_NAME && lp_softkw_is(ps, "begin"))) {
        depth++;
      } else if (ls->t.token == TK_FUNCTION) {
        /* function 是完整语句，不能用 depth++ 跟踪（其 end 会把 depth 拉回初始值导致提前返回）。
         * 改为递归调用 skip_block 跳过整个函数体，不改变外层 depth。 */
        lp_next(ps); /* skip 'function' */
        /* 跳过函数名（可能包含 . 和 : 如 Class:method） */
        while (ls->t.token == TK_NAME || ls->t.token == '.' || ls->t.token == ':') {
          lp_next(ps);
        }
        /* 跳过参数列表 (...) */
        if (ls->t.token == '(') {
          lp_next(ps);
          int paren_depth = 1;
          while (paren_depth > 0 && ls->t.token != TK_EOS) {
            if (ls->t.token == '(') paren_depth++;
            else if (ls->t.token == ')') paren_depth--;
            lp_next(ps);
          }
        }
        /* 递归跳过函数体，不改变外层 depth */
        skip_block(ps, TK_END);
        continue;
      } else if (ls->t.token == '}' || ls->t.token == ']'
                 || ls->t.token == TK_END) {
        if (ls->t.token == TK_END && depth == 1) {
          lp_next(ps);
          return;
        }
        depth--;
      }
      lp_next(ps);
    }
  } else {
    /* { } 或 [ ] 字符块：保留原有逻辑 */
    while (depth > 0 && ls->t.token != TK_EOS) {
      if (ls->t.token == '{' || ls->t.token == '[') {
        depth++;
      } else if (ls->t.token == '}' || ls->t.token == ']') {
        if (ls->t.token == end_char && depth == 1) {
          lp_next(ps);
          return;
        }
        depth--;
      }
      lp_next(ps);
    }
  }
}


/**
 * @brief 检查当前 token 是否为指定软关键字
 * @param ps 解析器状态
 * @param name 关键字名称
 * @return 1=是软关键字, 0=不是
 */
static int lp_softkw_is(ParserState *ps, const char *name) {
  LexState *ls = ps->ls;
  if (ls->t.token != TK_NAME) return 0;
  return strcmp(getstr(ls->t.seminfo.ts), name) == 0;
}


/**
 * @brief 解析 struct 语句: struct Name { field1 = val1, field2, ... }
 * 也支持旧语法: struct Name do ... end / struct Name begin ... end
 * @param ps 解析器状态
 * @return 语句节点
 */
static AstStmt *parse_struct_stat(ParserState *ps) {
  LexState *ls = ps->ls;
  int line = ls->linenumber;
  lp_next(ps); /* skip 'struct' */

  TString *name = lp_checkname(ps);

  /* 解析字段 */
  if (lp_testnext(ps, '{')) {
    int nentries = 0;
    int cap = 4;
    AstKVPair *pairs = cast(AstKVPair *, ast_pool_alloc(ps->pool, sizeof(AstKVPair) * cap));

    while (ls->t.token != '}' && ls->t.token != TK_EOS) {
      if (nentries >= cap) {
        cap *= 2;
        AstKVPair *newpairs = cast(AstKVPair *, ast_pool_alloc(ps->pool, sizeof(AstKVPair) * cap));
        memcpy(newpairs, pairs, sizeof(AstKVPair) * nentries);
        pairs = newpairs;
      }

      TString *fname = NULL;
      AstExpr *val = NULL;

      if (ls->t.token == TK_NAME) {
        fname = lp_checkname(ps);
        if (lp_testnext(ps, '=')) {
          val = parse_expr(ps);
        }
        /* 无值则默认为 nil */
      }

      AstExpr *key = ast_new_expr_str(ps->pool, fname ? fname : luaS_newliteral(ls->L, ""), AST_EXPR_STRING, line);
      pairs[nentries].key = key;
      pairs[nentries].value = val;
      nentries++;

      if (ls->t.token == ',' || ls->t.token == ';') lp_next(ps);
    }
    lp_checknext(ps, '}');

    return ast_new_stmt_typed_pairs(ps->pool, AST_STMT_STRUCT, name, pairs, nentries, line);
  }

  /* 旧语法：do...end / begin...end / 隐式结构体 */
  AstBlock body = {NULL, 0, 0};
  block_init(ps, &body);
  if (lp_testnext(ps, TK_DO)) {
    skip_block(ps, TK_END);
  } else if (ls->t.token == TK_NAME && lp_softkw_is(ps, "begin")) {
    lp_next(ps);
    skip_block(ps, TK_END);
  } else if (ls->t.token != TK_EOS) {
    skip_block(ps, TK_END);
  } else {
    lp_error_expected(ps, '{');
  }

  return ast_new_stmt_typed(ps->pool, AST_STMT_STRUCT, name, &body, line);
}


/**
 * @brief 解析 superstruct 语句: superstruct Name [ key: val, ... ]
 * @param ps 解析器状态
 * @return 语句节点
 */
static AstStmt *parse_superstruct_stat(ParserState *ps) {
  LexState *ls = ps->ls;
  int line = ls->linenumber;
  lp_next(ps); /* skip 'superstruct' */

  TString *name = lp_checkname(ps);

  lp_checknext(ps, '[');

  /* 解析键值对 key: val */
  int nentries = 0;
  int cap = 4;
  AstKVPair *pairs = cast(AstKVPair *, ast_pool_alloc(ps->pool, sizeof(AstKVPair) * cap));

  while (ls->t.token != ']' && ls->t.token != TK_EOS) {
    if (nentries >= cap) {
      cap *= 2;
      AstKVPair *newpairs = cast(AstKVPair *, ast_pool_alloc(ps->pool, sizeof(AstKVPair) * cap));
      memcpy(newpairs, pairs, sizeof(AstKVPair) * nentries);
      pairs = newpairs;
    }

    AstExpr *key;
    if (ls->t.token == TK_NAME) {
      key = ast_new_expr_str(ps->pool, ls->t.seminfo.ts, AST_EXPR_STRING, ls->linenumber);
      lp_next(ps);
    } else if (ls->t.token == TK_STRING) {
      key = ast_new_expr_str(ps->pool, ls->t.seminfo.ts, AST_EXPR_STRING, ls->linenumber);
      lp_next(ps);
    } else if (ls->t.token == '[') {
      lp_next(ps);
      key = parse_expr(ps);
      lp_checknext(ps, ']');
    } else {
      key = parse_expr(ps);
    }

    lp_checknext(ps, ':');
    AstExpr *value = parse_expr(ps);

    pairs[nentries].key = key;
    pairs[nentries].value = value;
    nentries++;

    if (ls->t.token == ',') lp_next(ps);
  }
  lp_checknext(ps, ']');

  return ast_new_stmt_typed_pairs(ps->pool, AST_STMT_SUPERSTRUCT, name, pairs, nentries, line);
}


/**
 * @brief 解析枚举语句: enum Name { A, B = 10, C } / enum Name do ... end
 *        也支持匿名枚举: enum { ... } / enum do ... end
 * @param ps 解析器状态
 * @return 语句节点
 */
static AstStmt *parse_enum_stat(ParserState *ps) {
  LexState *ls = ps->ls;
  int line = ls->linenumber;
  lp_next(ps); /* skip 'enum' */

  /* 检查是否有 enum class 语法 */
  int is_class = 0;
  if (lp_softkw_is(ps, "class")) {
    is_class = 1;
    lp_next(ps); /* skip 'class' */
  }

  TString *name = NULL;

  /* 如果当前token是块开始符，则为匿名枚举（无名称） */
  if (ls->t.token == '{' || ls->t.token == TK_DO
      || (ls->t.token == TK_NAME && lp_softkw_is(ps, "begin"))) {
    /* 匿名枚举，name 保持 NULL */
  } else {
    name = lp_checkname(ps);
  }

  /* 确定块结束符和块类型 */
  int use_brace = 0;
  if (lp_testnext(ps, '{')) {
    use_brace = 1;
  } else if (lp_testnext(ps, TK_DO)) {
    /* do...end 块 */
  } else if (ls->t.token == TK_NAME && lp_softkw_is(ps, "begin")) {
    lp_next(ps); /* skip 'begin' */
  } else {
    lp_error_expected(ps, '{');
  }

  /* 动态数组解析枚举成员 */
  #define ENUM_ENTRIES_INIT_CAP 8
  int cap = ENUM_ENTRIES_INIT_CAP;
  int nentries = 0;
  AstEnumEntry *entries = cast(AstEnumEntry *,
      ast_pool_alloc(ps->pool, cap * sizeof(AstEnumEntry)));

  for (;;) {
    /* 检查结束条件 */
    if (use_brace) {
      if (ls->t.token == '}') break;
    } else {
      if (ls->t.token == TK_END) break;
    }
    if (ls->t.token == TK_EOS) {
      if (use_brace)
        lp_error_expected(ps, '}');
      else
        lp_error_expected(ps, TK_END);
      break;
    }

    /* 跳过空语句（分号或逗号） */
    if (ls->t.token == ';' || ls->t.token == ',') {
      lp_next(ps);
      continue;
    }

    /* 解析枚举成员名 */
    if (ls->t.token != TK_NAME) {
      lp_error_expected(ps, TK_NAME);
      break;
    }

    /* 动态扩容 */
    if (nentries >= cap) {
      int new_cap = cap * 2;
      AstEnumEntry *new_entries = cast(AstEnumEntry *,
          ast_pool_alloc(ps->pool, new_cap * sizeof(AstEnumEntry)));
      memcpy(new_entries, entries, cap * sizeof(AstEnumEntry));
      entries = new_entries;
      cap = new_cap;
    }

    AstEnumEntry *entry = &entries[nentries];
    entry->name = lp_checkname(ps);
    entry->value_expr = NULL;  /* 默认无值，自动递增 */

    /* 检查是否有显式赋值 '=' */
    if (ls->t.token == '=') {
      lp_next(ps); /* skip '=' */
      entry->value_expr = parse_expr(ps);
    }

    nentries++;

    /* 处理可选的逗号分隔符 */
    if (use_brace) {
      if (ls->t.token != '}') {
        lp_testnext(ps, ',');  /* 可选的逗号 */
      }
    }
    /* do...end 块中不需要分隔符，直接继续 */
  }

  /* 跳过结束符 */
  if (use_brace) {
    lp_checknext(ps, '}');
  } else {
    lp_checknext(ps, TK_END);
  }

  #undef ENUM_ENTRIES_INIT_CAP

  return ast_new_stmt_enum(ps->pool, name, entries, nentries, is_class, line);
}


/**
 * @brief 解析 class 语句: class Name [extends Parent] [implements I1, I2] [use T1, T2] { ... } / do ... end
 * @param ps 解析器状态
 * @param class_flags 类修饰符标志（CLASS_FLAG_ABSTRACT/FINAL/SEALED，0表示无修饰符）
 * @return 语句节点
 */
static AstStmt *parse_class_stat(ParserState *ps, int class_flags) {
  LexState *ls = ps->ls;
  int line = ls->linenumber;
  lp_next(ps); /* skip 'class' */

  TString *name = lp_checkname(ps);

  /* 捕获父类名：支持 extends 关键字和 : 语法 */
  TString *extends_name = NULL;
  if (lp_testnext(ps, ':')) {
    /* class Name : ParentName 语法 */
    extends_name = lp_checkname(ps);
  } else if (lp_softkw_is(ps, "extends")) {
    lp_next(ps); /* skip 'extends' */
    extends_name = lp_checkname(ps);
  }

  /* 捕获 implements 接口名列表 */
  TString **implements = NULL;
  int nimplements = 0;
  int impl_cap = 0;
  if (lp_softkw_is(ps, "implements")) {
    lp_next(ps); /* skip 'implements' */
    do {
      if (nimplements >= impl_cap) {
        int new_cap = (impl_cap == 0) ? 4 : impl_cap * 2;
        TString **new_arr = cast(TString **,
          ast_pool_alloc(ps->pool, new_cap * sizeof(TString *)));
        if (implements) {
          memcpy(new_arr, implements, nimplements * sizeof(TString *));
        }
        implements = new_arr;
        impl_cap = new_cap;
      }
      implements[nimplements++] = lp_checkname(ps);
    } while (lp_testnext(ps, ','));
  }

  /* 捕获 use trait 名列表 */
  TString **use_traits = NULL;
  int nuse_traits = 0;
  int use_cap = 0;
  if (lp_softkw_is(ps, "use")) {
    lp_next(ps); /* skip 'use' */
    do {
      if (nuse_traits >= use_cap) {
        int new_cap = (use_cap == 0) ? 4 : use_cap * 2;
        TString **new_arr = cast(TString **,
          ast_pool_alloc(ps->pool, new_cap * sizeof(TString *)));
        if (use_traits) {
          memcpy(new_arr, use_traits, nuse_traits * sizeof(TString *));
        }
        use_traits = new_arr;
        use_cap = new_cap;
      }
      use_traits[nuse_traits++] = lp_checkname(ps);
    } while (lp_testnext(ps, ','));
  }

  /* 解析类体：接受 {, do 或 begin 作为块开始符 */
  /* 使用结构化成员解析，不再使用 parse_block */
  AstClassMember *members = NULL;
  int nmembers = 0;
  int member_cap = 4;

  AstBlock body = {NULL, 0, 0};  /* 保留兼容字段 */

  {
    AstFunc *oldfunc = ps->curfunc;
    scope_push(ps, 0);

    /* 跳过块开始符 */
    if (lp_testnext(ps, '{')) {
      /* 已消费 */
    } else if (lp_testnext(ps, TK_DO)) {
      /* 已消费 */
    } else if (ls->t.token == TK_NAME && lp_softkw_is(ps, "begin")) {
      lp_next(ps); /* skip 'begin' */
    }

    /* 分配成员数组 */
    members = cast(AstClassMember *,
      ast_pool_alloc(ps->pool, sizeof(AstClassMember) * member_cap));

    /* 解析类体成员 */
    while (ls->t.token != TK_END && ls->t.token != '}' && ls->t.token != TK_EOS) {
      /* 解析访问修饰符 */
      AstAccessLevel access = AST_ACCESS_DEFAULT;
      int is_static = 0;
      int is_abstract = 0;
      int is_final = 0;

      /* 修饰符循环 */
      while (ls->t.token == TK_NAME) {
        const char *kw = getstr(ls->t.seminfo.ts);
        if (strcmp(kw, "private") == 0) {
          access = AST_ACCESS_PRIVATE;
          lp_softkw_is(ps, kw); /* 确保识别为软关键字 */
          lp_next(ps);
        } else if (strcmp(kw, "protected") == 0) {
          access = AST_ACCESS_PROTECTED;
          lp_softkw_is(ps, kw);
          lp_next(ps);
        } else if (strcmp(kw, "public") == 0) {
          access = AST_ACCESS_PUBLIC;
          lp_softkw_is(ps, kw);
          lp_next(ps);
        } else if (strcmp(kw, "static") == 0) {
          is_static = 1;
          lp_softkw_is(ps, kw);
          lp_next(ps);
        } else if (strcmp(kw, "abstract") == 0) {
          is_abstract = 1;
          lp_softkw_is(ps, kw);
          lp_next(ps);
        } else if (strcmp(kw, "final") == 0) {
          is_final = 1;
          lp_softkw_is(ps, kw);
          lp_next(ps);
        } else {
          break;
        }
      }

      /* 扩大成员数组 */
      if (nmembers >= member_cap) {
        member_cap *= 2;
        AstClassMember *new_members = cast(AstClassMember *,
          ast_pool_alloc(ps->pool, sizeof(AstClassMember) * member_cap));
        memcpy(new_members, members, sizeof(AstClassMember) * nmembers);
        members = new_members;
      }

      int member_line = ls->linenumber;

      /* 检查是否是 getter/setter */
      if (ls->t.token == TK_NAME && strcmp(getstr(ls->t.seminfo.ts), "get") == 0) {
        lp_softkw_is(ps, "get");
        lp_next(ps);
        TString *prop_name = lp_checkname(ps);
        AstFunc *func = parse_funcbody(ps, member_line, 0, 0, 0);
        AstClassMember *m = &members[nmembers];
        m->kind = AST_MEMBER_GETTER;
        m->access = access;
        m->is_static = is_static;
        m->name = prop_name;
        m->u.method_func = func;
        m->line = member_line;
        nmembers++;
        continue;
      }

      if (ls->t.token == TK_NAME && strcmp(getstr(ls->t.seminfo.ts), "set") == 0) {
        lp_softkw_is(ps, "set");
        lp_next(ps);
        TString *prop_name = lp_checkname(ps);
        AstFunc *func = parse_funcbody(ps, member_line, 0, 0, 0);
        AstClassMember *m = &members[nmembers];
        m->kind = AST_MEMBER_SETTER;
        m->access = access;
        m->is_static = is_static;
        m->name = prop_name;
        m->u.method_func = func;
        m->line = member_line;
        nmembers++;
        continue;
      }

      /* 普通方法 */
      if (ls->t.token == TK_FUNCTION) {
        lp_next(ps); /* skip 'function' */
        TString *method_name = lp_checkname(ps);
        AstFunc *func = parse_funcbody(ps, member_line, 0, 0, 0);
        AstClassMember *m = &members[nmembers];
        m->access = access;
        m->is_static = is_static;
        m->name = method_name;
        m->u.method_func = func;
        m->line = member_line;

        if (is_abstract) {
          m->kind = AST_MEMBER_ABSTRACT;
        } else if (is_final) {
          m->kind = AST_MEMBER_FINAL;
        } else {
          m->kind = AST_MEMBER_METHOD;
        }
        nmembers++;
      } else if (ls->t.token == TK_NAME) {
        /* 属性定义: name = value */
        TString *prop_name = ls->t.seminfo.ts;
        lp_next(ps);
        AstExpr *value = NULL;
        if (lp_testnext(ps, '=')) {
          value = parse_expr(ps);
        }
        AstClassMember *m = &members[nmembers];
        m->kind = AST_MEMBER_PROPERTY;
        m->access = access;
        m->is_static = is_static;
        m->name = prop_name;
        m->u.property_value = value;
        m->line = member_line;
        nmembers++;
      } else if (ls->t.token == ';') {
        /* 空语句，跳过 */
        lp_next(ps);
      } else {
        /* 无法识别的成员，报错 */
        luaX_syntaxerror(ls, "invalid member definition in class body");
      }
    }

    /* 消费结束符 */
    if (ls->t.token == TK_END) {
      lp_next(ps);
    } else if (ls->t.token == '}') {
      lp_next(ps);
    }

    scope_pop(ps);
    ps->curfunc = oldfunc;
  }

  /* 创建 AST class 节点并使用 classstmt 专用字段 */
  AstStmt *s = ast_new_node(ps->pool, AstStmt, AST_STMT, line);
  s->kind = AST_STMT_CLASS;
  s->decorators = NULL;
  s->ndecorators = 0;
  s->u.classstmt.name = name;
  s->u.classstmt.extends_name = extends_name;
  s->u.classstmt.implements = implements;
  s->u.classstmt.nimplements = nimplements;
  s->u.classstmt.use_traits = use_traits;
  s->u.classstmt.nuse_traits = nuse_traits;
  s->u.classstmt.class_flags = class_flags;
  s->u.classstmt.body = body;
  s->u.classstmt.members = members;
  s->u.classstmt.nmembers = nmembers;
  return s;
}


/**
 * @brief 解析 trait 语句: trait Name { ... } / trait Name do ... end / trait Name begin ... end
 * @param ps 解析器状态
 * @return 语句节点
 */
static AstStmt *parse_trait_stat(ParserState *ps) {
  LexState *ls = ps->ls;
  int line = ls->linenumber;
  lp_next(ps); /* skip 'trait' */

  TString *name = lp_checkname(ps);

  /* 解析 trait 体：接受 {, do 或 begin 作为块开始符 */
  AstBlock body = {NULL, 0, 0};
  block_init(ps, &body);
  {
    AstFunc *oldfunc = ps->curfunc;
    scope_push(ps, 0);
    if (lp_testnext(ps, '{')) {
      parse_block(ps, &body);
      lp_checknext(ps, '}');
    } else if (lp_testnext(ps, TK_DO)) {
      parse_block(ps, &body);
      lp_checknext(ps, TK_END);
    } else if (ls->t.token == TK_NAME && lp_softkw_is(ps, "begin")) {
      lp_next(ps); /* skip 'begin' */
      parse_block(ps, &body);
      lp_checknext(ps, TK_END);
    } else if (ls->t.token != TK_EOS) {
      /* 隐式 trait 体：trait Name ... end */
      parse_block(ps, &body);
      lp_checknext(ps, TK_END);
    }
    scope_pop(ps);
    ps->curfunc = oldfunc;
  }

  return ast_new_stmt_typed(ps->pool, AST_STMT_TRAIT, name, &body, line);
}


/**
 * @brief 解析 interface 语句: interface Name { ... } / interface Name do ... end / interface Name begin ... end
 * @param ps 解析器状态
 * @return 语句节点
 */
static AstStmt *parse_interface_stat(ParserState *ps) {
  LexState *ls = ps->ls;
  int line = ls->linenumber;
  lp_next(ps); /* skip 'interface' */

  TString *name = lp_checkname(ps);

  /* 解析接口体：接受 {, do 或 begin 作为块开始符 */
  AstBlock body = {NULL, 0, 0};
  block_init(ps, &body);
  {
    AstFunc *oldfunc = ps->curfunc;
    scope_push(ps, 0);
    if (lp_testnext(ps, '{')) {
      parse_block(ps, &body);
      lp_checknext(ps, '}');
    } else if (lp_testnext(ps, TK_DO)) {
      parse_block(ps, &body);
      lp_checknext(ps, TK_END);
    } else if (ls->t.token == TK_NAME && lp_softkw_is(ps, "begin")) {
      lp_next(ps); /* skip 'begin' */
      parse_block(ps, &body);
      lp_checknext(ps, TK_END);
    } else if (ls->t.token != TK_EOS) {
      /* 隐式接口体：interface Name ... end */
      parse_block(ps, &body);
      lp_checknext(ps, TK_END);
    }
    scope_pop(ps);
    ps->curfunc = oldfunc;
  }

  return ast_new_stmt_typed(ps->pool, AST_STMT_INTERFACE, name, &body, line);
}


/**
 * @brief 解析匹配模式：_ | literal | name | is Type | low..high
 * @param ps 解析器状态
 * @return 模式节点
 */
static AstMatchPat *parse_match_pattern(ParserState *ps) {
  LexState *ls = ps->ls;
  int line = ls->linenumber;

  /* 通配符 _ */
  if (ls->t.token == TK_NAME && strcmp(getstr(ls->t.seminfo.ts), "_") == 0) {
    lp_next(ps);
    return ast_new_pat_wildcard(ps->pool, line);
  }

  /* is TypeName */
  if (ls->t.token == TK_IS) {
    lp_next(ps);
    TString *type_name = lp_checkname(ps);
    return ast_new_pat_type(ps->pool, type_name, line);
  }

  /* 变量绑定：name (后面不是 = 且不是关键字) */
  if (ls->t.token == TK_NAME) {
    int la = lp_lookahead(ps);
    /* 检查是否是简写表字段或变量绑定：name 后面是 ',' '}' '=>' 'if' 等 */
    if (la != '=') {
      TString *name = ls->t.seminfo.ts;
      lp_next(ps);
      return ast_new_pat_variable(ps->pool, name, line);
    }
  }

  /* 表解构 { ... } */
  if (ls->t.token == '{') {
    lp_next(ps);
    /* 解析表解构模式：{ field1, field2, ... } */
    int nfields = 0;
    int cap = 4;
    AstMatchPat **fields = cast(AstMatchPat **,
      ast_pool_alloc(ps->pool, cap * sizeof(AstMatchPat *)));

    if (ls->t.token != '}') {
      do {
        if (nfields >= cap) {
          int new_cap = cap * 2;
          AstMatchPat **new_fields = cast(AstMatchPat **,
            ast_pool_alloc(ps->pool, new_cap * sizeof(AstMatchPat *)));
          memcpy(new_fields, fields, nfields * sizeof(AstMatchPat *));
          fields = new_fields;
          cap = new_cap;
        }
        /* 递归解析每个字段的子模式 */
        fields[nfields++] = parse_match_pattern(ps);
      } while (lp_testnext(ps, ','));
    }
    lp_checknext(ps, '}');
    return ast_new_pat_table(ps->pool, fields, nfields, line);
  }

  /* 字面量或表达式 */
  AstExpr *e = parse_simpleexpr(ps);

  /* 检查是否为范围模式：low..high */
  if (ls->t.token == TK_CONCAT) {
    lp_next(ps); /* skip '..' */
    AstExpr *upper = parse_simpleexpr(ps);
    return ast_new_pat_range(ps->pool, e, upper, line);
  }

  return ast_new_pat_literal(ps->pool, e, line);
}


/**
 * @brief 解析 match 语句: match expr { case pattern => body, ... }
 * @param ps 解析器状态
 * @return 语句节点
 */
static AstStmt *parse_match_stat(ParserState *ps) {
  LexState *ls = ps->ls;
  int line = ls->linenumber;
  lp_next(ps); /* skip 'match' */

  /* 解析控制表达式 */
  AstExpr *control = parse_expr(ps);

  /* 跳过可选的分隔符：do, then, :, { */
  if (lp_testnext(ps, TK_DO) || lp_testnext(ps, TK_THEN) || lp_testnext(ps, ':')) {
    /* 已消费 */
  } else {
    lp_testnext(ps, '{'); /* 尝试消费 { */
  }

  /* 解析 case 臂 */
  int arm_capacity = 4;
  int narms = 0;
  AstMatchArm *arms = (AstMatchArm *)ast_pool_alloc(ps->pool, sizeof(AstMatchArm) * arm_capacity);

  while (ls->t.token != TK_END && ls->t.token != TK_EOS && ls->t.token != '}') {
    if (ls->t.token == TK_CASE) {
      lp_next(ps); /* skip 'case' */

      /* 扩大 arms 数组 */
      if (narms >= arm_capacity) {
        arm_capacity *= 2;
        AstMatchArm *new_arms = (AstMatchArm *)ast_pool_alloc(ps->pool, sizeof(AstMatchArm) * arm_capacity);
        memcpy(new_arms, arms, sizeof(AstMatchArm) * narms);
        arms = new_arms;
      }

      AstMatchArm *arm = &arms[narms];
      memset(arm, 0, sizeof(AstMatchArm));

      /* 解析模式（单个模式） */
      arm->pattern = parse_match_pattern(ps);

      /* 可选守卫条件 */
      if (ls->t.token == TK_IF) {
        lp_next(ps); /* skip 'if' */
        arm->guard = parse_expr(ps);
      }

      /* 解析臂体 */
      if (lp_testnext(ps, TK_MEAN)) {
        /* 箭头表达式体 */
        arm->is_arrow = 1;
        arm->body_expr = parse_expr(ps);
      } else {
        /* 语句块体 */
        arm->is_arrow = 0;
        /* 跳过可选分隔符 */
        lp_testnext(ps, ':');
        lp_testnext(ps, TK_DO);
        lp_testnext(ps, TK_THEN);
        arm->body_block = (AstBlock){NULL, 0, 0};
        block_init(ps, &arm->body_block);
        parse_block(ps, &arm->body_block);
      }

      narms++;
    } else {
      /* 遇到非法 token，报错 */
      luaX_syntaxerror(ls, "expected 'case' in match block");
    }
  }

  /* 消费结束符 */
  if (ls->t.token == TK_END) {
    lp_next(ps);
  } else if (ls->t.token == '}') {
    lp_next(ps);
  } else {
    luaX_syntaxerror(ls, "expected 'end' or '}' to close match block");
  }

  return ast_new_stmt_match(ps->pool, control, arms, narms, 0, line);
}


/**
 * @brief 解析 with 语句: with expr do ... end
 * @param ps 解析器状态
 * @return 语句节点
 */
static AstStmt *parse_with_stat(ParserState *ps) {
  LexState *ls = ps->ls;
  int line = ls->linenumber;
  lp_next(ps); /* skip 'with' */

  /* 解析 with(expr) 中的表达式 */
  lp_checknext(ps, '(');
  AstExpr *target = parse_expr(ps);
  lp_checknext(ps, ')');

  /* 解析 with 体：支持 {..}、do..end、begin..end */
  AstBlock body = {NULL, 0, 0};
  block_init(ps, &body);
  if (lp_testnext(ps, '{')) {
    parse_block(ps, &body);
    lp_checknext(ps, '}');
  } else if (lp_testnext(ps, TK_DO)) {
    parse_block(ps, &body);
    lp_checknext(ps, TK_END);
  } else if (ls->t.token == TK_NAME && lp_softkw_is(ps, "begin")) {
    lp_next(ps); /* skip 'begin' */
    parse_block(ps, &body);
    lp_checknext(ps, TK_END);
  } else {
    /* 默认：隐式块，直到 end */
    parse_block(ps, &body);
    lp_checknext(ps, TK_END);
  }

  return ast_new_stmt_with(ps->pool, target, &body, line);
}


/**
 * @brief 解析 asm 语句: asm( 指令序列 )
 * 将括号内的原始文本捕获为 TString，代码生成阶段解析并发射字节码
 * @param ps 解析器状态
 * @return 语句节点
 */
static AstStmt *parse_asm_stat(ParserState *ps) {
  LexState *ls = ps->ls;
  int line = ls->linenumber;
  lp_next(ps); /* 跳过 'asm' */

  lp_checknext(ps, '(');

  /* 使用动态缓冲区捕获原始文本 */
  size_t raw_len = 0;
  size_t raw_cap = 256;
  char *raw_buf = luaM_newblock(ls->L, raw_cap);
  raw_buf[0] = '\0';
  int depth = 1; /* 括号嵌套深度 */

  while (depth > 0 && ls->t.token != TK_EOS) {
    const char *token_text = ls->t.seminfo.ts ? getstr(ls->t.seminfo.ts) : NULL;
    size_t token_len = 0;

    if (ls->t.token == '(') depth++;
    else if (ls->t.token == ')') {
      depth--;
      if (depth == 0) {
        lp_next(ps); /* 消费 ')' */
        break;
      }
    }

    /* 获取 token 的文本表示 */
    if (token_text && ls->t.token != TK_EOS) {
      token_len = strlen(token_text);
    } else if (ls->t.token < 256) {
      /* 单字符 token */
      token_len = 1;
    }

    /* 追加到缓冲区 */
    if (token_len > 0) {
      if (raw_len + token_len + 2 > raw_cap) {
        raw_cap = (raw_len + token_len + 2) * 2;
        raw_buf = cast(char *, luaM_reallocvector(ls->L, raw_buf, raw_cap, raw_len + 1, char));
      }
      if (raw_len > 0) {
        raw_buf[raw_len++] = ' ';
      }
      if (token_text && ls->t.token != TK_EOS) {
        memcpy(raw_buf + raw_len, token_text, token_len);
      } else if (ls->t.token < 256) {
        raw_buf[raw_len] = (char)ls->t.token;
      }
      raw_len += token_len;
      raw_buf[raw_len] = '\0';
    }

    lp_next(ps);
  }

  /* 创建原始文本字符串 */
  TString *raw_body;
  if (raw_len > 0) {
    raw_body = luaS_newlstr(ls->L, raw_buf, raw_len);
  } else {
    raw_body = luaS_newliteral(ls->L, "");
  }

  luaM_free(ls->L, raw_buf);

  return ast_new_stmt_asm(ps->pool, raw_body, line);
}


/**
 * @brief 解析 concept 语句，支持三种语法：
 *   1. concept Name { body }           — 向后兼容，{} 体
 *   2. concept Name(params) body end   — 带参数列表，end 终止符
 *   3. concept Name(params) = expr     — 表达式体
 * 参考 lparser.c:conceptstat 第 10682-10729 行
 * @param ps 解析器状态
 * @return 语句节点
 */
static AstStmt *parse_concept_stat(ParserState *ps) {
  LexState *ls = ps->ls;
  int line = ls->linenumber;
  lp_next(ps); /* skip 'concept' */

  TString *name = lp_checkname(ps);

  /* 解析 concept 体 */
  AstBlock body = {NULL, 0, 0};
  block_init(ps, &body);

  /* 检查语法：{ body }、 (params) body end 或 (params) = expr */
  if (ls->t.token == '{') {
    /* 语法1: concept Name { body } — 向后兼容 */
    lp_next(ps); /* skip '{' */
    AstFunc *oldfunc = ps->curfunc;
    scope_push(ps, 0);
    parse_block(ps, &body);
    scope_pop(ps);
    ps->curfunc = oldfunc;
    lp_checknext(ps, '}');
  } else {
    /* 语法2/3: concept Name(params) body end 或 concept Name(params) = expr */
    int func_idx = ps->func_idx_counter++;
    int parent_idx = ps->curfunc ? ps->curfunc->func_idx : -1;
    AstFunc *f = ast_new_func(ps->pool, func_idx, parent_idx, line);
    f->source = ps->ls->source;
    f->is_vararg = 0;

    int nparams = 0;
    int param_cap = 4;
    AstFuncParam *params = cast(AstFuncParam *,
      ast_pool_alloc(ps->pool, param_cap * sizeof(AstFuncParam)));

    /* 解析可选的参数列表 () */
    if (lp_testnext(ps, '(')) {
      if (!lp_check(ps, ')')) {
        for (;;) {
          AstFuncParam *param;
          TString *pname;
          if (lp_testnext(ps, TK_DOTS)) {
            f->is_vararg = 1;
            break;
          }
          pname = lp_checkname(ps);
          if (nparams >= param_cap) {
            int new_cap = param_cap * 2;
            AstFuncParam *new_params = cast(AstFuncParam *,
              ast_pool_alloc(ps->pool, new_cap * sizeof(AstFuncParam)));
            memcpy(new_params, params, nparams * sizeof(AstFuncParam));
            params = new_params;
            param_cap = new_cap;
          }
          param = &params[nparams++];
          param->name = pname;
          param->default_value = NULL;
          param->attr = AST_ATTR_NONE;
          param->type_hint = NULL;
          if (!lp_testnext(ps, ',')) break;
        }
      }
      lp_checknext(ps, ')');
    }

    f->nparams = nparams;
    f->params = params;
    f->nlocals = nparams;

    /* 设置函数作用域并注册参数 */
    AstFunc *oldfunc = ps->curfunc;
    ps->curfunc = f;
    scope_push(ps, 0);
    {
      int i;
      for (i = 0; i < nparams; i++) {
        scope_add_local(ps, params[i].name, params[i].attr);
      }
    }

    if (lp_testnext(ps, '=')) {
      /* 语法3: concept Name(params) = expr — 表达式体 */
      AstExpr *ret_expr = parse_expr(ps);
      AstStmt *ret_stmt = ast_new_stmt_return(ps->pool, 1, line);
      ret_stmt->u.retstmt.values[0] = ret_expr;
      ast_block_add_stmt(ps->pool, &f->body, ret_stmt);
    } else {
      /* 语法2: concept Name(params) body end — 语句体 */
      parse_block(ps, &f->body);
      lp_checknext(ps, TK_END);
    }

    scope_pop(ps);
    ps->curfunc = oldfunc;

    /* 将函数添加到chunk并包装为 function 定义语句，供 codegen 生成 proto */
    ast_chunk_add_func(ps->chunk, f);
    {
      AstStmt *func_stmt = ast_new_stmt_localfunc(ps->pool, name, f, line);
      ast_block_add_stmt(ps->pool, &body, func_stmt);
    }
  }

  return ast_new_stmt_typed(ps->pool, AST_STMT_CONCEPT, name, &body, line);
}


/**
 * @brief 解析 command 语句: command Name { body } 或 command Name(params) body end
 * @param ps 解析器状态
 * @return 语句节点
 */
static AstStmt *parse_command_stat(ParserState *ps) {
  LexState *ls = ps->ls;
  int line = ls->linenumber;
  lp_next(ps); /* skip 'command' */

  TString *name = lp_checkname(ps);

  /* 解析 body */
  AstBlock body = {NULL, 0, 0};
  block_init(ps, &body);

  if (ls->t.token == '{') {
    /* 语法1: command Name { body } — 向后兼容 */
    lp_next(ps); /* skip '{' */
    AstFunc *oldfunc = ps->curfunc;
    scope_push(ps, 0);
    parse_block(ps, &body);
    scope_pop(ps);
    ps->curfunc = oldfunc;
    lp_checknext(ps, '}');
  } else if (ls->t.token == '(') {
    /* 语法2: command Name(params) body end — 标准函数体语法 */
    /* 解析参数列表 () */
    lp_next(ps); /* skip '(' */
    if (ls->t.token != ')') {
      do {
        if (ls->t.token == TK_DOTS) {
          lp_next(ps);
          break;
        }
        TString *pname = lp_checkname(ps);
        scope_add_local(ps, pname, AST_ATTR_NONE);
        lp_gettypehint(ps);  /* 解析可选的类型注解 */
      } while (lp_testnext(ps, ','));
    }
    lp_checknext(ps, ')');

    /* 解析 body 并用 end 终止 */
    AstFunc *oldfunc = ps->curfunc;
    scope_push(ps, 0);
    parse_block(ps, &body);
    scope_pop(ps);
    ps->curfunc = oldfunc;
    lp_checknext(ps, TK_END);
  }

  return ast_new_stmt_typed(ps->pool, AST_STMT_COMMAND, name, &body, line);
}


/**
 * @brief 解析 keyword 语句: keyword Name { body } 或 keyword Name(params) body end
 * @param ps 解析器状态
 * @return 语句节点
 */
static AstStmt *parse_keyword_stat(ParserState *ps) {
  LexState *ls = ps->ls;
  int line = ls->linenumber;
  lp_next(ps); /* skip 'keyword' */

  TString *name = lp_checkname(ps);

  /* 解析 body */
  AstBlock body = {NULL, 0, 0};
  block_init(ps, &body);

  if (ls->t.token == '{') {
    /* 语法1: keyword Name { body } — 向后兼容 */
    lp_next(ps); /* skip '{' */
    AstFunc *oldfunc = ps->curfunc;
    scope_push(ps, 0);
    parse_block(ps, &body);
    scope_pop(ps);
    ps->curfunc = oldfunc;
    lp_checknext(ps, '}');
  } else if (ls->t.token == '(') {
    /* 语法2: keyword Name(params) body end — 标准函数体语法 */
    /* 解析参数列表 () */
    lp_next(ps); /* skip '(' */
    if (ls->t.token != ')') {
      do {
        if (ls->t.token == TK_DOTS) {
          lp_next(ps);
          break;
        }
        TString *pname = lp_checkname(ps);
        scope_add_local(ps, pname, AST_ATTR_NONE);
        lp_gettypehint(ps);  /* 解析可选的类型注解 */
      } while (lp_testnext(ps, ','));
    }
    lp_checknext(ps, ')');

    /* 解析 body 并用 end 终止 */
    AstFunc *oldfunc = ps->curfunc;
    scope_push(ps, 0);
    parse_block(ps, &body);
    scope_pop(ps);
    ps->curfunc = oldfunc;
    lp_checknext(ps, TK_END);
  }

  return ast_new_stmt_typed(ps->pool, AST_STMT_KEYWORD, name, &body, line);
}


/**
 * @brief 解析 operator 语句: operator <符号> { body } 或 operator <符号>(params) body end
 * @param ps 解析器状态
 * @return 语句节点
 */
static AstStmt *parse_operator_stat(ParserState *ps) {
  LexState *ls = ps->ls;
  int line = ls->linenumber;
  lp_next(ps); /* skip 'operator' */

  /* operator 后面跟一个符号作为名称 */
  TString *name;
  if (is_nametoken(ls->t.token)) {
    name = lp_checkname(ps);
  } else {
    /* 运算符符号作为名称 */
    name = ls->t.seminfo.ts;
    lp_next(ps);
  }

  /* 解析 body */
  AstBlock body = {NULL, 0, 0};
  block_init(ps, &body);

  if (ls->t.token == '{') {
    /* 语法1: operator <符号> { body } — 向后兼容 */
    lp_next(ps); /* skip '{' */
    AstFunc *oldfunc = ps->curfunc;
    scope_push(ps, 0);
    parse_block(ps, &body);
    scope_pop(ps);
    ps->curfunc = oldfunc;
    lp_checknext(ps, '}');
  } else if (ls->t.token == '(') {
    /* 语法2: operator <符号>(params) body end — 标准函数体语法 */
    /* 解析参数列表 () */
    lp_next(ps); /* skip '(' */
    if (ls->t.token != ')') {
      do {
        if (ls->t.token == TK_DOTS) {
          lp_next(ps);
          break;
        }
        TString *pname = lp_checkname(ps);
        scope_add_local(ps, pname, AST_ATTR_NONE);
        lp_gettypehint(ps);  /* 解析可选的类型注解 */
      } while (lp_testnext(ps, ','));
    }
    lp_checknext(ps, ')');

    /* 解析 body 并用 end 终止 */
    AstFunc *oldfunc = ps->curfunc;
    scope_push(ps, 0);
    parse_block(ps, &body);
    scope_pop(ps);
    ps->curfunc = oldfunc;
    lp_checknext(ps, TK_END);
  }

  return ast_new_stmt_typed(ps->pool, AST_STMT_OPERATOR, name, &body, line);
}


/**
 * @brief 解析 global 语句
 * 支持语法：
 *   global function name() ... end — 全局函数
 *   global * — 通配符全局声明
 *   global name1, name2, ... = val1, val2, ... — 多名称全局变量声明
 *   global name1, name2, ... — 无赋值
 * @param ps 解析器状态
 * @return 语句节点
 */
static AstStmt *parse_global_stat(ParserState *ps) {
  LexState *ls = ps->ls;
  int line = ls->linenumber;
  lp_next(ps); /* skip 'global' */

  if (lp_testnext(ps, TK_FUNCTION)) {
    /* global function name() ... end */
    TString *name = lp_checkname(ps);
    int fline = ls->linenumber;
    AstFunc *f = parse_funcbody(ps, fline, 0, 0, 0);
    AstStmt *s = ast_new_stmt_localfunc(ps->pool, name, f, line);
    s->u.localfunc.local_idx = scope_find_local(ps, name);
    if (s->u.localfunc.local_idx < 0) {
      scope_add_local(ps, name, AST_ATTR_NONE);
      s->u.localfunc.local_idx = ps->curfunc->nlocals - 1;
    }
    return s;
  } else if (lp_testnext(ps, '*')) {
    /* global * — 通配符全局声明 */
    AstStmt *s = ast_new_stmt_global(ps->pool, 0, 0, line);
    s->u.global.has_wildcard = 1;
    return s;
  } else {
    /* global name1, name2, ... = val1, val2, ... */
    int nnames = 0;
    int ncap = 4;
    TString **names = cast(TString **, ast_pool_alloc(ps->pool, sizeof(TString *) * ncap));

    /* 读取逗号分隔的变量名列表 */
    do {
      if (nnames >= ncap) {
        ncap *= 2;
        TString **newnames = cast(TString **, ast_pool_alloc(ps->pool, sizeof(TString *) * ncap));
        memcpy(newnames, names, sizeof(TString *) * nnames);
        names = newnames;
      }
      names[nnames++] = lp_checkname(ps);
    } while (lp_testnext(ps, ','));

    int nvalues = 0;
    AstExpr **values = NULL;
    if (lp_testnext(ps, '=')) {
      values = parse_exprlist(ps, &nvalues);
    }

    AstStmt *s = ast_new_stmt_global(ps->pool, nnames, nvalues, line);
    {
      int i;
      for (i = 0; i < nnames; i++) {
        s->u.global.names[i] = names[i];
      }
      if (values != NULL && nvalues > 0) {
        for (i = 0; i < nvalues; i++) {
          s->u.global.values[i] = values[i];
        }
      }
    }
    return s;
  }
}


/* ================================================================
** constexpr 编译期预处理（$if / $define / $include 等指令）
** 这些指令在解析阶段处理，不产生 AST 节点
** ================================================================ */

/**
 * @brief 评估 $if 的单 token 条件
 * 支持：TK_TRUE(1), TK_FALSE(0), TK_INT(非0为真), TK_NAME(查 defines 表)
 * @param ps 解析器状态
 * @return 0=假, 1=真
 */
static int eval_const_condition(ParserState *ps) {
  LexState *ls = ps->ls;
  int val = 0;
  if (ls->t.token == TK_TRUE) val = 1;
  else if (ls->t.token == TK_FALSE) val = 0;
  else if (ls->t.token == TK_INT) val = (ls->t.seminfo.i != 0);
  else if (ls->t.token == TK_NAME) {
    if (ps->defines) {
      TValue key;
      setsvalue(ps->L, &key, ls->t.seminfo.ts);
      const TValue *v = luaH_get(ps->defines, &key);
      val = !l_isfalse(v);
    } else {
      val = 0;
    }
  } else {
    val = 0;
  }
  lp_next(ps); /* 消费条件值 */
  if (ls->t.token == TK_THEN) lp_next(ps); /* 跳过 then */
  return val;
}


/**
 * @brief 跳过 $if/$else/$elseif/$end 嵌套块（不解析为 AST）
 * 处理嵌套的 $if 块，正确处理深度计数
 * @param ps 解析器状态
 */
static void skip_dollar_block(ParserState *ps) {
  LexState *ls = ps->ls;
  int depth = 1;
  while (depth > 0 && ls->t.token != TK_EOS) {
    if (ls->t.token == TK_DOLLAR) {
      int la = luaX_lookahead(ls);
      if (la == TK_NAME) {
        const char *name = getstr(ls->lookahead.seminfo.ts);
        if (strcmp(name, "if") == 0) {
          depth++;
        } else if (strcmp(name, "end") == 0) {
          depth--;
          if (depth == 0) return; /* 不消费 $end，留给调用者处理 */
        } else if (strcmp(name, "else") == 0 || strcmp(name, "elseif") == 0) {
          if (depth == 1) return; /* 停在当前块的 else/elseif */
        }
      } else if (la == TK_IF) {
        depth++;
      } else if (la == TK_END) {
        depth--;
        if (depth == 0) return;
      } else if (la == TK_ELSE || la == TK_ELSEIF) {
        if (depth == 1) return;
      }
    }
    lp_next(ps);
  }
}


/**
 * @brief 消费 $end 结束标记
 * @param ps 解析器状态
 */
static void consume_dollar_end(ParserState *ps) {
  LexState *ls = ps->ls;
  if (ls->t.token == TK_DOLLAR) {
    lp_next(ps); /* 跳过 $ */
    if (ls->t.token == TK_END) {
      lp_next(ps);
    } else if (ls->t.token == TK_NAME && strcmp(getstr(ls->t.seminfo.ts), "end") == 0) {
      lp_next(ps);
    }
  }
}


/**
 * @brief 处理 $define name = value 编译期常量定义
 * 评估常量表达式并存入 defines 表
 * @param ps 解析器状态
 */
static void constexpr_define_stat(ParserState *ps) {
  LexState *ls = ps->ls;
  lp_next(ps); /* 跳过 'define' */

  /* 解析常量名 */
  if (!is_nametoken(ls->t.token)) {
    lp_error(ps, "expected constant name after $define");
    return;
  }
  TString *name = ls->t.seminfo.ts;
  lp_next(ps);

  /* 跳过 '=' */
  if (ls->t.token == '=') lp_next(ps);

  /* 解析常量表达式并求值 */
  AstExpr *expr = parse_expr(ps);
  TValue k;
  int is_const = 0;

  /* 简单常量求值：只支持字面量 */
  switch (expr->kind) {
    case AST_EXPR_NIL:
      setnilvalue(&k);
      is_const = 1;
      break;
    case AST_EXPR_TRUE:
      setbtvalue(&k);
      is_const = 1;
      break;
    case AST_EXPR_FALSE:
      setbfvalue(&k);
      is_const = 1;
      break;
    case AST_EXPR_INT:
      setivalue(&k, expr->u.ival);
      is_const = 1;
      break;
    case AST_EXPR_FLT:
      setfltvalue(&k, expr->u.nval);
      is_const = 1;
      break;
    case AST_EXPR_STRING:
      setsvalue(ps->L, &k, expr->u.strval);
      is_const = 1;
      break;
    default:
      break;
  }

  if (!is_const) {
    /* 尝试求值一元取反 */
    if (expr->kind == AST_EXPR_UNOP && expr->u.unop.op == AST_UN_MINUS) {
      AstExpr *operand = expr->u.unop.operand;
      if (operand->kind == AST_EXPR_INT) {
        setivalue(&k, -operand->u.ival);
        is_const = 1;
      } else if (operand->kind == AST_EXPR_FLT) {
        setfltvalue(&k, -operand->u.nval);
        is_const = 1;
      }
    }
  }

  if (!is_const) {
    lp_error(ps, "variable was not assigned a compile-time constant value");
    return;
  }

  /* 创建或获取 defines 表 */
  if (ps->defines == NULL) {
    ps->defines = luaH_new(ps->L);
    /* 锚定 defines 表防止 GC 回收 */
    sethvalue2s(ps->L, ps->L->top.p, ps->defines);
    ps->L->top.p++;
  }

  TValue key;
  setsvalue(ps->L, &key, name);
  luaH_set(ps->L, ps->defines, &key, &k);
}


/**
 * @brief 处理 $if 条件编译（递归处理 $else/$elseif/$end）
 * @param ps 解析器状态
 */
static void constexpr_if_stat(ParserState *ps) {
  int cond = eval_const_condition(ps);

  if (cond) {
    /* 条件为真：解析 body 到当前 AST */
    while (!is_block_end(ps->ls->t.token) &&
           !(ps->ls->t.token == TK_DOLLAR)) {
      AstStmt *s = parse_stat(ps);
      if (s != NULL) {
        ast_block_add_stmt(ps->pool, &ps->curfunc->body, s);
      }
      lp_testnext(ps, ';');
    }
  } else {
    /* 条件为假：跳过 token */
    skip_dollar_block(ps);
  }

  /* 处理 $else / $elseif / $end */
  if (ps->ls->t.token == TK_DOLLAR) {
    lp_next(ps); /* 跳过 $ */
    int is_else = 0, is_elseif = 0, is_end = 0;

    if (ps->ls->t.token == TK_ELSE) is_else = 1;
    else if (ps->ls->t.token == TK_ELSEIF) is_elseif = 1;
    else if (ps->ls->t.token == TK_END) is_end = 1;
    else if (ps->ls->t.token == TK_NAME) {
      const char *name = getstr(ps->ls->t.seminfo.ts);
      if (strcmp(name, "else") == 0) is_else = 1;
      else if (strcmp(name, "elseif") == 0) is_elseif = 1;
      else if (strcmp(name, "end") == 0) is_end = 1;
    }

    if (is_else) {
      lp_next(ps); /* 跳过 else */
      if (cond) {
        /* 已取 if 分支，跳过 else 块 */
        skip_dollar_block(ps);
        consume_dollar_end(ps);
      } else {
        /* 未取 if 分支，解析 else 块 */
        while (!is_block_end(ps->ls->t.token) &&
               !(ps->ls->t.token == TK_DOLLAR)) {
          AstStmt *s = parse_stat(ps);
          if (s != NULL) {
            ast_block_add_stmt(ps->pool, &ps->curfunc->body, s);
          }
          lp_testnext(ps, ';');
        }
        consume_dollar_end(ps);
      }
    } else if (is_elseif) {
      lp_next(ps); /* 跳过 elseif */
      if (cond) {
        /* 已取分支，跳过所有直到 $end */
        int depth = 1;
        while (depth > 0 && ps->ls->t.token != TK_EOS) {
          if (ps->ls->t.token == TK_DOLLAR) {
            int la = luaX_lookahead(ps->ls);
            if (la == TK_NAME) {
              const char *n = getstr(ps->ls->lookahead.seminfo.ts);
              if (strcmp(n, "if") == 0) depth++;
              else if (strcmp(n, "end") == 0) {
                depth--;
                if (depth == 0) break;
              }
            } else if (la == TK_IF) depth++;
            else if (la == TK_END) {
              depth--;
              if (depth == 0) break;
            }
          }
          lp_next(ps);
        }
        consume_dollar_end(ps);
      } else {
        /* 递归处理 elseif */
        constexpr_if_stat(ps);
      }
    } else if (is_end) {
      lp_next(ps); /* 跳过 end */
    }
  }
}


/**
 * @brief 解析 $ 预处理指令（constexpr）
 * 在解析阶段处理，不产生 AST 节点（返回 NULL）
 * 支持的指令：
 *   $if cond        - 条件编译
 *   $define name = value - 编译期常量定义
 *   $include "file" - 文件包含
 *   $haltcompiler   - 停止编译
 *   $type name = type - 类型别名定义
 *   $declare name : type <nodiscard> - 外部声明
 *   $alias ...      - 别名
 * @param ps 解析器状态
 * @return 始终返回 NULL（预处理指令不产生 AST 节点）
 */
static AstStmt *parse_constexpr_stat(ParserState *ps) {
  LexState *ls = ps->ls;
  lp_next(ps); /* 跳过 '$' */

  /* $if 条件编译 */
  if (ls->t.token == TK_IF) {
    lp_next(ps); /* 跳过 'if' */
    constexpr_if_stat(ps);
    return NULL;
  }

  /* 其他指令：$name ... */
  if (!is_nametoken(ls->t.token)) {
    lp_error(ps, "expected directive name after '$'");
    return NULL;
  }

  TString *ts = ls->t.seminfo.ts;
  const char *name = getstr(ts);
  lp_next(ps);

  if (strcmp(name, "include") == 0) {
    /* $include "filename" */
    if (ls->t.token != TK_STRING && ls->t.token != TK_RAWSTRING) {
      lp_error(ps, "expected filename string after $include");
      return NULL;
    }
    luaX_pushincludefile(ls, getstr(ls->t.seminfo.ts));
    lp_next(ps);
  }
  else if (strcmp(name, "alias") == 0) {
    /* $alias ... 跳过（类型系统指令） */
    /* 简单跳过直到行尾或遇到下一个 $ 指令 */
    while (ls->t.token != TK_EOS && ls->t.token != TK_DOLLAR &&
           ls->t.token != ';' && ls->t.token != '\n') {
      lp_next(ps);
    }
  }
  else if (strcmp(name, "haltcompiler") == 0) {
    /* $haltcompiler 跳过所有剩余 token */
    while (ls->t.token != TK_EOS) lp_next(ps);
  }
  else if (strcmp(name, "define") == 0) {
    constexpr_define_stat(ps);
  }
  else if (strcmp(name, "type") == 0) {
    /* $type name = type_expr 跳过 */
    while (ls->t.token != TK_EOS && ls->t.token != TK_DOLLAR &&
           ls->t.token != ';') {
      lp_next(ps);
    }
  }
  else if (strcmp(name, "declare") == 0) {
    /* $declare name : type <nodiscard> 跳过 */
    while (ls->t.token != TK_EOS && ls->t.token != TK_DOLLAR &&
           ls->t.token != ';') {
      lp_next(ps);
    }
  }
  else if (strcmp(name, "getproptype") == 0 ||
           strcmp(name, "gettype") == 0 ||
           strcmp(name, "getrettype") == 0 ||
           strcmp(name, "getargtype") == 0 ||
           strcmp(name, "getgeneric") == 0 ||
           strcmp(name, "getclass") == 0 ||
           strcmp(name, "getinherit") == 0 ||
           strcmp(name, "getprop") == 0 ||
           strcmp(name, "getmethods") == 0 ||
           strcmp(name, "getannotations") == 0 ||
           strcmp(name, "getparams") == 0 ||
           strcmp(name, "getstruct") == 0 ||
           strcmp(name, "getinterface") == 0 ||
           strcmp(name, "gettrait") == 0 ||
           strcmp(name, "getfn") == 0 ||
           strcmp(name, "getfns") == 0 ||
           strcmp(name, "getclosures") == 0 ||
           strcmp(name, "getarrow") == 0 ||
           strcmp(name, "getoverload") == 0 ||
           strcmp(name, "getoperator") == 0 ||
           strcmp(name, "getcommand") == 0 ||
           strcmp(name, "getkeyword") == 0 ||
           strcmp(name, "getenum") == 0 ||
           strcmp(name, "getenumval") == 0 ||
           strcmp(name, "getfunction") == 0 ||
           strcmp(name, "getglobal") == 0 ||
           strcmp(name, "getvar") == 0 ||
           strcmp(name, "getlocal") == 0 ||
           strcmp(name, "getconst") == 0 ||
           strcmp(name, "getupvalue") == 0 ||
           strcmp(name, "getclassname") == 0 ||
           strcmp(name, "getself") == 0 ||
           strcmp(name, "getasm") == 0 ||
           strcmp(name, "geterr") == 0) {
    /* 类型内省指令：跳过到行尾或下一个 $ 指令 */
    while (ls->t.token != TK_EOS && ls->t.token != TK_DOLLAR &&
           ls->t.token != ';') {
      lp_next(ps);
    }
  }
  else {
    /* 未知指令：跳过 */
    lp_error(ps, "unknown $ directive");
  }

  return NULL;
}


/**
 * @brief 解析 C++ 风格类型声明：TypeKeyword name [= value] 或 TypeKeyword name(args)
 * 将类型关键字声明转换为 local 变量声明或函数声明
 * @param ps 解析器状态
 * @return 语句节点
 */
static AstStmt *parse_declaration_stat(ParserState *ps) {
  LexState *ls = ps->ls;
  int line = ls->linenumber;
  lp_next(ps); /* 跳过类型关键字 */

  TString *name = lp_checkname(ps);

  if (ls->t.token == '(') {
    /* 函数声明：TypeKeyword name(args) { body } */
    AstFunc *f = parse_funcbody(ps, ls->linenumber, 0, 0, 0);
    AstExpr *func_expr = ast_new_expr_func(ps->pool, f, 0, line);

    /* 构建赋值语句：目标 name，值 func_expr */
    AstExpr *var = ast_new_expr_ident(ps->pool, name, line);
    AstAssignTarget tgt;
    AstExpr **values;
    AstStmt *s;
    expr_to_target(ps, var, &tgt);
    values = cast(AstExpr **, ast_pool_alloc(ps->pool, sizeof(AstExpr *)));
    values[0] = func_expr;
    s = ast_new_stmt_assign(ps->pool, 1, 1, line);
    s->u.assign.targets = cast(AstAssignTarget *,
      ast_pool_alloc(ps->pool, sizeof(AstAssignTarget)));
    s->u.assign.targets[0] = tgt;
    s->u.assign.values = values;
    return s;
  }

  /* 变量声明：TypeKeyword name [= value] */
  /* 在函数内部使用 local，在顶层使用 global */
  int is_global = (ps->curfunc == NULL);
  AstStmt *s = ast_new_stmt_local(ps->pool, 1, &name, is_global ? 0 : 1, line);
  s->u.local.attrs[0] = AST_ATTR_NONE;

  if (lp_testnext(ps, '=')) {
    AstExpr *value = parse_expr(ps);
    s->u.local.values = cast(AstExpr **, ast_pool_alloc(ps->pool, sizeof(AstExpr *)));
    s->u.local.values[0] = value;
  }

  if (!is_global) {
    scope_add_local(ps, name, AST_ATTR_NONE);
  }

  return s;
}


/**
 * @brief 解析单条语句
 * @param ps 解析器状态
 * @return 语句节点
 */
static AstStmt *parse_stat(ParserState *ps) {
  LexState *ls = ps->ls;
  int line = ls->linenumber;
  AstStmt *s;

  switch (ls->t.token) {
    case TK_CONST: {  /* const 声明（等同于 local const） */
      lp_next(ps); /* skip 'const' */
      if (lp_testnext(ps, TK_FUNCTION)) {
        lp_error(ps, "function cannot be declared as const");
        return NULL;
      }
      return parse_local_var_list(ps, 1);
    }

    case TK_LOCAL:
      return parse_local_stat(ps);

    case TK_GLOBAL:  /* global function name() ... end / global name = val */
      return parse_global_stat(ps);

    case TK_DO: {
      lp_next(ps);
      s = ast_new_stmt_block(ps->pool, line);
      scope_push(ps, 0);
      parse_block(ps, &s->u.block.block);
      scope_pop(ps);
      lp_checknext(ps, TK_END);
      return s;
    }

    case TK_IF:
      return parse_if_stat(ps);

    case TK_WHILE:
      return parse_while_stat(ps);

    case TK_REPEAT:
      return parse_repeat_stat(ps);

    case TK_RETURN:
      return parse_return_stat(ps);

    case TK_BREAK: {
      int level = 1;
      lp_next(ps);
      if (lp_check(ps, TK_INT)) {
        level = (int)ls->t.seminfo.i;
        if (level < 1) level = 1;
        lp_next(ps);
      }
      return ast_new_stmt_break(ps->pool, level, line);
    }

    case TK_CONTINUE: {
      int level = 1;
      lp_next(ps);
      if (lp_check(ps, TK_INT)) {
        level = (int)ls->t.seminfo.i;
        if (level < 1) level = 1;
        lp_next(ps);
      }
      return ast_new_stmt_continue(ps->pool, level, line);
    }

    case TK_GOTO: {
      TString *name;
      lp_next(ps);
      /* goto continue/break/goto 等关键字特殊处理 */
      int token = ls->t.token;
      if (token == TK_CONTINUE || token == TK_BREAK || token == TK_GOTO ||
          token == TK_DELETE || token == TK_GUARD || token == TK_LET) {
        name = ls->t.seminfo.ts;
        lp_next(ps);
      } else {
        name = lp_checkname(ps);
      }
      return ast_new_stmt_goto(ps->pool, name, line);
    }

    case TK_DBCOLON: {
      TString *name;
      lp_next(ps);
      /* 标签也支持关键字作为标签名 */
      if (is_nametoken(ls->t.token)) {
        name = lp_checkname(ps);
      } else {
        /* 关键字作为标签名: ::continue::, ::break:: 等 */
        name = ls->t.seminfo.ts;
        lp_next(ps);
      }
      lp_checknext(ps, TK_DBCOLON);
      return ast_new_stmt_label(ps->pool, name, line);
    }

    case TK_FOR:
      return parse_for_stat(ps);

    case TK_FUNCTION:
      return parse_func_stat(ps, 0);

    case TK_LET: {  /* let 变量声明，类似 local */
      lp_next(ps); /* skip 'let' */

      /* 处理解构赋值：let {a, b} = t 或 let [a, b] = t */
      if (ls->t.token == '{' || ls->t.token == '[') {
        int is_array = (ls->t.token == '[');
        int end_char = is_array ? ']' : '}';
        int nvars = 0;
        int cap = 4;
        TString **varnames = cast(TString **, ast_pool_alloc(ps->pool, cap * sizeof(TString *)));
        AstExpr **defaults = NULL;
        AstExpr *source = NULL;

        lp_next(ps); /* skip { or [ */
        do {
          TString *name;
          AstExpr *def = NULL;
          if (nvars >= cap) {
            int new_cap = cap * 2;
            TString **new_names = cast(TString **,
              ast_pool_alloc(ps->pool, new_cap * sizeof(TString *)));
            memcpy(new_names, varnames, nvars * sizeof(TString *));
            varnames = new_names;
            if (defaults) {
              AstExpr **new_defaults = cast(AstExpr **,
                ast_pool_alloc(ps->pool, new_cap * sizeof(AstExpr *)));
              memcpy(new_defaults, defaults, nvars * sizeof(AstExpr *));
              defaults = new_defaults;
            }
            cap = new_cap;
          }
          name = lp_checkname(ps);
          varnames[nvars] = name;
          scope_add_local(ps, name, AST_ATTR_NONE);
          /* 解析默认值 */
          if (lp_testnext(ps, '=')) {
            if (!defaults) {
              defaults = cast(AstExpr **, ast_pool_alloc(ps->pool, cap * sizeof(AstExpr *)));
              memset(defaults, 0, cap * sizeof(AstExpr *));
            }
            def = parse_expr(ps);
            defaults[nvars] = def;
          }
          nvars++;
        } while (lp_testnext(ps, ','));
        lp_checknext(ps, end_char);

        /* 解析 = expr */
        if (lp_testnext(ps, '=')) {
          source = parse_expr(ps);
        }

        return ast_new_stmt_take(ps->pool, nvars, varnames, defaults, source, is_array, line);
      }

      /* 处理 let take {a, b, ...} = expr */
      if (lp_testnext(ps, TK_TAKE)) {
        int nvars = 0;
        int cap = 4;
        TString **varnames = cast(TString **, ast_pool_alloc(ps->pool, cap * sizeof(TString *)));
        AstExpr *source = NULL;

        if (lp_testnext(ps, '{')) {
          do {
            TString *name;
            if (nvars >= cap) {
              int new_cap = cap * 2;
              TString **new_names = cast(TString **,
                ast_pool_alloc(ps->pool, new_cap * sizeof(TString *)));
              memcpy(new_names, varnames, nvars * sizeof(TString *));
              varnames = new_names;
              cap = new_cap;
            }
            name = lp_checkname(ps);
            varnames[nvars++] = name;
            scope_add_local(ps, name, AST_ATTR_NONE);
          } while (lp_testnext(ps, ','));
          lp_checknext(ps, '}');
        }

        if (lp_testnext(ps, '=')) {
          source = parse_expr(ps);
        }

        return ast_new_stmt_take(ps->pool, nvars, varnames, NULL, source, 0, line);
      }

      if (lp_testnext(ps, TK_FUNCTION)) {
        /* let function name() ... end */
        TString *name = lp_checkname(ps);
        int fline = ls->linenumber;
        AstFunc *f = parse_funcbody(ps, fline, 0, 0, 0);
        AstStmt *s = ast_new_stmt_localfunc(ps->pool, name, f, line);
        s->u.localfunc.local_idx = scope_find_local(ps, name);
        if (s->u.localfunc.local_idx < 0) {
          scope_add_local(ps, name, AST_ATTR_NONE);
          s->u.localfunc.local_idx = ps->curfunc->nlocals - 1;
        }
        return s;
      }
      /* let name, ... = expr — 复用 local 变量声明逻辑 */
      {
        int nnames = 0;
        int nvalues = 0;
        int cap = 4;
        TString **names = cast(TString **, ast_pool_alloc(ps->pool, cap * sizeof(TString *)));
        int *attrs = cast(int *, ast_pool_alloc(ps->pool, cap * sizeof(int)));
        AstExpr **values = NULL;
        AstStmt *s;

        do {
          TString *name;
          int attr = AST_ATTR_NONE;
          if (nnames >= cap) {
            int new_cap = cap * 2;
            TString **new_names = cast(TString **,
              ast_pool_alloc(ps->pool, new_cap * sizeof(TString *)));
            int *new_attrs = cast(int *,
              ast_pool_alloc(ps->pool, new_cap * sizeof(int)));
            memcpy(new_names, names, nnames * sizeof(TString *));
            memcpy(new_attrs, attrs, nnames * sizeof(int));
            names = new_names;
            attrs = new_attrs;
            cap = new_cap;
          }
          name = lp_checkname(ps);
          if (lp_testnext(ps, TK_CONST)) attr = AST_ATTR_CONST;
          names[nnames] = name;
          attrs[nnames] = attr;
          nnames++;
        } while (lp_testnext(ps, ','));

        if (lp_testnext(ps, '=')) {
          values = parse_exprlist(ps, &nvalues);
        }

        s = ast_new_stmt_local(ps->pool, nnames, names, nvalues, line);
        {
          int i;
          for (i = 0; i < nnames; i++) {
            s->u.local.names[i] = names[i];
            s->u.local.attrs[i] = attrs[i];
            scope_add_local(ps, names[i], attrs[i]);
          }
          if (values != NULL && nvalues > 0) {
            s->u.local.values = values;
          }
        }
        return s;
      }
    }

    case TK_WHEN: {  /* when 语句 → 复用 ifstmt AST */
      int stmt_line = ls->linenumber;
      AstStmt *s = ast_new_stmt_if(ps->pool, stmt_line);
      int arm_cap = 2;
      int narms = 0;
      AstIfArm *arms = cast(AstIfArm *,
        ast_pool_alloc(ps->pool, arm_cap * sizeof(AstIfArm)));

      lp_next(ps); /* skip 'when' */
      for (;;) {
        AstIfArm *arm;
        if (narms >= arm_cap) {
          int new_cap = arm_cap * 2;
          AstIfArm *new_arms = cast(AstIfArm *,
            ast_pool_alloc(ps->pool, new_cap * sizeof(AstIfArm)));
          memcpy(new_arms, arms, narms * sizeof(AstIfArm));
          arms = new_arms;
          arm_cap = new_cap;
        }
        arm = &arms[narms++];
        memset(arm, 0, sizeof(*arm));
        arm->cond = parse_expr(ps);
        lp_checknext(ps, TK_THEN);
        scope_push(ps, 0);
        parse_block(ps, &arm->body);
        scope_pop(ps);

        if (lp_testnext(ps, TK_CASE)) continue;
        if (lp_testnext(ps, TK_ELSE)) {
          s->u.ifstmt.has_else = 1;
          scope_push(ps, 0);
          parse_block(ps, &s->u.ifstmt.else_body);
          scope_pop(ps);
        }
        break;
      }
      lp_checknext(ps, TK_END);

      s->u.ifstmt.arms = arms;
      s->u.ifstmt.narms = narms;
      return s;
    }

    case TK_SWITCH: {  /* switch 语句 */
      return parse_switch_stat(ps);
    }

    case TK_GUARD: {  /* guard 语句 */
      return parse_guard_stat(ps);
    }

    case TK_ASYNC: {  /* async function */
      lp_next(ps); /* skip 'async' */
      if (lp_check(ps, TK_FUNCTION)) {
        return parse_func_stat(ps, 1);  /* is_async = 1 */
      }
      lp_error(ps, "expected 'function' after 'async'");
      return NULL;
    }

    case TK_AWAIT: {  /* await 语句（丢弃结果） */
      int stmt_line = ls->linenumber;
      lp_next(ps); /* skip 'await' */
      AstExpr *e = parse_expr(ps);
      AstExpr *await_expr = ast_new_expr_unop(ps->pool, AST_UN_AWAIT, e, stmt_line);
      return ast_new_stmt_expr(ps->pool, await_expr, stmt_line);
    }

    case TK_EXPORT: {  /* export 语句 */
      AstStmt *s = NULL;
      lp_next(ps); /* skip 'export' */
      if (lp_check(ps, TK_FUNCTION)) {
        /* export function name(args) body end
         * 必须创建局部函数（而非全局函数），以便导出表能正确引用 */
        lp_next(ps); /* skip 'function' */
        TString *fname = lp_checkname(ps);
        int line = ls->linenumber;

        /* 创建局部变量并注册导出 */
        scope_add_local(ps, fname, AST_ATTR_NONE);
        ast_block_add_export(ps->pool, &ps->curfunc->body, fname);

        /* 解析函数体 */
        AstFunc *f = parse_funcbody(ps, ls->linenumber, 0, 0, 0);
        AstExpr *func_expr = ast_new_expr_func(ps->pool, f, 0, line);

        /* 创建局部变量赋值语句 */
        AstStmt *s = ast_new_stmt_local(ps->pool, 1, &fname, 1, line);
        s->u.local.attrs[0] = AST_ATTR_NONE;
        s->u.local.values = cast(AstExpr **, ast_pool_alloc(ps->pool, sizeof(AstExpr *)));
        s->u.local.values[0] = func_expr;

        return s;
      }
      if (lp_testnext(ps, TK_LOCAL)) {
        s = parse_local_stat(ps);
        /* 注册局部变量导出 */
        if (s && s->kind == AST_STMT_LOCAL) {
          for (int i = 0; i < s->u.local.nnames; i++) {
            ast_block_add_export(ps->pool, &ps->curfunc->body, s->u.local.names[i]);
          }
        }
        return s;
      }
      if (lp_testnext(ps, TK_CONST)) {
        if (lp_check(ps, TK_FUNCTION)) {
          lp_error(ps, "function cannot be declared as const");
          return NULL;
        }
        s = parse_local_var_list(ps, 1);
        if (s && s->kind == AST_STMT_LOCAL) {
          for (int i = 0; i < s->u.local.nnames; i++) {
            ast_block_add_export(ps->pool, &ps->curfunc->body, s->u.local.names[i]);
          }
        }
        return s;
      }
      /* export struct */
      if (ls->t.token == TK_STRUCT) {
        s = parse_struct_stat(ps);
        if (s && s->kind == AST_STMT_STRUCT) {
          ast_block_add_export(ps->pool, &ps->curfunc->body, s->u.nsstruct.name);
        }
        return s;
      }
      /* export enum */
      if (ls->t.token == TK_ENUM) {
        s = parse_enum_stat(ps);
        if (s && s->kind == AST_STMT_ENUM) {
          ast_block_add_export(ps->pool, &ps->curfunc->body, s->u.enumstmt.name);
        }
        return s;
      }
      /* export 软关键字: class, interface, trait, abstract, final, sealed */
      if (ls->t.token == TK_NAME) {
        int skw = lp_softkw_is(ps, "class") ? 1 :
                  lp_softkw_is(ps, "interface") ? 2 :
                  lp_softkw_is(ps, "trait") ? 3 :
                  lp_softkw_is(ps, "abstract") ? 4 :
                  lp_softkw_is(ps, "final") ? 5 :
                  lp_softkw_is(ps, "sealed") ? 6 : 0;
        if (skw == 1) {
          /* export class */
          s = parse_class_stat(ps, 0);
          if (s && s->kind == AST_STMT_CLASS) ast_block_add_export(ps->pool, &ps->curfunc->body, s->u.classstmt.name);
          return s;
        } else if (skw == 2) {
          /* export interface */
          s = parse_interface_stat(ps);
          if (s && s->kind == AST_STMT_INTERFACE) ast_block_add_export(ps->pool, &ps->curfunc->body, s->u.nsstruct.name);
          return s;
        } else if (skw == 3) {
          /* export trait */
          s = parse_trait_stat(ps);
          if (s && s->kind == AST_STMT_TRAIT) ast_block_add_export(ps->pool, &ps->curfunc->body, s->u.nsstruct.name);
          return s;
        } else if (skw == 4) {
          /* export abstract class */
          lp_next(ps); /* skip 'abstract' */
          if (!lp_softkw_is(ps, "class")) {
            lp_error(ps, "'abstract' export must be followed by 'class'");
            return NULL;
          }
          s = parse_class_stat(ps, CLASS_FLAG_ABSTRACT);
          if (s && s->kind == AST_STMT_CLASS) ast_block_add_export(ps->pool, &ps->curfunc->body, s->u.classstmt.name);
          return s;
        } else if (skw == 5) {
          /* export final class */
          lp_next(ps); /* skip 'final' */
          if (!lp_softkw_is(ps, "class")) {
            lp_error(ps, "'final' export must be followed by 'class'");
            return NULL;
          }
          s = parse_class_stat(ps, CLASS_FLAG_FINAL);
          if (s && s->kind == AST_STMT_CLASS) ast_block_add_export(ps->pool, &ps->curfunc->body, s->u.classstmt.name);
          return s;
        } else if (skw == 6) {
          /* export sealed class */
          lp_next(ps); /* skip 'sealed' */
          if (!lp_softkw_is(ps, "class")) {
            lp_error(ps, "'sealed' export must be followed by 'class'");
            return NULL;
          }
          s = parse_class_stat(ps, CLASS_FLAG_SEALED);
          if (s && s->kind == AST_STMT_CLASS) ast_block_add_export(ps->pool, &ps->curfunc->body, s->u.classstmt.name);
          return s;
        }
      }
      /* export name = value (模块级变量导出) */
      if (is_nametoken(ls->t.token)) {
        TString *name = lp_checkname(ps);
        if (lp_testnext(ps, '=')) {
          AstExpr *value = parse_expr(ps);
          AstStmt *s = ast_new_stmt_local(ps->pool, 1, NULL, 1, line);
          s->u.local.names[0] = name;
          s->u.local.attrs[0] = AST_ATTR_NONE;
          s->u.local.values = cast(AstExpr **, ast_pool_alloc(ps->pool, sizeof(AstExpr *)));
          s->u.local.values[0] = value;
          scope_add_local(ps, name, AST_ATTR_NONE);
          ast_block_add_export(ps->pool, &ps->curfunc->body, name);
          return s;
        }
        lp_error(ps, "expected '=' after export variable name");
        return NULL;
      }
      lp_error(ps, "expected 'function', 'local', 'const', 'struct', 'enum', 'class', 'interface', 'trait', or variable name after 'export'");
      return NULL;
    }

    /* C++ 风格类型声明：TypeKeyword name [= value] */
    case TK_TYPE_INT:
    case TK_TYPE_FLOAT:
    case TK_DOUBLE:
    case TK_BOOL:
    case TK_VOID:
    case TK_CHAR:
    case TK_LONG: {
      return parse_declaration_stat(ps);
    }

    case TK_DELETE: {  /* delete 语句 */
      int stmt_line = ls->linenumber;
      lp_next(ps); /* skip 'delete' */
      AstExpr *e = parse_suffixedexpr(ps, parse_primary(ps));
      AstAssignTarget tgt;
      expr_to_target(ps, e, &tgt);
      AstExpr *nil_val = ast_new_expr_nil(ps->pool, stmt_line);
      AstStmt *s = ast_new_stmt_assign(ps->pool, 1, 1, stmt_line);
      *s->u.assign.targets = tgt;
      s->u.assign.values[0] = nil_val;
      return s;
    }

    case TK_STRUCT: {  /* struct 定义 */
      return parse_struct_stat(ps);
    }
    case TK_SUPERSTRUCT: {  /* superstruct/trait 定义 */
      return parse_superstruct_stat(ps);
    }
    case TK_ENUM: {  /* enum 定义 */
      return parse_enum_stat(ps);
    }
    case TK_WITH: {  /* with 语句 */
      return parse_with_stat(ps);
    }
    case TK_ASM: {  /* asm 语句 */
      return parse_asm_stat(ps);
    }
    case TK_CONCEPT: {  /* concept 语句 */
      return parse_concept_stat(ps);
    }

    case TK_COMMAND: {  /* command 语句 */
      return parse_command_stat(ps);
    }

    case TK_KEYWORD: {  /* keyword 语句 */
      return parse_keyword_stat(ps);
    }

    case TK_OPERATOR: {  /* operator 语句 */
      return parse_operator_stat(ps);
    }

    case TK_DOLLAR: {  /* $ if ... 预处理表达式 或 $embed/$object */
      int la = lp_lookahead(ps);
      if (la == TK_NAME) {
        /* 检查是否是 $embed 或 $object 表达式 */
        const char *name = getstr(ps->ls->lookahead.seminfo.ts);
        if (strcmp(name, "embed") == 0 || strcmp(name, "object") == 0) {
          /* 作为表达式语句处理 */
          goto default_expr;
        }
      }
      if (la == TK_IF || la == TK_ELSE || la == TK_ELSEIF || la == TK_END || la == TK_NAME) {
        return parse_constexpr_stat(ps);
      }
      /* 否则作为表达式语句处理 */
      goto default_expr;
    }

    case TK_TRY: {  /* try-catch-finally */
      return parse_try_stat(ps);
    }

    case TK_DEFER: {  /* defer 语句 */
      return parse_defer_stat(ps);
    }

    case TK_NAMESPACE: {  /* namespace 定义 */
      return parse_namespace_stat(ps);
    }

    case TK_USING: {  /* using 语句 */
      return parse_using_stat(ps);
    }

    case ';': {
      lp_next(ps);
      return ast_new_stmt_empty(ps->pool, line);
    }

    case '@': {  /* 装饰器: @expr 或 lambda: || -> expr */
      /* 在语句上下文中，@ 后跟 name 视为装饰器 */
      int la = lp_lookahead(ps);
      if (la == TK_NAME || is_nametoken(la)) {
        /* 收集所有连续装饰器表达式 */
        AstExpr **decorators = NULL;
        int ndecorators = 0;
        int dec_cap = 0;

        while (ls->t.token == '@') {
          lp_next(ps); /* skip '@' */
          AstExpr *dec_expr = parse_simpleexpr(ps);
          /* 动态扩容 */
          if (ndecorators >= dec_cap) {
            int new_cap = (dec_cap == 0) ? 4 : dec_cap * 2;
            AstExpr **new_arr = cast(AstExpr **,
              ast_pool_alloc(ps->pool, new_cap * sizeof(AstExpr *)));
            if (decorators) {
              memcpy(new_arr, decorators, ndecorators * sizeof(AstExpr *));
            }
            decorators = new_arr;
            dec_cap = new_cap;
          }
          decorators[ndecorators++] = dec_expr;
        }

        /* 解析被装饰的语句 */
        AstStmt *s = parse_stat(ps);

        /* 附加装饰器到语句 */
        if (s && ndecorators > 0) {
          s->decorators = decorators;
          s->ndecorators = ndecorators;
        }

        return s;
      }
      /* 否则按 lambda 表达式处理（|| -> expr） */
      goto default_expr;
    }

    default: {
    default_expr: {
      /* 解析表达式语句、赋值、复合赋值、自增 */
      int stmt_line = ls->linenumber;

      /* 尝试 Shell 风格命令调用: cmd arg1 arg2 ... */
      {
        AstStmt *cmd_stmt = lp_try_command_call(ps);
        if (cmd_stmt) return cmd_stmt;
      }

      /* 软关键字检查：class/trait/interface/match/abstract/final/sealed
       * 这些关键字不是独立 token，而是 TK_NAME，需要在表达式解析前检查 */
      if (ls->t.token == TK_NAME) {
        if (lp_softkw_is(ps, "class")) {
          return parse_class_stat(ps, 0);
        }
        if (lp_softkw_is(ps, "trait")) {
          return parse_trait_stat(ps);
        }
        if (lp_softkw_is(ps, "interface")) {
          return parse_interface_stat(ps);
        }
        if (lp_softkw_is(ps, "match")) {
          return parse_match_stat(ps);
        }
        /* abstract/final/sealed 是 class 的修饰符 */
        if (lp_softkw_is(ps, "abstract")) {
          lp_next(ps); /* skip 'abstract' */
          if (lp_softkw_is(ps, "class")) {
            return parse_class_stat(ps, CLASS_FLAG_ABSTRACT);
          }
          /* abstract 后必须跟 class */
          lp_error(ps, "'abstract' must be followed by 'class'");
        }
        if (lp_softkw_is(ps, "final")) {
          lp_next(ps); /* skip 'final' */
          if (lp_softkw_is(ps, "class")) {
            return parse_class_stat(ps, CLASS_FLAG_FINAL);
          }
          /* final 后必须跟 class */
          lp_error(ps, "'final' must be followed by 'class'");
        }
        if (lp_softkw_is(ps, "sealed")) {
          lp_next(ps); /* skip 'sealed' */
          if (lp_softkw_is(ps, "class")) {
            return parse_class_stat(ps, CLASS_FLAG_SEALED);
          }
          /* sealed 后必须跟 class */
          lp_error(ps, "'sealed' must be followed by 'class'");
        }
        /* trait require 声明: require function name(args) - 无函数体 */
        if (lp_softkw_is(ps, "require")) {
          int la = lp_lookahead(ps);
          if (la == TK_FUNCTION) {
            lp_next(ps); /* skip 'require' */
            lp_next(ps); /* skip 'function' */
            lp_checkname(ps); /* skip function name */
            if (lp_testnext(ps, '(')) {
              while (!lp_check(ps, ')') && !lp_check(ps, TK_EOS)) {
                lp_next(ps);
              }
              lp_checknext(ps, ')');
            }
            return ast_new_stmt_empty(ps->pool, stmt_line);
          }
          /* require 后不跟 function，则作为普通表达式 */
        }

        /* C++ 风格类型声明：TK_NAME TK_NAME [= value] at top level */
        if (ps->curfunc == NULL) {
          int la = lp_lookahead(ps);
          if (la == TK_NAME) {
            /* 检查第二个 name 后面是否跟着表达式起始（中缀调用） */
            int la2 = lp_lookahead2(ps);
            if (!is_expr_start_token(la2)) {
              /* 不是中缀调用，视为 C++ 风格类型声明 */
              return parse_declaration_stat(ps);
            }
          }
        }
      }

      /* 检查前缀自增 ++x */
      if (lp_testnext(ps, TK_PLUSPLUS)) {
        AstStmt *s;
        AstExpr *e = parse_simpleexpr(ps);
        AstAssignTarget tgt;
        expr_to_target(ps, e, &tgt);
        s = ast_new_stmt_incr(ps->pool, AST_INCR_PRE_INC, stmt_line);
        s->u.incr.target = cast(AstAssignTarget *,
          ast_pool_alloc(ps->pool, sizeof(AstAssignTarget)));
        *s->u.incr.target = tgt;
        return s;
      }

      {
        AstExpr *e = parse_expr(ps);
        int line = ls->linenumber;

        /* 检查海象操作符: name := expr 或 suffixedexp := expr */
        if (lp_testnext(ps, TK_WALRUS)) {
          AstExpr *rhs = parse_expr(ps);
          /* 如果左侧是简单变量名，创建 AST_EXPR_WALRUS 节点 */
          if (e->kind == AST_EXPR_IDENT) {
            AstExpr *walrus = ast_new_expr_walrus(ps->pool, e->u.strval, rhs, stmt_line);
            return ast_new_stmt_expr(ps->pool, walrus, stmt_line);
          }
          /* 复杂表达式：创建赋值语句 */
          {
            AstAssignTarget tgt;
            AstStmt *s;
            expr_to_target(ps, e, &tgt);
            s = ast_new_stmt_assign(ps->pool, 1, 1, stmt_line);
            s->u.assign.targets = cast(AstAssignTarget *,
              ast_pool_alloc(ps->pool, sizeof(AstAssignTarget)));
            s->u.assign.targets[0] = tgt;
            s->u.assign.values[0] = rhs;
            return s;
          }
        }

        /* 检查后缀自增 x++ */
        if (lp_testnext(ps, TK_PLUSPLUS)) {
          AstStmt *s;
          AstAssignTarget tgt;
          expr_to_target(ps, e, &tgt);
          s = ast_new_stmt_incr(ps->pool, AST_INCR_POST_INC, line);
          s->u.incr.target = cast(AstAssignTarget *,
            ast_pool_alloc(ps->pool, sizeof(AstAssignTarget)));
          *s->u.incr.target = tgt;
          return s;
        }

        /* 检查复合赋值运算符 */
        {
          int comp_op = get_compound_binop(ls->t.token);
          if (comp_op != -1) {
            AstStmt *s;
            AstAssignTarget tgt;
            AstExpr *value;
            lp_next(ps); /* skip compound op */
            value = parse_expr(ps);
            expr_to_target(ps, e, &tgt);
            s = ast_new_stmt_compound(ps->pool, (AstBinOp)comp_op, 1, value, line);
            s->u.compound.targets = cast(AstAssignTarget *,
              ast_pool_alloc(ps->pool, sizeof(AstAssignTarget)));
            s->u.compound.targets[0] = tgt;
            return s;
          }
        }

        /* 检查普通赋值 = 或多目标赋值 , */
        if (lp_check(ps, '=') || lp_check(ps, ',')) {
          int t_cap = 4;
          int ntargets = 0;
          int nvalues = 0;
          AstAssignTarget *targets;
          AstExpr **values;
          AstStmt *s;

          targets = cast(AstAssignTarget *,
            ast_pool_alloc(ps->pool, t_cap * sizeof(AstAssignTarget)));

          /* 添加第一个目标 */
          expr_to_target(ps, e, &targets[ntargets++]);

          /* 收集更多逗号分隔的目标 */
          while (lp_testnext(ps, ',')) {
            AstExpr *nexte = parse_expr(ps);
            if (ntargets >= t_cap) {
              int new_cap = t_cap * 2;
              AstAssignTarget *new_targets = cast(AstAssignTarget *,
                ast_pool_alloc(ps->pool, new_cap * sizeof(AstAssignTarget)));
              memcpy(new_targets, targets, ntargets * sizeof(AstAssignTarget));
              targets = new_targets;
              t_cap = new_cap;
            }
            expr_to_target(ps, nexte, &targets[ntargets++]);
          }

          lp_checknext(ps, '='); /* skip '=' */

          /* 解析右侧值列表 */
          values = parse_exprlist(ps, &nvalues);

          s = ast_new_stmt_assign(ps->pool, ntargets, nvalues, stmt_line);
          s->u.assign.targets = targets;
          s->u.assign.values = values;
          return s;
        }

        /* 普通表达式语句（函数调用） */
        s = ast_new_stmt_expr(ps->pool, e, stmt_line);
        return s;
      }
      }  /* closes default_expr: { */
    }
  }
}


/* ============================================================
 *                       主入口函数
 * ============================================================ */

/**
 * @brief 解析源码并构建AST主入口
 * @param L Lua状态机
 * @param z 输入流
 * @param buff 词法缓冲区
 * @param dyd 动态数据
 * @param name 源码文件名
 * @param firstchar 第一个预读字符
 * @return 构建好的AstChunk指针
 */
AstChunk *luaY_parse_ast(lua_State *L, ZIO *z, struct Mbuffer *buff,
                         struct Dyndata *dyd, const char *name, int firstchar) {
  LexState lexstate;
  ParserState ps;
  TString *sourcename;
  AstFunc *mainfunc;

  sourcename = luaS_new(L, name);

  memset(&lexstate, 0, sizeof(lexstate));

  lexstate.buff = buff;
  lexstate.dyd = dyd;
  dyd->actvar.n = 0;
  dyd->gt.n = 0;
  dyd->label.n = 0;

  lexstate.h = luaH_new(L);
  sethvalue2s(L, L->top.p, lexstate.h);
  luaD_inctop(L);

  luaX_setinput(L, &lexstate, z, sourcename, firstchar);

  memset(&ps, 0, sizeof(ps));
  ps.L = L;
  ps.ls = &lexstate;
  ps.nerr = 0;
  ps.func_idx_counter = 1; /* main_func是0，从1开始 */
  ps.defines = NULL; /* $define 常量表，按需创建 */

  ps.pool = cast(AstPool *, luaM_new(L, AstPool));
  ast_pool_init(L, ps.pool);
  ps.chunk = ast_new_chunk(ps.pool, sourcename);
  mainfunc = ps.chunk->main_func;
  mainfunc->line_defined = lexstate.linenumber;
  ps.curfunc = mainfunc;

  lp_next(&ps);
  /* 主函数 {} 快捷方式：将 { 开头的源码视为 return { ... } */
  /* 参考 lparser.c mainfunc 第13916-13917行 */
  if (ps.ls->t.token == '{') {
    int nvalues = 0;
    AstExpr **values = parse_exprlist(&ps, &nvalues);
    AstStmt *ret = ast_new_stmt_return(ps.pool, nvalues, ps.ls->linenumber);
    ret->u.retstmt.values = values;
    ast_block_add_stmt(ps.pool, &mainfunc->body, ret);
  } else {
    scope_push(&ps, 0);
    parse_block(&ps, &mainfunc->body);
    scope_pop(&ps);
  }

  lp_checknext(&ps, TK_EOS);

  ps.chunk->pool = ps.pool;
  L->top.p--;
  return ps.chunk;
}
