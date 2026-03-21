#include "plugin.h"

int plugin_load(lua_State *L) {
    const char *path = luaL_checkstring(L, 1);
    /* Basic placeholder loader logic */
    /* Checks if .lua or .plugin and runs appropriate parser/loader */
    lua_pushfstring(L, "Loaded plugin from path: %s", path);
    return 1;
}
