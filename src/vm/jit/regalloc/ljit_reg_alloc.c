#include "ljit_regalloc.h"

static void apply_mapping(ljit_regalloc_info_t *info, ljit_ir_val_t *val) {
    if (val->type == IR_VAL_REG) {
        int reg = val->v.reg;
        val->is_spilled = info->is_spilled[reg];
        if (val->is_spilled) {
            val->phys_reg = 0;
            // Provide a fallback if stack_offsets allocation failed
            if (info->stack_offsets) {
                val->stack_ofs = info->stack_offsets[reg];
            } else {
                val->stack_ofs = reg * sizeof(TValue);
            }
        } else {
            val->phys_reg = info->reg_mapping[reg];
            val->stack_ofs = 0;
        }
    }
}

void ljit_reg_alloc_process(ljit_ctx_t *ctx) {
    if (!ctx || !ctx->ir_head || !ctx->regalloc_info) return;

    ljit_regalloc_info_t *info = (ljit_regalloc_info_t *)ctx->regalloc_info;

    // Iterate over all IR nodes and apply the mapping to all register operands
    ljit_ir_node_t *node = ctx->ir_head;
    while (node) {
        apply_mapping(info, &node->dest);
        apply_mapping(info, &node->src1);
        apply_mapping(info, &node->src2);
        node = node->next;
    }
}
