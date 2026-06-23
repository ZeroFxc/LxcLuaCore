# Debug Session: error-function-crash

**Status:** [OPEN]
**Date:** 2026-06-23
**Symptoms:** 最简单的 `error("test")` 调用导致 C 层崩溃（access violation，退出码 0xC00000FF）

## 调用链

```
error("test") 
  → luaB_error (lbaselib.c:334-343)
    → luaL_optinteger(L, 2, 1)  // 获取 level
    → lua_settop(L, 1)          // 栈设为只有错误消息
    → luaL_where(L, level)      // 添加位置信息
      → lua_getstack(L, level, &ar)
      → lua_getinfo(L, "Sl", &ar)
        → funcinfo(&ar, f)
          → luaO_chunkid(ar.short_src, ar.source, ar.srclen)
      → lua_pushfstring(L, "%s:%d: ", ar.short_src, ar.currentline)
    → lua_pushvalue(L, 1)
    → lua_concat(L, 2)
    → lua_error(L)
      → luaG_errormsg(L)
        → luaD_throw(L, LUA_ERRRUN)
```

## Hypotheses

### A: luaL_where 中 lua_getinfo 的 "S" 选项导致 ar.short_src 缓冲区溢出
`luaO_chunkid` 在 `@` 分支中 `memcpy(out, source+1, srclen)` 复制了 `srclen` 个字符（而非 `srclen-1`），可能写入越界。

### B: luaL_where 中 lua_pushfstring 使用未初始化的 ar 成员
如果 `lua_getstack` 或 `lua_getinfo` 失败，`ar` 中的某些字段可能未初始化。

### C: lua_settop 后栈状态异常，导致后续操作访问无效内存
`lua_settop(L, 1)` 清除了栈，但 `luaL_optinteger` 在 `lua_settop` 之前调用，顺序正确。

### D: lua_concat 或 lua_pushfstring 触发 GC，GC 释放了正在使用的 Closure/Proto
`luaL_where` 中 `lua_getinfo` 获取的 `ar.source` 指向 Proto 的 TString，GC 可能释放 Proto。

### E: luaG_errormsg 中访问无效的 L->top 或栈对象
`luaG_errormsg` 在 `luaD_throw` 之前检查 `s2v(L->top.p - 1)`，如果 `L->top.p` 无效则崩溃。

## Evidence Log

（待收集）