#include <stdio.h>
#include <stdlib.h>
#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"
#include "lstate.h"
#include "translate.h"

int main(int argc, char *argv[]) {
    if (argc != 3) {
        fprintf(stderr, "Usage: %s <input.lua> <output.lua>\n", argv[0]);
        return 1;
    }

    const char *infile = argv[1];
    const char *outfile = argv[2];

    lua_State *L = luaL_newstate();
    if (!L) {
        fprintf(stderr, "Failed to create Lua state\n");
        return 1;
    }

    luaL_openlibs(L);

    // Load the script. This parses the Lua code and leaves a closure on the stack.
    if (luaL_loadfile(L, infile) != 0) {
        fprintf(stderr, "Failed to load file: %s\n", lua_tostring(L, -1));
        lua_close(L);
        return 1;
    }

    // A loaded chunk is represented as a closure on the stack
    const Closure *c = (const Closure *)lua_topointer(L, -1);
    if (!c || !c->l.p) {
        fprintf(stderr, "Failed to extract prototype from the loaded chunk\n");
        lua_close(L);
        return 1;
    }

    Proto *f = c->l.p;

    // Translate the prototype
    if (translate_and_dump(f, outfile) != 0) {
        fprintf(stderr, "Failed to translate and dump the prototype\n");
        lua_close(L);
        return 1;
    }

    lua_close(L);
    return 0;
}
