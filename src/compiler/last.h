/*
** $Id: last.h $
** Abstract Syntax Tree (AST) - Base Framework
** See Copyright Notice in lua.h
*/

#ifndef last_h
#define last_h

#include "llimits.h"
#include "lobject.h"
#include "llex.h"
#include "lparser.h"


/* 前置声明 */
typedef struct AstExpr AstExpr;
typedef struct AstStmt AstStmt;
typedef struct AstFunc AstFunc;
typedef struct AstChunk AstChunk;
typedef struct AstNode AstNode;
typedef struct AstPool AstPool;
typedef struct AstPoolChunk AstPoolChunk;


/**
 * @brief AST节点类型标签枚举
 */
typedef enum {
  AST_NODE = 0,
  AST_EXPR,
  AST_STMT,
  AST_FUNC,
  AST_CHUNK
} AstNodeKind;


/**
 * @brief AST内存池的分块结构
 *
 * 使用chunked bump allocator设计，每个块是一段连续内存，
 * 分配时从当前块的剩余空间中切分，块之间通过链表连接。
 * 块不会被realloc，保证已分配指针的稳定性。
 */
struct AstPoolChunk {
  AstPoolChunk *next;  /**< 链表中的下一个块 */
  size_t used;         /**< 已使用字节数 */
  size_t cap;          /**< 块总容量（data部分） */
  char data[];         /**< 柔性数组成员，实际存储数据 */
};


/**
 * @brief AST节点基类
 *
 * 所有具体AST节点（Expr/Stmt/Func/Chunk）都以此结构作为首成员，
 * 实现类似继承的效果。next字段用于内存池的节点链表。
 */
struct AstNode {
  AstNodeKind type;  /**< 节点类型标签 */
  int line;          /**< 源代码行号 */
  AstNode *next;     /**< 内存池链表指针 */
};


/**
 * @brief AST Arena内存池
 *
 * 使用分块bump分配器，分配速度快，释放时整体释放所有块。
 * 所有分配的内存自动清零，且按8字节对齐。
 */
struct AstPool {
  lua_State *L;         /**< Lua状态机指针，用于内存分配 */
  AstPoolChunk *chunks; /**< 内存块链表头 */
};


/* 默认块大小：8192字节 */
#define AST_POOL_CHUNK_SIZE  8192

/* 8字节对齐掩码 */
#define AST_POOL_ALIGN       8
#define AST_POOL_ALIGN_MASK  (cast(size_t, AST_POOL_ALIGN - 1))


/**
 * @brief 初始化AST内存池
 *
 * @param L Lua状态机
 * @param p 要初始化的内存池指针
 */
LUAI_FUNC void ast_pool_init(lua_State *L, AstPool *p);


/**
 * @brief 释放AST内存池中所有内存
 *
 * @param p 要释放的内存池指针
 */
LUAI_FUNC void ast_pool_free(AstPool *p);


/**
 * @brief 从内存池分配内存
 *
 * 分配的内存按8字节对齐，且自动清零。
 * 大于默认块大小的对象会单独分配一个块。
 *
 * @param p 内存池指针
 * @param bytes 需要分配的字节数
 * @return 分配到的内存指针（已清零）
 */
LUAI_FUNC void *ast_pool_alloc(AstPool *p, size_t bytes);


/**
 * @brief 创建AST节点的宏
 *
 * @param p AstPool指针
 * @param type C类型名（如AstExpr）
 * @param kind AstNodeKind枚举值
 * @param line 源代码行号
 * @return 初始化好的节点指针
 */
#define ast_new_node(p, type, kind, line)  \
  (cast(type *, ast_node_new((p), sizeof(type), (kind), (line))))


/**
 * @brief 内部函数：分配并初始化一个AST节点
 *
 * @param p 内存池指针
 * @param size 节点大小
 * @param kind 节点类型
 * @param line 行号
 * @return 节点指针
 */
LUAI_FUNC AstNode *ast_node_new(AstPool *p, size_t size, AstNodeKind kind, int line);


/* ---------- 二元运算符枚举（与lcode.h BinOpr对应） ---------- */
typedef enum {
  AST_BIN_ADD,
  AST_BIN_SUB,
  AST_BIN_MUL,
  AST_BIN_DIV,
  AST_BIN_IDIV,
  AST_BIN_MOD,
  AST_BIN_POW,
  AST_BIN_BAND,
  AST_BIN_BOR,
  AST_BIN_BXOR,
  AST_BIN_SHL,
  AST_BIN_SHR,
  AST_BIN_CONCAT,
  AST_BIN_PIPE,
  AST_BIN_REVPIPE,
  AST_BIN_SAFEPIPE,
  AST_BIN_EQ,
  AST_BIN_NE,
  AST_BIN_LT,
  AST_BIN_LE,
  AST_BIN_GT,
  AST_BIN_GE,
  AST_BIN_SPACESHIP,
  AST_BIN_IS,
  AST_BIN_IN,
  AST_BIN_AND,
  AST_BIN_OR,
  AST_BIN_NULLCOAL,
  AST_BIN_CASE,
  AST_BIN_INFIX,
  AST_BIN_MERGE,
  AST_BIN_AS
} AstBinOp;


/* ---------- 一元运算符枚举（与lcode.h UnOpr对应） ---------- */
typedef enum {
  AST_UN_MINUS,
  AST_UN_BNOT,
  AST_UN_NOT,
  AST_UN_LEN,
  AST_UN_AWAIT,
  AST_UN_TEST_Z,     /* [-z expr] 字符串长度为零测试 */
  AST_UN_TEST_N,     /* [-n expr] 字符串长度非零测试 */
  AST_UN_TEST_NIL,   /* [-nil expr] nil 类型测试 */
  AST_UN_TEST_BOOL,  /* [-bool expr] boolean 类型测试 */
  AST_UN_TEST_FUNC   /* [-func expr] function 类型测试 */
} AstUnOp;


/* ---------- 表达式节点类型 ---------- */
typedef enum {
  AST_EXPR_NIL,
  AST_EXPR_TRUE,
  AST_EXPR_FALSE,
  AST_EXPR_INT,
  AST_EXPR_FLT,
  AST_EXPR_STRING,
  AST_EXPR_INTERPSTRING,
  AST_EXPR_REGEX,
  AST_EXPR_VARARG,
  AST_EXPR_IDENT,
  AST_EXPR_BINOP,
  AST_EXPR_UNOP,
  AST_EXPR_CALL,
  AST_EXPR_METHOD_CALL,
  AST_EXPR_INDEX,
  AST_EXPR_TABLE_CTOR,
  AST_EXPR_MAP_CTOR,
  AST_EXPR_FUNC_EXPR,
  AST_EXPR_ARROW_FUNC,
  AST_EXPR_AWAIT,
  AST_EXPR_PIPE,
  AST_EXPR_REVPIPE,
  AST_EXPR_SAFEPIPE,
  AST_EXPR_NULLCOAL,
  AST_EXPR_SPACESHIP,
  AST_EXPR_IS,
  AST_EXPR_IN,
  AST_EXPR_MERGE,
  AST_EXPR_CONDEXPR,
  AST_EXPR_PAREN,
  AST_EXPR_OPTCHAIN,
  AST_EXPR_RANGE,
  AST_EXPR_SUPER,
  AST_EXPR_SWITCH_EXPR,
  AST_EXPR_SELECT_CASE,
  AST_EXPR_METHOD_REF,
  AST_EXPR_NEW,
  AST_EXPR_MATCH,
  AST_EXPR_TEST_TYPE,  /* [-type expr "typename"] 类型测试 */
  AST_EXPR_EMBED,      /* $embed "filename" 嵌入文件内容 */
  AST_EXPR_OBJECT,     /* $object { ... } 创建对象表 */
  AST_EXPR_SLICE,      /* 切片语法: t[start:end:step] */
  AST_EXPR_DICT_COMP,  /* 字典推导式: {for k,v in expr do/yield k_expr, v_expr if cond} */
  AST_EXPR_LIST_COMP,  /* 列表推导式: [for x in expr do/yield expr if cond] */
  AST_EXPR_SPREAD,     /* 展开运算符: ...expr 转换为 table.unpack(expr) */
  AST_EXPR_WALRUS,     /* 海象操作符: (name := expr) 赋值并返回值 */
  AST_EXPR_ASTPARSER   /* astparser 编译期代码块：预编译的 Proto + AstChunk */
} AstExprKind;


/* ---------- 表构造器条目类型 ---------- */
typedef enum {
  AST_TENTRY_POS,
  AST_TENTRY_KEY
} AstTableEntryKind;


/* ---------- 变量引用类型（parser阶段预解析填充） ---------- */
typedef enum {
  AST_VAR_LOCAL,
  AST_VAR_UPVAL,
  AST_VAR_GLOBAL,
  AST_VAR_CONST
} AstVarKind;


/* 前置声明 */
typedef struct AstCaseArm AstCaseArm;
typedef struct AstTableEntry AstTableEntry;
typedef struct AstMapEntry AstMapEntry;
typedef struct AstEnumEntry AstEnumEntry;
typedef struct AstMatchPat AstMatchPat;
typedef struct AstMatchArm AstMatchArm;
typedef struct AstKVPair AstKVPair;


/* ---------- 表构造器条目 ---------- */
struct AstTableEntry {
  AstTableEntryKind kind;
  AstExpr *key;
  AstExpr *value;
};


/* ---------- map构造器条目 ---------- */
struct AstMapEntry {
  AstExpr *key;
  AstExpr *value;
};


/* ---------- 键值对（用于 struct/superstruct 字段） ---------- */
struct AstKVPair {
  AstExpr *key;
  AstExpr *value;
};


/* ---------- 枚举成员条目 ---------- */
struct AstEnumEntry {
  TString *name;         /**< 枚举成员名 */
  AstExpr *value_expr;   /**< 枚举值表达式（NULL表示未赋值，自动递增） */
};


/* ---------- switch表达式分支 ---------- */
struct AstCaseArm {
  AstExpr **patterns;   /**< case匹配表达式数组（支持多值模式: case 1, 2, 3 ->） */
  int npatterns;        /**< 匹配表达式数量 */
  AstExpr *body;        /**< case体表达式 */
};


/* ---------- 表达式节点 ---------- */
struct AstExpr {
  AstNode node;
  AstExprKind kind;
  int paren;
  unsigned int nodiscard:1;
  unsigned int is_pipe_self:1;
  union {
    lua_Integer ival;
    lua_Number nval;
    TString *strval;
    struct { AstBinOp op; AstExpr *lhs; AstExpr *rhs; } binop;
    struct { AstUnOp op; AstExpr *operand; } unop;
    struct { AstExpr *callee; AstExpr **args; int nargs; } call;
    struct { AstExpr *recv; TString *method; AstExpr **args; int nargs; } mcall;
    struct { AstExpr *table; AstExpr *key; int keystr; int is_opt; } index;
    struct { AstTableEntry *entries; int nentries; int narr; int nrec; } table;
    struct { AstMapEntry *entries; int nentries; } map;
    struct { AstFunc *func; } func;
    struct { AstExpr *e1; AstExpr *e2; AstExpr *e3; } condexpr;
    struct { AstExpr *start; AstExpr *end; } range;
    struct { AstExpr *cond; AstCaseArm *arms; int narms; AstExpr *def; } switchx;
    struct { AstExpr *recv; AstExpr *placeholder; } pipe;
    struct { AstExpr *obj; TString *method; } super;
    struct { AstExpr *recv; TString *method; } method_ref;
    struct { AstExpr *class_expr; AstExpr **args; int nargs; } newexpr;
    struct { struct AstStmt *stmt; } match;  /* AST_EXPR_MATCH */
    struct { AstExpr *operand; TString *type_name; } test_type;  /* AST_EXPR_TEST_TYPE */
    struct { int var_kind; int idx; } var;
    struct { AstExpr *expr; } paren;
    struct { TString *filename; } embed;     /* AST_EXPR_EMBED：嵌入文件的完整内容 */
    struct { AstExpr *ctor; } object;       /* AST_EXPR_OBJECT：表构造器AST节点 */
    struct { AstExpr *table; AstExpr *start; AstExpr *end; AstExpr *step; } slice;  /* AST_EXPR_SLICE */
    struct { AstExpr *expr; } spread;      /* AST_EXPR_SPREAD：展开运算符 ...expr */
    struct { TString *name; AstExpr *expr; } walrus;  /* AST_EXPR_WALRUS：海象操作符 (name := expr) */
    struct { struct Proto *proto; struct AstChunk *chunk; } astparser;  /* AST_EXPR_ASTPARSER：预编译的 Proto */
  } u;
};


/* ---------- 表达式构造函数声明 ---------- */
LUAI_FUNC AstExpr *ast_new_expr_nil(AstPool *p, int line);
LUAI_FUNC AstExpr *ast_new_expr_bool(AstPool *p, int is_true, int line);
LUAI_FUNC AstExpr *ast_new_expr_int(AstPool *p, lua_Integer v, int line);
LUAI_FUNC AstExpr *ast_new_expr_flt(AstPool *p, lua_Number v, int line);
LUAI_FUNC AstExpr *ast_new_expr_str(AstPool *p, TString *s, AstExprKind kind, int line);
LUAI_FUNC AstExpr *ast_new_expr_vararg(AstPool *p, int line);
LUAI_FUNC AstExpr *ast_new_expr_ident(AstPool *p, TString *name, int line);
LUAI_FUNC AstExpr *ast_new_expr_binop(AstPool *p, AstBinOp op, AstExpr *lhs, AstExpr *rhs, int line);
LUAI_FUNC AstExpr *ast_new_expr_unop(AstPool *p, AstUnOp op, AstExpr *operand, int line);
LUAI_FUNC AstExpr *ast_new_expr_call(AstPool *p, AstExpr *callee, AstExpr **args, int nargs, int line);
LUAI_FUNC AstExpr *ast_new_expr_methodcall(AstPool *p, AstExpr *recv, TString *method, AstExpr **args, int nargs, int line);
LUAI_FUNC AstExpr *ast_new_expr_index(AstPool *p, AstExpr *table, AstExpr *key, int is_opt, int line);
LUAI_FUNC AstExpr *ast_new_expr_table(AstPool *p, AstTableEntry *entries, int nentries, int line);
LUAI_FUNC AstExpr *ast_new_expr_map(AstPool *p, AstMapEntry *entries, int nentries, int line);
LUAI_FUNC AstExpr *ast_new_expr_func(AstPool *p, AstFunc *func, int is_arrow, int line);
LUAI_FUNC AstExpr *ast_new_expr_condexpr(AstPool *p, AstExpr *cond, AstExpr *thn, AstExpr *els, int line);
LUAI_FUNC AstExpr *ast_new_expr_paren(AstPool *p, AstExpr *e, int line);
LUAI_FUNC AstExpr *ast_new_expr_range(AstPool *p, AstExpr *start, AstExpr *end, int line);
LUAI_FUNC AstExpr *ast_new_expr_pipe(AstPool *p, AstBinOp optype, AstExpr *e1, AstExpr *e2, int line);
LUAI_FUNC AstExpr *ast_new_expr_methodref(AstPool *p, AstExpr *recv, TString *method, int line);
LUAI_FUNC AstExpr *ast_new_expr_test_type(AstPool *p, AstExpr *operand, TString *type_name, int line);
LUAI_FUNC AstExpr *ast_new_expr_embed(AstPool *p, TString *filename, int line);
LUAI_FUNC AstExpr *ast_new_expr_object(AstPool *p, AstExpr *table, int line);
LUAI_FUNC AstExpr *ast_new_expr_slice(AstPool *p, AstExpr *table, AstExpr *start, AstExpr *end, AstExpr *step, int line);
LUAI_FUNC AstExpr *ast_new_expr_spread(AstPool *p, AstExpr *expr, int line);


/* ---------- 语句节点类型 ---------- */
typedef enum {
  AST_STMT_BLOCK,
  AST_STMT_LOCAL,
  AST_STMT_ASSIGN,
  AST_STMT_EXPR,
  AST_STMT_IF,
  AST_STMT_WHILE,
  AST_STMT_REPEAT,
  AST_STMT_FOR_NUM,
  AST_STMT_FOR_GEN,
  AST_STMT_DO,
  AST_STMT_RETURN,
  AST_STMT_BREAK,
  AST_STMT_CONTINUE,
  AST_STMT_GOTO,
  AST_STMT_LABEL,
  AST_STMT_SWITCH,
  AST_STMT_LOCAL_FUNC,
  AST_STMT_GLOBAL,
  AST_STMT_TRY,
  AST_STMT_CATCH,
  AST_STMT_FINALLY,
  AST_STMT_THROW,
  AST_STMT_DEFER,
  AST_STMT_USING,
  AST_STMT_NAMESPACE,
  AST_STMT_STRUCT,
  AST_STMT_SUPERSTRUCT,
  AST_STMT_ENUM,
  AST_STMT_CLASS,
  AST_STMT_TRAIT,
  AST_STMT_INTERFACE,
  AST_STMT_MATCH,
  AST_STMT_WITH,
  AST_STMT_ASM,
  AST_STMT_CONCEPT,
  AST_STMT_EXPORT,  /* 保留：export 在 parse 阶段展开为具体语句，不会生成此节点 */
  AST_STMT_WHILE_LET,
  AST_STMT_COMPOUND_ASSIGN,
  AST_STMT_INCR_DECR,
  AST_STMT_GUARD,
  AST_STMT_COMMAND,
  AST_STMT_KEYWORD,
  AST_STMT_OPERATOR,
  AST_STMT_EMPTY,
  AST_STMT_TAKE,
  AST_STMT_CONSTEXPR
} AstStmtKind;


/* ---------- 局部变量属性 ---------- */
typedef enum {
  AST_ATTR_NONE  = 0,
  AST_ATTR_CONST = 1,
  AST_ATTR_CLOSE = 2
} AstLocalAttr;


/* ---------- 赋值目标类型 ---------- */
typedef enum {
  AST_TGT_VAR,
  AST_TGT_INDEX
} AstAssignTargetKind;


/* ---------- 自增/自减类型 ---------- */
typedef enum {
  AST_INCR_PRE_INC,
  AST_INCR_PRE_DEC,
  AST_INCR_POST_INC,
  AST_INCR_POST_DEC
} AstIncrKind;


/* 前置声明 */
typedef struct AstBlock AstBlock;
typedef struct AstIfArm AstIfArm;
typedef struct AstAssignTarget AstAssignTarget;
typedef struct AstSwitchCase AstSwitchCase;


/* ---------- 匹配模式类型 ---------- */
typedef enum {
  AST_PAT_WILDCARD,    /* _ 通配符 */
  AST_PAT_LITERAL,     /* 字面量表达式 */
  AST_PAT_VARIABLE,    /* 变量绑定 */
  AST_PAT_RANGE,       /* 范围 low..high */
  AST_PAT_TYPE,        /* is TypeName */
  AST_PAT_OR,          /* 多值模式 pat1, pat2 (逗号分隔) */
  AST_PAT_TABLE,       /* 表解构 { field1, ... } */
} AstMatchPatKind;


/* ---------- 匹配模式节点 ---------- */
struct AstMatchPat {
  AstMatchPatKind kind;
  int line;
  union {
    AstExpr *literal;        /* AST_PAT_LITERAL */
    TString *var_name;       /* AST_PAT_VARIABLE */
    struct { AstExpr *low; AstExpr *high; } range; /* AST_PAT_RANGE */
    TString *type_name;      /* AST_PAT_TYPE */
    struct { AstMatchPat **pats; int npat; } or_pat; /* AST_PAT_OR */
    struct {
      AstMatchPat **fields;  /* 字段子模式数组 */
      int nfields;
    } table_pat;             /* AST_PAT_TABLE */
  } u;
};


/* ---------- 访问级别 ---------- */
typedef enum {
  AST_ACCESS_DEFAULT = 0,
  AST_ACCESS_PRIVATE = 1,
  AST_ACCESS_PROTECTED = 2,
  AST_ACCESS_PUBLIC = 3,
} AstAccessLevel;

/* ---------- 类成员类型 ---------- */
typedef enum {
  AST_MEMBER_METHOD,      /**< 普通方法 */
  AST_MEMBER_ABSTRACT,    /**< 抽象方法（无函数体） */
  AST_MEMBER_FINAL,       /**< final 方法 */
  AST_MEMBER_PROPERTY,    /**< 属性 */
  AST_MEMBER_GETTER,      /**< getter 属性访问器 */
  AST_MEMBER_SETTER,      /**< setter 属性访问器 */
  AST_MEMBER_NESTED_CLASS, /**< 嵌套类（内部类） */
} AstMemberKind;

/* ---------- 方法签名（用于 interface 方法声明和 trait require） ---------- */
typedef struct AstMethodSig {
  TString *name;     /**< 方法名 */
  int param_count;   /**< 参数个数（含 self） */
  int line;          /**< 定义行号 */
} AstMethodSig;

/* ---------- 类成员 ---------- */
typedef struct AstClassMember {
  AstMemberKind kind;
  AstAccessLevel access;   /**< 访问级别 */
  int is_static;           /**< 是否是静态成员 */
  int is_override;         /**< 是否标记 override */
  TString *name;           /**< 成员名 */
  union {
    AstFunc *method_func;      /**< 方法/抽象方法/final方法的函数体 */
    AstExpr *property_value;   /**< 属性初始值 */
    AstStmt *nested_class;     /**< 嵌套类定义的 AST 语句节点 */
  } u;
  AstExpr **decorators;    /**< 成员装饰器表达式列表（可为 NULL） */
  int ndecorators;         /**< 装饰器个数 */
  int line;
} AstClassMember;

/* ---------- 语句块（支持动态扩容） ---------- */
struct AstBlock {
  AstStmt **items;     /**< 语句数组指针 */
  int count;           /**< 当前语句数量 */
  int capacity;        /**< 数组容量 */
  TString **exports;   /**< 导出的名称数组（export 语句记录） */
  int nexports;        /**< 当前导出数量 */
  int exports_cap;     /**< 导出数组容量 */
};


/* ---------- 匹配臂 ---------- */
typedef struct AstMatchArm {
  AstMatchPat *pattern;   /* 匹配模式 */
  AstExpr *guard;          /* 守卫条件（NULL表示无守卫） */
  int is_arrow;            /* 1=箭头表达式体, 0=语句块体 */
  AstExpr *body_expr;      /* 箭头表达式体（is_arrow=1时有效） */
  AstBlock body_block;     /* 语句块体（is_arrow=0时有效） */
} AstMatchArm;


/* ---------- if分支臂（cond为NULL表示else分支） ---------- */
struct AstIfArm {
  AstExpr *cond;       /**< 条件表达式，else分支为NULL */
  AstBlock body;       /**< 分支体 */
  TString *let_var;    /**< if let 变量名（NULL 表示普通 if 条件） */
};


/* ---------- 赋值目标（变量或索引） ---------- */
struct AstAssignTarget {
  AstAssignTargetKind kind;
  union {
    struct {
      TString *name;   /**< 变量名 */
      int var_kind;    /**< AstVarKind */
      int idx;         /**< 局部变量/upvalue索引 */
    } var;
    struct {
      AstExpr *table;  /**< 表表达式 */
      AstExpr *key;    /**< 键表达式 */
    } index;
  } as;
};


/* ---------- switch case分支 ---------- */
struct AstSwitchCase {
  AstExpr **patterns;  /**< case匹配表达式数组（支持多值模式: case 1, 2, 3 ->） */
  int npatterns;       /**< 匹配表达式数量 */
  AstBlock body;       /**< case体 */
  int is_default;      /**< 是否为default分支 */
};


/* ---------- 语句节点 ---------- */
struct AstStmt {
  AstNode node;        /**< 继承基类 */
  AstStmtKind kind;    /**< 语句类型 */
  AstExpr **decorators; /**< 装饰器表达式数组，无装饰器时为NULL */
  int ndecorators;     /**< 装饰器数量 */
  union {
    /* AST_STMT_BLOCK / AST_STMT_DO */
    struct {
      AstBlock block;
    } block;

    /* AST_STMT_LOCAL */
    struct {
      int nnames;
      TString **names;
      int *attrs;      /**< AstLocalAttr数组 */
      int nvalues;
      AstExpr **values;
      TypeHint **type_hints; /**< 类型注解数组，与 names 一一对应，无类型注解则为 NULL */
    } local;

    /* AST_STMT_ASSIGN */
    struct {
      int ntargets;
      AstAssignTarget *targets;
      int nvalues;
      AstExpr **values;
    } assign;

    /* AST_STMT_EXPR */
    struct {
      AstExpr *expr;
    } expr;

    /* AST_STMT_IF */
    struct {
      AstIfArm *arms;
      int narms;
      int has_else;
      AstBlock else_body;
    } ifstmt;

    /* AST_STMT_WHILE / AST_STMT_REPEAT */
    struct {
      AstExpr *cond;   /**< repeat条件在body后 */
      AstBlock body;
      AstBlock else_body;  /**< while...else 的 else 块 */
      int has_else;        /**< 是否有 else 分支 */
    } whilestmt;

    /* AST_STMT_WHILE_LET */
    struct {
      int nnames;           /**< 变量数量 */
      TString **names;      /**< 变量名数组 */
      AstExpr *expr;        /**< 赋值表达式 */
      AstBlock body;        /**< 循环体 */
      AstBlock else_body;   /**< else 块 */
      int has_else;         /**< 是否有 else 分支 */
    } whilelet;

    /* AST_STMT_FOR_NUM */
    struct {
      TString *var;
      AstExpr *start;
      AstExpr *stop;
      AstExpr *step;   /**< NULL表示步长为1 */
      AstBlock body;
      AstBlock else_body;  /**< for...else 的 else 块 */
      int has_else;        /**< 是否有 else 分支 */
    } fornum;

    /* AST_STMT_FOR_GEN */
    struct {
      int nnames;
      TString **names;
      int nexprs;
      AstExpr **exprs;
      AstBlock body;
      AstBlock else_body;  /**< for...else 的 else 块 */
      int has_else;        /**< 是否有 else 分支 */
    } forgen;

    /* AST_STMT_RETURN */
    struct {
      int nvalues;
      AstExpr **values;
    } retstmt;

    /* AST_STMT_BREAK / AST_STMT_CONTINUE */
    struct {
      int level;       /**< break: level=1; continue: level=N */
    } contbrk;

    /* AST_STMT_GOTO / AST_STMT_LABEL */
    struct {
      TString *name;
      int label_id;
      int patch_pc;
    } label;

    /* AST_STMT_SWITCH */
    struct {
      AstExpr *cond;
      AstSwitchCase *cases;
      int ncases;
      AstBlock default_body;
      int has_default;
    } switchstmt;

    /* AST_STMT_LOCAL_FUNC */
    struct {
      TString *name;
      AstFunc *func;
      int local_idx;
    } localfunc;

    /* AST_STMT_GLOBAL */
    struct {
      int nnames;
      TString **names;
      int nvalues;
      AstExpr **values;
      int has_wildcard;  /* global * 通配符 */
    } global;

    /* AST_STMT_COMPOUND_ASSIGN */
    struct {
      AstBinOp op;     /**< AST_BIN_ADD对应+=等 */
      int ntargets;
      AstAssignTarget *targets;
      AstExpr *value;
    } compound;

    /* AST_STMT_INCR_DECR */
    struct {
      AstIncrKind kind;
      AstAssignTarget *target;
    } incr;

    /* AST_STMT_GUARD */
    struct {
      AstExpr *cond;        /* guard 条件表达式（guard let 时为 NULL） */
      TString *let_var;     /* guard let 变量名（普通 guard 时为 NULL） */
      AstExpr *let_value;   /* guard let 值表达式 */
      AstBlock else_block;  /* else 代码块 */
    } guard;

    /* AST_STMT_TRY / AST_STMT_CATCH / AST_STMT_FINALLY */
    struct {
      AstBlock body;
      AstExpr *catch_var;
      AstBlock catch_body;
      AstBlock finally_body;
    } trycatch;

    /* AST_STMT_THROW */
    struct {
      AstExpr *expr;
    } throwstmt;

    /* AST_STMT_DEFER */
    struct {
      AstBlock body;  /* defer 包裹的语句块 */
    } deferstmt;

    /* AST_STMT_USING */
    struct {
      int is_namespace;     /* 1=using namespace, 0=using Name::Member */
      TString *name;        /* 命名空间名或成员名 */
      TString *last_member; /* ::链的最后一个成员名（用于创建局部变量） */
    } usingstmt;

    /* AST_STMT_ENUM */
    struct {
      TString *name;           /**< 枚举名（NULL表示匿名枚举） */
      AstEnumEntry *entries;   /**< 枚举成员数组 */
      int nentries;            /**< 枚举成员数量 */
      int is_enum_class;       /**< 是否为 enum class */
    } enumstmt;

    /* AST_STMT_NAMESPACE / AST_STMT_STRUCT / AST_STMT_SUPERSTRUCT / AST_STMT_TRAIT / AST_STMT_INTERFACE */
    struct {
      TString *name;
      AstBlock body;
      AstKVPair *entries;   /**< struct/superstruct 字段数组 */
      int nentries;          /**< 字段数量 */
      TString **extends_names;  /**< 接口/类继承的父名数组 */
      int nextends;             /**< 父数量 */
      /* trait/interface 专用字段 */
      AstClassMember *methods;   /**< 方法数组（含函数体的默认实现） */
      int nmethods;              /**< 方法数量 */
      AstMethodSig *sigs;        /**< 方法签名数组（interface声明/trait require） */
      int nsigs;                 /**< 签名数量 */
    } nsstruct;

    /* AST_STMT_CLASS */
    /* 完整类定义：修饰符、继承、接口实现、trait混入 */
    struct {
      TString *name;           /**< 类名 */
      TString **extends_names;  /**< 父类名数组（NULL表示无父类，支持多继承） */
      int nextends;             /**< 父类数量 */
      TString **implements;    /**< 实现的接口名数组 */
      int nimplements;         /**< 接口数量 */
      TString **use_traits;    /**< 混入的trait名数组 */
      int nuse_traits;         /**< trait数量 */
      int class_flags;         /**< 类修饰符标志（CLASS_FLAG_ABSTRACT/FINAL/SEALED/SINGLETON） */
      AstBlock body;           /**< 类体（兼容旧格式：如果 members==NULL 则使用 body） */
      AstClassMember *members; /**< 类成员数组（新格式） */
      int nmembers;            /**< 成员数量 */
      TString **generic_params; /**< 泛型类型参数名数组，class<T> 语法，无泛型则为 NULL */
      int ngeneric_params;      /**< 泛型参数数量 */
    } classstmt;

    /* AST_STMT_TAKE */
    struct {
      int nvars;
      TString **varnames;
      AstExpr **defaults;   /**< 默认值表达式数组（与varnames对应，NULL表示无默认值） */
      AstExpr *source;
      int is_array;         /**< 是否为数组解构 [a, b]（0=表解构 {a, b}） */
    } take;

    /* AST_STMT_CONSTEXPR */
    struct {
      TString *directive;  /* 指令名（如 "if"） */
      AstExpr *cond;       /* 条件表达式 */
      AstBlock body;       /* 语句体 */
    } constexpr_stmt;

    /* AST_STMT_MATCH: match 语句 */
    struct {
      AstExpr *control;      /* 控制表达式 */
      AstMatchArm *arms;     /* 匹配臂数组 */
      int narms;             /* 匹配臂数量 */
      int is_expr;           /* 1=表达式模式（match expr {...}）, 0=语句模式 */
    } matchstmt;

    /* AST_STMT_WITH: with(expr) 环境管理 */
    struct {
      AstExpr *target;     /* with 目标表达式 */
      AstBlock body;       /* with 体 */
    } withstmt;

    /* AST_STMT_ASM: asm(...) 内联汇编 */
    struct {
      TString *raw_body;   /* 原始汇编文本（括号内的内容） */
    } asmstmt;
  } u;
};


/* ---------- 语句构造函数声明 ---------- */
/** 创建空语句块 */
LUAI_FUNC AstStmt *ast_new_stmt_block(AstPool *p, int line);

/** 向语句块中添加语句（自动扩容） */
LUAI_FUNC void ast_block_add_stmt(AstPool *p, AstBlock *blk, AstStmt *s);

/** 向语句块中添加导出名称（自动扩容） */
LUAI_FUNC void ast_block_add_export(AstPool *p, AstBlock *blk, TString *name);

/** 创建局部变量声明语句 */
LUAI_FUNC AstStmt *ast_new_stmt_local(AstPool *p, int nnames, TString **names, int nvalues, int line);

/** 创建赋值语句 */
LUAI_FUNC AstStmt *ast_new_stmt_assign(AstPool *p, int ntargets, int nvalues, int line);

/** 创建表达式语句 */
LUAI_FUNC AstStmt *ast_new_stmt_expr(AstPool *p, AstExpr *e, int line);

/** 创建if语句 */
LUAI_FUNC AstStmt *ast_new_stmt_if(AstPool *p, int line);

/** 创建while语句 */
LUAI_FUNC AstStmt *ast_new_stmt_while(AstPool *p, AstExpr *cond, int line);

/** 创建while let语句 */
LUAI_FUNC AstStmt *ast_new_stmt_while_let(AstPool *p, int nnames, TString **names, AstExpr *expr, int line);

/** 创建repeat语句 */
LUAI_FUNC AstStmt *ast_new_stmt_repeat(AstPool *p, int line);

/** 创建数值for语句 */
LUAI_FUNC AstStmt *ast_new_stmt_fornum(AstPool *p, TString *var, AstExpr *start, AstExpr *stop, AstExpr *step, int line);

/** 创建泛型for语句 */
LUAI_FUNC AstStmt *ast_new_stmt_forgen(AstPool *p, int nnames, int nexprs, int line);

/** 创建return语句 */
LUAI_FUNC AstStmt *ast_new_stmt_return(AstPool *p, int nvalues, int line);

/** 创建break语句 */
LUAI_FUNC AstStmt *ast_new_stmt_break(AstPool *p, int level, int line);

/** 创建continue语句 */
LUAI_FUNC AstStmt *ast_new_stmt_continue(AstPool *p, int level, int line);

/** 创建goto语句 */
LUAI_FUNC AstStmt *ast_new_stmt_goto(AstPool *p, TString *name, int line);

/** 创建label语句 */
LUAI_FUNC AstStmt *ast_new_stmt_label(AstPool *p, TString *name, int line);

/** 创建空语句 */
LUAI_FUNC AstStmt *ast_new_stmt_empty(AstPool *p, int line);

/** 创建复合赋值语句（+=, -=等） */
LUAI_FUNC AstStmt *ast_new_stmt_compound(AstPool *p, AstBinOp op, int ntargets, AstExpr *value, int line);

/** 创建自增/自减语句（++, --） */
LUAI_FUNC AstStmt *ast_new_stmt_incr(AstPool *p, AstIncrKind kind, int line);
LUAI_FUNC AstStmt *ast_new_stmt_guard(AstPool *p, AstExpr *cond, TString *let_var, AstExpr *let_value, AstBlock *else_block, int line);

/** 创建 try 语句 */
LUAI_FUNC AstStmt *ast_new_stmt_try(AstPool *p, AstBlock *body, AstExpr *catch_var, AstBlock *catch_body, AstBlock *finally_body, int line);

/** 创建 defer 语句 */
LUAI_FUNC AstStmt *ast_new_stmt_defer(AstPool *p, AstBlock *body, int line);

/** 创建 namespace 语句 */
LUAI_FUNC AstStmt *ast_new_stmt_namespace(AstPool *p, TString *name, AstBlock *body, int line);

/** 创建指定类型的命名空间风格语句 */
LUAI_FUNC AstStmt *ast_new_stmt_typed(AstPool *p, AstStmtKind kind, TString *name, AstBlock *body, int line);

/** 创建带键值对列表的语句（用于 struct/superstruct） */
LUAI_FUNC AstStmt *ast_new_stmt_typed_pairs(AstPool *p, AstStmtKind kind, TString *name, AstKVPair *pairs, int npairs, int line);

/** 创建 enum 语句 */
LUAI_FUNC AstStmt *ast_new_stmt_enum(AstPool *p, TString *name, AstEnumEntry *entries, int nentries, int is_enum_class, int line);

/** 创建 using 语句 */
LUAI_FUNC AstStmt *ast_new_stmt_using(AstPool *p, int is_namespace, TString *name, TString *last_member, int line);

/** 创建 throw 语句 */
LUAI_FUNC AstStmt *ast_new_stmt_throw(AstPool *p, AstExpr *expr, int line);

/** 创建局部函数声明语句 */
LUAI_FUNC AstStmt *ast_new_stmt_localfunc(AstPool *p, TString *name, AstFunc *func, int line);

/** 创建全局变量声明语句 */
LUAI_FUNC AstStmt *ast_new_stmt_global(AstPool *p, int nnames, int nvalues, int line);

/** 创建 take 解构赋值语句 */
LUAI_FUNC AstStmt *ast_new_stmt_take(AstPool *p, int nvars, TString **varnames, AstExpr **defaults, AstExpr *source, int is_array, int line);

/** 创建 constexpr 预处理语句 */
LUAI_FUNC AstStmt *ast_new_stmt_constexpr(AstPool *p, TString *directive, AstExpr *cond, AstBlock *body, int line);

/** 创建 with 语句 */
LUAI_FUNC AstStmt *ast_new_stmt_with(AstPool *p, AstExpr *target, AstBlock *body, int line);

/** 创建 asm 内联汇编语句 */
LUAI_FUNC AstStmt *ast_new_stmt_asm(AstPool *p, TString *raw_body, int line);

/** 创建 new 表达式 */
LUAI_FUNC AstExpr *ast_new_expr_new(AstPool *p, AstExpr *class_expr, AstExpr **args, int nargs, int line);
LUAI_FUNC AstExpr *ast_new_expr_match(AstPool *p, struct AstStmt *stmt, int line);
LUAI_FUNC AstExpr *ast_new_expr_super(AstPool *p, int line);
LUAI_FUNC AstExpr *ast_new_expr_walrus(AstPool *p, TString *name, AstExpr *expr, int line);
LUAI_FUNC AstExpr *ast_new_expr_astparser(AstPool *p, struct Proto *proto, struct AstChunk *chunk, int line);

/** 创建类成员节点 */
LUAI_FUNC AstClassMember *ast_new_class_member(AstPool *p, AstMemberKind kind, AstAccessLevel access,
                                                int is_static, TString *name, int line);

/** 创建if分支臂 */
LUAI_FUNC AstIfArm *ast_new_ifarm(AstPool *p, AstExpr *cond, int line);

/** 创建switch case分支 */
LUAI_FUNC AstSwitchCase *ast_new_switchcase(AstPool *p, AstExpr **patterns, int npatterns, int is_default, int line);


/* ---------- Upvalue来源枚举 ---------- */
typedef enum {
  AST_UPVAL_LOCAL,  /**< 来自父函数的局部变量 */
  AST_UPVAL_UPVAL   /**< 来自父函数的upvalue（传递捕获） */
} AstUpvalSrc;


/* ---------- Upvalue描述符 ---------- */
typedef struct AstUpvalueDesc {
  AstUpvalSrc src;   /**< upvalue来源类型 */
  int idx;           /**< 父函数局部变量索引或父upvalue索引 */
  TString *name;     /**< upvalue名称，用于调试信息 */
} AstUpvalueDesc;


/* ---------- 函数参数 ---------- */
typedef struct AstFuncParam {
  TString *name;          /**< 参数名 */
  AstExpr *default_value; /**< 默认值表达式，无默认值则为NULL */
  int attr;               /**< 参数属性：AST_ATTR_NONE/AST_ATTR_CONST/AST_ATTR_CLOSE */
  TypeHint *type_hint;    /**< 类型注解（TypeHint 指针），无类型注解则为 NULL */
} AstFuncParam;


/* 动态数组初始容量 */
#define AST_FUNC_UPVAL_INIT_CAP  4
#define AST_FUNC_CHILD_INIT_CAP  4
#define AST_CHUNK_FUNCS_INIT_CAP 4


/* ---------- 函数定义节点 ---------- */
struct AstFunc {
  AstNode node;               /**< 继承基类 */
  int func_idx;               /**< 唯一函数ID，主chunk为0，子函数按顺序编号 */
  int parent_idx;             /**< 父函数func_idx，主chunk为-1 */
  int nparams;                /**< 形参数量 */
  AstFuncParam *params;       /**< 形参数组 */
  int is_vararg;              /**< 是否有可变参数... */
  TString *vararg_name;       /**< 命名变参名称，...name 语法，无则为NULL */
  int is_async;               /**< 是否是 async 函数（需要 OP_ASYNCWRAP） */
  AstBlock body;              /**< 函数体语句块 */
  int nlocals;                /**< 局部变量总数（包括参数） */
  AstUpvalueDesc *upvalues;   /**< upvalue描述符数组 */
  int nupvalues;              /**< 当前upvalue数量 */
  int upval_cap;              /**< upvalue数组容量 */
  int nups;                   /**< 实际upvalue数量，codegen阶段填充 */
  int line_defined;           /**< 函数定义起始行号 */
  TString *source;            /**< 源码文件名 */
  TypeHint *return_type_hint; /**< 返回值类型注解，无类型注解则为 NULL */
  TString **generic_params;   /**< 泛型类型参数名数组，function<T> 语法，无泛型则为 NULL */
  int ngeneric_params;        /**< 泛型参数数量 */
  TypeHint **generic_constraints; /**< 泛型类型约束数组，与 generic_params 一一对应 */
  unsigned int nodiscard:1;   /**< 函数属性：<nodiscard> 标记，返回值不可丢弃 */
  AstFunc **child_funcs;      /**< 直接嵌套的子函数列表 */
  int nchild_funcs;           /**< 当前子函数数量 */
  int child_cap;              /**< 子函数数组容量 */
};


/* ---------- 编译单元（顶层chunk） ---------- */
struct AstChunk {
  AstNode node;               /**< 继承基类 */
  AstFunc *main_func;         /**< 主chunk作为一个函数 */
  AstFunc **all_funcs;        /**< 所有函数的平铺列表（包括main_func） */
  int nfuncs;                 /**< 当前函数总数 */
  int funcs_cap;              /**< 函数数组容量 */
  TString *source;            /**< 源码文件名 */
  AstPool *pool;              /**< 指向构建此chunk的内存池，方便释放 */
};


/* ---------- 函数与Chunk构造函数声明 ---------- */
/**
 * @brief 创建新的函数定义节点
 * @param p 内存池
 * @param func_idx 唯一函数ID
 * @param parent_idx 父函数ID，主chunk为-1
 * @param line 函数定义起始行号
 * @return 初始化好的AstFunc指针
 */
LUAI_FUNC AstFunc *ast_new_func(AstPool *p, int func_idx, int parent_idx, int line);

/**
 * @brief 创建新的编译单元（chunk）
 * @param p 内存池
 * @param source 源码文件名TString
 * @return 初始化好的AstChunk指针，main_func已自动创建
 */
LUAI_FUNC AstChunk *ast_new_chunk(AstPool *p, TString *source);

/**
 * @brief 将函数添加到chunk的函数列表和父函数的子函数列表
 * @param chunk 目标编译单元
 * @param f 要添加的函数（其parent_idx必须已设置）
 */
LUAI_FUNC void ast_chunk_add_func(AstChunk *chunk, AstFunc *f);

/**
 * @brief 创建函数参数
 * @param p 内存池
 * @param name 参数名TString
 * @param attr 参数属性（AST_ATTR_NONE等）
 * @return 初始化好的AstFuncParam指针
 */
LUAI_FUNC AstFuncParam *ast_new_param(AstPool *p, TString *name, int attr);

/**
 * @brief 向函数添加upvalue描述符（自动扩容）
 * @param p 内存池
 * @param f 目标函数
 * @param src upvalue来源
 * @param idx 父局部变量索引或父upvalue索引
 * @param name upvalue名称
 */
LUAI_FUNC void ast_func_add_upvalue(AstPool *p, AstFunc *f, AstUpvalSrc src, int idx, TString *name);


/* ---------- AST调试打印函数（S表达式风格） ---------- */
/**
 * @brief 打印表达式节点
 * @param out 输出文件指针
 * @param e 表达式节点指针
 * @param indent 当前缩进层级
 */
LUAI_FUNC void ast_dump_expr(FILE *out, AstExpr *e, int indent);

/**
 * @brief 打印语句节点
 * @param out 输出文件指针
 * @param s 语句节点指针
 * @param indent 当前缩进层级
 */
LUAI_FUNC void ast_dump_stmt(FILE *out, AstStmt *s, int indent);

/**
 * @brief 打印语句块
 * @param out 输出文件指针
 * @param blk 语句块指针
 * @param indent 当前缩进层级
 */
LUAI_FUNC void ast_dump_block(FILE *out, AstBlock *blk, int indent);

/**
 * @brief 打印函数节点
 * @param out 输出文件指针
 * @param f 函数节点指针
 * @param indent 当前缩进层级
 */
LUAI_FUNC void ast_dump_func(FILE *out, AstFunc *f, int indent);

/**
 * @brief 打印整个编译单元（chunk）
 * @param out 输出文件指针
 * @param chunk 编译单元指针
 */
LUAI_FUNC void ast_dump_chunk(FILE *out, AstChunk *chunk);


/* ---------- 匹配模式构造函数声明 ---------- */
LUAI_FUNC AstMatchPat *ast_new_pat_wildcard(AstPool *p, int line);
LUAI_FUNC AstMatchPat *ast_new_pat_literal(AstPool *p, AstExpr *literal, int line);
LUAI_FUNC AstMatchPat *ast_new_pat_variable(AstPool *p, TString *name, int line);
LUAI_FUNC AstMatchPat *ast_new_pat_range(AstPool *p, AstExpr *low, AstExpr *high, int line);
LUAI_FUNC AstMatchPat *ast_new_pat_type(AstPool *p, TString *type_name, int line);
LUAI_FUNC AstMatchPat *ast_new_pat_or(AstPool *p, AstMatchPat **pats, int npat, int line);
LUAI_FUNC AstMatchPat *ast_new_pat_table(AstPool *p, AstMatchPat **fields, int nfields, int line);
LUAI_FUNC AstStmt *ast_new_stmt_match(AstPool *p, AstExpr *control, AstMatchArm *arms, int narms, int is_expr, int line);


#endif
