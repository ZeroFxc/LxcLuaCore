#include <stdio.h>
#include <stdlib.h>
#include "lua.h"
#include "lualib.h"
#include "lauxlib.h"

extern int g_is_illegal_environment;

int main(int argc, char **argv) {
    if (argc < 2) {
        printf("Usage: %s <bytecode_file> [simulate_hook_flag]\\n", argv[0]);
        return 1;
    }

    if (argc == 3) {
        g_is_illegal_environment = 1;
        printf(">>> [Test] Simulating illegal environment (e.g. memory dumper injected)\\n");
    } else {
        printf(">>> [Test] Simulating normal, safe environment\\n");
    }

    lua_State *L = luaL_newstate();
    luaL_openlibs(L);

    printf(">>> Loading bytecode %s...\\n", argv[1]);
    if (luaL_loadfile(L, argv[1]) != LUA_OK) {
        printf("Error loading file: %s\\n", lua_tostring(L, -1));
        return 1;
    }

    printf(">>> Executing bytecode...\\n");
    if (lua_pcall(L, 0, 0, 0) != LUA_OK) {
        printf("Error executing file: %s\\n", lua_tostring(L, -1));
        return 1;
    }

    lua_close(L);
    printf(">>> Finished successfully.\\n");
    return 0;
}
