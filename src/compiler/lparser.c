/*
** $Id: lparser.c $
** Lua Parser
** See Copyright Notice in lua.h
*/

#define lparser_c
#define LUA_CORE

#include "lprefix.h"


#include <limits.h>
#include <string.h>
#include <stdio.h>

#include "lua.h"

#include "lapi.h"
#include "lauxlib.h"
#include "lcode.h"
#include "lclass.h"
#include "ldebug.h"
#include "ldo.h"
#include "lfunc.h"
#include "llex.h"
#include "lmem.h"
#include "lobject.h"
#include "lopcodes.h"
#include "lparser.h"
#include "lstate.h"
#include "lstring.h"
#include "ltable.h"
#include "lmap.h"
#include "ltm.h"
#include "lopnames.h"
#include "lobfuscate.h"
#include "last.h"
#include "last_parse.h"
#include "last_serialize.h"
#include "lcodegen.h"

__attribute__((noinline))
void lparser_vmp_hook_point(void) {
  VMP_MARKER(lparser_vmp);
}


extern void luaX_pushincludefile(LexState *ls, const char *filename);
extern void luaX_addalias(LexState *ls, TString *name, Token *tokens, int ntokens);


/* maximum number of local variables per function (LXCLUA: extended registers to 512) */
#define MAXVARS		512


/* because all strings are unified by the scanner, the parser
   can use pointer equality for string equality */

/*
** prototypes for recursive non-terminal functions
*/
void statement (LexState *ls);
void expr (LexState *ls, expdesc *v);
static int explist (LexState *ls, expdesc *v);
static void fixforjump (FuncState *fs, int pc, int dest, int back);
static void parse_test_value (LexState *ls, expdesc *v, int line, int allow_or);
static void simpleexp (LexState *ls, expdesc *v);

static int astparserstat (LexState *ls, int line);  /* astparser 块解析 */

void retstat (LexState *ls);
static TypeHint *gettypehint (LexState *ls);
static void check_type_compatibility(LexState *ls, TypeHint *target, expdesc *e);
static TypeHint *typehint_new(LexState *ls);
static void checktypehint (LexState *ls, TypeHint *th);
static void breakstat (LexState *ls);
static void buildglobal (LexState *ls, TString *varname, expdesc *var);
static int new_varkind (LexState *ls, TString *name, lu_byte kind);
static void switchstat (LexState *ls, int line);  /* switch语句的前向声明 */
static void matchstat (LexState *ls, int line);
static void matchexpr (LexState *ls, expdesc *v);  /* match表达式的前向声明 */
static void trystat (LexState *ls, int line);     /* try语句的前向声明 */
static void guardstat (LexState *ls, int line);   /* guard语句的前向声明 */
static void withstat (LexState *ls, int line);    /* with语句的前向声明 */
static void classstat (LexState *ls, int line, int class_flags, int isexport, int parent_class_reg);   /* class语句的前向声明 */
static void namespacestat (LexState *ls, int line);
static void declaration_stat (LexState *ls, int line);
static void usingstat (LexState *ls);
static void interfacestat (LexState *ls, int line, int isexport); /* interface语句的前向声明 */
static void structstat (LexState *ls, int line, int isexport);  /* struct语句的前向声明 */
static void superstructstat (LexState *ls, int line);           /* superstruct语句的前向声明 */
static void enumstat (LexState *ls, int line, int isexport);    /* enum语句的前向声明 */
static void newexpr (LexState *ls, expdesc *v);   /* onew表达式的前向声明 */
static void superexpr (LexState *ls, expdesc *v); /* osuper表达式的前向声明 */
static void cond_expr (LexState *ls, expdesc *v); /* 条件表达式的前向声明（不将{作为函数调用） */
static void constexprstat (LexState *ls);         /* 预处理语句 */
static void ifexpr (LexState *ls, expdesc *v);    /* if表达式前向声明 */
static int cond (LexState *ls);                   /* cond前向声明 */
static Vardesc *getlocalvardesc (FuncState *fs, int vidx); /* getlocalvardesc前向声明 */

static l_noret error_expected (LexState *ls, int token) {
  luaX_syntaxerror(ls,
      luaO_pushfstring(ls->L, "%s expected", luaX_token2str(ls, token)));
}

static void breaklvm(LexState *ls);


static l_noret errorlimit (FuncState *fs, int limit, const char *what) {
  lua_State *L = fs->ls->L;
  const char *msg;
  int line = fs->f->linedefined;
  const char *where = (line == 0)
                      ? "main function"
                      : luaO_pushfstring(L, "function at line %d", line);
  msg = luaO_pushfstring(L, "too many %s (limit is %d) in %s",
                             what, limit, where);
  luaX_syntaxerror(fs->ls, msg);
}


static void checklimit (FuncState *fs, int v, int l, const char *what) {
  if (v > l) errorlimit(fs, l, what);
}


/*
** Test whether next token is 'c'; if so, skip it.
*/
int testnext (LexState *ls, int c) {
  if (ls->t.token == c) {
    luaX_next(ls);
    return 1;
  }
  else return 0;
}
static int testtoken (LexState *ls, int c) {
  if (ls->t.token == c) {
    return 1;
  }
  else return 0;
}


/*
** =====================================================================
** 软关键字系统 (Soft Keyword System)
** 软关键字是上下文敏感的关键字，只在特定语法位置被识别为关键字
** 在其他位置可以作为普通标识符使用
** 
** 特性：
** - 支持多上下文（使用位掩码）
** - 支持前瞻匹配（后面跟什么时识别为关键字）
** - 支持排除列表（后面跟什么时不识别为关键字）
** - 使用哈希表优化查找效率
** =====================================================================
*/

/*
** 软关键字上下文类型（位掩码，支持组合）
*/
#define SOFTKW_CTX_NONE         0x00  /* 无上下文 */
#define SOFTKW_CTX_STMT_BEGIN   0x01  /* 语句开头（如 class, interface） */
#define SOFTKW_CTX_EXPR         0x02  /* 表达式中（如 new） */
#define SOFTKW_CTX_CLASS_BODY   0x04  /* 类体内部（如 private, protected） */
#define SOFTKW_CTX_CLASS_INHERIT 0x08 /* 类继承上下文（如 extends, implements） */
#define SOFTKW_CTX_TRAIT_BODY   0x10 /* trait体内部（如 require） */
#define SOFTKW_CTX_ANY          0xFF  /* 任意上下文 */

/*
** 软关键字 ID 枚举
** 用于在解析器中快速判断软关键字类型
*/
typedef enum {
  SKW_NONE = 0,
  /* 类定义相关 */
  SKW_CLASS,
  SKW_INTERFACE,
  SKW_EXTENDS,
  SKW_IMPLEMENTS,
  /* 访问修饰符 */
  SKW_PRIVATE,
  SKW_PROTECTED,
  SKW_PUBLIC,
  SKW_STATIC,
  SKW_ABSTRACT,
  SKW_FINAL,
  SKW_SEALED,    /* 密封类修饰符 */
  SKW_SINGLETON,  /* singleton 单例修饰符 */
  /* array */
  SKW_ARRAY,     /* 数组关键字 */
  /* getter/setter */
  SKW_GET,       /* getter 属性访问器 */
  SKW_SET,       /* setter 属性访问器 */
  SKW_OVERRIDE,    /* override 修饰符 */
  SKW_AS,         /* as 安全类型转换 */
  /* 表达式相关 */
  SKW_NEW,
  SKW_SUPER,
  SKW_MATCH,
  /* trait/mixin 相关 */
  SKW_TRAIT,
  SKW_USE,
  SKW_REQUIRE,
  /* 总数 */
  SKW_COUNT
} SoftKWID;

/*
** 软关键字定义结构（增强版）
*/
typedef struct {
  const char *name;           /* 关键字名称 */
  SoftKWID id;                /* 关键字 ID */
  unsigned int contexts;      /* 允许的上下文（位掩码） */
  int lookahead_tokens[16];    /* 前瞻匹配列表（以 0 结尾，后面跟这些时识别为关键字） */
  int exclude_tokens[16];      /* 排除列表（以 0 结尾，后面跟这些时不识别为关键字） */
  unsigned int hash;          /* 名称哈希值（运行时计算） */
} SoftKWDef;

/*
** 软关键字定义表
** 按字母顺序排列以便二分查找
*/
static SoftKWDef soft_keywords[] = {
  /* abstract - 语句开头（abstract class）或类体内（abstract function）*/
  {"abstract",   SKW_ABSTRACT,   SOFTKW_CTX_STMT_BEGIN | SOFTKW_CTX_CLASS_BODY,    {TK_FUNCTION, TK_NAME, 0}, {'=', 0}, 0},
  /* as - 安全类型转换运算符 */
  {"as",         SKW_AS,         SOFTKW_CTX_EXPR,          {TK_NAME, 0}, {'=', 0}, 0},
  /* class - 语句开头，后面必须跟类名 */
  {"class",      SKW_CLASS,      SOFTKW_CTX_STMT_BEGIN | SOFTKW_CTX_CLASS_BODY,    {TK_NAME, 0}, {'=', 0}, 0},
  /* extends - 类继承上下文，后面必须跟类名 */
  {"extends",    SKW_EXTENDS,    SOFTKW_CTX_CLASS_INHERIT, {TK_NAME, 0}, {'=', 0}, 0},
  /* final - 语句开头（final class）或类体内（final function）*/
  {"final",      SKW_FINAL,      SOFTKW_CTX_STMT_BEGIN | SOFTKW_CTX_CLASS_BODY,    {TK_FUNCTION, TK_NAME, 0}, {'=', 0}, 0},
  /* get - 类体内，getter属性访问器 */
  {"get",        SKW_GET,        SOFTKW_CTX_CLASS_BODY,    {TK_NAME, 0}, {'=', 0}, 0},
  /* implements - 类继承上下文，后面必须跟接口名 */
  {"implements", SKW_IMPLEMENTS, SOFTKW_CTX_CLASS_INHERIT, {TK_NAME, 0}, {'=', 0}, 0},
  /* interface - 语句开头，后面必须跟接口名 */
  {"interface",  SKW_INTERFACE,  SOFTKW_CTX_STMT_BEGIN,    {TK_NAME, 0}, {'=', 0}, 0},
  /* new - 表达式中，后面必须跟类名 */
  {"new",        SKW_NEW,        SOFTKW_CTX_EXPR,          {TK_NAME, 0}, {'=', 0}, 0},
  /* super - 表达式中，后面必须跟.或:或( */
  {"super",      SKW_SUPER,      SOFTKW_CTX_EXPR,          {'.', ':', '(', 0}, {'=', 0}, 0},
  /* override - 类体内，后面跟function */
  {"override",   SKW_OVERRIDE,   SOFTKW_CTX_CLASS_BODY,    {TK_FUNCTION, 0}, {'=', 0}, 0},
  /* private - 类体内，后面跟function或标识符名 */
  {"private",    SKW_PRIVATE,    SOFTKW_CTX_CLASS_BODY,    {TK_FUNCTION, TK_NAME, 0}, {'=', 0}, 0},
  /* protected - 类体内，后面跟function或标识符名 */
  {"protected",  SKW_PROTECTED,  SOFTKW_CTX_CLASS_BODY,    {TK_FUNCTION, TK_NAME, 0}, {'=', 0}, 0},
  /* public - 类体内，后面跟function或标识符名 */
  {"public",     SKW_PUBLIC,     SOFTKW_CTX_CLASS_BODY,    {TK_FUNCTION, TK_NAME, 0}, {'=', 0}, 0},
  /* sealed - 语句开头（sealed class）密封类 */
  {"sealed",     SKW_SEALED,     SOFTKW_CTX_STMT_BEGIN,    {TK_NAME, 0}, {'=', 0}, 0},
  /* set - 类体内，setter属性访问器 */
  {"set",        SKW_SET,        SOFTKW_CTX_CLASS_BODY,    {TK_NAME, 0}, {'=', 0}, 0},
  /* singleton - 语句开头（singleton class）单例类 */
  {"singleton",  SKW_SINGLETON,  SOFTKW_CTX_STMT_BEGIN,    {TK_NAME, 0}, {'=', 0}, 0},
  /* static - 类体内，后面跟function或标识符名 */
  {"static",     SKW_STATIC,     SOFTKW_CTX_CLASS_BODY,    {TK_FUNCTION, TK_NAME, 0}, {'=', 0}, 0},
  /* trait - 语句开头，后面必须跟trait名 */
  {"trait",      SKW_TRAIT,      SOFTKW_CTX_STMT_BEGIN,    {TK_NAME, 0}, {'=', 0}, 0},
  /* use - 类继承上下文，后面必须跟trait名 */
  {"use",        SKW_USE,        SOFTKW_CTX_CLASS_INHERIT, {TK_NAME, 0}, {'=', 0}, 0},
  /* require - trait体内，后面跟function */
  {"require",    SKW_REQUIRE,    SOFTKW_CTX_TRAIT_BODY,    {TK_FUNCTION, 0}, {'=', 0}, 0},
  {"match",      SKW_MATCH,      SOFTKW_CTX_STMT_BEGIN | SOFTKW_CTX_EXPR,    {TK_NAME, '{', '[', TK_STRING, TK_INT, TK_FLT, TK_TRUE, TK_FALSE, TK_NIL, '(', TK_NOT, '-', '#', TK_FUNCTION, 0}, {'=', '.', ':', '(', 0}, 0},
  /* 结束标记 */
  {NULL,         SKW_NONE,       0,                         {0}, {0}, 0}
};

/* 哈希表大小（使用质数以减少冲突；必须大于软关键字总数，否则开放寻址会丢关键字） */
#define SOFTKW_HASH_SIZE 41

/* 哈希表（存储软关键字定义的指针） */
static SoftKWDef *softkw_hashtable[SOFTKW_HASH_SIZE];

/* 标记哈希表是否已初始化 */
static int softkw_initialized = 0;


/*
** 计算字符串哈希值
** 参数：
**   s - 字符串
** 返回值：
**   哈希值
*/
static unsigned int softkw_hash (const char *s) {
  unsigned int h = 0;
  while (*s) {
    h = h * 31 + (unsigned char)*s++;
  }
  return h;
}


/*
** 初始化软关键字哈希表
** 说明：
**   在第一次使用前自动调用，构建哈希表以加速查找
*/
static void softkw_init (void) {
  if (softkw_initialized) return;
  LUA_LOGD("[SOFTKW] softkw_init ENTRY");
  
  /* 清空哈希表 */
  for (int i = 0; i < SOFTKW_HASH_SIZE; i++) {
    softkw_hashtable[i] = NULL;
  }
  LUA_LOGD("[SOFTKW] softkw_init: hashtable cleared");

  /* 计算每个软关键字的哈希值并插入哈希表 */
  int i;
  for (i = 0; soft_keywords[i].name != NULL; i++) {
    LUA_LOGD("[SOFTKW] softkw_init: [%d] BEFORE hash, name='%s'", i, soft_keywords[i].name);
    soft_keywords[i].hash = softkw_hash(soft_keywords[i].name);
    LUA_LOGD("[SOFTKW] softkw_init: [%d] AFTER hash=%u, BEFORE hashtable insert", i, soft_keywords[i].hash);
    /* 使用开放寻址法处理冲突 */
    unsigned int idx = soft_keywords[i].hash % SOFTKW_HASH_SIZE;
    int probe_count = 0;
    while (softkw_hashtable[idx] != NULL) {
      idx = (idx + 1) % SOFTKW_HASH_SIZE;
      probe_count++;
      if (probe_count > SOFTKW_HASH_SIZE) {
        LUA_LOGD("[SOFTKW] softkw_init: [%d] FATAL: hashtable full, infinite probe loop!", i);
        break;
      }
    }
    LUA_LOGD("[SOFTKW] softkw_init: [%d] AFTER hashtable insert at bucket[%d] (probes=%d)", i, idx, probe_count);
    softkw_hashtable[idx] = &soft_keywords[i];
  }
  LUA_LOGD("[SOFTKW] softkw_init: all keywords added (%d entries), checking soft_keywords[%d].name...", i, i);

  softkw_initialized = 1;
  LUA_LOGD("[SOFTKW] softkw_init END");
}


/*
** 根据名称查找软关键字定义（使用哈希表）
** 参数：
**   name - 关键字名称
** 返回值：
**   软关键字定义指针，未找到返回 NULL
*/
static SoftKWDef* softkw_find (const char *name) {
  LUA_LOGD("[SOFTKW] softkw_find ENTRY name='%s' initialized=%d", name, softkw_initialized);
  if (!softkw_initialized) softkw_init();
  
  unsigned int h = softkw_hash(name);
  unsigned int idx = h % SOFTKW_HASH_SIZE;
  int count = 0;
  LUA_LOGD("[SOFTKW] softkw_find: hash=%u idx=%u", h, idx);
  
  /* 开放寻址法查找 */
  while (softkw_hashtable[idx] != NULL && count < SOFTKW_HASH_SIZE) {
    LUA_LOGD("[SOFTKW] softkw_find: bucket[%d] has '%s', comparing with '%s'", idx, softkw_hashtable[idx]->name, name);
        if (softkw_hashtable[idx]->hash == h &&
            strcmp(softkw_hashtable[idx]->name, name) == 0) {
      LUA_LOGD("[SOFTKW] softkw_find: MATCH at bucket[%d]", idx);
      return softkw_hashtable[idx];
    }
    idx = (idx + 1) % SOFTKW_HASH_SIZE;
    count++;
  }
  
  LUA_LOGD("[SOFTKW] softkw_find: NOT FOUND");
  return NULL;
}


/*
** 根据 ID 查找软关键字定义
** 参数：
**   id - 软关键字 ID
** 返回值：
**   软关键字定义指针，未找到返回 NULL
*/
static SoftKWDef* softkw_findbyid (SoftKWID id) {
  LUA_LOGD("[SOFTKW] softkw_findbyid ENTRY id=%d initialized=%d", id, softkw_initialized);
  if (!softkw_initialized) {
    LUA_LOGD("[SOFTKW] softkw_findbyid: calling softkw_init()");
    softkw_init();
    LUA_LOGD("[SOFTKW] softkw_findbyid: softkw_init returned");
  }
  
  /* ID查找使用线性搜索（通常用于验证，不频繁调用） */
  for (int i = 0; soft_keywords[i].name != NULL; i++) {
    LUA_LOGD("[SOFTKW] softkw_findbyid: checking[%d] id=%d name='%s'", i, soft_keywords[i].id, soft_keywords[i].name);
    if (soft_keywords[i].id == id) {
      LUA_LOGD("[SOFTKW] softkw_findbyid: FOUND id=%d at index %d", id, i);
      return &soft_keywords[i];
    }
  }
  LUA_LOGD("[SOFTKW] softkw_findbyid: NOT FOUND id=%d", id);
  return NULL;
}


/*
** 检查前瞻token是否在匹配列表中
** 参数：
**   lookahead - 前瞻token
**   tokens - token列表（以0结尾）
** 返回值：
**   1 - 匹配
**   0 - 不匹配
*/
static int softkw_match_lookahead (int lookahead, const int *tokens) {
  if (tokens[0] == 0) return 1;  /* 空列表表示无条件匹配 */
  for (int i = 0; tokens[i] != 0 && i < 8; i++) {
    if (lookahead == tokens[i]) {
      return 1;
    }
  }
  return 0;
}


/*
** 检查前瞻token是否在排除列表中
** 参数：
**   lookahead - 前瞻token
**   tokens - 排除token列表（以0结尾）
** 返回值：
**   1 - 在排除列表中
**   0 - 不在排除列表中
*/
static int softkw_in_exclude (int lookahead, const int *tokens) {
  for (int i = 0; tokens[i] != 0 && i < 4; i++) {
    if (lookahead == tokens[i]) {
      return 1;
    }
  }
  return 0;
}


/*
** 检查当前 token 是否是指定上下文的软关键字
** 参数：
**   ls - 词法状态
**   context - 上下文类型（位掩码）
** 返回值：
**   软关键字 ID，不匹配返回 SKW_NONE
*/
__attribute__((noinline))
static SoftKWID softkw_check (LexState *ls, unsigned int context) {
  if (ls->t.token != TK_NAME) {
    return SKW_NONE;
  }
  
  const char *name = getstr(ls->t.seminfo.ts);
  SoftKWDef *def = softkw_find(name);
  
  if (def == NULL) {
    return SKW_NONE;
  }
  
  /* 检查上下文是否匹配（使用位掩码） */
  if ((def->contexts & context) == 0) {
    return SKW_NONE;
  }
  
  /* 获取前瞻token（优先使用已缓存的lookahead，避免重复调用luaX_lookahead） */
  int lookahead;
  if (ls->lookahead.token != TK_EOS) {
    lookahead = ls->lookahead.token;
  } else {
    lookahead = luaX_lookahead(ls);
  }
  
  /* 检查排除列表 */
  if (softkw_in_exclude(lookahead, def->exclude_tokens)) {
    return SKW_NONE;  /* 后面跟的是排除的token，当作普通标识符 */
  }
  
  /* 检查前瞻匹配列表 */
  if (!softkw_match_lookahead(lookahead, def->lookahead_tokens)) {
    return SKW_NONE;  /* 前瞻不匹配，当作普通标识符 */
  }
  
  return def->id;
}


/*
** 检查当前 token 是否是指定上下文的软关键字，如果是则消耗它
** 参数：
**   ls - 词法状态
**   context - 上下文类型（位掩码）
** 返回值：
**   软关键字 ID，不匹配返回 SKW_NONE（不消耗 token）
*/
static SoftKWID softkw_checknext (LexState *ls, unsigned int context) {
  SoftKWID id = softkw_check(ls, context);
  if (id != SKW_NONE) {
    luaX_next(ls);
  }
  return id;
}


/*
** 检查当前 token 是否是指定 ID 的软关键字
** 参数：
**   ls - 词法状态
**   id - 软关键字 ID
**   context - 上下文类型（位掩码），传入 0 表示不检查上下文
** 返回值：
**   1 - 是指定的软关键字
**   0 - 不是
*/
static int softkw_test (LexState *ls, SoftKWID id, unsigned int context) {
  LUA_LOGD("[PARSER] softkw_test ENTRY id=%d context=%d token=%d ts=%p", id, context, ls->t.token, (void*)ls->t.seminfo.ts);
  if (ls->t.token != TK_NAME) {
    return 0;
  }
  
  SoftKWDef *def = softkw_findbyid(id);
  LUA_LOGD("[PARSER] softkw_test: id=%d def=%p name='%s'", id, (void*)def, def ? def->name : "(null)");
  if (!def || !def->name) {
    return 0;
  }
  
  /* 检查 ls->t.seminfo.ts 是否有效 - 避免 ARM64 strcmp 因为未对齐访问而崩溃 */
  if (!ls->t.seminfo.ts) {
    LUA_LOGD("[PARSER] softkw_test: ts is NULL, skip");
    return 0;
  }
  
  const char *name = getstr(ls->t.seminfo.ts);
  if (!name) {
    LUA_LOGD("[PARSER] softkw_test: name is NULL, skip");
    return 0;
  }
  
  LUA_LOGD("[PARSER] softkw_test: comparing name='%s' vs def='%s'", name, def->name);

  /* 使用 ARM64-safe 逐字节 strcmp 替代 strcmp (通过 lprefix.h 宏劫持) */
  if (strcmp(name, def->name) != 0) {
    return 0;
  }
  
  /* 检查上下文是否匹配（如果指定了上下文） */
  if (context != 0 && (def->contexts & context) == 0) {
    return 0;
  }
  
  /* 获取前瞻token */
  int lookahead;
  if (ls->lookahead.token != TK_EOS) {
    lookahead = ls->lookahead.token;
    LUA_LOGD("[PARSER] softkw_test: cached lookahead=%d", lookahead);
  } else {
    LUA_LOGD("[PARSER] softkw_test: calling luaX_lookahead");
    lookahead = luaX_lookahead(ls);
    LUA_LOGD("[PARSER] softkw_test: luaX_lookahead returned %d", lookahead);
  }
  
  /* 检查排除列表 */
  if (softkw_in_exclude(lookahead, def->exclude_tokens)) {
    return 0;
  }
  
  /* 检查前瞻匹配列表 */
  if (!softkw_match_lookahead(lookahead, def->lookahead_tokens)) {
    return 0;
  }
  
  return 1;
}


/*
** 检查当前 token 是否是指定 ID 的软关键字，如果是则消耗它
** 参数：
**   ls - 词法状态
**   id - 软关键字 ID
**   context - 上下文类型（位掩码），传入 0 表示不检查上下文
** 返回值：
**   1 - 是指定的软关键字（已消耗）
**   0 - 不是（未消耗）
*/
static int softkw_testnext (LexState *ls, SoftKWID id, unsigned int context) {
  if (softkw_test(ls, id, context)) {
    luaX_next(ls);
    return 1;
  }
  return 0;
}


/*
** Check that next token is 'c'.
*/
__attribute__((noinline))
static void check (LexState *ls, int c) {
  if (ls->t.token != c) {
    error_expected(ls, c);
  }
}


/*
** Check that next token is 'c' and skip it.
*/
void checknext (LexState *ls, int c) {
  check(ls, c);
  luaX_next(ls);
}


#define check_condition(ls,c,msg)	{ if (!(c)) luaX_syntaxerror(ls, msg); }


/*
** Check that next token is 'what' and skip it. In case of error,
** raise an error that the expected 'what' should match a 'who'
** in line 'where' (if that is not the current line).
*/
void check_match (LexState *ls, int what, int who, int where) {
  if (l_unlikely(!testnext(ls, what))) {
    if (where == ls->linenumber)  /* all in the same line? */
      error_expected(ls, what);  /* do not need a complex message */
    else {
      luaX_syntaxerror(ls, luaO_pushfstring(ls->L,
             "%s expected (to close %s at line %d)",
              luaX_token2str(ls, what), luaX_token2str(ls, who), where));
    }
  }
}


static int is_type_token(int token);
static int is_nameable_keyword(int token);
static int is_nametoken(int token);

static TString *str_checkname (LexState *ls) {
  TString *ts;
  LUA_LOGD("[PARSER] str_checkname: token=%d", ls->t.token);
  if (is_nametoken(ls->t.token)) {
     ts = ls->t.seminfo.ts;
     luaX_next(ls);
     return ts;
  }
  check(ls, TK_NAME);
  return NULL;  /* unreachable */
}


static void init_exp (expdesc *e, expkind k, int i) {
  e->f = e->t = NO_JUMP;
  e->k = k;
  e->u.info = i;
  e->nodiscard = 0;
}


typedef struct DecoratorState {
    int num_decorators;
    int dec_regs;
    struct DecoratorState *prev;
} DecoratorState;

#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L
#define TLS_MODIFIER _Thread_local
#elif defined(__GNUC__)
#define TLS_MODIFIER __thread
#else
#define TLS_MODIFIER
#endif

static TLS_MODIFIER DecoratorState *current_decorator_state = NULL;

static void push_decorators(LexState *ls, int num, int regs) {
    DecoratorState *state = (DecoratorState *)luaM_malloc_(ls->L, sizeof(DecoratorState), 0);
    state->num_decorators = num;
    state->dec_regs = regs;
    state->prev = current_decorator_state;
    current_decorator_state = state;
}

static void pop_decorators(LexState *ls, int *num, int *regs) {
    if (current_decorator_state) {
        *num = current_decorator_state->num_decorators;
        *regs = current_decorator_state->dec_regs;
        DecoratorState *prev = current_decorator_state->prev;
        luaM_free_(ls->L, current_decorator_state, sizeof(DecoratorState));
        current_decorator_state = prev;
    } else {
        *num = 0;
        *regs = 0;
    }
}

static void apply_decorators_inline(LexState *ls, expdesc *v, expdesc *e) {
    int num_decs = 0, dec_regs = 0;
    pop_decorators(ls, &num_decs, &dec_regs);
    // printf("Applying %d decorators at line %d\n", num_decs, ls->linenumber);
    if (num_decs == 0) return;

    FuncState *fs = ls->fs;
    luaK_exp2nextreg(fs, e); /* put the function/class into a register */
    int target_reg = e->u.info;

    for (int i = num_decs - 1; i >= 0; i--) {
        int d_reg = dec_regs + i;
        int call_base = fs->freereg;
        luaK_reserveregs(fs, 2);

        luaK_codeABC(fs, OP_MOVE, call_base, d_reg, 0);
        luaK_codeABC(fs, OP_MOVE, call_base + 1, target_reg, 0);
        luaK_codeABC(fs, OP_CALL, call_base, 2, 2);
        luaK_codeABC(fs, OP_MOVE, target_reg, call_base, 0);

        fs->freereg -= 2;
    }

    init_exp(e, VNONRELOC, target_reg);
}


static int parse_decorators(LexState *ls);



static void codestring (expdesc *e, TString *s) {
  e->f = e->t = NO_JUMP;
  e->k = VKSTR;
  e->u.strval = s;
}


static void codename (LexState *ls, expdesc *e) {
  codestring(e, str_checkname(ls));
}


static void checkforshadowing (LexState *ls, FuncState *fs, TString *name) {
  /*
  FuncState *f = fs;
  while (f) {
    int i;
    for (i = cast_int(f->nactvar) - 1; i >= 0; i--) {
      Vardesc *vd = getlocalvardesc(f, i);
      if (eqstr(name, vd->vd.name)) {
        const char *msg = luaO_pushfstring(ls->L, "local '%s' shadows previous declaration", getstr(name));
        luaX_warning(ls, msg, WT_VAR_SHADOW);
        goto check_global;
      }
    }
    f = f->prev;
  }

check_global:
  {
    const char *s = getstr(name);
    if (strcmp(s, "table") == 0 || strcmp(s, "string") == 0 || strcmp(s, "arg") == 0 ||
        strcmp(s, "io") == 0 || strcmp(s, "os") == 0 || strcmp(s, "math") == 0) {
       const char *msg = luaO_pushfstring(ls->L, "local '%s' shadows global", s);
       luaX_warning(ls, msg, WT_GLOBAL_SHADOW);
    }
  }
  */
}

/*
** Register a new local variable in the active 'Proto' (for debug
** information).
*/
static int registerlocalvar (LexState *ls, FuncState *fs, TString *varname) {
  Proto *f = fs->f;
  int oldsize = f->sizelocvars;
  luaM_growvector(ls->L, f->locvars, fs->ndebugvars, f->sizelocvars,
                  LocVar, SHRT_MAX, "local variables");
  while (oldsize < f->sizelocvars)
    f->locvars[oldsize++].varname = NULL;
  f->locvars[fs->ndebugvars].varname = varname;
  f->locvars[fs->ndebugvars].startpc = fs->pc;
  luaC_objbarrier(ls->L, f, varname);
  return fs->ndebugvars++;
}


/*
** Create a new local variable with the given 'name'. Return its index
** in the function.
*/
int new_localvar (LexState *ls, TString *name) {
  lua_State *L = ls->L;
  FuncState *fs = ls->fs;
  Dyndata *dyd = ls->dyd;
  Vardesc *var;
  checkforshadowing(ls, fs, name);
  checklimit(fs, dyd->actvar.n + 1 - fs->firstlocal,
                 MAXVARS, "local variables");
  luaM_growvector(L, dyd->actvar.arr, dyd->actvar.n + 1,
                  dyd->actvar.size, Vardesc, USHRT_MAX, "local variables");
  var = &dyd->actvar.arr[dyd->actvar.n++];
  var->vd.kind = VDKREG;  /* default */
  var->vd.name = name;
  var->vd.used = 0;
  var->vd.hint = NULL;  /* 初始化类型提示为NULL，防止未初始化内存导致野指针 */
  var->vd.nodiscard = 0;  /* 初始化nodiscard标志 */
  return dyd->actvar.n - 1 - fs->firstlocal;
}

/*
** 在 nactvar 指示的位置插入一个新的局部变量描述符（Vardesc），
** 确保 actvar 数组和 nactvar 激活顺序一致。
** 与 new_localvar 不同，此函数将变量插入到当前 nactvar 位置，
** 而不是追加到 actvar 末尾。这解决了延迟 activate（如 localstat）
** 导致的 actvar/nactvar 不对齐问题。
** 参数：
**   ls - 词法分析状态
**   name - 变量名
**   reg - 变量所在的寄存器编号
** 返回值：变量的 vidx（相对于 firstlocal 的索引）
*/
static int insert_localvar (LexState *ls, TString *name, int reg) {
  FuncState *fs = ls->fs;
  Dyndata *dyd = ls->dyd;
  int insert_pos = fs->firstlocal + fs->nactvar;  /* 插入位置 = 当前激活位置 */

  luaM_growvector(ls->L, dyd->actvar.arr, dyd->actvar.n + 1,
                  dyd->actvar.size, Vardesc, USHRT_MAX, "local variables");
  /* 将插入位置之后的元素后移一位，为新变量腾出空间 */
  if (insert_pos < dyd->actvar.n) {
    memmove(&dyd->actvar.arr[insert_pos + 1], &dyd->actvar.arr[insert_pos],
            (dyd->actvar.n - insert_pos) * sizeof(Vardesc));
  }
  dyd->actvar.n++;

  Vardesc *var = &dyd->actvar.arr[insert_pos];
  memset(var, 0, sizeof(Vardesc));
  var->vd.kind = VDKREG;
  var->vd.name = name;
  var->vd.ridx = reg;
  var->vd.pidx = registerlocalvar(ls, fs, name);
  fs->nactvar++;  /* 激活该变量 */
  return fs->nactvar - 1 - fs->firstlocal;  /* 返回 vidx */
}


/*
** Return the "variable description" (Vardesc) of a given variable.
** (Unless noted otherwise, all variables are referred to by their
** compiler indices.)
*/
static Vardesc *getlocalvardesc (FuncState *fs, int vidx) {
  return &fs->ls->dyd->actvar.arr[fs->firstlocal + vidx];
}


/*
** Convert 'nvar', a compiler index level, to its corresponding
** register. For that, search for the highest variable below that level
** that is in a register and uses its register index ('ridx') plus one.
*/
static int reglevel (FuncState *fs, int nvar) {
  while (nvar-- > 0) {
    Vardesc *vd = getlocalvardesc(fs, nvar);  /* get previous variable */
    if (vd->vd.kind != RDKCTC)  /* is in a register? */
      return vd->vd.ridx + 1;
  }
  return 0;  /* no variables in registers */
}


/*
** Return the number of variables in the register stack for the given
** function.
*/
int luaY_nvarstack (FuncState *fs) {
  return reglevel(fs, fs->nactvar);
}


/*
** Get the debug-information entry for current variable 'vidx'.
*/
static LocVar *localdebuginfo (FuncState *fs, int vidx) {
  Vardesc *vd = getlocalvardesc(fs,  vidx);
  if (vd->vd.kind == RDKCTC || vd->vd.kind == GDKREG || vd->vd.kind == GDKCONST)
    return NULL;  /* no debug info. for constants or globals */
  else {
    int idx = vd->vd.pidx;
    lua_assert(idx < fs->ndebugvars);
    return &fs->f->locvars[idx];
  }
}


/*
** Create an expression representing variable 'vidx'
*/
static void init_var (FuncState *fs, expdesc *e, int vidx) {
  e->f = e->t = NO_JUMP;
  e->k = VLOCAL;
  e->u.var.vidx = vidx;
  e->u.var.ridx = getlocalvardesc(fs, vidx)->vd.ridx;
  e->nodiscard = getlocalvardesc(fs, vidx)->vd.nodiscard;
}


/*
** Raises an error if variable described by 'e' is read only
*/
static void check_readonly (LexState *ls, expdesc *e) {
  FuncState *fs = ls->fs;
  TString *varname = NULL;  /* to be set if variable is const */
  switch (e->k) {
    case VCONST: {
      varname = ls->dyd->actvar.arr[e->u.info].vd.name;
      break;
    }
    case VLOCAL: {
      Vardesc *vardesc = getlocalvardesc(fs, e->u.var.vidx);
      if (vardesc->vd.kind != VDKREG)  /* not a regular variable? */
        varname = vardesc->vd.name;
      break;
    }
    case VUPVAL: {
      Upvaldesc *up = &fs->f->upvalues[e->u.info];
      if (up->kind != VDKREG)
        varname = up->name;
      break;
    }
    default:
      return;  /* other cases cannot be read-only */
  }
  if (varname) {
    const char *msg = luaO_pushfstring(ls->L,
       "attempt to assign to const variable '%s'", getstr(varname));
    luaK_semerror(ls, msg);  /* error */
  }
}


/*
** Start the scope for the last 'nvars' created variables.
*/
void adjustlocalvars (LexState *ls, int nvars) {
  FuncState *fs = ls->fs;
  int reglevel = luaY_nvarstack(fs);
  int i;
  for (i = 0; i < nvars; i++) {
    int vidx = fs->nactvar++;
    Vardesc *var = getlocalvardesc(fs, vidx);
    var->vd.ridx = reglevel++;
    var->vd.pidx = registerlocalvar(ls, fs, var->vd.name);
  }
}


/*
** Close the scope for all variables up to level 'tolevel'.
** (debug info.)
*/
static void removevars (FuncState *fs, int tolevel) {
  int nremove = fs->nactvar - tolevel;
  if (nremove <= 0) return;
  while (fs->nactvar > tolevel) {
    LocVar *var = localdebuginfo(fs, --fs->nactvar);
    if (var)  /* does it have debug information? */
      var->endpc = fs->pc;

    Vardesc *vd = getlocalvardesc(fs, fs->nactvar);
    if (!vd->vd.used && vd->vd.kind == VDKREG && getstr(vd->vd.name)[0] != '_' && getstr(vd->vd.name)[0] != '(') {
       const char *msg = luaO_pushfstring(fs->ls->L, "unused local variable '%s'", getstr(vd->vd.name));
       luaX_warning(fs->ls, msg, WT_UNUSED_VAR);
       lua_pop(fs->ls->L, 1);
    }
  }
  /* 将剩余未激活的变量 shift 到前面填补空隙（而不是直接 truncate 丢弃） */
  /* 注意：actvar.arr 是全局数组，需要用 fs->firstlocal 转换为全局索引 */
  {
    int remove_start = fs->firstlocal + tolevel;
    int remaining = (int)(fs->ls->dyd->actvar.n) - (remove_start + nremove);
    if (remaining > 0) {
      memmove(&fs->ls->dyd->actvar.arr[remove_start],
              &fs->ls->dyd->actvar.arr[remove_start + nremove],
              remaining * sizeof(Vardesc));
    }
    fs->ls->dyd->actvar.n -= nremove;
  }
  fs->nactvar = tolevel;
}


/*
** Search the upvalues of the function 'fs' for one
** with the given 'name'.
*/
static int searchupvalue (FuncState *fs, TString *name) {
  int i;
  Upvaldesc *up = fs->f->upvalues;
  for (i = 0; i < fs->nups; i++) {
    if (eqstr(up[i].name, name)) return i;
  }
  return -1;  /* not found */
}


static Upvaldesc *allocupvalue (FuncState *fs) {
  Proto *f = fs->f;
  int oldsize = f->sizeupvalues;
  checklimit(fs, fs->nups + 1, MAXUPVAL, "upvalues");
  luaM_growvector(fs->ls->L, f->upvalues, fs->nups, f->sizeupvalues,
                  Upvaldesc, MAXUPVAL, "upvalues");
  while (oldsize < f->sizeupvalues)
    f->upvalues[oldsize++].name = NULL;
  return &f->upvalues[fs->nups++];
}


static int newupvalue (FuncState *fs, TString *name, expdesc *v) {
  Upvaldesc *up = allocupvalue(fs);
  FuncState *prev = fs->prev;
  if (v->k == VLOCAL) {
    up->instack = 1;
    up->idx = v->u.var.ridx;
    up->kind = getlocalvardesc(prev, v->u.var.vidx)->vd.kind;
    lua_assert(eqstr(name, getlocalvardesc(prev, v->u.var.vidx)->vd.name));
  }
  else {
    up->instack = 0;
    up->idx = (unsigned short)v->u.info;
    up->kind = prev->f->upvalues[v->u.info].kind;
    lua_assert(eqstr(name, prev->f->upvalues[v->u.info].name));
  }
  up->name = name;
  luaC_objbarrier(fs->ls->L, fs->f, name);
  return fs->nups - 1;
}


/*
** Look for an active local variable with the name 'n' in the
** function 'fs'. If found, initialize 'var' with it and return
** its expression kind; otherwise return -1.
*/
static int searchvar (FuncState *fs, TString *n, expdesc *var) {
  int i;
  for (i = cast_int(fs->nactvar) - 1; i >= 0; i--) {
    Vardesc *vd = getlocalvardesc(fs, i);
    if (eqstr(n, vd->vd.name)) {  /* found? */
      if (vd->vd.kind == RDKCTC)  /* compile-time constant? */
        init_exp(var, VCONST, fs->firstlocal + i);
      else  /* real variable */
        init_var(fs, var, i);
      vd->vd.used = 1;
      return var->k;
    }
  }
  return -1;  /* not found */
}


/*
** Mark block where variable at given level was defined
** (to emit close instructions later).
*/
static void markupval (FuncState *fs, int level) {
  BlockCnt *bl = fs->bl;
  while (bl->nactvar > level)
    bl = bl->previous;
  bl->upval = 1;
  fs->needclose = 1;
}


/*
** Mark that current block has a to-be-closed variable.
*/
static void marktobeclosed (FuncState *fs) {
  BlockCnt *bl = fs->bl;
  bl->upval = 1;
  bl->insidetbc = 1;
  fs->needclose = 1;
}


/*
** Find a variable with the given name 'n'. If it is an upvalue, add
** this upvalue into all intermediate functions. If it is a global, set
** 'var' as 'void' as a flag.
*/
static void singlevaraux (FuncState *fs, TString *n, expdesc *var, int base) {
  if (fs == NULL)  /* no more levels? */
    init_exp(var, VVOID, 0);  /* default is global */
  else {
    int v = searchvar(fs, n, var);  /* look up locals at current level */
    if (v >= 0) {  /* found? */
      Vardesc *vd = getlocalvardesc(fs, var->u.var.vidx);
      if (vd->vd.kind == GDKREG || vd->vd.kind == GDKCONST) {
        expdesc key;
        singlevaraux(fs, fs->ls->envn, var, 1);  /* get environment variable */
        lua_assert(var->k != VVOID);  /* this one must exist */
        codestring(&key, n);  /* key is variable name */
        luaK_indexed(fs, var, &key);  /* env[varname] */
        if (vd->vd.kind == GDKCONST) {
           var->u.ind.ro = 1;
        }
        return;
      }
      if (v == VLOCAL && !base)
        markupval(fs, var->u.var.vidx);  /* local will be used as an upval */
    }
    else {  /* not found as local at current level; try upvalues */
      int idx = searchupvalue(fs, n);  /* try existing upvalues */
      if (idx < 0) {  /* not found? */
        singlevaraux(fs->prev, n, var, 0);  /* try upper levels */
        if (var->k == VLOCAL || var->k == VUPVAL)  /* local or upvalue? */
          idx  = newupvalue(fs, n, var);  /* will be a new upvalue */
        else if (var->k == VINDEXED || var->k == VINDEXUP) {
          /* global variable found in outer scope (resolved to _ENV.x) */
          /* capture _ENV from outer scope */
          idx = searchupvalue(fs, fs->ls->envn);
          if (idx < 0) {
             expdesc env;
             singlevaraux(fs->prev, fs->ls->envn, &env, 1);
             idx = newupvalue(fs, fs->ls->envn, &env);
          }
          var->k = VINDEXUP;  /* now indexed via upvalue */
          var->u.ind.t = idx;
          /* var->u.ind.idx and keystr are preserved */
          /* We must re-internalize the key string in the current function's constants */
          int k = luaK_stringK(fs, n);
          var->u.ind.idx = k;
          var->u.ind.keystr = k;
        }
        else  /* it is a global or a constant */
          return;  /* don't need to do anything at this level */
      }
      init_exp(var, VUPVAL, idx);  /* new or old upvalue */
    }
  }
}


/*
** Find a variable with the given name 'n', handling global variables
** too.
*/
static void singlevar (LexState *ls, expdesc *var) {
  LUA_LOGD("[PARSER] singlevar ENTRY");
  TString *varname = str_checkname(ls);
  LUA_LOGD("[PARSER] singlevar name='%s'", getstr(varname));
  FuncState *fs = ls->fs;
  singlevaraux(fs, varname, var, 1);
  LUA_LOGD("[PARSER] singlevar aux DONE, v->k=%d", var->k);
  if (var->k == VVOID) {  /* global name? */
    expdesc key;
    singlevaraux(fs, ls->envn, var, 1);  /* get environment variable */
    lua_assert(var->k != VVOID);  /* this one must exist */
    codestring(&key, varname);  /* key is variable name */
    luaK_indexed(fs, var, &key);  /* env[varname] */
    if (ls->declared_globals) {
       TValue k;
       setsvalue(ls->L, &k, varname);
       const TValue *decl_v = luaH_get(ls->declared_globals, &k);
       if (!ttisnil(decl_v) && ttistable(decl_v)) {
          Table *decl = hvalue(decl_v);
          TValue nd_k;
          setsvalue(ls->L, &nd_k, luaS_newliteral(ls->L, "nodiscard"));
          if (!ttisnil(luaH_get(decl, &nd_k))) {
             var->nodiscard = 1;
          }
       }
    }
  }
}


/*
** Adjust the number of results from an expression list 'e' with 'nexps'
** expressions to 'nvars' values.
*/
static void adjust_assign (LexState *ls, int nvars, int nexps, expdesc *e) {
  FuncState *fs = ls->fs;
  int needed = nvars - nexps;  /* extra values needed */
  luaK_checkstack(fs, needed);
  if (hasmultret(e->k)) {  /* last expression has multiple returns? */
    int extra = needed + 1;  /* discount last expression itself */
    if (extra < 0)
      extra = 0;
    luaK_setreturns(fs, e, extra);  /* last exp. provides the difference */
  }
  else {
    if (e->k != VVOID)  /* at least one expression? */
      luaK_exp2nextreg(fs, e);  /* close last expression */
    if (needed > 0)  /* missing values? */
      luaK_nil(fs, fs->freereg, needed);  /* complete with nils */
  }
  if (needed > 0)
    luaK_reserveregs(fs, needed);  /* registers for extra values */
  else  /* adding 'needed' is actually a subtraction */
    fs->freereg = (fs->freereg + needed);  /* remove extra values */
}


/*
** Generates an error that a goto jumps into the scope of some
** local variable.
*/
static l_noret jumpscopeerror (LexState *ls, Labeldesc *gt) {
  const char *varname = getstr(getlocalvardesc(ls->fs, gt->nactvar)->vd.name);
  const char *msg = "<goto %s> at line %d jumps into the scope of local '%s'";
  msg = luaO_pushfstring(ls->L, msg, getstr(gt->name), gt->line, varname);
  luaK_semerror(ls, msg);  /* raise the error */
}


/*
** Solves the goto at index 'g' to given 'label' and removes it
** from the list of pending gotos.
** If it jumps into the scope of some variable, raises an error.
*/
static void solvegoto (LexState *ls, int g, Labeldesc *label) {
  int i;
  Labellist *gl = &ls->dyd->gt;  /* list of gotos */
  Labeldesc *gt = &gl->arr[g];  /* goto to be resolved */
  lua_assert(eqstr(gt->name, label->name));
  if (l_unlikely(gt->nactvar < label->nactvar))  /* enter some scope? */
    jumpscopeerror(ls, gt);
  luaK_patchlist(ls->fs, gt->pc, label->pc);
  for (i = g; i < gl->n - 1; i++)  /* remove goto from pending list */
    gl->arr[i] = gl->arr[i + 1];
  gl->n--;
}


/*
** Search for an active label with the given name.
*/
static Labeldesc *findlabel (LexState *ls, TString *name) {
  int i;
  Dyndata *dyd = ls->dyd;
  /* check labels in current function for a match */
  for (i = ls->fs->firstlabel; i < dyd->label.n; i++) {
    Labeldesc *lb = &dyd->label.arr[i];
    if (eqstr(lb->name, name))  /* correct label? */
      return lb;
  }
  return NULL;  /* label not found */
}


/*
** Adds a new label/goto in the corresponding list.
*/
static int newlabelentry (LexState *ls, Labellist *l, TString *name,
                          int line, int pc) {
  int n = l->n;
  luaM_growvector(ls->L, l->arr, n, l->size,
                  Labeldesc, SHRT_MAX, "labels/gotos");
  l->arr[n].name = name;
  l->arr[n].line = line;
  l->arr[n].nactvar = ls->fs->nactvar;
  l->arr[n].close = 0;
  l->arr[n].pc = pc;
  l->n = n + 1;
  return n;
}


static int newgotoentry (LexState *ls, TString *name, int line, int pc) {
  return newlabelentry(ls, &ls->dyd->gt, name, line, pc);
}


/*
** Solves forward jumps. Check whether new label 'lb' matches any
** pending gotos in current block and solves them. Return true
** if any of the gotos need to close upvalues.
*/
static int solvegotos (LexState *ls, Labeldesc *lb) {
  Labellist *gl = &ls->dyd->gt;
  int i = ls->fs->bl->firstgoto;
  int needsclose = 0;
  while (i < gl->n) {
    if (eqstr(gl->arr[i].name, lb->name)) {
      needsclose |= gl->arr[i].close;
      solvegoto(ls, i, lb);  /* will remove 'i' from the list */
    }
    else
      i++;
  }
  return needsclose;
}


/*
** Create a new label with the given 'name' at the given 'line'.
** 'last' tells whether label is the last non-op statement in its
** block. Solves all pending gotos to this new label and adds
** a close instruction if necessary.
** Returns true iff it added a close instruction.
*/
static int createlabel (LexState *ls, TString *name, int line,
                        int last) {
  FuncState *fs = ls->fs;
  Labellist *ll = &ls->dyd->label;
  int l = newlabelentry(ls, ll, name, line, luaK_getlabel(fs));
  if (last) {  /* label is last no-op statement in the block? */
    /* assume that locals are already out of scope */
    ll->arr[l].nactvar = fs->bl->nactvar;
  }
  if (solvegotos(ls, &ll->arr[l])) {  /* need close? */
    luaK_codeABC(fs, OP_CLOSE, luaY_nvarstack(fs), 0, 0);
    return 1;
  }
  return 0;
}


/*
** Adjust pending gotos to outer level of a block.
*/
static void movegotosout (FuncState *fs, BlockCnt *bl) {
  int i;
  Labellist *gl = &fs->ls->dyd->gt;
  /* correct pending gotos to current block */
  for (i = bl->firstgoto; i < gl->n; i++) {  /* for each pending goto */
    Labeldesc *gt = &gl->arr[i];
    /* leaving a variable scope? */
    if (reglevel(fs, gt->nactvar) > reglevel(fs, bl->nactvar))
      gt->close |= bl->upval;  /* jump may need a close */
    gt->nactvar = bl->nactvar;  /* update goto level */
  }
}


void enterblock (FuncState *fs, BlockCnt *bl, lu_byte isloop) {
  bl->isloop = isloop;
  bl->nactvar = fs->nactvar;
  bl->firstlabel = fs->ls->dyd->label.n;
  bl->firstgoto = fs->ls->dyd->gt.n;
  bl->upval = 0;
  bl->insidetbc = (fs->bl != NULL && fs->bl->insidetbc);
  bl->previous = fs->bl;
  fs->bl = bl;
  bl->exports.arr = NULL;
  bl->exports.n = 0;
  bl->exports.size = 0;
  lua_assert(fs->freereg == luaY_nvarstack(fs));
}


/*
** generates an error for an undefined 'goto'.
*/
static l_noret undefgoto (LexState *ls, Labeldesc *gt) {
  const char *msg;
  if (eqstr(gt->name, luaS_newliteral(ls->L, "break"))) {
    msg = "break statement at line %d is outside a loop";
    msg = luaO_pushfstring(ls->L, msg, gt->line);
  }
  else {
    msg = "no visible label '%s' for <goto> at line %d";
    msg = luaO_pushfstring(ls->L, msg, getstr(gt->name), gt->line);
  }
  luaK_semerror(ls, msg);
}


void add_export (LexState *ls, TString *name) {
  BlockCnt *bl = ls->fs->bl;
  if (bl->exports.n >= bl->exports.size) {
    bl->exports.size = (bl->exports.size == 0) ? 4 : bl->exports.size * 2;
    bl->exports.arr = luaM_reallocvector(ls->L, bl->exports.arr,
                                         bl->exports.n, bl->exports.size, TString*);
  }
  bl->exports.arr[bl->exports.n++] = name;
}

void leaveblock (FuncState *fs) {
  BlockCnt *bl = fs->bl;
  LexState *ls = fs->ls;
  if (bl->exports.n > 0) {
    int reg = fs->freereg;
    int pc = luaK_codeABC(fs, OP_NEWTABLE, reg, 0, 0);
    expdesc t;
    int i;
    luaK_code(fs, 0); /* Extra arg for NEWTABLE */
    init_exp(&t, VNONRELOC, reg);
    luaK_reserveregs(fs, 1);

    for (i = 0; i < bl->exports.n; i++) {
       expdesc k, v;
       TString *name = bl->exports.arr[i];
       expdesc t_copy = t;
       codestring(&k, name);
       singlevaraux(fs, name, &v, 1);
       luaK_exp2anyreg(fs, &v);
       luaK_indexed(fs, &t_copy, &k);
       luaK_storevar(fs, &t_copy, &v);
    }
    luaK_settablesize(fs, pc, reg, 0, bl->exports.n);
    luaK_ret(fs, reg, 1);

    luaM_freearray(ls->L, bl->exports.arr, bl->exports.size);
    bl->exports.n = 0;
    bl->exports.size = 0;
    bl->exports.arr = NULL;
  }
  int hasclose = 0;
  int stklevel = reglevel(fs, bl->nactvar);  /* level outside the block */
  if (bl->isloop)  /* fix pending breaks? */
    hasclose = createlabel(ls, luaS_newliteral(ls->L, "break"), 0, 0);
  if (!hasclose && bl->previous && bl->upval)
    luaK_codeABC(fs, OP_CLOSE, stklevel, 0, 0);
  fs->bl = bl->previous;
  removevars(fs, bl->nactvar);
  lua_assert(bl->nactvar == fs->nactvar);
  fs->freereg = stklevel;  /* free registers */
  ls->dyd->label.n = bl->firstlabel;  /* remove local labels */
  if (bl->previous)  /* inner block? */
    movegotosout(fs, bl);  /* update pending gotos to outer block */
  else {
    if (bl->firstgoto < ls->dyd->gt.n)  /* pending gotos in outer block? */
      undefgoto(ls, &ls->dyd->gt.arr[bl->firstgoto]);  /* error */
  }
}


/*
** adds a new prototype into list of prototypes
*/
Proto *addprototype (LexState *ls) {
  Proto *clp;
  lua_State *L = ls->L;
  FuncState *fs = ls->fs;
  Proto *f = fs->f;  /* prototype of current function */
  if (fs->np >= f->sizep) {
    int oldsize = f->sizep;
    luaM_growvector(L, f->p, fs->np, f->sizep, Proto *, MAXARG_Bx, "functions");
    while (oldsize < f->sizep)
      f->p[oldsize++] = NULL;
  }
  f->p[fs->np++] = clp = luaF_newproto(L);
  luaC_objbarrier(L, f, clp);
  return clp;
}


/*
** codes instruction to create new closure in parent function.
** The OP_CLOSURE instruction uses the last available register,
** so that, if it invokes the GC, the GC knows which registers
** are in use at that time.

*/
void codeclosure (LexState *ls, expdesc *v) {
  FuncState *fs = ls->fs->prev;
  init_exp(v, VRELOC, luaK_codeABx(fs, OP_CLOSURE, 0, fs->np - 1));
  luaK_exp2nextreg(fs, v);  /* fix it at the last register */
}


/*
** keyword 编译时注册表操作
** 将 keyword 名映射到编译后的 Proto，用于 $name 语法直接引用
*/
static Proto* keyword_lookup (LexState *ls, TString *name) {
  global_State *g = G(ls->L);
  int i;
  for (i = 0; i < g->kwreg_count; i++) {
    if (g->keyword_registry[i].name == name)
      return g->keyword_registry[i].p;
  }
  return NULL;  /* 未找到 */
}

static void keyword_register (LexState *ls, TString *name, Proto *p) {
  global_State *g = G(ls->L);
  int i;
  /* 检查是否已存在同名 keyword，覆盖 */
  for (i = 0; i < g->kwreg_count; i++) {
    if (g->keyword_registry[i].name == name) {
      g->keyword_registry[i].p = p;
      return;
    }
  }
  /* 动态扩容 */
  if (g->kwreg_count >= g->kwreg_size) {
    int newsize = (g->kwreg_size == 0) ? 8 : g->kwreg_size * 2;
    g->keyword_registry = luaM_reallocvector(
        ls->L, g->keyword_registry, g->kwreg_size, newsize, KeywordRegEntry);
    g->kwreg_size = newsize;
  }
  g->keyword_registry[g->kwreg_count].name = name;
  g->keyword_registry[g->kwreg_count].p = p;
  g->kwreg_count++;
}

/*
** codes instruction to create new concept in parent function.
*/
static void codeconcept (LexState *ls, expdesc *v) {
  FuncState *fs = ls->fs->prev;
  init_exp(v, VRELOC, luaK_codeABx(fs, OP_NEWCONCEPT, 0, fs->np - 1));
  luaK_exp2nextreg(fs, v);  /* fix it at the last register */
}


void open_func (LexState *ls, FuncState *fs, BlockCnt *bl) {
  Proto *f = fs->f;
  fs->prev = ls->fs;  /* linked list of funcstates */
  fs->ls = ls;
  ls->fs = fs;
  fs->pc = 0;
  fs->previousline = f->linedefined;
  fs->iwthabs = 0;
  fs->lasttarget = 0;
  fs->freereg = 0;
  fs->nk = 0;
  fs->nabslineinfo = 0;
  fs->np = 0;
  fs->nups = 0;
  fs->ndebugvars = 0;
  fs->nactvar = 0;
  fs->needclose = 0;
  fs->firstlocal = ls->dyd->actvar.n;
  fs->firstlabel = ls->dyd->label.n;
  fs->bl = NULL;
  f->source = ls->source;
  luaC_objbarrier(ls->L, f, f->source);
  f->maxstacksize = 2;  /* registers 0/1 are always valid */
  enterblock(fs, bl, 0);
}


void close_func (LexState *ls) {
  lua_State *L = ls->L;
  FuncState *fs = ls->fs;
  Proto *f = fs->f;
  leaveblock(fs);
  luaK_ret(fs, luaY_nvarstack(fs), 0);  /* final return */
  lua_assert(fs->bl == NULL);
  luaK_finish(fs);
  luaM_shrinkvector(L, f->code, f->sizecode, fs->pc, Instruction);
  luaM_shrinkvector(L, f->lineinfo, f->sizelineinfo, fs->pc, ls_byte);
  luaM_shrinkvector(L, f->abslineinfo, f->sizeabslineinfo,
                       fs->nabslineinfo, AbsLineInfo);
  luaM_shrinkvector(L, f->k, f->sizek, fs->nk, TValue);
  luaM_shrinkvector(L, f->p, f->sizep, fs->np, Proto *);
  luaM_shrinkvector(L, f->locvars, f->sizelocvars, fs->ndebugvars, LocVar);
  luaM_shrinkvector(L, f->upvalues, f->sizeupvalues, fs->nups, Upvaldesc);
  ls->fs = fs->prev;
  luaC_checkGC(L);
}


/*
** Create a global variable with the given name.
*/
static void buildglobal (LexState *ls, TString *varname, expdesc *var) {
  FuncState *fs = ls->fs;
  expdesc key;
  singlevaraux(fs, ls->envn, var, 1);  /* get environment variable */
  lua_assert(var->k != VVOID);  /* this one must exist */
  codestring(&key, varname);  /* key is variable name */
  luaK_indexed(fs, var, &key);  /* env[varname] */
}


/*
** Create a new variable with the given name and kind.
** Return its index in the function.
*/
static int new_varkind (LexState *ls, TString *name, lu_byte kind) {
  lua_State *L = ls->L;
  FuncState *fs = ls->fs;
  Dyndata *dyd = ls->dyd;
  Vardesc *var;
  checkforshadowing(ls, fs, name);
  checklimit(fs, dyd->actvar.n + 1 - fs->firstlocal,
                 MAXVARS, "local variables");
  luaM_growvector(L, dyd->actvar.arr, dyd->actvar.n + 1,
                  dyd->actvar.size, Vardesc, USHRT_MAX, "local variables");
  var = &dyd->actvar.arr[dyd->actvar.n++];
  var->vd.kind = kind;
  var->vd.name = name;
  var->vd.used = 0;
  return dyd->actvar.n - 1 - fs->firstlocal;
}

/*=================================================*/
/* GRAMMAR RULES */
/*=================================================*/


/*
** check whether current token is in the follow set of a block.
** 'until' closes syntactical blocks, but do not close scope,
** so it is handled in separate.
*/
static int block_follow (LexState *ls, int withuntil) {
  switch (ls->t.token) {
    case TK_ELSE: case TK_ELSEIF:
    case TK_END: case TK_EOS:
    case TK_CASE: case TK_DEFAULT:
    case '}':
      return 1;
    case TK_DOLLAR: {
       int la = luaX_lookahead(ls);
       if (la == TK_NAME) {
          const char *name = getstr(ls->lookahead.seminfo.ts);
          if (strcmp(name, "else") == 0 ||
              strcmp(name, "elseif") == 0 ||
              strcmp(name, "end") == 0) {
             return 1;
          }
       }
       else if (la == TK_ELSE || la == TK_ELSEIF || la == TK_END) {
          return 1;
       }
       return 0;
    }
    case TK_UNTIL: return withuntil;
    default: return 0;
  }
}


void statlist (LexState *ls) {
  /* statlist -> { stat [';'] } */
  while (!block_follow(ls, 1)) {

    statement(ls);
  }
}


static void fieldsel (LexState *ls, expdesc *v) {
  /* fieldsel -> ['.' | ':' | '::'] NAME */
  LUA_LOGD("[PARSER] fieldsel: token=%d", ls->t.token);
  FuncState *fs = ls->fs;
  expdesc key;
  luaK_exp2anyregup(fs, v);
  luaX_next(ls);  /* skip the dot or colon or double colon */
  
  /* Allow keywords as field names */
  if (ls->t.token == TK_NAME) {
    codename(ls, &key);
  }
  else {
    /* Handle keywords as field names */
    TString *ts;
    switch (ls->t.token) {
      /* Reserved words that can be used as field names */
      case TK_AND: ts = luaS_newliteral(ls->L, "and"); break;
      case TK_ASM: ts = luaS_newliteral(ls->L, "asm"); break;
      case TK_ASTPARSER: ts = luaS_newliteral(ls->L, "astparser"); break;
      case TK_ASYNC: ts = luaS_newliteral(ls->L, "async"); break;
      case TK_AWAIT: ts = luaS_newliteral(ls->L, "await"); break;
      case TK_BREAK: ts = luaS_newliteral(ls->L, "break"); break;
      case TK_CASE: ts = luaS_newliteral(ls->L, "case"); break;
      case TK_CATCH: ts = luaS_newliteral(ls->L, "catch"); break;
      case TK_COMMAND: ts = luaS_newliteral(ls->L, "command"); break;
      case TK_CONST: ts = luaS_newliteral(ls->L, "const"); break;
      case TK_CONTINUE: ts = luaS_newliteral(ls->L, "continue"); break;
      case TK_DEFAULT: ts = luaS_newliteral(ls->L, "default"); break;
      case TK_DEFER: ts = luaS_newliteral(ls->L, "defer"); break;
      case TK_DELETE: ts = luaS_newliteral(ls->L, "delete"); break;
      case TK_GUARD: ts = luaS_newliteral(ls->L, "guard"); break;
      case TK_LET: ts = luaS_newliteral(ls->L, "let"); break;
      case TK_DO: ts = luaS_newliteral(ls->L, "do"); break;
      case TK_ELSE: ts = luaS_newliteral(ls->L, "else"); break;
      case TK_ELSEIF: ts = luaS_newliteral(ls->L, "elseif"); break;
      case TK_END: ts = luaS_newliteral(ls->L, "end"); break;
      case TK_ENUM: ts = luaS_newliteral(ls->L, "enum"); break;
      case TK_FALSE: ts = luaS_newliteral(ls->L, "false"); break;
      case TK_FINALLY: ts = luaS_newliteral(ls->L, "finally"); break;
      case TK_FOR: ts = luaS_newliteral(ls->L, "for"); break;
      case TK_FUNCTION: ts = luaS_newliteral(ls->L, "function"); break;
      case TK_GLOBAL: ts = luaS_newliteral(ls->L, "global"); break;
      case TK_GOTO: ts = luaS_newliteral(ls->L, "goto"); break;
      case TK_IF: ts = luaS_newliteral(ls->L, "if"); break;
      case TK_IN: ts = luaS_newliteral(ls->L, "in"); break;
      case TK_IS: ts = luaS_newliteral(ls->L, "is"); break;
      case TK_INSTANCEOF: ts = luaS_newliteral(ls->L, "instanceof"); break;
      case TK_LAMBDA: ts = luaS_newliteral(ls->L, "lambda"); break;
      case TK_LOCAL: ts = luaS_newliteral(ls->L, "local"); break;
      case TK_NIL: ts = luaS_newliteral(ls->L, "nil"); break;
      case TK_NOT: ts = luaS_newliteral(ls->L, "not"); break;
      case TK_OR: ts = luaS_newliteral(ls->L, "or"); break;
      case TK_REPEAT: ts = luaS_newliteral(ls->L, "repeat"); break;
      case TK_RETURN: ts = luaS_newliteral(ls->L, "return"); break;
      case TK_STRUCT: ts = luaS_newliteral(ls->L, "struct"); break;
      case TK_SWITCH: ts = luaS_newliteral(ls->L, "switch"); break;
      case TK_TAKE: ts = luaS_newliteral(ls->L, "take"); break;
      case TK_THEN: ts = luaS_newliteral(ls->L, "then"); break;
      case TK_TRUE: ts = luaS_newliteral(ls->L, "true"); break;
      case TK_TRY: ts = luaS_newliteral(ls->L, "try"); break;
      case TK_UNTIL: ts = luaS_newliteral(ls->L, "until"); break;
      case TK_USING: ts = luaS_newliteral(ls->L, "using"); break;
      case TK_WHEN: ts = luaS_newliteral(ls->L, "when"); break;
      case TK_WITH: ts = luaS_newliteral(ls->L, "with"); break;
      case TK_WHILE: ts = luaS_newliteral(ls->L, "while"); break;
      case TK_KEYWORD: ts = luaS_newliteral(ls->L, "keyword"); break;
      case TK_OPERATOR: ts = luaS_newliteral(ls->L, "operator"); break;
      case TK_TYPE_INT: ts = luaS_newliteral(ls->L, "int"); break;
      case TK_TYPE_FLOAT: ts = luaS_newliteral(ls->L, "float"); break;
      case TK_DOUBLE: ts = luaS_newliteral(ls->L, "double"); break;
      case TK_BOOL: ts = luaS_newliteral(ls->L, "bool"); break;
      case TK_VOID: ts = luaS_newliteral(ls->L, "void"); break;
      case TK_CHAR: ts = luaS_newliteral(ls->L, "char"); break;
      case TK_LONG: ts = luaS_newliteral(ls->L, "long"); break;
      default: error_expected(ls, TK_NAME);
    }
    codestring(&key, ts);
    luaX_next(ls);
  }
  luaK_indexed(fs, v, &key);
}


static void yindex (LexState *ls, expdesc *v) {
  /* index -> '[' expr ']' */
  luaX_next(ls);  /* skip the '[' */
  expr(ls, v);
  luaK_exp2val(ls->fs, v);
  checknext(ls, ']');
}


/*
** 检查当前是否是切片语法的开始
** 切片语法: [start:end] 或 [start:end:step] 或 [:end] 或 [start:] 等
** 返回 1 表示是切片语法，0 表示是普通索引
** 
** @param ls 词法分析器状态
** @return 1 表示是切片语法，0 表示是普通索引
*/
static int is_slice_syntax (LexState *ls) {
  /* 已经在 '[' 后面，检查第一个 token 是否是 ':' */
  if (ls->t.token == ':') {
    return 1;  /* [:end] 或 [::step] 等形式 */
  }
  /* 需要向前看来确定是否是切片 */
  /* 解析第一个表达式，然后检查是否后面跟着 ':' */
  /* 但这样会消耗 token，所以我们使用 lookahead */
  return 0;  /* 默认按普通索引处理，在 yindex_or_slice 中进一步判断 */
}


/*
** 处理切片表达式语法: t[start:end] 或 t[start:end:step]
** 支持的形式:
** - t[a:b]     从索引 a 到 b（包含两端）
** - t[a:b:s]   从索引 a 到 b，步长为 s
** - t[:b]      等价于 t[1:b]
** - t[a:]      等价于 t[a:#t]
** - t[:]       等价于 t[1:#t] (复制整个数组部分)
** - t[::s]     步长为 s 的整个数组
** - 负索引：t[-3:-1] 取倒数3到倒数1个元素
** 
** 生成代码：
** - 将源表放入寄存器 base
** - 将 start, end, step 放入 base+1, base+2, base+3
** - 生成 OP_SLICE 指令
** 
** @param ls 词法分析器状态
** @param v 输入为源表表达式，输出为切片结果表达式
*/
static void sliceexpr (LexState *ls, expdesc *v) {
  FuncState *fs = ls->fs;
  int base;  /* 基础寄存器 */
  expdesc start_exp, end_exp, step_exp;
  int has_step = 0;
  
  /* 已经跳过了 '[' */
  
  /* 将源表放入寄存器 */
  luaK_exp2nextreg(fs, v);
  base = v->u.info;  /* 源表在 base 寄存器 */
  
  /* 解析 start 表达式 */
  if (ls->t.token == ':') {
    /* 省略 start，使用 nil 表示 1 */
    init_exp(&start_exp, VNIL, 0);
  }
  else {
    expr(ls, &start_exp);
  }
  luaK_exp2nextreg(fs, &start_exp);  /* start 在 base+1 */
  
  /* 必须有第一个 ':' */
  checknext(ls, ':');
  
  /* 解析 end 表达式 */
  if (ls->t.token == ']' || ls->t.token == ':') {
    /* 省略 end，使用 nil 表示 #t */
    init_exp(&end_exp, VNIL, 0);
  }
  else {
    expr(ls, &end_exp);
  }
  luaK_exp2nextreg(fs, &end_exp);  /* end 在 base+2 */
  
  /* 检查是否有 step */
  if (testnext(ls, ':')) {
    has_step = 1;
    if (ls->t.token == ']') {
      /* 省略 step，使用 nil 表示 1 */
      init_exp(&step_exp, VNIL, 0);
    }
    else {
      expr(ls, &step_exp);
    }
    luaK_exp2nextreg(fs, &step_exp);  /* step 在 base+3 */
  }
  else {
    /* 没有 step，使用 nil 表示 1 */
    init_exp(&step_exp, VNIL, 0);
    luaK_exp2nextreg(fs, &step_exp);  /* step 在 base+3 */
  }
  
  checknext(ls, ']');  /* 必须以 ']' 结束 */
  
  /* 生成 OP_SLICE 指令 */
  /* A = 结果寄存器（复用 base）, B = 源表寄存器, C = 标志 */
  luaK_codeABC(fs, OP_SLICE, base, base, has_step);
  
  /* 释放临时寄存器 (start, end, step) */
  fs->freereg = base + 1;
  
  /* 设置结果表达式 */
  v->k = VNONRELOC;
  v->u.info = base;
}


/*
** 处理索引或切片语法: t[exp] 或 t[start:end:step]
** 首先解析第一个表达式或检测 ':'，然后决定是普通索引还是切片
** 
** @param ls 词法分析器状态
** @param v 输入为源表表达式，输出为索引/切片结果表达式
** @return 1 如果是切片，0 如果是普通索引
*/
static int yindex_or_slice (LexState *ls, expdesc *v) {
  FuncState *fs = ls->fs;
  
  luaX_next(ls);  /* skip the '[' */
  
  /* 检查是否是切片语法: 第一个 token 是 ':' 或 TK_DBCOLON (::) */
  if (ls->t.token == ':' || ls->t.token == TK_DBCOLON) {
    /* 如果是 TK_DBCOLON (::)，需要拆分成两个 ':' token */
    int is_dbcolon = (ls->t.token == TK_DBCOLON);
    if (is_dbcolon) {
      /* 将当前 TK_DBCOLON 替换为 ':'，并将第二个 ':' 放入 pending */
      ls->t.token = ':';
      /* 使用 lexer 的 pending 机制塞入一个 ':' token */
      static Token pending_colon;
      pending_colon.token = ':';
      pending_colon.seminfo.ts = NULL;
      ls->pending_tokens = &pending_colon;
      ls->npending = 1;
      ls->pending_idx = 0;  /* 0 < 1，下次 llex 即返回这个 ':' */
    }
    
    /* 这是切片语法: [:end] 或 [::step] 等形式 */
    
    /* 将源表放入寄存器 */
    luaK_exp2nextreg(fs, v);
    int base = v->u.info;
    
    expdesc start_exp, end_exp, step_exp;
    int has_step = 0;
    
    /* start 省略，使用 nil */
    init_exp(&start_exp, VNIL, 0);
    luaK_exp2nextreg(fs, &start_exp);
    
    /* 跳过第一个 ':' */
    luaX_next(ls);
    
    /* 解析 end */
    if (ls->t.token == ']' || ls->t.token == ':') {
      init_exp(&end_exp, VNIL, 0);
    }
    else {
      expr(ls, &end_exp);
    }
    luaK_exp2nextreg(fs, &end_exp);
    
    /* 检查是否有 step */
    if (testnext(ls, ':')) {
      has_step = 1;
      if (ls->t.token == ']') {
        init_exp(&step_exp, VNIL, 0);
      }
      else {
        expr(ls, &step_exp);
      }
      luaK_exp2nextreg(fs, &step_exp);
    }
    else {
      init_exp(&step_exp, VNIL, 0);
      luaK_exp2nextreg(fs, &step_exp);
    }
    
    checknext(ls, ']');
    
    luaK_codeABC(fs, OP_SLICE, base, base, has_step);
    fs->freereg = base + 1;
    
    v->k = VNONRELOC;
    v->u.info = base;
    return 1;  /* 是切片 */
  }
  
  /* 
  ** 关键：在解析 key 表达式之前，先固定源表的位置
  ** 这样在 expr(ls, &key) 执行期间，v 的值不会被破坏
  */
  luaK_exp2anyregup(fs, v);
  
  /* 解析第一个表达式 */
  expdesc key;
  expr(ls, &key);
  
  /* 检查表达式后面是否跟着 ':' 或 TK_DBCOLON (::) */
  if (ls->t.token == ':' || ls->t.token == TK_DBCOLON) {
    /* 如果是 TK_DBCOLON (::)，需要拆分成两个 ':' token */
    int is_dbcolon2 = (ls->t.token == TK_DBCOLON);
    if (is_dbcolon2) {
      ls->t.token = ':';
      static Token pending_colon2;
      pending_colon2.token = ':';
      pending_colon2.seminfo.ts = NULL;
      ls->pending_tokens = &pending_colon2;
      ls->npending = 1;
      ls->pending_idx = 0;
    }
    
    /* 这是切片语法: [start:end] 或 [start:end:step] */
    
    /* 将源表移动到下一个连续寄存器位置（切片需要连续的寄存器布局） */
    luaK_exp2nextreg(fs, v);
    int base = v->u.info;
    
    /* 将 start 表达式放入下一个寄存器 */
    luaK_exp2nextreg(fs, &key);
    
    expdesc end_exp, step_exp;
    int has_step = 0;
    
    /* 跳过 ':' */
    luaX_next(ls);
    
    /* 解析 end */
    if (ls->t.token == ']' || ls->t.token == ':') {
      init_exp(&end_exp, VNIL, 0);
    }
    else {
      expr(ls, &end_exp);
    }
    luaK_exp2nextreg(fs, &end_exp);
    
    /* 检查是否有 step */
    if (testnext(ls, ':')) {
      has_step = 1;
      if (ls->t.token == ']') {
        init_exp(&step_exp, VNIL, 0);
      }
      else {
        expr(ls, &step_exp);
      }
      luaK_exp2nextreg(fs, &step_exp);
    }
    else {
      init_exp(&step_exp, VNIL, 0);
      luaK_exp2nextreg(fs, &step_exp);
    }
    
    checknext(ls, ']');
    
    luaK_codeABC(fs, OP_SLICE, base, base, has_step);
    fs->freereg = base + 1;
    
    v->k = VNONRELOC;
    v->u.info = base;
    return 1;  /* 是切片 */
  }
  
  /* 普通索引: [exp] */
  /* v 已经在前面通过 luaK_exp2anyregup 固定好了 */
  luaK_exp2val(fs, &key);
  checknext(ls, ']');
  luaK_indexed(fs, v, &key);
  return 0;  /* 不是切片 */
}


/*
** {===========================================================
** Rules for Constructors
** ============================================================
*/


typedef struct ConsControl {
  expdesc v;  /* last list item read */
  expdesc *t;  /* table descriptor */
  int nh;  /* total number of 'record' elements */
  int na;  /* number of array elements already stored */
  int tostore;  /* number of array elements pending to be stored */
  int has_spread; /* whether a spread operator was encountered */
} ConsControl;


static void recfield (LexState *ls, ConsControl *cc) {
  /* recfield -> (NAME | '['exp']') = exp */
  FuncState *fs = ls->fs;
  int reg = ls->fs->freereg;
  expdesc tab, key, val;
  if (is_nametoken(ls->t.token)) {
    checklimit(fs, cc->nh, MAX_INT, "items in a constructor");
    TString *ts = str_checkname(ls);
    codestring(&key, ts);
  }
  else  /* ls->t.token == '[' */
    yindex(ls, &key);
  cc->nh++;
  if (ls->t.token != '=' && ls->t.token != ':')
    error_expected(ls, '=');
  luaX_next(ls);
  tab = *cc->t;
  luaK_indexed(fs, &tab, &key);
  expr(ls, &val);
  luaK_storevar(fs, &tab, &val);
  fs->freereg = reg;  /* free registers */
}


static void closelistfield (FuncState *fs, ConsControl *cc) {
  if (cc->v.k == VVOID) return;  /* there is no list item */
  luaK_exp2nextreg(fs, &cc->v);
  cc->v.k = VVOID;
  if (cc->tostore == LFIELDS_PER_FLUSH) {
    if (!cc->has_spread) {
        luaK_setlist(fs, cc->t->u.info, cc->na, cc->tostore);  /* flush */
    }
    cc->na += cc->tostore;
    cc->tostore = 0;  /* no more items pending */
  }
}


static void lastlistfield (FuncState *fs, ConsControl *cc) {
  if (cc->tostore == 0) return;
  if (cc->has_spread && !hasmultret(cc->v.k)) return; /* If there was a spread, do not emit SETLIST for remaining! */
  if (hasmultret(cc->v.k)) {
    luaK_setmultret(fs, &cc->v);
    
    if (cc->has_spread) {
        /* Note: Using multret after spread operator overwrites dynamically added elements because `cc->na` is static. 
           Proper multret spread will require a new runtime instruction or complex loop block handling L->top, 
           so for now we preserve standard behavior which uses SETLIST and relies on cc->na. 
           We simply allow `hasmultret` to proceed and use `luaK_setlist`.
         */
    }
    
    luaK_setlist(fs, cc->t->u.info, cc->na, LUA_MULTRET);
    cc->na--;  /* do not count last expression (unknown number of elements) */
  }
  else {
    if (cc->v.k != VVOID)
      luaK_exp2nextreg(fs, &cc->v);
    /* Only flush statically if we have pending items to store and haven't had a spread */
    if (cc->tostore > 0 && !cc->has_spread) {
      luaK_setlist(fs, cc->t->u.info, cc->na, cc->tostore);
    }
  }
  cc->na += cc->tostore;
}


static void listfield (LexState *ls, ConsControl *cc) {
  /* listfield -> exp */
  expr(ls, &cc->v);
  
  if (cc->has_spread) {
      /* Dynamic append: table[#table+1] = exp */
      FuncState *fs = ls->fs;
      luaK_exp2nextreg(fs, &cc->v);
      
      int len_reg = fs->freereg;
      luaK_reserveregs(fs, 1);
      luaK_codeABC(fs, OP_LEN, len_reg, cc->t->u.info, 0);
      luaK_codeABCk(fs, OP_ADDI, len_reg, len_reg, int2sC(1), 0);
      
      expdesc tab, key;
      tab = *cc->t;
      init_exp(&key, VNONRELOC, len_reg);
      luaK_indexed(fs, &tab, &key);
      luaK_storevar(fs, &tab, &cc->v);
      
      fs->freereg = len_reg; /* Free the len_reg */
      cc->v.k = VVOID; /* Clear the value */
  } else {
      cc->tostore++;
  }
}


static void field (LexState *ls, ConsControl *cc) {
  /* field -> listfield | recfield */
  switch(ls->t.token) {
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
    case TK_LET: {  /* may be 'listfield' or 'recfield' */
      int lookahead = luaX_lookahead(ls);
      if (lookahead != '=' && lookahead != ':')  /* expression? */
        listfield(ls, cc);
      else
        recfield(ls, cc);
      break;
    }
    case '[': {
      recfield(ls, cc);
      break;
    }
    default: {
      listfield(ls, cc);
      break;
    }
  }
}


static void constructor (LexState *ls, expdesc *t) {
  FuncState *fs = ls->fs;
  int line = ls->linenumber;
  int pc;
  ConsControl cc;

  checknext(ls, '{');

  if (ls->t.token == '}') {
      pc = luaK_codeABC(fs, OP_NEWTABLE, 0, 0, 0);
      luaK_code(fs, 0);
      init_exp(t, VNONRELOC, fs->freereg);
      luaK_reserveregs(fs, 1);
      check_match(ls, '}', '{', line);
      luaK_settablesize(fs, pc, t->u.info, 0, 0);
      return;
  }

  if (ls->t.token == TK_FOR) {
      FuncState new_fs;
      BlockCnt bl;
      new_fs.f = addprototype(ls);
      new_fs.f->linedefined = line;
      open_func(ls, &new_fs, &bl);

      int t_vidx = new_localvarliteral(ls, "_t");
      adjustlocalvars(ls, 1);
      int t_reg = getlocalvardesc(&new_fs, t_vidx)->vd.ridx;
      new_fs.freereg = t_reg + 1;

      luaK_codeABC(&new_fs, OP_NEWTABLE, t_reg, 0, 0);
      luaK_code(&new_fs, 0);

      checknext(ls, TK_FOR);

      int base = new_fs.freereg;
      new_localvarliteral(ls, "(for state)");
      new_localvarliteral(ls, "(for state)");
      new_localvarliteral(ls, "(for state)");
      new_localvarliteral(ls, "(for state)");

      TString *loop_vars[20];
      int nvars = 0;
      do {
        loop_vars[nvars++] = str_checkname(ls);
      } while (testnext(ls, ',') && nvars < 20);

      checknext(ls, TK_IN);

      expdesc e;
      int nexps = explist(ls, &e);
      adjust_assign(ls, 4, nexps, &e);
      luaK_checkstack(&new_fs, 4);

      adjustlocalvars(ls, 4);

      int prep_jmp = luaK_codeABx(&new_fs, OP_TFORPREP, base, 0);
      int loop_start = luaK_getlabel(&new_fs);

      for (int i = 0; i < nvars; i++) {
          new_localvar(ls, loop_vars[i]);
      }
      adjustlocalvars(ls, nvars);
      luaK_reserveregs(&new_fs, nvars);

      if (ls->t.token == TK_DO) {
          luaX_next(ls);
      } else if (ls->t.token == TK_NAME && strcmp(getstr(ls->t.seminfo.ts), "yield") == 0) {
          luaX_next(ls);
      } else {
          luaX_syntaxerror(ls, "expected 'do' or 'yield' in dict comprehension");
      }

      expdesc key_v, val_v;
      expr(ls, &key_v);
      luaK_exp2nextreg(&new_fs, &key_v);
      checknext(ls, ',');
      expr(ls, &val_v);
      luaK_exp2nextreg(&new_fs, &val_v);

      int if_jmp = NO_JUMP;
      if (testnext(ls, TK_IF)) {
        expdesc cond_v;
        expr(ls, &cond_v);
        luaK_goiftrue(&new_fs, &cond_v);
        if_jmp = cond_v.f;
      }

      expdesc tab;
      init_exp(&tab, VNONRELOC, t_reg);
      luaK_indexed(&new_fs, &tab, &key_v);
      luaK_storevar(&new_fs, &tab, &val_v);

      if (if_jmp != NO_JUMP) {
        luaK_patchtohere(&new_fs, if_jmp);
      }

      new_fs.freereg = base + 4 + nvars;

      fixforjump(&new_fs, prep_jmp, luaK_getlabel(&new_fs), 0);
      luaK_codeABC(&new_fs, OP_TFORCALL, base, 0, nvars);
      int loop_jmp = luaK_codeABx(&new_fs, OP_TFORLOOP, base, 0);
      fixforjump(&new_fs, loop_jmp, prep_jmp + 1, 1);

      luaK_ret(&new_fs, t_reg, 1);

      check_match(ls, '}', '{', line);

      new_fs.f->lastlinedefined = ls->linenumber;
      close_func(ls);

      init_exp(t, VRELOC, luaK_codeABx(fs, OP_CLOSURE, 0, fs->np - 1));
      luaK_exp2nextreg(fs, t);

      int func_reg = t->u.info;
      init_exp(t, VCALL, luaK_codeABC(fs, OP_CALL, func_reg, 1, 2));
      luaK_fixline(fs, line);
      fs->freereg = func_reg + 1;

      return;
  }

  pc = luaK_codeABC(fs, OP_NEWTABLE, 0, 0, 0);
  luaK_code(fs, 0);  /* space for extra arg. */
  cc.na = cc.nh = cc.tostore = 0;
  cc.t = t;
  cc.has_spread = 0;
  init_exp(t, VNONRELOC, fs->freereg);  /* table will be at stack top */
  luaK_reserveregs(fs, 1);
  init_exp(&cc.v, VVOID, 0);  /* no value (yet) */

  do {
    lua_assert(cc.v.k == VVOID || cc.tostore > 0);
    if (ls->t.token == '}') break;

    if (ls->t.token == TK_DOTS) {
        closelistfield(fs, &cc);

        int la = luaX_lookahead(ls);
        if ((la == TK_NAME || la == '(' || la == '{' || la == TK_STRING
             || la == TK_RAWSTRING || la == TK_INTERPSTRING
             || la == TK_INT || la == TK_FLT
             || la == TK_TRUE || la == TK_FALSE || la == TK_NIL
             || la == '-' || la == TK_NOT || la == '#' || la == '~'
             || la == TK_FUNCTION || la == TK_LAMBDA)) {

            luaX_next(ls);

            expdesc table_mod, mkey;
            singlevaraux(fs, luaS_newliteral(ls->L, "table"), &table_mod, 1);
            if (table_mod.k == VVOID) {
                expdesc envkey;
                singlevaraux(fs, ls->envn, &table_mod, 1);
                codestring(&envkey, luaS_newliteral(ls->L, "table"));
                luaK_indexed(fs, &table_mod, &envkey);
            }
            luaK_exp2anyregup(fs, &table_mod);
            codestring(&mkey, luaS_newliteral(ls->L, "merge"));
            luaK_indexed(fs, &table_mod, &mkey);

            luaK_exp2nextreg(fs, &table_mod);
            int func_reg = table_mod.u.info;

            luaK_reserveregs(fs, 2);

            luaK_codeABC(fs, OP_MOVE, func_reg + 1, cc.t->u.info, 0);

            expdesc src_tab;
            expr(ls, &src_tab);
            luaK_exp2reg(fs, &src_tab, func_reg + 2);

            luaK_codeABC(fs, OP_CALL, func_reg, 3, 1);
            fs->freereg = func_reg;

            cc.v.k = VVOID;
            cc.tostore = 0;
            cc.has_spread = 1;
        } else {
            expr(ls, &cc.v);

            if (cc.has_spread) {
                luaK_exp2nextreg(fs, &cc.v);
                int len_reg = fs->freereg;
                luaK_reserveregs(fs, 1);
                luaK_codeABC(fs, OP_LEN, len_reg, cc.t->u.info, 0);
                luaK_codeABCk(fs, OP_ADDI, len_reg, len_reg, int2sC(1), 0);
                expdesc tab, key;
                tab = *cc.t;
                init_exp(&key, VNONRELOC, len_reg);
                luaK_indexed(fs, &tab, &key);
                luaK_storevar(fs, &tab, &cc.v);
                fs->freereg = len_reg;
                cc.v.k = VVOID;
            } else {
                cc.tostore++;
            }
            cc.has_spread = 1;
        }
    } else {
        closelistfield(fs, &cc);
        field(ls, &cc);
    }
  } while (testnext(ls, ',') || testnext(ls, ';'));
  check_match(ls, '}', '{', line);
  lastlistfield(fs, &cc);
  luaK_settablesize(fs, pc, t->u.info, cc.na, cc.nh);
}


/*
** 解析map字面量: [key = val, ...]
** map容器仅支持 key=value 对，不支持数组形式
** 与table的{}完全不同：底层使用OP_NEWMAP/OP_MAPSET
*/
static void mapconstructor (LexState *ls, expdesc *t) {
  FuncState *fs = ls->fs;
  int line = ls->linenumber;

  checknext(ls, '[');

  /* 生成OP_NEWMAP，预留寄存器 */
  int pc = luaK_codeABC(fs, OP_NEWMAP, fs->freereg, 0, 0);
  luaK_code(fs, 0);  /* extra arg */
  init_exp(t, VNONRELOC, fs->freereg);
  luaK_reserveregs(fs, 1);

  /* 空map: [] */
  if (ls->t.token == ']') {
    check_match(ls, ']', '[', line);
    return;
  }

  do {
    int map_reg = t->u.info;
    expdesc key, val;

    if (ls->t.token == '[') {
      /* [expr] = value: 计算键的表达式（优先级最高，避免与 name sugar 冲突） */
      luaX_next(ls);  /* 跳过 '[' */
      expr(ls, &key);
      checknext(ls, ']');
      checknext(ls, '=');
    }
    else if (ls->t.token == TK_NAME && luaX_lookahead(ls) == '=') {
      /* name = value 语法糖 (等价于 ["name"] = value) */
      /* 只有当下一个token是'='时才识别为name sugar */
      codestring(&key, str_checkname(ls));
      checknext(ls, '=');
    }
    else {
      /* map不支持不带key的数组形式，与table明确区分 */
      luaX_syntaxerror(ls, "map constructor requires 'key = value' or '[expr] = value' pair");
    }

    expr(ls, &val);

    /* 生成 OP_MAPSET: R[map_reg][R[key_reg]] := RK(val_reg) */
    luaK_exp2nextreg(fs, &key);
    int key_reg = key.u.info;
    luaK_exp2nextreg(fs, &val);
    int val_reg = val.u.info;

    luaK_codeABC(fs, OP_MAPSET, map_reg, key_reg, val_reg);

    fs->freereg = val_reg;  /* 释放临时寄存器，key和val共用一个reg */
    key_reg = key_reg;  /* 抑制未使用警告 */
  } while (testnext(ls, ',') || testnext(ls, ';'));

  check_match(ls, ']', '[', line);
  UNUSED(pc);
}


/* }=========================================================== */


static void setvararg (FuncState *fs, int nparams) {
  fs->f->is_vararg = 1;
  luaK_codeABC(fs, OP_VARARGPREP, nparams, 0, 0);
}


void namedvararg (LexState *ls, TString *varargname) {
  enterlevel(ls);
  new_localvar(ls, varargname);

  FuncState *fs = ls->fs;
  int pc = luaK_codeABC(fs, OP_NEWTABLE, fs->freereg, 0, 0);
  ConsControl cc;
  luaK_code(fs, 0);
  expdesc t;
  init_exp(&t, VNONRELOC, fs->freereg);
  cc.na = cc.nh = cc.tostore = 0;
  cc.t = &t;
  luaK_reserveregs(fs, 1);

  init_exp(&cc.v, VVARARG, luaK_codeABC(fs, OP_VARARG, 0, 0, 1));
  cc.tostore++;
  lastlistfield(fs, &cc);
  luaK_settablesize(fs, pc, t.u.info, cc.na, cc.nh);

  adjust_assign(ls, 1, 1, &t);
  adjustlocalvars(ls, 1);
  leavelevel(ls);
}


/**
 * 解析函数参数列表
 * 支持参数默认值语法：name = expr
 * 
 * 语法规则:
 *   parlist -> [ {NAME ['=' expr] ','} (NAME ['=' expr] | '...') ]
 * 
 * 默认值语义：
 *   - 当调用方未传入该参数（参数为nil）时，使用默认值
 *   - 显式传入nil也会触发默认值替换
 *   - 默认值表达式可以引用前面已声明的参数
 *   - 默认值可以是任意表达式（常量、变量、函数调用等）
 * 
 * 示例:
 *   function foo(x = 10, y = "hello", z = x * 2)
 *   function bar(a, b = 0, c = {})
 * 
 * 生成的字节码等价于:
 *   function foo(x, y, z)
 *       if x == nil then x = 10 end
 *       if y == nil then y = "hello" end
 *       if z == nil then z = x * 2 end
 *       -- 原始函数体
 *   end
 * 
 * @param ls 词法分析器状态
 * @param varargname 输出参数，如果存在具名可变参数则存储其名称
 */
void parlist (LexState *ls, TString **varargname) {
  /* parlist -> [ {NAME [':' type] ['=' expr] ','} (NAME [':' type] ['=' expr] | '...') ] */
  FuncState *fs = ls->fs;
  Proto *f = fs->f;
  int nparams = 0;
  int isvararg = 0;
  if (ls->t.token != ')') {  /* is 'parlist' not empty? */
    do {
      if (is_nametoken(ls->t.token)) {
          int vidx = new_localvar(ls, str_checkname(ls));
          getlocalvardesc(fs, vidx)->vd.hint = gettypehint(ls);
          /* 立即激活该参数变量并分配寄存器，以便后续默认值表达式可以引用它 */
          adjustlocalvars(ls, 1);
          luaK_reserveregs(fs, 1);
          nparams++;
          /* 检查是否有默认值 '=' */
          if (testnext(ls, '=')) {
              int param_reg = getlocalvardesc(fs, fs->nactvar - 1)->vd.ridx;
              /*
              ** 生成 nil 检查和条件跳转：
              ** OP_TESTNIL param param 0 k=0
              **   k=0: 如果参数是nil，则跳过下一条JMP（继续执行默认值赋值）
              **   k=0: 如果参数不是nil，则不跳过，执行JMP跳过默认值赋值
              */
              luaK_codeABCk(fs, OP_TESTNIL, param_reg, param_reg, 0, 0);
              int jmp_skip = luaK_jump(fs);  /* 不是nil时执行此JMP，跳过默认值赋值 */
              /* 解析默认值表达式并将结果存入参数寄存器 */
              expdesc default_val;
              expr(ls, &default_val);
              luaK_exp2reg(fs, &default_val, param_reg);
              /* 修复跳转目标：不是nil时跳到此处 */
              luaK_patchtohere(fs, jmp_skip);
          }
      }
      else if (ls->t.token == TK_DOTS) {
          luaX_next(ls);
          isvararg = 1;
          if (varargname && ls->t.token == TK_NAME) {
             *varargname = ls->t.seminfo.ts;
             luaX_next(ls);
          }
      }
      else {
          luaX_syntaxerror(ls, "<name> or '...' expected");
      }
    } while (!isvararg && testnext(ls, ','));
  }
  /* 参数已在循环中逐个激活，此处只需设置 numparams 和 vararg 标记 */
  f->numparams = fs->nactvar;
  if (isvararg)
    setvararg(fs, f->numparams);  /* declared vararg */
}


/**
 * 解析函数体
 * 支持两种语法：
 *   1. 标准语法: function name(params) block end
 *   2. 大括号语法糖: function name{block} (无参数函数的简写形式)
 * 
 * @param ls 词法状态
 * @param e 表达式描述符，用于存储闭包结果
 * @param ismethod 是否为方法（需要添加self参数）
 * @param line 函数定义所在行号
 */
static void body (LexState *ls, expdesc *e, int ismethod, int line) {
  /* body ->  '(' parlist ')' block END | '{' block '}' */
  FuncState new_fs;
  BlockCnt bl;
  int is_generic_factory = 0;
  
  if (ls->t.token == '{') {
    new_fs.f = addprototype(ls);
    new_fs.f->linedefined = line;
    open_func(ls, &new_fs, &bl);
    luaX_next(ls);
    if (ismethod) {
      new_localvarliteral(ls, "self");
      adjustlocalvars(ls, 1);
      luaK_reserveregs(&new_fs, 1);
    }
    while (ls->t.token != '}' && ls->t.token != TK_EOS) {

      statement(ls);
    }
    check_match(ls, '}', '{', line);
    new_fs.f->lastlinedefined = ls->linenumber;
    codeclosure(ls, e);
    close_func(ls);
    return;
  }

  /* Standard syntax: (parlist) */
  new_fs.f = addprototype(ls);
  new_fs.f->linedefined = line;
  open_func(ls, &new_fs, &bl);

  /* Helper array to store type mappings for generics */
  TString *mappings[MAXVARS];
  int nmappings = 0;
  for (int i = 0; i < MAXVARS; i++) mappings[i] = NULL;

  checknext(ls, '(');

  int method_added_self = 0;
  if (ismethod) {
      int has_self = 0;
      if (ls->t.token == TK_NAME) {
        const char *name = getstr(ls->t.seminfo.ts);
        if (strcmp(name, "self") == 0) has_self = 1;
      }
      if (!has_self) {
        new_localvarliteral(ls, "self");
        adjustlocalvars(ls, 1);
        luaK_reserveregs(&new_fs, 1);  /* 为self参数分配寄存器 */
        method_added_self = 1;
      }
  }
  
  TString *varargname = NULL;
  parlist(ls, &varargname);
  checknext(ls, ')');

  /* 修复：如果自动添加了self参数，确保numparams包含self */
  if (method_added_self) {
      /* parlist 在末尾设置了 f->numparams = fs->nactvar，
         但如果由于某种原因 self 没有被计入，手动修正。
         这里我们强制确保第一个局部变量是 self 且计入 numparams。
         实际上 fs->nactvar 应该已经包含 self，所以 numparams 应该已经正确。
         但保险起见我们验证一下，不一致时修正。 */
      int expected = new_fs.nactvar;
      if ((int)new_fs.f->numparams < expected) {
          new_fs.f->numparams = cast_byte(expected);
      }
  }

  {
     int i;
     for (i = 0; i < new_fs.f->numparams; i++) {
        Vardesc *vd = getlocalvardesc(&new_fs, i);
        if (vd->vd.hint) {
           int j;
           for (j = 0; j < MAX_TYPE_DESCS; j++) {
              if (vd->vd.hint->descs[j].type == LVT_NAME && vd->vd.hint->descs[j].typename) {
                 /* Using OP_CHECKTYPE A B C */
                 expdesc e_val;
                 init_var(&new_fs, &e_val, i);
                 luaK_exp2anyreg(&new_fs, &e_val);
                 int val_reg = e_val.u.info;

                 expdesc e_type;
                 singlevaraux(&new_fs, vd->vd.hint->descs[j].typename, &e_type, 1);
                 if (e_type.k == VVOID) {
                    expdesc key;
                    singlevaraux(&new_fs, ls->envn, &e_type, 1);
                    codestring(&key, vd->vd.hint->descs[j].typename);
                    luaK_indexed(&new_fs, &e_type, &key);
                 }
                 luaK_exp2nextreg(&new_fs, &e_type);
                 int type_reg = e_type.u.info;

                 int name_k = luaK_stringK(&new_fs, vd->vd.name);

                 luaK_codeABC(&new_fs, OP_CHECKTYPE, val_reg, type_reg, name_k);

                 new_fs.freereg = type_reg; /* Free type_reg */
              }
           }
        }
     }
  }
  
  if (ls->t.token == TK_REQUIRES) {
      is_generic_factory = 1;
  }
  else if (ls->t.token == '(') {
      int la1 = luaX_lookahead(ls);
      if (la1 == ')') is_generic_factory = 1;
      else if (la1 == TK_DOTS) {
          if (luaX_lookahead2(ls) == ')') is_generic_factory = 1;
      }
      else if (la1 == TK_NAME) {
          int la2 = luaX_lookahead2(ls);
          if (la2 == ',' || la2 == ')' || la2 == ':') is_generic_factory = 1;
      }
  }

  if (is_generic_factory) {
      /* Generic Factory Function */
      /* Current new_fs is Factory */
      /* Captured params are generics */

      int ngeneric = new_fs.f->numparams;
      if (ismethod) ngeneric--; /* exclude self */

      /* Open Impl function */
      FuncState impl_fs;
      BlockCnt impl_bl;
      impl_fs.f = addprototype(ls);
      impl_fs.f->linedefined = line;
      open_func(ls, &impl_fs, &impl_bl);

      /* Parse Impl params */
      checknext(ls, '(');
      TString *impl_vararg = NULL;
      parlist(ls, &impl_vararg);
      checknext(ls, ')');

      /* Capture type hints for mapping */
      nmappings = impl_fs.f->numparams;
      for (int i = 0; i < nmappings && i < MAXVARS; i++) {
          Vardesc *vd = getlocalvardesc(&impl_fs, i);
          if (vd->vd.hint && vd->vd.hint->descs[0].type == LVT_NAME) {
              mappings[i] = vd->vd.hint->descs[0].typename;
          }
      }

      {
         int i;
         for (i = 0; i < impl_fs.f->numparams; i++) {
            Vardesc *vd = getlocalvardesc(&impl_fs, i);
            if (vd->vd.hint) {
               int j;
               for (j = 0; j < MAX_TYPE_DESCS; j++) {
                  if (vd->vd.hint->descs[j].type == LVT_NAME && vd->vd.hint->descs[j].typename) {
                 /* Using OP_CHECKTYPE A B C */
                     expdesc e_val;
                     init_var(&impl_fs, &e_val, i);
                 luaK_exp2anyreg(&impl_fs, &e_val);
                 int val_reg = e_val.u.info;

                     expdesc e_type;
                     singlevaraux(&impl_fs, vd->vd.hint->descs[j].typename, &e_type, 1);
                     if (e_type.k == VVOID) {
                        expdesc key;
                        singlevaraux(&impl_fs, ls->envn, &e_type, 1);
                        codestring(&key, vd->vd.hint->descs[j].typename);
                        luaK_indexed(&impl_fs, &e_type, &key);
                     }
                     luaK_exp2nextreg(&impl_fs, &e_type);
                 int type_reg = e_type.u.info;

                 int name_k = luaK_stringK(&impl_fs, vd->vd.name);

                 luaK_codeABC(&impl_fs, OP_CHECKTYPE, val_reg, type_reg, name_k);

                 impl_fs.freereg = type_reg;
                  }
               }
            }
         }
      }

      /* Parse return type hint if any */
      if (testnext(ls, ':')) {
          if (testnext(ls, '(')) {
             do {
                 TypeHint *th = typehint_new(ls);
                 checktypehint(ls, th);
             } while (testnext(ls, ','));
             checknext(ls, ')');
          } else {
             if (ls->t.token == TK_NAME && strcmp(getstr(ls->t.seminfo.ts), "void") == 0) {
                 luaX_next(ls);
             } else {
                 TypeHint *th = typehint_new(ls);
                 checktypehint(ls, th);
             }
          }
      }

      if (ls->t.token == TK_REQUIRES) {
          luaX_next(ls);

          FuncState *save_fs = ls->fs;
          ls->fs = &new_fs;

          expdesc e;
          expr(ls, &e);

          ls->fs = save_fs;

          /* if not e then error("constraint failed") end */
          int cond = luaK_exp2anyreg(&new_fs, &e);
          luaK_codeABCk(&new_fs, OP_TEST, cond, 0, 0, 1);
          int jmp_skip = luaK_jump(&new_fs);

          expdesc err_func;
          singlevaraux(&new_fs, luaS_newliteral(ls->L, "error"), &err_func, 1);
          if (err_func.k == VVOID) {
             expdesc key;
             singlevaraux(&new_fs, ls->envn, &err_func, 1);
             codestring(&key, luaS_newliteral(ls->L, "error"));
             luaK_indexed(&new_fs, &err_func, &key);
          }
          luaK_exp2nextreg(&new_fs, &err_func);
          int err_reg = err_func.u.info;

          expdesc msg;
          codestring(&msg, luaS_newliteral(ls->L, "generic constraint failed"));
          luaK_exp2nextreg(&new_fs, &msg);

          luaK_codeABC(&new_fs, OP_CALL, err_reg, 2, 1);

          luaK_patchtohere(&new_fs, jmp_skip);
      }

      if (impl_vararg) namedvararg(ls, impl_vararg);
      statlist(ls);

      check_match(ls, TK_END, TK_FUNCTION, line);

      impl_fs.f->lastlinedefined = ls->linenumber;

      /* Close Impl: Generate OP_CLOSURE in Factory */
      /* We need to pass 'e' but 'e' is destination for Factory. */
      /* We need a temp expression for Impl closure */
      expdesc impl_e;
      codeclosure(ls, &impl_e);
      close_func(ls);

      /* Now we are back in Factory */
      /* Factory body: return impl_closure */
      luaK_ret(ls->fs, impl_e.u.info, 1);

      /* Close Factory */
      new_fs.f->lastlinedefined = ls->linenumber;
      codeclosure(ls, e);
      close_func(ls);

      /* Now we are in Parent */
      /* e contains Factory closure */
      /* Generate OP_GENERICWRAP */
      FuncState *fs = ls->fs;
      int factory_reg = luaK_exp2anyreg(fs, e);

      int base_args = fs->freereg;
      luaK_reserveregs(fs, 3);

      int arg1 = base_args;
      int arg2 = base_args + 1;
      int arg3 = base_args + 2;

      /* Arg 1: Factory */
      luaK_codeABC(fs, OP_MOVE, arg1, factory_reg, 0);

      /* Arg 2: Params table */
      int pc_arg2 = luaK_codeABC(fs, OP_NEWTABLE, arg2, 0, 0);
      luaK_code(fs, 0);

      /* Populate Arg 2 with generic param names */
      for (int i = 0; i < ngeneric; i++) {
          int idx = i + (ismethod ? 1 : 0);
          if (idx < new_fs.f->sizelocvars) {
              TString *pname = new_fs.f->locvars[idx].varname;
              if (pname) {
                  expdesc tab; init_exp(&tab, VNONRELOC, arg2);
                  expdesc key; init_exp(&key, VKINT, 0); key.u.ival = i + 1;
                  luaK_indexed(fs, &tab, &key);
                  expdesc val; codestring(&val, pname);
                  luaK_storevar(fs, &tab, &val);
              }
          }
      }
      luaK_settablesize(fs, pc_arg2, arg2, ngeneric, 0);

      /* Arg 3: Mapping table */
      int pc_arg3 = luaK_codeABC(fs, OP_NEWTABLE, arg3, 0, 0);
      luaK_code(fs, 0);

      /* Populate Arg 3 */
      for (int i = 0; i < nmappings; i++) {
          if (mappings[i]) {
              expdesc tab; init_exp(&tab, VNONRELOC, arg3);
              expdesc key; init_exp(&key, VKINT, 0); key.u.ival = i + 1;
              luaK_indexed(fs, &tab, &key);
              expdesc val; codestring(&val, mappings[i]);
              luaK_storevar(fs, &tab, &val);
          }
      }
      luaK_settablesize(fs, pc_arg3, arg3, nmappings, 0);

      luaK_codeABC(fs, OP_GENERICWRAP, base_args, base_args, 0);

      init_exp(e, VNONRELOC, base_args);
      fs->freereg = base_args + 1;
      return;
  }

  if (testnext(ls, ':')) {
      if (testnext(ls, '(')) {
          do {
              TypeHint *th = typehint_new(ls);
              checktypehint(ls, th);
          } while (testnext(ls, ','));
          checknext(ls, ')');
      } else {
          if (ls->t.token == TK_NAME && strcmp(getstr(ls->t.seminfo.ts), "void") == 0) {
              luaX_next(ls);
          } else {
              TypeHint *th = typehint_new(ls);
              checktypehint(ls, th);
          }
      }
  }
  if (ls->t.token == '<') {
      luaX_next(ls);
      if (ls->t.token == TK_NAME) {
         const char *attr = getstr(ls->t.seminfo.ts);
         if (strcmp(attr, "nodiscard") == 0) {
            new_fs.f->nodiscard = 1;
         }
         luaX_next(ls);
      }
      checknext(ls, '>');
  }
  if (varargname) namedvararg(ls, varargname);
  statlist(ls);
  
  check_match(ls, TK_END, TK_FUNCTION, line);

  new_fs.f->lastlinedefined = ls->linenumber;
  codeclosure(ls, e);
  close_func(ls);
}


/**
 * 解析 lambda 表达式的参数列表
 * 支持两种形式:
 *   1. 带括号: (param1, param2, ...)
 *   2. 无括号: param1, param2, ...
 * 
 * 支持参数默认值: param = expr
 * 
 * @param ls 词法分析器状态
 * @param varargname 输出参数，如果存在具名可变参数则存储其名称
 */
static void lambda_parlist(LexState *ls, TString **varargname) {
    /* lambda_parlist -> '(' [ param ['=' expr] { ',' param ['=' expr] } ] ')' */
    /* lambda_parlist -> [ param ['=' expr] { ',' param ['=' expr] } ] */
    if (testnext(ls, '(')) {
        parlist(ls, varargname);
        checknext(ls, ')');
        return;
    }
    FuncState *fs = ls->fs;
    Proto *f = fs->f;
    int nparams = 0;
    f->is_vararg = 0;
    if (is_nametoken(ls->t.token) || ls->t.token == TK_DOTS) {
        do {
            if (is_nametoken(ls->t.token)) {  /* param -> NAME */
                new_localvar(ls, str_checkname(ls));
                /* 立即激活该参数变量并分配寄存器 */
                adjustlocalvars(ls, 1);
                luaK_reserveregs(fs, 1);
                nparams++;
                /* 检查是否有默认值 '=' */
                if (testnext(ls, '=')) {
                    int param_reg = getlocalvardesc(fs, fs->nactvar - 1)->vd.ridx;
                    /* 生成 nil 检查：如果参数不是nil则跳过默认值赋值 */
                    luaK_codeABCk(fs, OP_TESTNIL, param_reg, param_reg, 0, 0);
                    int jmp_skip = luaK_jump(fs);
                    /* 解析默认值表达式 */
                    expdesc default_val;
                    expr(ls, &default_val);
                    luaK_exp2reg(fs, &default_val, param_reg);
                    luaK_patchtohere(fs, jmp_skip);
                }
            }
            else if (ls->t.token == TK_DOTS) {  /* param -> '...' */
                luaX_next(ls);
                f->is_vararg = 1;
                if (varargname && ls->t.token == TK_NAME) {
                   *varargname = ls->t.seminfo.ts;
                   luaX_next(ls);
                }
            }
            else {
                luaX_syntaxerror(ls, "<name> or '...' expected");
            }
        } while (!f->is_vararg && testnext(ls, ','));
    }
    /* 参数已在循环中逐个激活 */
    f->numparams = fs->nactvar;
}


static void lambda_body(LexState *ls, expdesc *e, int line) {
    /* lambda_body -> lambda_parlist ':'|let retstat          -- 表达式体 */
    /* lambda_body -> lambda_parlist '=>' statement            -- 箭头体 */
    /* lambda_body -> lambda_parlist statlist TK_END           -- 块体 */
    FuncState new_fs;
    BlockCnt bl;
    new_fs.f = addprototype(ls);
    new_fs.f->linedefined = line;
    open_func(ls, &new_fs, &bl);
    TString *varargname = NULL;
    lambda_parlist(ls, &varargname);
    if (varargname) namedvararg(ls, varargname);
    if (testnext(ls, TK_LET)||testnext(ls, ':')) {
        /* 表达式体: lambda(x): x * 2 */
        enterlevel(ls);
        retstat(ls);
        lua_assert(ls->fs->f->maxstacksize >= ls->fs->freereg &&
                   ls->fs->freereg >= ls->fs->nactvar);
        ls->fs->freereg = ls->fs->nactvar;  /* free registers */
        leavelevel(ls);
    } else if (testnext(ls, TK_MEAN)) {
        /* 箭头体: lambda(x) => statement */
        statement(ls);
    } else {
        /* 块体: lambda(x) body end */
        statlist(ls);
        check_match(ls, TK_END, TK_LAMBDA, line);
    }
    new_fs.f->lastlinedefined = ls->linenumber;
    codeclosure(ls, e);
    close_func(ls);
}


static int explist (LexState *ls, expdesc *v) {
  /* explist -> expr { ',' expr } */
  int n = 1;  /* at least one expression */
  LUA_LOGD("[PARSER] explist START");
  expr(ls, v);
  LUA_LOGD("[PARSER] explist: first expr done");
  while (testnext(ls, ',')) {
    luaK_exp2nextreg(ls->fs, v);
    expr(ls, v);
    n++;
  }
  LUA_LOGD("[PARSER] explist END, n=%d", n);
  return n;
}


static void funcargs (LexState *ls, expdesc *f, int line) {
  FuncState *fs = ls->fs;
  expdesc args;
  int base, nparams;
  int nodiscard = f->nodiscard;
  switch (ls->t.token) {
    case '(': {  /* funcargs -> '(' [ explist ] ')' */
      luaX_next(ls);
      if (ls->t.token == ')')  /* arg list is empty? */
        args.k = VVOID;
      else {
        TypeHint *f_hint = NULL;
        if (f->k == VLOCAL) {
           f_hint = getlocalvardesc(fs, f->u.var.vidx)->vd.hint;
        }
        
        int n = 0;
        do {
           if (n > 0) {
              luaK_exp2nextreg(ls->fs, &args);
           }
           expr(ls, &args);
           
           if (f_hint) {
              for (int i=0; i<MAX_TYPE_DESCS; i++) {
                 if (f_hint->descs[i].type == LVT_FUNC) {
                    if (n < f_hint->descs[i].nparam)
                       check_type_compatibility(ls, f_hint->descs[i].params[n], &args);
                 }
              }
           }
           n++;
        } while (testnext(ls, ','));
        
        if (hasmultret(args.k))
          luaK_setmultret(fs, &args);
      }
      check_match(ls, ')', '(', line);
      break;
    }
    case '{': {  /* funcargs -> constructor */
      constructor(ls, &args);
      break;
    }
    case TK_STRING:
    case TK_RAWSTRING: {  /* funcargs -> STRING / RAWSTRING */
      codestring(&args, ls->t.seminfo.ts);
      luaX_next(ls);  /* must use 'seminfo' before 'next' */
      break;
    }
    default: {
      luaX_syntaxerror(ls, "function arguments expected");
    }
  }
  lua_assert(f->k == VNONRELOC);
  base = f->u.info;  /* base register for call */
  if (hasmultret(args.k))
    nparams = LUA_MULTRET;  /* open call */
  else {
    if (args.k != VVOID)
      luaK_exp2nextreg(fs, &args);  /* close last argument */
    nparams = fs->freereg - (base+1);
  }
  init_exp(f, VCALL, luaK_codeABC(fs, OP_CALL, base, nparams+1, 2));
  f->nodiscard = nodiscard;
  luaK_fixline(fs, line);
  fs->freereg = base+1;  /* call remove function and arguments and leaves
                            (unless changed) one result */
}




/*
** {===========================================================
** Expression parsing
** ============================================================
*/

static void parse_generic_arrow_body(LexState *ls, FuncState *factory_fs, expdesc *v, int line) {
    /* Generic Arrow Function: (T, U)(args) => ... */
    /* factory_fs is Factory */
    int ngeneric = factory_fs->f->numparams;

    /* Helper array to store type mappings for generics */
    TString *mappings[MAXVARS];
    int nmappings = 0;
    for (int i = 0; i < MAXVARS; i++) mappings[i] = NULL;

    /* Open Impl function */
    FuncState impl_fs;
    BlockCnt impl_bl;
    impl_fs.f = addprototype(ls);
    impl_fs.f->linedefined = line;

    open_func(ls, &impl_fs, &impl_bl);

    /* Parse Impl params */
    checknext(ls, '(');
    TString *impl_vararg = NULL;
    parlist(ls, &impl_vararg);
    checknext(ls, ')');

    /* Capture type hints for mapping */
    nmappings = impl_fs.f->numparams;
    for (int i = 0; i < nmappings && i < MAXVARS; i++) {
        Vardesc *vd = getlocalvardesc(&impl_fs, i);
        if (vd->vd.hint && vd->vd.hint->descs[0].type == LVT_NAME) {
            mappings[i] = vd->vd.hint->descs[0].typename;
        }
    }

    /* Type check injection */
    {
       int i;
       for (i = 0; i < impl_fs.f->numparams; i++) {
          Vardesc *vd = getlocalvardesc(&impl_fs, i);
          if (vd->vd.hint) {
             int j;
             for (j = 0; j < MAX_TYPE_DESCS; j++) {
                if (vd->vd.hint->descs[j].type == LVT_NAME && vd->vd.hint->descs[j].typename) {
                 /* Using OP_CHECKTYPE A B C */
                   expdesc e_val;
                   init_var(&impl_fs, &e_val, i);
                 luaK_exp2anyreg(&impl_fs, &e_val);
                 int val_reg = e_val.u.info;

                   expdesc e_type;
                   singlevaraux(&impl_fs, vd->vd.hint->descs[j].typename, &e_type, 1);
                   if (e_type.k == VVOID) {
                      expdesc key;
                      singlevaraux(&impl_fs, ls->envn, &e_type, 1);
                      codestring(&key, vd->vd.hint->descs[j].typename);
                      luaK_indexed(&impl_fs, &e_type, &key);
                   }
                   luaK_exp2nextreg(&impl_fs, &e_type);
                 int type_reg = e_type.u.info;

                 int name_k = luaK_stringK(&impl_fs, vd->vd.name);

                 luaK_codeABC(&impl_fs, OP_CHECKTYPE, val_reg, type_reg, name_k);

                 impl_fs.freereg = type_reg;
                }
             }
          }
       }
    }

    /* Expect => */
    if (ls->t.token == TK_MEAN) {
        luaX_next(ls);
    } else {
        luaX_syntaxerror(ls, "expected '=>' after generic arrow function parameters");
    }

    if (impl_vararg) namedvararg(ls, impl_vararg);

    /* Parse body */
    if (ls->t.token == '{') {
       luaX_next(ls);
       while (ls->t.token != '}' && ls->t.token != TK_EOS) {

          statement(ls);
       }
       check_match(ls, '}', '{', line);
    } else {
       enterlevel(ls);
       retstat(ls);
       impl_fs.freereg = impl_fs.nactvar;
       leavelevel(ls);
    }

    impl_fs.f->lastlinedefined = ls->linenumber;

    /* Close Impl */
    expdesc impl_e;
    codeclosure(ls, &impl_e);
    close_func(ls);

    /* Back in Factory (factory_fs) */
    /* Return impl_closure */
    luaK_ret(factory_fs, impl_e.u.info, 1);

    factory_fs->f->lastlinedefined = ls->linenumber;

    /* Close Factory */
    /* We need to save generic parameter names before closing */
    TString *generic_names[MAXVARS];
    for(int i=0; i<ngeneric; i++) generic_names[i] = NULL;

    for (int i = 0; i < ngeneric; i++) {
       if (i < factory_fs->f->sizelocvars) {
           generic_names[i] = factory_fs->f->locvars[i].varname;
       }
    }

    expdesc factory_e;
    codeclosure(ls, &factory_e);

    close_func(ls);
    /* Now ls->fs is Parent */

    /* Generate OP_GENERICWRAP */
    FuncState *fs = ls->fs;
    int factory_reg = luaK_exp2anyreg(fs, &factory_e);

    int base_args = fs->freereg;
    luaK_reserveregs(fs, 3);

    int arg1 = base_args;
    int arg2 = base_args + 1;
    int arg3 = base_args + 2;

    /* Arg 1: Factory */
    luaK_codeABC(fs, OP_MOVE, arg1, factory_reg, 0);

    /* Arg 2: Params table */
    int pc_arg2 = luaK_codeABC(fs, OP_NEWTABLE, arg2, 0, 0);
    luaK_code(fs, 0);

    /* Populate Arg 2 using saved names */
    for (int i = 0; i < ngeneric; i++) {
        if (generic_names[i]) {
            expdesc tab; init_exp(&tab, VNONRELOC, arg2);
            expdesc key; init_exp(&key, VKINT, 0); key.u.ival = i + 1;
            luaK_indexed(fs, &tab, &key);
            expdesc val; codestring(&val, generic_names[i]);
            luaK_storevar(fs, &tab, &val);
        }
    }
    luaK_settablesize(fs, pc_arg2, arg2, ngeneric, 0);

    /* Arg 3: Mapping table */
    int pc_arg3 = luaK_codeABC(fs, OP_NEWTABLE, arg3, 0, 0);
    luaK_code(fs, 0);

    /* Populate Arg 3 */
    for (int i = 0; i < nmappings; i++) {
        if (mappings[i]) {
            expdesc tab; init_exp(&tab, VNONRELOC, arg3);
            expdesc key; init_exp(&key, VKINT, 0); key.u.ival = i + 1;
            luaK_indexed(fs, &tab, &key);
            expdesc val; codestring(&val, mappings[i]);
            luaK_storevar(fs, &tab, &val);
        }
    }
    luaK_settablesize(fs, pc_arg3, arg3, nmappings, 0);

    luaK_codeABC(fs, OP_GENERICWRAP, base_args, base_args, 0);

    init_exp(v, VNONRELOC, base_args);
    fs->freereg = base_args + 1;
}

static void primaryexp (LexState *ls, expdesc *v) {
  /* primaryexp -> NAME | '(' expr ')' | STRING | constructor | NEW | SUPER */
  LUA_LOGD("[PARSER] primaryexp entry token=%d line=%d", ls->t.token, ls->linenumber);
  switch (ls->t.token) {
    case '(': {
      int line = ls->linenumber;

      /*
      ** Arrow Function Detection
      ** Case 1: ( ... ) => ...  (Empty or Multi-param or Single-param with comma)
      ** We check this BEFORE consuming '('.
      */
      int is_arrow = 0;
      int la1 = luaX_lookahead(ls);

      /* Case: () => */
      if (la1 == ')') {
         if (luaX_lookahead2(ls) == TK_MEAN) {
            is_arrow = 1;
         }
      }
      /* Case: (name, ...) => — 仅当不在 infix 参数位置时检测，否则会误把函数调用参数当箭头 */
      else if ((la1 == TK_NAME || la1 == TK_DOTS || is_type_token(la1)) && luaX_lookahead2(ls) == ',' &&
               !(ls->expr_flags & E_INFIX_ARG)) {
         is_arrow = 1;
      }

      if (is_arrow) {
         luaX_next(ls); /* skip '(' */

         /* Parse parameters manually */
         FuncState new_fs;
         BlockCnt bl;
         new_fs.f = addprototype(ls);
         new_fs.f->linedefined = line;
         open_func(ls, &new_fs, &bl);

         TString *varargname = NULL;
         int nparams = 0;
         if (ls->t.token != ')') {
             do {
                if (is_nametoken(ls->t.token)) {
                   new_localvar(ls, str_checkname(ls));
                   nparams++;
                } else if (ls->t.token == TK_DOTS) {
                   luaX_next(ls);
                   new_fs.f->is_vararg = 1;
                   if (ls->t.token == TK_NAME) {
                      varargname = ls->t.seminfo.ts;
                      luaX_next(ls);
                   }
                } else {
                   luaX_syntaxerror(ls, "<name> or '...' expected in arrow function args");
                }
             } while (!new_fs.f->is_vararg && testnext(ls, ','));
         }

         adjustlocalvars(ls, nparams);
         new_fs.f->numparams = new_fs.nactvar;
         if (new_fs.f->is_vararg)
            setvararg(&new_fs, new_fs.f->numparams);
         luaK_reserveregs(&new_fs, new_fs.nactvar);

         checknext(ls, ')');

         /* Expect => or ( for Generic Arrow */
         if (ls->t.token == TK_MEAN) {
            luaX_next(ls); /* skip => */

            if (varargname) namedvararg(ls, varargname);

            if (ls->t.token == '{') {
               statement(ls);
            } else {
               enterlevel(ls);
               retstat(ls);
               new_fs.freereg = new_fs.nactvar;
               leavelevel(ls);
            }

            new_fs.f->lastlinedefined = ls->linenumber;
            codeclosure(ls, v);
            close_func(ls);
            return;
         } else if (ls->t.token == '(') {
            parse_generic_arrow_body(ls, &new_fs, v, line);
            return;
         } else {
            luaX_syntaxerror(ls, "expected '=>' after arrow function parameters");
         }
      }

      /*
      ** Standard '(' case, but we need to check for `(name) =>` and `(...) =>`
      ** which require peeking 3 tokens deep: ( name ) =>
      ** We do this AFTER consuming '(' so we can use lookahead2 to see '=>'.
      */
      int old_flags = ls->expr_flags;
      ls->expr_flags = 0;
      luaX_next(ls); /* skip '(' */

      /* Check for (name) => or (...) => */
      if ((is_nametoken(ls->t.token) || ls->t.token == TK_DOTS) &&
          luaX_lookahead(ls) == ')' &&
          luaX_lookahead2(ls) == TK_MEAN) {

          /* It is a single-param arrow function! */
          TString *param_name = NULL;
          int is_vararg = 0;

          if (is_nametoken(ls->t.token)) {
             param_name = ls->t.seminfo.ts;
          } else {
             is_vararg = 1;
          }
          luaX_next(ls); /* consume name/... */
          luaX_next(ls); /* consume ) */
          luaX_next(ls); /* consume => */

          FuncState new_fs;
          BlockCnt bl;
          new_fs.f = addprototype(ls);
          new_fs.f->linedefined = line;
          open_func(ls, &new_fs, &bl);

          if (is_vararg) {
             new_fs.f->is_vararg = 1;
             setvararg(&new_fs, 0);
          } else {
             new_localvar(ls, param_name);
             adjustlocalvars(ls, 1);
             new_fs.f->numparams = 1;
             luaK_reserveregs(&new_fs, 1);
          }

          if (ls->t.token == '{') {
             statement(ls);
          } else {
             enterlevel(ls);
             retstat(ls);
             new_fs.freereg = new_fs.nactvar;
             leavelevel(ls);
          }

          new_fs.f->lastlinedefined = ls->linenumber;
          codeclosure(ls, v);
          close_func(ls);
          return;
      }

      /* 检查是否是海象操作符: (name := expr) */
      if (ls->t.token == TK_NAME && luaX_lookahead(ls) == TK_WALRUS) {
          TString *varname = ls->t.seminfo.ts;
          int save = ls->linenumber;
          luaX_next(ls);  /* skip NAME */
          luaX_next(ls);  /* skip := */
          expdesc e;
          expr(ls, &e);
          ls->expr_flags = old_flags;
          check_match(ls, ')', '(', save);
          /* 查找变量并存储 */
          singlevaraux(ls->fs, varname, v, 0);
          if (v->k == VVOID) {
            expdesc key;
            singlevaraux(ls->fs, ls->envn, v, 1);
            codestring(&key, varname);
            luaK_indexed(ls->fs, v, &key);
          }
          luaK_storevar(ls->fs, v, &e);
          luaK_exp2nextreg(ls->fs, &e);
          init_exp(v, VNONRELOC, e.u.info);
          return;
      }

      /* 在 infix 参数位置，(arg...) 应被解析为函数调用参数列表，而非单个括号表达式 */
      if (old_flags & E_INFIX_ARG) {
        if (ls->t.token == ')')
          v->k = VVOID;
        else
          explist(ls, v);
      } else {
        expr(ls, v);
      }
      ls->expr_flags = old_flags;
      check_match(ls, ')', '(', line);
      luaK_dischargevars(ls->fs, v);
      return;
    }
    case TK_NAME: {
      /* 使用软关键字系统检查 match 表达式 */
      LUA_LOGD("[PARSER] primaryexp TK_NAME: name='%s'", getstr(ls->t.seminfo.ts));
    LUA_LOGD("[PARSER] primaryexp: calling softkw_test MATCH");
    if (softkw_test(ls, SKW_MATCH, SOFTKW_CTX_EXPR)) {
        LUA_LOGD("[PARSER] primaryexp: softkw MATCH");
        matchexpr(ls, v);
        return;
      }
      LUA_LOGD("[PARSER] primaryexp: softkw_test MATCH returned 0");
    /* 使用软关键字系统检查 new */
    LUA_LOGD("[PARSER] primaryexp: calling softkw_test NEW");
    if (softkw_test(ls, SKW_NEW, SOFTKW_CTX_EXPR)) {
        /* onew ClassName(args...) - 创建类实例 */
        LUA_LOGD("[PARSER] primaryexp: softkw NEW");
        newexpr(ls, v);
        return;
      }
      LUA_LOGD("[PARSER] primaryexp: softkw_test NEW returned 0");
    /* 使用软关键字系统检查 osuper（需要前瞻 . 或 :） */
    LUA_LOGD("[PARSER] primaryexp: calling softkw_test SUPER");
    if (softkw_test(ls, SKW_SUPER, SOFTKW_CTX_EXPR)) {
        /* osuper.method 或 osuper:method - 调用父类方法 */
        LUA_LOGD("[PARSER] primaryexp: softkw SUPER");
        /* Check if 'self' exists in scope before treating as keyword */
        expdesc self_exp;
        TString *self_name = luaS_newliteral(ls->L, "self");
        singlevaraux(ls->fs, self_name, &self_exp, 1);
        LUA_LOGD("[PARSER] primaryexp: SUPER self checked, k=%d", self_exp.k);

        if (self_exp.k != VVOID) {
           superexpr(ls, v);
           return;
        }
      }
      LUA_LOGD("[PARSER] primaryexp: softkw_test SUPER returned 0");
    /* 普通标识符 */
    LUA_LOGD("[PARSER] primaryexp: calling singlevar for name='%s'", getstr(ls->t.seminfo.ts));
    singlevar(ls, v);
      LUA_LOGD("[PARSER] primaryexp: singlevar DONE, v->k=%d", v->k);
      return;
    }
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
      singlevar(ls, v);
      return;
    }
    case TK_STRING:
    case TK_RAWSTRING: {
      codestring(v, ls->t.seminfo.ts);
      luaX_next(ls);
      return;
    }
    case TK_REGEX: {  /* 正则字面量 /pattern/flags */
      FuncState *fs = ls->fs;
      TString *ts = ls->t.seminfo.ts;
      luaX_next(ls);
      int kidx = luaK_stringK(fs, ts);
      init_exp(v, VRELOC, luaK_codeABx(fs, OP_REGEX, 0, kidx));
      return;
    }
    case '{': {
      constructor(ls, v);
      return;
    }
    case '[': {
      /* map字面量: [key = val, ...] */
      mapconstructor(ls, v);
      return;
    }
    case TK_DOLLAR: {
      FuncState *fs = ls->fs;
      int line = ls->linenumber;
      TString *kwname;
      expdesc keywords_table, key_exp;
      
      luaX_next(ls);  /* Skip '$' */
      if (!is_nametoken(ls->t.token))
        error_expected(ls, TK_NAME);
      kwname = ls->t.seminfo.ts;
      
      if (strcmp(getstr(kwname), "embed") == 0) {
         luaX_next(ls); /* skip embed */
         if (ls->t.token != TK_STRING && ls->t.token != TK_RAWSTRING) {
             luaX_syntaxerror(ls, "expected string literal after $embed");
         }
         const char *filename = getstr(ls->t.seminfo.ts);
         FILE *f = fopen(filename, "rb");
         if (!f) {
             luaX_syntaxerror(ls, luaO_pushfstring(ls->L, "cannot open file '%s' for $embed", filename));
         }
         fseek(f, 0, SEEK_END);
         long size = ftell(f);
         fseek(f, 0, SEEK_SET);
         char *buf = luaM_newvector(ls->L, size + 1, char);
         if (size > 0 && fread(buf, 1, size, f) != (size_t)size) {
             fclose(f);
             luaM_freearray(ls->L, buf, size + 1);
             luaX_syntaxerror(ls, "failed to read file for $embed");
         }
         fclose(f);
         buf[size] = '\0';
         TString *ts = luaS_newlstr(ls->L, buf, size);
         luaM_freearray(ls->L, buf, size + 1);
         codestring(v, ts);
         luaX_next(ls); /* skip string */
         return;
      }

      if (strcmp(getstr(kwname), "object") == 0) {
        luaX_next(ls); /* skip 'object' */
        checknext(ls, '(');

        /* create new table */
        int pc = luaK_codeABC(fs, OP_NEWTABLE, 0, 0, 0);
        ConsControl cc;
        luaK_code(fs, 0);  /* space for extra arg. */
        cc.na = cc.nh = cc.tostore = 0;
        cc.t = v;
        init_exp(v, VNONRELOC, fs->freereg);  /* table will be at stack top */
        luaK_reserveregs(fs, 1);
        init_exp(&cc.v, VVOID, 0);  /* no value (yet) */

        while (ls->t.token != ')') {
            TString *varname = str_checkname(ls);
            expdesc key, val;

            codestring(&key, varname);
            singlevaraux(fs, varname, &val, 1);
            if (val.k == VVOID) { /* global? */
                expdesc k;
                singlevaraux(fs, ls->envn, &val, 1);
                codestring(&k, varname);
                luaK_indexed(fs, &val, &k);
            }

            cc.nh++;

            /* t[key] = val */
            expdesc tab = *cc.t;
            luaK_indexed(fs, &tab, &key);
            luaK_storevar(fs, &tab, &val);

            if (ls->t.token == ',') luaX_next(ls);
            else break;
        }
        checknext(ls, ')');
        luaK_settablesize(fs, pc, v->u.info, cc.na, cc.nh);
        return;
      }

      /* $name(args) → 从 keyword 编译时注册表查找 Proto 直接创建 closure */
      /* 无需运行时 _KEYWORDS 表查询 */

      luaX_next(ls);  /* Skip name */

      /* 从 keyword 编译时注册表查找 Proto */
      {
        Proto *kwproto = keyword_lookup(ls, kwname);
        if (kwproto != NULL) {
          /* keyword 必须是纯函数(无upvalue)，已在 keywordstat 中校验 */
          /* 直接将 keyword proto 加入当前函数子原型列表 */
          Proto *f = ls->fs->f;
          if (ls->fs->np >= f->sizep) {
            int oldsize = f->sizep;
            luaM_growvector(ls->L, f->p, ls->fs->np, f->sizep, Proto *, MAXARG_Bx, "functions");
            while (oldsize < f->sizep)
              f->p[oldsize++] = NULL;
          }
          int proto_idx = ls->fs->np++;
          f->p[proto_idx] = kwproto;
          luaC_objbarrier(ls->L, f, kwproto);
          /* 生成 OP_CLOSURE 指令，结果写入当前 freereg 寄存器 */
          int reg = fs->freereg;
          luaK_codeABx(fs, OP_CLOSURE, reg, proto_idx);
          /* 使用 VNONRELOC 确保 funcargs 能正确获取寄存器 */
          init_exp(v, VNONRELOC, reg);
          fs->freereg = reg + 1;
          return;
        } else {
          /* keyword 未找到，给出友好的编译时错误 */
          luaX_syntaxerror(ls, luaO_pushfstring(ls->L,
            "keyword '$%s' not found (did you forget 'keyword %s(...) end'?)",
            getstr(kwname), getstr(kwname)));
        }
      }
    }
    case TK_DOLLDOLL: {
      /**
       * 运算符调用语法: $$<运算符>(args)
       * 等价于: _OPERATORS["<运算符>"](args)
       * 
       * 用于调用 operator 关键字定义的自定义运算符
       * 示例: $$++(a) 调用 _OPERATORS["++"](a)
       *       $$^(a, b) 调用 _OPERATORS["^"](a, b)
       */
      FuncState *fs = ls->fs;
      TString *opname = NULL;
      const char *opstr = NULL;
      expdesc operators_table, key_exp;
      
      luaX_next(ls);  /* 跳过 '$$' */
      
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
          luaX_syntaxerror(ls, "expected operator symbol after '$$'");
      }
      
      if (opstr != NULL) {
        opname = luaS_new(ls->L, opstr);
      }
      
      luaX_next(ls);  /* 跳过运算符符号 */
      
      /* 获取 _OPERATORS 表 (via opcode) */
      init_exp(&operators_table, VNONRELOC, fs->freereg);
      luaK_codeABC(fs, OP_GETOPS, fs->freereg, 0, 0);
      luaK_reserveregs(fs, 1);
      
      /* 获取 _OPERATORS[运算符] */
      luaK_exp2anyreg(fs, &operators_table);
      codestring(&key_exp, opname);
      luaK_indexed(fs, &operators_table, &key_exp);
      
      /* 返回函数表达式，让 suffixedexp 继续处理后续的函数调用 */
      *v = operators_table;
      return;
    }
    case '@': {  /* || -> 无参lambda 表达式 */
      int line = ls->linenumber;
      luaX_next(ls);  /* 跳过 '@' (即 ||) */

      /* 期望 -> */
      if (!testnext(ls, TK_ARROW)) {
        luaX_syntaxerror(ls, "expected '->' after '||' in lambda expression");
      }

      /* 创建闭包 */
      FuncState new_fs;
      BlockCnt bl;
      new_fs.f = addprototype(ls);
      new_fs.f->linedefined = line;
      open_func(ls, &new_fs, &bl);
      new_fs.f->numparams = 0;
      new_fs.f->is_vararg = 0;

      /* 解析函数体 */
      if (ls->t.token == '{') {
        statement(ls);
      } else {
        enterlevel(ls);
        retstat(ls);
        new_fs.freereg = new_fs.nactvar;
        leavelevel(ls);
      }

      new_fs.f->lastlinedefined = ls->linenumber;
      codeclosure(ls, v);
      close_func(ls);
      return;
    }
    case '|': {  /* |params| -> lambda 表达式 */
      int line = ls->linenumber;
      luaX_next(ls);  /* 跳过第一个 '|' */

      /* 创建闭包 */
      FuncState new_fs;
      BlockCnt bl;
      new_fs.f = addprototype(ls);
      new_fs.f->linedefined = line;
      open_func(ls, &new_fs, &bl);

      TString *varargname = NULL;
      int nparams = 0;

      /* 解析参数列表 */
      if (ls->t.token != '|') {
        do {
          if (is_nametoken(ls->t.token)) {
            new_localvar(ls, str_checkname(ls));
            nparams++;
          } else if (ls->t.token == TK_DOTS) {
            luaX_next(ls);
            new_fs.f->is_vararg = 1;
            if (ls->t.token == TK_NAME) {
              varargname = ls->t.seminfo.ts;
              luaX_next(ls);
            }
          } else {
            luaX_syntaxerror(ls, "<name> or '...' expected in lambda parameter list");
          }
        } while (!new_fs.f->is_vararg && testnext(ls, ','));
      }

      adjustlocalvars(ls, nparams);
      new_fs.f->numparams = new_fs.nactvar;
      if (new_fs.f->is_vararg)
        setvararg(&new_fs, new_fs.f->numparams);
      luaK_reserveregs(&new_fs, new_fs.nactvar);
      if (varargname) namedvararg(ls, varargname);

      /* 期望闭合的 '|' */
      checknext(ls, '|');

      /* 期望 -> */
      if (!testnext(ls, TK_ARROW)) {
        luaX_syntaxerror(ls, "expected '->' after parameter list in lambda expression");
      }

      /* 解析函数体 */
      if (ls->t.token == '{') {
        statement(ls);
      } else {
        enterlevel(ls);
        retstat(ls);
        new_fs.freereg = new_fs.nactvar;
        leavelevel(ls);
      }

      new_fs.f->lastlinedefined = ls->linenumber;
      codeclosure(ls, v);
      close_func(ls);
      return;
    }
    case TK_ASTPARSER: {
      /* astparser 作为表达式使用：解析 AST 并返回 callable 闭包 */
      int reg = astparserstat(ls, ls->linenumber);
      init_exp(v, VNONRELOC, reg);
      return;
    }
    default: {
      luaX_syntaxerror(ls, "unexpected symbol");
    }
  }
}


/*
** 解析管道右侧的函数表达式
** 只解析主表达式和字段访问（. []），不递归处理管道运算符
** 确保管道运算符是左关联的
** 
** @param ls 词法状态
** @param v 输出表达式描述符
*/
static void pipe_funcexp (LexState *ls, expdesc *v) {
  FuncState *fs = ls->fs;
  /* 先解析主表达式 */
  primaryexp(ls, v);
  /* 只处理字段访问，不处理管道 */
  for (;;) {
    switch (ls->t.token) {
      case '.':
      case TK_DBCOLON: {  /* fieldsel or label */
        /* 检测 ::name:: 标签模式：若 :: 后跟 NAME 再跟 ::，则是 label 而非字段访问
           区分链式命名空间 (obj::ns::member) 需要 lookahead 三步：
           - label: ::NAME::  → 第二个 :: 之后不是 NAME（通常是另一个 statement）
           - 命名空间: obj::ns::member → 第二个 :: 之后还是 NAME
           关键：lookahead3 如果在不同行，一定是 label（跨行不可能是命名空间链）*/
        int la = luaX_lookahead(ls);
        if (la == TK_NAME) {
          int la2 = luaX_lookahead2(ls);
          if (la2 == TK_DBCOLON) {
            int la3 = luaX_lookahead3(ls);
            /* 跨行一定是 label */
            if (la3 != TK_NAME || ls->lookahead3.linenumber != ls->lookahead2.linenumber) {
              /* 这是 label ::name::，停止 suffixedexp 循环，交由 statement() 处理 */
              return;
            }
          }
        }
        fieldsel(ls, v);
        break;
      }
      case '[': {  /* '[' exp ']' 或切片语法 '[' start:end:step ']' */
        yindex_or_slice(ls, v);
        break;
      }
      default: return;  /* 遇到其他 token 就停止 */
    }
  }
}


static void suffixedexp (LexState *ls, expdesc *v) {
  /* suffixedexp ->
       primaryexp { '.' NAME | '?.' NAME | '[' exp ']' | ':' NAME funcargs | funcargs | '|>' suffixedexp } */
  FuncState *fs = ls->fs;
  int line = ls->linenumber;
  int opt_jumps = NO_JUMP;
  primaryexp(ls, v);
  LUA_LOGD("[PARSER] suffixedexp: primaryexp DONE, v->k=%d token=%d", v->k, ls->t.token);
  for (;;) {
    LUA_LOGD("[PARSER] suffixedexp LOOP: token=%d line=%d", ls->t.token, ls->linenumber);
    switch (ls->t.token) {
      case TK_OPTCHAIN: {  /* '?.' 可选链字段访问 */
        expdesc key;
        int reg;
        int jmp_skip;
        int idx;
        
        /* 将表达式转换为寄存器 */
        luaK_dischargevars(fs, v);
        luaK_exp2nextreg(fs, v); reg = v->u.info;
        
        /* 生成 TESTNIL 指令：k=1 表示非nil时跳过下一条JMP */
        luaK_codeABCk(fs, OP_TESTNIL, reg, reg, 0, 1);
        /* 生成跳转指令：是 nil 时执行此JMP跳过后续字段访问 */
        jmp_skip = luaK_jump(fs);
        
        /* 累积短路跳转，在最后才修复 */
        luaK_concat(fs, &opt_jumps, jmp_skip);

        /* 不是 nil，进行正常字段访问 */
        luaX_next(ls);  /* 跳过 '?.' */
        
        if (ls->t.token == '(') {
            /* 可选链调用: obj?.() */
            v->k = VNONRELOC;
            v->u.info = reg;
            v->t = NO_JUMP;
            v->f = NO_JUMP;
            luaK_exp2nextreg(fs, v);
            funcargs(ls, v, line);
            break;
        }

        /* 允许关键字作为字段名 */
        if (ls->t.token == TK_NAME) {
          codename(ls, &key);
        }
        else {
          /* 处理关键字作为字段名：所有保留字的seminfo.ts已由词法分析器设置 */
          TString *ts = ls->t.seminfo.ts;
          if (ts == NULL) {
            error_expected(ls, TK_NAME);
          }
          codestring(&key, ts);
          luaX_next(ls);
        }
        
        /* 使用 luaK_indexed 获取字段名常量索引，但不释放寄存器 */
        /* 临时设置 v 的状态用于 luaK_indexed */
        v->k = VNONRELOC;
        v->u.info = reg;
        luaK_indexed(fs, v, &key);  /* v 变成 VINDEXSTR，v->u.ind.idx 是常量索引 */
        idx = v->u.ind.idx;
        
        /* 手动生成 GETFIELD 指令，结果存入 reg（覆盖原表的位置） */
        luaK_codeABC(fs, OP_GETFIELD, reg, reg, idx);
        
        /* 重置表达式状态为 VNONRELOC，值在 reg 中，清除跳转列表 */
        v->k = VNONRELOC;
        v->u.info = reg;
        v->t = NO_JUMP;
        v->f = NO_JUMP;
        break;
      }
      case '.':
      case TK_DBCOLON: {  /* fieldsel or label */
        /* 检测 ::name:: 标签模式：若 :: 后跟 NAME 再跟 ::，则是 label 而非字段访问
           区分链式命名空间 (obj::ns::member) 需要 lookahead 三步：
           - label: ::NAME::  → 第二个 :: 之后不是 NAME（通常是另一个 statement）
           - 命名空间: obj::ns::member → 第二个 :: 之后还是 NAME
           关键：lookahead3 如果在不同行，一定是 label（跨行不可能是命名空间链）*/
        int la = luaX_lookahead(ls);
        if (la == TK_NAME) {
          int la2 = luaX_lookahead2(ls);
          if (la2 == TK_DBCOLON) {
            int la3 = luaX_lookahead3(ls);
            /* 跨行一定是 label */
            if (la3 != TK_NAME || ls->lookahead3.linenumber != ls->lookahead2.linenumber) {
              /* 这是 label ::name::，停止 suffixedexp 循环，交由 statement() 处理 */
              return;
            }
          }
        }
        fieldsel(ls, v);
        break;
      }
      case '[': {  /* '[' exp ']' 或切片语法 '[' start:end:step ']' */
        yindex_or_slice(ls, v);
        break;
      }
      case ':': {  /* ':' NAME funcargs */
        if (ls->expr_flags & E_NO_COLON) {
           int next = luaX_lookahead(ls);
           if (next == TK_NAME) {
               int next2 = luaX_lookahead2(ls);
               if (next2 == '(' || next2 == '{' || next2 == TK_STRING || next2 == TK_INTERPSTRING || next2 == TK_RAWSTRING) {
                   /* It IS a method call, proceed */
               } else {
                   return;
               }
           } else {
               return;
           }
        }
        expdesc key;
        luaX_next(ls);
        codename(ls, &key);
        luaK_self(fs, v, &key);
        funcargs(ls, v, line);
        break;
      }
      case '(': case TK_STRING: case TK_RAWSTRING: case '{': {  /* funcargs */
        luaK_exp2nextreg(fs, v);
        funcargs(ls, v, line);
        break;
      }
      /* 注意：'|>' 管道不在此处处理。suffixedexp 不感知 subexpr 的
         优先级上下文，若在此消费管道会破坏链式管道的左结合性
         （如 a |> f |> g 会被解析成 a |> (f |> g)）。
         管道统一由 subexpr 二元运算符路径处理（getbinopr/OPR_PIPE）。 */
      case TK_REVPIPE: {  /* '<|' 反向管道 */
        luaX_next(ls);
        expdesc e;
        /* 支持反向管道右侧直接使用字面量和匿名函数 */
        switch (ls->t.token) {
          case TK_FUNCTION: {  /* 匿名函数 */
            body(ls, &e, 0, ls->linenumber);
            break;
          }
          case TK_LAMBDA: {  /* lambda表达式 */
            lambda_body(ls, &e, ls->linenumber);
            break;
          }
          case TK_INT: {  /* 整数常量 */
            init_exp(&e, VKINT, 0);
            e.u.ival = ls->t.seminfo.i;
            luaX_next(ls);
            break;
          }
          case TK_FLT: {  /* 浮点数常量 */
            init_exp(&e, VKFLT, 0);
            e.u.nval = ls->t.seminfo.r;
            luaX_next(ls);
            break;
          }
          case TK_STRING:
          case TK_RAWSTRING: {  /* 字符串常量 */
            codestring(&e, ls->t.seminfo.ts);
            luaX_next(ls);
            break;
          }
          case TK_TRUE: {  /* true常量 */
            init_exp(&e, VTRUE, 0);
            luaX_next(ls);
            break;
          }
          case TK_FALSE: {  /* false常量 */
            init_exp(&e, VFALSE, 0);
            luaX_next(ls);
            break;
          }
          case TK_NIL: {  /* nil常量 */
            init_exp(&e, VNIL, 0);
            luaX_next(ls);
            break;
          }
          case '{': {  /* 表常量作为参数 */
            constructor(ls, &e);
            break;
          }
          default: {
            /* 解析函数表达式（不递归处理管道，确保左关联） */
            pipe_funcexp(ls, &e);
            break;
          }
        }
        /* 生成反向管道运算符代码：v 是函数，e 是参数 */
        luaK_revpipe(fs, v, &e);
        break;
      }
      case TK_SAFEPIPE: {  /* '|?>' 安全管道 */
        /*
        ** 安全管道运算符: x |?> f
        ** 功能描述：如果 x 为 nil，则结果为 nil；否则结果为 f(x)
        ** 用于避免 nil 值导致的错误
        */
        luaX_next(ls);
        expdesc e;
        /* 支持管道符右侧直接使用字面量和匿名函数 */
        switch (ls->t.token) {
          case TK_FUNCTION: {  /* 匿名函数 */
            body(ls, &e, 0, ls->linenumber);
            break;
          }
          case TK_LAMBDA: {  /* lambda表达式 */
            lambda_body(ls, &e, ls->linenumber);
            break;
          }
          case TK_INT: {  /* 整数常量 */
            init_exp(&e, VKINT, 0);
            e.u.ival = ls->t.seminfo.i;
            luaX_next(ls);
            break;
          }
          case TK_FLT: {  /* 浮点数常量 */
            init_exp(&e, VKFLT, 0);
            e.u.nval = ls->t.seminfo.r;
            luaX_next(ls);
            break;
          }
          case TK_STRING:
          case TK_RAWSTRING: {  /* 字符串常量 */
            codestring(&e, ls->t.seminfo.ts);
            luaX_next(ls);
            break;
          }
          case TK_TRUE: {  /* true常量 */
            init_exp(&e, VTRUE, 0);
            luaX_next(ls);
            break;
          }
          case TK_FALSE: {  /* false常量 */
            init_exp(&e, VFALSE, 0);
            luaX_next(ls);
            break;
          }
          case TK_NIL: {  /* nil常量 */
            init_exp(&e, VNIL, 0);
            luaX_next(ls);
            break;
          }
          case '{': {  /* 表常量作为函数 */
            constructor(ls, &e);
            break;
          }
          default: {
            /* 解析函数表达式（不递归处理管道，确保左关联） */
            pipe_funcexp(ls, &e);
            break;
          }
        }
        /* 生成安全管道运算符代码 */
        luaK_safepipe(fs, v, &e);
        break;
      }

      default: goto end_loop;
    }
  }

end_loop:
  if (opt_jumps != NO_JUMP) {
    luaK_patchtohere(fs, opt_jumps);
  }
}


static void ifexpr (LexState *ls, expdesc *v) {
  FuncState *fs = ls->fs;
  int condition;
  int escape = NO_JUMP;
  int reg;

  luaX_next(ls); /* skip IF */
  condition = cond(ls);
  checknext(ls, TK_THEN);
  expr(ls, v);
  luaK_exp2nextreg(fs, v);
  reg = v->u.info;
  luaK_concat(fs, &escape, luaK_jump(fs));
  luaK_patchtohere(fs, condition);

  while (ls->t.token == TK_ELSEIF) {
    luaX_next(ls); /* skip ELSEIF */
    condition = cond(ls);
    checknext(ls, TK_THEN);
    expr(ls, v);
    luaK_exp2reg(fs, v, reg);
    luaK_concat(fs, &escape, luaK_jump(fs));
    luaK_patchtohere(fs, condition);
  }

  checknext(ls, TK_ELSE);
  expr(ls, v);
  checknext(ls, TK_END);
  luaK_exp2reg(fs, v, reg);
  luaK_patchtohere(fs, escape);
}

/**
 * 解析测试表达式的值部分，支持链式操作符
 * 处理: [ expr ], [ expr -op expr ], [ expr = expr ], [ cond1 -a cond2 ], [ cond1 -o cond2 ]
 * -a 优先级高于 -o: -a 的右操作数不包含 -o 链
 * @param allow_or 是否允许解析 -o 操作符（-a 递归调用时传 0）
 */
static void parse_test_value (LexState *ls, expdesc *v, int line, int allow_or) {
  FuncState *fs = ls->fs;
  int single_value = 1;  /* 标记是否为单值表达式（无操作符） */

  /* 解析第一个值表达式 */
  simpleexp(ls, v);

  /*
   * 内层循环: 处理比较操作符和 -a (AND)
   * -a 优先级高于 -o，-a 的右操作数递归调用时不包含 -o
   */
  for (;;) {
    /* 字符串比较: =, ==, != */
    if (ls->t.token == '=') {
      single_value = 0;
      luaX_next(ls);
      expdesc e2;
      simpleexp(ls, &e2);
      luaK_infix(fs, OPR_EQ, v);
      luaK_posfix(fs, OPR_EQ, v, &e2, line);
      continue;
    }
    if (ls->t.token == TK_EQ) {
      single_value = 0;
      luaX_next(ls);
      expdesc e2;
      simpleexp(ls, &e2);
      luaK_infix(fs, OPR_EQ, v);
      luaK_posfix(fs, OPR_EQ, v, &e2, line);
      continue;
    }
    if (ls->t.token == TK_NE) {
      single_value = 0;
      luaX_next(ls);
      expdesc e2;
      simpleexp(ls, &e2);
      luaK_infix(fs, OPR_NE, v);
      luaK_posfix(fs, OPR_NE, v, &e2, line);
      continue;
    }

    /* 二元比较操作符: -eq, -ne, -gt, -lt, -ge, -le */
    /* 逻辑操作符: -a (AND) */
    if (ls->t.token == '-') {
      /* -o 始终留给外层循环处理（优先级最低） */
      if (luaX_lookahead(ls) == TK_NAME &&
          strcmp(getstr(ls->lookahead.seminfo.ts), "o") == 0) {
        break;
      }
      luaX_next(ls);
      if (ls->t.token == TK_NAME || ls->t.token == TK_NIL || ls->t.token == TK_BOOL) {
        const char *opname = getstr(ls->t.seminfo.ts);
        luaX_next(ls);  /* 跳过操作符名 */

        BinOpr binop = OPR_NOBINOPR;
        if (strcmp(opname, "eq") == 0) binop = OPR_EQ;
        else if (strcmp(opname, "ne") == 0) binop = OPR_NE;
        else if (strcmp(opname, "gt") == 0) binop = OPR_GT;
        else if (strcmp(opname, "lt") == 0) binop = OPR_LT;
        else if (strcmp(opname, "ge") == 0) binop = OPR_GE;
        else if (strcmp(opname, "le") == 0) binop = OPR_LE;

        if (binop != OPR_NOBINOPR) {
          single_value = 0;
          expdesc e2;
          simpleexp(ls, &e2);
          luaK_infix(fs, binop, v);
          luaK_posfix(fs, binop, v, &e2, line);
          continue;
        }

        /* 逻辑操作符 -a (AND): 右操作数递归解析，传递 allow_or=0 */
        /* 先调用 infix 将 v 的 false 跳转 patch 到右操作数之前 */
        if (strcmp(opname, "a") == 0) {
          single_value = 0;
          expdesc e2;
          luaK_infix(fs, OPR_AND, v);
          parse_test_value(ls, &e2, line, 0);
          luaK_posfix(fs, OPR_AND, v, &e2, line);
          continue;
        }

        /* 内层不支持 -o 或其他操作符 */
        luaX_syntaxerror(ls, luaO_pushfstring(ls->L,
          "unsupported operator '-%s' in test expression", opname));
      } else {
        luaX_syntaxerror(ls, "expected operator name after '-' in test expression");
      }
    }

    break;
  }

  /*
   * 外层循环: 处理 -o (OR)，优先级低于 -a
   * 仅在 allow_or 时处理
   */
  if (allow_or) {
    for (;;) {
      if (ls->t.token == '-') {
        luaX_next(ls);
        if (ls->t.token == TK_NAME && strcmp(getstr(ls->t.seminfo.ts), "o") == 0) {
          luaX_next(ls);  /* 跳过 'o' */
          single_value = 0;
          expdesc e2;
          /* 先调用 infix 将 v 的 false 跳转 patch 到右操作数之前 */
          luaK_infix(fs, OPR_OR, v);
          /* -o 右操作数递归解析，内层可包含 -a 但不包含 -o */
          parse_test_value(ls, &e2, line, 0);
          luaK_posfix(fs, OPR_OR, v, &e2, line);
          continue;
        }
        luaX_syntaxerror(ls, luaO_pushfstring(ls->L,
          "expected '-o' after '-', got '-%s'", getstr(ls->t.seminfo.ts)));
      }
      break;
    }
  }

  /* 单值: [ expr ] - 转换为布尔值，使用 not not expr 确保总是返回布尔 */
  if (single_value) {
    luaK_dischargevars(fs, v);
    luaK_prefix(fs, OPR_NOT, v, line);
    luaK_prefix(fs, OPR_NOT, v, line);
    luaK_exp2nextreg(fs, v);
  }
}

static void simpleexp (LexState *ls, expdesc *v) {
  /* simpleexp -> FLT | INT | NIL | TRUE | FALSE | ... |
                  constructor | FUNCTION body | suffixedexp */
  LUA_LOGD("[PARSER] simpleexp entry token=%d line=%d", ls->t.token, ls->linenumber);
  switch (ls->t.token) {
    case TK_IF: {
      ifexpr(ls, v);
      return;
    }
    case TK_ASTPARSER: {
      /* astparser(...) expression: 必须走 primaryexp 的 TK_ASTPARSER case
       * （在 primaryexp 中会调 astparserstat 解析源码字符串并返回闭包）。
       * 不能落 default→suffixedexp 这条路径，因为某些上下文（如 localstat
       * 的 expr 解析入口）可能绕过 suffixedexp/primaryexp，导致无法命中。 */
      suffixedexp(ls, v);
      return;
    }
    case TK_FLT: {
      init_exp(v, VKFLT, 0);
      v->u.nval = ls->t.seminfo.r;
      luaX_next(ls);
      break;
    }
    case TK_INT: {
      init_exp(v, VKINT, 0);
      v->u.ival = ls->t.seminfo.i;
      luaX_next(ls);
      break;
    }
    case TK_NIL: {
      init_exp(v, VNIL, 0);
      luaX_next(ls);
      break;
    }
    case TK_TRUE: {
      init_exp(v, VTRUE, 0);
      luaX_next(ls);
      break;
    }
    case TK_FALSE: {
      init_exp(v, VFALSE, 0);
      luaX_next(ls);
      break;
    }
    case TK_DOTS: {  /* vararg or spread operator */
      FuncState *fs = ls->fs;
      int dots_line = ls->linenumber;  /* 记录 '...' 所在行号 */
      int la = luaX_lookahead(ls);
      /*
      ** 展开运算符要求 '...' 和后续表达式必须在同一行。
      ** 如果跨行（如 varargs 赋值后换行），按标准 varargs 处理，
      ** 避免误将下一行的标识符当作展开目标。
      */
      if ((la == TK_NAME || la == '(' || la == '{' || la == TK_STRING || la == TK_RAWSTRING || la == TK_INTERPSTRING || la == TK_INT || la == TK_FLT || la == TK_TRUE || la == TK_FALSE || la == TK_NIL || la == '-' || la == TK_NOT || la == '#' || la == '~' || la == TK_FUNCTION || la == TK_LAMBDA) && ls->linenumber == dots_line) {
        luaX_next(ls); /* skip '...' */

        /* Generate: table.unpack(expr) */
        expdesc table_var;
        singlevaraux(fs, luaS_newliteral(ls->L, "table"), &table_var, 1);
        if (table_var.k == VVOID) {
          expdesc key;
          singlevaraux(fs, ls->envn, &table_var, 1);
          codestring(&key, luaS_newliteral(ls->L, "table"));
          luaK_indexed(fs, &table_var, &key);
        }
        luaK_exp2anyregup(fs, &table_var);

        expdesc unpack_key;
        codestring(&unpack_key, luaS_newliteral(ls->L, "unpack"));
        luaK_indexed(fs, &table_var, &unpack_key);

        luaK_exp2nextreg(fs, &table_var);
        int func_reg = table_var.u.info;

        expdesc arg;
        expr(ls, &arg); /* Parse the expression to spread */
        luaK_exp2nextreg(fs, &arg);

        init_exp(v, VCALL, luaK_codeABC(fs, OP_CALL, func_reg, 2, 0)); /* 1 arg, multiple returns */
        fs->freereg = func_reg + 1;
      } else {
        check_condition(ls, fs->f->is_vararg, "cannot use '...' outside a vararg function");
        init_exp(v, VVARARG, luaK_codeABC(fs, OP_VARARG, 0, 0, 1));
        luaX_next(ls);
      }
      break;
    }
    case TK_FUNCTION: {
      luaX_next(ls);
      body(ls, v, 0, ls->linenumber);
      return;
    }
    case TK_LAMBDA: {
      luaX_next(ls);
      lambda_body(ls, v, ls->linenumber);
      return;
    }
    case TK_INTERPSTRING: {
      /*
      ** 字符串插值处理 - 统一语法版本
      ** 
      ** 语法规则:
      ** - ${name}    : 简单变量插值，直接引用变量
      ** - ${[expr]}  : 复杂表达式插值，方括号内是任意表达式
      ** - $$         : 输出字面量 $ (无需反斜杠转义)
      ** - $          : 后面不跟 { 或 $ 时，就是普通字符
      ** 
      ** 示例:
      **   local name = "World"
      **   local age = 25
      **   local msg1 = "Hello, ${name}!"           -- "Hello, World!"
      **   local msg2 = "Age: ${[age + 1]} years"   -- "Age: 26 years"
      **   local msg3 = "Price: $$100"              -- "Price: $100"
      **   local msg4 = "$${name}"                  -- "${name}" (字面量)
      **
      ** 实现原理:
      ** - ${name}: 直接查找变量，根据类型决定是否 tostring
      ** - ${[expr]}: 收集表达式中的局部变量，生成
      **   function(var1, var2, ...) return tostring(expr) end 并调用
      */
      TString *interp_str = ls->t.seminfo.ts;
      const char *str = getstr(interp_str);
      size_t len = tsslen(interp_str);
      FuncState *fs = ls->fs;
      
      luaX_next(ls);  /* 跳过字符串token */
      
      /* 检查是否有插值标记 */
      int has_interpolation = 0;
      size_t check_i;
      for (check_i = 0; check_i < len; check_i++) {
        if (str[check_i] == '$' && check_i + 1 < len && str[check_i + 1] == '{') {
          has_interpolation = 1;
          break;
        }
      }
      
      if (!has_interpolation) {
        /* 普通字符串，直接返回 */
        codestring(v, interp_str);
        break;
      }
      
      /* 有插值，收集所有片段到连续寄存器 */
      int base_reg = fs->freereg;  /* 第一个片段的寄存器 */
      int part_count = 0;          /* 片段数量 */
      size_t i = 0;
      size_t last_end = 0;
      
      while (i < len) {
        if (str[i] == '$' && i + 1 < len && str[i + 1] == '{') {
          /* 处理 ${...} 前面的字符串部分 */
          if (i > last_end) {
            TString *part_str = luaS_newlstr(ls->L, str + last_end, i - last_end);
            codestring(v, part_str);
            luaK_exp2nextreg(fs, v);
            part_count++;
          }
          
          i += 2;  /* 跳过 ${ */
          
          /* 检查是否是表达式模式 ${[expr]} */
          int is_expr_mode = (i < len && str[i] == '[');
          
          if (is_expr_mode) {
            i++;  /* 跳过 [ */
            size_t expr_start = i;
            int depth = 1;  /* [ ] 深度 */
            int brace_depth = 0;  /* { } 深度 */
            
            /* 找到匹配的 ]} (支持嵌套) */
            while (i < len && depth > 0) {
              if (str[i] == '[') depth++;
              else if (str[i] == ']') {
                depth--;
                if (depth == 0 && brace_depth == 0) break;
              }
              else if (str[i] == '{') brace_depth++;
              else if (str[i] == '}') brace_depth--;
              i++;
            }
            
            size_t expr_len = i - expr_start;
            i++;  /* 跳过 ] */
            if (i < len && str[i] == '}') i++;  /* 跳过 } */
            last_end = i;
            
            if (expr_len > 0) {
              /*
              ** 检查表达式是否只是一个简单的标识符
              ** 如果是，就像 ${name} 一样处理，不使用 load()
              */
              int is_simple_id = 1;
              size_t check_j;
              for (check_j = 0; check_j < expr_len; check_j++) {
                char c = str[expr_start + check_j];
                if (check_j == 0) {
                  if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_')) {
                    is_simple_id = 0;
                    break;
                  }
                } else {
                  if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || 
                        (c >= '0' && c <= '9') || c == '_')) {
                    is_simple_id = 0;
                    break;
                  }
                }
              }
              
              if (is_simple_id) {
                /*
                ** 简单标识符处理 - 和 ${name} 相同的逻辑
                */
                TString *varname = luaS_newlstr(ls->L, str + expr_start, expr_len);
                expdesc var_exp;
                
                int varkind = searchvar(fs, varname, &var_exp);
                if (varkind >= 0) {
                   Vardesc *vd = getlocalvardesc(fs, var_exp.u.var.vidx);
                   if (vd->vd.kind == GDKREG || vd->vd.kind == GDKCONST)
                      varkind = -1;
                }
                if (varkind < 0) {
                  singlevaraux(fs, varname, &var_exp, 1);
                  /* 处理全局变量 */
                  if (var_exp.k == VVOID) {
                    expdesc key;
                    singlevaraux(fs, ls->envn, &var_exp, 1);
                    codestring(&key, varname);
                    luaK_indexed(fs, &var_exp, &key);
                  }
                }
                
                /* 调用 tostring */
                expdesc tostring_func;
                TString *tostring_name = luaS_newliteral(ls->L, "tostring");
                singlevaraux(fs, tostring_name, &tostring_func, 1);
                if (tostring_func.k == VVOID) {
                  expdesc env_v;
                  singlevaraux(fs, ls->envn, &env_v, 1);
                  expdesc key;
                  codestring(&key, tostring_name);
                  luaK_indexed(fs, &env_v, &key);
                  tostring_func = env_v;
                }
                
                luaK_exp2nextreg(fs, &tostring_func);
                int call_reg = fs->freereg - 1;
                luaK_exp2nextreg(fs, &var_exp);
                luaK_codeABC(fs, OP_CALL, call_reg, 2, 2);
                fs->freereg = call_reg + 1;
                part_count++;
              }
              else {
              /*
              ** 复杂表达式处理 - 支持访问局部变量
              ** 
              ** 实现原理:
              ** 1. 收集当前作用域的所有局部变量
              ** 2. 提取表达式中可能用到的变量名
              ** 3. 生成函数: function(var1, var2, ...) return tostring(expr) end
              ** 4. 用局部变量的值作为参数调用这个函数
              **
              ** 示例: age=25, "${[age + 1]}" 变成:
              **   (function(age) return tostring(age + 1) end)(age)
              */
              
              /* 收集表达式中的标识符 */
              #define MAX_INTERP_VARS 32
              TString *used_vars[MAX_INTERP_VARS];
              int var_regs[MAX_INTERP_VARS];  /* 变量在栈上的寄存器 */
              int nused = 0;
              
              /* 扫描表达式，提取所有标识符 */
              size_t scan_i = 0;
              while (scan_i < expr_len && nused < MAX_INTERP_VARS) {
                char c = str[expr_start + scan_i];
                /* 跳过字符串字面量 */
                if (c == '"' || c == '\'') {
                  char quote = c;
                  scan_i++;
                  while (scan_i < expr_len && str[expr_start + scan_i] != quote) {
                    if (str[expr_start + scan_i] == '\\') scan_i++;
                    scan_i++;
                  }
                  scan_i++;
                  continue;
                }
                /* 检查是否是标识符开始 */
                if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_') {
                  size_t id_start = scan_i;
                  while (scan_i < expr_len) {
                    c = str[expr_start + scan_i];
                    if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                          (c >= '0' && c <= '9') || c == '_')) break;
                    scan_i++;
                  }
                  size_t id_len = scan_i - id_start;
                  
                  /* 检查是否是关键字（跳过） */
                  const char *id = str + expr_start + id_start;
                  int is_keyword = 0;
                  if (id_len == 3 && (strncmp(id, "and", 3) == 0 || 
                                      strncmp(id, "for", 3) == 0 ||
                                      strncmp(id, "not", 3) == 0 ||
                                      strncmp(id, "nil", 3) == 0 ||
                                      strncmp(id, "end", 3) == 0)) is_keyword = 1;
                  else if (id_len == 2 && (strncmp(id, "do", 2) == 0 ||
                                           strncmp(id, "if", 2) == 0 ||
                                           strncmp(id, "in", 2) == 0 ||
                                           strncmp(id, "or", 2) == 0)) is_keyword = 1;
                  else if (id_len == 4 && (strncmp(id, "then", 4) == 0 ||
                                           strncmp(id, "else", 4) == 0 ||
                                           strncmp(id, "true", 4) == 0)) is_keyword = 1;
                  else if (id_len == 5 && (strncmp(id, "while", 5) == 0 ||
                                           strncmp(id, "false", 5) == 0 ||
                                           strncmp(id, "local", 5) == 0 ||
                                           strncmp(id, "break", 5) == 0)) is_keyword = 1;
                  else if (id_len == 6 && (strncmp(id, "return", 6) == 0 ||
                                           strncmp(id, "repeat", 6) == 0)) is_keyword = 1;
                  else if (id_len == 8 && strncmp(id, "function", 8) == 0) is_keyword = 1;
                  
                  if (!is_keyword) {
                    TString *varname = luaS_newlstr(ls->L, id, id_len);
                    expdesc var_test;
                    
                    /* 检查是否是局部变量或上值 */
                    int varkind = searchvar(fs, varname, &var_test);
                    if (varkind >= 0) {
                      Vardesc *vd = getlocalvardesc(fs, var_test.u.var.vidx);
                      if (vd->vd.kind == GDKREG || vd->vd.kind == GDKCONST)
                         varkind = -1;
                    }
                    if (varkind >= 0) {
                      /* 是局部变量，记录下来 */
                      int already_added = 0;
                      int k;
                      for (k = 0; k < nused; k++) {
                        if (eqstr(used_vars[k], varname)) {
                          already_added = 1;
                          break;
                        }
                      }
                      if (!already_added && nused < MAX_INTERP_VARS) {
                        used_vars[nused] = varname;
                        var_regs[nused] = var_test.u.var.ridx;
                        nused++;
                      }
                    }
                  }
                }
                else {
                  scan_i++;
                }
              }
              
              /*
              ** 生成代码字符串:
              ** function(var1, var2) return tostring(expr) end
              ** 在编译期使用 luaL_loadbuffer 预编译闭包，避免运行时调用 load()
              */

              /* calculate total length */
              size_t total_len = 16; /* "return function(" */
              for (int k = 0; k < nused; k++) {
                  total_len += tsslen(used_vars[k]);
                  if (k < nused - 1) total_len += 2; /* ", " */
              }
              total_len += 19; /* ") return tostring(" */
              total_len += expr_len;
              total_len += 5; /* ") end" */

              char *code_str = luaM_newblock(ls->L, total_len + 1);

              /* 构建代码字符串 */
              size_t pos = 0;
              memcpy(code_str + pos, "return function(", 16); pos += 16;
              for (int k = 0; k < nused; k++) {
                  size_t l = tsslen(used_vars[k]);
                  memcpy(code_str + pos, getstr(used_vars[k]), l); pos += l;
                  if (k < nused - 1) {
                      memcpy(code_str + pos, ", ", 2); pos += 2;
                  }
              }
              memcpy(code_str + pos, ") return tostring(", 18); pos += 18;
              memcpy(code_str + pos, str + expr_start, expr_len); pos += expr_len;
              memcpy(code_str + pos, ") end", 5); pos += 5;
              code_str[pos] = '\0';

              /*
              ** 在编译期使用 luaL_loadbuffer 编译代码字符串，
              ** 然后调用得到闭包，存入常量表中。
              ** 运行时直接加载常量并调用，避免依赖 load() 全局函数。
              */
              int status = luaL_loadbuffer(ls->L, code_str, pos, "=interp");
              if (status != LUA_OK) {
                const char *err = lua_tostring(ls->L, -1);
                luaO_pushfstring(ls->L, "interpolation: failed to compile expression '%s': %s",
                                 code_str, err ? err : "unknown error");
                lua_pop(ls->L, 1);
                luaM_freearray(ls->L, code_str, total_len + 1);
                luaK_semerror(ls, lua_tostring(ls->L, -1));
                /* unreachable */
              }

              /* 执行 chunk 得到闭包函数 */
              status = lua_pcall(ls->L, 0, 1, 0);
              if (status != LUA_OK) {
                const char *err = lua_tostring(ls->L, -1);
                luaO_pushfstring(ls->L, "interpolation: failed to evaluate expression '%s': %s",
                                 code_str, err ? err : "unknown error");
                lua_pop(ls->L, 1);
                luaM_freearray(ls->L, code_str, total_len + 1);
                luaK_semerror(ls, lua_tostring(ls->L, -1));
                /* unreachable */
              }

              /*
              ** 栈顶是闭包函数 function(var1, var2, ...) return tostring(expr) end
              ** 将其存入常量表，运行时直接加载并调用
              */
              TValue closure_val;
              setobj(ls->L, &closure_val, s2v(ls->L->top.p - 1));
              int closure_kidx = luaK_closureK(fs, &closure_val);
              ls->L->top.p--;  /* 弹出闭包 */

              luaM_freearray(ls->L, code_str, total_len + 1);

              /* 运行时：加载预编译闭包常量，推入参数，调用 */
              expdesc closure_exp;
              init_exp(&closure_exp, VK, closure_kidx);
              luaK_exp2nextreg(fs, &closure_exp);
              int closure_reg = fs->freereg - 1;

              for (int k = 0; k < nused; k++) {
                  expdesc var_exp;
                  int vk = searchvar(fs, used_vars[k], &var_exp);
                  if (vk < 0) {
                     singlevaraux(fs, used_vars[k], &var_exp, 1);
                  }
                  luaK_exp2nextreg(fs, &var_exp);
              }
              luaK_codeABC(fs, OP_CALL, closure_reg, nused + 1, 2);
              fs->freereg = closure_reg + 1;

              /* 移动结果到正确位置 */
              if (closure_reg != base_reg + part_count) {
                luaK_codeABC(fs, OP_MOVE, base_reg + part_count, closure_reg, 0);
                fs->freereg = base_reg + part_count + 1;
              }

              part_count++;
              }  /* end of else (complex expression) */
            }
          }
          else {
            /* 简单变量模式 ${name} */
            size_t expr_start = i;
            int depth = 1;
            
            /* 找到匹配的 } */
            while (i < len && depth > 0) {
              if (str[i] == '{') depth++;
              else if (str[i] == '}') depth--;
              if (depth > 0) i++;
            }
            
            size_t expr_len = i - expr_start;
            i++;  /* 跳过 } */
            last_end = i;
            
            if (expr_len > 0) {
              /* 检查是否是简单变量名 */
              int is_simple_var = 1;
              size_t j;
              for (j = 0; j < expr_len; j++) {
                char c = str[expr_start + j];
                if (j == 0) {
                  if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_')) {
                    is_simple_var = 0;
                    break;
                  }
                } else {
                  if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || 
                        (c >= '0' && c <= '9') || c == '_')) {
                    is_simple_var = 0;
                    break;
                  }
                }
              }
              
              if (is_simple_var) {
                /*
                ** 简单变量名处理
                ** 直接查找变量，字符串/数字类型跳过 tostring
                */
                TString *varname = luaS_newlstr(ls->L, str + expr_start, expr_len);
                expdesc var_exp;
                
                int varkind = searchvar(fs, varname, &var_exp);
                if (varkind >= 0) {
                   Vardesc *vd = getlocalvardesc(fs, var_exp.u.var.vidx);
                   if (vd->vd.kind == GDKREG || vd->vd.kind == GDKCONST)
                      varkind = -1;
                }
                if (varkind < 0) {
                  singlevaraux(fs, varname, &var_exp, 1);
                  /* 处理全局变量: 当 singlevaraux 返回 VVOID 时，需要通过 _ENV 访问 */
                  if (var_exp.k == VVOID) {
                    expdesc key;
                    singlevaraux(fs, ls->envn, &var_exp, 1);
                    codestring(&key, varname);
                    luaK_indexed(fs, &var_exp, &key);
                  }
                }
                
                /* 检查是否是编译期常量 */
                if (var_exp.k == VKSTR) {
                  /* 字符串常量，直接使用 */
                  luaK_exp2nextreg(fs, &var_exp);
                  part_count++;
                }
                else if (var_exp.k == VKINT || var_exp.k == VKFLT) {
                  /* 数字常量，需要转字符串 */
                  /* 获取 tostring 函数 */
                  expdesc tostring_func;
                  TString *tostring_name = luaS_newliteral(ls->L, "tostring");
                  singlevaraux(fs, tostring_name, &tostring_func, 1);
                  if (tostring_func.k == VVOID) {
                    expdesc env_v;
                    singlevaraux(fs, ls->envn, &env_v, 1);
                    expdesc key;
                    codestring(&key, tostring_name);
                    luaK_indexed(fs, &env_v, &key);
                    tostring_func = env_v;
                  }
                  
                  luaK_exp2nextreg(fs, &tostring_func);
                  int call_reg = fs->freereg - 1;
                  luaK_exp2nextreg(fs, &var_exp);
                  luaK_codeABC(fs, OP_CALL, call_reg, 2, 2);
                  fs->freereg = call_reg + 1;
                  part_count++;
                }
                else {
                  /* 运行时类型，调用 tostring */
                  expdesc tostring_func;
                  TString *tostring_name = luaS_newliteral(ls->L, "tostring");
                  singlevaraux(fs, tostring_name, &tostring_func, 1);
                  if (tostring_func.k == VVOID) {
                    expdesc env_v;
                    singlevaraux(fs, ls->envn, &env_v, 1);
                    expdesc key;
                    codestring(&key, tostring_name);
                    luaK_indexed(fs, &env_v, &key);
                    tostring_func = env_v;
                  }
                  
                  luaK_exp2nextreg(fs, &tostring_func);
                  int call_reg = fs->freereg - 1;
                  luaK_exp2nextreg(fs, &var_exp);
                  luaK_codeABC(fs, OP_CALL, call_reg, 2, 2);
                  fs->freereg = call_reg + 1;
                  part_count++;
                }
              }
              else {
                /* ${...} 内容不是有效变量名，报错或当作字面量处理 */
                TString *part_str = luaS_newlstr(ls->L, str + expr_start - 2, expr_len + 3);
                codestring(v, part_str);
                luaK_exp2nextreg(fs, v);
                part_count++;
              }
            }
          }
        }
        else {
          i++;
        }
      }
      
      /* 处理最后的字符串部分 */
      if (last_end < len) {
        TString *part_str = luaS_newlstr(ls->L, str + last_end, len - last_end);
        codestring(v, part_str);
        luaK_exp2nextreg(fs, v);
        part_count++;
      }
      
      /* 使用 OP_CONCAT 连接所有片段 */
      if (part_count == 0) {
        TString *empty_str = luaS_newliteral(ls->L, "");
        codestring(v, empty_str);
      }
      else if (part_count == 1) {
        init_exp(v, VNONRELOC, base_reg);
      }
      else {
        /* OP_CONCAT A B C: R[A] := R[A] .. ... .. R[A + B - 1] */
        luaK_codeABC(fs, OP_CONCAT, base_reg, part_count, 0);
        fs->freereg = base_reg + 1;
        init_exp(v, VNONRELOC, base_reg);
      }
      
      v->t = NO_JUMP;
      v->f = NO_JUMP;
      return;
    }
    case TK_SWITCH: {
      /**
       * Switch 表达式 — 编译层直接实现（结果寄存器法）
       * 
       * 不再使用 IIFE 模拟：
       *   旧: a = (function() switch (exp) do case... end end)()
       *   新: 直接在当前 FuncState 中生成比较分支字节码，
       *       每个 case => expr 存入结果寄存器，无函数包装开销。
       * 
       * @see luaK_switchexpression() in lcode.c
       */
      luaK_switchexpression(ls, v);
      return;
    }
    case TK_ARROW: {
      /**
       * 箭头函数语法糖（语句形式）: ->(args){ stat } 或 ->{ stat }
       * 等价于: function(args) stat end
       * 迁移到 lcode.c 编译层实现。
       */
      luaK_arrow_statement(ls, v);
      return;
    }
    case TK_MEAN: {
      /**
       * 箭头函数语法糖（表达式形式）: =>(args){ exp } 或 =>{ exp }
       * 等价于: function(args) return exp end
       * 迁移到 lcode.c 编译层实现。
       */
      luaK_arrow_expression(ls, v);
      return;
    }
    case '[': {
      if (luaX_lookahead(ls) == TK_FOR) {
          int line = ls->linenumber;
          luaX_next(ls); /* skip '[' */

          FuncState new_fs;
          BlockCnt bl;
          new_fs.f = addprototype(ls);
          new_fs.f->linedefined = line;
          open_func(ls, &new_fs, &bl);

          int t_vidx = new_localvarliteral(ls, "_t");
          adjustlocalvars(ls, 1);
          int t_reg = getlocalvardesc(&new_fs, t_vidx)->vd.ridx;
          new_fs.freereg = t_reg + 1;

          luaK_codeABC(&new_fs, OP_NEWTABLE, t_reg, 0, 0);
          luaK_code(&new_fs, 0);

          checknext(ls, TK_FOR);

          int base = new_fs.freereg;
          new_localvarliteral(ls, "(for state)");
          new_localvarliteral(ls, "(for state)");
          new_localvarliteral(ls, "(for state)");
          new_localvarliteral(ls, "(for state)");

          TString *loop_vars[20];
          int nvars = 0;
          do {
            loop_vars[nvars++] = str_checkname(ls);
          } while (testnext(ls, ',') && nvars < 20);

          checknext(ls, TK_IN);

          expdesc e;
          int nexps = explist(ls, &e);
          adjust_assign(ls, 4, nexps, &e);
          luaK_checkstack(&new_fs, 4);

          adjustlocalvars(ls, 4);

          int prep_jmp = luaK_codeABx(&new_fs, OP_TFORPREP, base, 0);
          int loop_start = luaK_getlabel(&new_fs);

          for (int i = 0; i < nvars; i++) {
              new_localvar(ls, loop_vars[i]);
          }
          adjustlocalvars(ls, nvars);
          luaK_reserveregs(&new_fs, nvars);

          if (ls->t.token == TK_DO) {
              luaX_next(ls);
          } else if (ls->t.token == TK_NAME && strcmp(getstr(ls->t.seminfo.ts), "yield") == 0) {
              luaX_next(ls);
          } else {
              luaX_syntaxerror(ls, "expected 'do' or 'yield' in list comprehension");
          }

          expdesc expr_v;
          expr(ls, &expr_v);
          /* 必须在解析条件之前将表达式物化到寄存器中，
          ** 否则条件求值的临时寄存器会覆盖表达式结果。
          ** 因为 expr_v 是 VRELOCABLE，其目标寄存器尚未分配，
          ** 延迟到条件解析之后分配会导致和条件的临时寄存器重叠。 */
          luaK_exp2nextreg(&new_fs, &expr_v);

          int if_jmp = NO_JUMP;
          if (testnext(ls, TK_IF)) {
            expdesc cond_v;
            expr(ls, &cond_v);
            luaK_goiftrue(&new_fs, &cond_v);
            if_jmp = cond_v.f;
          }

          int len_reg = new_fs.freereg;
          luaK_reserveregs(&new_fs, 1);
          luaK_codeABC(&new_fs, OP_LEN, len_reg, t_reg, 0);
          luaK_codeABCk(&new_fs, OP_ADDI, len_reg, len_reg, int2sC(1), 0);
          luaK_codeABCk(&new_fs, OP_MMBINI, len_reg, int2sC(1), TM_ADD, 0);

          expdesc tab, key, val;
          init_exp(&tab, VNONRELOC, t_reg);
          init_exp(&key, VNONRELOC, len_reg);
          init_exp(&val, VNONRELOC, expr_v.u.info);
          luaK_indexed(&new_fs, &tab, &key);
          luaK_storevar(&new_fs, &tab, &val);

          if (if_jmp != NO_JUMP) {
            luaK_patchtohere(&new_fs, if_jmp);
          }

          new_fs.freereg = base + 4 + nvars;

          fixforjump(&new_fs, prep_jmp, luaK_getlabel(&new_fs), 0);
          luaK_codeABC(&new_fs, OP_TFORCALL, base, 0, nvars);
          int loop_jmp = luaK_codeABx(&new_fs, OP_TFORLOOP, base, 0);
          fixforjump(&new_fs, loop_jmp, prep_jmp + 1, 1);

          luaK_ret(&new_fs, t_reg, 1);

          checknext(ls, ']');

          new_fs.f->lastlinedefined = ls->linenumber;
          close_func(ls);

          FuncState *fs = ls->fs;
          init_exp(v, VRELOC, luaK_codeABx(fs, OP_CLOSURE, 0, fs->np - 1));
          luaK_exp2nextreg(fs, v);

          int func_reg = v->u.info;
          init_exp(v, VCALL, luaK_codeABC(fs, OP_CALL, func_reg, 1, 2));
          luaK_fixline(fs, line);
          fs->freereg = func_reg + 1;

          return;
      }
      /* 检测 map 字面量: [], [name = val, ...], [[expr] = val, ...] */
      {
        int la = luaX_lookahead(ls);
        if (la == ']') {
          /* 空map: [] */
          mapconstructor(ls, v);
          return;
        }
        if (la == '[') {
          /* map with [expr] key: [[expr] = val, ...] */
          mapconstructor(ls, v);
          return;
        }
        if (la == TK_NAME) {
          int la2 = luaX_lookahead2(ls);
          if (la2 == '=') {
            /* map with name sugar: [name = val, ...] */
            mapconstructor(ls, v);
            return;
          }
        }
      }
      /**
       * 条件测试表达式语法: [ test_expr ]
       * 直接将测试表达式编译为字节码，不依赖 __test__ 运行时函数
       *
       * 支持的操作：
       *   - 逻辑非: [ ! cond ]
       *   - 字符串测试: [ -z str ], [ -n str ]
       *   - 类型测试: [ -nil var ], [ -bool var ], [ -func var ], [ -type var "typename" ]
       *   - 数值比较: [ a -eq b ], [ a -ne b ], [ a -gt b ], [ a -lt b ], [ a -ge b ], [ a -le b ]
       *   - 字符串比较: [ str1 = str2 ], [ str1 == str2 ], [ str1 != str2 ]
       *   - 逻辑运算: [ cond1 -a cond2 ], [ cond1 -o cond2 ]
       *   - 单值测试: [ expr ] (检查值是否为真)
       */
      {
        FuncState *fs = ls->fs;
        int line = ls->linenumber;
        luaX_next(ls);  /* 跳过 '[' */

        /* 处理逻辑非前缀: [ ! cond ] */
        int has_not = 0;
        if (ls->t.token == '!' || ls->t.token == TK_NOT) {
          has_not = 1;
          luaX_next(ls);  /* 跳过 '!' */
        }

        /* 处理带 '-' 前缀的一元操作符 */
        if (ls->t.token == '-') {
          luaX_next(ls);  /* 跳过 '-' */
          if (ls->t.token == TK_NAME || ls->t.token == TK_NIL || ls->t.token == TK_BOOL) {
            const char *opname = getstr(ls->t.seminfo.ts);
            luaX_next(ls);  /* 跳过操作符名 */

            /* 字符串测试: [ -z str ], [ -n str ] */
            if (strcmp(opname, "z") == 0 || strcmp(opname, "n") == 0) {
              expdesc arg;
              expr(ls, &arg);
              luaK_prefix(fs, OPR_LEN, &arg, line);  /* 生成 #str */
              expdesc zero;
              init_exp(&zero, VKINT, 0);
              zero.u.ival = 0;
              BinOpr cmp_op = (opname[0] == 'z') ? OPR_EQ : OPR_NE;
              luaK_infix(fs, cmp_op, &arg);
              luaK_posfix(fs, cmp_op, &arg, &zero, line);
              *v = arg;
              luaK_exp2nextreg(fs, v);
            }
            /* 类型测试: [ -nil var ], [ -bool var ], [ -func var ] */
            else if (strcmp(opname, "nil") == 0 || strcmp(opname, "bool") == 0 ||
                     strcmp(opname, "func") == 0) {
              expdesc arg;
              expr(ls, &arg);
              int r1 = luaK_exp2anyreg(fs, &arg);
              /* 将简写操作符名映射到 Lua 标准类型名 */
              const char *typename_full;
              if (strcmp(opname, "bool") == 0)
                typename_full = "boolean";
              else if (strcmp(opname, "func") == 0)
                typename_full = "function";
              else
                typename_full = opname;  /* "nil" 直接匹配 */
              int type_k = luaK_stringK(fs, luaS_new(ls->L, typename_full));
              /* OP_IS A B C k: if (type(R[A]) == K[B]) ~= k then pc++ */
              /* k=0: 类型匹配时跳过 JMP，执行 LOADTRUE；不匹配时执行 JMP 跳到 LOADFALSE */
              luaK_codeABCk(fs, OP_IS, r1, type_k, 0, 0);
              int jmp_false = luaK_jump(fs);
              luaK_codeABC(fs, OP_LOADTRUE, r1, 0, 0);
              int jmp_end = luaK_jump(fs);
              luaK_patchtohere(fs, jmp_false);
              luaK_codeABC(fs, OP_LOADFALSE, r1, 0, 0);
              luaK_patchtohere(fs, jmp_end);
              init_exp(v, VNONRELOC, r1);
            }
            /* 类型测试: [ -type var "typename" ] */
            else if (strcmp(opname, "type") == 0) {
              expdesc arg;
              expr(ls, &arg);
              int r1 = luaK_exp2anyreg(fs, &arg);
              if (ls->t.token == TK_STRING || ls->t.token == TK_RAWSTRING ||
                  ls->t.token == TK_INTERPSTRING) {
                int type_k = luaK_stringK(fs, ls->t.seminfo.ts);
                luaX_next(ls);  /* 跳过类型名字符串 */
                luaK_codeABCk(fs, OP_IS, r1, type_k, 0, 0);
                int jmp_false = luaK_jump(fs);
                luaK_codeABC(fs, OP_LOADTRUE, r1, 0, 0);
                int jmp_end = luaK_jump(fs);
                luaK_patchtohere(fs, jmp_false);
                luaK_codeABC(fs, OP_LOADFALSE, r1, 0, 0);
                luaK_patchtohere(fs, jmp_end);
                init_exp(v, VNONRELOC, r1);
              } else {
                luaX_syntaxerror(ls, "expected type name string after -type");
              }
            }
            /* 比较操作符缺少左操作数: [ -eq expr ] 等 */
            else if (strcmp(opname, "eq") == 0 || strcmp(opname, "ne") == 0 ||
                     strcmp(opname, "gt") == 0 || strcmp(opname, "lt") == 0 ||
                     strcmp(opname, "ge") == 0 || strcmp(opname, "le") == 0) {
              luaX_syntaxerror(ls, luaO_pushfstring(ls->L, "comparison operator '-%s' requires left operand", opname));
            }
            /* 逻辑操作符缺少左操作数: [ -a expr ], [ -o expr ] */
            else if (strcmp(opname, "a") == 0 || strcmp(opname, "o") == 0) {
              luaX_syntaxerror(ls, luaO_pushfstring(ls->L, "logical operator '-%s' requires left operand", opname));
            }
            /* 文件测试及不支持的扩展操作符 */
            else {
              luaX_syntaxerror(ls, luaO_pushfstring(ls->L, "unsupported test operator '-%s' in [ ... ] syntax", opname));
            }
          } else {
            luaX_syntaxerror(ls, "expected operator name after '-' in test expression");
          }
        }
        /* 处理值表达式: [ expr ], [ expr -op expr ], [ expr = expr ] 等，支持链式 */
        else {
          parse_test_value(ls, v, line, 1);
        }

        /* 应用逻辑非前缀 */
        if (has_not) {
          luaK_prefix(fs, OPR_NOT, v, line);
        }

        check_match(ls, ']', '[', line);
        return;
      }
    }
    case TK_REGEX: {  /* 正则字面量 /pattern/flags */
      FuncState *fs = ls->fs;
      TString *ts = ls->t.seminfo.ts;
      luaX_next(ls);
      int kidx = luaK_stringK(fs, ts);
      init_exp(v, VRELOC, luaK_codeABx(fs, OP_REGEX, 0, kidx));
      break;
    }
    default: {
      suffixedexp(ls, v);
      return;
    }
  }
}


static UnOpr getunopr (int op) {
  switch (op) {
    case TK_NOT: return OPR_NOT;
    case '-': return OPR_MINUS;
    case '~': return OPR_BNOT;
    case '#': return OPR_LEN;
    case TK_AWAIT: return OPR_AWAIT;
    default: return OPR_NOUNOPR;
  }
}


static BinOpr getbinopr (int op) {
  switch (op) {
    case '+': return OPR_ADD;
    case '-': return OPR_SUB;
    case '*': return OPR_MUL;
    case '%': return OPR_MOD;
    case '^': return OPR_POW;
    case '/': return OPR_DIV;
    case TK_IDIV: return OPR_IDIV;
    case '&': return OPR_BAND;
    case '|': return OPR_BOR;
    case '~': return OPR_BXOR;
    case TK_SHL: return OPR_SHL;
    case TK_SHR: return OPR_SHR;
    case TK_CONCAT: return OPR_CONCAT;
    case TK_PIPE: return OPR_PIPE;
    case TK_NE: return OPR_NE;
    case TK_EQ: return OPR_EQ;
    case '<': return OPR_LT;
    case TK_LE: return OPR_LE;
    case '>': return OPR_GT;
    case TK_GE: return OPR_GE;
    case TK_SPACESHIP: return OPR_SPACESHIP;
    case TK_IS: return OPR_IS;
    case TK_INSTANCEOF: return OPR_IS;
    case TK_AND: return OPR_AND;
    case TK_OR: return OPR_OR;
    case TK_IN: return OPR_IN;
    case TK_NULLCOAL: return OPR_NULLCOAL;
    case TK_MEAN: return OPR_CASE;
    case TK_MERGE: return OPR_MERGE;  /* '<>' 表合并 */
    default: return OPR_NOBINOPR;
  }
}


/*
** Priority table for binary operators.
*/
static const struct {
  lu_byte left;  /* left priority for each binary operator */
  lu_byte right; /* right priority */
} priority[] = {  /* ORDER OPR */
   {10, 10}, {10, 10},           /* '+' '-' */
   {11, 11}, {11, 11},           /* '*' '%' */
   {14, 13},                  /* '^' (right associative) */
   {11, 11}, {11, 11},           /* '/' '//' */
   {6, 6}, {4, 4}, {5, 5},   /* '&' '|' '~' */
   {7, 7}, {7, 7},           /* '<<' '>>' */
   {9, 8},                   /* '..' (right associative) */
   {8, 8},                   /* '|>' 管道（左结合：a |> f |> g == g(f(a))） */
   {3, 3}, {3, 3}, {3, 3},   /* ==, <, <= */
   {3, 3}, {3, 3}, {3, 3},   /* ~=, >, >= */
   {3, 3},                   /* <=> (spaceship) */
   {3, 3},                   /* is */
   {3, 3},                   /* as */
   {3, 3},                   /* in */
   {2, 2}, {1, 1},           /* and, or */
   {1, 1},                   /* ?? (null coalescing, right associative) */
   {1, 1},                   /* => (case operator) */
   {5, 5},                   /* INFIX (infix function call, left associative) */
   {5, 5}                    /* <> (table merge, left associative) */
};

#define UNARY_PRIORITY	12  /* priority for unary operators */
#define PRI_CASE        1   /* priority for case arrow '=>' */
#define PRI_RANGE_EXPR  9   /* priority to stop before '..' for range pattern parsing */


/**
 * @brief 判断一个 token 是否可以作为中缀函数调用右侧表达式的起始。
 * @param token 要检查的 token 类型
 * @return 1 如果可以开始表达式，否则 0
 */
static int is_infix_expr_start (int token) {
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
 * @brief 检查当前 token 是否与 lookahead 在同一行，用于防止中缀调用跨行。
 * 
 * 通过比较当前 token (ls->t) 被词法解析时的行号与最近一次 
 * luaX_lookahead 调用后 ls->linenumber 的值来判断。
 * 由于 luaX_lookahead 更新了 ls->linenumber 为 lookahead token 的行号，
 * 如果两者相同则说明在同一行。
 * 
 * @param ls 词法状态
 * @return 1 如果同一行，0 否则
 */
static int is_same_line_infix (LexState *ls) {
  return ls->t.linenumber == ls->linenumber;
}


/*
** subexpr -> (simpleexp | unop subexpr) { binop subexpr }
** where 'binop' is any binary operator with a priority higher than 'limit'
*/
static BinOpr subexpr (LexState *ls, expdesc *v, int limit) {
  BinOpr op;
  UnOpr uop;
  enterlevel(ls);
  LUA_LOGD("[PARSER] subexpr ENTRY limit=%d token=%d line=%d", limit, ls->t.token, ls->linenumber);

  if (ls->t.token == '#' && luaX_lookahead(ls) == TK_NAME && strcmp(getstr(ls->lookahead.seminfo.ts), "embed") == 0) {
      luaX_next(ls); /* skip '#' */
      luaX_next(ls); /* skip 'embed' */
      if (ls->t.token != TK_STRING && ls->t.token != TK_RAWSTRING) {
          luaX_syntaxerror(ls, "expected string literal after #embed");
      }
      const char *filename = getstr(ls->t.seminfo.ts);
      FILE *f = fopen(filename, "rb");
      if (!f) {
          luaX_syntaxerror(ls, luaO_pushfstring(ls->L, "cannot open file '%s' for #embed", filename));
      }
      fseek(f, 0, SEEK_END);
      long size = ftell(f);
      fseek(f, 0, SEEK_SET);
      char *buf = luaM_newvector(ls->L, size + 1, char);
      if (size > 0 && fread(buf, 1, size, f) != (size_t)size) {
          fclose(f);
          luaM_freearray(ls->L, buf, size + 1);
          luaX_syntaxerror(ls, "failed to read file for #embed");
      }
      fclose(f);
      buf[size] = '\0';
      TString *ts = luaS_newlstr(ls->L, buf, size);
      luaM_freearray(ls->L, buf, size + 1);
      codestring(v, ts);
      int embed_expr_line = ls->linenumber;  /* 保存字符串所在行号 */
      luaX_next(ls); /* skip string */

      /* parse binary operators */
      op = getbinopr(ls->t.token);
      if (op == OPR_NOBINOPR && ls->t.token == TK_NAME &&
          ls->t.linenumber == embed_expr_line &&
          is_infix_expr_start(luaX_lookahead(ls)) && is_same_line_infix(ls)) {
        op = OPR_INFIX;
      }
      while (op != OPR_NOBINOPR && priority[op].left > limit) {
        expdesc v2;
        BinOpr nextop;
        int line = ls->linenumber;
        if (op == OPR_INFIX) {
          TString *method = ls->t.seminfo.ts;
          luaX_next(ls);  /* 跳过方法名 */
          {
            expdesc key;
            codestring(&key, method);
            luaK_self(ls->fs, v, &key);
          }
          int old_ifx = ls->expr_flags;
          ls->expr_flags |= E_INFIX_ARG;
          nextop = subexpr(ls, &v2, priority[OPR_INFIX].right);
          ls->expr_flags = old_ifx;
          {
            int base = v->u.info;
            if (hasmultret(v2.k))
              luaK_setmultret(ls->fs, &v2);
            else {
              if (v2.k != VVOID)
                luaK_exp2nextreg(ls->fs, &v2);
            }
            int nparams = (v2.k == VVOID) ? 2 : (ls->fs->freereg - base);
            init_exp(v, VCALL, luaK_codeABC(ls->fs, OP_CALL, base, nparams, 2));
            luaK_fixline(ls->fs, line);
            ls->fs->freereg = base + 1;
          }
          op = nextop;
          if (op == OPR_NOBINOPR && ls->t.token == TK_NAME && ls->t.linenumber == line &&
              is_infix_expr_start(luaX_lookahead(ls)) && is_same_line_infix(ls)) {
            op = OPR_INFIX;
          }
        } else {
          /* 范围操作符检测：'..' 前无空格且两端为整数常量时生成范围表 */
          int concat_nospace = (op == OPR_CONCAT) ? ls->t.nospace : 0;
          luaX_next(ls);  /* skip operator */
          if (op == OPR_CONCAT && concat_nospace && v->k == VKINT) {
            /* 范围操作符：先解析右操作数，若也是整数则生成范围表 */
            nextop = subexpr(ls, &v2, priority[op].right);
            if (v2.k == VKINT) {
              luaK_range(ls->fs, v, v->u.ival, v2.u.ival, line);
              op = nextop;
              continue;  /* 跳过 infix/posfix，直接处理下一个运算符 */
            }
            /* 右操作数不是整数，回退到正常拼接 */
            luaK_infix(ls->fs, op, v);
            luaK_posfix(ls->fs, op, v, &v2, line);
          } else {
            luaK_infix(ls->fs, op, v);
            /* read sub-expression with higher priority */
            nextop = subexpr(ls, &v2, priority[op].right);
            luaK_posfix(ls->fs, op, v, &v2, line);
          }
          op = nextop;
          if (op == OPR_NOBINOPR && ls->t.token == TK_NAME && ls->t.linenumber == line &&
              is_infix_expr_start(luaX_lookahead(ls)) && is_same_line_infix(ls)) {
            op = OPR_INFIX;
          }
        }
      }
      leavelevel(ls);
      return op;  /* return first untreated operator */
  }

  /* 保存表达式起始行号，用于防止跨行中缀检测（必须在 simpleexp/前缀之前保存） */
  int expr_line = ls->linenumber;
  uop = getunopr(ls->t.token);
  LUA_LOGD("[PARSER] subexpr: uop=%d expr_line=%d", uop, expr_line);
  if (uop != OPR_NOUNOPR) {  /* prefix (unary) operator? */
    int line = ls->linenumber;
    luaX_next(ls);  /* skip operator */
    int saved_freereg = ls->fs->freereg;  /* 保存 await 之前的 freereg，作为结果寄存器 */
    subexpr(ls, v, UNARY_PRIORITY);
    if (uop == OPR_AWAIT) {
        /* await 表达式：编译为 OP_AWAIT 指令（纯语法级，不依赖 coroutine.yield）
         * OP_AWAIT A B: R[A] = await(R[B])
         *   result_reg     = 结果寄存器（与 adjustlocalvars 对齐）
         *   result_reg + 1 = Promise 参数 */
        FuncState *fs = ls->fs;
        int result_reg = saved_freereg;  /* 结果寄存器 == local 变量将被分配的寄存器 */

        /* 将 Promise 参数移到 result_reg + 1 */
        luaK_exp2reg(fs, v, result_reg + 1);

        /* OP_AWAIT result_reg, result_reg+1 — VM 直接处理，无需运行时查表 */
        luaK_codeABC(fs, OP_AWAIT, result_reg, result_reg + 1, 0);
        init_exp(v, VNONRELOC, result_reg);  /* 结果在 result_reg 中 */
        fs->freereg = result_reg + 1;  /* 只有结果存活 */
        luaK_fixline(fs, line);
    } else {
        luaK_prefix(ls->fs, uop, v, line);
    }
  }
  else {
    simpleexp(ls, v);
    LUA_LOGD("[PARSER] subexpr: simpleexp DONE, v->k=%d token=%d line=%d", v->k, ls->t.token, ls->linenumber);
  }
  /* expand while operators have priorities higher than 'limit' */
  op = getbinopr(ls->t.token);
  LUA_LOGD("[PARSER] subexpr: first op=%d limit=%d token=%d", op, limit, ls->t.token);
  /* 检测 as 安全类型转换运算符（必须在 infix 检测之前，避免被误识别为 infix 调用） */
  if (op == OPR_NOBINOPR && ls->t.token == TK_NAME &&
      strcmp(getstr(ls->t.seminfo.ts), "as") == 0 &&
      luaX_lookahead(ls) == TK_NAME) {
    op = OPR_AS;
  }
  /* 检测中缀函数调用: expr NAME expr => expr:NAME(expr)
     要求方法名与表达式起始在同一行，防止跨行误检测
     且要求expression不是已完成的函数调用(VCALL) */
  if (op == OPR_NOBINOPR && ls->t.token == TK_NAME &&
      ls->t.linenumber == expr_line && v->k != VCALL) {
    int la = luaX_lookahead(ls);
    if (is_infix_expr_start(la) && is_same_line_infix(ls)) {
      op = OPR_INFIX;  /* 有参中缀 */
    } else {
      /* 无参中缀调用: receiver method => receiver:method() */
      int is_noarg = (la == TK_EOS || ls->lookahead.linenumber != ls->t.linenumber);
      if (is_noarg) {
        int line = ls->t.linenumber;
        TString *method = ls->t.seminfo.ts;
        luaX_next(ls);  /* 跳过方法名 */
        {
          expdesc key;
          codestring(&key, method);
          luaK_self(ls->fs, v, &key);
        }
        int base = v->u.info;
        init_exp(v, VCALL, luaK_codeABC(ls->fs, OP_CALL, base, 2, 2));
        luaK_fixline(ls->fs, line);
        ls->fs->freereg = base + 1;
        /* 重新检测后续运算符（仅在同一行内继续链） */
        op = getbinopr(ls->t.token);
        /* 检测 as 安全类型转换（必须在 infix 检测之前） */
        if (op == OPR_NOBINOPR && ls->t.token == TK_NAME &&
            strcmp(getstr(ls->t.seminfo.ts), "as") == 0 &&
            luaX_lookahead(ls) == TK_NAME) {
          op = OPR_AS;
        }
        if (op == OPR_NOBINOPR && ls->t.token == TK_NAME && ls->t.linenumber == line &&
            is_infix_expr_start(luaX_lookahead(ls)) && is_same_line_infix(ls)) {
          op = OPR_INFIX;
        }
      }
    }
  }
  while (op != OPR_NOBINOPR && priority[op].left > limit) {
    expdesc v2;
    BinOpr nextop;
    int line = ls->linenumber;
    LUA_LOGD("[PARSER] subexpr LOOP op=%d prio_left=%d limit=%d token=%d line=%d", op, priority[op].left, limit, ls->t.token, ls->linenumber);
    if (op == OPR_INFIX) {
      /* 中缀函数调用: receiver NAME argument => receiver:NAME(argument) */
      TString *method = ls->t.seminfo.ts;
      luaX_next(ls);  /* 跳过方法名 */
      /* 设置方法调用: v 是 receiver, method 是方法名 */
      {
        expdesc key;
        codestring(&key, method);
        luaK_self(ls->fs, v, &key);
      }
      /* 解析参数表达式 */
      int old_ifx2 = ls->expr_flags;
      ls->expr_flags |= E_INFIX_ARG;
      nextop = subexpr(ls, &v2, priority[OPR_INFIX].right);
      ls->expr_flags = old_ifx2;
      /* 生成函数调用 */
      {
        int base = v->u.info;
        if (hasmultret(v2.k))
          luaK_setmultret(ls->fs, &v2);
        else {
          if (v2.k != VVOID)
            luaK_exp2nextreg(ls->fs, &v2);
        }
        int nparams = (v2.k == VVOID) ? 2 : (ls->fs->freereg - base);
        init_exp(v, VCALL, luaK_codeABC(ls->fs, OP_CALL, base, nparams, 2));
        luaK_fixline(ls->fs, line);
        ls->fs->freereg = base + 1;
      }
      op = nextop;
      /* 如果 nextop 返回中缀但当前 token 已跨行，取消中缀链 */
      if (op == OPR_INFIX && ls->t.linenumber != line) {
        op = OPR_NOBINOPR;
      }
      /* 继续检测中缀调用 */
      if (op == OPR_NOBINOPR && ls->t.token == TK_NAME && ls->t.linenumber == line &&
          is_infix_expr_start(luaX_lookahead(ls)) && is_same_line_infix(ls)) {
        op = OPR_INFIX;
      }
    } else {
      /* 范围操作符检测：'..' 前无空格且两端为整数常量时生成范围表 */
      int concat_nospace = (op == OPR_CONCAT) ? ls->t.nospace : 0;
      luaX_next(ls);  /* skip operator */
      if (op == OPR_CONCAT && concat_nospace && v->k == VKINT) {
        /* 范围操作符：先解析右操作数，若也是整数则生成范围表 */
        nextop = subexpr(ls, &v2, priority[op].right);
        if (v2.k == VKINT) {
          luaK_range(ls->fs, v, v->u.ival, v2.u.ival, line);
          op = nextop;
          /* 如果 nextop 返回中缀但当前 token 已跨行，取消中缀链 */
          if (op == OPR_INFIX && ls->t.linenumber != line) {
            op = OPR_NOBINOPR;
          }
          if (op == OPR_NOBINOPR && ls->t.token == TK_NAME && ls->t.linenumber == line &&
              is_infix_expr_start(luaX_lookahead(ls)) && is_same_line_infix(ls)) {
            op = OPR_INFIX;
          }
          continue;  /* 跳过 infix/posfix，直接处理下一个运算符 */
        }
        /* 右操作数不是整数，回退到正常拼接 */
        luaK_infix(ls->fs, op, v);
        luaK_posfix(ls->fs, op, v, &v2, line);
      } else {
        luaK_infix(ls->fs, op, v);
        /* is 运算符特殊处理：区分类型关键字和类名 */
        if (op == OPR_IS && is_type_token(ls->t.token) && ls->t.token != TK_NAME) {
          /* 类型关键字：int/float/bool/void/char/long → 字符串常量 → OP_IS */
          TString *ts = str_checkname(ls);
          codestring(&v2, ts);
          nextop = getbinopr(ls->t.token);
        } else {
          /* read sub-expression with higher priority */
          nextop = subexpr(ls, &v2, priority[op].right);
        }
        luaK_posfix(ls->fs, op, v, &v2, line);
      }
      op = nextop;
      /* 如果 nextop 返回中缀但当前 token 已跨行，取消中缀链 */
      if (op == OPR_INFIX && ls->t.linenumber != line) {
        op = OPR_NOBINOPR;
      }
      /* 检测中缀调用 */
      if (op == OPR_NOBINOPR && ls->t.token == TK_NAME && ls->t.linenumber == line &&
          is_infix_expr_start(luaX_lookahead(ls)) && is_same_line_infix(ls)) {
        op = OPR_INFIX;
      }
    }
  }
  LUA_LOGD("[PARSER] subexpr EXIT limit=%d nextop=%d token=%d", limit, op, ls->t.token);
  leavelevel(ls);
  return op;  /* return first untreated operator */
}


void expr (LexState *ls, expdesc *v) {
  LUA_LOGD("[PARSER] expr START, token=%d line=%d", ls->t.token, ls->linenumber);
  /* 裸单参箭头函数: x => expr（与 AST 解析器对齐）
     TK_MEAN 不是 TK_NAME，不会与中缀调用检测冲突 */
  if (ls->t.token == TK_NAME && luaX_lookahead(ls) == TK_MEAN) {
    int line = ls->linenumber;
    TString *pname = ls->t.seminfo.ts;
    luaX_next(ls); /* 跳过参数名 */
    luaX_next(ls); /* 跳过 '=>' */
    FuncState new_fs;
    BlockCnt bl;
    new_fs.f = addprototype(ls);
    new_fs.f->linedefined = line;
    open_func(ls, &new_fs, &bl);
    new_localvar(ls, pname);
    adjustlocalvars(ls, 1);
    new_fs.f->numparams = 1;
    luaK_reserveregs(&new_fs, 1);
    enterlevel(ls);
    retstat(ls);  /* 表达式体作为 return 值 */
    new_fs.freereg = new_fs.nactvar;
    leavelevel(ls);
    new_fs.f->lastlinedefined = ls->linenumber;
    codeclosure(ls, v);
    close_func(ls);
    return;
  }
  subexpr(ls, v, 0);
  LUA_LOGD("[PARSER] expr END, token=%d", ls->t.token);
  if (ls->t.token == '?') {
    /* printf("DEBUG: Ternary found at line %d\n", ls->linenumber); */
    int escape = NO_JUMP;
    int condition;
    int reg;
    FuncState *fs = ls->fs;

    luaX_next(ls); /* skip '?' */

    /* condition is in v */
    if (v->k == VNIL) v->k = VFALSE;
    luaK_goiftrue(fs, v);
    condition = v->f;

    /* true branch */
    int old_flags = ls->expr_flags;
    ls->expr_flags |= E_NO_COLON;
    expr(ls, v);
    ls->expr_flags = old_flags;
    luaK_exp2nextreg(fs, v);
    reg = v->u.info;

    luaK_concat(fs, &escape, luaK_jump(fs));
    luaK_patchtohere(fs, condition);

    checknext(ls, ':');

    /* false branch */
    expr(ls, v);
    luaK_exp2reg(fs, v, reg);

    luaK_patchtohere(fs, escape);
  }
}

/*
** expr_nocase: parse an expression, but stop before '=>' (OPR_CASE).
** Used by switch expression / switch statement case value parsing
** to prevent the binary => operator from consuming the case arrow.
*/
void expr_nocase (LexState *ls, expdesc *v) {
  subexpr(ls, v, PRI_CASE);  /* limit >= 1 stops before OPR_CASE */
}


/*
** 条件表达式专用的 suffixedexp
** 与 suffixedexp 相同，但不将 '{' 作为函数调用参数
** 这样在 if cond {} 语法中，{} 会被正确解析为代码块而非函数调用
*/
static void cond_suffixedexp (LexState *ls, expdesc *v) {
  FuncState *fs = ls->fs;
  int line = ls->linenumber;
  primaryexp(ls, v);
  for (;;) {
    switch (ls->t.token) {
      case TK_OPTCHAIN: {  /* '?.' 可选链字段访问 */
        expdesc key;
        int reg;
        int jmp_skip;
        int idx;
        
        luaK_dischargevars(fs, v);
        luaK_exp2nextreg(fs, v); reg = v->u.info;
        
        luaK_codeABCk(fs, OP_TESTNIL, reg, reg, 0, 1);
        jmp_skip = luaK_jump(fs);
        
        luaX_next(ls);  /* 跳过 '?.' */
        
        if (ls->t.token == TK_NAME) {
          codename(ls, &key);
        }
        else {
          /* 处理关键字作为字段名：所有保留字的seminfo.ts已由词法分析器设置 */
          TString *ts = ls->t.seminfo.ts;
          if (ts == NULL) {
            error_expected(ls, TK_NAME);
          }
          codestring(&key, ts);
          luaX_next(ls);
        }
        
        v->k = VNONRELOC;
        v->u.info = reg;
        luaK_indexed(fs, v, &key);
        idx = v->u.ind.idx;
        
        luaK_codeABC(fs, OP_GETFIELD, reg, reg, idx);
        
        luaK_patchtohere(fs, jmp_skip);
        
        v->k = VNONRELOC;
        v->u.info = reg;
        v->t = NO_JUMP;
        v->f = NO_JUMP;
        break;
      }
      case '.': {  /* fieldsel */
        fieldsel(ls, v);
        break;
      }
      case '[': {  /* '[' exp ']' 或切片语法 */
        yindex_or_slice(ls, v);
        break;
      }
      case ':': {  /* ':' NAME funcargs */
        expdesc key;
        luaX_next(ls);
        codename(ls, &key);
        luaK_self(fs, v, &key);
        funcargs(ls, v, line);
        break;
      }
      case '(': case TK_STRING: case TK_RAWSTRING: {  /* funcargs - 注意：不包含 '{' */
        luaK_exp2nextreg(fs, v);
        funcargs(ls, v, line);
        break;
      }
      /* 注意：条件表达式中不处理 '{' 作为函数调用，
         这样 if cond {} 中的 {} 会被识别为代码块 */
      default: return;  /* 遇到其他 token（包括 '{'）就停止 */
    }
  }
}


/*
** 条件表达式专用的 simpleexp
** 与 simpleexp 相同，但使用 cond_suffixedexp
*/
static void cond_simpleexp (LexState *ls, expdesc *v) {
  switch (ls->t.token) {
    case TK_FLT: {
      init_exp(v, VKFLT, 0);
      v->u.nval = ls->t.seminfo.r;
      luaX_next(ls);
      break;
    }
    case TK_INT: {
      init_exp(v, VKINT, 0);
      v->u.ival = ls->t.seminfo.i;
      luaX_next(ls);
      break;
    }
    case TK_NIL: {
      init_exp(v, VNIL, 0);
      luaX_next(ls);
      break;
    }
    case TK_TRUE: {
      init_exp(v, VTRUE, 0);
      luaX_next(ls);
      break;
    }
    case TK_FALSE: {
      init_exp(v, VFALSE, 0);
      luaX_next(ls);
      break;
    }
    case TK_DOTS: {  /* vararg or spread operator */
      FuncState *fs = ls->fs;
      int dots_line = ls->linenumber;  /* 记录 '...' 所在行号 */
      int la = luaX_lookahead(ls);
      /*
      ** 展开运算符要求 '...' 和后续表达式必须在同一行。
      ** 如果跨行（如 varargs 赋值后换行），按标准 varargs 处理，
      ** 避免误将下一行的标识符当作展开目标。
      */
      if ((la == TK_NAME || la == '(' || la == '{' || la == TK_STRING || la == TK_RAWSTRING || la == TK_INTERPSTRING || la == TK_INT || la == TK_FLT || la == TK_TRUE || la == TK_FALSE || la == TK_NIL || la == '-' || la == TK_NOT || la == '#' || la == '~' || la == TK_FUNCTION || la == TK_LAMBDA) && ls->linenumber == dots_line) {
        luaX_next(ls); /* skip '...' */

        /* Generate: table.unpack(expr) */
        expdesc table_var;
        singlevaraux(fs, luaS_newliteral(ls->L, "table"), &table_var, 1);
        if (table_var.k == VVOID) {
          expdesc key;
          singlevaraux(fs, ls->envn, &table_var, 1);
          codestring(&key, luaS_newliteral(ls->L, "table"));
          luaK_indexed(fs, &table_var, &key);
        }
        luaK_exp2anyregup(fs, &table_var);

        expdesc unpack_key;
        codestring(&unpack_key, luaS_newliteral(ls->L, "unpack"));
        luaK_indexed(fs, &table_var, &unpack_key);

        luaK_exp2nextreg(fs, &table_var);
        int func_reg = table_var.u.info;

        expdesc arg;
        expr(ls, &arg); /* Parse the expression to spread */
        luaK_exp2nextreg(fs, &arg);

        init_exp(v, VCALL, luaK_codeABC(fs, OP_CALL, func_reg, 2, 0)); /* 1 arg, multiple returns */
        fs->freereg = func_reg + 1;
      } else {
        check_condition(ls, fs->f->is_vararg, "cannot use '...' outside a vararg function");
        init_exp(v, VVARARG, luaK_codeABC(fs, OP_VARARG, 0, 0, 1));
        luaX_next(ls);
      }
      break;
    }
    case TK_STRING:
    case TK_RAWSTRING: {
      codestring(v, ls->t.seminfo.ts);
      luaX_next(ls);
      break;
    }
    default: {
      cond_suffixedexp(ls, v);
      return;
    }
  }
}


/*
** 条件表达式专用的 subexpr
** 与 subexpr 相同，但使用 cond_simpleexp
*/
static BinOpr cond_subexpr (LexState *ls, expdesc *v, int limit) {
  BinOpr op;
  UnOpr uop;
  enterlevel(ls);
  /* 保存表达式起始行号，用于防止跨行中缀检测 */
  int cond_expr_line = ls->linenumber;
  uop = getunopr(ls->t.token);
  if (uop != OPR_NOUNOPR) {  /* prefix (unary) operator? */
    int line = ls->linenumber;
    luaX_next(ls);  /* skip operator */
    cond_subexpr(ls, v, UNARY_PRIORITY);
    luaK_prefix(ls->fs, uop, v, line);
  }
  else cond_simpleexp(ls, v);
  /* expand while operators have priorities higher than 'limit' */
  op = getbinopr(ls->t.token);
  /* 检测 as 安全类型转换运算符（必须在 infix 检测之前） */
  if (op == OPR_NOBINOPR && ls->t.token == TK_NAME &&
      strcmp(getstr(ls->t.seminfo.ts), "as") == 0 &&
      luaX_lookahead(ls) == TK_NAME) {
    op = OPR_AS;
  }
  /* 检测中缀函数调用 */
  if (op == OPR_NOBINOPR && ls->t.token == TK_NAME &&
      ls->t.linenumber == cond_expr_line) {
    int la = luaX_lookahead(ls);
    if (is_infix_expr_start(la) && is_same_line_infix(ls)) {
      op = OPR_INFIX;  /* 有参中缀 */
    } else {
      /* 无参中缀调用: receiver method => receiver:method() */
      int is_noarg = (la == TK_EOS || ls->lookahead.linenumber != ls->t.linenumber);
      if (is_noarg) {
        int line = ls->t.linenumber;
        TString *method = ls->t.seminfo.ts;
        luaX_next(ls);  /* 跳过方法名 */
        {
          expdesc key;
          codestring(&key, method);
          luaK_self(ls->fs, v, &key);
        }
        int base = v->u.info;
        init_exp(v, VCALL, luaK_codeABC(ls->fs, OP_CALL, base, 2, 2));
        luaK_fixline(ls->fs, line);
        ls->fs->freereg = base + 1;
        /* 重新检测后续运算符（仅在同一行内继续链） */
        op = getbinopr(ls->t.token);
        if (op == OPR_NOBINOPR && ls->t.token == TK_NAME && ls->t.linenumber == line &&
            is_infix_expr_start(luaX_lookahead(ls)) && is_same_line_infix(ls)) {
          op = OPR_INFIX;
        }
      }
    }
  }
  while (op != OPR_NOBINOPR && priority[op].left > limit) {
    expdesc v2;
    BinOpr nextop;
    int line = ls->linenumber;
    if (op == OPR_INFIX) {
      TString *method = ls->t.seminfo.ts;
      luaX_next(ls);  /* 跳过方法名 */
      {
        expdesc key;
        codestring(&key, method);
        luaK_self(ls->fs, v, &key);
      }
      int old_ifx3 = ls->expr_flags;
      ls->expr_flags |= E_INFIX_ARG;
      nextop = cond_subexpr(ls, &v2, priority[OPR_INFIX].right);
      ls->expr_flags = old_ifx3;
      {
        int base = v->u.info;
        if (hasmultret(v2.k))
          luaK_setmultret(ls->fs, &v2);
        else {
          if (v2.k != VVOID)
            luaK_exp2nextreg(ls->fs, &v2);
        }
        int nparams = (v2.k == VVOID) ? 2 : (ls->fs->freereg - base);
        init_exp(v, VCALL, luaK_codeABC(ls->fs, OP_CALL, base, nparams, 2));
        luaK_fixline(ls->fs, line);
        ls->fs->freereg = base + 1;
      }
      op = nextop;
      /* 如果 nextop 返回中缀但当前 token 已跨行，取消中缀链 */
      if (op == OPR_INFIX && ls->t.linenumber != line) {
        op = OPR_NOBINOPR;
      }
      if (op == OPR_NOBINOPR && ls->t.token == TK_NAME && ls->t.linenumber == line &&
          is_infix_expr_start(luaX_lookahead(ls)) && is_same_line_infix(ls)) {
        op = OPR_INFIX;
      }
    } else {
      luaX_next(ls);  /* skip operator */
      luaK_infix(ls->fs, op, v);
      /* read sub-expression with higher priority */
      nextop = cond_subexpr(ls, &v2, priority[op].right);
      luaK_posfix(ls->fs, op, v, &v2, line);
      op = nextop;
      /* 如果 nextop 返回中缀但当前 token 已跨行，取消中缀链 */
      if (op == OPR_INFIX && ls->t.linenumber != line) {
        op = OPR_NOBINOPR;
      }
      if (op == OPR_NOBINOPR && ls->t.token == TK_NAME && ls->t.linenumber == line &&
          is_infix_expr_start(luaX_lookahead(ls)) && is_same_line_infix(ls)) {
        op = OPR_INFIX;
      }
    }
  }
  leavelevel(ls);
  return op;  /* return first untreated operator */
}


/*
** 条件表达式解析
** 用于 if/while/until 等控制结构的条件部分
** 与 expr 的区别是不将 '{' 作为函数调用参数
** 这样 if cond {} 语法中的 {} 会被正确解析为代码块
*/
static void cond_expr (LexState *ls, expdesc *v) {
  cond_subexpr(ls, v, 0);
  if (ls->t.token == '?') {
    FuncState *fs = ls->fs;
    int escape = NO_JUMP;
    int reg;

    luaK_goiftrue(fs, v);
    int cond_jmp = v->f;
    v->f = NO_JUMP;
    v->t = NO_JUMP;

    luaX_next(ls); /* skip '?' */

    expdesc v2;
    cond_expr(ls, &v2); /* parse true branch */

    luaK_exp2nextreg(fs, &v2);
    reg = v2.u.info;

    luaK_concat(fs, &escape, luaK_jump(fs));

    checknext(ls, ':');

    luaK_patchtohere(fs, cond_jmp);

    expdesc v3;
    cond_expr(ls, &v3); /* parse false branch */
    luaK_exp2reg(fs, &v3, reg);

    luaK_patchtohere(fs, escape);

    init_exp(v, VNONRELOC, reg);
  }
}

/* }========================================================= */



/*
** {===========================================================
** Rules for Statements
** ============================================================
*/


static void block (LexState *ls) {
  /* block -> statlist */
  FuncState *fs = ls->fs;
  BlockCnt bl;
  enterblock(fs, &bl, 0);
  statlist(ls);
  leaveblock(fs);
}


/*
** structure to chain all variables in the left-hand side of an
** assignment
*/
struct LHS_assign {
  struct LHS_assign *prev;
  expdesc v;  /* variable (global, local, upvalue, or indexed) */
};


/*
** check whether, in an assignment to an upvalue/local variable, the
** upvalue/local variable is begin used in a previous assignment to a
** table. If so, save original upvalue/local value in a safe place and
** use this safe copy in the previous assignment.
*/
static void check_conflict (LexState *ls, struct LHS_assign *lh, expdesc *v) {
  FuncState *fs = ls->fs;
  lu_byte extra = fs->freereg;  /* eventual position to save local variable */
  int conflict = 0;
  for (; lh; lh = lh->prev) {  /* check all previous assignments */
    if (vkisindexed(lh->v.k)) {  /* assignment to table field? */
      if (lh->v.k == VINDEXUP) {  /* is table an upvalue? */
        if (v->k == VUPVAL && lh->v.u.ind.t == v->u.info) {
          conflict = 1;  /* table is the upvalue being assigned now */
          lh->v.k = VINDEXSTR;
          lh->v.u.ind.t = extra;  /* assignment will use safe copy */
        }
      }
      else {  /* table is a register */
        if (v->k == VLOCAL && lh->v.u.ind.t == v->u.var.ridx) {
          conflict = 1;  /* table is the local being assigned now */
          lh->v.u.ind.t = extra;  /* assignment will use safe copy */
        }
        /* is index the local being assigned? */
        if (lh->v.k == VINDEXED && v->k == VLOCAL &&
            lh->v.u.ind.idx == v->u.var.ridx) {
          conflict = 1;
          lh->v.u.ind.idx = extra;  /* previous assignment will use safe copy */
        }
      }
    }
  }
  if (conflict) {
    /* copy upvalue/local value to a temporary (in position 'extra') */
    if (v->k == VLOCAL)
      luaK_codeABC(fs, OP_MOVE, extra, v->u.var.ridx, 0);
    else
      luaK_codeABC(fs, OP_GETUPVAL, extra, v->u.info, 0);
    luaK_reserveregs(fs, 1);
  }
}


/* Create code to store the "top" register in 'var' */
static void storevartop (FuncState *fs, expdesc *var) {
  expdesc e;
  init_exp(&e, VNONRELOC, fs->freereg - 1);
  luaK_storevar(fs, var, &e);  /* will also free the top register */
}


/*
** Parse and compile a multiple assignment. The first "variable"
** (a 'suffixedexp') was already read by the caller.
**
** assignment -> suffixedexp restassign
** restassign -> ',' suffixedexp restassign | '=' explist
*/
static void restassign (LexState *ls, struct LHS_assign *lh, int nvars) {
  expdesc e;
  check_condition(ls, vkisvar(lh->v.k), "syntax error");
  check_readonly(ls, &lh->v);
  if (testnext(ls, ',')) {  /* restassign -> ',' suffixedexp restassign */
    struct LHS_assign nv;
    nv.prev = lh;
    suffixedexp(ls, &nv.v);
    if (!vkisindexed(nv.v.k))
      check_conflict(ls, lh, &nv.v);
    enterlevel(ls);  /* control recursion depth */
    restassign(ls, &nv, nvars+1);
    leavelevel(ls);
  }
  else {  /* restassign -> '=' explist */
    int nexps;
    checknext(ls, '=');

    /* 多元连等赋值: a = b = c = 10 */
    if (ls->t.token == TK_NAME && luaX_lookahead(ls) == '=') {
      expdesc chain[64];
      int nchain = 0;
      chain[nchain] = lh->v;
      nchain++;

      for (;;) {
        if (ls->t.token != TK_NAME) break;
        suffixedexp(ls, &chain[nchain]);
        check_condition(ls, vkisvar(chain[nchain].k), "syntax error");
        check_readonly(ls, &chain[nchain]);
        nchain++;
        if (ls->t.token != '=') break;
        luaX_next(ls);
        if (nchain >= 64) break;
      }

      nexps = explist(ls, &e);
      if (nexps != 1)
        adjust_assign(ls, 1, nexps, &e);
      else
        luaK_setoneret(ls->fs, &e);

      luaK_exp2anyreg(ls->fs, &e);
      int rhs_reg = e.u.info;

      for (int i = nchain - 1; i >= 0; i--) {
        int new_reg = ls->fs->freereg;
        luaK_reserveregs(ls->fs, 1);
        luaK_codeABC(ls->fs, OP_MOVE, new_reg, rhs_reg, 0);
        expdesc copy;
        init_exp(&copy, VNONRELOC, new_reg);
        luaK_storevar(ls->fs, &chain[i], &copy);
      }
      ls->fs->freereg = rhs_reg;
      return;
    }

    nexps = explist(ls, &e);
    if (nexps != nvars)
      adjust_assign(ls, nvars, nexps, &e);
    else {
      luaK_setoneret(ls->fs, &e);  /* close last expression */
      luaK_storevar(ls->fs, &lh->v, &e);
      return;  /* avoid default */
    }
  }
  init_exp(&e, VNONRELOC, ls->fs->freereg-1);  /* default assignment */
  luaK_storevar(ls->fs, &lh->v, &e);
}


/*
** 解析条件表达式
*/
static int cond (LexState *ls) {
  /* cond -> exp */
  expdesc v;
  expr(ls, &v);  /* read condition */
  if (v.k == VNIL) v.k = VFALSE;  /* 'falses' are all equal here */
  luaK_goiftrue(ls->fs, &v);
  return v.f;
}


static void gotostat (LexState *ls) {
  FuncState *fs = ls->fs;
  int line = ls->linenumber;
  if (ls->t.token == TK_CONTINUE) {
    luaX_next(ls);
    newgotoentry(ls, luaS_newliteral(ls->L, "continue"), line, luaK_jump(fs));
    return;
  }
  TString *name = str_checkname(ls);  /* label's name */
  Labeldesc *lb = findlabel(ls, name);
  if (lb == NULL)  /* no label? */
    /* forward jump; will be resolved when the label is declared */
    newgotoentry(ls, name, line, luaK_jump(fs));
  else {  /* found a label */
    /* backward jump; will be resolved here */
    int lblevel = reglevel(fs, lb->nactvar);  /* label level */
    if (luaY_nvarstack(fs) > lblevel)  /* leaving the scope of a variable? */
      luaK_codeABC(fs, OP_CLOSE, lblevel, 0, 0);
    /* create jump and link it to the label */
    luaK_patchlist(fs, luaK_jump(fs), lb->pc);
  }
}


/*
** Break statement. Semantically equivalent to "goto break".
*/
static void breakstat (LexState *ls) {
  int line = ls->linenumber;
  int temp = ls->t.token;
  luaX_next(ls);  /* skip break */
  /* 多层级 break N / continue N（与 AST 解析器对齐） */
  int level = 1;
  if (ls->t.token == TK_INT) {
    level = (int)ls->t.seminfo.i;
    if (level < 1) level = 1;
    luaX_next(ls);
  }
  if (level > 1) {
    if (level > ls->loop_depth) {
      luaK_semerror(ls, luaO_pushfstring(ls->L,
        "%s level %d exceeds loop depth %d",
        (temp == TK_BREAK) ? "break" : "continue", level, ls->loop_depth));
    }
    if (temp == TK_BREAK) {
      /* 目标循环的绝对深度；跳转到其隐藏标签 __lpbrk<D>（由该循环结束时创建） */
      int target_depth = ls->loop_depth - level + 1;
      const char *fmt = luaO_pushfstring(ls->L, "__lpbrk%d", target_depth);
      TString *name = luaS_new(ls->L, fmt);
      ls->L->top.p--;  /* 字符串已 intern，弹出 pushfstring 的栈值 */
      newgotoentry(ls, name, line, luaK_jump(ls->fs));
    } else {
      /* continue N：跳转到目标循环体内的 continue 标签（由循环体创建） */
      newgotoentry(ls, luaS_newliteral(ls->L, "continue"), line, luaK_jump(ls->fs));
    }
    return;
  }
  if(temp==TK_BREAK) {
      newgotoentry(ls, luaS_newliteral(ls->L, "break"), line, luaK_jump(ls->fs));
  }else if(temp==TK_CONTINUE){
      newgotoentry(ls, luaS_newliteral(ls->L, "continue"), line, luaK_jump(ls->fs));
  }
}


/*
** 循环结束时注册该层循环的隐藏 break 标签 __lpbrk<depth>，
** 仅当存在指向它的待定 goto 时才创建，解析 break N 跳转
*/
static void lp_exit_loop (LexState *ls) {
  int depth = ls->loop_depth;
  const char *fmt = luaO_pushfstring(ls->L, "__lpbrk%d", depth);
  TString *name = luaS_new(ls->L, fmt);
  ls->L->top.p--;  /* 字符串已 intern，弹出 pushfstring 的栈值 */
  Labellist *gl = &ls->dyd->gt;
  int i;
  for (i = 0; i < gl->n; i++) {
    if (gl->arr[i].name == name) {
      createlabel(ls, name, 0, 0);
      break;
    }
  }
  ls->loop_depth--;
}

/*
** Check whether there is already a label with the given 'name'.
*/
static void checkrepeated (LexState *ls, TString *name) {
  Labeldesc *lb = findlabel(ls, name);
  if (l_unlikely(lb != NULL)) {  /* already defined? */
    const char *msg = "label '%s' already defined on line %d";
    msg = luaO_pushfstring(ls->L, msg, getstr(name), lb->line);
    luaK_semerror(ls, msg);  /* error */
  }
}


static void labelstat (LexState *ls, TString *name, int line) {
  /* label -> '::' NAME '::' */
  checknext(ls, TK_DBCOLON);  /* skip double colon */
  while (ls->t.token == ';' || ls->t.token == TK_DBCOLON)
    statement(ls);  /* skip other no-op statements */
  checkrepeated(ls, name);  /* check for repeated labels */
  createlabel(ls, name, line, block_follow(ls, 0));
}


static int is_stmt_terminator (int token);

static void whilestat (LexState *ls, int line) {
  /* whilestat -> WHILE cond DO block END | WHILE let NAME {',' NAME} '=' explist DO block END */
  FuncState *fs = ls->fs;
  int whileinit;
  int condexit;
  BlockCnt bl;
  
  if (luaX_lookahead(ls) == TK_LET) {
    int nvars = 1;
    int nexps;
    expdesc e;
    
    luaX_next(ls);  /* skip WHILE */
    luaX_next(ls);  /* skip let */
    
    ls->loop_depth++;  /* break N 循环深度跟踪 */
    whileinit = luaK_getlabel(fs);
    
    /* Open loop block encompassing let condition + loop body */
    enterblock(fs, &bl, 1);
    
    new_localvar(ls, str_checkname(ls));
    while (testnext(ls, ',')) {
      new_localvar(ls, str_checkname(ls));
      nvars++;
    }
    checknext(ls, '=');
    
    nexps = explist(ls, &e);
    adjust_assign(ls, nvars, nexps, &e);
    adjustlocalvars(ls, nvars);
    
    expdesc cond_v;
    init_exp(&cond_v, VLOCAL, fs->nactvar - nvars);
    luaK_goiftrue(fs, &cond_v);
    condexit = cond_v.f;
    
    if (ls->t.token == TK_DO) luaX_next(ls);
    
    /* Parse the statements inside loop without creating a separate block layer
       so the let variables are visible */
    while (!is_stmt_terminator(ls->t.token) && ls->t.token != TK_EOS && ls->t.token != TK_END) {
        statement(ls);
    }
    
    createlabel(ls, luaS_newliteral(ls->L, "continue"), 0, 0);
    luaK_jumpto(fs, whileinit);
    luaK_patchtohere(fs, condexit);
    if (testnext(ls, TK_ELSE)) {
        while (!is_stmt_terminator(ls->t.token) && ls->t.token != TK_EOS && ls->t.token != TK_END) {
            statement(ls);
        }
    }
    check_match(ls, TK_END, TK_WHILE, line);
    lp_exit_loop(ls);
    leaveblock(fs);  /* leaves loop block, discarding locals */
  } else {
    luaX_next(ls);  /* skip WHILE */
    ls->loop_depth++;  /* break N 循环深度跟踪 */
    whileinit = luaK_getlabel(fs);
    condexit = cond(ls);
    enterblock(fs, &bl, 1);
    if (ls->t.token == TK_DO) luaX_next(ls);
    block(ls);
    createlabel(ls, luaS_newliteral(ls->L, "continue"), 0, 0);
    luaK_jumpto(fs, whileinit);
    luaK_patchtohere(fs, condexit);  /* false conditions finish the loop */
    if (testnext(ls, TK_ELSE)) {
        block(ls);
    }
    check_match(ls, TK_END, TK_WHILE, line);
    lp_exit_loop(ls);
    leaveblock(fs);
  }
}


static void repeatstat (LexState *ls, int line) {
  /* repeatstat -> REPEAT block UNTIL cond */
  int condexit;
  FuncState *fs = ls->fs;
  int repeat_init = luaK_getlabel(fs);
  BlockCnt bl1, bl2;
  ls->loop_depth++;  /* break N 循环深度跟踪 */
  enterblock(fs, &bl1, 1);  /* loop block */
  enterblock(fs, &bl2, 0);  /* scope block */
  luaX_next(ls);  /* skip REPEAT */
  statlist(ls);
  createlabel(ls, luaS_newliteral(ls->L, "continue"), 0, 0);
  check_match(ls, TK_UNTIL, TK_REPEAT, line);
  condexit = cond(ls);  /* read condition (inside scope block) */
  leaveblock(fs);  /* finish scope */
  if (bl2.upval) {  /* upvalues? */
    int exit = luaK_jump(fs);  /* normal exit must jump over fix */
    luaK_patchtohere(fs, condexit);  /* repetition must close upvalues */
    luaK_codeABC(fs, OP_CLOSE, reglevel(fs, bl2.nactvar), 0, 0);
    condexit = luaK_jump(fs);  /* repeat after closing upvalues */
    luaK_patchtohere(fs, exit);  /* normal exit comes to here */
  }
  luaK_patchlist(fs, condexit, repeat_init);  /* close the loop */
  lp_exit_loop(ls);
  leaveblock(fs);  /* finish loop */
}


/*
** Read an expression and generate code to put its results in next
** stack slot.
**
*/
static void exp1 (LexState *ls) {
  expdesc e;
  expr(ls, &e);
  luaK_exp2nextreg(ls->fs, &e);
  lua_assert(e.k == VNONRELOC);
}


/*
** Fix for instruction at position 'pc' to jump to 'dest'.
** (Jump addresses are relative in Lua). 'back' true means
** a back jump.
*/
static void fixforjump (FuncState *fs, int pc, int dest, int back) {
  Instruction *jmp = &fs->f->code[pc];
  int offset = dest - (pc + 1);
  if (back)
    offset = -offset;
  if (l_unlikely(offset > MAXARG_Bx))
    luaX_syntaxerror(fs->ls, "control structure too long");
  SETARG_Bx(*jmp, offset);
}


/*
** Generate code for a 'for' loop.
*/
static void forbody (LexState *ls, int base, int line, int nvars, int isgen) {
  /* forbody -> DO block */
  static const OpCode forprep[2] = {OP_FORPREP, OP_TFORPREP};
  static const OpCode forloop[2] = {OP_FORLOOP, OP_TFORLOOP};
  BlockCnt bl;
  FuncState *fs = ls->fs;
  int prep, endfor;
  if (ls->t.token == TK_DO) luaX_next(ls);
  prep = luaK_codeABx(fs, forprep[isgen], base, 0);
  enterblock(fs, &bl, 0);  /* scope for declared variables */
  adjustlocalvars(ls, nvars);
  luaK_reserveregs(fs, nvars);
  block(ls);
  createlabel(ls, luaS_newliteral(ls->L, "continue"), 0, 0);
  leaveblock(fs);  /* end of scope for declared variables */
  fixforjump(fs, prep, luaK_getlabel(fs), 0);
  if (isgen) {  /* generic for? */
    luaK_codeABC(fs, OP_TFORCALL, base, 0, nvars);
    luaK_fixline(fs, line);
  }
  endfor = luaK_codeABx(fs, forloop[isgen], base, 0);
  fixforjump(fs, endfor, prep + 1, 1);
  luaK_fixline(fs, line);
}


static void fornum (LexState *ls, TString *varname, int line) {
  /* fornum -> NAME = exp,exp[,exp] forbody */
  FuncState *fs = ls->fs;
  int base = fs->freereg;
  new_localvarliteral(ls, "(for state)");
  new_localvarliteral(ls, "(for state)");
  new_localvarliteral(ls, "(for state)");
  new_varkind(ls, varname, RDKCONST);  /* 控制变量设为只读常量 */
  checknext(ls, '=');
  exp1(ls);  /* initial value */
  checknext(ls, ',');
  exp1(ls);  /* limit */
  if (testnext(ls, ','))
    exp1(ls);  /* optional step */
  else {  /* default step = 1 */
    luaK_int(fs, fs->freereg, 1);
    luaK_reserveregs(fs, 1);
  }
  adjustlocalvars(ls, 3);  /* control variables */
  forbody(ls, base, line, 1, 0);
}


static void forlist (LexState *ls, TString *indexname) {
  /* forlist -> NAME {,NAME} IN explist forbody */
  FuncState *fs = ls->fs;
  expdesc e;
  int nvars = 5;  /* gen, state, control, toclose, 'indexname' */
  int line;
  int base = fs->freereg;
  /* create control variables */
  new_localvarliteral(ls, "(for state)");
  new_localvarliteral(ls, "(for state)");
  new_localvarliteral(ls, "(for state)");
  new_localvarliteral(ls, "(for state)");
  /* create declared variables */
  new_varkind(ls, indexname, RDKCONST);  /* 控制变量设为只读常量 */
  while (testnext(ls, ',')) {
    new_localvar(ls, str_checkname(ls));
    nvars++;
  }
  if (ls->t.token == TK_IN) luaX_next(ls);
  line = ls->linenumber;
  adjust_assign(ls, 4, explist(ls, &e), &e);
  adjustlocalvars(ls, 4);  /* control variables */
  marktobeclosed(fs);  /* last control var. must be closed */
  luaK_checkstack(fs, 3);  /* extra space to call generator */
  forbody(ls, base, line, nvars - 4, 1);
}


static void forstat (LexState *ls, int line) {
  /* forstat -> FOR (fornum | forlist) END */
  FuncState *fs = ls->fs;
  TString *varname;
  BlockCnt bl;
  ls->loop_depth++;  /* break N 循环深度跟踪 */
  enterblock(fs, &bl, 1);  /* scope for loop and control variables */
  luaX_next(ls);  /* skip 'for' */
  varname = str_checkname(ls);  /* first variable name */
  switch (ls->t.token) {
    case '=': fornum(ls, varname, line); break;
    case ',': case TK_IN: forlist(ls, varname); break;
    default: luaX_syntaxerror(ls, "'=' or 'in' expected");
  }
  if (testnext(ls, TK_ELSE)) {
    block(ls);
  }
  check_match(ls, TK_END, TK_FOR, line);
  lp_exit_loop(ls);
  leaveblock(fs);  /* loop scope ('break' jumps to this point) */
}


/*
** 解析 if/elseif 条件块
** 语法支持两种形式：
**   1. 传统形式: if cond then block
**   2. 大括号形式: if cond { block }
** 
** 参数：
**   ls - 词法状态
**   escapelist - 跳出列表
** 返回值：
**   1 如果使用大括号语法，0 否则
*/
static int test_let_then_block (LexState *ls, int *escapelist) {
  /* test_let_then_block -> let NAME {',' NAME} '=' explist [THEN | '{'] block ['}'] */
  BlockCnt bl;
  FuncState *fs = ls->fs;
  int jf;  /* instruction to skip 'then' code (if condition is false) */
  int use_brace = 0;  /* 是否使用大括号语法 */
  int nvars = 1;
  int nexps;
  expdesc e;
  
  checknext(ls, TK_LET);  /* skip let */
  
  /* open a new block for the 'let' variables.
     This block encompasses both the assignment and the 'then' block. */
  enterblock(fs, &bl, 0);

  /* parse variables */
  new_localvar(ls, str_checkname(ls));
  while (testnext(ls, ',')) {
    new_localvar(ls, str_checkname(ls));
    nvars++;
  }
  
  checknext(ls, '=');
  
  /* parse expressions and assign to variables */
  nexps = explist(ls, &e);
  adjust_assign(ls, nvars, nexps, &e);
  adjustlocalvars(ls, nvars);
  
  /* Create condition based on the first let variable */
  expdesc cond_v;
  init_exp(&cond_v, VLOCAL, fs->nactvar - nvars);
  
  /* 检查是否是大括号语法 */
  if (ls->t.token == '{') {
    use_brace = 1;
    luaX_next(ls);  /* skip '{' */
  } else if (ls->t.token == TK_THEN) {
    luaX_next(ls);  /* skip 'then' */
  } else if (ls->t.token == TK_DO) {
    luaX_next(ls);  /* skip 'do' (Universal Block Opener) */
  }

  /* Evaluate the first variable as a boolean condition */
  luaK_goiftrue(fs, &cond_v);  /* skip over block if condition is false */
  jf = cond_v.f;

  /* The actual block execution */
  if (use_brace) {
    while (ls->t.token != '}' && ls->t.token != TK_EOS) {
      statement(ls);
    }
    checknext(ls, '}');
  } else {
    /* statlist loop */
    while (!is_stmt_terminator(ls->t.token) && ls->t.token != TK_EOS && ls->t.token != TK_ELSE && ls->t.token != TK_ELSEIF) {
       statement(ls);
    }
  }

  /* Must leave the block to clean up the locals BEFORE patching jumps so `else` doesn't see them */
  leaveblock(fs);  /* end of 'let' block */

  if (ls->t.token == TK_ELSE || ls->t.token == TK_ELSEIF || ls->t.token == TK_CASE || ls->t.token == TK_WHEN)
    luaK_concat(fs, escapelist, luaK_jump(fs));  /* must jump over it */
  luaK_patchtohere(fs, jf);

  return use_brace;
}

static int test_then_block (LexState *ls, int *escapelist) {
  /* test_then_block -> cond [THEN | '{'] block ['}'] */
  BlockCnt bl;
  FuncState *fs = ls->fs;
  expdesc v;
  int jf;  /* instruction to skip 'then' code (if condition is false) */
  int use_brace = 0;  /* 是否使用大括号语法 */
  /* IF or ELSEIF has already been skipped by the caller (ifstat) */
  cond_expr(ls, &v);  /* read condition (使用 cond_expr 避免 { 被误解为函数调用) */
  
  /* 检查是否是大括号语法 */
  if (ls->t.token == '{') {
    use_brace = 1;
    luaX_next(ls);  /* skip '{' */
  } else if (ls->t.token == TK_THEN) {
    luaX_next(ls);  /* skip 'then' */
  } else if (ls->t.token == TK_DO) {
    luaX_next(ls);  /* skip 'do' (Universal Block Opener) */
  }
  
  if (ls->t.token == TK_BREAK||ls->t.token==TK_CONTINUE) {  /* 'if x then break' ? */
    int line = ls->linenumber;
    luaK_goiffalse(ls->fs, &v);  /* will jump if condition is true */
    if(ls->t.token==TK_BREAK) {
      int is_breakkw = 1;
      luaX_next(ls);  /* skip 'break' */
      enterblock(fs, &bl, 0);  /* must enter block before 'goto' */
      /* 支持 break N 多层级跳转（与 AST 解析器对齐） */
      if (is_breakkw && ls->t.token == TK_INT) {
        int level = (int)ls->t.seminfo.i;
        if (level < 1) level = 1;
        luaX_next(ls);
        if (level > 1) {
          if (level > ls->loop_depth) {
            luaK_semerror(ls, luaO_pushfstring(ls->L,
              "break level %d exceeds loop depth %d", level, ls->loop_depth));
          }
          {
            int target_depth = ls->loop_depth - level + 1;
            const char *fmt = luaO_pushfstring(ls->L, "__lpbrk%d", target_depth);
            TString *name = luaS_new(ls->L, fmt);
            ls->L->top.p--;  /* 字符串已 intern，弹出 pushfstring 的栈值 */
            newgotoentry(ls, name, line, v.t);
          }
        } else {
          newgotoentry(ls, luaS_newliteral(ls->L, "break"), line, v.t);
        }
      } else {
        newgotoentry(ls, luaS_newliteral(ls->L, "break"), line, v.t);
      }
    }else{
      enterblock(fs, &bl, 0);  /* must enter block before 'goto' */
      newgotoentry(ls, luaS_newliteral(ls->L, "continue"), line, v.t);
    }
    while (testnext(ls, ';')) {}  /* skip semicolons */
    if (block_follow(ls, 0) || (use_brace && ls->t.token == '}')) {  /* jump is the entire block? */
      leaveblock(fs);
      if (use_brace) checknext(ls, '}');
      return use_brace;  /* and that is it */
    }
    else  /* must skip over 'then' part if condition is false */
      jf = luaK_jump(fs);
  }
  else {  /* regular case (not a break) */
    luaK_goiftrue(ls->fs, &v);  /* skip over block if condition is false */
    enterblock(fs, &bl, 0);
    jf = v.f;
  }
  
  /* 解析块内容 */
  if (use_brace) {
    /* 大括号语法：解析到 '}' 结束 */
    while (ls->t.token != '}' && ls->t.token != TK_EOS) {
      statement(ls);
    }
    checknext(ls, '}');
  } else {
    statlist(ls);  /* 'then' part */
  }
  
  leaveblock(fs);
  if (ls->t.token == TK_ELSE ||
      ls->t.token == TK_ELSEIF)  /* followed by 'else'/'elseif'? */
    luaK_concat(fs, escapelist, luaK_jump(fs));  /* must jump over it */
  luaK_patchtohere(fs, jf);
  return use_brace;
}


/*
** if 语句解析
** 支持两种语法形式：
**   1. 传统形式: if cond then block {elseif cond then block} [else block] end
**   2. 大括号形式: if cond { block } {elseif cond { block }} [else { block }]
** 
** 参数：
**   ls - 词法状态
**   line - if 关键字所在行号
*/
static void ifstat (LexState *ls, int line) {
  /* ifstat -> IF cond [THEN|'{'] block {ELSEIF cond [THEN|'{'] block} [ELSE ['{'] block ['}']] [END] */
  FuncState *fs = ls->fs;
  int escapelist = NO_JUMP;  /* exit list for finished parts */
  int use_brace;
  
  luaX_next(ls);  /* skip IF */
  
  if (ls->t.token == TK_LET) {
    use_brace = test_let_then_block(ls, &escapelist);
  } else {
    use_brace = test_then_block(ls, &escapelist);  /* cond THEN block */
  }
  
  while (ls->t.token == TK_ELSEIF) {
    int elseif_brace;
    luaX_next(ls); /* skip ELSEIF */
    if (ls->t.token == TK_LET) {
      elseif_brace = test_let_then_block(ls, &escapelist);
    } else {
      elseif_brace = test_then_block(ls, &escapelist);  /* cond THEN block */
    }
    use_brace = use_brace || elseif_brace;
  }
  
  if (testnext(ls, TK_ELSE)) {
    /* else 部分 */
    if (use_brace && ls->t.token == '{') {
      /* else { block } */
      luaX_next(ls);  /* skip '{' */
      while (ls->t.token != '}' && ls->t.token != TK_EOS) {
        statement(ls);
      }
      checknext(ls, '}');
    } else {
      block(ls);  /* 'else' part */
    }
  }
  
  /* 只有传统语法需要 end */
  if (!use_brace) {
    check_match(ls, TK_END, TK_IF, line);
  }
  
  luaK_patchtohere(fs, escapelist);  /* patch escape list to 'if' end */
}


static void single_test_then_block (LexState *ls, int *escapelist) {
    /* test_then_block -> [IF | ELSEIF] cond THEN block */
    BlockCnt bl;
    int line;
    FuncState *fs = ls->fs;
    TString *jlb = NULL;
    int target = NO_JUMP;
    expdesc v;
    int jf;  /* instruction to skip 'then' code (if condition is false) */
    luaX_next(ls);  /* skip IF or ELSEIF */
    cond_expr(ls, &v);  /* read condition (使用 cond_expr 避免 { 被误解为函数调用) */
    line = ls->linenumber;
    testnext(ls, TK_THEN);  /* 可选的 'then'（与 AST 解析器对齐：when/case cond then ...） */
    if (ls->t.token == TK_GOTO || ls->t.token == TK_BREAK || ls->t.token == TK_CONTINUE) {
        luaK_goiffalse(ls->fs, &v);  /* will jump to label if condition is true */
        enterblock(fs, &bl, 0);  /* must enter block before 'goto' */
        gotostat(ls);  /* handle goto/break */
        leaveblock(fs);
        return;
    }
    else {  /* regular case (not a jump) */
        luaK_goiftrue(ls->fs, &v);  /* skip over block if condition is false */
        enterblock(fs, &bl, 0);
        jf = v.f;
    }
    /* 分支体：语句列表，直到 case/else/end（与 AST 解析器的块语义对齐） */
    while (ls->t.token != TK_CASE && ls->t.token != TK_ELSE &&
           ls->t.token != TK_END && ls->t.token != TK_EOS &&
           !block_follow(ls, 1)) {
      statement(ls);
    }
    leaveblock(fs);
    if (ls->t.token == TK_ELSE || ls->t.token == TK_CASE ||
        ls->t.token == TK_WHEN)  /* followed by 'else'/'elseif'? */
        luaK_concat(fs, escapelist, luaK_jump(fs));  /* must jump over it */
    luaK_patchtohere(fs, jf);
}

static void single_block (LexState *ls) {
    /* block -> statlist */
    FuncState *fs = ls->fs;
    BlockCnt bl;
    enterblock(fs, &bl, 0);
    statement(ls);
    leaveblock(fs);
}


static void single_ifstat (LexState *ls, int line) {
    /* ifstat -> IF cond THEN block {ELSEIF cond THEN block} [ELSE block] END */
    FuncState *fs = ls->fs;
    int escapelist = NO_JUMP;  /* exit list for finished parts */
    single_test_then_block(ls, &escapelist);  /* IF cond THEN block */
    if (testnext(ls,'`'))
        single_block(ls);  /* 'else' part */
    luaK_patchtohere(fs, escapelist);  /* patch escape list to 'if' end */
}


static void whenstat (LexState *ls, int line) {
    /* whenstat -> WHEN cond block {CASE cond block} [ELSE block] END */
    FuncState *fs = ls->fs;
    int escapelist = NO_JUMP;  /* exit list for finished parts */
    single_test_then_block(ls, &escapelist);  /* WHEN cond block */
    while (ls->t.token == TK_CASE)
        single_test_then_block(ls, &escapelist);  /* CASE cond block */
    if (testnext(ls, TK_ELSE))
        single_block(ls);  /* 'else' part */
    check_match(ls, TK_END, TK_WHEN, line);  /* 消费 'end' */
    luaK_patchtohere(fs, escapelist);  /* patch escape list to 'when' end */
}


//===================================== SWITCH =============================================
/*
** 解析单个模式匹配分支（支持多值、类型、范围、解构等模式）
** 参数：
**   ls - 词法分析状态
**   ctrl - 控制表达式（被匹配的值）
**   next_check_jump - 失败跳转链表（用于串联多个case的失败跳转）
** 说明：
**   支持的模式类型：
**   1. 通配符 _ ：匹配任意值
**   2. 变量绑定 name ：匹配任意值并绑定到局部变量
**   3. 表解构 {a, b, c} ：匹配表并解构字段
**   4. 类型模式 is TypeName ：检查值的类型
**   5. 范围模式 low..high ：检查值是否在范围内
**   6. 字面量模式 expr ：相等性比较
**   7. 多值模式 pat1, pat2, pat3 ：匹配任意一个模式（OR逻辑）
*/
static void parse_pattern(LexState *ls, expdesc *ctrl, int *next_check_jump, int *success_jump, int allow_multi) {
  FuncState *fs = ls->fs;
  int first_pattern = 1;
  int prev_false_jumps = NO_JUMP;  /* 前一个子模式的失败跳转（用于多值模式） */

  /* 循环处理多值模式（逗号分隔） */
  do {
    if (!first_pattern) {
      luaX_next(ls); /* skip ',' */
      /* 确保 ctrl 保持 VNONRELOC 状态 */
      ctrl->k = VNONRELOC;
      /* 将前一个子模式的失败跳转修补到当前位置（即下一个子模式检查的开始） */
      /* 多值模式：case 1, 2, 3 -> 中，1 失败后应继续尝试 2，而不是直接跳到 next_check */
      luaK_patchtohere(fs, prev_false_jumps);
      prev_false_jumps = NO_JUMP;
    }
    first_pattern = 0;

    int current_false_jump = NO_JUMP;

    /* --- 通配符模式：_ 匹配任意值 --- */
    if (ls->t.token == TK_NAME && strcmp(getstr(ls->t.seminfo.ts), "_") == 0) {
       luaX_next(ls);
       /* 通配符总是匹配成功，跳转到分支体（如果提供了success_jump） */
       if (success_jump != NULL)
         luaK_concat(fs, success_jump, luaK_jump(fs));
    }
    /* --- 变量绑定模式：name (后面不是 =) --- */
    else if (ls->t.token == TK_NAME && luaX_lookahead(ls) != '=') {
       TString *name = str_checkname(ls);
       int reg = luaY_nvarstack(fs);  /* actual register */
       /* 使用 insert_localvar 在 nactvar 位置插入变量，确保 actvar/nactvar 对齐 */
       int vidx = insert_localvar(ls, name, reg);
       if (fs->freereg < fs->nactvar) fs->freereg = fs->nactvar;
       if (reg != ctrl->u.info) {
          luaK_codeABC(fs, OP_MOVE, reg, ctrl->u.info, 0);
       }
       /* 变量绑定总是匹配成功，跳转到分支体（如果提供了success_jump） */
       if (success_jump != NULL)
         luaK_concat(fs, success_jump, luaK_jump(fs));
    }
    /* --- 类型模式：is TypeName 或 is ClassName --- */
    else if (ls->t.token == TK_IS) {
       luaX_next(ls); /* skip 'is' */
       /* 区分类型关键字和类名 */
       if (is_type_token(ls->t.token) && ls->t.token != TK_NAME) {
         /* 类型关键字：int/float/bool/void/char/long → 使用 OP_IS */
         TString *type_name = str_checkname(ls);
         int type_k = luaK_stringK(fs, type_name);
         /* OP_IS: 检查 R[ctrl] 的类型是否等于 K[type_k] */
         /* k=0 表示：类型匹配时跳过下一条指令（JMP），不匹配时执行JMP跳转到next_check */
         luaK_codeABCk(fs, OP_IS, ctrl->u.info, type_k, 0, 0);
         luaK_concat(fs, &current_false_jump, luaK_jump(fs));
       }
       else {
         /* 类名：解析为表达式，使用 OP_INSTANCEOF */
         expdesc class_exp;
         expr(ls, &class_exp);
         int class_reg = luaK_exp2anyreg(fs, &class_exp);
         /* OP_INSTANCEOF: 检查 R[ctrl] instanceof R[class_reg] */
         /* k=0 表示：匹配时跳过下一条指令（JMP），不匹配时执行JMP跳转到next_check */
         luaK_codeABCk(fs, OP_INSTANCEOF, ctrl->u.info, class_reg, 0, 0);
         luaK_concat(fs, &current_false_jump, luaK_jump(fs));
       }
       /* 类型匹配成功后跳转到分支体（如果提供了success_jump） */
       if (success_jump != NULL)
         luaK_concat(fs, success_jump, luaK_jump(fs));
    }
    /* --- 表解构模式：{ field1, field2, ... } --- */
    else if (ls->t.token == '{') {
       luaX_next(ls);
       /* 保存控制值到安全寄存器，避免被字段变量覆盖 */
       int ctrl_save_reg = fs->freereg;
       luaK_reserveregs(fs, 1);
       luaK_codeABC(fs, OP_MOVE, ctrl_save_reg, ctrl->u.info, 0);
       int orig_ctrl_reg = ctrl->u.info;
       ctrl->u.info = ctrl_save_reg;
       int idx = 1;
       while (ls->t.token != '}' && ls->t.token != TK_EOS) {
          expdesc key;

          if (ls->t.token == '[') {
             luaX_next(ls);
             expr(ls, &key);
             checknext(ls, ']');
             checknext(ls, '=');
          } else if (ls->t.token == TK_NAME) {
             int la = luaX_lookahead(ls);
             if (la == '=') {
                 codestring(&key, str_checkname(ls));
                 luaX_next(ls);
             } else {
                 /* 简写形式：{x} 等价于 {x = x}，使用字段名字符串作为键 */
                 /* 不消耗 token，让递归 parse_pattern 处理变量绑定 */
                 codestring(&key, ls->t.seminfo.ts);
             }
          } else {
             init_exp(&key, VKINT, 0); key.u.ival = idx++;
          }

          luaK_exp2anyregup(fs, &key);

          int val_reg = fs->freereg;
          luaK_reserveregs(fs, 1);
          luaK_codeABC(fs, OP_GETTABLE, val_reg, ctrl->u.info, key.u.info);

          /* 将 val_reg 移动到 nactvar 位置，为局部变量腾出空间 */
          int target_reg = luaY_nvarstack(fs);  /* use actual register, not variable index */
          if (val_reg != target_reg) {
             luaK_codeABC(fs, OP_MOVE, target_reg, val_reg, 0);
          }

          /* 重置 freereg 到新 'val' 的末尾 */
          fs->freereg = target_reg + 1;

          expdesc val;
          init_exp(&val, VNONRELOC, target_reg);

          /* 递归解析子模式（支持嵌套解构） */
          /* 表字段模式不直接跳转到分支体，而是fallthrough到下一个字段 */
          /* 传入NULL作为success_jump，字段匹配成功后自然fallthrough */
          parse_pattern(ls, &val, &current_false_jump, NULL, 0);

          if (testnext(ls, ',')) {}
       }
       check_match(ls, '}', '{', ls->linenumber);
       /* 所有表字段匹配成功后，跳转到分支体 */
       if (success_jump != NULL)
         luaK_concat(fs, success_jump, luaK_jump(fs));
    }
    /* --- 字面量模式 / 范围模式 --- */
    else {
       expdesc e;
       expdesc c = *ctrl;
       int old_flags = ls->expr_flags;
       ls->expr_flags |= E_NO_COLON;
       /* 使用 PRI_RANGE_EXPR 限制，在 '..' 之前停止，以便检测范围模式 */
       subexpr(ls, &e, PRI_RANGE_EXPR);
       ls->expr_flags = old_flags;

       /* 检查是否为范围模式：low..high */
       if (ls->t.token == TK_CONCAT) {
          /* 范围模式：生成 (ctrl >= low) and (ctrl <= high) */
          luaX_next(ls); /* skip '..' */
          expdesc upper;
          /* 上限也用 PRI_RANGE_EXPR 限制，避免 '..' 被消费 */
          subexpr(ls, &upper, PRI_RANGE_EXPR);

          /* 检查 ctrl >= low */
          luaK_infix(fs, OPR_GE, &c);
          luaK_posfix(fs, OPR_GE, &c, &e, ls->linenumber);
          luaK_goiftrue(fs, &c);
          int ge_false = c.f;  /* ctrl < low 时的跳转 */

          /* 检查 ctrl <= upper */
          expdesc c2 = *ctrl;
          luaK_infix(fs, OPR_LE, &c2);
          luaK_posfix(fs, OPR_LE, &c2, &upper, ls->linenumber);
          luaK_goiftrue(fs, &c2);

          /* 两个条件都失败时跳转到当前子模式的失败跳转链 */
          luaK_concat(fs, &current_false_jump, ge_false);
          luaK_concat(fs, &current_false_jump, c2.f);
          /* 范围匹配成功后跳转到分支体（如果提供了success_jump） */
          if (success_jump != NULL)
            luaK_concat(fs, success_jump, luaK_jump(fs));
       }
       else {
          /* 字面量模式：生成 ctrl == expr */
          luaK_infix(fs, OPR_EQ, &c);
          luaK_posfix(fs, OPR_EQ, &c, &e, ls->linenumber);

          luaK_goiftrue(fs, &c);
          luaK_concat(fs, &current_false_jump, c.f);
          /* 字面量匹配成功后跳转到分支体（如果提供了success_jump） */
          if (success_jump != NULL)
            luaK_concat(fs, success_jump, luaK_jump(fs));
       }
    }

    /* 保存当前子模式的失败跳转，供下一次迭代使用 */
    /* 如果是最后一个子模式，这些跳转会转到 next_check_jump */
    /* 如果后面还有子模式，这些跳转会被修补到下一个子模式检查的开始 */
    prev_false_jumps = current_false_jump;

  } while (allow_multi && ls->t.token == ',');

  /* 最后一个子模式的失败跳转汇总到 next_check_jump */
  /* 对于单值模式，这就是唯一的失败跳转 */
  luaK_concat(fs, next_check_jump, prev_false_jumps);
}

/*
** match 表达式/语句的共享核心解析函数
** 参数：
**   ls - 词法分析状态
**   v - 表达式结果存储（仅 is_expr=1 时有效）
**   is_expr - 是否为表达式模式（1=表达式，0=语句）
** 说明：
**   - 表达式模式：只支持箭头形式 (case pattern => expr)，结果存入 v
**   - 语句模式：支持箭头形式和块形式，箭头形式使用 luaK_ret 返回
*/
static void match_body (LexState *ls, expdesc *v, int is_expr) {
  FuncState *fs = ls->fs;
  BlockCnt bl;
  expdesc ctrl;
  int jump_to_check = NO_JUMP;
  int finish_jump = NO_JUMP;
  int result_reg = -1;
  int line = ls->linenumber;  /* 记录 match 关键字所在行号 */

  luaX_next(ls);  /* skip MATCH */

  enterblock(fs, &bl, 1); /* isloop=1 to support break */

  expr(ls, &ctrl); /* parse control expression */

  /* 保存控制值到寄存器（不创建局部变量，避免 actvar/nactvar 不对齐） */
  luaK_exp2nextreg(fs, &ctrl);

  /* 表达式模式：在控制变量之后分配结果寄存器（避免寄存器冲突） */
  if (is_expr) {
    result_reg = fs->freereg;
    luaK_reserveregs(fs, 1);
  }

  if(!testnext(ls, TK_DO)){
      if(!testnext(ls, TK_THEN)){
        if (!testnext(ls, ':')){
          testnext(ls, '{');
        }
      }
  }

  jump_to_check = luaK_jump(fs);

  while (ls->t.token != TK_END && ls->t.token != TK_EOS && ls->t.token != '}') {
    if (ls->t.token == TK_CASE) {
      int next_check_jump = NO_JUMP;

      /* 生成检查代码 */
      luaK_patchtohere(fs, jump_to_check);

      luaX_next(ls); /* skip CASE */

      BlockCnt case_bl;
      enterblock(fs, &case_bl, 0);

      /* 解析模式并构建失败跳转和成功跳转 */
      int success_jump = NO_JUMP;
      parse_pattern(ls, &ctrl, &next_check_jump, &success_jump, 1);

      /* 修补成功跳转：模式匹配成功后跳转到分支体（守卫条件之前） */
      luaK_patchtohere(fs, success_jump);

      /* 可选守卫条件（用 expr_nocase 防止把分支的 => 当成 OPR_CASE 消费） */
      if (testnext(ls, TK_IF)) {
         expdesc cond;
         expr_nocase(ls, &cond);
         luaK_goiftrue(fs, &cond);
         luaK_concat(fs, &next_check_jump, cond.f);
      }

      /* 分支体（支持 -> 与 => 两种箭头，与 AST 解析器对齐） */
      if (testnext(ls, TK_ARROW) || testnext(ls, TK_MEAN)) {
         expdesc e;
         expr(ls, &e);
         if (is_expr) {
           /* 表达式模式：将结果存入结果寄存器 */
           luaK_exp2reg(fs, &e, result_reg);
         } else {
           /* 语句模式：评估表达式（副作用），不返回 */
           luaK_exp2nextreg(fs, &e);
           /* 不调用 luaK_ret，避免提前终止函数导致后续代码不可达 */
         }
      } else {
         if (is_expr) {
           luaX_syntaxerror(ls, "match expression requires '=>' arrow form for each case");
         }
         testnext(ls, ':');
         testnext(ls, TK_DO);
         testnext(ls, TK_THEN);
         statlist(ls);
      }

      leaveblock(fs);

      /* 跳转到 match 结束 */
      luaK_concat(fs, &finish_jump, luaK_jump(fs));

      jump_to_check = next_check_jump;
    } else {
       luaX_syntaxerror(ls, "expected 'case'");
    }
  }

  /* 修补挂起的检查（没有case匹配时） */
  luaK_patchtohere(fs, jump_to_check);

  /* 如果没有匹配的case，表达式模式返回 nil */
  if (is_expr) {
    luaK_codeABC(fs, OP_LOADNIL, result_reg, result_reg, 0);
  }

  /* match 结束 */
  if (finish_jump != NO_JUMP) {
    luaK_patchtohere(fs, finish_jump);
  }

  if (ls->t.token == TK_END) {
    luaX_next(ls);
  } else {
    check_match(ls, '}', '{', line);
  }

  leaveblock(fs);

  /* 表达式模式：设置返回值 */
  if (is_expr) {
    /* 离开块后 nactvar 恢复到外部块的状态，需要将结果移动到外部块的变量基址，
    ** 以便后续 local 赋值等操作能正确获取结果 */
    int target_reg = fs->nactvar;
    if (result_reg != target_reg) {
      luaK_codeABC(fs, OP_MOVE, target_reg, result_reg, 0);
    }
    init_exp(v, VNONRELOC, target_reg);
    fs->freereg = (target_reg + 1);
  }
}

/*
** match 语句解析（保留原有功能）
** 语法：match expr { case pattern => expr | case pattern: block } end
*/
static void matchstat (LexState *ls, int line) {
  (void)line;  /* line 参数在 match_body 内部处理 */
  match_body(ls, NULL, 0);
}

/*
** match 表达式解析（新增：函数式编程增强）
** 语法：match expr { case pattern => expr, ... }
** 示例：
**   local result = match x do
**     case 1 => "one"
**     case 2, 3, 4 => "small"
**     case 5..10 => "medium"
**     case is string => "text"
**     case _ => "other"
**   end
*/
static void matchexpr (LexState *ls, expdesc *v) {
  match_body(ls, v, 1);
}

static void switchstat (LexState *ls, int line) {
  FuncState *fs = ls->fs;
  BlockCnt bl;
  expdesc ctrl;
  int jump_to_check;
  int escapelist = NO_JUMP;
  int default_label = -1;
  int previous_body_active = 0; /* To track if we need to generate fallthrough jump */

  luaX_next(ls);  /* skip SWITCH */

  enterblock(fs, &bl, 1); /* isloop=1 to support break */

  expr(ls, &ctrl); /* parse control expression */

  /* Save control value to a local variable to ensure register safety */
  luaK_exp2nextreg(fs, &ctrl);
  new_localvarliteral(ls, "(switch control)");
  adjustlocalvars(ls, 1);

  if(!testnext(ls, TK_DO)){
      if(!testnext(ls, TK_THEN)){
        if (!testnext(ls, ':')){
          testnext(ls, '{');
        }
      }
  }

  /* Initial jump to first check */
  jump_to_check = luaK_jump(fs);

  while (ls->t.token != TK_END && ls->t.token != TK_EOS && ls->t.token != '}') {
    if (ls->t.token == TK_CASE) {
      int to_body_jump = NO_JUMP;
      int next_check_jump;

      /* Handle escape from previous body */
      if (previous_body_active) {
         luaK_concat(fs, &escapelist, luaK_jump(fs));
      }

      /* Now generating check code */
      luaK_patchtohere(fs, jump_to_check);

      luaX_next(ls); /* skip CASE */

      /* Parse conditions */
      do {
        expdesc e;
        expdesc c = ctrl; /* Copy ctrl expdesc */
        int old_flags = ls->expr_flags;
        ls->expr_flags |= E_NO_COLON;
        expr(ls, &e);
        ls->expr_flags = old_flags;

        luaK_infix(fs, OPR_EQ, &c);
        luaK_posfix(fs, OPR_EQ, &c, &e, ls->linenumber);

        luaK_goiftrue(fs, &c);
        /* If false, it jumps to c.f. */
        /* If true, it is here. Generate jump to body. */
        {
           int j = luaK_jump(fs);
           luaK_concat(fs, &to_body_jump, j);
        }
        /* Patch c.f to here (next check/condition) */
        luaK_patchtohere(fs, c.f);

      } while (testnext(ls, ','));

      /* If we fall through here, it means all checks failed. */
      /* Jump to next check block */
      next_check_jump = luaK_jump(fs);
      jump_to_check = next_check_jump;

      /* Body Start */
      luaK_patchtohere(fs, to_body_jump);


      /* Parse Body（支持 -> 与 => 两种箭头，与 AST 解析器对齐） */
      if (testnext(ls, TK_ARROW) || testnext(ls, TK_MEAN)) {
         expdesc e;
         expr(ls, &e);
         luaK_exp2nextreg(fs, &e);
         /* 箭头体执行完后跳转到 switch 结束（不返回函数） */
         luaK_concat(fs, &escapelist, luaK_jump(fs));
         previous_body_active = 0;
      } else {
         testnext(ls, ':');
         testnext(ls, TK_DO);
         testnext(ls, TK_THEN);
         /* checknext(ls, '{');  optional brace? */

         BlockCnt casebl;
         enterblock(fs, &casebl, 0);
         statlist(ls);
         leaveblock(fs);
         previous_body_active = 1;
      }

    } else if (ls->t.token == TK_DEFAULT) {
      if (default_label != -1) luaX_syntaxerror(ls, "multiple default blocks");

      /* Handle escape from previous body */
      if (previous_body_active) {
         luaK_concat(fs, &escapelist, luaK_jump(fs));
      }

      /* Do NOT patch checks to skip here. Let them skip this block entirely. */

      /* Default Body Start */
      default_label = luaK_getlabel(fs);

      /* Patch fallthrough to here */


      luaX_next(ls); /* skip DEFAULT */

      if (testnext(ls, TK_ARROW) || testnext(ls, TK_MEAN)) {
         expdesc e;
         expr(ls, &e);
         luaK_exp2nextreg(fs, &e);
         /* 箭头体执行完后跳转到 switch 结束（不返回函数） */
         luaK_concat(fs, &escapelist, luaK_jump(fs));
         previous_body_active = 0;
      } else {
         testnext(ls, ':');
         testnext(ls, TK_DO);
         testnext(ls, TK_THEN);
         {
           BlockCnt casebl;
           enterblock(fs, &casebl, 0);
           statlist(ls);
           leaveblock(fs);
         }
         previous_body_active = 1;
      }
    } else {
       luaX_syntaxerror(ls, "expected 'case' or 'default'");
    }
  }

  /* End of switch */

  /* Patch dangling checks */
  if (default_label != -1) {
    luaK_patchlist(fs, jump_to_check, default_label);
  } else {
    luaK_patchtohere(fs, jump_to_check); /* Falls through to end */
  }

  luaK_patchtohere(fs, escapelist);

  if (ls->t.token == TK_END) {
    luaX_next(ls);
  } else {
    check_match(ls, '}', '{', line);
  }

  leaveblock(fs);
}


/*
** guard 语句解析
** 语法:
**   guard cond else { block }
**   guard let name = expr else { block }
**
** 功能:
**   计算条件，如果条件为 nil/false，则执行 else 块；否则继续执行。
**   guard let 形式先进行赋值，再检查是否为 nil。
*/
static void guardstat (LexState *ls, int line) {
  FuncState *fs = ls->fs;
  expdesc v;
  int jf;  /* 条件为假时跳转到 else 块 */
  int nvars = 0;

  luaX_next(ls);  /* skip GUARD */

  /* 检查是否是 guard let 形式 */
  if (testnext(ls, TK_LET)) {
    /* guard let name = expr else { ... } */
    nvars = 1;
    /* 解析变量名 */
    if (ls->t.token == TK_NAME) {
      TString *varname = str_checkname(ls);
      /* 创建局部变量 */
      new_localvar(ls, varname);
      adjustlocalvars(ls, 1);
      luaK_reserveregs(fs, 1);
    } else {
      luaX_syntaxerror(ls, "<name> expected after 'let' in guard statement");
    }
    checknext(ls, '=');
    expr(ls, &v);
    /* 将表达式赋值给新变量 */
    {
      expdesc var;
      init_var(fs, &var, fs->nactvar - 1);
      luaK_storevar(fs, &var, &v);
    }
    /* 重新加载变量进行nil检查 */
    init_exp(&v, VLOCAL, fs->nactvar - 1);
    luaK_dischargevars(fs, &v);
  } else {
    /* 普通 guard cond else { ... } */
    cond_expr(ls, &v);
  }

  checknext(ls, TK_ELSE);

  /* 解析 else 块 */
  if (ls->t.token != '{') {
    luaX_syntaxerror(ls, "'{' expected after 'else' in guard statement");
  }
  luaX_next(ls);  /* skip '{' */

  /* 条件为假/nil时执行 else 块 */
  if (nvars) {
    /* guard let: 检查变量是否为 nil
     * luaK_goifnil语义:
     *   - nil时 → fall through（执行else块）
     *   - 非nil时 → 跳转到v.t列表
     */
    luaK_goifnil(fs, &v);

    /* 解析 else 块（nil时执行） */
    while (ls->t.token != '}' && ls->t.token != TK_EOS) {
      statement(ls);
    }
    checknext(ls, '}');

    /* 非nil时跳转到这里，跳过else块继续执行 */
    luaK_patchtohere(fs, v.t);
  } else {
    /* 普通guard: cond为假时执行else块，为真时跳过
     * luaK_goiffalse语义:
     *   - 条件为假 → fall through（执行else块）
     *   - 条件为真 → 跳转到v.t列表（跳过else块）
     */
    luaK_goiffalse(fs, &v);

    /* 解析 else 块（条件为假时执行） */
    while (ls->t.token != '}' && ls->t.token != TK_EOS) {
      statement(ls);
    }
    checknext(ls, '}');

    /* 条件为真时跳转到这里，跳过else块继续执行 */
    luaK_patchtohere(fs, v.t);
  }
}


/*
** try-catch-finally 语句解析
** 语法: try statlist [catch(name) statlist] [finally statlist] end
** 
** 实现原理：
** 将 try-catch-finally 转换为等价的 pcall 调用：
**   local __ok__, __err__ = pcall(function() try_block end)
**   if not __ok__ then
**     local e = __err__
**     catch_block
**   end
**   finally_block
**
** 参数：
**   ls - 词法状态
**   line - try 关键字所在行号
*/
static void trystat (LexState *ls, int line) {
  FuncState *fs = ls->fs;
  BlockCnt bl;
  int base;
  expdesc pcall_func, closure_exp, ok_var, err_var;
  int ok_reg, err_reg;
  TString *err_name = NULL;
  int has_catch = 0;
  int has_finally = 0;
  
  luaX_next(ls);  /* skip TRY */
  
  /* 进入外层 block */
  enterblock(fs, &bl, 0);
  
  /* 创建两个局部变量 __ok__ 和 __err__ */
  new_localvarliteral(ls, "__try_ok__");
  new_localvarliteral(ls, "__try_err__");
  adjustlocalvars(ls, 2);
  ok_reg = fs->nactvar - 2;
  err_reg = fs->nactvar - 1;
  
  /* 获取 pcall 全局函数 */
  singlevaraux(fs, luaS_newliteral(ls->L, "pcall"), &pcall_func, 1);
  if (pcall_func.k == VVOID) {
    expdesc key;
    singlevaraux(fs, ls->envn, &pcall_func, 1);
    codestring(&key, luaS_newliteral(ls->L, "pcall"));
    luaK_indexed(fs, &pcall_func, &key);
  }
  luaK_exp2nextreg(fs, &pcall_func);
  base = pcall_func.u.info;
  
  /* 创建闭包：function() try_block end */
  {
    FuncState new_fs;
    BlockCnt new_bl;
    new_fs.f = addprototype(ls);
    new_fs.f->linedefined = line;
    open_func(ls, &new_fs, &new_bl);
    
    /* 解析 try 块直到遇到 catch/finally/end */
    while (ls->t.token != TK_CATCH && 
           ls->t.token != TK_FINALLY && 
           ls->t.token != TK_END && 
           ls->t.token != TK_EOS) {
      statement(ls);

    }
    
    new_fs.f->lastlinedefined = ls->linenumber;
    codeclosure(ls, &closure_exp);
    close_func(ls);
  }
  
  /* 将闭包放入下一个寄存器 */
  luaK_exp2nextreg(fs, &closure_exp);
  
  /* 调用 pcall(closure)，返回 ok, err */
  luaK_codeABC(fs, OP_CALL, base, 2, 3);  /* 1个参数，2个返回值 */
  fs->freereg = base + 2;
  
  /* 将结果存储到局部变量 */
  init_exp(&ok_var, VLOCAL, reglevel(fs, ok_reg));
  init_exp(&err_var, VLOCAL, reglevel(fs, err_reg));
  {
    expdesc result;
    init_exp(&result, VNONRELOC, base);
    luaK_storevar(fs, &ok_var, &result);
    init_exp(&result, VNONRELOC, base + 1);
    luaK_storevar(fs, &err_var, &result);
  }
  
  /* 解析 catch 块 */
  if (ls->t.token == TK_CATCH) {
    has_catch = 1;
    expdesc cond;
    BlockCnt catch_bl;
    int jt;  /* 跳转：ok 为真时跳过 catch 块 */
    
    luaX_next(ls);  /* skip CATCH */
    
    /* 解析 catch(e) 中的变量名 */
    checknext(ls, '(');
    err_name = str_checkname(ls);
    checknext(ls, ')');
    
    /* 生成条件跳转：如果 __ok__ 为真则跳过 catch 块 */
    init_exp(&cond, VLOCAL, reglevel(fs, ok_reg));
    luaK_exp2anyreg(fs, &cond);
    luaK_goiffalse(fs, &cond);  /* 假 -> fallthrough 执行 catch；真 -> 跳转跳过 catch */
    jt = cond.t;  /* 保存真跳转位置 */
    
    /* 进入 catch 块 */
    enterblock(fs, &catch_bl, 0);
    
    /* 创建局部变量 e = __err__ */
    new_localvar(ls, err_name);
    adjustlocalvars(ls, 1);
    {
      expdesc err_val;
      init_exp(&err_val, VLOCAL, reglevel(fs, err_reg));
      luaK_exp2nextreg(fs, &err_val);
    }
    
    /* 解析 catch 块语句 */
    while (ls->t.token != TK_FINALLY && 
           ls->t.token != TK_END && 
           ls->t.token != TK_EOS) {
      statement(ls);

    }
    
    leaveblock(fs);
    luaK_patchtohere(fs, jt);  /* 真跳转跳到这里（跳过 catch） */
  }
  
  /* 解析 finally 块 */
  if (ls->t.token == TK_FINALLY) {
    has_finally = 1;
    luaX_next(ls);  /* skip FINALLY */
    
    /* finally 块无条件执行 */
    while (ls->t.token != TK_END && ls->t.token != TK_EOS) {
      statement(ls);

    }
  }
  
  check_match(ls, TK_END, TK_TRY, line);
  leaveblock(fs);
}


/*
** with 语句解析
** 语法: with(expr) { block }
** 
** 实现原理：
** 将 with(expr) { block } 转换为等价代码：
**   do
**     local __with_target__ = expr
**     local __with_saved_env__ = _ENV
**     _ENV = __with_create_env__(__with_target__, __with_saved_env__)
**     -- block
**     _ENV = __with_saved_env__
**   end
**
** 参数：
**   ls - 词法状态
**   line - with 关键字所在行号
*/
static void withstat (LexState *ls, int line) {
  FuncState *fs = ls->fs;
  BlockCnt bl;
  expdesc target_exp, env_var, saved_env_exp, func_exp, new_env_exp;
  int target_reg, saved_env_reg;
  int base;
  
  luaX_next(ls);  /* skip WITH */
  
  /* 进入块作用域 */
  enterblock(fs, &bl, 0);
  
  /* 解析 with(expr) 中的表达式 */
  checknext(ls, '(');
  expr(ls, &target_exp);
  checknext(ls, ')');
  
  /* 创建局部变量 __with_target__ 存储目标表 */
  new_localvarliteral(ls, "__with_target__");
  luaK_exp2nextreg(fs, &target_exp);
  adjustlocalvars(ls, 1);
  target_reg = fs->nactvar - 1;
  
  /* 创建局部变量 __with_saved_env__ 存储原 _ENV */
  new_localvarliteral(ls, "__with_saved_env__");
  singlevaraux(fs, ls->envn, &env_var, 1);  /* 获取 _ENV */
  if (env_var.k == VVOID) {
    /* _ENV 不存在，从全局获取 */
    expdesc key;
    singlevaraux(fs, ls->envn, &env_var, 1);
  }
  luaK_exp2nextreg(fs, &env_var);
  adjustlocalvars(ls, 1);
  saved_env_reg = fs->nactvar - 1;
  
  /* 调用 __with_create_env__(__with_target__, __with_saved_env__) */
  /* 获取 __with_create_env__ 全局函数 */
  singlevaraux(fs, luaS_newliteral(ls->L, "__with_create_env__"), &func_exp, 1);
  if (func_exp.k == VVOID) {
    expdesc key;
    singlevaraux(fs, ls->envn, &func_exp, 1);
    codestring(&key, luaS_newliteral(ls->L, "__with_create_env__"));
    luaK_indexed(fs, &func_exp, &key);
  }
  luaK_exp2nextreg(fs, &func_exp);
  base = func_exp.u.info;
  
  /* 参数1: __with_target__ */
  {
    expdesc arg;
    init_exp(&arg, VLOCAL, reglevel(fs, target_reg));
    luaK_exp2nextreg(fs, &arg);
  }
  
  /* 参数2: __with_saved_env__ */
  {
    expdesc arg;
    init_exp(&arg, VLOCAL, reglevel(fs, saved_env_reg));
    luaK_exp2nextreg(fs, &arg);
  }
  
  /* 调用函数，返回新环境 */
  luaK_codeABC(fs, OP_CALL, base, 3, 2);  /* 2个参数，1个返回值 */
  fs->freereg = base + 1;
  
  /* 将新环境赋值给 _ENV */
  {
    expdesc env_dst;
    expdesc result;
    singlevaraux(fs, ls->envn, &env_dst, 1);
    init_exp(&result, VNONRELOC, base);
    luaK_storevar(fs, &env_dst, &result);
  }
  
  /* 解析块内容 */
  checknext(ls, '{');
  while (ls->t.token != '}' && ls->t.token != TK_EOS) {
    statement(ls);
  }
  checknext(ls, '}');
  
  /* 恢复 _ENV = __with_saved_env__ */
  {
    expdesc env_dst, saved_val;
    singlevaraux(fs, ls->envn, &env_dst, 1);
    init_exp(&saved_val, VLOCAL, reglevel(fs, saved_env_reg));
    luaK_exp2anyreg(fs, &saved_val);
    luaK_storevar(fs, &env_dst, &saved_val);
  }
  
  leaveblock(fs);
}

//========================================================================================


static void localfunc (LexState *ls, int isexport, int isasync) {
  expdesc b;
  FuncState *fs = ls->fs;
  int fvar = fs->nactvar;  /* function's variable index */
  TString *name = str_checkname(ls);
  new_localvar(ls, name);  /* new local variable */
  if (isexport) add_export(ls, name);
  adjustlocalvars(ls, 1);  /* enter its scope */
  body(ls, &b, 0, ls->linenumber);  /* function created in next register */

  if (isasync) {
      FuncState *fs = ls->fs;
      /*
       * 纯语法级 async 标记：直接在函数 Proto 上设置 PF_ASYNC 标志，
       * 不再创建 CClosure 包装器。调用时 luaD_precall 检测标志，
       * 直接走 VM 异步路径。
       */
      luaK_exp2nextreg(fs, &b);
      luaK_codeABC(fs, OP_ASYNCWRAP, 0, b.u.info, 0);

      /* 将结果移动到局部变量寄存器 fvar */
      if (fvar != b.u.info)
        luaK_codeABC(fs, OP_MOVE, fvar, b.u.info, 0);

      fs->freereg = fvar + 1;
  }

  if (fs->f->p[fs->np - 1]->nodiscard) {
     getlocalvardesc(fs, fvar)->vd.nodiscard = 1;
  }
  /* debug information will only see the variable after this point! */
  localdebuginfo(fs, fvar)->startpc = fs->pc;
}


static lu_byte getvarattribute (LexState *ls, lu_byte df) {
  /* attrib -> ['<' NAME '>'] */
  if (testnext(ls, '<')) {
    const char *attr;
    if (ls->t.token == TK_CONST) {
      attr = "const";
      luaX_next(ls);
    }
    else {
      TString *ts = str_checkname(ls);
      attr = getstr(ts);
    }
    checknext(ls, '>');
    if (strcmp(attr, "const") == 0)
      return RDKCONST;  /* 只读变量 */
    else if (strcmp(attr, "close") == 0)
      return RDKTOCLOSE;  /* 待关闭变量 */
    else
      luaK_semerror(ls, "unknown attribute '%s'", attr);
  }
  return df;  /* 返回默认值 */
}


static void checktoclose (FuncState *fs, int level) {
  if (level != -1) {  /* is there a to-be-closed variable? */
    marktobeclosed(fs);
    luaK_codeABC(fs, OP_TBC, reglevel(fs, level), 0, 0);
  }
}


/*
** =======================================================================
** take 解构语法实现
** 语法格式: local take {变量列表} = 目标表
** 支持: 基础键值解构、缺省值、数组解构(跳过元素)、嵌套解构
** =======================================================================
*/

/* 解构项的最大数量 */
#define MAX_DESTRUCT_ITEMS 64

/* 解构项类型 */
typedef struct DestructItem {
  TString *varname;       /* 局部变量名 */
  TString *keyname;       /* 表中的键名 (如果为NULL则使用varname) */
  int array_index;        /* 数组索引 (0表示键值模式, >0表示数组模式) */
  int has_default;        /* 是否有默认值 */
  int default_reg;        /* 默认值所在寄存器 */
  int is_nested;          /* 是否是嵌套解构 */
  int nested_start;       /* 嵌套解构的起始索引 */
  int nested_count;       /* 嵌套解构的项数量 */
} DestructItem;

/*
** 解析解构项列表
** 参数:
**   ls: 词法状态
**   items: 解构项数组
**   max_items: 最大项数
**   array_mode: 是否为数组模式 (检测到跳过元素时自动切换)
** 返回: 解构项数量
*/
static int parse_destruct_items(LexState *ls, DestructItem *items, int max_items, int *array_mode) {
  int count = 0;
  int array_idx = 1;  /* 数组索引从1开始 */
  
  checknext(ls, '{');
  
  while (ls->t.token != '}' && count < max_items) {
    DestructItem *item = &items[count];
    memset(item, 0, sizeof(DestructItem));
    
    /* 检测跳过元素 (空位，如 {a, , b}) */
    if (ls->t.token == ',') {
      /* 切换到数组模式 */
      *array_mode = 1;
      array_idx++;  /* 跳过这个索引 */
      luaX_next(ls);  /* 跳过逗号 */
      continue;
    }
    
    /* 检测是否是嵌套解构: name = {nested} 或直接 {nested} */
    if (ls->t.token == '{') {
      /* 直接嵌套解构，不支持这种形式，报错 */
      luaX_syntaxerror(ls, "nested destructuring must specify key name, e.g. addr = {city}");
    }
    
    /* 解析变量名/键名 */
    if (ls->t.token != TK_NAME) {
      luaX_syntaxerror(ls, "identifier expected in destructuring");
    }
    item->varname = ls->t.seminfo.ts;
    item->keyname = item->varname;  /* 默认键名与变量名相同 */
    luaX_next(ls);
    
    /* 检测默认值或嵌套解构: name = expr 或 name = {nested} */
    if (testnext(ls, '=')) {
      if (ls->t.token == '{') {
        /* 嵌套解构: name = {nested_items} */
        item->is_nested = 1;
        item->nested_start = count + 1;
        
        /* 递归解析嵌套项 */
        int nested_array_mode = 0;
        int nested_count = parse_destruct_items(ls, &items[count + 1], 
                                                 max_items - count - 1, 
                                                 &nested_array_mode);
        item->nested_count = nested_count;
        count += nested_count;  /* 跳过嵌套项 */
      } else {
        /* 有默认值 */
        item->has_default = 1;
        /* 默认值表达式稍后在代码生成阶段处理 */
      }
    }
    
    /* 设置数组索引 */
    if (*array_mode) {
      item->array_index = array_idx++;
    }
    
    count++;
    
    /* 检查逗号或结束 */
    if (ls->t.token == ',') {
      luaX_next(ls);
      /* 如果是数组模式，增加索引 */
      if (*array_mode && ls->t.token != '}') {
        /* 索引将在下一次循环开始时设置 */
      }
    } else if (ls->t.token != '}') {
      luaX_syntaxerror(ls, "',' or '}' expected in destructuring list");
    }
  }
  
  checknext(ls, '}');
  return count;
}

/*
** 为单个解构项生成代码
** 参数:
**   ls: 词法状态
**   item: 解构项
**   source_reg: 源表寄存器
**   items: 所有解构项 (用于嵌套解构)
**   all_count: 所有解构项数量
*/
static void codegen_destruct_item(LexState *ls, DestructItem *item, int source_reg,
                                   DestructItem *items, int all_count) {
  FuncState *fs = ls->fs;
  expdesc source, key, val;
  
  if (item->is_nested) {
    /* 嵌套解构: 先获取嵌套表，然后递归处理 */
    expdesc nested_table;
    int nested_reg;
    
    /* 获取嵌套表: source[keyname] */
    init_exp(&source, VNONRELOC, source_reg);
    codestring(&key, item->keyname);
    luaK_indexed(fs, &source, &key);
    luaK_exp2nextreg(fs, &source);
    nested_reg = source.u.info;
    
    /* 递归处理嵌套项 */
    int i;
    for (i = 0; i < item->nested_count; i++) {
      DestructItem *nested_item = &items[item->nested_start + i];
      if (!nested_item->is_nested) {
        codegen_destruct_item(ls, nested_item, nested_reg, items, all_count);
      }
    }
    
    fs->freereg = nested_reg;  /* 释放嵌套表寄存器 */
    return;
  }
  
  /* 创建局部变量 */
  int vidx = new_localvar(ls, item->varname);
  
  /* 生成从源表读取值的代码 */
  init_exp(&source, VNONRELOC, source_reg);
  
  if (item->array_index > 0) {
    /* 数组模式: source[index] */
    init_exp(&key, VKINT, 0);
    key.u.ival = item->array_index;
  } else {
    /* 键值模式: source[keyname] */
    codestring(&key, item->keyname);
  }
  
  luaK_indexed(fs, &source, &key);
  luaK_exp2nextreg(fs, &source);
  
  /* 调整局部变量 */
  adjustlocalvars(ls, 1);
}



/*
** takestat_full - 解析 take 解构语句
** 语法: local take {name, age = 18, ...} = source_table
** 
** 将表字段解构到局部变量
** 支持: 键值解构、数组解构(跳过元素)、嵌套解构、默认值
**
** 参数:
**   ls - 词法分析器状态
*/
static void takestat_full(LexState *ls) {
  FuncState *fs = ls->fs;
  int line = ls->linenumber;
  expdesc source_exp;
  int source_reg;
  
  /* 收集变量信息 */
  TString *varnames[MAX_DESTRUCT_ITEMS];
  TString *keynames[MAX_DESTRUCT_ITEMS];
  int array_indices[MAX_DESTRUCT_ITEMS];
  int is_nested[MAX_DESTRUCT_ITEMS];
  TString *nested_keyname[MAX_DESTRUCT_ITEMS];
  int has_default[MAX_DESTRUCT_ITEMS];  /* 是否有默认值 */
  expdesc default_exps[MAX_DESTRUCT_ITEMS];  /* 默认值表达式 */
  int nvars = 0;
  int array_mode = 0;
  int array_idx = 1;
  int i;
  int end_token = '}';
  
  if (ls->t.token == '[') {
      array_mode = 1;
      end_token = ']';
      luaX_next(ls);
  } else {
      checknext(ls, '{');
  }
  
  /* 第一阶段：收集所有变量信息 */
  while (ls->t.token != end_token && nvars < MAX_DESTRUCT_ITEMS) {
    /* 跳过空位（数组模式） */
    if (ls->t.token == ',') {
      array_mode = 1;
      array_idx++;
      luaX_next(ls);
      continue;
    }
    
    if (ls->t.token != TK_NAME) {
      luaX_syntaxerror(ls, "identifier expected in destructuring");
    }
    
    TString *name = ls->t.seminfo.ts;
    luaX_next(ls);
    
    varnames[nvars] = name;
    keynames[nvars] = name;
    array_indices[nvars] = array_mode ? array_idx : 0;
    is_nested[nvars] = 0;
    nested_keyname[nvars] = NULL;
    has_default[nvars] = 0;
    init_exp(&default_exps[nvars], VVOID, 0);
    
    if (testnext(ls, '=')) {
      if (ls->t.token == '{') {
        /* 嵌套解构: name = {fields} */
        TString *parent_key = name;
        luaX_next(ls);  /* 跳过 '{' */
        
        /* 不为父项创建变量，只为嵌套项创建 */
        nvars--;  /* 撤销父项 */
        
        while (ls->t.token != '}' && nvars < MAX_DESTRUCT_ITEMS) {
          if (ls->t.token == ',') {
            luaX_next(ls);
            continue;
          }
          
          if (ls->t.token != TK_NAME) {
            luaX_syntaxerror(ls, "identifier expected in nested destructuring");
          }
          
          varnames[nvars] = ls->t.seminfo.ts;
          keynames[nvars] = varnames[nvars];
          array_indices[nvars] = 0;
          is_nested[nvars] = 1;
          nested_keyname[nvars] = parent_key;
          has_default[nvars] = 0;
          init_exp(&default_exps[nvars], VVOID, 0);
          
          luaX_next(ls);
          
          /* 支持嵌套解构的默认值 */
          if (testnext(ls, '=')) {
            has_default[nvars] = 1;
            expr(ls, &default_exps[nvars]);
          }
          
          nvars++;
          
          if (ls->t.token == ',') {
            luaX_next(ls);
          }
        }
        checknext(ls, '}');
        
        if (array_mode) array_idx++;
        if (ls->t.token == ',') luaX_next(ls);
        continue;
      } else {
        /* 支持默认值: name = default_expr */
        has_default[nvars] = 1;
        expr(ls, &default_exps[nvars]);
      }
    }
    
    if (array_mode) {
      array_idx++;
    }
    
    nvars++;
    
    if (ls->t.token == ',') {
      luaX_next(ls);
    }
  }
  
  checknext(ls, end_token);
  checknext(ls, '=');
  
  /* 第二阶段：创建所有局部变量 */
  for (i = 0; i < nvars; i++) {
    new_localvar(ls, varnames[i]);
  }
  
  /* 获取变量起始寄存器 */
  int var_base = luaY_nvarstack(fs);
  
  /* 预留寄存器空间给变量 */
  luaK_reserveregs(fs, nvars);
  
  /* 第三阶段：解析源表达式到临时寄存器（在变量区域之后） */
  expr(ls, &source_exp);
  luaK_exp2nextreg(fs, &source_exp);
  source_reg = source_exp.u.info;
  
  /* 第四阶段：为每个变量生成从源表读取值的代码 */
  for (i = 0; i < nvars; i++) {
    expdesc src, key_exp;
    int target_reg = var_base + i;
    int actual_source = source_reg;
    
    /* 如果是嵌套项，先获取嵌套表 */
    if (is_nested[i] && nested_keyname[i] != NULL) {
      expdesc nested_src, nested_key;
      init_exp(&nested_src, VNONRELOC, source_reg);
      codestring(&nested_key, nested_keyname[i]);
      luaK_indexed(fs, &nested_src, &nested_key);
      luaK_exp2nextreg(fs, &nested_src);
      actual_source = nested_src.u.info;
    }
    
    /* 生成 table[key] 的读取代码 */
    init_exp(&src, VNONRELOC, actual_source);
    if (array_indices[i] > 0) {
      /* 数组模式: table[index] */
      init_exp(&key_exp, VKINT, 0);
      key_exp.u.ival = array_indices[i];
    } else {
      /* 键值模式: table[keyname] */
      codestring(&key_exp, keynames[i]);
    }
    luaK_indexed(fs, &src, &key_exp);
    
    /* 生成条件赋值: 如果 table[key] 为 nil 则使用默认值 */
    if (has_default[i]) {
      /* 将 table[key] 加载到临时寄存器 */
      luaK_exp2nextreg(fs, &src);
      int val_reg = src.u.info;
      
      /* 生成 nil 检查 */
      luaK_codeABCk(fs, OP_TESTNIL, val_reg, val_reg, 0, 0);
      int jmp_to_default = luaK_jump(fs);
      
      /* table[key] 不为 nil，使用 table[key] 的值 */
      init_exp(&src, VNONRELOC, val_reg);
      luaK_exp2reg(fs, &src, target_reg);
      
      int jmp_end = luaK_jump(fs);
      
      /* table[key] 为 nil，使用默认值 */
      luaK_patchtohere(fs, jmp_to_default);
      
      /* 将默认值移到目标寄存器 */
      luaK_exp2reg(fs, &default_exps[i], target_reg);
      
      luaK_patchtohere(fs, jmp_end);
      
      /* 恢复 freereg */
      fs->freereg = target_reg + 1;
    } else {
      /* 无默认值，直接从表读取 */
      luaK_exp2reg(fs, &src, target_reg);
    }
    
    /* 重置 freereg 保护源表（在 source_reg 之后） */
    fs->freereg = source_reg + 1;
  }
  
  /* 第五阶段：一次性激活所有变量 */
  adjustlocalvars(ls, nvars);
  
  /* 释放源表临时寄存器，freereg 回到变量区域末尾 */
  fs->freereg = var_base + nvars;
}

/* ========================================================================= */
/* TYPE HINTING AND DESTRUCTURING SUPPORT                                   */
/* ========================================================================= */

static TypeHint *typehint_new(LexState *ls) {
  TypeHint *th = luaM_new(ls->L, TypeHint);
  for (int i = 0; i < MAX_TYPE_DESCS; i++) {
    th->descs[i].type = LVT_NONE;
    th->descs[i].nparam = -1;
    th->descs[i].nret = -1;
    th->descs[i].proto = NULL;
    th->descs[i].nfields = -1;
  }
  th->next = ls->all_type_hints;
  ls->all_type_hints = th;
  return th;
}

static void typehint_free(LexState *ls) {
  TypeHint *curr = ls->all_type_hints;
  while (curr) {
    TypeHint *next = curr->next;
    luaM_free(ls->L, curr);
    curr = next;
  }
  ls->all_type_hints = NULL;
}

static void th_emplace_desc(TypeHint *th, TypeDesc td) {
  for (int i = 0; i < MAX_TYPE_DESCS; i++) {
    if (th->descs[i].type == td.type) return; /* Already present */
    if (th->descs[i].type == LVT_NONE) {
      th->descs[i] = td;
      return;
    }
  }
  /* Full: degrade to ANY */
  th->descs[0].type = LVT_ANY;
  th->descs[1].type = LVT_NONE;
  th->descs[2].type = LVT_NONE;
}

static void checktypehint (LexState *ls, TypeHint *th);

static TypeHint* get_named_type_opt(LexState* ls, const TString* name) {
  const TValue *o = luaH_getstr(ls->named_types, (TString *)name);
  if (!ttisnil(o)) {
    return (TypeHint*)pvalue(o);
  }
  /* printf("DEBUG: named type '%s' not found\n", getstr(name)); */
  return NULL;
}

static void checktypehint (LexState *ls, TypeHint *th) {
  if (testnext(ls, '?')) {
    TypeDesc td; td.type = LVT_NULL;
    th_emplace_desc(th, td);
  }
  do {
    if (ls->t.token == '{') { /* Table type */
      luaX_next(ls);
      TypeDesc td;
      td.type = LVT_TABLE;
      td.nfields = 0;
      while (ls->t.token != '}') {
        TString *ts = str_checkname(ls);
        checknext(ls, ':');
        TypeHint *fieldth = typehint_new(ls);
        checktypehint(ls, fieldth);
        if (td.nfields < MAX_TYPED_FIELDS) {
          td.names[td.nfields] = ts;
          td.hints[td.nfields] = fieldth;
          td.nfields++;
        }
        if (!testnext(ls, ',') && !testnext(ls, ';')) break;
      }
      checknext(ls, '}');
      th_emplace_desc(th, td);
      continue;
    }
    
    const char *tname;
    TString *ts = NULL;
    if (ls->t.token == TK_FUNCTION) {
       tname = "function";
       luaX_next(ls);
    } else {
       ts = str_checkname(ls);
       tname = getstr(ts);
    }

    TypeDesc td;
    td.type = LVT_NONE;
    
    if (strcmp(tname, "number") == 0) td.type = LVT_NUMBER;
    else if (strcmp(tname, "int") == 0 || strcmp(tname, "integer") == 0) td.type = LVT_INT;
    else if (strcmp(tname, "float") == 0) td.type = LVT_FLT;
    else if (strcmp(tname, "table") == 0) td.type = LVT_TABLE;
    else if (strcmp(tname, "string") == 0) td.type = LVT_STR;
    else if (strcmp(tname, "boolean") == 0 || strcmp(tname, "bool") == 0) td.type = LVT_BOOL;
    else if (strcmp(tname, "function") == 0) {
      td.type = LVT_FUNC;
      td.nparam = -1;
      td.nret = -1;
      if (testnext(ls, '(')) {
         /* Parse params */
         td.nparam = 0;
         if (ls->t.token != ')') {
           do {
             if (ls->t.token == TK_NAME && luaX_lookahead(ls) == ':') {
               checknext(ls, TK_NAME); /* name */
               checknext(ls, ':');
             }
             if (td.nparam < MAX_TYPED_PARAMS) {
               td.params[td.nparam] = typehint_new(ls);
               checktypehint(ls, td.params[td.nparam]);
               td.nparam++;
             } else {
               TypeHint *ign = typehint_new(ls);
               checktypehint(ls, ign);
             }
           } while (testnext(ls, ','));
         }
         checknext(ls, ')');
      }
      if (ls->t.token == ':') {
         luaX_next(ls);
         td.nret = 0;
         if (testnext(ls, '(')) {
             do {
                 if (td.nret < MAX_TYPED_RETURNS) {
                     td.returns[td.nret] = typehint_new(ls);
                     checktypehint(ls, td.returns[td.nret]);
                     td.nret++;
                 } else {
                     TypeHint *ign = typehint_new(ls);
                     checktypehint(ls, ign);
                 }
             } while (testnext(ls, ','));
             checknext(ls, ')');
         } else {
             /* Single return type or void */
             if (ls->t.token == TK_NAME && strcmp(getstr(ls->t.seminfo.ts), "void") == 0) {
                 luaX_next(ls);
                 td.nret = 0;
             } else {
                 td.nret = 1;
                 td.returns[0] = typehint_new(ls);
                 checktypehint(ls, td.returns[0]);
             }
         }
      }
    }
    else if (strcmp(tname, "any") == 0) td.type = LVT_ANY;
    else if (strcmp(tname, "nil") == 0) td.type = LVT_NIL;
    else if (strcmp(tname, "void") == 0) {
       td.type = LVT_NULL;
    }
    else if (strcmp(tname, "userdata") == 0) td.type = LVT_USERDATA;
    else {
      TypeHint *named = get_named_type_opt(ls, ts);
      if (named) {
        /* Merge named type */
        for (int i=0; i<MAX_TYPE_DESCS; i++) {
           if (named->descs[i].type != LVT_NONE)
             th_emplace_desc(th, named->descs[i]);
        }
        td.type = LVT_NONE; /* processed */
      } else {
        /* Store unknown type name for runtime check */
        td.type = LVT_NAME;
        td.typename = ts;
      }
    }
    
    if (td.type != LVT_NONE) th_emplace_desc(th, td);
    
  } while (testnext(ls, '|'));
  
  if (testnext(ls, '?')) {
     TypeDesc td; td.type = LVT_NULL;
     th_emplace_desc(th, td);
  }
}

static TypeHint *gettypehint (LexState *ls) {
  if (testnext(ls, ':')) {
    TypeHint *th = typehint_new(ls);
    checktypehint(ls, th);
    return th;
  }
  return NULL;
}

static void check_type_compatibility(LexState *ls, TypeHint *target, expdesc *e) {
  /* Very basic check for literals */
  if (!target || !e) return;
  
  ValType e_type = LVT_NONE;
  if (e->k == VKINT) e_type = LVT_INT;
  else if (e->k == VKFLT) e_type = LVT_FLT;
  else if (e->k == VKSTR) e_type = LVT_STR;
  else if (e->k == VTRUE || e->k == VFALSE) e_type = LVT_BOOL;
  else if (e->k == VNIL) e_type = LVT_NIL;
  
  if (e_type == LVT_NONE) return; /* Unknown compile time type */
  
  int compatible = 0;
  for (int i=0; i<MAX_TYPE_DESCS; i++) {
    ValType t = target->descs[i].type;
    if (t == LVT_ANY) { compatible = 1; break; }
    if (t == e_type) { compatible = 1; break; }
    if (t == LVT_NUMBER && (e_type == LVT_INT || e_type == LVT_FLT)) { compatible = 1; break; }
    if (t == LVT_BOOL && (e_type == LVT_BOOL)) { compatible = 1; break; }
    if (t == LVT_NULL && e_type == LVT_NIL) { compatible = 1; break; }
  }
  
  if (!compatible) {
    luaX_warning(ls, "type mismatch", WT_TYPE_MISMATCH);
  }
}

/* Destructuring support */
static void destructuring (LexState *ls) {
   /* local {a, b} = t */
   TString *names[MAXVARS];
   int nnames = 0;
   luaX_next(ls); /* skip { */
   do {
     names[nnames++] = str_checkname(ls);
   } while (testnext(ls, ',') && nnames < MAXVARS);
   checknext(ls, '}');
   
   checknext(ls, '=');
   expdesc e;
   expr(ls, &e);
   
   int base = luaY_nvarstack(ls->fs);
   
   /* Move table to safe reg */
   luaK_exp2reg(ls->fs, &e, base + nnames);
   int tbl_reg = base + nnames;
   /* Reserve registers for locals + table temp */
   if (ls->fs->freereg < tbl_reg + 1)
       ls->fs->freereg = tbl_reg + 1;
   
   for (int i=0; i<nnames; i++) {
     new_localvar(ls, names[i]);
     
     expdesc t;
     init_exp(&t, VNONRELOC, tbl_reg); 
     expdesc k;
     init_exp(&k, VKSTR, 0);
     k.u.strval = names[i];
     
     luaK_indexed(ls->fs, &t, &k); 
     /* 't' now contains the indexed variable expression */
     
     expdesc lvar;
     init_exp(&lvar, VLOCAL, 0);
     lvar.u.var.vidx = 0;
     lvar.u.var.ridx = base + i;
     
     luaK_storevar(ls->fs, &lvar, &t);
   }
   
   adjustlocalvars(ls, nnames);
   ls->fs->freereg = base + nnames;
}

static void arraydestructuring (LexState *ls) {
   /* local [a, b] = t */
   TString *names[MAXVARS];
   int nnames = 0;
   luaX_next(ls); /* skip [ */
   do {
     names[nnames++] = str_checkname(ls);
   } while (testnext(ls, ',') && nnames < MAXVARS);
   checknext(ls, ']');
   
   checknext(ls, '=');
   expdesc e;
   expr(ls, &e);
   
   int base = luaY_nvarstack(ls->fs);
   
   /* Move table to safe reg */
   luaK_exp2reg(ls->fs, &e, base + nnames);
   int tbl_reg = base + nnames;
   /* Reserve registers for locals + table temp */
   if (ls->fs->freereg < tbl_reg + 1)
       ls->fs->freereg = tbl_reg + 1;
   
   for (int i=0; i<nnames; i++) {
     new_localvar(ls, names[i]);
     expdesc t;
     init_exp(&t, VNONRELOC, tbl_reg); 
     expdesc k;
     init_exp(&k, VKINT, 0);
     k.u.ival = i + 1;
     
     luaK_indexed(ls->fs, &t, &k); 
     /* 't' now contains the indexed variable expression */
     
     expdesc lvar;
     init_exp(&lvar, VLOCAL, 0);
     lvar.u.var.vidx = 0;
     lvar.u.var.ridx = base + i;
     
     luaK_storevar(ls->fs, &lvar, &t);
   }
   
   adjustlocalvars(ls, nnames);
   ls->fs->freereg = base + nnames;
}

static void localstat (LexState *ls, int isexport) {
  LUA_LOGD("[PARSER] localstat START, isexport=%d", isexport);
  if (ls->t.token == '{') {
    destructuring(ls);
    return;
  }
  if (ls->t.token == '[') {
    arraydestructuring(ls);
    return;
  }
  /* stat -> LOCAL ATTRIB NAME ATTRIB { ',' NAME ATTRIB } ['=' explist] */
  /* stat -> CONST NAME ATTRIB { ',' NAME ATTRIB } ['=' explist] */
  FuncState *fs = ls->fs;
  int toclose = -1;  /* index of to-be-closed variable (if any) */
  Vardesc *var;  /* last variable */
  int vidx, kind;
  int nvars = 0;
  int nexps;
  expdesc e;
  /* check if this is a const declaration */
  int isconst = (ls->lasttoken == TK_CONST);
  lu_byte defkind = getvarattribute(ls, isconst ? RDKCONST : VDKREG);
  
  do {
    TString *varname = str_checkname(ls);
    LUA_LOGD("[PARSER] localstat: varname='%s' nvars=%d", getstr(varname), nvars);
    /* 检查变量是否已经存在 */
    if (isconst) {
      /* 对于const声明，检查变量是否已经存在于当前作用域 */
      int i;
      for (i = cast_int(fs->nactvar) - 1; i >= 0; i--) {
        Vardesc *vd = getlocalvardesc(fs, i);
        if (eqstr(varname, vd->vd.name)) {  /* 找到同名变量 */
          /* 检查是否是const变量 */
          if (vd->vd.kind == RDKCONST || vd->vd.kind == RDKCTC) {
            luaK_semerror(ls, "const variable '%s' already defined", getstr(varname));
          }
          /* 非const变量允许被const重新声明，跳出循环 */
          break;
        }
      }
    }
    vidx = new_localvar(ls, varname);
    getlocalvardesc(fs, vidx)->vd.hint = gettypehint(ls);
    if (isexport) {
        add_export(ls, varname);
    }
    kind = getvarattribute(ls, defkind);
    getlocalvardesc(fs, vidx)->vd.kind = kind;
    if (kind == RDKTOCLOSE) {  /* to-be-closed? */
      if (toclose != -1)  /* one already present? */
        luaK_semerror(ls, "multiple to-be-closed variables in local list");
      toclose = fs->nactvar + nvars;
    }
    nvars++;
  } while (testnext(ls, ','));
  LUA_LOGD("[PARSER] localstat: after varlist, nvars=%d, current token=%d", nvars, ls->t.token);
  if (testnext(ls, '=')) {
    LUA_LOGD("[PARSER] localstat: parsing RHS expression");
    nexps = explist(ls, &e);
    LUA_LOGD("[PARSER] localstat: RHS done, nexps=%d", nexps);
    if (nvars == nexps) {
       Vardesc *lastvar = getlocalvardesc(fs, vidx);
       check_type_compatibility(ls, lastvar->vd.hint, &e);
    }
  }
  else {
    e.k = VVOID;
    nexps = 0;
    /* const variables must be initialized */
    if (isconst)
      luaK_semerror(ls, "const variable must be initialized");
  }
  var = getlocalvardesc(fs, vidx);  /* get last variable */
  if (nvars == nexps &&  /* no adjustments? */
      var->vd.kind == RDKCONST &&  /* last variable is const? */
      luaK_exp2const(fs, &e, &var->k)) {  /* compile-time constant? */
    var->vd.kind = RDKCTC;  /* variable is a compile-time constant */
    adjustlocalvars(ls, nvars - 1);  /* exclude last variable */
    fs->nactvar++;  /* but count it */
  }
  else {
    /* 管道/调用结果寄存器优化：
       RHS 为单值 VNONRELOC 且结果在 >= nvarstack 的寄存器时
       （如 local a = 3 |> f |> g，管道结果在 func_reg+1 远高于 nvarstack），
       直接移入目标寄存器，跳过 adjust_assign 避免多余的 exp2nextreg 操作。
       注意：带有条件跳转列表的表达式（and/or/??）不能走此路径，
       因为需要通过 luaK_exp2reg/luaK_storevar 正确 patch TESTSET 的目标寄存器和跳转地址。 */
    if (nvars == 1 && nexps == 1 && e.k != VVOID && !hasmultret(e.k) &&
        e.k == VNONRELOC && e.u.info >= luaY_nvarstack(fs) &&
        e.t == NO_JUMP && e.f == NO_JUMP) {
      int target_reg = luaY_nvarstack(fs);  /* 新局部变量的寄存器 */
      if (e.u.info != target_reg) {
        /* 管道结果在非目标寄存器，移入目标寄存器 */
        luaK_codeABC(fs, OP_MOVE, target_reg, e.u.info, 0);
      }
      adjustlocalvars(ls, nvars);  /* 激活局部变量 */
      fs->freereg = target_reg + nvars;
    } else {
      adjust_assign(ls, nvars, nexps, &e);
      adjustlocalvars(ls, nvars);
    }
  }
  checktoclose(fs, toclose);
  LUA_LOGD("[PARSER] localstat END, nvars=%d", nvars);
}


static lu_byte getglobalattribute (LexState *ls, lu_byte df) {
  lu_byte kind = getvarattribute(ls, df);
  switch (kind) {
    case RDKTOCLOSE:
      luaK_semerror(ls, "global variables cannot be to-be-closed");
      return kind;  /* to avoid warnings */
    case RDKCONST:
      return GDKCONST;  /* adjust kind for global variable */
    default:
      return kind;
  }
}


static void checkglobal (LexState *ls, TString *varname, int line) {
  FuncState *fs = ls->fs;
  expdesc var;
  int k;
  buildglobal(ls, varname, &var);  /* create global variable in 'var' */
  k = var.u.ind.keystr;  /* index of global name in 'k' */
  luaK_codecheckglobal(fs, &var, k, line);
}


/*
** Recursively traverse list of globals to be initalized. When
** going, generate table description for the global. In the end,
** after all indices have been generated, read list of initializing
** expressions. When returning, generate the assignment of the value on
** the stack to the corresponding table description. 'n' is the variable
** being handled, range [0, nvars - 1].
*/
static void initglobal (LexState *ls, int nvars, int firstidx, int n,
                        int line) {
  if (n == nvars) {  /* traversed all variables? */
    expdesc e;
    int nexps = explist(ls, &e);  /* read list of expressions */
    adjust_assign(ls, nvars, nexps, &e);
  }
  else {  /* handle variable 'n' */
    FuncState *fs = ls->fs;
    expdesc var;
    TString *varname = getlocalvardesc(fs, firstidx + n)->vd.name;
    buildglobal(ls, varname, &var);  /* create global variable in 'var' */
    enterlevel(ls);  /* control recursion depth */
    initglobal(ls, nvars, firstidx, n + 1, line);
    leavelevel(ls);
    checkglobal(ls, varname, line);
    storevartop(fs, &var);
  }
}


static void globalnames (LexState *ls, lu_byte defkind) {
  FuncState *fs = ls->fs;
  int nvars = 0;
  int lastidx;  /* index of last registered variable */
  do {  /* for each name */
    TString *vname = str_checkname(ls);
    lu_byte kind = getglobalattribute(ls, defkind);
    lastidx = new_varkind(ls, vname, kind);
    nvars++;
  } while (testnext(ls, ','));
  if (testnext(ls, '='))  /* initialization? */
    initglobal(ls, nvars, lastidx - nvars + 1, 0, ls->linenumber);
  fs->nactvar = (fs->nactvar + nvars);  /* activate declaration */
}


static void globalstat (LexState *ls) {
  /* globalstat -> (GLOBAL) attrib '*'
     globalstat -> (GLOBAL) attrib NAME attrib {',' NAME attrib} */
  FuncState *fs = ls->fs;
  /* get prefixed attribute (if any); default is regular global variable */
  lu_byte defkind = getglobalattribute(ls, GDKREG);
  if (!testnext(ls, '*'))
    globalnames(ls, defkind);
  else {
    /* use NULL as name to represent '*' entries */
    new_varkind(ls, NULL, defkind);
    fs->nactvar++;  /* activate declaration */
  }
}


static void globalfunc (LexState *ls, int line) {
  /* globalfunc -> (GLOBAL FUNCTION) NAME body */
  expdesc var, b;
  FuncState *fs = ls->fs;
  TString *fname = str_checkname(ls);
  new_varkind(ls, fname, GDKREG);  /* declare global variable */
  fs->nactvar++;  /* enter its scope */
  buildglobal(ls, fname, &var);
  body(ls, &b, 0, ls->linenumber);  /* compile and return closure in 'b' */
  checkglobal(ls, fname, line);
  luaK_storevar(fs, &var, &b);
  luaK_fixline(fs, line);  /* definition "happens" in the first line */
}


static void globalstatfunc (LexState *ls, int line) {
  /* stat -> GLOBAL globalfunc | GLOBAL globalstat */
  luaX_next(ls);  /* skip 'global' */
  if (testnext(ls, TK_FUNCTION))
    globalfunc(ls, line);
  else
    globalstat(ls);
}


static int funcname (LexState *ls, expdesc *v) {
  /* funcname -> NAME {fieldsel} [':' NAME] */
  int ismethod = 0;
  singlevar(ls, v);
  while (ls->t.token == '.')
    fieldsel(ls, v);
  if (ls->t.token == ':') {
    ismethod = 1;
    fieldsel(ls, v);
  }
  return ismethod;
}


/*
** ============================================================
** 内联汇编支持 (asm statement)
** ============================================================
*/

/*
** 汇编标签结构（用于跳转目标）
** 支持前向引用和后向引用
*/
#define ASM_INIT_LABELS 16    /* 标签初始容量 */
#define ASM_INIT_PENDING 32   /* 待修补跳转初始容量 */
#define ASM_INIT_DEFINES 16   /* 汇编常量定义初始容量 */

typedef struct AsmLabel {
  TString *name;    /* 标签名称 */
  int pc;           /* 标签对应的 PC 位置，-1 表示尚未定义 */
  int line;         /* 标签定义的行号 */
} AsmLabel;

typedef struct AsmPending {
  TString *label;   /* 目标标签名称 */
  int pc;           /* 需要修补的指令位置 */
  int line;         /* 引用标签的行号 */
  int isJump;       /* 是否是跳转指令（需要计算偏移） */
} AsmPending;

/*
** 汇编常量定义结构
** 用于 def 伪指令定义的编译期常量
*/
typedef struct AsmDefine {
  TString *name;    /* 常量名称 */
  lua_Integer value;/* 常量值 */
} AsmDefine;

typedef struct AsmContext {
  AsmLabel *labels;       /* 标签动态数组 */
  int nlabels;            /* 标签数量 */
  int labels_cap;         /* 标签数组容量 */
  AsmPending *pending;    /* 待修补跳转动态数组 */
  int npending;           /* 待修补数量 */
  int pending_cap;        /* 待修补数组容量 */
  AsmDefine *defines;     /* 常量定义动态数组 */
  int ndefines;           /* 常量定义数量 */
  int defines_cap;        /* 常量定义数组容量 */
  struct AsmContext *parent;  /* 父级上下文（用于嵌套 asm） */
} AsmContext;


/*
** 初始化汇编上下文
** 参数：
**   L - Lua 状态机（用于内存分配）
**   ctx - 汇编上下文
**   parent - 父级上下文（嵌套时非 NULL）
*/
static void asm_initcontext (lua_State *L, AsmContext *ctx, AsmContext *parent) {
  ctx->labels = luaM_newvector(L, ASM_INIT_LABELS, AsmLabel);
  ctx->nlabels = 0;
  ctx->labels_cap = ASM_INIT_LABELS;
  ctx->pending = luaM_newvector(L, ASM_INIT_PENDING, AsmPending);
  ctx->npending = 0;
  ctx->pending_cap = ASM_INIT_PENDING;
  ctx->defines = luaM_newvector(L, ASM_INIT_DEFINES, AsmDefine);
  ctx->ndefines = 0;
  ctx->defines_cap = ASM_INIT_DEFINES;
  ctx->parent = parent;
}


/*
** 释放汇编上下文
** 参数：
**   L - Lua 状态机
**   ctx - 汇编上下文
*/
static void asm_freecontext (lua_State *L, AsmContext *ctx) {
  luaM_freearray(L, ctx->labels, ctx->labels_cap);
  luaM_freearray(L, ctx->pending, ctx->pending_cap);
  luaM_freearray(L, ctx->defines, ctx->defines_cap);
  ctx->labels = NULL;
  ctx->pending = NULL;
  ctx->defines = NULL;
  ctx->nlabels = ctx->npending = ctx->ndefines = 0;
  ctx->labels_cap = ctx->pending_cap = ctx->defines_cap = 0;
}


/*
** 根据操作码名称查找对应的 OpCode
** 参数：
**   name - 操作码名称字符串
** 返回值：
**   对应的 OpCode，如果找不到则返回 -1
*/
static int find_opcode (const char *name) {
  int i;
  for (i = 0; opnames[i] != NULL; i++) {
    if (strcmp(opnames[i], name) == 0)
      return i;
  }
  return -1;
}


/*
** 在汇编上下文中查找标签
** 参数：
**   ctx - 汇编上下文
**   name - 标签名称
** 返回值：
**   标签索引，如果找不到则返回 -1
*/
static int asm_findlabel (AsmContext *ctx, TString *name) {
  int i;
  for (i = 0; i < ctx->nlabels; i++) {
    if (ctx->labels[i].name == name)
      return i;
  }
  return -1;
}


/*
** 定义汇编标签
** 参数：
**   ls - 词法状态
**   ctx - 汇编上下文
**   name - 标签名称
**   pc - 标签位置
**   line - 行号
*/
static void asm_deflabel (LexState *ls, AsmContext *ctx, TString *name, int pc, int line) {
  int idx = asm_findlabel(ctx, name);
  if (idx >= 0) {
    /* 标签已存在 */
    if (ctx->labels[idx].pc >= 0) {
      luaK_semerror(ls, "duplicate label '%s' in asm", getstr(name));
    }
    /* 标签之前被前向引用，现在定义它 */
    ctx->labels[idx].pc = pc;
    ctx->labels[idx].line = line;
  }
  else {
    /* 新标签 - 动态扩容 */
    if (ctx->nlabels >= ctx->labels_cap) {
      int newcap = ctx->labels_cap * 2;
      ctx->labels = luaM_reallocvector(ls->L, ctx->labels, ctx->labels_cap, newcap, AsmLabel);
      ctx->labels_cap = newcap;
    }
    ctx->labels[ctx->nlabels].name = name;
    ctx->labels[ctx->nlabels].pc = pc;
    ctx->labels[ctx->nlabels].line = line;
    ctx->nlabels++;
  }
}


/*
** 在汇编上下文中查找常量定义（包括父级上下文）
** 参数：
**   ctx - 汇编上下文
**   name - 常量名称
**   out_ctx - 输出参数，返回找到定义的上下文（可为 NULL）
** 返回值：
**   常量索引，如果找不到则返回 -1
*/
static int asm_finddefine_ex (AsmContext *ctx, TString *name, AsmContext **out_ctx) {
  AsmContext *cur = ctx;
  while (cur != NULL) {
    int i;
    for (i = 0; i < cur->ndefines; i++) {
      if (cur->defines[i].name == name) {
        if (out_ctx) *out_ctx = cur;
        return i;
      }
    }
    cur = cur->parent;  /* 向上查找父级上下文 */
  }
  if (out_ctx) *out_ctx = NULL;
  return -1;
}


/*
** 在汇编上下文中查找常量定义
** 参数：
**   ctx - 汇编上下文
**   name - 常量名称
** 返回值：
**   常量索引，如果找不到则返回 -1
*/
static int asm_finddefine (AsmContext *ctx, TString *name) {
  return asm_finddefine_ex(ctx, name, NULL);
}


/*
** 添加或更新汇编常量定义
** 参数：
**   ls - 词法状态
**   ctx - 汇编上下文
**   name - 常量名称
**   value - 常量值
*/
static void asm_adddefine (LexState *ls, AsmContext *ctx, TString *name, lua_Integer value) {
  int idx;
  int i;
  /* 只在当前上下文中查找（不向上查找父级） */
  for (i = 0; i < ctx->ndefines; i++) {
    if (ctx->defines[i].name == name) {
      /* 更新已存在的定义 */
      ctx->defines[i].value = value;
      return;
    }
  }
  /* 新定义 - 动态扩容 */
  if (ctx->ndefines >= ctx->defines_cap) {
    int newcap = ctx->defines_cap * 2;
    ctx->defines = luaM_reallocvector(ls->L, ctx->defines, ctx->defines_cap, newcap, AsmDefine);
    ctx->defines_cap = newcap;
  }
  ctx->defines[ctx->ndefines].name = name;
  ctx->defines[ctx->ndefines].value = value;
  ctx->ndefines++;
  (void)idx;  /* unused */
}


/*
** 引用汇编标签（可能是前向引用）
** 参数：
**   ls - 词法状态
**   ctx - 汇编上下文
**   name - 标签名称
** 返回值：
**   标签的 PC 位置，如果是前向引用则返回 -1
*/
static int asm_reflabel (LexState *ls, AsmContext *ctx, TString *name) {
  int idx = asm_findlabel(ctx, name);
  if (idx >= 0 && ctx->labels[idx].pc >= 0) {
    return ctx->labels[idx].pc;
  }
  /* 前向引用或未定义，先创建占位标签 */
  if (idx < 0) {
    /* 动态扩容 */
    if (ctx->nlabels >= ctx->labels_cap) {
      int newcap = ctx->labels_cap * 2;
      ctx->labels = luaM_reallocvector(ls->L, ctx->labels, ctx->labels_cap, newcap, AsmLabel);
      ctx->labels_cap = newcap;
    }
    ctx->labels[ctx->nlabels].name = name;
    ctx->labels[ctx->nlabels].pc = -1;  /* 尚未定义 */
    ctx->labels[ctx->nlabels].line = ls->linenumber;
    ctx->nlabels++;
  }
  return -1;  /* 返回 -1 表示需要后续修补 */
}


/*
** 添加待修补的跳转指令
** 参数：
**   ls - 词法状态
**   ctx - 汇编上下文
**   label - 目标标签名称
**   pc - 指令位置
**   line - 行号
**   isJump - 是否需要计算相对偏移
*/
static void asm_addpending (LexState *ls, AsmContext *ctx, TString *label, 
                            int pc, int line, int isJump) {
  /* 动态扩容 */
  if (ctx->npending >= ctx->pending_cap) {
    int newcap = ctx->pending_cap * 2;
    ctx->pending = luaM_reallocvector(ls->L, ctx->pending, ctx->pending_cap, newcap, AsmPending);
    ctx->pending_cap = newcap;
  }
  ctx->pending[ctx->npending].label = label;
  ctx->pending[ctx->npending].pc = pc;
  ctx->pending[ctx->npending].line = line;
  ctx->pending[ctx->npending].isJump = isJump;
  ctx->npending++;
}


/*
** 修补所有待处理的跳转指令
** 参数：
**   ls - 词法状态
**   fs - 函数状态
**   ctx - 汇编上下文
*/
static void asm_patchpending (LexState *ls, FuncState *fs, AsmContext *ctx) {
  int i;
  for (i = 0; i < ctx->npending; i++) {
    AsmPending *p = &ctx->pending[i];
    int idx = asm_findlabel(ctx, p->label);
    if (idx < 0 || ctx->labels[idx].pc < 0) {
      luaK_semerror(ls, "undefined label '%s' in asm", getstr(p->label));
    }
    int target = ctx->labels[idx].pc;
    Instruction *inst = &fs->f->code[p->pc];
    OpCode op = GET_OPCODE(*inst);
    
    if (p->isJump) {
      /* 计算相对偏移并修补跳转指令 */
      int offset = target - (p->pc + 1);  /* 相对于下一条指令的偏移 */
      if (getOpMode(op) == isJ) {
        /* isJ 格式：sJ 参数 */
        SETARG_sJ(*inst, offset);
      }
      else if (getOpMode(op) == iAsBx) {
        /* iAsBx 格式：sBx 参数 */
        SETARG_sBx(*inst, offset);
      }
      else {
        /* 其他格式：检查是否是 loop 相关指令 (iABx 格式) */
        if (op == OP_FORLOOP || op == OP_TFORLOOP) {
          /* FORLOOP/TFORLOOP 使用无符号 Bx 表示向后跳转偏移: pc -= Bx */
          /* offset = target - (pc + 1) */
          /* 所以 Bx = -offset = (pc + 1) - target */
          if (offset > 0) {
            luaK_semerror(ls, "jump target for loop instruction must be backward");
          }
          offset = -offset;
          if (offset > MAXARG_Bx) {
            luaK_semerror(ls, "control structure too long");
          }
          SETARG_Bx(*inst, cast_uint(offset));
        }
        else if (op == OP_FORPREP || op == OP_TFORPREP) {
          /* FORPREP/TFORPREP 使用无符号 Bx 表示向前跳转偏移: pc += Bx (+1 for FORPREP) */
          /* 注意：FORPREP 在 lvm.c 中 pc += Bx + 1，所以 Bx = offset - 1 */
          /* TFORPREP 在 lvm.c 中 pc += Bx，所以 Bx = offset */

          if (offset < 0) {
            luaK_semerror(ls, "jump target for prep instruction must be forward");
          }

          if (op == OP_FORPREP) offset--;

          if (offset < 0 || offset > MAXARG_Bx) {
             luaK_semerror(ls, "control structure too long or invalid target");
          }
          SETARG_Bx(*inst, cast_uint(offset));
        }
        else {
          /* 其他情况：直接设置为目标 PC */
          SETARG_Bx(*inst, cast_uint(target));
        }
      }
    }
    else {
      /* 非跳转指令，直接使用目标 PC */
      /* 根据指令格式设置适当的参数 */
      enum OpMode mode = getOpMode(op);
      if (mode == iABx || mode == iAsBx) {
        SETARG_Bx(*inst, cast_uint(target));
      }
      else if (mode == iAx) {
        SETARG_Ax(*inst, target);
      }
      else {
        /* iABC 格式，假设目标在 B 或 C 字段 */
        SETARG_B(*inst, target);
      }
    }
  }
}


/*
** 检查参数是否在有效范围内
** 参数：
**   ls - 词法状态
**   val - 参数值
**   max - 最大值
**   name - 参数名称（用于错误消息）
*/
static void asm_checkrange (LexState *ls, lua_Integer val, lua_Integer max, const char *name) {
  if (val < 0 || val > max) {
    luaK_semerror(ls, "asm %s out of range (got %lld, max %lld)", 
                  name, (long long)val, (long long)max);
  }
}


/*
** 检查带符号参数是否在有效范围内
** 参数：
**   ls - 词法状态
**   val - 参数值
**   min - 最小值
**   max - 最大值
**   name - 参数名称
*/
static void asm_checkrange_signed (LexState *ls, lua_Integer val, 
                                   lua_Integer min, lua_Integer max, const char *name) {
  if (val < min || val > max) {
    luaK_semerror(ls, "asm %s out of range (got %lld, range %lld to %lld)", 
                  name, (long long)val, (long long)min, (long long)max);
  }
}


/* Forward declarations for inline assembly expression parser */
static lua_Integer asm_get_expr_ex (LexState *ls, AsmContext *ctx, int *pendingPc, TString **pendingLabel, int *isLabelRef);
static lua_Integer asm_get_or (LexState *ls, AsmContext *ctx, int *pendingPc, TString **pendingLabel, int *isLabelRef);
static lua_Integer asm_get_xor (LexState *ls, AsmContext *ctx, int *pendingPc, TString **pendingLabel, int *isLabelRef);
static lua_Integer asm_get_and (LexState *ls, AsmContext *ctx, int *pendingPc, TString **pendingLabel, int *isLabelRef);
static lua_Integer asm_get_shift (LexState *ls, AsmContext *ctx, int *pendingPc, TString **pendingLabel, int *isLabelRef);
static lua_Integer asm_get_add (LexState *ls, AsmContext *ctx, int *pendingPc, TString **pendingLabel, int *isLabelRef);
static lua_Integer asm_get_mul (LexState *ls, AsmContext *ctx, int *pendingPc, TString **pendingLabel, int *isLabelRef);
static lua_Integer asm_get_unary (LexState *ls, AsmContext *ctx, int *pendingPc, TString **pendingLabel, int *isLabelRef);
static lua_Integer asm_get_primary (LexState *ls, AsmContext *ctx, int *pendingPc, TString **pendingLabel, int *isLabelRef);

static lua_Integer asm_get_primary (LexState *ls, AsmContext *ctx, 
                                    int *pendingPc, TString **pendingLabel, int *isLabelRef) {
  lua_Integer val;
  FuncState *fs = ls->fs;
  
  if (pendingPc) *pendingPc = -1;
  if (pendingLabel) *pendingLabel = NULL;
  if (isLabelRef) *isLabelRef = 0;
  
  if (ls->t.token == '(') {
    luaX_next(ls);  /* 跳过 '(' */
    val = asm_get_expr_ex(ls, ctx, pendingPc, pendingLabel, isLabelRef);
    checknext(ls, ')');
    return val;
  }
  else if (ls->t.token == TK_INT) {
    val = ls->t.seminfo.i;
    luaX_next(ls);
    return val;
  }
  else if (ls->t.token == TK_DOLLAR) {
    /* $varname 或 $(varname + offset) */
    luaX_next(ls);  /* 跳过 '$' */
    int has_paren = testnext(ls, '(');
    check(ls, TK_NAME);
    TString *varname = ls->t.seminfo.ts;
    luaX_next(ls);  /* 跳过变量名 */
    
    expdesc var;
    int varkind = searchvar(fs, varname, &var);
    if (varkind < 0) {
      luaK_semerror(ls, "undefined local variable '%s' in asm", getstr(varname));
    }
    val = var.u.var.ridx;
    
    if (has_paren) {
      if (testnext(ls, '+')) {
        val += asm_get_expr_ex(ls, ctx, pendingPc, pendingLabel, isLabelRef);
      }
      else if (testnext(ls, '-')) {
        val -= asm_get_expr_ex(ls, ctx, pendingPc, pendingLabel, isLabelRef);
      }
      checknext(ls, ')');
    }
    return val;
  }
  else if (ls->t.token == '%') {
    /* %n 或 %(expression) */
    luaX_next(ls);  /* 跳过 '%' */
    if (testnext(ls, '(')) {
      val = asm_get_expr_ex(ls, ctx, pendingPc, pendingLabel, isLabelRef);
      checknext(ls, ')');
    } else {
      check(ls, TK_INT);
      val = ls->t.seminfo.i;
      luaX_next(ls);
    }
    if (val < 0 || val > 255) {
      luaK_semerror(ls, "register index out of range (0-255) in asm: %lld", (long long)val);
    }
    return val;
  }
  else if (ls->t.token == TK_NAME) {
    /* 检查是否是 R(expression) / r(expression) 或 Rn/rn 格式 */
    const char *name = getstr(ls->t.seminfo.ts);
    TString *ts = ls->t.seminfo.ts;
    
    if ((strcmp(name, "R") == 0 || strcmp(name, "r") == 0) && luaX_lookahead(ls) == '(') {
      luaX_next(ls);  /* 跳过 'R' */
      luaX_next(ls);  /* 跳过 '(' */
      val = asm_get_expr_ex(ls, ctx, pendingPc, pendingLabel, isLabelRef);
      checknext(ls, ')');
      if (val < 0 || val > 255) {
        luaK_semerror(ls, "register index out of range (0-255) in asm: R(%lld)", (long long)val);
      }
      return val;
    }
    else if ((name[0] == 'R' || name[0] == 'r') && name[1] >= '0' && name[1] <= '9') {
      /* 解析寄存器编号 */
      val = 0;
      int i = 1;
      while (name[i] >= '0' && name[i] <= '9') {
        val = val * 10 + (name[i] - '0');
        i++;
      }
      if (name[i] == '\0') {  /* 确保格式正确，如 R0, R10, R255 */
        if (val > 255) {
          luaK_semerror(ls, "register index out of range (0-255) in asm: R%lld", (long long)val);
        }
        luaX_next(ls);
        return val;
      }
    }
    /* 检查是否是通过 def 定义的常量（包括父级上下文） */
    if (ctx != NULL) {
      AsmContext *found_ctx = NULL;
      int defIdx = asm_finddefine_ex(ctx, ts, &found_ctx);
      if (defIdx >= 0 && found_ctx != NULL) {
        luaX_next(ls);
        return found_ctx->defines[defIdx].value;
      }
    }
    /* 不是寄存器格式也不是定义的常量，报错 */
    luaX_syntaxerror(ls, "integer or expression expected in asm instruction");
    return 0;
  }
  else if (ls->t.token == '^') {
    /* ^varname - 获取 upvalue 的索引 */
    TString *varname;
    int idx;
    luaX_next(ls);  /* 跳过 '^' */
    check(ls, TK_NAME);
    varname = ls->t.seminfo.ts;
    idx = searchupvalue(fs, varname);
    if (idx < 0) {
      luaK_semerror(ls, "undefined upvalue '%s' in asm", getstr(varname));
    }
    luaX_next(ls);  /* 跳过变量名 */
    return idx;
  }
  else if (ls->t.token == '#') {
    /* #constant - 常量相关操作 */
    luaX_next(ls);  /* 跳过 '#' */
    if (ls->t.token == TK_STRING || ls->t.token == TK_RAWSTRING) {
      /* 字符串常量 - 添加到常量池并返回索引 */
      TString *s = ls->t.seminfo.ts;
      val = luaK_stringK(fs, s);
      luaX_next(ls);
      return val;
    }
    else if (ls->t.token == TK_INT) {
      /* #123 - 整数值本身 */
      val = ls->t.seminfo.i;
      luaX_next(ls);
      return val;
    }
    else if (ls->t.token == TK_FLT) {
      /* #3.14 - 浮点数值截断为整数 */
      val = (lua_Integer)ls->t.seminfo.r;
      luaX_next(ls);
      return val;
    }
    else if (ls->t.token == '-') {
      /* 负数常量 */
      luaX_next(ls);
      if (ls->t.token == TK_INT) {
        val = -ls->t.seminfo.i;
        luaX_next(ls);
        return val;
      }
      else if (ls->t.token == TK_FLT) {
        val = (lua_Integer)(-ls->t.seminfo.r);
        luaX_next(ls);
        return val;
      }
      else {
        luaX_syntaxerror(ls, "number expected after '#-' in asm");
        return 0;
      }
    }
    else if (ls->t.token == TK_NAME) {
      /* #K... - 添加到常量池 */
      const char *name = getstr(ls->t.seminfo.ts);
      if (name[0] == 'K' || name[0] == 'k') {
        if (name[1] == 'F' || name[1] == 'f') {
          luaX_next(ls);
          if (ls->t.token == TK_FLT) {
            val = luaK_numberK(fs, ls->t.seminfo.r);
            luaX_next(ls);
            return val;
          }
          else if (ls->t.token == TK_INT) {
            val = luaK_numberK(fs, (lua_Number)ls->t.seminfo.i);
            luaX_next(ls);
            return val;
          }
          else if (ls->t.token == '-') {
            luaX_next(ls);
            if (ls->t.token == TK_FLT) {
              val = luaK_numberK(fs, -ls->t.seminfo.r);
              luaX_next(ls);
              return val;
            }
            else if (ls->t.token == TK_INT) {
              val = luaK_numberK(fs, (lua_Number)(-ls->t.seminfo.i));
              luaX_next(ls);
              return val;
            }
          }
          luaX_syntaxerror(ls, "number expected after '#KF' in asm");
          return 0;
        }
        else if (name[1] == 'I' || name[1] == 'i' || name[1] == '\0') {
          luaX_next(ls);
          if (ls->t.token == TK_INT) {
            val = luaK_intK(fs, ls->t.seminfo.i);
            luaX_next(ls);
            return val;
          }
          else if (ls->t.token == '-') {
            luaX_next(ls);
            if (ls->t.token == TK_INT) {
              val = luaK_intK(fs, -ls->t.seminfo.i);
              luaX_next(ls);
              return val;
            }
          }
          luaX_syntaxerror(ls, "integer expected after '#K' in asm");
          return 0;
        }
      }
      luaX_syntaxerror(ls, "invalid constant specifier after '#' in asm");
      return 0;
    }
    else {
      luaX_syntaxerror(ls, "constant expected after '#' in asm");
      return 0;
    }
  }
  else if (ls->t.token == '@') {
    /* @ 或 @label - PC 位置或标签引用 */
    luaX_next(ls);
    if (ls->t.token == TK_NAME && ctx != NULL) {
      TString *labelname = ls->t.seminfo.ts;
      int labelIdx = asm_findlabel(ctx, labelname);
      int defIdx = asm_finddefine(ctx, labelname);
      if (labelIdx >= 0 || defIdx < 0) {
        int labelpc = asm_reflabel(ls, ctx, labelname);
        luaX_next(ls);
        if (labelpc < 0) {
          if (pendingLabel) *pendingLabel = labelname;
          return 0;
        }
        if (isLabelRef) *isLabelRef = 1;
        return labelpc;
      }
    }
    return fs->pc;
  }
  else if (ls->t.token == TK_NOT) {
    /* !specifier - 特殊值 */
    luaX_next(ls);
    check(ls, TK_NAME);
    const char *specname = getstr(ls->t.seminfo.ts);
    luaX_next(ls);
    
    if (strcmp(specname, "freereg") == 0) {
      return fs->freereg;
    }
    else if (strcmp(specname, "nactvar") == 0) {
      return fs->nactvar;
    }
    else if (strcmp(specname, "pc") == 0) {
      return fs->pc;
    }
    else if (strcmp(specname, "nk") == 0) {
      return fs->nk;
    }
    else if (strcmp(specname, "np") == 0) {
      return fs->np;
    }
    else {
      luaK_semerror(ls, "unknown special value '!%s' in asm", specname);
      return 0;
    }
  }
  else {
    luaX_syntaxerror(ls, "integer or expression expected in asm instruction");
    return 0;
  }
}

static lua_Integer asm_get_unary (LexState *ls, AsmContext *ctx, int *pendingPc, TString **pendingLabel, int *isLabelRef) {
  if (testnext(ls, '-')) {
    return -asm_get_unary(ls, ctx, pendingPc, pendingLabel, isLabelRef);
  }
  else if (testnext(ls, '~')) {
    return ~asm_get_unary(ls, ctx, pendingPc, pendingLabel, isLabelRef);
  }
  else {
    return asm_get_primary(ls, ctx, pendingPc, pendingLabel, isLabelRef);
  }
}

static lua_Integer asm_get_mul (LexState *ls, AsmContext *ctx, int *pendingPc, TString **pendingLabel, int *isLabelRef) {
  lua_Integer val = asm_get_unary(ls, ctx, pendingPc, pendingLabel, isLabelRef);
  for (;;) {
    if (testnext(ls, '*')) {
      val *= asm_get_unary(ls, ctx, pendingPc, pendingLabel, isLabelRef);
    }
    else if (testnext(ls, '/')) {
      lua_Integer denom = asm_get_unary(ls, ctx, pendingPc, pendingLabel, isLabelRef);
      if (denom == 0) luaK_semerror(ls, "division by zero in asm expression");
      val /= denom;
    }
    else if (testnext(ls, '%')) {
      lua_Integer denom = asm_get_unary(ls, ctx, pendingPc, pendingLabel, isLabelRef);
      if (denom == 0) luaK_semerror(ls, "division by zero in asm expression");
      val %= denom;
    }
    else if (testnext(ls, TK_IDIV)) {  /* // */
      lua_Integer denom = asm_get_unary(ls, ctx, pendingPc, pendingLabel, isLabelRef);
      if (denom == 0) luaK_semerror(ls, "division by zero in asm expression");
      val /= denom;
    }
    else {
      break;
    }
  }
  return val;
}

static lua_Integer asm_get_add (LexState *ls, AsmContext *ctx, int *pendingPc, TString **pendingLabel, int *isLabelRef) {
  lua_Integer val = asm_get_mul(ls, ctx, pendingPc, pendingLabel, isLabelRef);
  for (;;) {
    if (testnext(ls, '+')) {
      val += asm_get_mul(ls, ctx, pendingPc, pendingLabel, isLabelRef);
    }
    else if (testnext(ls, '-')) {
      val -= asm_get_mul(ls, ctx, pendingPc, pendingLabel, isLabelRef);
    }
    else {
      break;
    }
  }
  return val;
}

static lua_Integer asm_get_shift (LexState *ls, AsmContext *ctx, int *pendingPc, TString **pendingLabel, int *isLabelRef) {
  lua_Integer val = asm_get_add(ls, ctx, pendingPc, pendingLabel, isLabelRef);
  for (;;) {
    if (testnext(ls, TK_SHL)) {  /* << */
      val <<= asm_get_add(ls, ctx, pendingPc, pendingLabel, isLabelRef);
    }
    else if (testnext(ls, TK_SHR)) {  /* >> */
      val >>= asm_get_add(ls, ctx, pendingPc, pendingLabel, isLabelRef);
    }
    else {
      break;
    }
  }
  return val;
}

static lua_Integer asm_get_and (LexState *ls, AsmContext *ctx, int *pendingPc, TString **pendingLabel, int *isLabelRef) {
  lua_Integer val = asm_get_shift(ls, ctx, pendingPc, pendingLabel, isLabelRef);
  while (testnext(ls, '&')) {
    val &= asm_get_shift(ls, ctx, pendingPc, pendingLabel, isLabelRef);
  }
  return val;
}

static lua_Integer asm_get_xor (LexState *ls, AsmContext *ctx, int *pendingPc, TString **pendingLabel, int *isLabelRef) {
  lua_Integer val = asm_get_and(ls, ctx, pendingPc, pendingLabel, isLabelRef);
  while (testnext(ls, '~')) {  /* binary XOR */
    val ^= asm_get_and(ls, ctx, pendingPc, pendingLabel, isLabelRef);
  }
  return val;
}

static lua_Integer asm_get_or (LexState *ls, AsmContext *ctx, int *pendingPc, TString **pendingLabel, int *isLabelRef) {
  lua_Integer val = asm_get_xor(ls, ctx, pendingPc, pendingLabel, isLabelRef);
  while (testnext(ls, '|')) {
    val |= asm_get_xor(ls, ctx, pendingPc, pendingLabel, isLabelRef);
  }
  return val;
}

static lua_Integer asm_get_expr_ex (LexState *ls, AsmContext *ctx, int *pendingPc, TString **pendingLabel, int *isLabelRef) {
  return asm_get_or(ls, ctx, pendingPc, pendingLabel, isLabelRef);
}

static lua_Integer asm_getint_ex (LexState *ls, AsmContext *ctx, 
                                   int *pendingPc, TString **pendingLabel, int *isLabelRef) {
  return asm_get_expr_ex(ls, ctx, pendingPc, pendingLabel, isLabelRef);
}


/*
** 解析汇编指令中的整数参数（简化版，不支持前向标签引用）
*/
static lua_Integer asm_getint (LexState *ls) {
  return asm_getint_ex(ls, NULL, NULL, NULL, NULL);
}


/*
** 尝试解析汇编指令中的可选整数参数
** 如果下一个 token 不是有效的参数格式，则返回默认值
** 支持格式：整数、负号、$varname、^varname、#constant、@、!specifier
** 参数：
**   ls - 词法状态
**   defval - 默认值
** 返回值：
**   解析到的整数值，或默认值
*/
static lua_Integer asm_trygetint (LexState *ls, lua_Integer defval) {
  if (ls->t.token == TK_INT || ls->t.token == '-' || ls->t.token == '~' ||
      ls->t.token == TK_DOLLAR || ls->t.token == '^' || 
      ls->t.token == '#' || ls->t.token == TK_OR ||
      ls->t.token == TK_NOT || ls->t.token == '%' ||
      ls->t.token == '(') {
    return asm_getint(ls);
  }
  /* 检查是否是 Rn 格式的寄存器引用 */
  if (ls->t.token == TK_NAME) {
    const char *name = getstr(ls->t.seminfo.ts);
    if ((name[0] == 'R' || name[0] == 'r') && name[1] >= '0' && name[1] <= '9') {
      return asm_getint(ls);
    }
    if (strcmp(name, "R") == 0 || strcmp(name, "r") == 0) {
      return asm_getint(ls);
    }
  }
  return defval;
}


/*
** 尝试解析带标签支持的可选整数参数
*/
static lua_Integer asm_trygetint_ex (LexState *ls, AsmContext *ctx,
                                      lua_Integer defval,
                                      int *pendingPc, TString **pendingLabel, int *isLabelRef) {
  if (ls->t.token == TK_INT || ls->t.token == '-' || ls->t.token == '~' ||
      ls->t.token == TK_DOLLAR || ls->t.token == '^' || 
      ls->t.token == '#' || ls->t.token == TK_OR ||
      ls->t.token == TK_NOT || ls->t.token == '%' ||
      ls->t.token == '(') {
    return asm_getint_ex(ls, ctx, pendingPc, pendingLabel, isLabelRef);
  }
  /* 检查是否是 Rn 格式的寄存器引用 */
  if (ls->t.token == TK_NAME) {
    const char *name = getstr(ls->t.seminfo.ts);
    if ((name[0] == 'R' || name[0] == 'r') && name[1] >= '0' && name[1] <= '9') {
      return asm_getint_ex(ls, ctx, pendingPc, pendingLabel, isLabelRef);
    }
    if (strcmp(name, "R") == 0 || strcmp(name, "r") == 0) {
      return asm_getint_ex(ls, ctx, pendingPc, pendingLabel, isLabelRef);
    }
    /* Check defines */
    if (ctx != NULL) {
       if (asm_finddefine_ex(ctx, ls->t.seminfo.ts, NULL) >= 0) {
          return asm_getint_ex(ls, ctx, pendingPc, pendingLabel, isLabelRef);
       }
    }
  }
  if (pendingPc) *pendingPc = -1;
  if (pendingLabel) *pendingLabel = NULL;
  if (isLabelRef) *isLabelRef = 0;
  return defval;
}






/*
** 前向声明：递归解析 asm 块主体
*/
/*
** 发射一条跳转指令，自动处理前向引用和后向引用
*/
static void asm_emit_jmp (LexState *ls, FuncState *fs, AsmContext *ctx, TString *label, int line) {
  int labelIdx = asm_findlabel(ctx, label);
  if (labelIdx >= 0 && ctx->labels[labelIdx].pc >= 0) {
    /* 后向跳转 */
    int target_pc = ctx->labels[labelIdx].pc;
    int current_pc = fs->pc;
    int offset = target_pc - (current_pc + 1);  /* 相对于下一条指令的偏移 */
    Instruction jmp_inst = CREATE_sJ(OP_JMP, offset + OFFSET_sJ, 0);
    luaK_code(fs, jmp_inst);
    luaK_fixline(fs, line);
  }
  else {
    /* 前向跳转 - 添加到待修补列表 */
    int instpc = fs->pc;
    Instruction jmp_inst = CREATE_sJ(OP_JMP, OFFSET_sJ, 0);
    luaK_code(fs, jmp_inst);
    luaK_fixline(fs, line);
    asm_addpending(ls, ctx, label, instpc, line, 1);
  }
}


static void asm_parse_body (LexState *ls, FuncState *fs, AsmContext *ctx, int line);

/*
** astparser 语法块：块内使用 AST 解析器解析正常 Lua 代码
** 语法: astparser( Lua代码 )
*/

/* sentinel marker: 标记 string-mode astparser CClosure 的 upvalue2 为"延迟模式"
 * （与原路径 inputmode=ast 产生的 CClosure (upvalue2=NULL) 区分）
 * 声明为全局，last_parse.c 的延迟模式也需引用同一地址作为 sentinel。
 * 放在 astparser_runner 之前以便可见。 */
/* AstChunk* 与 sentinel 比较时，避免不同指针类型比较告警：
 * AstChunk chunk; sentinel 用 char[]，比较时 cast 到 const void* */
char astparser_string_mode_sentinel[1];  /* 地址作为 sentinel，值无意义 */


/*
** astparser_runner - C 闭包回调函数
** 
** 当 astparser 创建的 callable 闭包被调用时执行。
** 从 upvalue 获取 Proto* 和 AstChunk*，创建 Lua 闭包并执行。
** 
** 参数：
**   L - Lua 状态，参数由调用者传入
** 返回值：
**   返回 Lua 闭包执行的结果数量
*/

/*
** 设置 AST Lua table 的 metatable（使 tbl:unparse() 等方法可用）
** 与 lastlib.c parse_src_to_ast_table 保持一致：
**   meta.__index = ast 模块表（registry["ASTLIB_MODULE_TABLE"]）
** metatable 缓存于 registry["ASTLIB_META_AST_TABLE"]，避免每次重建
*/
static void set_ast_metatable(lua_State *L, int tbl_idx) {
  tbl_idx = lua_absindex(L, tbl_idx);
  /* 尝试从缓存取 metatable */
  lua_getfield(L, LUA_REGISTRYINDEX, "ASTLIB_META_AST_TABLE");
  if (!lua_istable(L, -1)) {
    lua_pop(L, 1);  /* 弹 nil */
    lua_newtable(L);  /* 新 meta */
    lua_getfield(L, LUA_REGISTRYINDEX, "ASTLIB_MODULE_TABLE");
    if (lua_istable(L, -1)) {
      lua_setfield(L, -2, "__index");  /* meta.__index = astmod */
    } else {
      lua_pop(L, 1);  /* astmod 未注册：不设 __index，用户仍可用 ast.xxx(tbl) 形式 */
    }
    lua_pushvalue(L, -1);
    lua_setfield(L, LUA_REGISTRYINDEX, "ASTLIB_META_AST_TABLE");  /* 缓存 */
  }
  /* -1: metatable */
  lua_setmetatable(L, tbl_idx);
}

int astparser_runner (lua_State *L) {
  Proto *p = (Proto *)lua_touserdata(L, lua_upvalueindex(1));
  AstChunk *chunk = (AstChunk *)lua_touserdata(L, lua_upvalueindex(2));
  StkId func = L->ci->func.p;
  int nargs = cast_int(L->top.p - (func + 1));

  /* ============================================================
   * 延迟模式（string-mode astparser("source") / astparser([[src]]) 路径）：
   * upvalue[2] == &astparser_string_mode_sentinel 表示延迟模式，
   * upvalue[1] 是 registry 中 serialized_ast_table 的整数引用。
   * 这种模式下 parse-time 只做 parse+serialize，codegen 放到运行时，确保 codegen 路径
   * 与 inputmode=ast 完全一致（避免 stack-local Dyndata 导致的 UB 与反序列化 bug）。
   * ============================================================ */
  if ((const void*)chunk == (const void*)astparser_string_mode_sentinel) {
    int ast_ref = (int)(uintptr_t)p;

    /* 检测选项 */
    int want_ast = 0;
    int inputmode_ast = 0;
    if (nargs >= 1 && lua_istable(L, 1)) {
      lua_getfield(L, 1, "ast");
      want_ast = lua_toboolean(L, -1);
      lua_pop(L, 1);
      lua_getfield(L, 1, "inputmode");
      if (lua_isstring(L, -1) && strcmp(lua_tostring(L, -1), "ast") == 0) {
        inputmode_ast = 1;
      }
      lua_pop(L, 1);
    }

    /* 从 registry 取出 serialized AST table */
    lua_rawgeti(L, LUA_REGISTRYINDEX, ast_ref);
    if (!lua_istable(L, -1)) {
      lua_pop(L, 1);
      return luaL_error(L, "astparser: serialized AST not found in registry");
    }

    /* {ast=true} → 直接返回序列化的 AST table（与原路径一致） */
    if (want_ast || inputmode_ast) {
      /* inputmode=ast 语义要求先"执行 inner→得到 AST table"，我们直接把它 push 到返回位置 */
      if (inputmode_ast) {
        /* 先返回 AST table，再走 ast→closure 的变换：把 table 留在栈顶供后续 codegen */
        /* inputmode_ast 会在下面继续复用现有 codegen 逻辑 */
      } else {
        /* want_ast: 直接返回 table（1 返回值），从 registry 取出的 table 在栈顶，留在那里 */
        set_ast_metatable(L, -1);
        luaL_unref(L, LUA_REGISTRYINDEX, ast_ref);  /* 用完释放引用 */
        /* 将当前 CClosure upvalue 改为 (NULL,NULL)，防止后续调用再访问已释放的 ast_ref */
        {
          CClosure *cc = clCvalue(s2v(func));
          /* upvalue 是 lightuserdata（非 collectable），无需 write barrier */
          setpvalue(&cc->upvalue[0], NULL);
          setpvalue(&cc->upvalue[1], NULL);
        }
        return 1;
      }
    }

    /* codegen：复用 inputmode=ast 路径的核心逻辑（已验证无问题） */
    if (!inputmode_ast) {
      /* 默认模式：从 AST table 反序列化+codegen，返回最终运行闭包 */
    }
    /* 不管是 inputmode=ast 还是默认模式，现在栈顶都是 serialized AST table，直接走同一套 codegen */
    {
      AstChunk *new_chunk = ast_deserialize_from_lua(L, -1);
      new_chunk->main_func->is_vararg = 1;
      Dyndata temp_dyd;
      memset(&temp_dyd, 0, sizeof(temp_dyd));
      Proto *new_p = luaY_codegen_chunk(L, new_chunk, &temp_dyd);
      ast_pool_free(new_chunk->pool);
      luaM_free(L, new_chunk->pool);
      lua_pop(L, 1);  /* 弹出 AST table */

      /* ============================================================
       * 缓存已 codegen 的结果，避免同一 CClosure 反复调用时反复 deserialize/codegen。
       * 将当前 CClosure 的两个 upvalue 改写成：
       *   upvalue[0] = new_p (Proto*)  [lightuserdata]
       *   upvalue[1] = NULL            [标记非 delay 模式]
       * 这样下次调用同一 CClosure 时，chunk != sentinel，会走下方「原路径」分支，
       * 直接用 new_p 生成 LuaClosure 执行，不再访问 registry ast_ref。
       * 改写完成后立即 luaL_unref(ast_ref)，避免泄漏 registry 引用。
       *
       * inputmode_ast 分支：同样改写 upvalue（当前 CClosure 用完即弃，改为 NULL），
       * 但返回的是新包装的 CClosure(new_p,NULL)，不是当前这个。
       * ============================================================ */
      {
        CClosure *cc = clCvalue(s2v(func));
        if (inputmode_ast) {
          setpvalue(&cc->upvalue[0], NULL);
          setpvalue(&cc->upvalue[1], NULL);
        } else {
          setpvalue(&cc->upvalue[0], new_p);
          setpvalue(&cc->upvalue[1], NULL);
        }
      }
      luaL_unref(L, LUA_REGISTRYINDEX, ast_ref);

      /* inputmode="ast"：和原路径一致，返回包装了新 Proto 的 CClosure(runner)
       * （不立即执行；用户语义：parser({inputmode="ast"}) 得到新可执行 chunk） */
      if (inputmode_ast) {
        lua_pushlightuserdata(L, new_p);
        lua_pushlightuserdata(L, NULL);
        lua_pushcclosure(L, astparser_runner, 2);
        return 1;
      }

      /* ============================================================
       * 默认模式：通过 lua_pcall 执行 inner Lua Closure（安全可重入，支持 pcall 嵌套）。
       *
       * 栈布局修复：必须把 inner LuaClosure 和参数从 CClosure 的参数起始槽 func+1 开始写入
       * （覆盖原参数区），而不是 push 到新栈顶。否则 lua_pcall 结果会放在新栈顶，
       * 用 L->top - (L->ci.func + 1) 计数会错误地把中间槽也算入返回值（比如
       * add_kw(100,7) 会算成 4 个返回值而不是 1 个）。
       * ============================================================ */
      {
        LClosure *cl = luaF_newLclosure(L, new_p->sizeupvalues);
        cl->p = new_p;
        luaF_initupvals(L, cl);
        if (cl->nupvalues > 0) {
          lua_rawgeti(L, LUA_REGISTRYINDEX, LUA_RIDX_GLOBALS);
          setobj(L, cl->upvals[0]->v.p, s2v(L->top.p - 1));
          L->top.p--;
        }
        /* 必须先把参数从 (func+1..func+nargs) 搬到 (base+1..base+nargs)，
         * 再把 LuaClosure 写到 base=func+1：否则 i=0 读 func+1 时会读到刚写的 LuaClosure，
         * 参数被覆盖成函数值（比如 real_add(100,7) 的 x 会变成 LuaClosure，算术报错）。 */
        StkId base = func + 1;
        int i;
        for (i = nargs - 1; i >= 0; i--) {
          setobj2s(L, base + 1 + i, s2v(func + 1 + i));
        }
        setclLvalue(L, s2v(base), cl);  /* 最后写 LuaClosure，避免覆盖参数源 */
        L->top.p = base + 1 + nargs;
        if (lua_pcall(L, nargs, LUA_MULTRET, 0) != LUA_OK) lua_error(L);
        return cast_int(L->top.p - base);
      }
      /* （原 inputmode_ast 分支在新的代码结构里不会到达 — 因为 lua_pcall 分支直接 return） */
      (void)inputmode_ast;
    }
  }

  /* 以下为原路径（char-mode astparser( char stream )） */
  
  /* 检测 {ast=true} 选项 */
  if (nargs >= 1 && lua_istable(L, 1)) {
    lua_getfield(L, 1, "ast");
    if (lua_toboolean(L, -1)) {
      lua_pop(L, 1);
      /* 序列化 AST 并返回 */
      ast_serialize_to_lua(L, chunk);
      set_ast_metatable(L, -1);
      /* 释放 AST 内存 */
      ast_pool_free(chunk->pool);
      luaM_free(L, chunk->pool);
      return 1;
    }
    lua_pop(L, 1);
  }
  
  /* 检测 {inputmode="ast"} 选项 */
  int inputmode_ast = 0;
  if (nargs >= 1 && lua_istable(L, 1)) {
    lua_getfield(L, 1, "inputmode");
    if (lua_isstring(L, -1) && strcmp(lua_tostring(L, -1), "ast") == 0) {
      inputmode_ast = 1;
    }
    lua_pop(L, 1);
  }
  
  /* 创建 Lua 闭包 */
  LClosure *cl = luaF_newLclosure(L, p->sizeupvalues);
  cl->p = p;
  luaF_initupvals(L, cl);
  
  /* 设置 _ENV upvalue 为全局表 */
  if (cl->nupvalues > 0) {
    lua_rawgeti(L, LUA_REGISTRYINDEX, LUA_RIDX_GLOBALS);
    setobj(L, cl->upvals[0]->v.p, s2v(L->top.p - 1));
    L->top.p--;
  }
  
  /* {inputmode="ast"} 模式：先 lua_pcall 执行 inner Lua Closure 获取 AST table，
   * 反序列化后 codegen 生成新 Proto，再包装 CClosure 返回。
   * 栈布局与 delay 路径保持一致：LuaClosure+参数覆盖写入 func+1..func+1+nargs，
   * lua_pcall(nargs, 1, 0) 结果（AST table）在 func+1，之后在原地继续 codegen + push CClosure。 */
  if (inputmode_ast) {
    StkId base2 = func + 1;
    int i;
    /* 先倒序搬参数：func+1+i → base2+1+i，防止 i=0 时写 base2+1 覆盖 func+1（同一槽）
     * 虽然 nargs>0 时 base2+1 = func+2，不会和 func+1 相同，但统一写法更安全。 */
    for (i = nargs - 1; i >= 0; i--) {
      setobj2s(L, base2 + 1 + i, s2v(func + 1 + i));
    }
    setclLvalue(L, s2v(base2), cl);  /* 最后写 LuaClosure，避免覆盖参数源 */
    L->top.p = base2 + 1 + nargs;
    if (lua_pcall(L, nargs, 1, 0) != LUA_OK) lua_error(L);
    if (!lua_istable(L, -1)) {
      luaG_runerror(L, "astparser: inputmode='ast' expects inner function to return an AST table");
    }
    AstChunk *new_chunk = ast_deserialize_from_lua(L, -1);
    /* 主函数必须是 vararg（和原解析路径保持一致） */
    new_chunk->main_func->is_vararg = 1;
    /* 编译：需要临时 Dyndata */
    Dyndata temp_dyd;
    memset(&temp_dyd, 0, sizeof(temp_dyd));
    Proto *new_p = luaY_codegen_chunk(L, new_chunk, &temp_dyd);
    /* 释放反序列化的 AST */
    ast_pool_free(new_chunk->pool);
    luaM_free(L, new_chunk->pool);
    lua_pop(L, 1);  /* 弹出 AST table（lua_pcall 结果已在 func+1，pop 后 L->top = func+1） */
    /* 创建新的 C 闭包：push 到当前栈顶 func+1，即 astparser_runner 的返回值 */
    lua_pushlightuserdata(L, new_p);
    lua_pushlightuserdata(L, NULL);  /* chunk=NULL 表示无 AST */
    lua_pushcclosure(L, astparser_runner, 2);
    return 1;
  }
  
  /* ============================================================
   * 默认模式：通过 lua_pcall 执行 Lua Closure（p），安全可重入。
   * 用户语义：local f = astparser("return 5"); local r = f();
   *           f() 返回 5（和 luaL_loadstring + 立即调用相同）。
   *
   * 栈布局注意事项：
   * - CClosure(func) 位于 L->ci->func.p，参数从 func+1 开始。
   * - lua_pcall 执行完后结果必须放在 func+1..func+nrets（C 函数返回值约定），
   *   所以不能把 inner LuaClosure 压到新的栈顶（会让结果跑飞），
   *   正确做法：把 LuaClosure 写到 func+1（覆盖原参数区起始），
   *   参数搬到 func+2..func+1+nargs，再 lua_pcall(nargs, LUA_MULTRET)。
   *   这样 lua_pcall 自动把结果覆盖写在 func+1 起始，最终 L->top - (func+1) 即实际结果数。
   * ============================================================ */
  {
    StkId base = func + 1;  /* CClosure 参数/返回值起始槽（lua_pcall 结果也必须放这） */
    int i;
    /* 先倒序搬参数：func+1+i → base+1+i，防止 i=0 时覆盖源参数（与 delay/inputmode_ast 两条路径保持一致）。
     * 正序搬运 bug：i=0 写 base+1(=func+2) 后，i=1 再读 func+2 时会读到刚被覆盖的值。 */
    for (i = nargs - 1; i >= 0; i--) {
      setobj2s(L, base + 1 + i, s2v(func + 1 + i));
    }
    setclLvalue(L, s2v(base), cl);  /* 最后写 LuaClosure，避免覆盖参数源 */
    L->top.p = base + 1 + nargs;  /* 校正栈顶 */
    if (lua_pcall(L, nargs, LUA_MULTRET, 0) != LUA_OK) lua_error(L);
    /* lua_pcall 成功后 base+1..base+nrets 已经是返回值序列 */
    return cast_int(L->top.p - base);
  }
}


/*
** 内联汇编语句解析（内部版本，支持嵌套）
** 参数：
**   ls - 词法状态
**   line - 起始行号
**   parent_ctx - 父级上下文（用于嵌套 asm，可为 NULL）
*/
static void asmstat_ex (LexState *ls, int line, AsmContext *parent_ctx);


/*
** 内联汇编语句解析
** 语法: asm( 指令序列 )
** 
** 指令格式:
**   OPCODE A B C [k]    -- iABC 格式
**   OPCODE A Bx         -- iABx 格式
**   OPCODE A sBx        -- iAsBx 格式
**   OPCODE sJ           -- isJ 格式
**   OPCODE Ax           -- iAx 格式
** 
** 辅助语法（可用于任何参数位置）:
**   $varname   - 获取局部变量的寄存器编号
**   ^varname   - 获取 upvalue 的索引
**   #"str"     - 获取字符串常量的常量池索引
**   #123       - 直接返回整数值（用于 LOADI 等）
**   #K 123     - 将整数添加到常量池并返回索引
**   #KF 3.14   - 将浮点数添加到常量池并返回索引
**   @          - 获取当前 PC 位置
**   @label     - 获取标签的 PC 位置（支持前向引用）
**   :label     - 定义标签（标记当前 PC 位置）
**   !freereg   - 当前空闲寄存器编号
**   !nactvar   - 当前活跃局部变量数量
**   !pc        - 当前 PC
**   !nk        - 当前常量池大小
**   !np        - 当前子函数原型数量
** 
** 示例:
**   asm( MOVE $b $a )           -- b = a
**   asm( LOADI $x 100 )         -- x = 100
**   asm( GETUPVAL $local ^upv ) -- local = upvalue
**   asm( LOADK $s #"hello" )    -- s = "hello"
**   asm( LOADK $n #K 42 )       -- n = 常量池中的 42
**   asm( :loop; ... ; JMP @loop ) -- 循环
** 
** 参数：
**   ls - 词法状态
**   line - 起始行号
*/
static void asmstat (LexState *ls, int line) {
  asmstat_ex(ls, line, NULL);
}


/* dummy reader for in-memory ZIO: always returns EOF */
static const char *astparser_zreader_dummy (lua_State *L, void *data, size_t *size) {
  (void)L; (void)data;
  *size = 0;
  return NULL;
}

/*
** astparser 语法块：块内使用 AST 解析器解析正常 Lua 代码
** 语法: astparser( Lua代码 )
** 
** 从 ZIO 读取原始源码，跟踪嵌套括号，解析后创建 callable C 闭包
** C 闭包内部持有 Proto* 和 AstChunk* 作为 upvalue，
** 被调用时创建 Lua 闭包并执行。
** 参数：
**   ls - 词法状态
**   line - 起始行号
*/
static int astparserstat (LexState *ls, int line) {
  FuncState *fs = ls->fs;
  lua_State *L = ls->L;
  Proto *f = fs->f;
  ZIO *z = ls->z;
  expdesc v;
  int c;
  (void)line;
  
  luaX_next(ls);  /* 跳过 'astparser' */
  checknext(ls, '(');  /* 消费 '(' —— 此时 ls->t.token == '(' 后第一个 token */

  /* ============================================================
   * 模式 A：astparser("源代码字符串") / astparser([[长字符串]])
   * 直接读取 lexer 给出的字符串字面量的 TString 内容作源码。
   * 这样Lua层不再需要考虑括号深度平衡、长括号包装等hack。
   * ============================================================ */
  if (ls->t.token == TK_STRING || ls->t.token == TK_RAWSTRING ||
      ls->t.token == TK_INTERPSTRING) {
    TString *src_ts = ls->t.seminfo.ts;
    const char *src_ptr = getstr(src_ts);
    size_t src_len = tsslen(src_ts);

    /* 正常消费流程：
     * 1. 消费字符串 token → ls->t.token = ')'
     * 2. checknext 消费 ')' → ls->t.token = ')' 之后第一个源码 token
     * （这与 char mode 读完括号字符流后 checknext(')') 的最终 token 状态完全一致） */
    luaX_next(ls);  /* 消费字符串 token → ls->t.token = ')' */
    checknext(ls, ')');  /* 消费 ')' → ls->t.token = 源码后续 token */

    /* 拷贝一份到可写 buf（因为 luaY_parse_ast 里的 buf 可能被追加 '\0'） */
    size_t buf_len_A = src_len;
    size_t buf_cap_A = buf_len_A + 16;
    char *buf_A = luaM_newvector(L, buf_cap_A, char);
    memcpy(buf_A, src_ptr, buf_len_A);
    buf_A[buf_len_A] = '\0';  /* NUL 终止（与原字符流路径一致） */

    /* 创建 ZIO 用于 AST 解析：firstchar 取 buf[0]，ZIO 从 buf+1 开始读剩余部分
     * （这与原字符流路径完全对称） */
    int firstchar_val = (buf_len_A > 0) ? (unsigned char)buf_A[0] : '\n';
    ZIO ast_z;
    memset(&ast_z, 0, sizeof(ast_z));
    ast_z.L = L;
    ast_z.p = (buf_len_A > 0) ? buf_A + 1 : buf_A;
    ast_z.n = (buf_len_A > 0) ? buf_len_A - 1 : 0;
    ast_z.reader = astparser_zreader_dummy;

    /* AST 解析用的 Mbuffer */
    Mbuffer ast_buff;
    luaZ_initbuffer(L, &ast_buff);

    /* 创建 Dyndata */
    Dyndata ast_dyd;
    memset(&ast_dyd, 0, sizeof(ast_dyd));

    /* 暂停 GC */
    int old_gc = lua_gc(L, LUA_GCISRUNNING, 0);
    lua_gc(L, LUA_GCSTOP, 0);

    /* AST 解析（完全等价于原路径） */
    AstChunk *orig_chunk = luaY_parse_ast(L, &ast_z, &ast_buff, &ast_dyd, "astparser",
                                    firstchar_val);
    if (orig_chunk == NULL) {
      if (old_gc) lua_gc(L, LUA_GCRESTART, 0);
      luaZ_freebuffer(L, &ast_buff);
      luaM_free(L, buf_A);
      luaX_syntaxerror(ls, "AST parse failed (string mode)");
    }

    /* 序列化原始 AST → Lua table（已验证正确，与 parser({ast=true}) 结果一致） */
    ast_serialize_to_lua(L, orig_chunk);

    /* 释放原始 chunk（不再需要，之后完全靠 serialized table） */
    ast_pool_free(orig_chunk->pool);
    luaM_free(L, orig_chunk->pool);
    orig_chunk = NULL;

    /* 恢复 GC */
    if (old_gc) lua_gc(L, LUA_GCRESTART, 0);

    /* 将 serialized AST table 存入 registry，得到整数引用 */
    int ast_ref = luaL_ref(L, LUA_REGISTRYINDEX);

    /* ============================================================
     * 创建 parser CClosure（延迟 codegen 模式）：
     * upvalue[1] = ast_ref 整数（lightuserdata cast）
     * upvalue[2] = &astparser_string_mode_sentinel（sentinel 标记，区别于 NULL）
     * astparser_runner 调用时从 registry 取出 table，复用已验证的 inputmode=ast 路径：
     *   反序列化 → codegen → 再返回一个新的 CClosure（持有真实 Proto）
     * 这保证所有 3 种调用模式（默认/AST模式/延迟模式）的 codegen 逻辑完全一致。
     * ============================================================ */
    lua_pushlightuserdata(L, (void*)(uintptr_t)ast_ref);
    lua_pushlightuserdata(L, (void*)astparser_string_mode_sentinel);  /* sentinel 标记延迟模式 */
    lua_pushcclosure(L, astparser_runner, 2);
    (void)ast_dyd;  /* 未使用（codegen 移到 astparser_runner） */

    /* 将栈顶的 C 闭包添加到常量表，生成指令把它加载到寄存器 */
    {
      int kidx = luaK_closureK(fs, s2v(L->top.p - 1));
      L->top.p--;  /* 弹出 C 闭包（常量表已持有引用） */
      int pc = luaK_codeABx(fs, OP_LOADK, 0, kidx);
      init_exp(&v, VRELOC, pc);
      luaK_exp2nextreg(fs, &v);
    }

    /* 释放源码缓冲区 */
    luaZ_freebuffer(L, &ast_buff);
    luaM_free(L, buf_A);

    return v.u.info;  /* 返回结果寄存器编号 */
  }

  /* ============================================================
   * 模式 B：原有 astparser( 源代码字符流 ) 路径
   *   保持完整向后兼容（括号深度计数扫源代码）
   * ============================================================ */
  /* 从 ZIO 读取原始源码，跟踪嵌套括号，跳过字符串和注释 */
  size_t buf_len = 0, buf_cap = 256;
  char *buf = luaM_newvector(L, buf_cap, char);
  
  int depth = 1;  /* 已经消费了 '('，深度为 1 */
  c = ls->current;  /* 块内第一个字符 */
  
  while (depth > 0) {
    if (c == EOZ) {
      luaM_free(L, buf);
      luaX_syntaxerror(ls, "unfinished astparser block");
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
  int firstchar_val = (buf_len > 0) ? (unsigned char)buf[0] : '\n';
  ZIO ast_z;
  memset(&ast_z, 0, sizeof(ast_z));
  ast_z.L = L;
  ast_z.p = (buf_len > 0) ? buf + 1 : buf;
  ast_z.n = (buf_len > 0) ? buf_len - 1 : 0;
  ast_z.reader = astparser_zreader_dummy;  /* 设置 reader，避免 zgetc 耗尽缓冲后空指针崩溃 */
  
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
    firstchar_val);
  chunk->main_func->is_vararg = 1;
  Proto *p = luaY_codegen_chunk(L, chunk, &ast_dyd);
  
  /* 恢复 GC */
  if (old_gc) lua_gc(L, LUA_GCRESTART, 0);
  
  /* 不释放 AST 内存池 - 保留给后续 {ast=true} 选项 */
  /* 将 Proto 添加到当前函数的子函数列表 */
  if (fs->np >= f->sizep) {
    int oldsize = f->sizep;
    luaM_growvector(L, f->p, fs->np, f->sizep, Proto *, MAXARG_Bx, "functions");
    while (oldsize < f->sizep) f->p[oldsize++] = NULL;
  }
  f->p[fs->np++] = p;
  luaC_objbarrier(L, f, p);
  
  /* 在 Lua 栈上创建 C 闭包：upvalue 1=Proto*, upvalue 2=AstChunk* */
  lua_pushlightuserdata(L, p);
  lua_pushlightuserdata(L, chunk);
  lua_pushcclosure(L, astparser_runner, 2);
  
  /* 将栈顶的 C 闭包添加到常量表，获取常量索引 */
  {
    int kidx = luaK_closureK(fs, s2v(L->top.p - 1));
    L->top.p--;  /* 弹出 C 闭包（常量表已持有引用） */
    
    /* 生成 OP_LOADK 将 C 闭包加载到寄存器 */
    int pc = luaK_codeABx(fs, OP_LOADK, 0, kidx);
    init_exp(&v, VRELOC, pc);
    luaK_exp2nextreg(fs, &v);
  }
  
  /* 释放源码缓冲区 */
  luaZ_freebuffer(L, &ast_buff);
  luaM_free(L, buf);
  
  return v.u.info;  /* 返回结果寄存器编号 */
}


static void asmstat_ex (LexState *ls, int line, AsmContext *parent_ctx) {
  FuncState *fs = ls->fs;
  AsmContext ctx;

  /* 初始化汇编上下文（动态分配，支持嵌套） */
  asm_initcontext(ls->L, &ctx, parent_ctx);

  luaX_next(ls);  /* 跳过 'asm' */
  checknext(ls, '(');
  
  /* 解析指令序列直到遇到 ')' */
  asm_parse_body(ls, fs, &ctx, line);

  /* 修补所有待处理的前向引用 */
  asm_patchpending(ls, fs, &ctx);

  /* 释放汇编上下文 */
  asm_freecontext(ls->L, &ctx);

  checknext(ls, ')');
}


/*
** 递归解析 asm 块主体
** 这个函数包含 asm 主循环的完整逻辑，支持所有伪指令和嵌套
** 参数：
**   ls - 词法状态
**   fs - 函数状态
**   ctx - 当前汇编上下文
**   line - 行号
*/
static void asm_parse_body (LexState *ls, FuncState *fs, AsmContext *ctx, int line) {
  /* 解析指令序列直到遇到 ')' */
  while (ls->t.token != ')') {
    const char *opname;
    int opcode;
    enum OpMode mode;
    Instruction inst;
    int instpc;  /* 当前指令 of PC */
    TString *pendingLabel = NULL;
    int needsPatch = 0;
    int isJumpInst = 0;
    
    /*
    ** 跳过注释内容
    */
    for (;;) {
      if (ls->t.token == ';') {
        luaX_next(ls);  /* 跳过 ';' */
        if (ls->t.token == TK_STRING || ls->t.token == TK_RAWSTRING) {
          luaX_next(ls);
        }
      }
      else if (ls->t.token == TK_STRING || ls->t.token == TK_RAWSTRING) {
        luaX_next(ls);
      }
      else {
        break;
      }
    }
    
    if (ls->t.token == ')') break;
    
    /* 检查是否是标签定义 */
    if (ls->t.token == ':') {
      luaX_next(ls);
      check(ls, TK_NAME);
      TString *labelname = ls->t.seminfo.ts;
      asm_deflabel(ls, ctx, labelname, fs->pc, ls->linenumber);
      luaX_next(ls);
      testnext(ls, ';');
      continue;
    }
    
    check(ls, TK_NAME);
    opname = getstr(ls->t.seminfo.ts);
    
    /* comment 伪指令 */
    if (strcmp(opname, "comment") == 0 || strcmp(opname, "rem") == 0 ||
        strcmp(opname, "COMMENT") == 0 || strcmp(opname, "REM") == 0) {
      luaX_next(ls);
      if (ls->t.token == TK_STRING || ls->t.token == TK_RAWSTRING) {
        luaX_next(ls);
      }
      testnext(ls, ';');
      continue;
    }
    
    /* nop 伪指令 */
    if (strcmp(opname, "nop") == 0) {
      int nop_count = 1;
      luaX_next(ls);
      if (ls->t.token == TK_INT) {
        nop_count = (int)ls->t.seminfo.i;
        luaX_next(ls);
      }
      for (int j = 0; j < nop_count; j++) {
        Instruction nop_inst = CREATE_ABCk(OP_MOVE, 0, 0, 0, 0);
        luaK_code(fs, nop_inst);
        luaK_fixline(fs, line);
      }
      testnext(ls, ';');
      continue;
    }
    
    /* raw 伪指令 */
    if (strcmp(opname, "raw") == 0) {
      luaX_next(ls);
      lua_Integer raw_val = asm_getint_ex(ls, ctx, NULL, NULL, NULL);
      Instruction raw_inst = (Instruction)raw_val;
      luaK_code(fs, raw_inst);
      luaK_fixline(fs, line);
      testnext(ls, ';');
      continue;
    }
    
    /* emit 伪指令 */
    if (strcmp(opname, "emit") == 0) {
      luaX_next(ls);
      do {
        lua_Integer emit_val = asm_getint_ex(ls, ctx, NULL, NULL, NULL);
        Instruction emit_inst = (Instruction)emit_val;
        luaK_code(fs, emit_inst);
        luaK_fixline(fs, line);
      } while (testnext(ls, ','));
      testnext(ls, ';');
      continue;
    }
    
    /* 嵌套 asm 伪指令 */
    if (strcmp(opname, "asm") == 0) {
      int nested_line = ls->linenumber;
      AsmContext nested_ctx;
      luaX_next(ls);
      checknext(ls, '(');
      asm_initcontext(ls->L, &nested_ctx, ctx);
      asm_parse_body(ls, fs, &nested_ctx, nested_line);
      asm_patchpending(ls, fs, &nested_ctx);
      asm_freecontext(ls->L, &nested_ctx);
      checknext(ls, ')');
      testnext(ls, ';');
      continue;
    }
    
    /* jmpx 伪指令 */
    if (strcmp(opname, "jmpx") == 0 || strcmp(opname, "JMPX") == 0) {
      luaX_next(ls);
      if (ls->t.token != TK_OR) {
        luaK_semerror(ls, "jmpx requires @label argument");
      }
      luaX_next(ls);
      check(ls, TK_NAME);
      TString *label = ls->t.seminfo.ts;
      luaX_next(ls);
      asm_emit_jmp(ls, fs, ctx, label, line);
      testnext(ls, ';');
      continue;
    }
    
    /* align 伪指令 */
    if (strcmp(opname, "align") == 0) {
      luaX_next(ls);
      int align_val = (int)asm_getint_ex(ls, ctx, NULL, NULL, NULL);
      if (align_val < 1) {
        luaK_semerror(ls, "align value must be positive");
      }
      while (fs->pc % align_val != 0) {
        Instruction nop_inst = CREATE_ABCk(OP_MOVE, 0, 0, 0, 0);
        luaK_code(fs, nop_inst);
        luaK_fixline(fs, line);
      }
      testnext(ls, ';');
      continue;
    }
    
    /* def 伪指令 */
    if (strcmp(opname, "def") == 0 || strcmp(opname, "define") == 0) {
      luaX_next(ls);
      check(ls, TK_NAME);
      TString *def_name = ls->t.seminfo.ts;
      luaX_next(ls);
      lua_Integer def_value = asm_getint_ex(ls, ctx, NULL, NULL, NULL);
      asm_adddefine(ls, ctx, def_name, def_value);
      testnext(ls, ';');
      continue;
    }
    
    /* newreg 伪指令 */
    if (strcmp(opname, "newreg") == 0) {
      luaX_next(ls);
      check(ls, TK_NAME);
      TString *reg_name = ls->t.seminfo.ts;
      luaX_next(ls);
      int reg = fs->freereg;
      luaK_reserveregs(fs, 1);
      asm_adddefine(ls, ctx, reg_name, reg);
      testnext(ls, ';');
      continue;
    }
    
    /* getglobal 伪指令 */
    if (strcmp(opname, "getglobal") == 0) {
      luaX_next(ls);
      int reg_dest = (int)asm_getint_ex(ls, ctx, NULL, NULL, NULL);
      TString *key_name;
      if (ls->t.token == TK_STRING || ls->t.token == TK_RAWSTRING) {
        key_name = ls->t.seminfo.ts;
        luaX_next(ls);
      } else {
        check(ls, TK_NAME);
        key_name = ls->t.seminfo.ts;
        luaX_next(ls);
      }
      expdesc env_exp;
      singlevaraux(fs, ls->envn, &env_exp, 1);
      if (env_exp.k != VUPVAL) {
        luaK_semerror(ls, "cannot resolve _ENV for getglobal");
      }
      int env_idx = env_exp.u.info;
      int k = luaK_stringK(fs, key_name);
      Instruction inst = CREATE_ABCk(OP_GETTABUP, reg_dest, env_idx, k, 0);
      luaK_code(fs, inst);
      luaK_fixline(fs, line);
      if (reg_dest >= fs->freereg) {
        int needed = reg_dest + 1 - fs->freereg;
        luaK_checkstack(fs, needed);
        fs->freereg = (reg_dest + 1);
      }
      testnext(ls, ';');
      continue;
    }
    
    /* setglobal 伪指令 */
    if (strcmp(opname, "setglobal") == 0) {
      luaX_next(ls);
      int reg_src = (int)asm_getint_ex(ls, ctx, NULL, NULL, NULL);
      TString *key_name;
      if (ls->t.token == TK_STRING || ls->t.token == TK_RAWSTRING) {
        key_name = ls->t.seminfo.ts;
        luaX_next(ls);
      } else {
        check(ls, TK_NAME);
        key_name = ls->t.seminfo.ts;
        luaX_next(ls);
      }
      expdesc env_exp;
      singlevaraux(fs, ls->envn, &env_exp, 1);
      if (env_exp.k != VUPVAL) {
        luaK_semerror(ls, "cannot resolve _ENV for setglobal");
      }
      int env_idx = env_exp.u.info;
      int k = luaK_stringK(fs, key_name);
      Instruction inst = CREATE_ABCk(OP_SETTABUP, env_idx, k, reg_src, 0);
      luaK_code(fs, inst);
      luaK_fixline(fs, line);
      testnext(ls, ';');
      continue;
    }
    
    /* _print / asmprint */
    if (strcmp(opname, "_print") == 0 || strcmp(opname, "asmprint") == 0) {
      luaX_next(ls);
      if (ls->t.token == TK_STRING || ls->t.token == TK_RAWSTRING) {
        const char *msg = getstr(ls->t.seminfo.ts);
        luaX_next(ls);
        if (ls->t.token == TK_INT || ls->t.token == '-' ||
            ls->t.token == TK_DOLLAR || ls->t.token == '%' ||
            ls->t.token == TK_NOT || ls->t.token == TK_OR ||
            ls->t.token == TK_NAME || ls->t.token == '(' || ls->t.token == '~') {
          lua_Integer val = asm_getint_ex(ls, ctx, NULL, NULL, NULL);
          printf("[ASM] %s: %lld\n", msg, (long long)val);
        } else {
          printf("[ASM] %s\n", msg);
        }
      }
      else if (ls->t.token == TK_INT || ls->t.token == '-' ||
               ls->t.token == TK_DOLLAR || ls->t.token == '%' ||
               ls->t.token == TK_NOT || ls->t.token == TK_OR ||
               ls->t.token == TK_NAME || ls->t.token == '(' || ls->t.token == '~') {
        lua_Integer val = asm_getint_ex(ls, ctx, NULL, NULL, NULL);
        printf("[ASM] value: %lld\n", (long long)val);
      }
      else {
        luaK_semerror(ls, "_print expects string or value");
      }
      testnext(ls, ';');
      continue;
    }
    
    /* _assert / asmassert */
    if (strcmp(opname, "_assert") == 0 || strcmp(opname, "asmassert") == 0) {
      luaX_next(ls);
      lua_Integer left_val = asm_getint_ex(ls, ctx, NULL, NULL, NULL);
      int cond_result = 0;
      lua_Integer right_val;
      if (ls->t.token == TK_EQ) {
        luaX_next(ls);
        right_val = asm_getint_ex(ls, ctx, NULL, NULL, NULL);
        cond_result = (left_val == right_val);
      }
      else if (ls->t.token == TK_NE) {
        luaX_next(ls);
        right_val = asm_getint_ex(ls, ctx, NULL, NULL, NULL);
        cond_result = (left_val != right_val);
      }
      else if (ls->t.token == '>') {
        luaX_next(ls);
        if (ls->t.token == '=') {
          luaX_next(ls);
          right_val = asm_getint_ex(ls, ctx, NULL, NULL, NULL);
          cond_result = (left_val >= right_val);
        } else {
          right_val = asm_getint_ex(ls, ctx, NULL, NULL, NULL);
          cond_result = (left_val > right_val);
        }
      }
      else if (ls->t.token == '<') {
        luaX_next(ls);
        if (ls->t.token == '=') {
          luaX_next(ls);
          right_val = asm_getint_ex(ls, ctx, NULL, NULL, NULL);
          cond_result = (left_val <= right_val);
        } else {
          right_val = asm_getint_ex(ls, ctx, NULL, NULL, NULL);
          cond_result = (left_val < right_val);
        }
      }
      else if (ls->t.token == TK_GE) {
        luaX_next(ls);
        right_val = asm_getint_ex(ls, ctx, NULL, NULL, NULL);
        cond_result = (left_val >= right_val);
      }
      else if (ls->t.token == TK_LE) {
        luaX_next(ls);
        right_val = asm_getint_ex(ls, ctx, NULL, NULL, NULL);
        cond_result = (left_val <= right_val);
      }
      else {
        cond_result = (left_val != 0);
      }
      
      if (!cond_result) {
        if (ls->t.token == TK_STRING || ls->t.token == TK_RAWSTRING) {
          const char *msg = getstr(ls->t.seminfo.ts);
          luaX_next(ls);
          luaK_semerror(ls, "asm assertion failed: %s", msg);
        } else {
          luaK_semerror(ls, "asm assertion failed");
        }
      } else {
        if (ls->t.token == TK_STRING || ls->t.token == TK_RAWSTRING) {
          luaX_next(ls);
        }
      }
      testnext(ls, ';');
      continue;
    }
    
    /* _info / asminfo */
    if (strcmp(opname, "_info") == 0 || strcmp(opname, "asminfo") == 0) {
      luaX_next(ls);
      printf("[ASM INFO] line=%d, pc=%d, freereg=%d, nactvar=%d, nk=%d\n",
             ls->linenumber, fs->pc, fs->freereg, fs->nactvar, fs->nk);
      testnext(ls, ';');
      continue;
    }
    
    /* db / dw / dd */
    if (strcmp(opname, "db") == 0) {
      unsigned char bytes[4] = {0, 0, 0, 0};
      int byte_count = 0;
      luaX_next(ls);
      do {
        lua_Integer byte_val = asm_getint_ex(ls, ctx, NULL, NULL, NULL);
        if (byte_count < 4) {
          bytes[byte_count++] = (unsigned char)(byte_val & 0xFF);
        }
        if (byte_count == 4) {
          Instruction db_inst = bytes[0] | (bytes[1] << 8) | (bytes[2] << 16) | (bytes[3] << 24);
          luaK_code(fs, db_inst);
          luaK_fixline(fs, line);
          byte_count = 0;
          memset(bytes, 0, 4);
        }
      } while (testnext(ls, ','));
      if (byte_count > 0) {
        Instruction db_inst = bytes[0] | (bytes[1] << 8) | (bytes[2] << 16) | (bytes[3] << 24);
        luaK_code(fs, db_inst);
        luaK_fixline(fs, line);
      }
      testnext(ls, ';');
      continue;
    }
    if (strcmp(opname, "dw") == 0) {
      unsigned short words[2] = {0, 0};
      int word_count = 0;
      luaX_next(ls);
      do {
        lua_Integer word_val = asm_getint_ex(ls, ctx, NULL, NULL, NULL);
        if (word_count < 2) {
          words[word_count++] = (unsigned short)(word_val & 0xFFFF);
        }
        if (word_count == 2) {
          Instruction dw_inst = words[0] | (words[1] << 16);
          luaK_code(fs, dw_inst);
          luaK_fixline(fs, line);
          word_count = 0;
          memset(words, 0, 4);
        }
      } while (testnext(ls, ','));
      if (word_count > 0) {
        Instruction dw_inst = words[0] | (words[1] << 16);
        luaK_code(fs, dw_inst);
        luaK_fixline(fs, line);
      }
      testnext(ls, ';');
      continue;
    }
    if (strcmp(opname, "dd") == 0) {
      luaX_next(ls);
      do {
        lua_Integer dword_val = asm_getint_ex(ls, ctx, NULL, NULL, NULL);
        Instruction dd_inst = (Instruction)(dword_val & 0xFFFFFFFF);
        luaK_code(fs, dd_inst);
        luaK_fixline(fs, line);
      } while (testnext(ls, ','));
      testnext(ls, ';');
      continue;
    }
    
    /* str "string" */
    if (strcmp(opname, "str") == 0) {
      luaX_next(ls);
      if (ls->t.token == TK_STRING || ls->t.token == TK_RAWSTRING) {
        TString *str_data = ls->t.seminfo.ts;
        int idx = luaK_stringK(fs, str_data);
        (void)idx;
        luaX_next(ls);
      } else {
        luaK_semerror(ls, "str expects a string literal");
      }
      testnext(ls, ';');
      continue;
    }
    
    /* rep count { ... } */
    if (strcmp(opname, "rep") == 0 || strcmp(opname, "repeat") == 0) {
      luaX_next(ls);
      int rep_count = (int)asm_getint_ex(ls, ctx, NULL, NULL, NULL);
      if (rep_count < 0) {
        luaK_semerror(ls, "rep count must be non-negative");
      }
      checknext(ls, '{');
      int rep_start_pc = fs->pc;
      
      asm_parse_body(ls, fs, ctx, line);
      
      int rep_end_pc = fs->pc;
      int instr_count = rep_end_pc - rep_start_pc;
      checknext(ls, '}');
      
      for (int i = 1; i < rep_count; i++) {
        for (int j = 0; j < instr_count; j++) {
          Instruction copied_inst = fs->f->code[rep_start_pc + j];
          luaK_code(fs, copied_inst);
          luaK_fixline(fs, line);
        }
      }
      testnext(ls, ';');
      continue;
    }
    
    /* junk "string" / junk count */
    if (strcmp(opname, "junk") == 0 || strcmp(opname, "garbage") == 0) {
      luaX_next(ls);
      if (ls->t.token == TK_STRING || ls->t.token == TK_RAWSTRING) {
        TString *junk_str = ls->t.seminfo.ts;
        const char *str = getstr(junk_str);
        size_t len = tsslen(junk_str);
        {
          Instruction len_inst = CREATE_Ax(OP_EXTRAARG, (int)(len & MAXARG_Ax));
          luaK_code(fs, len_inst);
          luaK_fixline(fs, line);
        }
        for (size_t i = 0; i < len; i += 3) {
          unsigned int data = 0;
          data |= ((unsigned char)str[i]) << 0;
          if (i + 1 < len) data |= ((unsigned char)str[i + 1]) << 8;
          if (i + 2 < len) data |= ((unsigned char)str[i + 2]) << 16;
          data &= MAXARG_Ax;
          Instruction data_inst = CREATE_Ax(OP_EXTRAARG, (int)data);
          luaK_code(fs, data_inst);
          luaK_fixline(fs, line);
        }
        luaX_next(ls);
      }
      else if (ls->t.token == TK_INT) {
        int junk_count = (int)ls->t.seminfo.i;
        luaX_next(ls);
        if (junk_count < 0) {
          luaK_semerror(ls, "junk count must be non-negative");
        }
        for (int j = 0; j < junk_count; j++) {
          Instruction nop_inst = CREATE_ABCk(OP_NOP, 0, 0, 0, 0);
          luaK_code(fs, nop_inst);
          luaK_fixline(fs, line);
        }
      }
      else {
        luaK_semerror(ls, "junk expects a string or integer count");
      }
      testnext(ls, ';');
      continue;
    }
    
    /* _if / _else / _endif */
    if (strcmp(opname, "_if") == 0 || strcmp(opname, "asmif") == 0) {
      luaX_next(ls);
      lua_Integer left_val = asm_getint_ex(ls, ctx, NULL, NULL, NULL);
      int cond_result = 0;
      lua_Integer right_val;
      if (ls->t.token == TK_EQ) {
        luaX_next(ls);
        right_val = asm_getint_ex(ls, ctx, NULL, NULL, NULL);
        cond_result = (left_val == right_val);
      }
      else if (ls->t.token == TK_NE) {
        luaX_next(ls);
        right_val = asm_getint_ex(ls, ctx, NULL, NULL, NULL);
        cond_result = (left_val != right_val);
      }
      else if (ls->t.token == '>') {
        luaX_next(ls);
        if (ls->t.token == '=') {
          luaX_next(ls);
          right_val = asm_getint_ex(ls, ctx, NULL, NULL, NULL);
          cond_result = (left_val >= right_val);
        } else {
          right_val = asm_getint_ex(ls, ctx, NULL, NULL, NULL);
          cond_result = (left_val > right_val);
        }
      }
      else if (ls->t.token == '<') {
        luaX_next(ls);
        if (ls->t.token == '=') {
          luaX_next(ls);
          right_val = asm_getint_ex(ls, ctx, NULL, NULL, NULL);
          cond_result = (left_val <= right_val);
        } else {
          right_val = asm_getint_ex(ls, ctx, NULL, NULL, NULL);
          cond_result = (left_val < right_val);
        }
      }
      else if (ls->t.token == TK_GE) {
        luaX_next(ls);
        right_val = asm_getint_ex(ls, ctx, NULL, NULL, NULL);
        cond_result = (left_val >= right_val);
      }
      else if (ls->t.token == TK_LE) {
        luaX_next(ls);
        right_val = asm_getint_ex(ls, ctx, NULL, NULL, NULL);
        cond_result = (left_val <= right_val);
      }
      else {
        cond_result = (left_val != 0);
      }
      
      if (!cond_result) {
        int nest_level = 1;
        while (nest_level > 0 && ls->t.token != TK_EOS && ls->t.token != ')') {
          if (ls->t.token == TK_NAME) {
            const char *name = getstr(ls->t.seminfo.ts);
            if (strcmp(name, "_if") == 0 || strcmp(name, "asmif") == 0) {
              nest_level++;
            }
            else if (strcmp(name, "_endif") == 0 || strcmp(name, "asmend") == 0) {
              if (nest_level == 1) {
                luaX_next(ls);
                nest_level = 0;
                break;
              } else { nest_level--; }
            }
            else if (nest_level == 1 && (strcmp(name, "_else") == 0 || strcmp(name, "asmelse") == 0)) {
              luaX_next(ls);
              testnext(ls, ';');
              nest_level = 0;
              break;
            }
          }
          if (nest_level > 0) luaX_next(ls);
        }
      }
      testnext(ls, ';');
      continue;
    }
    
    if (strcmp(opname, "_else") == 0 || strcmp(opname, "asmelse") == 0) {
      int nest_level = 1;
      luaX_next(ls);
      while (nest_level > 0 && ls->t.token != TK_EOS && ls->t.token != ')') {
        if (ls->t.token == TK_NAME) {
          const char *name = getstr(ls->t.seminfo.ts);
          if (strcmp(name, "_if") == 0 || strcmp(name, "asmif") == 0) {
            nest_level++;
          }
          else if (strcmp(name, "_endif") == 0 || strcmp(name, "asmend") == 0) {
            if (nest_level == 1) {
              luaX_next(ls);
              break;
            } else { nest_level--; }
          }
        }
        luaX_next(ls);
      }
      testnext(ls, ';');
      continue;
    }
    
    if (strcmp(opname, "_endif") == 0 || strcmp(opname, "asmend") == 0) {
      luaX_next(ls);
      testnext(ls, ';');
      continue;
    }
    
    /* -------------------------------------------------------------
    ** 优化功能：新增条件跳转伪指令 je, jne, jl, jle, jg, jge, jtrue, jfalse
    ** -------------------------------------------------------------
    */
    int is_cj = 0;
    int cj_type = 0; /* 1: je, 2: jne, 3: jl, 4: jle, 5: jg, 6: jge, 7: jtrue, 8: jfalse */
    if (strcmp(opname, "je") == 0 || strcmp(opname, "JE") == 0) { is_cj = 1; cj_type = 1; }
    else if (strcmp(opname, "jne") == 0 || strcmp(opname, "JNE") == 0) { is_cj = 1; cj_type = 2; }
    else if (strcmp(opname, "jl") == 0 || strcmp(opname, "JL") == 0) { is_cj = 1; cj_type = 3; }
    else if (strcmp(opname, "jle") == 0 || strcmp(opname, "JLE") == 0) { is_cj = 1; cj_type = 4; }
    else if (strcmp(opname, "jg") == 0 || strcmp(opname, "JG") == 0) { is_cj = 1; cj_type = 5; }
    else if (strcmp(opname, "jge") == 0 || strcmp(opname, "JGE") == 0) { is_cj = 1; cj_type = 6; }
    else if (strcmp(opname, "jtrue") == 0 || strcmp(opname, "JTRUE") == 0) { is_cj = 1; cj_type = 7; }
    else if (strcmp(opname, "jfalse") == 0 || strcmp(opname, "JFALSE") == 0) { is_cj = 1; cj_type = 8; }
    
    if (is_cj) {
      luaX_next(ls); /* 跳过 opname */
      
      /* 1. 解析第一个操作数 a */
      int a = (int)asm_getint_ex(ls, ctx, NULL, NULL, NULL);
      
      int b = 0;
      int is_b_const = 0;
      int is_b_imm = 0;
      
      /* jtrue 和 jfalse 只有两个参数: a 和 @label */
      if (cj_type != 7 && cj_type != 8) {
        /* 2. 检测并解析第二个操作数 b 的类型 */
        if (ls->t.token == '#') {
          int next_tok = luaX_lookahead(ls);
          if (next_tok == TK_NAME) {
            is_b_const = 1;
          } else if (next_tok == TK_STRING || next_tok == TK_RAWSTRING) {
            is_b_const = 1;
          } else {
            is_b_imm = 1;
          }
        }
        else if (ls->t.token == TK_INT || ls->t.token == '-') {
          is_b_imm = 1;
        }
        
        b = (int)asm_getint_ex(ls, ctx, NULL, NULL, NULL);
      }
      
      /* 3. 解析目标标签 @label */
      if (ls->t.token != TK_OR) {
        luaK_semerror(ls, "conditional jump requires @label as target");
      }
      luaX_next(ls); /* 跳过 '@' */
      check(ls, TK_NAME);
      TString *label = ls->t.seminfo.ts;
      luaX_next(ls);
      
      /* 4. 根据类型生成比较指令 */
      Instruction comp_inst = 0;
      if (cj_type == 1) { /* je: equal */
        if (is_b_const) {
          comp_inst = CREATE_ABCk(OP_EQK, a, b, 0, 0);
        } else if (is_b_imm) {
          asm_checkrange_signed(ls, b, -OFFSET_sC, OFFSET_sC, "sB");
          comp_inst = CREATE_ABCk(OP_EQI, a, int2sC(b), 0, 0);
        } else {
          comp_inst = CREATE_ABCk(OP_EQ, a, b, 0, 0);
        }
      }
      else if (cj_type == 2) { /* jne: not equal */
        if (is_b_const) {
          comp_inst = CREATE_ABCk(OP_EQK, a, b, 1, 0);
        } else if (is_b_imm) {
          asm_checkrange_signed(ls, b, -OFFSET_sC, OFFSET_sC, "sB");
          comp_inst = CREATE_ABCk(OP_EQI, a, int2sC(b), 1, 0);
        } else {
          comp_inst = CREATE_ABCk(OP_EQ, a, b, 1, 0);
        }
      }
      else if (cj_type == 3) { /* jl: less than */
        if (is_b_const) {
          luaK_semerror(ls, "less-than comparison does not support constant pool operands");
        } else if (is_b_imm) {
          asm_checkrange_signed(ls, b, -OFFSET_sC, OFFSET_sC, "sB");
          comp_inst = CREATE_ABCk(OP_LTI, a, int2sC(b), 0, 0);
        } else {
          comp_inst = CREATE_ABCk(OP_LT, a, b, 0, 0);
        }
      }
      else if (cj_type == 4) { /* jle: less than or equal */
        if (is_b_const) {
          luaK_semerror(ls, "less-equal comparison does not support constant pool operands");
        } else if (is_b_imm) {
          asm_checkrange_signed(ls, b, -OFFSET_sC, OFFSET_sC, "sB");
          comp_inst = CREATE_ABCk(OP_LEI, a, int2sC(b), 0, 0);
        } else {
          comp_inst = CREATE_ABCk(OP_LE, a, b, 0, 0);
        }
      }
      else if (cj_type == 5) { /* jg: greater than */
        if (is_b_const) {
          luaK_semerror(ls, "greater-than comparison does not support constant pool operands");
        } else if (is_b_imm) {
          asm_checkrange_signed(ls, b, -OFFSET_sC, OFFSET_sC, "sB");
          comp_inst = CREATE_ABCk(OP_GTI, a, int2sC(b), 0, 0);
        } else {
          /* a > b is equivalent to b < a */
          comp_inst = CREATE_ABCk(OP_LT, b, a, 0, 0);
        }
      }
      else if (cj_type == 6) { /* jge: greater than or equal */
        if (is_b_const) {
          luaK_semerror(ls, "greater-equal comparison does not support constant pool operands");
        } else if (is_b_imm) {
          asm_checkrange_signed(ls, b, -OFFSET_sC, OFFSET_sC, "sB");
          comp_inst = CREATE_ABCk(OP_GEI, a, int2sC(b), 0, 0);
        } else {
          /* a >= b is equivalent to b <= a */
          comp_inst = CREATE_ABCk(OP_LE, b, a, 0, 0);
        }
      }
      else if (cj_type == 7) { /* jtrue: test true */
        comp_inst = CREATE_ABCk(OP_TEST, a, 0, 0, 0);
      }
      else if (cj_type == 8) { /* jfalse: test false */
        comp_inst = CREATE_ABCk(OP_TEST, a, 0, 0, 1);
      }
      
      luaK_code(fs, comp_inst);
      luaK_fixline(fs, line);
      
      /* Emit target jump instruction */
      asm_emit_jmp(ls, fs, ctx, label, line);
      
      testnext(ls, ';');
      continue;
    }
    
    /* -------------------------------------------------------------
    ** 正常汇编指令解析
    ** -------------------------------------------------------------
    */
    opcode = find_opcode(opname);
    if (opcode < 0) {
      luaK_semerror(ls, "unknown opcode '%s' in asm", opname);
    }
    
    luaX_next(ls);  /* 跳过操作码名称 */
    mode = getOpMode(opcode);
    instpc = fs->pc;  /* 记录当前 PC */
    
    switch (mode) {
      case iABC: {
        int a = (int)asm_getint_ex(ls, ctx, NULL, NULL, NULL);
        int b = (int)asm_trygetint_ex(ls, ctx, 0, NULL, &pendingLabel, NULL);
        if (pendingLabel) needsPatch = 1;
        int c = (int)asm_trygetint_ex(ls, ctx, 0, NULL, pendingLabel ? NULL : &pendingLabel, NULL);
        if (pendingLabel && !needsPatch) needsPatch = 1;
        int k = (int)asm_trygetint(ls, 0);
        
        asm_checkrange(ls, a, MAXARG_A, "A");
        asm_checkrange(ls, b, MAXARG_B, "B");
        asm_checkrange(ls, c, MAXARG_C, "C");
        asm_checkrange(ls, k, 1, "k");
        
        if (opcode == OP_GTI || opcode == OP_GEI || 
            opcode == OP_LTI || opcode == OP_LEI || 
            opcode == OP_EQI || opcode == OP_MMBINI) {
          asm_checkrange_signed(ls, b, -OFFSET_sC, OFFSET_sC, "sB");
          b = int2sC(b);
          inst = CREATE_ABCk(opcode, a, b, c, k);
        }
        else if (opcode == OP_ADDI || opcode == OP_SHLI || opcode == OP_SHRI) {
          asm_checkrange_signed(ls, c, -OFFSET_sC, OFFSET_sC, "sC");
          c = int2sC(c);
          inst = CREATE_ABCk(opcode, a, b, c, k);
        }
        else {
          inst = CREATE_ABCk(opcode, a, b, c, k);
        }
        break;
      }
      case ivABC: {
        int a = (int)asm_getint_ex(ls, ctx, NULL, NULL, NULL);
        int vb = (int)asm_trygetint_ex(ls, ctx, 0, NULL, &pendingLabel, NULL);
        if (pendingLabel) needsPatch = 1;
        int vc = (int)asm_trygetint_ex(ls, ctx, 0, NULL, pendingLabel ? NULL : &pendingLabel, NULL);
        if (pendingLabel && !needsPatch) needsPatch = 1;
        int k = (int)asm_trygetint(ls, 0);
        
        asm_checkrange(ls, a, MAXARG_A, "A");
        asm_checkrange(ls, vb, MAXARG_vB, "vB");
        asm_checkrange(ls, vc, MAXARG_vC, "vC");
        asm_checkrange(ls, k, 1, "k");
        
        inst = CREATE_vABCk(opcode, a, vb, vc, k);
        break;
      }
      case iABx: {
        int a = (int)asm_getint_ex(ls, ctx, NULL, NULL, NULL);
        int isLabelRef = 0;
        unsigned int bx = (unsigned int)asm_getint_ex(ls, ctx, NULL, &pendingLabel, &isLabelRef);
        if (pendingLabel) {
          needsPatch = 1;
          if (opcode == OP_FORLOOP || opcode == OP_TFORLOOP ||
              opcode == OP_FORPREP || opcode == OP_TFORPREP) {
            isJumpInst = 1;
          }
        } else if (isLabelRef) {
          int offset;
          int target = (int)bx;
          if (opcode == OP_FORLOOP || opcode == OP_TFORLOOP) {
             offset = (instpc + 1) - target;
             if (offset <= 0) luaK_semerror(ls, "jump target for loop instruction must be backward");
             bx = (unsigned int)offset;
          } else if (opcode == OP_FORPREP || opcode == OP_TFORPREP) {
             offset = target - (instpc + 1);
             if (offset < 0) luaK_semerror(ls, "jump target for prep instruction must be forward");
             if (opcode == OP_FORPREP) offset--;
             bx = (unsigned int)offset;
          }
        }
        
        asm_checkrange(ls, a, MAXARG_A, "A");
        asm_checkrange(ls, bx, MAXARG_Bx, "Bx");
        inst = CREATE_ABx(opcode, a, bx);
        break;
      }
      case iAsBx: {
        int a = (int)asm_getint_ex(ls, ctx, NULL, NULL, NULL);
        int sbx = (int)asm_getint_ex(ls, ctx, NULL, &pendingLabel, NULL);
        if (pendingLabel) {
          needsPatch = 1;
          isJumpInst = 1;
        }
        
        asm_checkrange(ls, a, MAXARG_A, "A");
        asm_checkrange_signed(ls, sbx, -OFFSET_sBx, OFFSET_sBx, "sBx");
        inst = CREATE_ABx(opcode, a, cast_uint(sbx + OFFSET_sBx));
        break;
      }
      case iAx: {
        int ax = (int)asm_getint_ex(ls, ctx, NULL, &pendingLabel, NULL);
        if (pendingLabel) needsPatch = 1;
        
        asm_checkrange(ls, ax, MAXARG_Ax, "Ax");
        inst = CREATE_Ax(opcode, ax);
        break;
      }
      case isJ: {
        int isLabelRef = 0;
        int sj = (int)asm_getint_ex(ls, ctx, NULL, &pendingLabel, &isLabelRef);
        if (pendingLabel) {
          needsPatch = 1;
          isJumpInst = 1;
        } else if (isLabelRef) {
          sj = sj - (instpc + 1);
        }
        
        asm_checkrange_signed(ls, sj, -OFFSET_sJ, OFFSET_sJ, "sJ");
        inst = CREATE_sJ(opcode, sj + OFFSET_sJ, 0);
        break;
      }
      default: {
        luaK_semerror(ls, "unsupported opcode mode in asm");
        inst = 0;
      }
    }
    
    luaK_code(fs, inst);
    luaK_fixline(fs, line);
    
    if (needsPatch && pendingLabel) {
      asm_addpending(ls, ctx, pendingLabel, instpc, ls->linenumber, isJumpInst);
    }
    
    /* 自动生成 MMBIN 系列指令 */
    if (opcode >= OP_ADD && opcode <= OP_SHR) {
      int b = GETARG_B(inst);
      int c = GETARG_C(inst);
      TMS tm = cast(TMS, (opcode - OP_ADD) + TM_ADD);
      luaK_codeABCk(fs, OP_MMBIN, b, c, cast_int(tm), 0);
      luaK_fixline(fs, line);
    }
    else if (opcode == OP_ADDI) {
      int b = GETARG_B(inst);
      int sc = GETARG_C(inst);
      luaK_codeABCk(fs, OP_MMBINI, b, sc, TM_ADD, 0);
      luaK_fixline(fs, line);
    }
    else if (opcode == OP_SHLI) {
      int b = GETARG_B(inst);
      int sc = GETARG_C(inst);
      luaK_codeABCk(fs, OP_MMBINI, b, sc, TM_SHL, 0);
      luaK_fixline(fs, line);
    }
    else if (opcode == OP_SHRI) {
      int b = GETARG_B(inst);
      int sc = GETARG_C(inst);
      luaK_codeABCk(fs, OP_MMBINI, b, sc, TM_SHR, 0);
      luaK_fixline(fs, line);
    }
    else if (opcode >= OP_ADDK && opcode <= OP_IDIVK) {
      int b = GETARG_B(inst);
      int c = GETARG_C(inst);
      TMS tm = cast(TMS, (opcode - OP_ADDK) + TM_ADD);
      luaK_codeABCk(fs, OP_MMBINK, b, c, cast_int(tm), 0);
      luaK_fixline(fs, line);
    }
    else if (opcode >= OP_BANDK && opcode <= OP_BXORK) {
      int b = GETARG_B(inst);
      int c = GETARG_C(inst);
      TMS tm = cast(TMS, (opcode - OP_BANDK) + TM_BAND);
      luaK_codeABCk(fs, OP_MMBINK, b, c, cast_int(tm), 0);
      luaK_fixline(fs, line);
    }
    
    if (testAMode(opcode)) {
      int a = GETARG_A(inst);
      if (a >= fs->freereg) {
        int needed = a + 1 - fs->freereg;
        luaK_checkstack(fs, needed);
        fs->freereg = (a + 1);
      }
    }
    
    testnext(ls, ';');
  }
}/*
** 命令声明语法处理
** 语法: command 命令名(参数列表) 代码块 end
** 等价于: function 命令名(参数列表) 代码块 end; _CMDS["命令名"] = true
** 
** 参数：
**   ls - 词法状态
**   line - 行号
*/
static void commandstat (LexState *ls, int line) {
  /* commandstat -> COMMAND funcname body */
  expdesc v, b;
  TString *cmdname;
  
  luaX_next(ls);  /* skip COMMAND */
  
  /* 先保存命令名（不消费 token） */
  check(ls, TK_NAME);
  cmdname = ls->t.seminfo.ts;
  
  /* 使用 singlevar 获取变量描述符（这会消费 NAME token） */
  singlevar(ls, &v);
  
  /* 检查是否为只读 */
  check_readonly(ls, &v);
  
  /* 解析函数体 */
  body(ls, &b, 0, line);
  
  /* 存储函数到变量 */
  apply_decorators_inline(ls, &v, &b);
  luaK_storevar(ls->fs, &v, &b);
  luaK_fixline(ls->fs, line);
  
  /* 将命令名注册到 _CMDS 表: _CMDS[命令名] = true */
  {
    FuncState *fs = ls->fs;
    expdesc cmds_table, key_exp, val_exp;
    
    /* 获取 _CMDS 表 (via opcode) */
    init_exp(&cmds_table, VNONRELOC, fs->freereg);
    luaK_codeABC(fs, OP_GETCMDS, fs->freereg, 0, 0);
    luaK_reserveregs(fs, 1);
    
    /* 设置 _CMDS[命令名] = true */
    luaK_exp2anyregup(fs, &cmds_table);
    codestring(&key_exp, cmdname);
    init_exp(&val_exp, VTRUE, 0);
    luaK_indexed(fs, &cmds_table, &key_exp);
    luaK_storevar(fs, &cmds_table, &val_exp);
  }
}


/*
** 关键字声明语法处理
** 语法: keyword 关键字名(参数列表) 代码块 end
** 
** 编译时处理：
**   1. 像普通函数一样编译函数体
**   2. 将编译后的 Proto 注册到全局 keyword 注册表
**   3. $name(args) 调用时直接从注册表引用 Proto，无需运行时表查询
**
** 参数：
**   ls - 词法状态
**   line - 行号
*/
static void keywordstat (LexState *ls, int line) {
  /* keywordstat -> KEYWORD funcname body */
  expdesc v, b;
  TString *kwname;
  Proto *kwproto;
  
  luaX_next(ls);  /* skip KEYWORD */
  
  /* 先保存关键字名（不消费 token）允许类型标记作为keyword名 */
  if (!is_nametoken(ls->t.token))
    error_expected(ls, TK_NAME);
  kwname = ls->t.seminfo.ts;
  
  /* 使用 singlevar 获取变量描述符（这会消费 NAME token） */
  singlevar(ls, &v);
  
  /* 检查是否为只读 */
  check_readonly(ls, &v);
  
  /* 解析函数体，此时 b 是 VRELOC，b.u.info 指向 OP_CLOSURE 指令 */
  body(ls, &b, 0, line);
  
  /* 在 apply_decorators_inline / luaK_storevar 之前提取 Proto */
  /* body() 已调用 codeclosure，其中 luaK_exp2nextreg 将 b 从 VRELOC 改为 VNONRELOC */
  /* b.u.info 现在是寄存器号，需要从指令中反向查找 OP_CLOSURE */
  {
    int reg = b.u.info;
    Instruction *code = ls->fs->f->code;
    int i;
    kwproto = NULL;
    for (i = ls->fs->pc - 1; i >= 0; i--) {
      if (GET_OPCODE(code[i]) == OP_CLOSURE && GETARG_A(code[i]) == reg) {
        kwproto = ls->fs->f->p[GETARG_Bx(code[i])];
        break;
      }
    }
  }
  lua_assert(kwproto != NULL);
  /* keyword 必须是纯函数，不能捕获 upvalue */
  /* 否则从其他编译单元创建 closure 时 upvalue 会无效 */
  if (kwproto->sizeupvalues > 0) {
    luaX_syntaxerror(ls,
      "keyword cannot capture upvalues (use parameters instead of outer variables)");
  }
  /* 注册到 keyword 编译时注册表 */
  keyword_register(ls, kwname, kwproto);
  
  /* 之后应用 decorator 和 store（保证可以像普通函数一样调用） */
  apply_decorators_inline(ls, &v, &b);
  luaK_storevar(ls->fs, &v, &b);
  luaK_fixline(ls->fs, line);
  /* 不再需要 _KEYWORDS 运行时表，keyword 现在是真正的编译时特性 */
}


/*
** operatorstat - 解析 operator 语句
** 语法: operator <符号> (参数列表) 语句块 end
** 功能描述：
**   定义自定义运算符，将函数存储到 _OPERATORS 表中
** 参数：
**   ls - 词法状态
**   line - 行号
** 示例：
**   operator ++ (a) return a + 1 end
**   operator ** (a, b) return a ^ b end
*/
static void operatorstat (LexState *ls, int line) {
  /* operatorstat -> OPERATOR <符号> body */
  /* printf("Parsing operatorstat\n"); */
  expdesc b;
  TString *opname = NULL;
  FuncState *fs = ls->fs;
  const char *opstr = NULL;
  
  luaX_next(ls);  /* 跳过 OPERATOR */
  
  /* 解析运算符符号 - 支持各种符号组合 */
  int tok = ls->t.token;
  
  /* 根据当前token类型获取运算符字符串 */
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
      /* 支持命名运算符如 __add, __sub 等 */
      opname = ls->t.seminfo.ts;
      break;
    case TK_STRING:
      /* 支持字符串形式的运算符如 "**" */
      opname = ls->t.seminfo.ts;
      break;
    default:
      luaX_syntaxerror(ls, "expected operator symbol after 'operator'");
  }
  
  /* 如果是固定字符串，创建 TString */
  if (opstr != NULL) {
    opname = luaS_new(ls->L, opstr);
  }
  
  luaX_next(ls);  /* 消费运算符符号 */
  
  /* 解析函数体 (参数列表和函数体) */
  body(ls, &b, 0, line);
  
  /* 将函数注册到 _OPERATORS 表: _OPERATORS[运算符] = 函数 */
  {
    expdesc operators_table, key_exp;
    
    /* 获取 _OPERATORS 表 (via opcode) */
    init_exp(&operators_table, VNONRELOC, fs->freereg);
    luaK_codeABC(fs, OP_GETOPS, fs->freereg, 0, 0);
    luaK_reserveregs(fs, 1);
    
    /* 确保函数在寄存器中 */
    luaK_exp2anyreg(fs, &b);
    
    /* 设置 _OPERATORS[运算符] = 函数 */
    luaK_exp2anyregup(fs, &operators_table);
    codestring(&key_exp, opname);
    luaK_indexed(fs, &operators_table, &key_exp);
    luaK_storevar(fs, &operators_table, &b);
  }
  
  luaK_fixline(fs, line);
}


static void conceptstat (LexState *ls, int line) {
  /* conceptstat -> CONCEPT funcname body */
  expdesc v, b;
  int ismethod;

  luaX_next(ls);  /* skip CONCEPT */
  ismethod = funcname(ls, &v);

  if (ismethod) {
      luaX_syntaxerror(ls, "concepts cannot be methods");
  }

  check_readonly(ls, &v);

  FuncState new_fs;
  BlockCnt bl;
  new_fs.f = addprototype(ls);
  new_fs.f->linedefined = line;
  open_func(ls, &new_fs, &bl);

  if (ls->t.token == '(') {
      checknext(ls, '(');
      parlist(ls, NULL);
      checknext(ls, ')');
  }

  if (testnext(ls, '=')) {
      /* Expression body: return expr */
      expdesc e;
      expr(ls, &e);
      luaK_ret(&new_fs, luaK_exp2anyreg(&new_fs, &e), 1);

      new_fs.f->lastlinedefined = ls->linenumber;
      codeconcept(ls, &b);
      close_func(ls);
  } else {
      statlist(ls);
      check_match(ls, TK_END, TK_CONCEPT, line);

      new_fs.f->lastlinedefined = ls->linenumber;
      codeconcept(ls, &b);
      close_func(ls);
  }

  apply_decorators_inline(ls, &v, &b);
  luaK_storevar(ls->fs, &v, &b);
  luaK_fixline(ls->fs, line);
}


static void funcstat (LexState *ls, int line, int isasync) {
  /* funcstat -> FUNCTION funcname body */
  int ismethod;
  expdesc v, b;
  luaX_next(ls);  /* skip FUNCTION */
  ismethod = funcname(ls, &v);
  check_readonly(ls, &v);
  body(ls, &b, ismethod, line);

  if (isasync) {
      FuncState *fs = ls->fs;
      /*
       * 纯语法级 async 标记：直接在函数 Proto 上设置 PF_ASYNC 标志。
       * 不再创建 CClosure 包装器，不需要额外寄存器。
       */
      luaK_exp2nextreg(fs, &b);
      luaK_codeABC(fs, OP_ASYNCWRAP, 0, b.u.info, 0);
  }

  apply_decorators_inline(ls, &v, &b);
  luaK_storevar(ls->fs, &v, &b);
  luaK_fixline(ls->fs, line);  /* definition "happens" in the first line */
}


/*
** =====================================================================
** 面向对象系统：类定义解析
** 语法格式:
**   class ClassName [extends ParentClass] [implements Interface1, Interface2]
**     [static] function methodName(self, args) ... end
**     propertyName = value
**   end
** =====================================================================
*/

/*
** 访问级别枚举
*/
#define ACCESS_PUBLIC    0
#define ACCESS_PROTECTED 1
#define ACCESS_PRIVATE   2

/*
** 解析类方法定义
** 参数：
**   ls - 词法状态
**   class_reg - 类表所在的寄存器
**   is_static - 是否是静态方法
**   access_level - 访问级别（ACCESS_PUBLIC/PROTECTED/PRIVATE）
**   is_override - 是否是 override 方法（需校验父类存在同名方法）
** 说明：
**   解析 function methodName(self, ...) ... end 形式的方法定义
*/
static void class_method(LexState *ls, int class_reg, int is_static, int access_level, int is_override) {
  FuncState *fs = ls->fs;
  int line = ls->linenumber;
  expdesc method_exp, key_exp;
  
  checknext(ls, TK_FUNCTION);
  
  /* 获取方法名 */
  TString *method_name = str_checkname(ls);
  
  /* 生成方法体 */
  /* 非静态方法自动添加 self 参数 */
  body(ls, &method_exp, !is_static, line);
  apply_decorators_inline(ls, NULL, &method_exp);
  
  /* 将方法存储到类表中 */
  int methods_reg = fs->freereg;
  luaK_reserveregs(fs, 1);
  
  /* 根据静态性和访问级别选择存储表 */
  TString *table_name_ts;
  if (is_static) {
    table_name_ts = luaS_newliteral(ls->L, "__statics");  /* 静态方法存储到 __statics */
  } else if (access_level == ACCESS_PRIVATE) {
    table_name_ts = luaS_newliteral(ls->L, "__privates");
  } else if (access_level == ACCESS_PROTECTED) {
    table_name_ts = luaS_newliteral(ls->L, "__protected");
  } else {
    table_name_ts = luaS_newliteral(ls->L, "__methods");  /* 公开方法 */
  }
  
  /* 生成: R[methods_reg] = R[class_reg][table_name] */
  init_exp(&key_exp, VK, luaK_stringK(fs, table_name_ts));
  expdesc class_exp;
  init_exp(&class_exp, VNONRELOC, class_reg);
  luaK_indexed(fs, &class_exp, &key_exp);
  luaK_exp2nextreg(fs, &class_exp);
  
  /* 设置方法: methods[method_name] = method_func */
  codestring(&key_exp, method_name);
  luaK_exp2anyreg(fs, &method_exp);
  
  /* 使用SETFIELD指令设置方法 */
  int key_k = luaK_stringK(fs, method_name);
  
  /* 如果是 override 方法，生成 OP_CHECKOVERRIDE 校验父类存在同名方法 */
  if (is_override) {
    luaK_codeABC(fs, OP_CHECKOVERRIDE, class_reg, key_k, 0);
  }
  
  /* 根据静态性使用不同的操作码 */
  if (is_static) {
    luaK_codeABC(fs, OP_SETSTATIC, class_reg, key_k, method_exp.u.info);
  } else {
    luaK_codeABC(fs, OP_SETMETHOD, class_reg, key_k, method_exp.u.info);
  }
  
  fs->freereg = class_reg + 1;  /* 释放临时寄存器 */
}


/*
** 解析类属性定义
** 参数：
**   ls - 词法状态
**   class_reg - 类表所在的寄存器
**   is_static - 是否是静态属性
**   access_level - 访问级别（ACCESS_PUBLIC/PROTECTED/PRIVATE）
** 说明：
**   解析 propertyName = value 形式的属性定义
*/
static void class_property(LexState *ls, int class_reg, int is_static, int access_level) {
  FuncState *fs = ls->fs;
  expdesc key_exp, val_exp;
  
  /* 获取属性名 */
  TString *prop_name = str_checkname(ls);
  
  /* 解析赋值 */
  checknext(ls, '=');
  expr(ls, &val_exp);
  luaK_exp2anyreg(fs, &val_exp);
  
  /* 设置属性到对应表 */
  int statics_reg = fs->freereg;
  luaK_reserveregs(fs, 1);
  
  /* 根据访问级别和静态性选择存储表 */
  TString *table_name_ts;
  if (is_static) {
    table_name_ts = luaS_newliteral(ls->L, "__statics");  /* 静态属性存储到 __statics */
  } else if (access_level == ACCESS_PRIVATE) {
    table_name_ts = luaS_newliteral(ls->L, "__privates");
  } else if (access_level == ACCESS_PROTECTED) {
    table_name_ts = luaS_newliteral(ls->L, "__protected");
  } else {
    table_name_ts = luaS_newliteral(ls->L, "__statics");  /* 公开属性也存储到 __statics（类级别属性） */
  }
  
  init_exp(&key_exp, VK, luaK_stringK(fs, table_name_ts));
  expdesc class_exp;
  init_exp(&class_exp, VNONRELOC, class_reg);
  luaK_indexed(fs, &class_exp, &key_exp);
  luaK_exp2nextreg(fs, &class_exp);
  
  int key_k = luaK_stringK(fs, prop_name);
  luaK_codeABC(fs, OP_SETFIELD, class_exp.u.info, key_k, val_exp.u.info);
  
  fs->freereg = class_reg + 1;
}


/*
** 解析getter属性访问器
** 参数：
**   ls - 词法状态
**   class_reg - 类表所在的寄存器
**   access_level - 访问级别（ACCESS_PUBLIC/PROTECTED/PRIVATE）
** 语法：
**   [private|protected|public] get propertyName(self) ... end
** 说明：
**   当访问指定属性时，会调用getter函数
**   支持访问控制修饰符
*/
static void class_getter(LexState *ls, int class_reg, int access_level) {
  FuncState *fs = ls->fs;
  expdesc key_exp, method_exp;
  int line = ls->linenumber;
  
  /* 跳过 'get' 已在调用前处理 */
  
  /* 获取属性名 */
  TString *prop_name = str_checkname(ls);
  
  /* 生成getter函数体 */
  body(ls, &method_exp, 1, line);
  apply_decorators_inline(ls, NULL, &method_exp);
  
  /* 根据访问级别选择存储表 */
  const char *table_name;
  if (access_level == ACCESS_PRIVATE) {
    table_name = "__private_getters";
  } else if (access_level == ACCESS_PROTECTED) {
    table_name = "__protected_getters";
  } else {
    table_name = "__getters";  /* 公开 */
  }
  
  /* 将getter存储到对应的表中 */
  int getters_reg = fs->freereg;
  luaK_reserveregs(fs, 1);
  
  TString *getters_ts = luaS_new(ls->L, table_name);
  init_exp(&key_exp, VK, luaK_stringK(fs, getters_ts));
  expdesc class_exp;
  init_exp(&class_exp, VNONRELOC, class_reg);
  luaK_indexed(fs, &class_exp, &key_exp);
  luaK_exp2nextreg(fs, &class_exp);
  
  /* 设置: getters_table[prop_name] = getter_func */
  luaK_exp2anyreg(fs, &method_exp);
  int key_k = luaK_stringK(fs, prop_name);
  luaK_codeABC(fs, OP_SETFIELD, class_exp.u.info, key_k, method_exp.u.info);
  
  fs->freereg = class_reg + 1;
}


/*
** 解析setter属性访问器
** 参数：
**   ls - 词法状态
**   class_reg - 类表所在的寄存器
**   access_level - 访问级别（ACCESS_PUBLIC/PROTECTED/PRIVATE）
** 语法：
**   [private|protected|public] set propertyName(self, value) ... end
** 说明：
**   当设置指定属性时，会调用setter函数
**   支持访问控制修饰符
*/
static void class_setter(LexState *ls, int class_reg, int access_level) {
  FuncState *fs = ls->fs;
  expdesc key_exp, method_exp;
  int line = ls->linenumber;
  
  /* 跳过 'set' 已在调用前处理 */
  
  /* 获取属性名 */
  TString *prop_name = str_checkname(ls);
  
  /* 生成setter函数体 */
  body(ls, &method_exp, 1, line);
  apply_decorators_inline(ls, NULL, &method_exp);
  
  /* 根据访问级别选择存储表 */
  const char *table_name;
  if (access_level == ACCESS_PRIVATE) {
    table_name = "__private_setters";
  } else if (access_level == ACCESS_PROTECTED) {
    table_name = "__protected_setters";
  } else {
    table_name = "__setters";  /* 公开 */
  }
  
  /* 将setter存储到对应的表中 */
  int setters_reg = fs->freereg;
  luaK_reserveregs(fs, 1);
  
  TString *setters_ts = luaS_new(ls->L, table_name);
  init_exp(&key_exp, VK, luaK_stringK(fs, setters_ts));
  expdesc class_exp;
  init_exp(&class_exp, VNONRELOC, class_reg);
  luaK_indexed(fs, &class_exp, &key_exp);
  luaK_exp2nextreg(fs, &class_exp);
  
  /* 设置: setters_table[prop_name] = setter_func */
  luaK_exp2anyreg(fs, &method_exp);
  int key_k = luaK_stringK(fs, prop_name);
  luaK_codeABC(fs, OP_SETFIELD, class_exp.u.info, key_k, method_exp.u.info);
  
  fs->freereg = class_reg + 1;
}


/*
** 解析抽象方法声明
** 参数：
**   ls - 词法状态
**   class_reg - 类表所在的寄存器
**   is_static - 是否是静态方法
**   access_level - 访问级别（ACCESS_PUBLIC/PROTECTED/PRIVATE）
** 说明：
**   解析 abstract function methodName(params) 形式的抽象方法声明
**   抽象方法只有签名，没有方法体，子类必须实现
*/
static void class_abstract_method(LexState *ls, int class_reg, int is_static, int access_level) {
  FuncState *fs = ls->fs;
  expdesc key_exp;
  
  checknext(ls, TK_FUNCTION);
  
  /* 获取方法名 */
  TString *method_name = str_checkname(ls);
  
  /* 解析参数列表并计算参数个数 */
  checknext(ls, '(');
  int param_count = 0;
  while (ls->t.token != ')' && ls->t.token != TK_EOS) {
    if (ls->t.token == TK_NAME) {
      param_count++;
    }
    luaX_next(ls);
  }
  checknext(ls, ')');
  
  /* 将抽象方法名添加到 __abstracts 表，值为参数个数 */
  int abstracts_reg = fs->freereg;
  luaK_reserveregs(fs, 1);
  
  TString *abstracts_ts = luaS_newliteral(ls->L, "__abstracts");
  init_exp(&key_exp, VK, luaK_stringK(fs, abstracts_ts));
  expdesc class_exp;
  init_exp(&class_exp, VNONRELOC, class_reg);
  luaK_indexed(fs, &class_exp, &key_exp);
  luaK_exp2nextreg(fs, &class_exp);
  
  /* 设置 abstracts[method_name] = param_count */
  int method_k = luaK_stringK(fs, method_name);
  luaK_int(fs, fs->freereg, param_count);
  luaK_reserveregs(fs, 1);
  luaK_codeABC(fs, OP_SETFIELD, class_exp.u.info, method_k, fs->freereg - 1);
  
  /* 同时标记类为抽象类 */
  /* 设置 __flags |= CLASS_FLAG_ABSTRACT */
  TString *flags_ts = luaS_newliteral(ls->L, "__flags");
  int flags_k = luaK_stringK(fs, flags_ts);
  expdesc class_exp2;
  init_exp(&class_exp2, VNONRELOC, class_reg);
  
  /* 获取当前 flags */
  int flags_reg = fs->freereg;
  luaK_reserveregs(fs, 1);
  luaK_codeABC(fs, OP_GETFIELD, flags_reg, class_reg, flags_k);
  
  /* flags |= CLASS_FLAG_ABSTRACT (0x02) */
  luaK_int(fs, fs->freereg, CLASS_FLAG_ABSTRACT);
  luaK_reserveregs(fs, 1);
  luaK_codeABC(fs, OP_BOR, flags_reg, flags_reg, fs->freereg - 1);
  luaK_codeABC(fs, OP_MMBIN, flags_reg, flags_reg, TM_BOR);
  
  /* 写回 flags */
  luaK_codeABC(fs, OP_SETFIELD, class_reg, flags_k, flags_reg);
  
  fs->freereg = class_reg + 1;  /* 释放临时寄存器 */
}


/*
** 解析 final 方法定义
** 参数：
**   ls - 词法状态
**   class_reg - 类表所在的寄存器
**   is_static - 是否是静态方法
**   access_level - 访问级别（ACCESS_PUBLIC/PROTECTED/PRIVATE）
** 说明：
**   解析 final function methodName(params) ... end 形式的 final 方法
**   final 方法不可被子类重写
*/
static void class_final_method(LexState *ls, int class_reg, int is_static, int access_level) {
  FuncState *fs = ls->fs;
  int line = ls->linenumber;
  expdesc method_exp, key_exp;
  
  checknext(ls, TK_FUNCTION);
  
  /* 获取方法名 */
  TString *method_name = str_checkname(ls);
  
  /* 生成方法体 */
  body(ls, &method_exp, 0, line);
  
  /* 将方法存储到对应表中 */
  int methods_reg = fs->freereg;
  luaK_reserveregs(fs, 1);
  
  TString *table_name_ts;
  if (is_static) {
    table_name_ts = luaS_newliteral(ls->L, "__statics");  /* 静态final方法存储到 __statics */
  } else if (access_level == ACCESS_PRIVATE) {
    table_name_ts = luaS_newliteral(ls->L, "__privates");
  } else if (access_level == ACCESS_PROTECTED) {
    table_name_ts = luaS_newliteral(ls->L, "__protected");
  } else {
    table_name_ts = luaS_newliteral(ls->L, "__methods");
  }
  
  init_exp(&key_exp, VK, luaK_stringK(fs, table_name_ts));
  expdesc class_exp;
  init_exp(&class_exp, VNONRELOC, class_reg);
  luaK_indexed(fs, &class_exp, &key_exp);
  luaK_exp2nextreg(fs, &class_exp);
  
  codestring(&key_exp, method_name);
  luaK_exp2anyreg(fs, &method_exp);
  
  int key_k = luaK_stringK(fs, method_name);
  luaK_codeABC(fs, OP_SETMETHOD, class_reg, key_k, method_exp.u.info);
  
  /* 将方法名添加到 __finals 表，标记为不可重写 */
  TString *finals_ts = luaS_newliteral(ls->L, "__finals");
  init_exp(&key_exp, VK, luaK_stringK(fs, finals_ts));
  init_exp(&class_exp, VNONRELOC, class_reg);
  luaK_indexed(fs, &class_exp, &key_exp);
  luaK_exp2nextreg(fs, &class_exp);
  
  /* 设置 finals[method_name] = true */
  int method_k = luaK_stringK(fs, method_name);
  luaK_codeABC(fs, OP_LOADTRUE, fs->freereg, 0, 0);
  luaK_reserveregs(fs, 1);
  luaK_codeABC(fs, OP_SETFIELD, class_exp.u.info, method_k, fs->freereg - 1);
  
  fs->freereg = class_reg + 1;  /* 释放临时寄存器 */
}


/*
** 解析类定义语句
** 参数：
**   ls - 词法状态
**   line - 起始行号
**   class_flags - 类修饰符标志（CLASS_FLAG_ABSTRACT、CLASS_FLAG_FINAL、CLASS_FLAG_SEALED、CLASS_FLAG_SINGLETON）
** 语法：
**   [abstract|final|sealed|singleton] class ClassName [extends ParentClass] [implements Interface, ...]
**     成员定义...
**   end
*/
/*
** 解析 extends/implements/use 后的父类/接口/trait 表达式
** 与 expr 相同，但不把 '{' 当作函数调用实参（f{} 糖语法），
** 否则 'class B extends A {' 中的 '{' 会被当作调用参数吃掉类体
*/
static void parentexpr (LexState *ls, expdesc *v) {
  cond_suffixedexp(ls, v);
}


static void classstat(LexState *ls, int line, int class_flags, int isexport, int parent_class_reg) {
  FuncState *fs = ls->fs;
  expdesc class_exp, parent_exp, v;
  TString *classname;
  int has_parent = 0;
  int has_static_init = 0;  /* 类体中出现 static function init 时置位 */
  int class_reg;
  luaX_next(ls);  /* 跳过 'class' */
  
  /* 获取类名 */
  classname = str_checkname(ls);
  
  /* 检查泛型参数 <T1, T2, ...> */
  int has_typeparams = 0;
  TString *type_params[8];  /* 最多 8 个类型参数 */
  int num_params = 0;
  if (ls->t.token == '<') {
    has_typeparams = 1;
    luaX_next(ls);  /* 跳过 '<' */
    
    /* 收集类型参数名 */
    do {
      type_params[num_params++] = str_checkname(ls);
    } while (testnext(ls, ',') && num_params < 8);
    
    checknext(ls, '>');
  }
  
  /* 创建类表 - 使用OP_NEWCLASS操作码 */
  class_reg = fs->freereg;
  luaK_reserveregs(fs, 1);
  
  /* 生成 NEWCLASS 指令: R[class_reg] = newclass(K[Bx]) */
  int classname_k = luaK_stringK(fs, classname);
  luaK_codeABx(fs, OP_NEWCLASS, class_reg, classname_k);
  
  /* 如果有类修饰符（abstract、final、sealed），设置类标志 */
  if (class_flags != 0) {
    /* 获取 __flags 字段 */
    TString *flags_ts = luaS_newliteral(ls->L, "__flags");
    int flags_k = luaK_stringK(fs, flags_ts);
    int flags_reg = fs->freereg;
    luaK_reserveregs(fs, 1);
    
    /* 读取当前 flags */
    luaK_codeABC(fs, OP_GETFIELD, flags_reg, class_reg, flags_k);
    
    /* flags |= class_flags */
    luaK_int(fs, fs->freereg, class_flags);
    luaK_reserveregs(fs, 1);
    luaK_codeABC(fs, OP_BOR, flags_reg, flags_reg, fs->freereg - 1);
    luaK_codeABC(fs, OP_MMBIN, flags_reg, flags_reg, TM_BOR);
    
    /* 写回 flags */
    luaK_codeABC(fs, OP_SETFIELD, class_reg, flags_k, flags_reg);
    
    fs->freereg = class_reg + 1;  /* 释放临时寄存器 */
  }
  
  /* 如果有泛型参数，创建类型参数表并存储到类的 __typeparams */
  if (has_typeparams) {
    int typeparams_reg = fs->freereg;
    luaK_reserveregs(fs, 1);
    int pc = luaK_codeABC(fs, OP_NEWTABLE, typeparams_reg, 0, 0);
    luaK_code(fs, 0);  /* extra arg */
    
    for (int i = 0; i < num_params; i++) {
      int name_k = luaK_stringK(fs, type_params[i]);
      luaK_codeABx(fs, OP_LOADK, fs->freereg, name_k);
      luaK_reserveregs(fs, 1);
      luaK_codeABC(fs, OP_SETI, typeparams_reg, i + 1, fs->freereg - 1);
      fs->freereg = typeparams_reg + 2;
    }
    
    /* 存储类型参数到类元数据 */
    TString *tp_key = luaS_newliteral(ls->L, "__typeparams");
    int tp_key_k = luaK_stringK(fs, tp_key);
    luaK_codeABC(fs, OP_SETFIELD, class_reg, tp_key_k, typeparams_reg);
    fs->freereg = class_reg + 1;
  }
  
  /* 检查是否有继承：支持 'class Name : Parent' 冒号语法（单继承，与 AST 解析器对齐） */
  if (testnext(ls, ':')) {
    has_parent = 1;
    parentexpr(ls, &parent_exp);
    luaK_exp2nextreg(fs, &parent_exp);
    luaK_codeABC(fs, OP_INHERIT, class_reg, parent_exp.u.info, 0);
    fs->freereg--;  /* 释放父类寄存器 */
  }
  /* 检查是否有继承（软关键字 extends），支持单继承和多继承（逗号分隔） */
  else if (softkw_testnext(ls, SKW_EXTENDS, SOFTKW_CTX_CLASS_INHERIT)) {
    has_parent = 1;
    /* 解析第一个父类表达式 */
    parentexpr(ls, &parent_exp);
    luaK_exp2nextreg(fs, &parent_exp);
    int first_parent_reg = parent_exp.u.info;

    if (testnext(ls, ',')) {
      /* 多继承：创建父类列表table，收集所有父类 */
      int parents_reg = fs->freereg;
      luaK_reserveregs(fs, 1);
      int table_pc = luaK_codeABC(fs, OP_NEWTABLE, parents_reg, 0, 0);
      /* 预分配数组空间，先按2个预估，后面动态增长 */
      luaK_settablesize(fs, table_pc, parents_reg, 4, 0);
      fs->pc++;

      /* 添加第一个父类到列表 */
      luaK_codeABC(fs, OP_SETI, parents_reg, 1, first_parent_reg);
      /* 注意：此处不能 freereg--，parents_reg 的表寄存器必须保持占用，
         否则后续父类会 exp2nextreg 到 parents_reg 覆盖父类列表表 */
      int parent_count = 1;

      /* 解析后续父类 */
      do {
        expdesc next_parent;
        parentexpr(ls, &next_parent);
        luaK_exp2nextreg(fs, &next_parent);
        parent_count++;
        luaK_codeABC(fs, OP_SETI, parents_reg, parent_count, next_parent.u.info);
        fs->freereg--;
      } while (testnext(ls, ','));

      /* OP_MULTIINHERIT: 继承父类列表中的所有类并计算MRO */
      luaK_codeABC(fs, OP_MULTIINHERIT, class_reg, parents_reg, 0);
      fs->freereg = class_reg + 1;  /* 释放父类列表寄存器 */
    } else {
      /* 单继承：直接使用OP_INHERIT */
      luaK_codeABC(fs, OP_INHERIT, class_reg, first_parent_reg, 0);
      fs->freereg--;  /* 释放父类寄存器 */
    }
  }
  
  /* 检查是否实现接口（软关键字 implements） */
  if (softkw_testnext(ls, SKW_IMPLEMENTS, SOFTKW_CTX_CLASS_INHERIT)) {
    do {
      expdesc iface_exp;
      parentexpr(ls, &iface_exp);
      luaK_exp2nextreg(fs, &iface_exp);
      /* 生成 OP_IMPLEMENT 指令: R[class_reg] implements R[iface_reg] */
      luaK_codeABC(fs, OP_IMPLEMENT, class_reg, iface_exp.u.info, 0);
      fs->freereg--;
    } while (testnext(ls, ','));
  }
  
  /* 检查是否使用trait（软关键字 use） */
  if (softkw_testnext(ls, SKW_USE, SOFTKW_CTX_CLASS_INHERIT)) {
    do {
      expdesc trait_exp;
      parentexpr(ls, &trait_exp);
      luaK_exp2nextreg(fs, &trait_exp);
      /* 生成 OP_USETRAIT 指令: R[class_reg] use R[trait_reg] */
      luaK_codeABC(fs, OP_USETRAIT, class_reg, trait_exp.u.info, 0);
      fs->freereg--;
    } while (testnext(ls, ','));
  }
  
  int has_brace = testnext(ls, '{');
  if (!has_brace) testnext(ls, TK_DO); /* Optional Universal Block Opener */

  /* 解析类体 */
  while (!(has_brace ? testnext(ls, '}') : testnext(ls, TK_END))) {
    if (ls->t.token == TK_EOS) {
      luaX_syntaxerror(ls, "'end' expected to close class definition");
      break;
    }
    
    if (ls->t.token == '@') {
      int num_decs = parse_decorators(ls);
      push_decorators(ls, num_decs, fs->freereg - num_decs);
    }


    /* 解析修饰符（支持任意顺序组合） */
    int access_level = ACCESS_PUBLIC;  /* 默认公开 */
    int is_static = 0;
    int is_abstract = 0;
    int is_final = 0;
    int is_override = 0;
    int has_access_modifier = 0;  /* 是否已经设置过访问修饰符 */
    
    /* 循环检查所有修饰符，支持任意顺序 */
    int found_modifier = 1;
    while (found_modifier) {
      found_modifier = 0;
      SoftKWID skw = softkw_check(ls, SOFTKW_CTX_CLASS_BODY);
      
      switch (skw) {
        case SKW_PRIVATE:
          if (has_access_modifier) {
            luaX_syntaxerror(ls, "multiple access modifiers not allowed");
          }
          access_level = ACCESS_PRIVATE;
          has_access_modifier = 1;
          softkw_checknext(ls, SOFTKW_CTX_CLASS_BODY);  /* 消费token */
          found_modifier = 1;
          break;
        case SKW_PROTECTED:
          if (has_access_modifier) {
            luaX_syntaxerror(ls, "multiple access modifiers not allowed");
          }
          access_level = ACCESS_PROTECTED;
          has_access_modifier = 1;
          softkw_checknext(ls, SOFTKW_CTX_CLASS_BODY);
          found_modifier = 1;
          break;
        case SKW_PUBLIC:
          if (has_access_modifier) {
            luaX_syntaxerror(ls, "multiple access modifiers not allowed");
          }
          access_level = ACCESS_PUBLIC;
          has_access_modifier = 1;
          softkw_checknext(ls, SOFTKW_CTX_CLASS_BODY);
          found_modifier = 1;
          break;
        case SKW_STATIC:
          if (is_static) {
            luaX_syntaxerror(ls, "duplicate 'static' modifier");
          }
          is_static = 1;
          softkw_checknext(ls, SOFTKW_CTX_CLASS_BODY);
          found_modifier = 1;
          break;
        case SKW_ABSTRACT:
          if (is_abstract) {
            luaX_syntaxerror(ls, "duplicate 'abstract' modifier");
          }
          is_abstract = 1;
          softkw_checknext(ls, SOFTKW_CTX_CLASS_BODY);
          found_modifier = 1;
          break;
        case SKW_FINAL:
          if (is_final) {
            luaX_syntaxerror(ls, "duplicate 'final' modifier");
          }
          is_final = 1;
          softkw_checknext(ls, SOFTKW_CTX_CLASS_BODY);
          found_modifier = 1;
          break;
        case SKW_OVERRIDE:
          if (is_override) {
            luaX_syntaxerror(ls, "duplicate 'override' modifier");
          }
          is_override = 1;
          softkw_checknext(ls, SOFTKW_CTX_CLASS_BODY);
          found_modifier = 1;
          break;
        default:
          break;
      }
    }
    
    /* abstract 和 final 互斥 */
    if (is_abstract && is_final) {
      luaX_syntaxerror(ls, "method cannot be both 'abstract' and 'final'");
    }
    
    /* static 和 abstract 互斥（静态方法不能被重写，因此不能是抽象的） */
    if (is_static && is_abstract) {
      luaX_syntaxerror(ls, "static method cannot be 'abstract'");
    }
    
    /* static 和 override 互斥（静态方法不参与继承链，无需重写） */
    if (is_static && is_override) {
      luaX_syntaxerror(ls, "static method cannot be 'override'");
    }
    
    /* abstract 和 override 互斥 */
    if (is_abstract && is_override) {
      luaX_syntaxerror(ls, "method cannot be both 'abstract' and 'override'");
    }
    
    /* 检查是否是 getter/setter */
    if (softkw_testnext(ls, SKW_GET, SOFTKW_CTX_CLASS_BODY)) {
      /* getter 属性访问器 */
      class_getter(ls, class_reg, access_level);
      continue;
    }
    else if (softkw_testnext(ls, SKW_SET, SOFTKW_CTX_CLASS_BODY)) {
      /* setter 属性访问器 */
      class_setter(ls, class_reg, access_level);
      continue;
    }
    
    /* 解析成员 */
    if (is_abstract && ls->t.token == TK_FUNCTION) {
      /* 抽象方法声明 */
      class_abstract_method(ls, class_reg, is_static, access_level);
    }
    else if (is_final && ls->t.token == TK_FUNCTION) {
      /* final 方法定义 */
      class_final_method(ls, class_reg, is_static, access_level);
    }
    else if (ls->t.token == TK_FUNCTION) {
      /* 普通方法定义 */
      /* 前瞻方法名，检查是否是静态构造函数 static function init */
      int is_static_init = 0;
      if (is_static) {
        int la = luaX_lookahead(ls);
        if (la == TK_NAME) {
          /* 需要进一步检查方法名是否是 init。这里先标记，在 class_method 后处理 */
          is_static_init = 1;
        }
      }
      class_method(ls, class_reg, is_static, access_level, is_override);
      /* 静态构造函数：记录标记，等类存储到全局变量后再发射 OP_STATICINIT，
         使静态 init 内部能通过全局类名访问类自身 */
      if (is_static_init) {
        has_static_init = 1;
      }
    }
    else if (ls->t.token == TK_NAME) {
      /* 检查是否是嵌套类定义 */
      if (softkw_test(ls, SKW_CLASS, SOFTKW_CTX_CLASS_BODY)) {
        /* 嵌套类：递归解析，存储到父类的 __statics 表 */
        classstat(ls, ls->linenumber, 0, 0, class_reg);
        continue;
      }
      /* 属性定义 */
      class_property(ls, class_reg, is_static, access_level);
    }
    else if (ls->t.token == ';') {
      /* 空语句，跳过 */
      luaX_next(ls);
    }
    else if (ls->t.token == TK_END) {
      /* 类体结束，不报错 */
      break;
    }
    else {
      luaX_syntaxerror(ls, "invalid member definition in class body");
    }
  }
  
  /* 将类存储到变量中 */
  if (parent_class_reg >= 0) {
    /* 嵌套类：存储到父类的 __statics 表 */
    int name_k = luaK_stringK(fs, classname);
    luaK_codeABC(fs, OP_SETSTATIC, parent_class_reg, name_k, class_reg);
    /* 同时注册到全局作用域（与 AST 解析器对齐）：
       使兄弟嵌套类可作为 extends 父类被解析，且 is 运算符能找到 */
    {
      expdesc gv, cv;
      buildglobal(ls, classname, &gv);
      init_exp(&cv, VNONRELOC, class_reg);
      luaK_storevar(fs, &gv, &cv);
    }
  }
  /* 检查是在全局还是局部作用域 */
  else if (isexport) {
     new_localvar(ls, classname);
     add_export(ls, classname);
     adjustlocalvars(ls, 1);
     init_var(fs, &v, fs->nactvar - 1);
  } else {
     buildglobal(ls, classname, &v);
  }
  if (parent_class_reg < 0) {
    init_exp(&class_exp, VNONRELOC, class_reg);
    apply_decorators_inline(ls, &v, &class_exp);
    luaK_storevar(fs, &v, &class_exp);
    /* 静态构造函数：类存储到全局变量后调用（使静态 init 能访问全局类名） */
    if (has_static_init) {
      /* OP_STATICINIT A B: 调用类的静态构造函数
         A = class_reg（类表寄存器）
         B = 0（VM 层从 class.__statics.init 查找并调用） */
      luaK_codeABC(fs, OP_STATICINIT, class_reg, 0, 0);
    }
  }
  
  luaK_fixline(fs, line);
}


/*
** 解析trait定义语句
** 参数：
**   ls - 词法状态
**   line - 起始行号
** 语法：
**   trait TraitName
**     function methodName(self, ...)
**       -- 默认实现
**     end
**     require function methodName(self, ...): returnType
**   end
*/
static void traitstat(LexState *ls, int line, int isexport) {
  FuncState *fs = ls->fs;
  expdesc trait_exp, v;
  TString *traitname;
  int trait_reg;

  luaX_next(ls);  /* 跳过 'trait' */

  /* 获取trait名 */
  traitname = str_checkname(ls);

  /* 创建trait表 */
  trait_reg = fs->freereg;
  luaK_reserveregs(fs, 1);

  /* 使用 NEWCLASS 创建trait（trait本质上是一个不能实例化的类） */
  int traitname_k = luaK_stringK(fs, traitname);
  luaK_codeABx(fs, OP_NEWCLASS, trait_reg, traitname_k);

  /* 设置trait标志 */
  luaK_codeABC(fs, OP_SETTRAITFLAG, trait_reg, 0, 0);

  /* 解析trait体：支持 do...end / begin...end / 隐式 end 以及 {...} 块体 */
  int trait_end_tok = TK_END;
  if (testnext(ls, '{')) {
    trait_end_tok = '}';
  } else {
    testnext(ls, TK_DO);  /* 可选块开始符 */
  }
  while (!testnext(ls, trait_end_tok)) {
    if (ls->t.token == TK_EOS) {
      luaX_syntaxerror(ls, "'end' expected to close trait definition");
      break;
    }

    /* 检查是否是require关键字 */
    int is_require = softkw_testnext(ls, SKW_REQUIRE, SOFTKW_CTX_TRAIT_BODY);

    if (ls->t.token == TK_FUNCTION) {
      luaX_next(ls);  /* 跳过 'function' */

      TString *method_name = str_checkname(ls);

      if (is_require) {
        /* require方法: 声明方法签名，不提供实现 */
        checknext(ls, '(');
        /* 解析参数列表并计算参数个数 */
        int param_count = 0;
        while (ls->t.token != ')' && ls->t.token != TK_EOS) {
          if (ls->t.token == TK_NAME) {
            param_count++;
          }
          luaX_next(ls);
        }
        checknext(ls, ')');
        /* 可选返回类型 */
        if (ls->t.token == ':') {
          luaX_next(ls);  /* 跳过 ':' */
          if (is_type_token(ls->t.token)) {
            luaX_next(ls);  /* 跳过返回类型 */
          }
        }

        /* 生成 SETTRAITREQUIRE 指令 */
        int method_k = luaK_stringK(fs, method_name);
        luaK_codeABC(fs, OP_SETTRAITREQUIRE, trait_reg, method_k, param_count);
      } else {
        /* 普通方法: 编译函数体 */
        /* 创建闭包函数 */
        int has_params = 0;
        /* 检查参数列表 */
        if (ls->t.token == '(') {
          has_params = 1;
        }

        /* 创建方法函数体 */
        expdesc method;
        body(ls, &method, 0, ls->linenumber);

        /* 将方法添加到trait */
        luaK_exp2nextreg(fs, &method);
        int method_k = luaK_stringK(fs, method_name);
        luaK_codeABC(fs, OP_SETMETHOD, trait_reg, method_k, method.u.info);
        fs->freereg = trait_reg + 1;  /* 释放方法寄存器 */
      }
    }
    else if (ls->t.token == ';') {
      luaX_next(ls);
    }
    else {
      luaX_syntaxerror(ls, "only methods allowed in trait");
    }
  }

  /* 将trait存储到变量中 */
  if (isexport) {
     new_localvar(ls, traitname);
     add_export(ls, traitname);
     adjustlocalvars(ls, 1);
     init_var(fs, &v, fs->nactvar - 1);
  } else {
     buildglobal(ls, traitname, &v);
  }
  init_exp(&trait_exp, VNONRELOC, trait_reg);
  apply_decorators_inline(ls, &v, &trait_exp);
  luaK_storevar(fs, &v, &trait_exp);

  luaK_fixline(fs, line);
}


/*
** 解析接口定义语句
** 参数：
**   ls - 词法状态
**   line - 起始行号
** 语法：
**   interface InterfaceName
**     function methodName(self, ...)
**   end
*/
static void interfacestat(LexState *ls, int line, int isexport) {
  FuncState *fs = ls->fs;
  expdesc iface_exp, v;
  TString *ifacename;
  int iface_reg;
  
  luaX_next(ls);  /* 跳过 'ointerface' */
  
  /* 获取接口名 */
  ifacename = str_checkname(ls);
  
  /* 创建接口表 */
  iface_reg = fs->freereg;
  luaK_reserveregs(fs, 1);
  
  /* 使用 NEWCLASS 创建接口（带有接口标志） */
  int ifacename_k = luaK_stringK(fs, ifacename);
  luaK_codeABx(fs, OP_NEWCLASS, iface_reg, ifacename_k);
  
  /* 设置接口标志 */
  luaK_codeABC(fs, OP_SETIFACEFLAG, iface_reg, 0, 0);
  
  /* 检查接口继承（软关键字 extends） */
  /* 接口可以继承多个父接口，以逗号分隔 */
  if (softkw_testnext(ls, SKW_EXTENDS, SOFTKW_CTX_CLASS_INHERIT)) {
    do {
      expdesc parent_iface_exp;
      /* 用 pipe_funcexp（仅字段访问）避免把接口体 '{' 当成调用实参 */
      pipe_funcexp(ls, &parent_iface_exp);
      luaK_exp2nextreg(fs, &parent_iface_exp);
      /* 生成 OP_EXTENDIFACE 指令: R[iface_reg].__parent := R[parent_reg] */
      luaK_codeABC(fs, OP_EXTENDIFACE, iface_reg, parent_iface_exp.u.info, 0);
      fs->freereg--;
    } while (testnext(ls, ','));
  }
  
  /* 解析接口体：支持 do...end / begin...end / 隐式 end 以及 {...} 块体；只允许方法声明 */
  int iface_end_tok = TK_END;
  if (testnext(ls, '{')) {
    iface_end_tok = '}';
  } else {
    testnext(ls, TK_DO);  /* 可选块开始符 */
  }
  while (!testnext(ls, iface_end_tok)) {
    if (ls->t.token == TK_EOS) {
      luaX_syntaxerror(ls, "'end' expected to close interface definition");
      break;
    }
    
    /* 可选的 require 前缀（require function name(...)），与 trait 语法对齐 */
    if (ls->t.token == TK_NAME && strcmp(getstr(ls->t.seminfo.ts), "require") == 0) {
      luaX_next(ls);
    }
    
    if (testnext(ls, TK_FUNCTION)) {
      /* 方法声明（只有签名，没有实现） */
      TString *method_name = str_checkname(ls);
      checknext(ls, '(');
      /* 解析参数列表并计算参数个数 */
      int param_count = 0;
      while (ls->t.token != ')' && ls->t.token != TK_EOS) {
        if (ls->t.token == TK_NAME) {
          param_count++;
        }
        luaX_next(ls);
      }
      checknext(ls, ')');
      
      /* 记录方法签名到接口表，值为参数个数 */
      int method_k = luaK_stringK(fs, method_name);
      luaK_codeABC(fs, OP_ADDMETHOD, iface_reg, method_k, param_count);
    }
    else if (ls->t.token == ';') {
      luaX_next(ls);
    }
    else {
      luaX_syntaxerror(ls, "only method declarations allowed in interface");
    }
  }
  
  /* 将接口存储到变量中 */
  if (isexport) {
     new_localvar(ls, ifacename);
     add_export(ls, ifacename);
     adjustlocalvars(ls, 1);
     init_var(fs, &v, fs->nactvar - 1);
  } else {
     buildglobal(ls, ifacename, &v);
  }
  init_exp(&iface_exp, VNONRELOC, iface_reg);
  luaK_storevar(fs, &v, &iface_exp);
  
  luaK_fixline(fs, line);
}


static int is_type_token(int token) {
  return token == TK_TYPE_INT || token == TK_TYPE_FLOAT || token == TK_DOUBLE ||
         token == TK_BOOL || token == TK_VOID || token == TK_CHAR ||
         token == TK_LONG || token == TK_NAME; /* NAME for structs/classes */
}

/* 判断token是否是可作为标识符使用的软关键字（delete/guard/let） */
static int is_nameable_keyword(int token) {
  return token == TK_DELETE || token == TK_GUARD || token == TK_LET;
}

/* 判断token是否可作为名字使用（普通名字+类型名+软关键字） */
static int is_nametoken(int token) {
  return token == TK_NAME || is_type_token(token) || is_nameable_keyword(token);
}

/*
** 解析 struct 定义
** 语法: struct Name { field = value, ... }
** 编译为: Name = __struct_define("Name", { "field", value, ... })
*/
static void superstructstat (LexState *ls, int line) {
  FuncState *fs = ls->fs;
  expdesc v, key, val;
  TString *name;

  luaX_next(ls);  /* skip SUPERSTRUCT */
  name = str_checkname(ls);

  /* Create SuperStruct in a register */
  int name_k = luaK_stringK(fs, name);
  init_exp(&v, VRELOC, luaK_codeABx(fs, OP_NEWSUPER, 0, name_k));
  luaK_exp2nextreg(fs, &v);
  int ss_reg = v.u.info;

  checknext(ls, '[');

  while (ls->t.token != ']' && ls->t.token != TK_EOS) {
    if (ls->t.token == TK_NAME) {
      codestring(&key, ls->t.seminfo.ts);
      luaX_next(ls);
    } else if (ls->t.token == TK_STRING) {
      codestring(&key, ls->t.seminfo.ts);
      luaX_next(ls);
    } else if (ls->t.token == '[') {
      luaX_next(ls);
      expr(ls, &key);
      checknext(ls, ']');
    } else {
      expr(ls, &key);
    }

    checknext(ls, ':');
    expr(ls, &val);

    luaK_exp2nextreg(fs, &val);
    luaK_exp2nextreg(fs, &key);

    luaK_codeABC(fs, OP_SETSUPER, ss_reg, key.u.info, val.u.info);

    fs->freereg = ss_reg + 1;

    if (ls->t.token == ',') luaX_next(ls);
  }
  checknext(ls, ']');

  expdesc var;
  buildglobal(ls, name, &var);
  luaK_storevar(fs, &var, &v);

  luaK_fixline(fs, line);
}

static void structstat (LexState *ls, int line, int isexport) {
  FuncState *fs = ls->fs;
  expdesc struct_name_exp, v;
  TString *structname;

  luaX_next(ls);  /* skip 'struct' */

  /* Get struct name */
  structname = str_checkname(ls);

  int is_generic = 0;
  FuncState factory_fs;
  BlockCnt factory_bl;
  int nparams = 0;

  if (ls->t.token == '(') {
      is_generic = 1;

      /* Open factory function */
      /* We need to add prototype to parent before switching fs */
      Proto *p = addprototype(ls);
      p->linedefined = line;
      factory_fs.f = p;

      open_func(ls, &factory_fs, &factory_bl);

      luaX_next(ls); /* skip '(' */

      /* Parse generic params */
      do {
          TString *pname = str_checkname(ls);
          /* Optional constraint */
          if (testnext(ls, ':')) {
              TypeHint *th = typehint_new(ls);
              checktypehint(ls, th);
          }
          new_localvar(ls, pname);
          nparams++;
      } while (testnext(ls, ','));

      checknext(ls, ')');

      adjustlocalvars(ls, nparams);
      factory_fs.f->numparams = factory_fs.nactvar;
      luaK_reserveregs(&factory_fs, factory_fs.nactvar);

      fs = &factory_fs;
  }

  /* Prepare to call struct.define(name, {fields}) */
  expdesc func_exp;
  singlevaraux(fs, luaS_newliteral(ls->L, "struct"), &func_exp, 1);
  if (func_exp.k == VVOID) {
      /* Fallback to _ENV */
      expdesc key;
      singlevaraux(fs, ls->envn, &func_exp, 1);
      codestring(&key, luaS_newliteral(ls->L, "struct"));
      luaK_indexed(fs, &func_exp, &key);
  }

  /* Ensure struct table is in register */
  luaK_exp2anyregup(fs, &func_exp);

  /* Get 'define' field */
  expdesc key_define;
  codestring(&key_define, luaS_newliteral(ls->L, "define"));
  luaK_indexed(fs, &func_exp, &key_define);

  luaK_exp2nextreg(fs, &func_exp);
  int func_reg = func_exp.u.info;

  /* Arg 1: Name string */
  expdesc name_arg;
  codestring(&name_arg, structname);
  luaK_exp2nextreg(fs, &name_arg);

  /* Arg 2: Fields table */
  /* We build the table manually using OP_NEWTABLE and filling it as an array */
  /* The array content: { "field1", val1, "field2", val2, ... } */
  int table_reg = fs->freereg;
  int pc = luaK_codeABC(fs, OP_NEWTABLE, table_reg, 0, 0);
  luaK_code(fs, 0); /* extra arg */
  luaK_reserveregs(fs, 1);

  checknext(ls, '{');

  int i = 1; /* Array index, 1-based */
  while (ls->t.token != '}' && ls->t.token != TK_EOS) {
      TString *fname = NULL;
      expdesc val_exp;

      int is_typed = 0;
      if (is_type_token(ls->t.token)) {
          if (ls->t.token != TK_NAME) {
              is_typed = 1;
          } else if (luaX_lookahead(ls) == TK_NAME) {
              is_typed = 1;
          }
      }

      if (is_typed) {
          /* Type Field syntax: Type Field [= Value] */
          TString *type_name = NULL;
          if (ls->t.token == TK_NAME) {
              type_name = str_checkname(ls);
          } else {
              const char *ts = NULL;
              switch (ls->t.token) {
                  case TK_TYPE_INT: ts = "int"; break;
                  case TK_TYPE_FLOAT: ts = "float"; break;
                  case TK_DOUBLE: ts = "double"; break;
                  case TK_BOOL: ts = "bool"; break;
                  case TK_VOID: ts = "void"; break;
                  case TK_CHAR: ts = "char"; break;
                  case TK_LONG: ts = "long"; break;
                  default: luaX_syntaxerror(ls, "unexpected type token"); break;
              }
              type_name = luaS_new(ls->L, ts);
              luaX_next(ls);
          }
          fname = str_checkname(ls);

          if (ls->t.token == '[') {
              luaX_next(ls); /* skip '[' */

              expdesc array_func;
              singlevaraux(fs, luaS_newliteral(ls->L, "array"), &array_func, 1);
              if (array_func.k == VVOID) {
                  expdesc k;
                  singlevaraux(fs, ls->envn, &array_func, 1);
                  codestring(&k, luaS_newliteral(ls->L, "array"));
                  luaK_indexed(fs, &array_func, &k);
              }
              luaK_exp2nextreg(fs, &array_func);
              int base = array_func.u.info;

              expdesc type_arg;
              const char *tname = getstr(type_name);
              if (strcmp(tname, "int") == 0 || strcmp(tname, "integer") == 0 ||
                  strcmp(tname, "float") == 0 || strcmp(tname, "number") == 0 ||
                  strcmp(tname, "bool") == 0 || strcmp(tname, "boolean") == 0 ||
                  strcmp(tname, "string") == 0) {
                  codestring(&type_arg, type_name);
              } else {
                  singlevaraux(fs, type_name, &type_arg, 1);
                  if (type_arg.k == VVOID) {
                      expdesc k;
                      singlevaraux(fs, ls->envn, &type_arg, 1);
                      codestring(&k, type_name);
                      luaK_indexed(fs, &type_arg, &k);
                  }
              }
              luaK_exp2nextreg(fs, &type_arg);

              luaK_codeABC(fs, OP_CALL, base, 2, 2);
              /* Result (factory) is in 'base' */

              expdesc size_exp;
              expr(ls, &size_exp);
              checknext(ls, ']');

              expdesc factory_exp;
              init_exp(&factory_exp, VNONRELOC, base);
              luaK_indexed(fs, &factory_exp, &size_exp);

              luaK_exp2nextreg(fs, &factory_exp);
              val_exp = factory_exp;
          } else if (ls->t.token == '=') {
              luaX_next(ls);
              expr(ls, &val_exp);
          } else {
              /* Generate Default Value based on Type */
              const char *tname = getstr(type_name);
              if (strcmp(tname, "int") == 0 || strcmp(tname, "integer") == 0) {
                  init_exp(&val_exp, VKINT, 0); val_exp.u.ival = 0;
              } else if (strcmp(tname, "float") == 0 || strcmp(tname, "number") == 0) {
                  init_exp(&val_exp, VKFLT, 0); val_exp.u.nval = 0.0;
              } else if (strcmp(tname, "bool") == 0 || strcmp(tname, "boolean") == 0) {
                  init_exp(&val_exp, VFALSE, 0);
              } else if (strcmp(tname, "string") == 0) {
                  codestring(&val_exp, luaS_newliteral(ls->L, ""));
              } else {
                  /* Custom/Struct Type: emit Type() */
                  expdesc type_func;
                  singlevaraux(fs, type_name, &type_func, 1);
                  if (type_func.k == VVOID) {
                      expdesc k;
                      singlevaraux(fs, ls->envn, &type_func, 1);
                      codestring(&k, type_name);
                      luaK_indexed(fs, &type_func, &k);
                  }
                  luaK_exp2nextreg(fs, &type_func);
                  int base = type_func.u.info;
                  init_exp(&val_exp, VCALL, luaK_codeABC(fs, OP_CALL, base, 1, 2));
                  fs->freereg = base + 1;
              }
          }
      } else if (ls->t.token == TK_NAME) {
          /* Field = Value 语法；另支持 Field: Value 默认值语法（与 AST 解析器对齐） */
          fname = str_checkname(ls);
          if (testnext(ls, ':')) {
              /* 区分 'name: Type [= value]' 与 'name: value'：
                 冒号后跟类型注解 token 且其后是 '='/','/'}' 时视为类型注解，
                 否则把冒号后的内容当作默认值表达式 */
              int la = luaX_lookahead(ls);
              int is_typehint = is_type_token(ls->t.token) &&
                                (la == '=' || la == ',' || la == '}' || la == ';');
              if (is_typehint) {
                  TypeHint *th = typehint_new(ls);
                  checktypehint(ls, th);
                  checknext(ls, '=');
                  expr(ls, &val_exp);
              } else {
                  expr(ls, &val_exp);  /* name: value → 默认值 */
              }
          } else {
              checknext(ls, '=');
              expr(ls, &val_exp);
          }
      } else {
          error_expected(ls, TK_NAME);
      }

      luaK_exp2nextreg(fs, &val_exp);

      /* Store name at index i */
      expdesc t_exp;
      init_exp(&t_exp, VNONRELOC, table_reg);

      expdesc key_idx;
      init_exp(&key_idx, VKINT, 0);
      key_idx.u.ival = i;
      luaK_indexed(fs, &t_exp, &key_idx);

      expdesc fname_exp;
      codestring(&fname_exp, fname);
      luaK_storevar(fs, &t_exp, &fname_exp);

      /* Store value at index i+1 */
      init_exp(&t_exp, VNONRELOC, table_reg);

      key_idx.u.ival = i + 1;
      luaK_indexed(fs, &t_exp, &key_idx);

      luaK_storevar(fs, &t_exp, &val_exp);

      i += 2;

      if (ls->t.token == ',' || ls->t.token == ';')
          luaX_next(ls);
  }
  check_match(ls, '}', '{', line);

  luaK_settablesize(fs, pc, table_reg, i - 1, 0);

  /* Call __struct_define */
  init_exp(&v, VCALL, luaK_codeABC(fs, OP_CALL, func_reg, 3, 2)); /* 2 args, 1 result */
  fs->freereg = func_reg + 1; /* Result is at func_reg */

  if (is_generic) {
      /* Generate return v */
      luaK_ret(fs, func_reg, 1);

      /* Close factory */
      factory_fs.f->lastlinedefined = ls->linenumber;
      codeclosure(ls, &v);
      close_func(ls);

      /* Restore fs to parent */
      fs = ls->fs;
  }

  /* Store result in variable */
  if (isexport) {
     new_localvar(ls, structname);
     add_export(ls, structname);
     adjustlocalvars(ls, 1);
     init_var(fs, &struct_name_exp, fs->nactvar - 1);
  } else {
     buildglobal(ls, structname, &struct_name_exp);
  }

  /* v is now the result of the call (VCALL) or closure */
  luaK_storevar(fs, &struct_name_exp, &v);

  luaK_fixline(fs, line);
}


/*
** 解析枚举定义
** 参数：
**   ls - 词法状态
**   line - enum 关键字所在行号
** 语法：
**   enum EnumName
**       Name [= value]
**       ...
**   end
** 
**   或大括号语法：
**   enum EnumName {
**       Name [= value],
**       ...
**   }
** 
** 枚举会被编译为一个表，其中枚举成员作为键，值为整数
** 如果没有显式赋值，则从0开始自动递增
*/
static void enumstat(LexState *ls, int line, int isexport) {
  FuncState *fs = ls->fs;
  expdesc enum_exp, v;
  TString *enumname;
  int enum_reg;
  int use_brace = 0;
  lua_Integer auto_value = 1;  /* 自动递增的枚举值，从1开始（与 AST 解析器对齐） */
  int nh = 0;  /* 枚举成员数量 */
  /* 成员名/值收集（供反射方法生成，与 AST 解析器对齐） */
  TString **member_names = NULL;
  lua_Integer *member_values = NULL;
  int member_cap = 0;
  int member_cap2 = 0;
  
  luaX_next(ls);  /* 跳过 'enum' */
  
  /* 支持 'enum class Name' 形式（与 AST 解析器对齐）：
     带 class 的作用域枚举不注入成员到全局 */
  int is_scoped = 0;
  if (ls->t.token == TK_NAME && strcmp(getstr(ls->t.seminfo.ts), "class") == 0) {
    is_scoped = 1;
    luaX_next(ls);
  }
  
  /* 获取枚举名 */
  enumname = str_checkname(ls);
  
  /* 检查是否使用大括号语法 */
  if (ls->t.token == '{') {
    use_brace = 1;
    luaX_next(ls);  /* 跳过 '{' */
  } else {
    testnext(ls, TK_DO); /* Optional Universal Block Opener */
  }
  
  /* 创建枚举表 */
  enum_reg = fs->freereg;
  int pc = luaK_codeABC(fs, OP_NEWTABLE, enum_reg, 0, 0);
  luaK_code(fs, 0);  /* 为额外参数预留空间 */
  luaK_reserveregs(fs, 1);
  
  /* 解析枚举成员 */
  for (;;) {
    /* 检查结束条件 */
    if (use_brace) {
      if (ls->t.token == '}') break;
    } else {
      if (ls->t.token == TK_END) break;
    }
    
    if (ls->t.token == TK_EOS) {
      if (use_brace) {
        luaX_syntaxerror(ls, "'}' expected to close enum definition");
      } else {
        luaX_syntaxerror(ls, "'end' expected to close enum definition");
      }
      break;
    }
    
    /* 跳过空语句 */
    if (ls->t.token == ';' || ls->t.token == ',') {
      luaX_next(ls);
      continue;
    }
    
    /* 解析枚举成员名 */
    if (ls->t.token != TK_NAME) {
      luaX_syntaxerror(ls, "enum member name expected");
      break;
    }
    
    TString *member_name = str_checkname(ls);
    expdesc key, val;
    
    /* 设置键为成员名 */
    codestring(&key, member_name);
    
    /* 解析成员值（显式赋值或自动递增） */
    expdesc value_exp;
    lua_Integer rec_val;  /* 记录成员值（用于反射表） */
    if (testnext(ls, '=')) {
      /* 显式赋值 */
      expr(ls, &value_exp);
      /* 尝试获取常量值用于自动递增 */
      if (value_exp.k == VKINT) {
        rec_val = value_exp.u.ival;
        auto_value = value_exp.u.ival + 1;
      } else if (value_exp.k == VKFLT) {
        rec_val = (lua_Integer)value_exp.u.nval;
        auto_value = (lua_Integer)value_exp.u.nval + 1;
      } else {
        /* 非常量表达式，无法确定下一个自动值 */
        rec_val = auto_value;
        auto_value++;
      }
    } else {
      /* 自动赋值 */
      rec_val = auto_value;
      init_exp(&value_exp, VKINT, 0);
      value_exp.u.ival = auto_value++;
      (void)val;
    }
    
    /* 记录成员名/值 */
    if (nh >= member_cap) {
      luaM_growvector(ls->L, member_names, nh, member_cap,
                      TString *, MAX_INT, "enum members");
    }
    if (nh >= member_cap2) {
      luaM_growvector(ls->L, member_values, nh, member_cap2,
                      lua_Integer, MAX_INT, "enum values");
    }
    member_names[nh] = member_name;
    member_values[nh] = rec_val;
    
    /* 物化值到寄存器（供枚举表存储与全局注入共用） */
    luaK_exp2nextreg(fs, &value_exp);
    {
      int val_reg = value_exp.u.info;
      /* 存入枚举表 */
      {
        expdesc tab, vreg;
        init_exp(&tab, VNONRELOC, enum_reg);
        luaK_indexed(fs, &tab, &key);
        init_exp(&vreg, VNONRELOC, val_reg);
        luaK_storevar(fs, &tab, &vreg);
      }
      /* 非作用域枚举：成员同时注入全局（与 AST 解析器对齐） */
      if (!is_scoped) {
        expdesc gv, vreg;
        buildglobal(ls, member_name, &gv);
        init_exp(&vreg, VNONRELOC, val_reg);
        luaK_storevar(fs, &gv, &vreg);
      }
      fs->freereg = enum_reg + 1;  /* 释放值寄存器 */
    }
    
    nh++;
    
    /* 处理分隔符 */
    if (use_brace) {
      if (ls->t.token != '}') {
        testnext(ls, ',');  /* 可选的逗号 */
      }
    }
  }
  
  /* 跳过结束符 */
  if (use_brace) {
    checknext(ls, '}');
  } else {
    check_match(ls, TK_END, TK_ENUM, line);
  }
  
  /* 设置表大小 */
  luaK_settablesize(fs, pc, enum_reg, 0, nh);
  
  /* 创建反射机制：_names/_values/_vkmap 表与 names/values/kvmap/vkmap 方法
     （与 AST 解析器对齐） */
  {
    int names_reg = enum_reg + 1;
    int names_pc = luaK_codeABC(fs, OP_NEWTABLE, names_reg, 0, 0);
    luaK_code(fs, 0);
    luaK_reserveregs(fs, 1);
    int values_reg = enum_reg + 2;
    int values_pc = luaK_codeABC(fs, OP_NEWTABLE, values_reg, 0, 0);
    luaK_code(fs, 0);
    luaK_reserveregs(fs, 1);
    int vkmap_reg = enum_reg + 3;
    int vkmap_pc = luaK_codeABC(fs, OP_NEWTABLE, vkmap_reg, 0, 0);
    luaK_code(fs, 0);
    luaK_reserveregs(fs, 1);
    int i2;
    for (i2 = 0; i2 < nh; i2++) {
      expdesc key_idx, item_val, t2;
      /* _names[i+1] = 成员名 */
      init_exp(&key_idx, VKINT, 0); key_idx.u.ival = i2 + 1;
      codestring(&item_val, member_names[i2]);
      init_exp(&t2, VNONRELOC, names_reg);
      luaK_indexed(fs, &t2, &key_idx);
      luaK_storevar(fs, &t2, &item_val);
      fs->freereg = enum_reg + 4;
      /* _values[i+1] = 成员值 */
      init_exp(&key_idx, VKINT, 0); key_idx.u.ival = i2 + 1;
      init_exp(&item_val, VKINT, 0); item_val.u.ival = member_values[i2];
      init_exp(&t2, VNONRELOC, values_reg);
      luaK_indexed(fs, &t2, &key_idx);
      luaK_storevar(fs, &t2, &item_val);
      fs->freereg = enum_reg + 4;
      /* _vkmap[成员值] = 成员名 */
      init_exp(&key_idx, VKINT, 0); key_idx.u.ival = member_values[i2];
      codestring(&item_val, member_names[i2]);
      init_exp(&t2, VNONRELOC, vkmap_reg);
      luaK_indexed(fs, &t2, &key_idx);
      luaK_storevar(fs, &t2, &item_val);
      fs->freereg = enum_reg + 4;
    }
    luaK_settablesize(fs, names_pc, names_reg, nh, 0);
    luaK_settablesize(fs, values_pc, values_reg, nh, 0);
    luaK_settablesize(fs, vkmap_pc, vkmap_reg, 0, nh);
    /* 将反射表与 _nmembers 存到枚举表 */
    {
      expdesc ek, ev, et;
      codestring(&ek, luaS_newliteral(ls->L, "_names"));
      init_exp(&et, VNONRELOC, enum_reg);
      luaK_indexed(fs, &et, &ek);
      init_exp(&ev, VNONRELOC, names_reg);
      luaK_storevar(fs, &et, &ev);
      codestring(&ek, luaS_newliteral(ls->L, "_values"));
      init_exp(&et, VNONRELOC, enum_reg);
      luaK_indexed(fs, &et, &ek);
      init_exp(&ev, VNONRELOC, values_reg);
      luaK_storevar(fs, &et, &ev);
      codestring(&ek, luaS_newliteral(ls->L, "_vkmap"));
      init_exp(&et, VNONRELOC, enum_reg);
      luaK_indexed(fs, &et, &ek);
      init_exp(&ev, VNONRELOC, vkmap_reg);
      luaK_storevar(fs, &et, &ev);
      codestring(&ek, luaS_newliteral(ls->L, "_nmembers"));
      init_exp(&et, VNONRELOC, enum_reg);
      luaK_indexed(fs, &et, &ek);
      init_exp(&ev, VKINT, 0); ev.u.ival = nh;
      luaK_storevar(fs, &et, &ev);
      fs->freereg = enum_reg + 4;
    }
    /* 创建方法闭包: names/values/kvmap/vkmap */
    {
      static const char *method_names[4] = {"names", "values", "kvmap", "vkmap"};
      static const char *field_names[4] = {"_names", "_values", NULL, "_vkmap"};
      int j;
      for (j = 0; j < 4; j++) {
        Proto *mp = luaF_newproto(ls->L);
        mp->numparams = 1;
        mp->is_vararg = 0;
        mp->source = fs->f->source;
        mp->linedefined = line;
        mp->lastlinedefined = line;
        if (field_names[j] == NULL) {
          /* kvmap: function(self) return self end */
          mp->maxstacksize = 1;
          mp->sizecode = 1;
          mp->code = luaM_newvector(ls->L, 1, Instruction);
          mp->code[0] = CREATE_ABCk(OP_RETURN, 0, 2, 0, 0);
        } else {
          /* names/values/vkmap: function(self) return self._XXX end */
          TString *fname = luaS_new(ls->L, field_names[j]);
          mp->maxstacksize = 2;
          mp->sizecode = 2;
          mp->code = luaM_newvector(ls->L, 2, Instruction);
          mp->code[0] = CREATE_ABCk(OP_GETFIELD, 1, 0, 0, 1);
          mp->code[1] = CREATE_ABCk(OP_RETURN, 1, 2, 0, 0);
          mp->sizek = 1;
          mp->k = luaM_newvector(ls->L, 1, TValue);
          setsvalue(ls->L, &mp->k[0], fname);
        }
        /* 添加到父函数的 proto 数组 */
        int bx = fs->np++;
        luaM_growvector(ls->L, fs->f->p, bx, fs->f->sizep, Proto *, MAX_INT, "functions");
        fs->f->p[bx] = mp;
        /* 创建闭包并存储到枚举表 */
        expdesc mcl;
        init_exp(&mcl, VRELOC, luaK_codeABx(fs, OP_CLOSURE, enum_reg + 4, bx));
        luaK_exp2nextreg(fs, &mcl);
        expdesc mk, et2;
        codestring(&mk, luaS_new(ls->L, method_names[j]));
        init_exp(&et2, VNONRELOC, enum_reg);
        luaK_indexed(fs, &et2, &mk);
        luaK_storevar(fs, &et2, &mcl);
        fs->freereg = enum_reg + 4;
      }
    }
    fs->freereg = enum_reg + 1;
  }
  luaM_freearray(ls->L, member_names, member_cap);
  luaM_freearray(ls->L, member_values, member_cap2);
  
  /* 将枚举表存储到全局变量中 */
  if (isexport) {
     new_localvar(ls, enumname);
     add_export(ls, enumname);
     adjustlocalvars(ls, 1);
     init_var(fs, &v, fs->nactvar - 1);
  } else {
     buildglobal(ls, enumname, &v);
  }
  init_exp(&enum_exp, VNONRELOC, enum_reg);
  luaK_storevar(fs, &v, &enum_exp);
  
  luaK_fixline(fs, line);
}


/*
** 解析 new 表达式
** 参数：
**   ls - 词法状态
**   v - 返回的表达式描述符
** 语法：
**   new ClassName(args...)
*/
static void newexpr(LexState *ls, expdesc *v) {
  FuncState *fs = ls->fs;
  int line = ls->linenumber;
  expdesc class_exp, args_exp;
  
  luaX_next(ls);  /* 跳过 'onew' */
  
  /* 只解析主表达式（类名），不解析后面的函数调用 */
  primaryexp(ls, &class_exp);
  luaK_exp2nextreg(fs, &class_exp);
  
  /* 解析构造函数参数 */
  int nargs = 0;
  if (testnext(ls, '(')) {
    if (ls->t.token != ')') {
      do {
        expr(ls, &args_exp);
        luaK_exp2nextreg(fs, &args_exp);
        nargs++;
      } while (testnext(ls, ','));
    }
    checknext(ls, ')');
  }
  
  /* 生成 NEWOBJ 指令 */
  int result_reg = class_exp.u.info;
  luaK_codeABC(fs, OP_NEWOBJ, result_reg, class_exp.u.info, nargs + 1);
  
  init_exp(v, VNONRELOC, result_reg);
  fs->freereg = result_reg + 1;
}


/*
** 解析 super 表达式
** 参数：
**   ls - 词法状态
**   v - 返回的表达式描述符
** 语法：
**   super.methodName(args...)
**   super:methodName(args...)
*/
static void superexpr(LexState *ls, expdesc *v) {
  FuncState *fs = ls->fs;
  int line = ls->linenumber;
  
  luaX_next(ls);  /* 跳过 'osuper' */
  
  /* 查找当前作用域中的self变量 */
  expdesc self_exp;
  TString *self_name = luaS_newliteral(ls->L, "self");
  singlevaraux(fs, self_name, &self_exp, 1);
  
  if (self_exp.k == VVOID) {
    luaX_syntaxerror(ls, "'super' can only be used inside class methods");
  }
  
  /* 检查是否是 super(...) 调用构造函数 */
  if (ls->t.token == '(') {
    /* super(args) -> super:__init(args) */
    luaK_exp2anyreg(fs, &self_exp);
    int self_reg = self_exp.u.info;

    /* 分配连续寄存器用于调用: [method, self, arg1, arg2, ...] */
    int base_reg = fs->freereg;
    luaK_reserveregs(fs, 2);  /* 为 method 和 self 预留 */

    /* 生成 GETSUPER: base_reg = 父类 init 方法 */
    TString *init_name = luaS_newliteral(ls->L, "init");
    int method_k = luaK_stringK(fs, init_name);
    luaK_codeABC(fs, OP_GETSUPER, base_reg, self_reg, method_k);

    /* base_reg + 1 = self */
    luaK_codeABC(fs, OP_MOVE, base_reg + 1, self_reg, 0);

    /* 处理参数列表 */
    expdesc args;
    int nparams;
    luaX_next(ls);  /* 跳过 '(' */
    if (ls->t.token == ')') {
      args.k = VVOID;
    } else {
      explist(ls, &args);
      if (hasmultret(args.k))
        luaK_setmultret(fs, &args);
    }
    check_match(ls, ')', '(', line);

    if (hasmultret(args.k))
      nparams = LUA_MULTRET;
    else {
      if (args.k != VVOID)
        luaK_exp2nextreg(fs, &args);
      nparams = fs->freereg - (base_reg + 1);  /* self 也是参数 */
    }

    /* 生成 CALL 指令 */
    init_exp(v, VCALL, luaK_codeABC(fs, OP_CALL, base_reg, nparams + 1, 2));
    luaK_fixline(fs, line);
    fs->freereg = base_reg + 1;  /* 调用后只留一个返回值 */
    return;
  }

  int is_method_call = 0;
  if (ls->t.token == ':') {
    is_method_call = 1;
    luaX_next(ls);  /* 跳过 ':' */
  }
  else if (ls->t.token == '.') {
    luaX_next(ls);  /* 跳过 '.' */
  }
  else {
    luaX_syntaxerror(ls, "'.', ':' or '(' expected after 'super'");
  }
  
  /* 获取方法名 */
  TString *method_name = str_checkname(ls);
  
  if (is_method_call) {
    /*
    ** super:method(args) - 方法调用语法
    ** 需要直接处理完整的调用，避免 suffixedexp 重新分配寄存器
    */
    luaK_exp2anyreg(fs, &self_exp);
    int self_reg = self_exp.u.info;
    
    /* 分配连续寄存器用于调用: [method, self, arg1, arg2, ...] */
    int base_reg = fs->freereg;
    luaK_reserveregs(fs, 2);  /* 为 method 和 self 预留 */
    
    /* 生成 GETSUPER: base_reg = 父类方法 */
    int method_k = luaK_stringK(fs, method_name);
    luaK_codeABC(fs, OP_GETSUPER, base_reg, self_reg, method_k);
    
    /* base_reg + 1 = self */
    luaK_codeABC(fs, OP_MOVE, base_reg + 1, self_reg, 0);
    
    /* 现在处理参数列表 */
    if (ls->t.token == '(') {
      expdesc args;
      int nparams;
      luaX_next(ls);  /* 跳过 '(' */
      if (ls->t.token == ')') {
        args.k = VVOID;
      } else {
        explist(ls, &args);
        if (hasmultret(args.k))
          luaK_setmultret(fs, &args);
      }
      check_match(ls, ')', '(', line);
      
      if (hasmultret(args.k))
        nparams = LUA_MULTRET;
      else {
        if (args.k != VVOID)
          luaK_exp2nextreg(fs, &args);
        nparams = fs->freereg - (base_reg + 1);  /* self 也是参数 */
      }
      
      /* 生成 CALL 指令 */
      init_exp(v, VCALL, luaK_codeABC(fs, OP_CALL, base_reg, nparams + 1, 2));
      luaK_fixline(fs, line);
      fs->freereg = base_reg + 1;  /* 调用后只留一个返回值 */
    } else {
      luaX_syntaxerror(ls, "'(' expected after 'super:method'");
    }
  }
  else {
    /*
    ** super.method - 只获取父类方法，不绑定 self
    */
    luaK_exp2anyreg(fs, &self_exp);
    int method_k = luaK_stringK(fs, method_name);
    int result_reg = fs->freereg;
    luaK_reserveregs(fs, 1);
    luaK_codeABC(fs, OP_GETSUPER, result_reg, self_exp.u.info, method_k);
    
    init_exp(v, VNONRELOC, result_reg);
  }
}


/*
** 检查token是否是复合赋值运算符
** 参数：
**   token - 要检查的token
** 返回值：
**   对应的二元运算符类型，如果不是复合赋值运算符则返回OPR_NOBINOPR
*/
static BinOpr getcompoundop (int token) {
  switch (token) {
    case TK_ADDEQ:    return OPR_ADD;     /* += */
    case TK_SUBEQ:    return OPR_SUB;     /* -= */
    case TK_MULEQ:    return OPR_MUL;     /* *= */
    case TK_DIVEQ:    return OPR_DIV;     /* /= */
    case TK_IDIVEQ:   return OPR_IDIV;    /* //= */
    case TK_MODEQ:    return OPR_MOD;     /* %= */
    case TK_BANDEQ:   return OPR_BAND;    /* &= */
    case TK_BOREQ:    return OPR_BOR;     /* |= */
    case TK_BXOREQ:   return OPR_BXOR;    /* ~= 作为位异或赋值 */
    case TK_SHREQ:    return OPR_SHR;     /* >>= */
    case TK_SHLEQ:    return OPR_SHL;     /* <<= */
    case TK_CONCATEQ: return OPR_CONCAT;  /* ..= */
    case TK_NULLCOALEQ: return OPR_NULLCOAL; /* ??= */
    case TK_ANDANDEQ: return OPR_AND;     /* &&= 逻辑与赋值（与 AST 解析器对齐） */
    case TK_OROREQ:   return OPR_OR;      /* ||= 逻辑或赋值（与 AST 解析器对齐） */
    case TK_POWEQ:    return OPR_POW;     /* ^= */
    case TK_NE:       return OPR_BXOR;    /* ~= 在赋值上下文中作为位异或赋值 */
    default:          return OPR_NOBINOPR;
  }
}


/*
** 处理复合赋值运算符
** 语法: var op= expr  =>  var = var op expr
** 参数：
**   ls - 词法状态
**   var - 左侧变量的表达式描述符
**   opr - 二元运算符类型
*/
static void compoundassign (LexState *ls, expdesc *var, BinOpr opr) {
  FuncState *fs = ls->fs;
  expdesc e1, e2;
  int line = ls->linenumber;
  
  /* 检查变量是否可赋值 */
  check_condition(ls, vkisvar(var->k), "syntax error");
  check_readonly(ls, var);
  
  /* 跳过复合赋值运算符 */
  luaX_next(ls);
  
  /* 复制变量表达式用于读取当前值 */
  e1 = *var;
  
  /* 将变量转换为寄存器（读取当前值） */
  luaK_exp2nextreg(fs, &e1);
  
  /* 读取右侧表达式 */
  expr(ls, &e2);
  
  /* 准备二元运算 */
  luaK_infix(fs, opr, &e1);
  
  /* 执行二元运算 */
  luaK_posfix(fs, opr, &e1, &e2, line);
  
  /* 将结果转换为任意寄存器 */
  luaK_exp2anyreg(fs, &e1);
  
  /* 将结果存储回变量 */
  luaK_storevar(fs, var, &e1);
}


/*
** 处理自增运算符 (a++)
** 语法: var++  =>  var = var + 1
** 参数：
**   ls - 词法状态
**   var - 变量的表达式描述符
*/
static void incrementstat (LexState *ls, expdesc *var) {
  FuncState *fs = ls->fs;
  expdesc e1, e2;
  int line = ls->linenumber;
  
  /* 检查变量是否可赋值 */
  check_condition(ls, vkisvar(var->k), "syntax error");
  check_readonly(ls, var);
  
  /* 跳过 ++ 运算符（前缀形式已由调用方消费） */
  luaX_next(ls);
  
  /* 复制变量表达式用于读取当前值 */
  e1 = *var;
  
  /* 将变量转换为寄存器（读取当前值） */
  luaK_exp2nextreg(fs, &e1);
  
  /* 创建常量1 */
  init_exp(&e2, VKINT, 0);
  e2.u.ival = 1;
  
  /* 准备加法运算 */
  luaK_infix(fs, OPR_ADD, &e1);
  
  /* 执行加法运算 */
  luaK_posfix(fs, OPR_ADD, &e1, &e2, line);
  
  /* 将结果转换为任意寄存器 */
  luaK_exp2anyreg(fs, &e1);
  
  /* 将结果存储回变量 */
  luaK_storevar(fs, var, &e1);
}


/*
** 前缀自增 ++var（++ 已由调用方消费，不再跳过操作符）
*/
static void prefixincrementstat (LexState *ls, expdesc *var) {
  FuncState *fs = ls->fs;
  expdesc e1, e2;
  int line = ls->linenumber;
  check_condition(ls, vkisvar(var->k), "syntax error");
  check_readonly(ls, var);
  e1 = *var;
  luaK_exp2nextreg(fs, &e1);
  init_exp(&e2, VKINT, 0);
  e2.u.ival = 1;
  luaK_infix(fs, OPR_ADD, &e1);
  luaK_posfix(fs, OPR_ADD, &e1, &e2, line);
  luaK_exp2anyreg(fs, &e1);
  luaK_storevar(fs, var, &e1);
}


/* 前向声明：Shell 风格命令调用 */
static int try_command_call (LexState *ls);

static void exprstat (LexState *ls) {
  /* stat -> func | assignment | compoundassign | increment | cmdcall | walrus */
  FuncState *fs = ls->fs;
  struct LHS_assign v;
  /* 优先尝试 Shell 风格命令调用 */
  if (try_command_call(ls)) {
    return;
  }

  /* 检查海象操作符: NAME := expr (作为独立语句) */
  if (ls->t.token == TK_NAME && luaX_lookahead(ls) == TK_WALRUS) {
    TString *varname = ls->t.seminfo.ts;
    luaX_next(ls);  /* 跳过 NAME */
    luaX_next(ls);  /* 跳过 := */

    /* 解析右侧表达式 */
    expdesc e;
    expr(ls, &e);

    /* 查找变量（先查找局部/upvalue，再查找全局） */
    singlevaraux(fs, varname, &v.v, 0);
    if (v.v.k == VVOID) {
      /* 变量不存在，作为全局变量 */
      singlevaraux(fs, ls->envn, &v.v, 1);
      expdesc key;
      codestring(&key, varname);
      luaK_indexed(fs, &v.v, &key);
    }
    luaK_storevar(fs, &v.v, &e);

    /* 海象操作符作为语句时不使用表达式的值 */
    return;
  }

  /* 保存语句起始行号，用于中缀链跨行检测（suffixedexp 可能消费多行 token）*/
  int stmt_line = ls->linenumber;
  suffixedexp(ls, &v.v);

  /* 检查是否是海象操作符: suffixedexp := expr (如 t.name := val) */
  if (ls->t.token == TK_WALRUS) {
    expdesc e;
    luaX_next(ls);  /* 跳过 := */
    expr(ls, &e);
    luaK_storevar(fs, &v.v, &e);
    return;
  }

  /* 检查是否是自增运算符 */
  if (ls->t.token == TK_PLUSPLUS) {
    incrementstat(ls, &v.v);
    return;
  }

  /* 检查是否是复合赋值运算符 */
  BinOpr opr = getcompoundop(ls->t.token);
  if (opr != OPR_NOBINOPR) {
    compoundassign(ls, &v.v, opr);
    return;
  }

  if (ls->t.token == '=' || ls->t.token == ',') { /* stat -> assignment ? */
    v.prev = NULL;
    restassign(ls, &v, 1);
  }
  else {  /* stat -> func or infix call */
    /* 中缀调用链: receiver method1 arg1 method2 arg2 ...
       无参中缀: receiver method => receiver:method() (行尾)
       关键：使用语句起始行号 stmt_line，防止 suffixedexp 消费了后续行的 token
       导致 receiver_line 错误地指向后续行 */
    int receiver_line = stmt_line;
    while (ls->t.token == TK_NAME && v.v.k != VCALL) {
      /* 方法名必须与 receiver 在同一行，防止跨行误检测 */
      if (ls->t.linenumber != receiver_line)
        break;
      int lookahead = luaX_lookahead(ls);
      int has_arg = is_infix_expr_start(lookahead) && is_same_line_infix(ls);
      /* 通过行号检测行尾：lookahead 已读下一个 token，如果它不在当前行或已是 EOS，说明方法名在行尾 */
      int is_eol = (lookahead == TK_EOS || ls->lookahead.linenumber != ls->t.linenumber);
      /* 既不是同行的有参中缀，也不是行尾的无参中缀，退出 */
      if (!has_arg && !is_eol)
        break;
      TString *method = ls->t.seminfo.ts;
      int line = ls->linenumber;
      luaX_next(ls);  /* 跳过方法名 */
      /* 设置方法调用: receiver:method(arg) */
      {
        expdesc key;
        codestring(&key, method);
        luaK_self(fs, &v.v, &key);
      }
      if (has_arg) {
        /* 解析参数 (使用 infix 优先级限制) */
        expdesc v2;
        int old_ifx4 = ls->expr_flags;
        ls->expr_flags |= E_INFIX_ARG;
        BinOpr nextop = subexpr(ls, &v2, priority[OPR_INFIX].right);
        ls->expr_flags = old_ifx4;
        /* 生成函数调用 */
        int base = v.v.u.info;
        if (hasmultret(v2.k))
          luaK_setmultret(fs, &v2);
        else {
          if (v2.k != VVOID)
            luaK_exp2nextreg(fs, &v2);
        }
        int nparams = (v2.k == VVOID) ? 2 : (fs->freereg - base);
        init_exp(&v.v, VCALL, luaK_codeABC(fs, OP_CALL, base, nparams, 2));
        luaK_fixline(fs, line);
        fs->freereg = base + 1;
        /* 如果被真正的二元运算符终止（非中缀），停止中缀链 */
        if (nextop != OPR_NOBINOPR && nextop != OPR_INFIX) break;
        /* 如果当前 token 已跨行，停止中缀链（防止跨越行边界继续解析） */
        if (ls->t.linenumber != line) break;
      }
      else {
        /* 无参数中缀调用: receiver method => receiver:method() */
        int base = v.v.u.info;
        init_exp(&v.v, VCALL, luaK_codeABC(fs, OP_CALL, base, 2, 2));
        luaK_fixline(fs, line);
        fs->freereg = base + 1;
        break;  /* 无参数中缀调用后不继续链 */
      }
    }
    Instruction *inst;
    check_condition(ls, v.v.k == VCALL, "syntax error");
    inst = &getinstruction(fs, &v.v);
    SETARG_C(*inst, 1);  /* call statement uses no results */
    if (v.v.nodiscard) {
       luaX_warning(ls, "discarding return value of function declared '<nodiscard>'", WT_DISCARDED_RETURN);
    }
  }
}


/*
** 检查当前 token 是否可以作为命令参数的开始
** 参数：
**   token - 要检查的 token
** 返回值：
**   1 如果可以作为参数开始，0 否则
*/
static int is_cmd_arg_start (int token) {
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


/*
** 检查当前 token 是否是语句结束符
** 参数：
**   token - 要检查的 token
** 返回值：
**   1 如果是语句结束符，0 否则
*/
static int is_stmt_terminator (int token) {
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


/*
** Shell 风格命令调用语法处理
** 语法: 命令名 参数1 参数2 ...
** 等价于: 命令名(参数1, 参数2, ...)
** 
** 参数：
**   ls - 词法状态
** 返回值：
**   1 如果成功解析为命令调用，0 否则
*/
static int try_command_call (LexState *ls) {
  FuncState *fs = ls->fs;
  /* 检查是否是 TK_NAME 后面跟着参数 */
  if (ls->t.token != TK_NAME) {
    return 0;
  }

  /* Check if it is a soft keyword that starts an expression (like new, super) */
  if (softkw_test(ls, SKW_NEW, SOFTKW_CTX_EXPR) ||
      softkw_test(ls, SKW_SUPER, SOFTKW_CTX_EXPR)) {
    return 0;
  }

  /* Check if it is a soft keyword that starts a statement (like class, interface) */
  if (softkw_test(ls, SKW_CLASS, SOFTKW_CTX_STMT_BEGIN) ||
      softkw_test(ls, SKW_INTERFACE, SOFTKW_CTX_STMT_BEGIN) ||
      softkw_test(ls, SKW_ABSTRACT, SOFTKW_CTX_STMT_BEGIN) ||
      softkw_test(ls, SKW_FINAL, SOFTKW_CTX_STMT_BEGIN) ||
      softkw_test(ls, SKW_SEALED, SOFTKW_CTX_STMT_BEGIN)) {
    return 0;
  }
  
  /* 预读下一个 token，判断是否可能是命令调用 */
  int lookahead = luaX_lookahead(ls);
  
  /* 如果是普通函数调用/方法调用/字段访问/赋值，不处理 */
  if (lookahead == '(' || lookahead == ':' || lookahead == '.' ||
      lookahead == '=' || lookahead == ',' || lookahead == '[' ||
      lookahead == TK_PLUSPLUS || getcompoundop(lookahead) != OPR_NOBINOPR) {
    return 0;
  }

  /* 如果看起来像中缀调用 (Name Name <expr_start>)，不处理 */
  if (ls->t.seminfo.ts != NULL && lookahead == TK_NAME) {
    int la2 = luaX_lookahead2(ls);
    if (is_infix_expr_start(la2)) {
      return 0;
    }
  }
  
  /* 如果下一个 token 不能作为命令参数开始，不处理 */
  if (!is_cmd_arg_start(lookahead)) {
    return 0;
  }
  
  /*
  ** 重要：Lua 原生支持 func "string" 和 func {table} 语法（单参数调用）
  ** 这种情况会在 suffixedexp 中正确处理链式调用（如 .method()）
  ** 只有当检测到多个参数时，才使用命令调用模式
  ** 
  ** 判断逻辑：如果第一个参数是字符串或表，且后面紧跟 '.' 或 ':'
  ** 或者后面没有更多参数，就让 suffixedexp 处理
  */
  if (lookahead == TK_STRING || lookahead == TK_INTERPSTRING || lookahead == TK_RAWSTRING || lookahead == '{') {
    /* 这是 Lua 原生支持的单参数调用语法，让 suffixedexp 处理 */
    /* 它会正确处理后续的链式调用 */
    return 0;
  }
  
  /* 解析命令调用 */
  int line = ls->linenumber;
  expdesc func;
  int base;
  int nargs = 0;
  
  /* 获取命令名 */
  TString *cmdname = ls->t.seminfo.ts;
  
  /* 首先检查 _CMDS[命令名] 是否存在，生成运行时检查代码 */
  /* 获取命令函数 */
  singlevar(ls, &func);
  luaK_exp2nextreg(fs, &func);
  base = func.u.info;
  
  /* 解析参数列表（只在同一行内解析，换行即停止） */
  while (!is_stmt_terminator(ls->t.token) && ls->t.token != TK_EOS && ls->linenumber == line) {
    expdesc arg;
    
    /* 检查是否遇到语句结束 */
    if (is_stmt_terminator(ls->t.token)) {
      break;
    }
    
    /* 处理特殊的操作符参数（如 -f, -r 等） */
    if (ls->t.token == '-') {
      int next = luaX_lookahead(ls);
      if (next == TK_NAME) {
        /* 构造 "-xxx" 字符串 */
        luaX_next(ls);  /* 跳过 '-' */
        TString *op_name = ls->t.seminfo.ts;
        const char *name = getstr(op_name);
        size_t len = tsslen(op_name);
        char *buf = luaM_newvector(ls->L, len + 2, char);
        buf[0] = '-';
        memcpy(buf + 1, name, len);
        buf[len + 1] = '\0';
        TString *op_str = luaS_newlstr(ls->L, buf, len + 1);
        luaM_freearray(ls->L, buf, len + 2);
        codestring(&arg, op_str);
        luaK_exp2nextreg(fs, &arg);
        nargs++;
        luaX_next(ls);
        continue;
      } else if (next == TK_INT || next == TK_FLT) {
        /* 负数 */
        luaX_next(ls);  /* 跳过 '-' */
        if (ls->t.token == TK_INT) {
          init_exp(&arg, VKINT, 0);
          arg.u.ival = -ls->t.seminfo.i;
        } else {
          init_exp(&arg, VKFLT, 0);
          arg.u.nval = -ls->t.seminfo.r;
        }
        luaK_exp2nextreg(fs, &arg);
        nargs++;
        luaX_next(ls);
        continue;
      }
    }
    
    /* 处理普通参数 */
    switch (ls->t.token) {
      case TK_STRING:
      case TK_INTERPSTRING:
      case TK_RAWSTRING: {
        codestring(&arg, ls->t.seminfo.ts);
        luaK_exp2nextreg(fs, &arg);
        nargs++;
        luaX_next(ls);
        break;
      }
      case TK_INT: {
        init_exp(&arg, VKINT, 0);
        arg.u.ival = ls->t.seminfo.i;
        luaK_exp2nextreg(fs, &arg);
        nargs++;
        luaX_next(ls);
        break;
      }
      case TK_FLT: {
        init_exp(&arg, VKFLT, 0);
        arg.u.nval = ls->t.seminfo.r;
        luaK_exp2nextreg(fs, &arg);
        nargs++;
        luaX_next(ls);
        break;
      }
      case TK_TRUE: {
        init_exp(&arg, VTRUE, 0);
        luaK_exp2nextreg(fs, &arg);
        nargs++;
        luaX_next(ls);
        break;
      }
      case TK_FALSE: {
        init_exp(&arg, VFALSE, 0);
        luaK_exp2nextreg(fs, &arg);
        nargs++;
        luaX_next(ls);
        break;
      }
      case TK_NIL: {
        init_exp(&arg, VNIL, 0);
        luaK_exp2nextreg(fs, &arg);
        nargs++;
        luaX_next(ls);
        break;
      }
      case TK_NAME: {
        /* 变量引用 */
        singlevar(ls, &arg);
        luaK_exp2nextreg(fs, &arg);
        nargs++;
        break;
      }
      case '{': {
        /* 表构造器 */
        constructor(ls, &arg);
        luaK_exp2nextreg(fs, &arg);
        nargs++;
        break;
      }
      case '(': {
        /* 括号表达式 */
        luaX_next(ls);
        expr(ls, &arg);
        checknext(ls, ')');
        luaK_exp2nextreg(fs, &arg);
        nargs++;
        break;
      }
      default: {
        /* 不是有效参数，停止解析 */
        goto done_args;
      }
    }
  }
  
done_args:
  /* 生成函数调用指令，C=1表示不需要返回值（C=0是multret会破坏栈状态） */
  init_exp(&func, VCALL, luaK_codeABC(fs, OP_CALL, base, nargs + 1, 1));
  luaK_fixline(fs, line);
  fs->freereg = base;
  
  return 1;
}


void retstat (LexState *ls) {
  /* stat -> RETURN [explist] [';'] */
  FuncState *fs = ls->fs;
  expdesc e;
  int nret;  /* number of values being returned */
  int first = luaY_nvarstack(fs);  /* first slot to be returned */
  if (block_follow(ls, 1) || ls->t.token == ';')
    nret = 0;  /* return no values */
  else {
    nret = explist(ls, &e);  /* optional return values */
    if (hasmultret(e.k)) {
      luaK_setmultret(fs, &e);
      if (e.k == VCALL && nret == 1 && !fs->bl->insidetbc) {  /* tail call? */
        SET_OPCODE(getinstruction(fs,&e), OP_TAILCALL);
        lua_assert(GETARG_A(getinstruction(fs,&e)) == luaY_nvarstack(fs));
      }
      nret = LUA_MULTRET;  /* return all values */
    }
    else {
      if (nret == 1)  /* only one single value? */
        first = luaK_exp2anyreg(fs, &e);  /* can use original slot */
      else {  /* values must go to the top of the stack */
        luaK_exp2nextreg(fs, &e);
        lua_assert(nret == fs->freereg - first);
      }
    }
  }
  luaK_ret(fs, first, nret);
  testnext(ls, ';');  /* skip optional semicolon */
}


static int is_preprocessor_directive(const char *name) {
  return strcmp(name, "include") == 0 ||
         strcmp(name, "alias") == 0 ||
         strcmp(name, "define") == 0 ||
         strcmp(name, "if") == 0 ||
         strcmp(name, "else") == 0 ||
         strcmp(name, "elseif") == 0 ||
         strcmp(name, "end") == 0 ||
         strcmp(name, "haltcompiler") == 0 ||
         strcmp(name, "type") == 0 ||
         strcmp(name, "declare") == 0;
}

static void parse_alias(LexState *ls) {
  TString *name = str_checkname(ls);
  checknext(ls, '=');

  int capacity = 8;
  int n = 0;
  Token *tokens = luaM_newvector(ls->L, capacity, Token);
  int line = ls->linenumber;

  while (ls->linenumber == line && ls->t.token != TK_EOS) {
     if (n >= capacity) {
       int oldcap = capacity;
       capacity *= 2;
       tokens = luaM_reallocvector(ls->L, tokens, oldcap, capacity, Token);
     }
     tokens[n++] = ls->t;
     luaX_next(ls);
  }

  luaX_addalias(ls, name, tokens, n);
}

static int eval_const_condition(LexState *ls) {
  int val = 0;
  /* Simple evaluation: literals */
  if (ls->t.token == TK_TRUE) val = 1;
  else if (ls->t.token == TK_FALSE) val = 0;
  else if (ls->t.token == TK_INT) val = (ls->t.seminfo.i != 0);
  else if (ls->t.token == TK_NAME) {
     if (ls->defines) {
        TValue key;
        setsvalue(ls->L, &key, ls->t.seminfo.ts);
        const TValue *v = luaH_get(ls->defines, &key);
        val = !l_isfalse(v);
     } else {
        val = 0;
     }
  }
  else {
     /* luaX_syntaxerror(ls, "invalid condition in $if"); */
     val = 0;
  }
  luaX_next(ls); /* consume value */
  if (ls->t.token == TK_THEN) luaX_next(ls);

  return val;
}

static void constexprdefinestat (LexState *ls) {
  luaX_next(ls); /* skip 'define' */
  TString *name = str_checkname(ls);
  if (ls->t.token == '=')
    luaX_next(ls);

  expdesc e;
  expr(ls, &e);

  TValue k;
  if (!luaK_exp2const(ls->fs, &e, &k)) {
     luaX_syntaxerror(ls, "variable was not assigned a compile-time constant value");
  }

  if (ls->defines == NULL) {
     ls->defines = luaH_new(ls->L);
     /* anchor defines table to prevent GC */
     sethvalue2s(ls->L, ls->L->top.p, ls->defines);
     ls->L->top.p++;
  }

  TValue key;
  setsvalue(ls->L, &key, name);
  luaH_set(ls->L, ls->defines, &key, &k);
}

static void skip_block(LexState *ls) {
  int depth = 1;
  while (depth > 0 && ls->t.token != TK_EOS) {
    if (ls->t.token == TK_DOLLAR) {
       int la = luaX_lookahead(ls);
       if (la == TK_NAME) {
         const char *name = getstr(ls->lookahead.seminfo.ts);
         if (strcmp(name, "if") == 0) {
           depth++;
         }
         else if (strcmp(name, "end") == 0) {
           depth--;
           if (depth == 0) return; /* Don't consume $end yet */
         }
         else if (strcmp(name, "else") == 0 || strcmp(name, "elseif") == 0) {
           if (depth == 1) {
             return; /* Stop at else/elseif of current block */
           }
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
    luaX_next(ls);
  }
}

static void consume_end_tag(LexState *ls) {
  if (ls->t.token == TK_DOLLAR) {
    luaX_next(ls);
    if (ls->t.token == TK_END) {
      luaX_next(ls);
    } else if (ls->t.token == TK_NAME && strcmp(getstr(ls->t.seminfo.ts), "end") == 0) {
      luaX_next(ls);
    }
  }
}

static void constexprifstat(LexState *ls) {
   int cond = eval_const_condition(ls);

   if (cond) {
      statlist(ls);
   } else {
      skip_block(ls);
   }

   if (ls->t.token == TK_DOLLAR) {
      luaX_next(ls); /* skip $ */
      int is_else = 0;
      int is_elseif = 0;
      int is_end = 0;

      if (ls->t.token == TK_ELSE) is_else = 1;
      else if (ls->t.token == TK_ELSEIF) is_elseif = 1;
      else if (ls->t.token == TK_END) is_end = 1;
      else if (ls->t.token == TK_NAME) {
         const char *name = getstr(ls->t.seminfo.ts);
         if (strcmp(name, "else") == 0) is_else = 1;
         else if (strcmp(name, "elseif") == 0) is_elseif = 1;
         else if (strcmp(name, "end") == 0) is_end = 1;
      }

      if (is_else) {
         luaX_next(ls);
         if (cond) {
            skip_block(ls);
            consume_end_tag(ls);
         } else {
            statlist(ls);
            consume_end_tag(ls);
         }
      } else if (is_elseif) {
         luaX_next(ls);
         if (cond) {
            /* We took the if branch, so skip everything until end */
            int depth = 1;
            while (depth > 0 && ls->t.token != TK_EOS) {
               if (ls->t.token == TK_DOLLAR) {
                  int la = luaX_lookahead(ls);
                  if (la == TK_NAME) {
                     const char *n = getstr(ls->lookahead.seminfo.ts);
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
               luaX_next(ls);
            }
            consume_end_tag(ls);
         } else {
            constexprifstat(ls);
         }
      } else if (is_end) {
         luaX_next(ls);
      }
   }
}

static void constexprstat (LexState *ls) {
  luaX_next(ls); /* skip $ */

  if (ls->t.token == TK_IF) {
     luaX_next(ls);
     constexprifstat(ls);
     return;
  }

  /* Fallback for other directives that are names */
  if (ls->t.token != TK_NAME) {
     /* Should not happen if statement() checked correctly, but for safety */
     return;
  }

  TString *ts = ls->t.seminfo.ts;
  const char *name = getstr(ts);

  if (strcmp(name, "include") == 0) {
     luaX_next(ls);
     if (ls->t.token != TK_STRING && ls->t.token != TK_RAWSTRING) {
       luaX_syntaxerror(ls, "expected filename string after $include");
     }
     luaX_pushincludefile(ls, getstr(ls->t.seminfo.ts));
     luaX_next(ls);
  }
  else if (strcmp(name, "alias") == 0) {
     luaX_next(ls);
     parse_alias(ls);
  }
  else if (strcmp(name, "haltcompiler") == 0) {
     while (ls->t.token != TK_EOS) luaX_next(ls);
  }
  else if (strcmp(name, "if") == 0) {
     luaX_next(ls);
     constexprifstat(ls);
  }
  else if (strcmp(name, "define") == 0) {
     constexprdefinestat(ls);
  }
  else if (strcmp(name, "type") == 0) {
     luaX_next(ls); /* skip 'type' */
     TString *name = str_checkname(ls);
     checknext(ls, '=');
     TypeHint *th = typehint_new(ls);
     checktypehint(ls, th);
     
     TValue key, val;
     setsvalue(ls->L, &key, name);
     setpvalue(&val, th);
     luaH_set(ls->L, ls->named_types, &key, &val);
     /* printf("DEBUG: defined type '%s'\n", getstr(name)); */
  }
  else if (strcmp(name, "declare") == 0) {
     luaX_next(ls); /* skip 'declare' */
     TString *name = str_checkname(ls);
     TypeHint *th = NULL;
     int nodiscard = 0;

     if (testnext(ls, ':')) {
        th = typehint_new(ls);
        checktypehint(ls, th);
     }

     if (testnext(ls, '<')) {
        if (ls->t.token == TK_NAME) {
           const char *attr = getstr(ls->t.seminfo.ts);
           if (strcmp(attr, "nodiscard") == 0) {
              nodiscard = 1;
           }
           luaX_next(ls);
        }
        checknext(ls, '>');
     }

     TValue key, val;
     setsvalue(ls->L, &key, name);

     Table *decl = luaH_new(ls->L);
     sethvalue2s(ls->L, ls->L->top.p, decl);
     ls->L->top.p++;

     if (nodiscard) {
        TValue k, v;
        setsvalue(ls->L, &k, luaS_newliteral(ls->L, "nodiscard"));
        setbtvalue(&v);
        luaH_set(ls->L, decl, &k, &v);
     }

     if (th) {
        TValue k, v;
        setsvalue(ls->L, &k, luaS_newliteral(ls->L, "type"));
        setpvalue(&v, th);
        luaH_set(ls->L, decl, &k, &v);
     }

     sethvalue(ls->L, &val, decl);

     luaH_set(ls->L, ls->declared_globals, &key, &val);

     ls->L->top.p--; /* pop decl */
  }
  else {
     /* unknown directive - ignore line */
     luaX_next(ls);
     int line = ls->linenumber;
     while (ls->linenumber == line && ls->t.token != TK_EOS) luaX_next(ls);
  }
}

static void deferstat (LexState *ls) {
  FuncState *fs = ls->fs;
  int line = ls->linenumber;
  luaX_next(ls);  /* skip DEFER */

  expdesc b;
  FuncState new_fs;
  BlockCnt bl;
  new_fs.f = addprototype(ls);
  new_fs.f->linedefined = line;
  open_func(ls, &new_fs, &bl);
  new_fs.f->numparams = 0;
  new_fs.f->is_vararg = 0;

  statement(ls);

  new_fs.f->lastlinedefined = ls->linenumber;
  codeclosure(ls, &b);
  close_func(ls);

  int vidx = new_localvarliteral(ls, "(defer)");
  getlocalvardesc(fs, vidx)->vd.kind = RDKTOCLOSE;

  adjustlocalvars(ls, 1);

  expdesc v;
  init_var(fs, &v, vidx);
  luaK_storevar(fs, &v, &b);

  checktoclose(fs, fs->nactvar - 1);
}

/**
 * 解析 delete 语句
 * 语法: delete suffixedexp
 * 功能: 删除表中的指定键（将其设为nil）
 * 示例: delete t.key, delete t["key"], delete t.a.b.c
 * 
 * @param ls 词法分析器状态
 */
static void deletestat (LexState *ls) {
  FuncState *fs = ls->fs;
  luaX_next(ls);  /* 跳过 delete 关键字 */

  expdesc v;
  suffixedexp(ls, &v);  /* 解析要删除的变量/字段 */

  /* 只能删除表字段或全局变量，不能删除局部变量/upvalue */
  check_condition(ls, vkisindexed(v.k) || v.k == VINDEXED || v.k == VINDEXUP || v.k == VINDEXI || v.k == VINDEXSTR,
                  "cannot delete local variable or upvalue");
  check_readonly(ls, &v);

  /* 生成nil常量并赋值给该字段 */
  expdesc nilval;
  init_exp(&nilval, VNIL, 0);
  luaK_storevar(fs, &v, &nilval);
}

/**
 * 解析 C++ 风格的函数参数列表
 * 支持类型前缀（int x, float y 等）和参数默认值（x = expr）
 * 
 * 语法规则:
 *   cpp_parlist -> [ [Type] NAME ['=' expr] { ',' [Type] NAME ['=' expr] } ]
 * 
 * @param ls 词法分析器状态
 */
static void cpp_parlist (LexState *ls) {
  FuncState *fs = ls->fs;
  Proto *f = fs->f;
  int nparams = 0;
  int isvararg = 0;
  if (ls->t.token != ')') {
    do {
      /* Consume type if present */
      if (is_type_token(ls->t.token)) {
         /* If it is NAME, check if it's followed by another NAME (Type Name) */
         if (ls->t.token == TK_NAME) {
            if (luaX_lookahead(ls) == TK_NAME) {
               luaX_next(ls); /* Skip type */
            }
         } else {
            luaX_next(ls); /* Skip primitive type */
         }
      }

      switch (ls->t.token) {
        case TK_NAME: {
          new_localvar(ls, str_checkname(ls));
          /* 立即激活该参数变量并分配寄存器 */
          adjustlocalvars(ls, 1);
          luaK_reserveregs(fs, 1);
          nparams++;
          /* 检查是否有默认值 '=' */
          if (testnext(ls, '=')) {
              int param_reg = getlocalvardesc(fs, fs->nactvar - 1)->vd.ridx;
              /* 生成 nil 检查：如果参数不是nil则跳过默认值赋值 */
              luaK_codeABCk(fs, OP_TESTNIL, param_reg, param_reg, 0, 0);
              int jmp_skip = luaK_jump(fs);
              /* 解析默认值表达式 */
              expdesc default_val;
              expr(ls, &default_val);
              luaK_exp2reg(fs, &default_val, param_reg);
              luaK_patchtohere(fs, jmp_skip);
          }
          break;
        }
        case TK_DOTS: {
          luaX_next(ls);
          isvararg = 1;
          break;
        }
        default: luaX_syntaxerror(ls, "<name> or '...' expected");
      }
    } while (!isvararg && testnext(ls, ','));
  }
  /* 参数已在循环中逐个激活 */
  f->numparams = fs->nactvar;
  if (isvararg)
    setvararg(fs, f->numparams);
}

static void declaration_stat (LexState *ls, int line) {
  /* Current token is a Type keyword. Skip it. */
  luaX_next(ls);

  TString *name = str_checkname(ls);

  if (ls->t.token == '(') {
     /* Function definition: Type Name(...) { ... } */
     expdesc v, b;

     /* Resolve variable (global/field) */
     singlevaraux(ls->fs, name, &v, 1);
     if (v.k == VVOID) { /* global name? */
       expdesc key;
       singlevaraux(ls->fs, ls->envn, &v, 1);  /* get environment variable */
       codestring(&key, name);  /* key is variable name */
       luaK_indexed(ls->fs, &v, &key);  /* env[varname] */
     }

     FuncState new_fs;
     BlockCnt bl;
     new_fs.f = addprototype(ls);
     new_fs.f->linedefined = line;
     open_func(ls, &new_fs, &bl);

     checknext(ls, '(');
     cpp_parlist(ls);
     checknext(ls, ')');

     checknext(ls, '{');
     while (!testtoken(ls, '}')) {
       if (ls->t.token == TK_EOS)
         luaX_syntaxerror(ls, "unfinished function");
       statement(ls);
     }
     luaX_next(ls); /* skip '}' */

     new_fs.f->lastlinedefined = ls->linenumber;
     codeclosure(ls, &b);
     close_func(ls);

     apply_decorators_inline(ls, &v, &b);
  luaK_storevar(ls->fs, &v, &b);
     luaK_fixline(ls->fs, line);

  } else {
     /* Variable declaration: Type Name [= Value]; */
     int is_local = (ls->fs->f->linedefined != 0);

     if (is_local) {
        int vidx = new_localvar(ls, name);
        adjustlocalvars(ls, 1);
        if (testnext(ls, '=')) {
           expdesc e;
           expr(ls, &e);
           expdesc var;
           init_var(ls->fs, &var, vidx);
           luaK_storevar(ls->fs, &var, &e);
        }
        testnext(ls, ';');
     } else {
        expdesc var;
        singlevaraux(ls->fs, name, &var, 1);
        if (var.k == VVOID) {
           expdesc key;
           singlevaraux(ls->fs, ls->envn, &var, 1);
           codestring(&key, name);
           luaK_indexed(ls->fs, &var, &key);
        }

        if (testnext(ls, '=')) {
           expdesc e;
           expr(ls, &e);
           luaK_storevar(ls->fs, &var, &e);
        }
        testnext(ls, ';');
     }
  }
}

static void namespacestat (LexState *ls, int line) {
  FuncState *fs = ls->fs;
  expdesc v, ns;
  TString *name;
  BlockCnt bl;

  luaX_next(ls);  /* skip NAMESPACE */
  name = str_checkname(ls);

  /* Emit OP_NEWNAMESPACE */
  int name_k = luaK_stringK(fs, name);
  init_exp(&ns, VRELOC, luaK_codeABx(fs, OP_NEWNAMESPACE, 0, name_k));
  luaK_exp2nextreg(fs, &ns);

  /* Check for optional argument list: namespace Name (var1, var2) */
  if (ls->t.token == '(') {
    luaX_next(ls);
    while (ls->t.token != ')' && ls->t.token != TK_EOS) {
      TString *argname = str_checkname(ls);

      expdesc val;
      singlevaraux(fs, argname, &val, 1);
      if (val.k == VVOID) {
        expdesc key;
        singlevaraux(fs, ls->envn, &val, 1);
        codestring(&key, argname);
        luaK_indexed(fs, &val, &key);
      }

      luaK_exp2nextreg(fs, &val);

      /* ns[argname] = val */
      expdesc ns_tmp = ns;
      expdesc key;
      codestring(&key, argname);
      luaK_indexed(fs, &ns_tmp, &key);
      luaK_storevar(fs, &ns_tmp, &val);

      if (ls->t.token == ',') {
        luaX_next(ls);
      }
    }
    checknext(ls, ')');
  }

  /* Store in global variable */
  buildglobal(ls, name, &v);
  luaK_storevar(fs, &v, &ns);

  checknext(ls, '{');

  enterblock(fs, &bl, 0);

  /* Create local _ENV = ns */
  int vidx = new_localvarliteral(ls, "_ENV");
  adjustlocalvars(ls, 1);
  fs->freereg = luaY_nvarstack(fs);

  /* Assign ns to _ENV */
  expdesc env_var;
  init_var(fs, &env_var, vidx);
  luaK_storevar(fs, &env_var, &ns);

  while (!testtoken(ls, '}')) {
    if (ls->t.token == TK_EOS)
      luaX_syntaxerror(ls, "unfinished namespace");
    statement(ls);
  }
  luaX_next(ls); /* skip '}' */

  leaveblock(fs);
}

static void usingstat(LexState *ls) {
  luaX_next(ls); /* skip using */

  if (ls->t.token == TK_NAMESPACE) {
     /* using namespace Name[::Member::...]; */
     luaX_next(ls);
     expdesc ns, env;
     TString *name = str_checkname(ls);

     /* Resolve namespace */
     singlevaraux(ls->fs, name, &ns, 1);
     /* Resolve _ENV */
     singlevaraux(ls->fs, ls->envn, &env, 1);

     if (ns.k == VVOID || env.k == VVOID) {
        if (ns.k == VVOID) {
           expdesc key;
           singlevaraux(ls->fs, ls->envn, &ns, 1);
           codestring(&key, name);
           luaK_indexed(ls->fs, &ns, &key);
        }
     }

     /* 处理 ::Member 链: using namespace Outer::Inner::Nested */
     while (testnext(ls, TK_DBCOLON)) {
        TString *member = str_checkname(ls);

        luaK_exp2anyregup(ls->fs, &ns);
        expdesc key;
        codestring(&key, member);
        luaK_indexed(ls->fs, &ns, &key);
     }

     luaK_exp2nextreg(ls->fs, &env);
     luaK_exp2nextreg(ls->fs, &ns);

     /* OP_LINKNAMESPACE A B: R[A]->using_next = R[B] */
     luaK_codeABC(ls->fs, OP_LINKNAMESPACE, env.u.info, ns.u.info, 0);
  } else {
     /* using Name::Member::...; */
     TString *name = str_checkname(ls);
     expdesc e;

     /* Resolve first part */
     singlevaraux(ls->fs, name, &e, 1);
     if (e.k == VVOID) {
        expdesc key;
        singlevaraux(ls->fs, ls->envn, &e, 1);
        codestring(&key, name);
        luaK_indexed(ls->fs, &e, &key);
     }

     /* Loop for ::Member parts */
     while (testnext(ls, TK_DBCOLON)) {
        TString *member = str_checkname(ls);
        name = member; /* Update name for local variable creation */

        luaK_exp2anyregup(ls->fs, &e);
        expdesc key;
        codestring(&key, member);
        luaK_indexed(ls->fs, &e, &key);
     }

     /* Create local variable with the last name */
     int vidx = new_localvar(ls, name);
     adjustlocalvars(ls, 1);
     ls->fs->freereg = luaY_nvarstack(ls->fs);

     expdesc v;
     init_var(ls->fs, &v, vidx);
     luaK_storevar(ls->fs, &v, &e);
  }
  testnext(ls, ';');  /* 结尾分号可选（与 AST 解析器对齐） */
}

void statement (LexState *ls) {
  int line = ls->linenumber;  /* may be needed for error messages */
  LUA_LOGD("[PARSER] statement: line=%d token=%d", line, ls->t.token);
  enterlevel(ls);
  switch (ls->t.token) {
        case '@': {
      int num_decs = parse_decorators(ls);
      push_decorators(ls, num_decs, ls->fs->freereg - num_decs);

      statement(ls);

      /* If decorators were not consumed, clean up */
      int unused_num, unused_regs;
      pop_decorators(ls, &unused_num, &unused_regs);
      if (unused_num > 0) {
          ls->fs->freereg -= unused_num;
      }
      break;
    }
    case ';': {  /* stat -> ';' (empty statement) */
      luaX_next(ls);  /* skip ';' */
      break;
    }
    case TK_WHEN: {  /* stat -> ifstat */
      whenstat(ls, line);
      break;
    }
    case TK_IF: {  /* stat -> ifstat */
      ifstat(ls, line);
      break;
    }
    case TK_DOLLAR: { /* stat -> constexprstat or macro */
      int la = luaX_lookahead(ls);
      if (la == TK_NAME) {
         TString *ts = ls->lookahead.seminfo.ts;
         const char *name = getstr(ts);
         if (is_preprocessor_directive(name)) {
            constexprstat(ls);
            break;
         }
      } else if (la == TK_IF || la == TK_ELSE || la == TK_ELSEIF || la == TK_END) {
         constexprstat(ls);
         break;
      }
      /* Fallthrough to exprstat */
      exprstat(ls);
      break;
    }
    case TK_SWITCH:{
      switchstat(ls, line);
      break;
    }
    case TK_WHILE: {  /* stat -> whilestat */
      whilestat(ls, line);
      break;
    }
    case TK_DO: {  /* stat -> DO block END */
      luaX_next(ls);  /* skip DO */
      block(ls);
      check_match(ls, TK_END, TK_DO, line);
      break;
    }
    case TK_FOR: {  /* stat -> forstat */
      forstat(ls, line);
      break;
    }
    case TK_REPEAT: {  /* stat -> repeatstat */
      repeatstat(ls, line);
      break;
    }
    case TK_TRY: {  /* stat -> trystat */
      trystat(ls, line);
      break;
    }
    case TK_GUARD: {  /* stat -> guardstat */
      guardstat(ls, line);
      break;
    }
    case TK_DEFER: {
      deferstat(ls);
      break;
    }
    case TK_DELETE: {
      deletestat(ls);
      break;
    }
    case TK_LET: {  /* let 变量声明，类似 local */
      luaX_next(ls);  /* skip LET */
      if (testnext(ls, TK_FUNCTION))  /* let function? */
        localfunc(ls, 0, 0);
      else if (testnext(ls, TK_TAKE))  /* let take {...} = expr 解构? */
        takestat_full(ls);
      else
        localstat(ls, 0);
      break;
    }
    case TK_WITH: {  /* stat -> withstat */
      withstat(ls, line);
      break;
    }
    case TK_ASM: {  /* stat -> asmstat */
      asmstat(ls, line);
      break;
    }
    case TK_ASTPARSER: {  /* stat -> astparserstat (0 args call the compiled closure) */
      int reg = astparserstat(ls, line);
      /* OP_CALL A B C: B-1 args (B=1→0 args), C-1 returns (C=1→0 returns discard) */
      luaK_codeABC(ls->fs, OP_CALL, reg, 1, 1);
      break;
    }
    case TK_ASYNC: {  /* stat -> async function */
      luaX_next(ls);
      if (ls->t.token == TK_FUNCTION) {
          funcstat(ls, line, 1);
      } else {
          luaX_syntaxerror(ls, "expected 'function' after 'async'");
      }
      break;
    }
    case TK_AWAIT: {  /* stat -> await expr */
      /* 将 await expr 编译为 OP_AWAIT 指令，丢弃返回值（纯语法级） */
      luaX_next(ls);  /* 跳过 await */
      {
        expdesc v;
        expr(ls, &v);  /* 解析 await 的参数表达式 */
        FuncState *fs = ls->fs;
        int reg = fs->freereg;
        luaK_exp2reg(fs, &v, reg);  /* Promise 存入 reg */
        luaK_codeABC(fs, OP_AWAIT, reg, reg, 0);  /* 结果覆盖同一寄存器（丢弃） */
        fs->freereg = reg + 1;
        luaK_fixline(fs, line);
      }
      break;
    }
    case TK_FUNCTION: {  /* stat -> funcstat */
      funcstat(ls, line, 0);
      break;
    }
    case TK_CONCEPT: {  /* stat -> conceptstat */
      conceptstat(ls, line);
      break;
    }
    case TK_STRUCT: {  /* stat -> structstat */
      structstat(ls, line, 0);
      break;
    }
    case TK_SUPERSTRUCT: {  /* stat -> superstructstat */
      superstructstat(ls, line);
      break;
    }
    case TK_ENUM: {  /* stat -> enumstat */
      enumstat(ls, line, 0);
      break;
    }
    case TK_EXPORT: {
      luaX_next(ls);
      if (testnext(ls, TK_FUNCTION)) {
        localfunc(ls, 1, 0);
      }
      else if (testnext(ls, TK_LOCAL)) {
        localstat(ls, 1);
      }
      else if (ls->t.token == TK_STRUCT) {
        structstat(ls, line, 1);
      }
      else if (ls->t.token == TK_ENUM) {
        enumstat(ls, line, 1);
      }
      else if (testnext(ls, TK_CONST)) {
        if (testnext(ls, TK_FUNCTION))
          luaK_semerror(ls, "function cannot be declared as const");
        else
          localstat(ls, 1);
      }
      else {
        SoftKWID skw = softkw_check(ls, SOFTKW_CTX_STMT_BEGIN);
        if (skw == SKW_CLASS) {
          classstat(ls, line, 0, 1, -1);
        }
        else if (skw == SKW_ABSTRACT) {
          luaX_next(ls);
          if (softkw_check(ls, SOFTKW_CTX_STMT_BEGIN) == SKW_CLASS)
             classstat(ls, line, CLASS_FLAG_ABSTRACT, 1, -1);
          else
             luaX_syntaxerror(ls, "'abstract' export must be followed by 'class'");
        }
        else if (skw == SKW_FINAL) {
          luaX_next(ls);
          if (softkw_check(ls, SOFTKW_CTX_STMT_BEGIN) == SKW_CLASS)
             classstat(ls, line, CLASS_FLAG_FINAL, 1, -1);
          else
             luaX_syntaxerror(ls, "'final' export must be followed by 'class'");
        }
        else if (skw == SKW_SEALED) {
          luaX_next(ls);
          if (softkw_check(ls, SOFTKW_CTX_STMT_BEGIN) == SKW_CLASS)
             classstat(ls, line, CLASS_FLAG_SEALED, 1, -1);
          else
             luaX_syntaxerror(ls, "'sealed' export must be followed by 'class'");
        }
        else if (skw == SKW_SINGLETON) {
          luaX_next(ls);
          if (softkw_check(ls, SOFTKW_CTX_STMT_BEGIN) == SKW_CLASS)
             classstat(ls, line, CLASS_FLAG_SINGLETON, 1, -1);
          else
             luaX_syntaxerror(ls, "'singleton' export must be followed by 'class'");
        }
        else if (skw == SKW_INTERFACE) {
          /* interface export */
          interfacestat(ls, line, 1);
        }
        else if (skw == SKW_TRAIT) {
          /* trait export */
          traitstat(ls, line, 1);
        }
        else if (ls->t.token == TK_NAME) {
          localstat(ls, 1);
        }
        else {
          luaX_syntaxerror(ls, "unexpected token after export");
        }
      }
      break;
    }
    case TK_COMMAND: {  /* stat -> commandstat */
      commandstat(ls, line);
      break;
    }
    case TK_KEYWORD: {  /* stat -> keywordstat */
      keywordstat(ls, line);
      break;
    }
    case TK_OPERATOR: {  /* stat -> operatorstat */
      operatorstat(ls, line);
      break;
    }
    case TK_LOCAL: {  /* stat -> localstat */
      luaX_next(ls);  /* skip LOCAL */
      if (testnext(ls, TK_FUNCTION))  /* local function? */
        localfunc(ls, 0, 0);
      else if (ls->t.token == TK_ASYNC) {
          luaX_next(ls);
          checknext(ls, TK_FUNCTION);
          localfunc(ls, 0, 1);
      }
      else if (testnext(ls, TK_TAKE))  /* local take {...} = expr 解构? */
        takestat_full(ls);
      else
        localstat(ls, 0);
      break;
    }
    case TK_CONST: {  /* stat -> conststat */
      luaX_next(ls);  /* skip CONST */
      if (testnext(ls, TK_FUNCTION))  /* const function? */
        luaK_semerror(ls, "function cannot be declared as const");
      else
        localstat(ls, 0);
      break;
    }
    case TK_GLOBAL: {  /* stat -> globalstatfunc */
      globalstatfunc(ls, line);
      break;
    }
    case TK_DBCOLON: {  /* stat -> label */
      luaX_next(ls);  /* skip double colon */
      if (ls->t.token == TK_CONTINUE) {
        TString *name = luaS_newliteral(ls->L, "continue");
        luaX_next(ls);
        labelstat(ls, name, line);
      } else {
        labelstat(ls, str_checkname(ls), line);
      }
      break;
    }
    case TK_RETURN: {  /* stat -> retstat */
      luaX_next(ls);  /* skip RETURN */
      retstat(ls);
      break;
    }
    case TK_CONTINUE:
    case TK_BREAK: {  /* stat -> breakstat */
      breakstat(ls);
      if(!block_follow(ls,1)){
          luaX_syntaxerror(ls,"break or continue is unreachable statement");
      }
      break;
    }
    case TK_GOTO: {  /* stat -> 'goto' NAME */
      luaX_next(ls);  /* skip 'goto' */
      gotostat(ls);
      break;
    }
    case TK_NAMESPACE: {
      namespacestat(ls, line);
      break;
    }
    case TK_USING: {
      usingstat(ls);
      break;
    }
    case TK_TYPE_INT:
    case TK_TYPE_FLOAT:
    case TK_DOUBLE:
    case TK_BOOL:
    case TK_VOID:
    case TK_CHAR:
    case TK_LONG: {
      if (luaX_lookahead(ls) == TK_NAME || is_type_token(luaX_lookahead(ls))) {
         declaration_stat(ls, line);
      } else {
         exprstat(ls);
      }
      break;
    }
    case TK_NAME: {
      /* 使用软关键字系统检查语句开头的软关键字 */
      SoftKWID skw = softkw_check(ls, SOFTKW_CTX_STMT_BEGIN);
      if (skw == SKW_MATCH) {
        matchstat(ls, line);
        break;
      }
      else if (skw == SKW_CLASS) {
        /* class 作为软关键字，触发类定义解析 */
        classstat(ls, line, 0, 0, -1);  /* 无修饰符 */
        break;
      }
      else if (skw == SKW_INTERFACE) {
        /* interface 作为软关键字，触发接口定义解析 */
        interfacestat(ls, line, 0);
        break;
      }
      else if (skw == SKW_TRAIT) {
        /* trait 作为软关键字，触发trait定义解析 */
        traitstat(ls, line, 0);
        break;
      }
      else if (skw == SKW_ABSTRACT) {
        /* abstract class 语法 */
        luaX_next(ls);  /* 跳过 'abstract' */
        SoftKWID next_skw = softkw_check(ls, SOFTKW_CTX_STMT_BEGIN);
        if (next_skw == SKW_CLASS) {
          classstat(ls, line, CLASS_FLAG_ABSTRACT, 0, -1);
        } else {
          luaX_syntaxerror(ls, "'class' expected after 'abstract'");
        }
        break;
      }
      else if (skw == SKW_FINAL) {
        /* final class 语法 */
        luaX_next(ls);  /* 跳过 'final' */
        SoftKWID next_skw = softkw_check(ls, SOFTKW_CTX_STMT_BEGIN);
        if (next_skw == SKW_CLASS) {
          classstat(ls, line, CLASS_FLAG_FINAL, 0, -1);
        } else {
          luaX_syntaxerror(ls, "'class' expected after 'final'");
        }
        break;
      }
      else if (skw == SKW_SEALED) {
        /* sealed class 语法 */
        luaX_next(ls);  /* 跳过 'sealed' */
        SoftKWID next_skw = softkw_check(ls, SOFTKW_CTX_STMT_BEGIN);
        if (next_skw == SKW_CLASS) {
          classstat(ls, line, CLASS_FLAG_SEALED, 0, -1);
        } else {
          luaX_syntaxerror(ls, "'class' expected after 'sealed'");
        }
        break;
      }
      else if (skw == SKW_SINGLETON) {
        /* singleton class 语法 */
        luaX_next(ls);  /* 跳过 'singleton' */
        SoftKWID next_skw = softkw_check(ls, SOFTKW_CTX_STMT_BEGIN);
        if (next_skw == SKW_CLASS) {
          classstat(ls, line, CLASS_FLAG_SINGLETON, 0, -1);
        } else {
          luaX_syntaxerror(ls, "'class' expected after 'singleton'");
        }
        break;
      }

      /* Check for C++ declaration: Type Name
         but NOT if it looks like an infix function call: Name Name <expr_start> */
      if (luaX_lookahead(ls) == TK_NAME) {
         int receiver_line = ls->t.linenumber;        /* name1 的行号 */
         int method_line = ls->lookahead.linenumber;   /* name2 的行号 */
         int la2 = luaX_lookahead2(ls);
         /* 中缀调用要求 receiver 和 method 在同一行，且 arg 是表达式起始 */
         if (is_infix_expr_start(la2) && receiver_line == method_line) {
            /* 可能是中缀调用: name1 name2 expr，交给 exprstat 处理 */
            exprstat(ls);
         } else {
            declaration_stat(ls, line);
         }
         break;
      }

  #if defined(LUA_COMPAT_GLOBAL)
      /* compatibility code to parse global keyword when "global"
         is not reserved */
      if (ls->t.seminfo.ts == ls->glbn) {  /* current = "global"? */
        int lk = luaX_lookahead(ls);
        if (lk == '<' || lk == TK_NAME || lk == '*' || lk == TK_FUNCTION) {
          /* 'global <attrib>' or 'global name' or 'global *' or
             'global function' */
          globalstatfunc(ls, line);
          break;
        }
      }  /* else... */
  #endif
      /* 不是软关键字，按普通语句处理 */
      exprstat(ls);
      break;
    }
    default: {  /* stat -> func | assignment */
      SoftKWID skw = softkw_check(ls, SOFTKW_CTX_STMT_BEGIN);
      if (skw == SKW_MATCH) {
        matchstat(ls, line);
        break;
      }
      /* 前缀自增 ++x（与 AST 解析器对齐） */
      if (ls->t.token == TK_PLUSPLUS) {
        luaX_next(ls);  /* 跳过 ++ */
        struct LHS_assign pv;
        suffixedexp(ls, &pv.v);
        prefixincrementstat(ls, &pv.v);
        break;
      }
      exprstat(ls);
      break;
    }
  }
  lua_assert(ls->fs->f->maxstacksize >= ls->fs->freereg &&
             ls->fs->freereg >= luaY_nvarstack(ls->fs));
  ls->fs->freereg = luaY_nvarstack(ls->fs);  /* free registers */
  leavelevel(ls);
}

/* }=========================================================== */

/* }=========================================================== */


/*
** compiles the main function, which is a regular vararg function with an
** upvalue named LUA_ENV
*/
static void mainfunc (LexState *ls, FuncState *fs) {
  BlockCnt bl;
  Upvaldesc *env;
  open_func(ls, fs, &bl);
  LUA_LOGD("[PARSER] mainfunc START, source='%s'", getstr(fs->f->source));
  setvararg(fs, 0);  /* main function is always declared vararg */
  env = allocupvalue(fs);  /* ...set environment upvalue */
  env->instack = 1;
  env->idx = 0;
  env->kind = VDKREG;
  env->name = ls->envn;
  luaC_objbarrier(ls->L, fs->f, env->name);
  luaX_next(ls);  /* read first token */
  LUA_LOGD("[PARSER] first token=%d", ls->t.token);
  if(testtoken(ls,'{'))
    retstat(ls);
  else {
    statlist(ls);  /* parse main body */
  }
  check(ls, TK_EOS);
  close_func(ls);
  LUA_LOGD("[PARSER] mainfunc END, sizecode=%d sizep=%d nups=%d",
          fs->f->sizecode, fs->f->sizep, fs->f->sizeupvalues);
}


LClosure *luaY_parser (lua_State *L, ZIO *z, Mbuffer *buff,
                       Dyndata *dyd, const char *name, int firstchar) {
#if  0 /* 使用 AST 解析器 (last_parse.c)：正确处理 astparser() 语法 */
  /* AST-based parser: parse into LAST AST, then codegen to Proto */
  lparser_vmp_hook_point();
  LUA_LOGD("[ASTPARSER] luaY_parser START(ast), name='%s'", name);
  LClosure *cl = luaF_newLclosure(L, 1);
  setclLvalue2s(L, L->top.p, cl);
  luaD_inctop(L);
  dyd->actvar.n = dyd->gt.n = dyd->label.n = 0;
  int old_gc_state = lua_gc(L, LUA_GCISRUNNING, 0);
  lua_gc(L, LUA_GCSTOP, 0);
  AstChunk *chunk = luaY_parse_ast(L, z, buff, dyd, name, firstchar);
  chunk->main_func->is_vararg = 1;
  Proto *p = luaY_codegen_chunk(L, chunk, dyd);
  cl->p = p;
  luaC_objbarrier(L, cl, p);
  ast_pool_free(chunk->pool);
  luaM_free(L, chunk->pool);
  if (old_gc_state) lua_gc(L, LUA_GCRESTART, 0);
  lua_assert(dyd->actvar.n == 0 && dyd->gt.n == 0 && dyd->label.n == 0);
  lua_assert(cl->nupvalues == cl->p->sizeupvalues);
  return cl;
#else
  /* 旧版解析器：直接解析+codegen，不使用AST中间表示 */
  LexState lexstate;
  FuncState funcstate;
  lparser_vmp_hook_point();
  LUA_LOGD("[PARSER] luaY_parser START, name='%s', firstchar=0x%x", name, firstchar);
  LClosure *cl = luaF_newLclosure(L, 1);
  setclLvalue2s(L, L->top.p, cl);
  luaD_inctop(L);
  lexstate.h = luaH_new(L);
  sethvalue2s(L, L->top.p, lexstate.h);
  luaD_inctop(L);
  lexstate.named_types = luaH_new(L);
  sethvalue2s(L, L->top.p, lexstate.named_types);
  luaD_inctop(L);
  lexstate.declared_globals = luaH_new(L);
  sethvalue2s(L, L->top.p, lexstate.declared_globals);
  luaD_inctop(L);
  lexstate.all_type_hints = NULL;
  lexstate.defines = NULL;
  funcstate.f = cl->p = luaF_newproto(L);
  luaC_objbarrier(L, cl, cl->p);
  funcstate.f->source = luaS_new(L, name);
  luaC_objbarrier(L, funcstate.f, funcstate.f->source);
  lexstate.buff = buff;
  lexstate.dyd = dyd;
  lexstate.curpos = 0;
  lexstate.tokpos = 0;
  lexstate.loop_depth = 0;
  dyd->actvar.n = dyd->gt.n = dyd->label.n = 0;
  luaX_setinput(L, &lexstate, z, funcstate.f->source, firstchar);
  mainfunc(&lexstate, &funcstate);
  lua_assert(!funcstate.prev && funcstate.nups == 1 && !lexstate.fs);
  lua_assert(dyd->actvar.n == 0 && dyd->gt.n == 0 && dyd->label.n == 0);
  typehint_free(&lexstate);
  if (lexstate.defines) {
    L->top.p--;
  }
  L->top.p--;
  L->top.p--;
  L->top.p--;
  LUA_LOGD("[PARSER] luaY_parser DONE, name='%s', proto=%p sizecode=%d sizep=%d", name, (void*)cl->p, cl->p->sizecode, cl->p->sizep);
  return cl;
#endif
}






/* Decorator implementation */
static int parse_decorators(LexState *ls) {
  FuncState *fs = ls->fs;
  int num = 0;
  while (ls->t.token == '@') {
      luaX_next(ls); /* skip '@' */
      expdesc dec_exp;
      expr(ls, &dec_exp);
      luaK_exp2nextreg(fs, &dec_exp);
      num++;
  }
  return num;
}
