/*
** This file has been pre-processed with DynASM.
** https://luajit.org/dynasm.html
** DynASM version 1.5.0, DynASM x64 version 1.5.0
** DO NOT EDIT! The original file is in "src/vm/dasm/vm_dasm_x64.dasc".
*/

#line 1 "src/vm/dasm/vm_dasm_x64.dasc"
/*
** DynASM x64 VM skeleton for LXCLua
** Runtime code generation for x64 Windows (Microsoft x64 calling convention)
*/

//|.arch x64
#if DASM_VERSION != 10500
#error "Version mismatch between DynASM and included encoding engine"
#endif
#line 7 "src/vm/dasm/vm_dasm_x64.dasc"

#include <stddef.h>
#include "dasm_proto.h"

static void dasm_emit_code(dasm_State **Dst) {
//|  ->vm_code_start:
//|  ->dasmV_execute:
//|    ret
//|  ->vm_code_end:
dasm_put(Dst, 0);
#line 16 "src/vm/dasm/vm_dasm_x64.dasc"
}

//|.actionlist dasm_actions_
static const unsigned char dasm_actions_[8] = {
  248,10,248,11,195,248,12,255
};

#line 19 "src/vm/dasm/vm_dasm_x64.dasc"
//|.globals DASM_VM_
enum {
  DASM_VM_vm_code_start,
  DASM_VM_dasmV_execute,
  DASM_VM_vm_code_end,
  DASM_VM__MAX
};
#line 20 "src/vm/dasm/vm_dasm_x64.dasc"

#define DASM_VM_MAXGLOB	DASM_VM__MAX
