sed -i 's/case IR_CLOSURE:/case IR_CLOSURE: ljit_cg_emit_closure(node, ctx); break;/g' src/vm/jit/codegen/ljit_codegen.c
