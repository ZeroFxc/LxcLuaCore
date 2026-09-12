/*
 * lbctc_runner.c - LBCTC 转译 C 代码的运行器
 * 功能描述：加载转译后的模块函数，创建 Lua 状态机并执行
 * 参数：无
 * 返回值：0 成功，非 0 失败
 */
#include "lxclua.h"
#include <stdio.h>
#include <stdlib.h>

/* wasm 相关模块的空 stub（链接用，不实际加载） */
int luaopen_wasm3(lua_State *L) { (void)L; return 0; }
int luaopen_wasmtime(lua_State *L) { (void)L; return 0; }

/* 转译后生成的模块入口函数声明（由 basic_syntax.c 提供） */
extern int luaopen_module(lua_State *L);

/*
 * main: 程序入口
 * 功能：创建 Lua 状态机 -> 打开标准库 -> 加载转译模块 -> 执行 -> 关闭
 * 返回：0 成功，非 0 失败
 */
int main(int argc, char **argv) {
    (void)argc; (void)argv;
    int rc = 0;

    /* 创建 Lua 状态机，使用标准分配器 luaL_newstate（默认 alloc+随机种子）
     * 避免自定义 smoke_alloc 的 realloc 边界问题（如 STATUS_HEAP_CORRUPTION） */
    lua_State *L = luaL_newstate();
    if (!L) {
        fprintf(stderr, "runner FAIL: luaL_newstate returned NULL\n");
        return 1;
    }

    /* 打开所有标准库 */
    luaL_openlibs(L);

    /* 压入模块入口函数（由转译生成的 luaopen_module 提供） */
    lua_pushcfunction(L, luaopen_module);

    /* 执行模块函数（0 参数，多返回值） */
    if (lua_pcall(L, 0, LUA_MULTRET, 0) != LUA_OK) {
        const char *err = lua_tostring(L, -1);
        fprintf(stderr, "runner FAIL: lua_pcall error: %s\n", err ? err : "(null)");
        /* 打印完整栈内容，排查 LBCTC emit 的 slot 是否正确 */
        {
            int top = lua_gettop(L) - 1; /* 排除错误本身 */
            fprintf(stderr, "--- stack dump (excl err, top=%d) ---\n", top);
            for (int i = 1; i <= top && i <= 30; i++) {
                int t = lua_type(L, i);
                const char *tn = lua_typename(L, t);
                fprintf(stderr, "  slot %d: type=%s", i, tn);
                switch (t) {
                    case LUA_TSTRING:
                        fprintf(stderr, " val='%s'", lua_tostring(L, i));
                        break;
                    case LUA_TNUMBER:
                        fprintf(stderr, " val=%s", lua_tostring(L, i));
                        break;
                    case LUA_TBOOLEAN:
                        fprintf(stderr, " val=%s", lua_toboolean(L,i)?"true":"false");
                        break;
                    case LUA_TNIL:
                        break;
                    case LUA_TUSERDATA:
                        fprintf(stderr, " ptr=%p", lua_touserdata(L,i));
                        break;
                    case LUA_TTABLE: {
                        /* 尝试判断是否是对象（有 __vars）还是类（有 __methods） */
                        lua_pushstring(L, "__vars");
                        lua_rawget(L, i);
                        if (!lua_isnil(L, -1)) fprintf(stderr, " (object)");
                        lua_pop(L, 1);
                        lua_pushstring(L, "__methods");
                        lua_rawget(L, i);
                        if (!lua_isnil(L, -1)) fprintf(stderr, " (class)");
                        lua_pop(L, 1);
                        break;
                    }
                }
                fprintf(stderr, "\n");
            }
            fprintf(stderr, "--- stack dump end ---\n");
        }
        /* 输出 traceback */
        luaL_traceback(L, L, err ? err : "?", 0);
        fprintf(stderr, "TRACEBACK:\n%s\n", lua_tostring(L, -1));
        lua_pop(L, 1);
        rc = 2;
        lua_pop(L, 1);
    }

    /* 关闭 Lua 状态机 */
    lua_close(L);
    return rc;
}
