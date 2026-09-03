#include "lxclua.h"
#include <stdio.h>

int main() {
    lua_State *L = luaL_newstate();
    luaL_openlibs(L);
    
    // Run the struct definition
    const char *code =
        "struct Point { x: 0, y: 0 }\n"
        "local p = Point{x = 10, y = 20}\n"
        "print('before setfield, p.x =', p.x)\n"
        "p.x = 100\n"
        "print('after setfield, p.x =', p.x)\n";
    
    if (luaL_dostring(L, code) != LUA_OK) {
        printf("Error: %s\n", lua_tostring(L, -1));
    }
    
    lua_close(L);
    return 0;
}
