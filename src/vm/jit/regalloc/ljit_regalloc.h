#ifndef ljit_regalloc_h
#define ljit_regalloc_h

#include "../ir/ljit_ir.h"

typedef struct {
    int start;
    int end;
} ljit_live_interval_t;

typedef struct {
    ljit_live_interval_t *intervals; /* Array of size proto->maxstacksize */
    char *interference_graph;        /* Adjacency matrix: maxstacksize * maxstacksize */
    int *reg_mapping;                /* Array of physical registers assigned to vregs */
    int *is_spilled;                 /* Array of booleans */
    int *stack_offsets;              /* Array of stack offsets for spilled vregs */
} ljit_regalloc_info_t;

void ljit_regalloc(ljit_ctx_t *ctx);
void ljit_reg_live(ljit_ctx_t *ctx);
void ljit_reg_graph(ljit_ctx_t *ctx);
void ljit_reg_color(ljit_ctx_t *ctx);
void ljit_reg_spill(ljit_ctx_t *ctx);
void ljit_reg_alloc_process(ljit_ctx_t *ctx);

#endif
