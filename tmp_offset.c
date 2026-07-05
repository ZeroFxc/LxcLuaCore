#include <stdio.h>
#include "src/core/lobject.h"
#include "src/core/lstate.h"
int main() {
    printf("sizeof(TValue)=%zu\n", sizeof(TValue));
    printf("offsetof(value_)=%zu\n", (size_t)&((TValue*)0)->value_);
    printf("offsetof(tt_)=%zu\n", (size_t)&((TValue*)0)->tt_);
    printf("sizeof(Value)=%zu\n", sizeof(Value));
    printf("offsetof(ivalue)=%zu\n", (size_t)&((Value*)0)->i);
    printf("offsetof(fltvalue)=%zu\n", (size_t)&((Value*)0)->n);
    printf("offsetof(gc)=%zu\n", (size_t)&((Value*)0)->gc);
    printf("sizeof(Table)=%zu\n", sizeof(Table));
    printf("offsetof(Table.flags)=%zu\n", (size_t)&((Table*)0)->flags);
    printf("offsetof(Table.metatable)=%zu\n", (size_t)&((Table*)0)->metatable);
    printf("offsetof(Table.alimit)=%zu\n", (size_t)&((Table*)0)->alimit);
    printf("offsetof(Table.array)=%zu\n", (size_t)&((Table*)0)->array);
    printf("offsetof(Table.node)=%zu\n", (size_t)&((Table*)0)->node);
    printf("LUA_VNUMINT=%d LUA_VNUMFLT=%d LUA_TNIL=%d LUA_VTABLE=%d\n", LUA_VNUMINT, LUA_VNUMFLT, LUA_TNIL, LUA_VTABLE);
    printf("LUA_VSHRSTR=%d LUA_VLNGSTR=%d\n", LUA_VSHRSTR, LUA_VLNGSTR);
    printf("TM_NEWINDEX=%d TM_INDEX=%d\n", TM_NEWINDEX, TM_INDEX);
    return 0;
}
