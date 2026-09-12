/*
 * lbctc_runner2.c - LBCTC 转译 C 代码的运行器 (修正版)
 * 生成的 C 代码入口函数为 luaopen_0 (而非 luaopen_module)
 */
#include "lxclua.h"
#include <stdio.h>
#include <stdlib.h>

/* wasm stubs */
int luaopen_wasm3(lua_State *L) { (void)L; return 0; }
int luaopen_wasmtime(lua_State *L) { (void)L; return 0; }

/* 缺失的扩展 API 声明（库中有定义但头文件未导出） */
LUA_API void lua_extendiface(lua_State *L, int child_idx, int parent_idx);

/* 转译后生成的模块入口函数声明 */
extern int luaopen_0(lua_State *L);

int main(int argc, char **argv) {
    (void)argc; (void)argv;
    int rc = 0;

    lua_State *L = luaL_newstate();
    if (!L) {
        fprintf(stderr, "runner FAIL: luaL_newstate returned NULL\n");
        return 1;
    }

    luaL_openlibs(L);

    lua_pushcfunction(L, luaopen_0);

    if (lua_pcall(L, 0, LUA_MULTRET, 0) != LUA_OK) {
        const char *err = lua_tostring(L, -1);
        fprintf(stderr, "runner FAIL: lua_pcall error: %s\n", err ? err : "(null)");
        {
            int top = lua_gettop(L) - 1;
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
        luaL_traceback(L, L, err ? err : "?", 0);
        fprintf(stderr, "TRACEBACK:\n%s\n", lua_tostring(L, -1));
        lua_pop(L, 1);
        rc = 2;
        lua_pop(L, 1);
    }

    lua_close(L);
    return rc;
}
