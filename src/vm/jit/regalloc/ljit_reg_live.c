#include "ljit_regalloc.h"
#include <stdlib.h>

static void update_interval(ljit_regalloc_info_t *info, ljit_ir_val_t *val, int time, int max_vregs) {
    if (val->type == IR_VAL_REG && val->v.reg >= 0 && val->v.reg < max_vregs) {
        int reg = val->v.reg;
        if (info->intervals[reg].start == -1) {
            info->intervals[reg].start = time;
        }
        info->intervals[reg].end = time;
    }
}

void ljit_reg_live(ljit_ctx_t *ctx) {
    if (!ctx || !ctx->ir_head || !ctx->proto) return;

    int max_vregs = ctx->proto->maxstacksize;

    ljit_regalloc_info_t *info = (ljit_regalloc_info_t *)calloc(1, sizeof(ljit_regalloc_info_t));
    if (!info) return;

    info->intervals = (ljit_live_interval_t *)calloc(max_vregs, sizeof(ljit_live_interval_t));
    if (!info->intervals) {
        free(info);
        return;
    }

    // Initialize intervals to [-1, -1]
    for (int i = 0; i < max_vregs; i++) {
        info->intervals[i].start = -1;
        info->intervals[i].end = -1;
    }

    // Assign sequential ID to represent time
    int time = 0;
    ljit_ir_node_t *node = ctx->ir_head;

    while (node) {
        time++;

        update_interval(info, &node->dest, time, max_vregs);
        update_interval(info, &node->src1, time, max_vregs);
        update_interval(info, &node->src2, time, max_vregs);

        node = node->next;
    }

    ctx->regalloc_info = info;
}
