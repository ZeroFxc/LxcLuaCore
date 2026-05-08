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

    // Available registers for allocation
    int available_regs[] = {SLJIT_S1, SLJIT_S2, SLJIT_R2, SLJIT_R3};
    int num_available_regs = sizeof(available_regs) / sizeof(available_regs[0]);

    // Initialize mappings
    for (int i = 0; i < max_vregs; i++) {
        info->reg_mapping[i] = -1;
        info->is_spilled[i] = 1; // Default to spilled
    }

    // Stack for Chaitin-Briggs graph coloring
    int *stack = (int *)calloc(max_vregs, sizeof(int));
    int *degrees = (int *)calloc(max_vregs, sizeof(int));
    int *status = (int *)calloc(max_vregs, sizeof(int));
    int *used_colors = (int *)calloc(num_available_regs, sizeof(int));

    if (!stack || !degrees || !status || !used_colors) {
        if (stack) free(stack);
        if (degrees) free(degrees);
        if (status) free(status);
        if (used_colors) free(used_colors);
        return;
    }

    int stack_top = 0;

    // Calculate initial degrees
    for (int i = 0; i < max_vregs; i++) {
        if (info->intervals[i].start == -1) {
            status[i] = 3; // Ignore unused vregs
            continue;
        }
        for (int j = 0; j < max_vregs; j++) {
            if (i != j && info->interference_graph[i * max_vregs + j]) {
                degrees[i]++;
            }
        }
    }

    int remaining = 0;
    for (int i = 0; i < max_vregs; i++) {
        if (status[i] == 0) remaining++;
    }

    // Simplify and Spill phases
    while (remaining > 0) {
        int found = 0;

        // Simplify phase: find a node with degree < K
        for (int i = 0; i < max_vregs; i++) {
            if (status[i] == 0 && degrees[i] < num_available_regs) {
                // Remove node from graph
                status[i] = 1;
                stack[stack_top++] = i;
                remaining--;
                found = 1;

                // Decrease degree of neighbors
                for (int j = 0; j < max_vregs; j++) {
                    if (status[j] == 0 && info->interference_graph[i * max_vregs + j]) {
                        degrees[j]--;
                    }
                }
                break;
            }
        }

        // Spill phase: if no node < K found, pick a node to spill
        if (!found) {
            int spill_node = -1;
            int max_degree = -1;

            // Pick node with highest degree to spill (heuristic)
            for (int i = 0; i < max_vregs; i++) {
                if (status[i] == 0 && degrees[i] > max_degree) {
                    spill_node = i;
                    max_degree = degrees[i];
                }
            }

            if (spill_node != -1) {
                status[spill_node] = 2; // Mark as spilled
                stack[stack_top++] = spill_node;
                remaining--;

                // Decrease degree of neighbors
                for (int j = 0; j < max_vregs; j++) {
                    if (status[j] == 0 && info->interference_graph[spill_node * max_vregs + j]) {
                        degrees[j]--;
                    }
                }
            }
        }
    }

    // Select phase: pop nodes from stack and assign colors
    while (stack_top > 0) {
        int node = stack[--stack_top];

        memset(used_colors, 0, num_available_regs * sizeof(int));

        // Mark colors used by already colored neighbors
        for (int j = 0; j < max_vregs; j++) {
            if (node != j && info->interference_graph[node * max_vregs + j]) {
                if (status[j] == 3 && info->reg_mapping[j] != -1) {
                    for (int k = 0; k < num_available_regs; k++) {
                        if (available_regs[k] == info->reg_mapping[j]) {
                            used_colors[k] = 1;
                            break;
                        }
                    }
                }
            }
        }

        // Find available color
        int assigned_color = -1;
        for (int k = 0; k < num_available_regs; k++) {
            if (used_colors[k] == 0) {
                assigned_color = available_regs[k];
                break;
            }
        }

        if (assigned_color != -1) {
            info->reg_mapping[node] = assigned_color;
            info->is_spilled[node] = 0;
            status[node] = 3; // Colored
        } else {
            // Actual spill
            info->reg_mapping[node] = -1;
            info->is_spilled[node] = 1;
            status[node] = 3; // Processed (spilled)
        }
    }

    free(used_colors);
    free(stack);
    free(degrees);
    free(status);
}
