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
    info->is_livein = (int *)calloc(max_vregs, sizeof(int));
    if (!info->intervals || !info->is_livein) {
        free(info->intervals);
        free(info->is_livein);
        free(info);
        return;
    }

    /* 初始化 */
    for (int i = 0; i < max_vregs; i++) {
        info->intervals[i].start = -1;
        info->intervals[i].end = -1;
        info->is_livein[i] = 0;
    }

    /*
     * 跟踪每个寄存器的首次定义时间和首次使用时间.
     * sentinel = max_vregs + 1, 大于任何合法的间隔时间.
     */
    int sentinel = max_vregs + 1;
    int *first_def = (int *)calloc(max_vregs, sizeof(int));
    int *first_use = (int *)calloc(max_vregs, sizeof(int));
    for (int i = 0; i < max_vregs; i++) {
        first_def[i] = sentinel;
        first_use[i] = sentinel;
    }

    int time = 0;
    ljit_ir_node_t *node = ctx->ir_head;

    while (node) {
        time++;

        /* 先记录 dest: 定义 */
        if (node->dest.type == IR_VAL_REG && node->dest.v.reg >= 0 && node->dest.v.reg < max_vregs) {
            int r = node->dest.v.reg;
            if (time < first_def[r]) first_def[r] = time;
        }

        update_interval(info, &node->dest, time, max_vregs);
        update_interval(info, &node->src1, time, max_vregs);
        update_interval(info, &node->src2, time, max_vregs);

        node = node->next;
    }

    /*
     * 重新扫描以记录首次使用时间(src1/src2):
     * 必须分两遍扫描, 因为同一个node可能同时定义和使用同一寄存器(如 ADD R0 R0 R1),
     * need to capture both def and use times independently.
     */
    time = 0;
    node = ctx->ir_head;
    while (node) {
        time++;
        if (node->src1.type == IR_VAL_REG && node->src1.v.reg >= 0 && node->src1.v.reg < max_vregs) {
            int r = node->src1.v.reg;
            if (time < first_use[r]) first_use[r] = time;
        }
        if (node->src2.type == IR_VAL_REG && node->src2.v.reg >= 0 && node->src2.v.reg < max_vregs) {
            int r = node->src2.v.reg;
            if (time < first_use[r]) first_use[r] = time;
        }
        node = node->next;
    }

    /* 标记live-in: 首次使用早于首次定义(或从未定义) */
    for (int i = 0; i < max_vregs; i++) {
        if (first_use[i] < first_def[i]) {
            info->is_livein[i] = 1;
        }
    }

    free(first_def);
    free(first_use);

    ctx->regalloc_info = info;
}
