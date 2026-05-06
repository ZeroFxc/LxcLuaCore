#include "ljit_regalloc.h"

/*
 * MVP Register Allocation (Direct Stack Mapping)
 * Instead of complex graph coloring, we map every Lua virtual register
 * directly to its corresponding stack offset on the Lua stack.
 * In Lua, register 'n' is typically stored at: base + n * sizeof(TValue)
 */

static void map_operand(ljit_ir_val_t *val) {
    if (val->type == IR_VAL_REG) {
        val->is_spilled = 1; /* Always mapped to memory (Lua stack) */
        val->phys_reg = 0;   /* No physical register assigned */
        /* Assuming we pass the Lua 'base' pointer in a known register (e.g., S0),
           the offset for register N is N * sizeof(TValue) */
        val->stack_ofs = val->v.reg * sizeof(TValue);
    }
}

void ljit_regalloc(ljit_ctx_t *ctx) {
    if (!ctx || !ctx->ir_head) return;

    /* Iterate over all IR nodes and map virtual registers to stack offsets */
    ljit_ir_node_t *node = ctx->ir_head;
    while (node) {
        map_operand(&node->dest);
        map_operand(&node->src1);
        map_operand(&node->src2);

        node = node->next;
    }
}
