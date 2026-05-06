#include "ljit_codegen.h"
#include "../core/ljit_internal.h"

void ljit_codegen(void *ctx) {
    if (!ctx) return;

    // Outline the code generation flow
    ljit_cg_emit_add(ctx);
    ljit_cg_emit_sub(ctx);
    ljit_cg_emit_jmp(ctx);
    ljit_cg_emit_ret(ctx);
    ljit_cg_emit_gettable(ctx);
    ljit_cg_emit_settable(ctx);
    ljit_cg_emit_call(ctx);
    ljit_cg_emit_conv(ctx);
}
