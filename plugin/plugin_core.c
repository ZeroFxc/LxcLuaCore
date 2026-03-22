#include "plugin.h"

static const luaL_Reg plugin_funcs[] = {
    {"install", plugin_install},
    {"parse", plugin_parse},
    {"load", plugin_load},
    {"dump", plugin_dump},
    {"undump", plugin_undump},
    {"shop_connect", plugin_shop_connect},
    {NULL, NULL}
};

/*
** Plugin initialization
*/
int luaopen_plugin(lua_State *L) {
    luaL_newlib(L, plugin_funcs);
    return 1;
}
