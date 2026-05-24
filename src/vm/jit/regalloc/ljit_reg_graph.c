#include "ljit_regalloc.h"
#include <stdlib.h>

void ljit_reg_graph(ljit_ctx_t *ctx) {
    if (!ctx || !ctx->proto || !ctx->regalloc_info) return;

    int max_vregs = ctx->proto->maxstacksize;
    ljit_regalloc_info_t *info = (ljit_regalloc_info_t *)ctx->regalloc_info;

    info->interference_graph = (char *)calloc(max_vregs * max_vregs, sizeof(char));
    if (!info->interference_graph) return;

    /* 基于live区间重叠构建干涉图 */
    for (int i = 0; i < max_vregs; i++) {
        for (int j = i + 1; j < max_vregs; j++) {
            if (info->intervals[i].start != -1 && info->intervals[j].start != -1) {
                if (info->intervals[i].start <= info->intervals[j].end &&
                    info->intervals[j].start <= info->intervals[i].end) {
                    info->interference_graph[i * max_vregs + j] = 1;
                    info->interference_graph[j * max_vregs + i] = 1;
                }
            }
        }
    }

    /*
     * 关键修复: live-in寄存器互相干涉.
     * live-in寄存器在函数入口处全部需要从Lua栈加载值到物理寄存器.
     * 如果两个live-in寄存器共享同一物理寄存器, 后加载的会覆盖先加载的.
     * 因此必须确保所有live-in寄存器分配到不同的物理寄存器.
     */
    for (int i = 0; i < max_vregs; i++) {
        if (!info->is_livein || !info->is_livein[i]) continue;
        for (int j = i + 1; j < max_vregs; j++) {
            if (!info->is_livein[j]) continue;
            info->interference_graph[i * max_vregs + j] = 1;
            info->interference_graph[j * max_vregs + i] = 1;
        }
    }
}
