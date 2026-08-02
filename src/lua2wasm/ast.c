#include "ast.h"
#include "xalloc.h"
#include <stdlib.h>
#include <string.h>

struct PoolChunk {
    PoolChunk *next;
    size_t used;
    size_t cap;
    /* Flexible array member follows. */
    char data[];
};

#define POOL_CHUNK_BYTES 8192

void node_pool_init(NodePool *p) {
    p->chunks = NULL;
}

void node_pool_free(NodePool *p) {
    PoolChunk *c = p->chunks;
    while (c) {
        PoolChunk *next = c->next;
        free(c);
        c = next;
    }
    p->chunks = NULL;
}

void *node_pool_alloc(NodePool *p, size_t bytes) {
    size_t aligned = (bytes + 7u) & ~(size_t)7u;
    PoolChunk *c = p->chunks;
    if (!c || c->used + aligned > c->cap) {
        /* Need a new chunk. Pick max(default size, requested) so any single
         * request fits. Chunks are never realloc'd, so returned pointers
         * remain stable for the lifetime of the pool. */
        size_t cap = aligned > POOL_CHUNK_BYTES ? aligned : POOL_CHUNK_BYTES;
        c = xmalloc(sizeof(PoolChunk) + cap);
        c->next = p->chunks;
        c->used = 0;
        c->cap = cap;
        p->chunks = c;
    }
    void *ptr = c->data + c->used;
    c->used += aligned;
    memset(ptr, 0, aligned);
    return ptr;
}

Expr *expr_new(NodePool *p, ExprKind k, int line) {
    Expr *e = node_pool_alloc(p, sizeof(Expr));
    e->kind = k;
    e->line = line;
    e->paren = 0;
    return e;
}

Stmt *stmt_new(NodePool *p, StmtKind k, int line) {
    Stmt *s = node_pool_alloc(p, sizeof(Stmt));
    s->kind = k;
    s->line = line;
    return s;
}

LuaFunc *func_new(NodePool *p, int func_idx, int line) {
    LuaFunc *f = node_pool_alloc(p, sizeof(LuaFunc));
    f->func_idx = func_idx;
    f->line = line;
    return f;
}

/* 返回表达式类型的字符串名称，用于 dump/调试 */
const char *expr_kind_name(ExprKind k) {
    switch (k) {
    case EXPR_NIL:          return "nil";
    case EXPR_TRUE:         return "true";
    case EXPR_FALSE:        return "false";
    case EXPR_INT:          return "int";
    case EXPR_FLOAT:        return "float";
    case EXPR_STRING:       return "string";
    case EXPR_VAR:          return "var";
    case EXPR_CALL:         return "call";
    case EXPR_BINOP:        return "binop";
    case EXPR_UNOP:         return "unop";
    case EXPR_FUNCTION:     return "function";
    case EXPR_INDEX:        return "index";
    case EXPR_TABLE:        return "table";
    case EXPR_METHOD_CALL:  return "method_call";
    case EXPR_VARARG:       return "vararg";
    case EXPR_IS:           return "is";
    default:                return "?";
    }
}

/* 返回语句类型的字符串名称，用于 dump/调试 */
const char *stmt_kind_name(StmtKind k) {
    switch (k) {
    case STMT_LOCAL:         return "local";
    case STMT_ASSIGN:        return "assign";
    case STMT_EXPR:          return "expr_stmt";
    case STMT_IF:            return "if";
    case STMT_WHILE:         return "while";
    case STMT_DO:            return "do";
    case STMT_RETURN:        return "return";
    case STMT_LOCAL_FUNC:    return "local_func";
    case STMT_FOR_NUM:       return "for_num";
    case STMT_FOR_GEN:       return "for_gen";
    case STMT_REPEAT:        return "repeat";
    case STMT_BREAK:         return "break";
    case STMT_GLOBAL:        return "global";
    case STMT_GOTO:          return "goto";
    case STMT_LABEL:         return "label";
    case STMT_CLASS:         return "class";
    case STMT_INTERFACE:     return "interface";
    case STMT_TRAIT:         return "trait";
    case STMT_METH_OVERRIDE: return "meth_override";
    case STMT_IFACE_EXTENDS: return "iface_extends";
    case STMT_NESTED_CLASS:  return "nested_class";
    case STMT_IS_CLASS:      return "is_class";
    default:                 return "?";
    }
}
