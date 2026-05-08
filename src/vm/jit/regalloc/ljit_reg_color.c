#include "ljit_regalloc.h"
#include "../../../jit/sljitLir.h"
#include <stdlib.h>
#include <string.h>

void ljit_reg_color(ljit_ctx_t *ctx) {
    if (!ctx || !ctx->proto || !ctx->regalloc_info) return;

    int max_vregs = ctx->proto->maxstacksize;
    ljit_regalloc_info_t *info = (ljit_regalloc_info_t *)ctx->regalloc_info;

    info->reg_mapping = (int *)calloc(max_vregs, sizeof(int));
    info->is_spilled = (int *)calloc(max_vregs, sizeof(int));

    if (!info->reg_mapping || !info->is_spilled) return;

    // We will utilize SLJIT_S1, SLJIT_S2, SLJIT_R2, and SLJIT_R3
    int available_regs[] = {SLJIT_S1, SLJIT_S2, SLJIT_R2, SLJIT_R3};
    int num_available_regs = sizeof(available_regs) / sizeof(available_regs[0]);

    // Initialize all as unmapped (-1)
    for (int i = 0; i < max_vregs; i++) {
        info->reg_mapping[i] = -1;
        info->is_spilled[i] = 1; // Default to spilled
    }

    // Array to track used colors (registers) for neighbors of the current node
    int *used_colors = (int *)calloc(num_available_regs, sizeof(int));
    if (!used_colors) return;

    // Greedy graph coloring
    for (int i = 0; i < max_vregs; i++) {
        // Skip unused virtual registers
        if (info->intervals[i].start == -1) continue;

        memset(used_colors, 0, num_available_regs * sizeof(int));

        // Mark colors used by neighbors
        for (int j = 0; j < max_vregs; j++) {
            if (i != j && info->interference_graph[i * max_vregs + j]) {
                if (info->reg_mapping[j] != -1) {
                    // Find which available register it is mapped to
                    for (int k = 0; k < num_available_regs; k++) {
                        if (available_regs[k] == info->reg_mapping[j]) {
                            used_colors[k] = 1;
                            break;
                        }
                    }
                }
            }
        }

        // Find the first unused color
        int assigned_color = -1;
        for (int k = 0; k < num_available_regs; k++) {
            if (used_colors[k] == 0) {
                assigned_color = available_regs[k];
                break;
            }
        }

        if (assigned_color != -1) {
            info->reg_mapping[i] = assigned_color;
            info->is_spilled[i] = 0;
        } else {
            // Spilled to stack
            info->reg_mapping[i] = -1;
            info->is_spilled[i] = 1;
        }
    }

    free(used_colors);
}
