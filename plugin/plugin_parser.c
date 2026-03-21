#include "plugin.h"

int plugin_parse(lua_State *L) {
    const char *data = luaL_checkstring(L, 1);
    /* Basic placeholder parser for custom `.plugin` language */
    lua_pushstring(L, "Parsed plugin data (mock)");
    return 1;
}
