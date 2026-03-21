#include "plugin.h"

int plugin_dump(lua_State *L) {
    /* Basic placeholder for plugin binary dump */
    lua_pushstring(L, "mock_plugin_binary_data");
    return 1;
}
