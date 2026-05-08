#include "ljit_regalloc.h"
#include <stdlib.h>

static void apply_mapping(ljit_regalloc_info_t *info, ljit_ir_val_t *val) {
    if (val->type == IR_VAL_REG) {
        int reg = val->v.reg;
        val->is_spilled = info->is_spilled[reg];
        if (val->is_spilled) {
            val->phys_reg = 0;
            val->stack_ofs = reg * sizeof(TValue);
        } else {
            val->phys_reg = info->reg_mapping[reg];
            val->stack_ofs = 0;
        }
    }
}

void ljit_regalloc(ljit_ctx_t *ctx) {
    if (!ctx || !ctx->ir_head) return;

    // Run passes
    ljit_reg_live(ctx);
    ljit_reg_graph(ctx);
    ljit_reg_color(ctx);

    ljit_regalloc_info_t *info = (ljit_regalloc_info_t *)ctx->regalloc_info;
    if (!info) return;

    // Iterate over all IR nodes and apply the mapping to all register operands
    ljit_ir_node_t *node = ctx->ir_head;
    while (node) {
        apply_mapping(info, &node->dest);
        apply_mapping(info, &node->src1);
        apply_mapping(info, &node->src2);
        node = node->next;
    }

    // Cleanup info
    if (info->intervals) free(info->intervals);
    if (info->interference_graph) free(info->interference_graph);
    if (info->reg_mapping) free(info->reg_mapping);
    if (info->is_spilled) free(info->is_spilled);
    free(info);
    ctx->regalloc_info = NULL;
}
