#include <stdio.h>
#include "src/core/lobject.h"
#include "src/core/lstate.h"
#include "src/core/ldo.h"
#include "src/core/lfunc.h"
int main() {
    printf("sizeof(CallInfo)=%zu\n", sizeof(CallInfo));
    printf("offsetof(CallInfo.func)=%zu\n", offsetof(CallInfo, func));
    printf("offsetof(CallInfo.top)=%zu\n", offsetof(CallInfo, top));
    printf("offsetof(CallInfo.u.l.savedpc)=%zu\n", offsetof(CallInfo, u.l.savedpc));
    printf("offsetof(CallInfo.previous)=%zu\n", offsetof(CallInfo, previous));
    printf("offsetof(lua_State.top)=%zu\n", offsetof(lua_State, top));
    printf("offsetof(lua_State.ci)=%zu\n", offsetof(lua_State, ci));
    printf("sizeof(LClosure)=%zu\n", sizeof(LClosure));
    printf("offsetof(LClosure.p)=%zu\n", offsetof(LClosure, p));
    printf("offsetof(LClosure.upvals)=%zu\n", offsetof(LClosure, upvals));
    printf("sizeof(Proto)=%zu\n", sizeof(Proto));
    printf("offsetof(Proto.k)=%zu\n", offsetof(Proto, k));
    printf("offsetof(Proto.code)=%zu\n", offsetof(Proto, code));
    printf("offsetof(Proto.sizecode)=%zu\n", offsetof(Proto, sizecode));
    printf("sizeof(Instruction)=%zu\n", sizeof(Instruction));
    printf("sizeof(TValue)=%zu\n", sizeof(TValue));
    printf("offsetof(TValue.value_)=%zu\n", offsetof(TValue, value_));
    printf("offsetof(TValue.tt_)=%zu\n", offsetof(TValue, tt_));
    printf("sizeof(StkId_rel)=%zu\n", sizeof(StkId));
    printf("sizeof(UpVal)=%zu\n", sizeof(UpVal));
    printf("offsetof(UpVal.v)=%zu\n", offsetof(UpVal, v));
    return 0;
}
