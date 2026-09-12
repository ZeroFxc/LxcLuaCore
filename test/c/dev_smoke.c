/*
 * dev_smoke.c - LXCLUA 聚合头 smoke 测试
 * 仅 #include "lxclua.h"，验证头文件完整性
 */
#include "lxclua.h"
#include <stdio.h>

/* wasm 相关模块的空 stub（smoke test 不加载这些模块，仅提供链接符号） */
/* 函数签名: lua_CFunction = int (*)(lua_State *) */
int luaopen_wasm3(lua_State *L) { (void)L; return 0; }
int luaopen_wasmtime(lua_State *L) { (void)L; return 0; }

/* 简单内存分配器，使用默认 malloc/free */
static void *smoke_alloc(void *ud, void *ptr, size_t osize, size_t nsize) {
    (void)ud; (void)osize;
    if (nsize == 0) { free(ptr); return NULL; }
    return realloc(ptr, nsize);
}

/*
 * main: 创建 lua_State -> 打开标准库 -> 执行 print('smoke OK') -> 关闭
 * 参数: argc/argv 未使用
 * 返回: 0 成功，非 0 失败
 */
int main(int argc, char **argv) {
    (void)argc; (void)argv;
    int rc = 0;
    lua_State *L = lua_newstate(smoke_alloc, NULL, 0x12345678u);
    if (!L) {
        fprintf(stderr, "smoke FAIL: lua_newstate returned NULL\n");
        return 1;
    }
    luaL_openlibs(L);
    if (luaL_dostring(L, "print('smoke OK')") != LUA_OK) {
        const char *err = lua_tostring(L, -1);
        fprintf(stderr, "smoke FAIL: dostring error: %s\n", err ? err : "(null)");
        rc = 2;
        lua_pop(L, 1);
    }
    lua_close(L);
    return rc;
}
