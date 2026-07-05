#ifndef ljit_analyze_h
#define ljit_analyze_h

#include "../ir/ljit_ir.h"

/* Type inference enum for Lua types */
typedef enum {
    JIT_TYPE_ANY = 0,
    JIT_TYPE_NIL,
    JIT_TYPE_BOOL,
    JIT_TYPE_INT,
    JIT_TYPE_NUM,
    JIT_TYPE_STR,
    JIT_TYPE_TAB,
    JIT_TYPE_FUNC,
    JIT_TYPE_USERDATA
} ljit_type_t;

/* Analysis information stored in ctx->analyze_info */
typedef struct {
    int max_regs;       /* Number of virtual registers used (proto->maxstacksize) */
    ljit_type_t *reg_types; /* Array of inferred types for each virtual register */
    int *def_pc;        /* Array storing the PC where the register was last defined */
    int *is_live;       /* Boolean array indicating if a register is live */
    /* CFG 数据流分析字段 */
    int num_bbs;        /* BB 数量 */
    ljit_type_t *in_types;  /* 扁平数组: num_bbs * max_regs, 每个 BB 入口类型 */
    ljit_type_t *out_types; /* 扁平数组: num_bbs * max_regs, 每个 BB 出口类型 */
} ljit_analyze_info_t;

void ljit_analyze(ljit_ctx_t *ctx);
void ljit_translate(ljit_ctx_t *ctx);
void ljit_analyze_destroy(ljit_ctx_t *ctx);

#endif
