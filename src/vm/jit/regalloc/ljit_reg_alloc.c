#include "ljit_regalloc.h"
#include "../core/ljit_debug.h"
#include <stdio.h>

static void apply_mapping(ljit_regalloc_info_t *info, ljit_ir_val_t *val) {
    if (val->type == IR_VAL_REG) {
        int reg = val->v.reg;
        val->is_spilled = info->is_spilled[reg];

        if (info->stack_offsets) {
            val->stack_ofs = info->stack_offsets[reg];
        } else {
            val->stack_ofs = reg * sizeof(TValue);
        }

        if (val->is_spilled) {
            val->phys_reg = 0;
        } else {
            val->phys_reg = info->reg_mapping[reg];
        }
    }
}

void ljit_reg_alloc_process(ljit_ctx_t *ctx) {
    if (!ctx || !ctx->ir_head || !ctx->regalloc_info) return;

    ljit_regalloc_info_t *info = (ljit_regalloc_info_t *)ctx->regalloc_info;
    int maxstack = ctx->proto->maxstacksize;

    /* 调试: 打印所有寄存器的映射 */
    JIT_DBG(MOD_REG, "register mapping (maxstack=%d):", maxstack);
    for (int r = 0; r < maxstack; r++) {
        JIT_DBG(MOD_REG, "  R%d -> spilled=%d, phys_reg=%d (SLJIT_S%d=%d)",
            r, info->is_spilled[r],
            info->is_spilled[r] ? 0 : info->reg_mapping[r],
            info->is_spilled[r] ? -1 : (info->reg_mapping[r] - 10),
            info->is_spilled[r] ? 0 : info->reg_mapping[r]);
    }

    /* 迭代所有 IR 节点, 应用映射 */
    ljit_ir_node_t *node = ctx->ir_head;
    while (node) {
        apply_mapping(info, &node->dest);
        apply_mapping(info, &node->src1);
        apply_mapping(info, &node->src2);

        /* 调试: 打印每个节点的映射结果 */
        JIT_DBG(MOD_REG, "node pc=%d op=%d: dest(reg=%d,sp=%d,pr=%d) src1(reg=%d,sp=%d,pr=%d) src2(reg=%d,sp=%d,pr=%d)",
            node->original_pc, node->op,
            node->dest.type == IR_VAL_REG ? node->dest.v.reg : -1,
            node->dest.is_spilled, node->dest.phys_reg,
            node->src1.type == IR_VAL_REG ? node->src1.v.reg : -1,
            node->src1.is_spilled, node->src1.phys_reg,
            node->src2.type == IR_VAL_REG ? node->src2.v.reg : -1,
            node->src2.is_spilled, node->src2.phys_reg);

        node = node->next;
    }
}


