#ifndef ljit_ir_h
#define ljit_ir_h

#include "../core/ljit_internal.h"
#include "../../../core/lobject.h"
#include "../../../core/lstate.h"

/* IR Operand Types */
typedef enum {
    IR_VAL_NONE = 0,
    IR_VAL_REG,      /* Virtual register / Local variable */
    IR_VAL_CONST,    /* Constant index */
    IR_VAL_UPVAL,    /* Upvalue index */
    IR_VAL_INT,      /* Immediate integer */
    IR_VAL_NUM,      /* Immediate number */
    IR_VAL_LABEL     /* Jump label */
} ljit_ir_val_type_t;

typedef struct {
    ljit_ir_val_type_t type;
    union {
        int reg;
        int k;
        int uv;
        lua_Integer i;
        lua_Number n;
        int label_id;
    } v;

    /* Physical mapping fields for CodeGen */
    int is_spilled; /* 1 if mapped to stack, 0 if mapped to physical register */
    int phys_reg;   /* SLJIT physical register id (if not spilled) */
    int stack_ofs;  /* Stack offset in bytes (if spilled, relative to base pointer) */
} ljit_ir_val_t;

/* IR Opcodes */
typedef enum {
    IR_NOP = 0,
    IR_MOV,
    IR_LOADK,
    IR_LOADI,
    IR_LOADF,
    IR_LOADNIL,
    IR_LOADBOOL,
    /* Arithmetic */
    IR_ADD, IR_SUB, IR_MUL, IR_DIV, IR_IDIV, IR_MOD, IR_POW,
    IR_BAND, IR_BOR, IR_BXOR, IR_SHL, IR_SHR,
    IR_UNM, IR_BNOT, IR_NOT,
    /* Control Flow */
    IR_JMP, IR_CJMP,  /* CJMP: Conditional Jump (jump if true/false) */
    IR_RET,
    /* Tables/Calls (simplified) */
    IR_NEWTABLE, IR_GETTABLE, IR_SETTABLE,
    IR_CALL
} ljit_ir_op_t;

/* IR Node Structure (Doubly-linked list) */
typedef struct ljit_ir_node {
    ljit_ir_op_t op;
    ljit_ir_val_t dest;
    ljit_ir_val_t src1;
    ljit_ir_val_t src2;
    int original_pc;  /* Maps back to original bytecode PC */
    struct ljit_ir_node *prev;
    struct ljit_ir_node *next;
} ljit_ir_node_t;

typedef struct ljit_bb {
    int start_pc;
    int end_pc;
    struct ljit_bb *next;
} ljit_bb_t;

typedef struct ljit_ctx {
    lua_State *L;
    Proto *proto;
    ljit_bb_t *cfg;
    ljit_ir_node_t *ir_head;
    ljit_ir_node_t *ir_tail;
    int next_label_id;
    void *compiler;
    struct sljit_label **labels;
    int num_jumps;
    struct sljit_jump **jumps;
    int *jump_targets;
} ljit_ctx_t;

void *ljit_context_create(lua_State *L, Proto *proto);
ljit_bb_t *ljit_ir_bb_build(Proto *proto);
void ljit_context_destroy(void *ctx);

/* IR Node Management */
ljit_ir_node_t *ljit_ir_new(ljit_ir_op_t op, int pc);
void ljit_ir_append(ljit_ctx_t *ctx, ljit_ir_node_t *node);

/* Label Management */
int ljit_ir_new_label(ljit_ctx_t *ctx);

#endif
