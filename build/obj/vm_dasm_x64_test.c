/*
** This file has been pre-processed with DynASM.
** https://luajit.org/dynasm.html
** DynASM version 1.5.0, DynASM x64 version 1.5.0
** DO NOT EDIT! The original file is in "src/vm/dasm/vm_dasm_x64.dasc".
*/

#line 1 "src/vm/dasm/vm_dasm_x64.dasc"
/*
** DynASM x64 VM for LXCLua
** Runtime code generation for x64 Windows (Microsoft x64 calling convention)
*/

//|.arch x64
#if DASM_VERSION != 10500
#error "Version mismatch between DynASM and included encoding engine"
#endif
#line 7 "src/vm/dasm/vm_dasm_x64.dasc"

#include <windows.h>
#include <stddef.h>
#include <string.h>

#include "lua.h"
#include "lstate.h"
#include "ldo.h"
#include "lvm.h"
#include "vm_dasm.h"

#define Dst_DECL	dasm_State **Dst
#define Dst_REF		(*Dst)
#include "dasm_proto.h"

#include "dasm_x86.h"

static void *g_vm_code = NULL;
static size_t g_vm_codesize = 0;
static int g_dasm_initialized = 0;

typedef void (*dasm_execute_fn)(lua_State *L, CallInfo *ci);

static void dasm_emit_code(dasm_State **Dst) {
//|  ->vm_code_start:
//|  ->dasmV_execute:
//|    ret
//|  ->vm_code_end:
dasm_put(Dst, 0);
#line 35 "src/vm/dasm/vm_dasm_x64.dasc"
}

//|.actionlist dasm_actions_
static const unsigned char dasm_actions_[8] = {
  248,10,248,11,195,248,12,255
};

#line 38 "src/vm/dasm/vm_dasm_x64.dasc"
//|.globals DASM_VM_
enum {
  DASM_VM_vm_code_start,
  DASM_VM_dasmV_execute,
  DASM_VM_vm_code_end,
  DASM_VM__MAX
};
#line 39 "src/vm/dasm/vm_dasm_x64.dasc"

static void *g_dasm_globals[DASM_VM__MAX];

static int dasmV_do_init(void) {
  dasm_State *D = NULL;
  dasm_State **Dst = &D;
  size_t codesz;
  int status;
  void *code;
  dasm_execute_fn fn;

  if (g_dasm_initialized) return 0;

  memset(g_dasm_globals, 0, sizeof(g_dasm_globals));

  dasm_init(Dst, 1);
  dasm_setupglobal(Dst, g_dasm_globals, DASM_VM__MAX);
  dasm_setup(Dst, dasm_actions_);

  dasm_emit_code(Dst);

  if ((status = dasm_link(Dst, &codesz)) != 0) {
    dasm_free(Dst);
    return status;
  }

  code = VirtualAlloc(NULL, codesz, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
  if (!code) {
    dasm_free(Dst);
    return -1;
  }

  if ((status = dasm_encode(Dst, code)) != 0) {
    VirtualFree(code, 0, MEM_RELEASE);
    dasm_free(Dst);
    return status;
  }

  dasm_free(Dst);

  g_vm_code = code;
  g_vm_codesize = codesz;
  g_dasm_initialized = 1;

  return 0;
}

int dasmV_init(void) {
  return dasmV_do_init();
}

void dasmV_execute(lua_State *L, CallInfo *ci) {
  if (!g_dasm_initialized) {
    dasmV_do_init();
  }
  luaV_execute(L, ci);
}
