#include "ljit_regalloc.h"
#include <stdlib.h>

void ljit_reg_graph(ljit_ctx_t *ctx) {
    if (!ctx || !ctx->proto || !ctx->regalloc_info) return;

    int max_vregs = ctx->proto->maxstacksize;
    ljit_regalloc_info_t *info = (ljit_regalloc_info_t *)ctx->regalloc_info;

    // Allocate an adjacency matrix for the interference graph
    // We use a 1D array of size max_vregs * max_vregs
    info->interference_graph = (char *)calloc(max_vregs * max_vregs, sizeof(char));
    if (!info->interference_graph) return;

    // Build the interference graph based on overlapping live intervals
    for (int i = 0; i < max_vregs; i++) {
        for (int j = i + 1; j < max_vregs; j++) {
            // Check if both intervals are valid
            if (info->intervals[i].start != -1 && info->intervals[j].start != -1) {
                // Check if interval i overlaps with interval j
                if (info->intervals[i].start <= info->intervals[j].end &&
                    info->intervals[j].start <= info->intervals[i].end) {
                    // There is an interference, add an edge (undirected)
                    info->interference_graph[i * max_vregs + j] = 1;
                    info->interference_graph[j * max_vregs + i] = 1;
                }
            }
        }
    }
}
