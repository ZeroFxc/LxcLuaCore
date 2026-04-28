/*
** test_obfuscate.c - 直接测试混淆器
*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"
#include "lobfuscate.h"

/* 使用 debug.getinfo 获取 Proto */
static int run_obf_test(lua_State *L, const char *code, const char *name, int expected) {
    printf("--- 测试: %s ---\n", name);
    
    /* 创建 wrapper: 加载代码然后调用 debug.getinfo 获取 proto */
    const char *wrapper = 
        "local f = function() %s end\n"
        "local info = debug.getinfo(f, 'S')\n"
        "return f, info.what\n";
    
    char buf[8192];
    snprintf(buf, sizeof(buf), wrapper, code);
    
    if (luaL_loadstring(L, buf) != LUA_OK) {
        printf("  ✗ wrapper加载失败: %s\n", lua_tostring(L, -1));
        lua_pop(L, 1);
        return 0;
    }
    
    if (lua_pcall(L, 0, 2, 0) != LUA_OK) {
        printf("  ✗ wrapper执行失败: %s\n", lua_tostring(L, -1));
        lua_pop(L, 1);
        return 0;
    }
    
    const char *what = lua_tostring(L, -1);
    printf("  what=%s\n", what ? what : "null");
    
    if (!what || (what[0] != 'L' && what[0] != 'm')) {
        printf("  ✗ 不是Lua函数\n");
        lua_pop(L, 2);
        return 0;
    }
    
    /* 获取 Proto 指针 */
    const void *ptr = lua_topointer(L, -2);
    printf("  closure ptr=%p\n", ptr);
    
    if (!ptr) {
        printf("  ✗ 无法获取closure\n");
        lua_pop(L, 2);
        return 0;
    }
    
    /* 通过lua_upvaluejoin hack 获取proto? 不, 直接用debug.getinfo */
    lua_Debug ar;
    memset(&ar, 0, sizeof(ar));
    
    /* 用 ">S" 获取函数自身的信息 */
    lua_pushvalue(L, -2); /* push function */
    if (lua_getinfo(L, ">Su", &ar) == 0) {
        printf("  ✗ getinfo失败\n");
        lua_pop(L, 3);
        return 0;
    }
    printf("  source=%s, linedefined=%d, nups=%d\n", 
           ar.source ? ar.source : "null", ar.linedefined, ar.nups);
    lua_pop(L, 1); /* pop func */
    
    /* 我们需要直接获取Proto, 但是lua API不暴露它 */
    /* 让我们尝试另一个方法: 直接dump bytecode */
    
    /* 或者, 更简单的方法: 使用 luac 编译代码得到 .luac, 然后用我们的加载器 */
    
    /* 最简单的方案: 让 luaO_flatten 通过lua的debug hook来运行 */
    /* 实际上, 让我们用luaL_loadstring然后直接执行 */
    
    lua_pop(L, 2); /* pop function and what */
    
    /* 现在重新加载原始代码 */
    if (luaL_loadstring(L, code) != LUA_OK) {
        printf("  ✗ 重新加载失败: %s\n", lua_tostring(L, -1));
        lua_pop(L, 1);
        return 0;
    }
    
    /* 使用 debug.getproto 如果可用 */
    lua_pushliteral(L, "debug");
    lua_gettable(L, LUA_REGISTRYINDEX);
    if (lua_isnil(L, -1)) {
        lua_pop(L, 1);
        lua_getglobal(L, "debug");
    }
    
    lua_pushliteral(L, "getproto");
    lua_gettable(L, -2);
    
    if (lua_isnil(L, -1)) {
        printf("  debug.getproto不可用, 使用内部API\n");
        lua_pop(L, 3);
        
        /* 直接使用lua_getinfo获取 */
        lua_pushvalue(L, -1);
        
        /* 通过lua的debug API获取proto */
        /* Lua 5.4中可以通过 lua_getinfo(L, ">S", &ar) 获取 */
        lua_Debug info;
        if (lua_getinfo(L, ">S", &info)) {
            /* 这里我们得不到直接的proto指针, 需要别的方法 */
        }
        lua_pop(L, 1);
        
        printf("  ✗ 无法获取Proto\n");
        return 0;
    }
    
    /* debug.getproto(func, index) */
    lua_pop(L, 2); /* remove debug table and getproto */
    lua_pushvalue(L, -2); /* push function */
    lua_pushinteger(L, 0); /* first proto */
    
    if (lua_pcall(L, 2, 1, 0) != LUA_OK) {
        printf("  ✗ debug.getproto失败: %s\n", lua_tostring(L, -1));
        lua_pop(L, 2);
        return 0;
    }
    
    Proto *p = (Proto *)lua_topointer(L, -1);
    printf("  Proto ptr=%p, sizecode=%d\n", p, p ? p->sizecode : 0);
    
    if (!p || p->sizecode < 4) {
        printf("  - 跳过(代码太短或无效)\n");
        lua_pop(L, 2);
        return 1;
    }
    
    int orig_size = p->sizecode;
    
    /* 应用混淆 */
    int flags = OBFUSCATE_CFF | OBFUSCATE_BLOCK_SHUFFLE | OBFUSCATE_BOGUS_BLOCKS | OBFUSCATE_RANDOM_NOP;
    unsigned int seed = 12345;
    
    printf("  开始混淆 (flags=0x%x, orig=%d)...\n", flags, orig_size);
    fflush(stdout);
    
    int result = luaO_flatten(L, p, flags, seed, "cff_debug.log");
    if (result != 0) {
        printf("  ✗ 混淆失败! 返回码=%d\n", result);
        lua_pop(L, 2);
        return 0;
    }
    
    printf("  混淆后指令数: %d\n", p->sizecode);
    
    /* 执行混淆后的函数 */
    printf("  执行中...\n");
    fflush(stdout);
    
    int call_result = lua_pcall(L, 0, 1, 0);
    if (call_result != LUA_OK) {
        printf("  ✗ 执行失败! 错误: %s\n", lua_tostring(L, -1));
        lua_pop(L, 1);
        return 0;
    }
    
    int actual = (int)lua_tointeger(L, -1);
    lua_pop(L, 1);
    
    if (actual == expected) {
        printf("  ✓ 通过! 结果=%d\n", actual);
        return 1;
    } else {
        printf("  ✗ 失败! 期望=%d, 实际=%d\n", expected, actual);
        return 0;
    }
}

int main(int argc, char **argv) {
    lua_State *L = luaL_newstate();
    if (!L) {
        fprintf(stderr, "无法创建Lua状态\n");
        return 1;
    }
    luaL_openlibs(L);
    
    printf("=== 混淆器正确性测试 ===\n\n");
    
    int passed = 0, failed = 0;
    
    /* 测试1: 简单加法 */
    if (run_obf_test(L,
        "return 3 + 5",
        "简单加法", 8))
        passed++; else failed++;
    
    /* 测试2: 函数调用 */
    if (run_obf_test(L,
        "local function add(a, b) return a + b end return add(3, 5)",
        "函数调用", 8))
        passed++; else failed++;
    
    /* 测试3: 条件分支 */
    if (run_obf_test(L,
        "local a, b = 10, 20; if a > b then return a else return b end",
        "条件分支", 20))
        passed++; else failed++;
    
    /* 测试4: for循环 */
    if (run_obf_test(L,
        "local sum = 0; for i = 1, 10 do sum = sum + i; end; return sum",
        "for循环", 55))
        passed++; else failed++;
    
    printf("\n=== 结果: 通过=%d, 失败=%d ===\n", passed, failed);
    
    lua_close(L);
    return failed > 0 ? 1 : 0;
}
