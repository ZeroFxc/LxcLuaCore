#include "plugin.h"

int plugin_shop_connect(lua_State *L) {
    const char *url = luaL_optstring(L, 1, "https://shop.lxclua.org");
    /* Real logic would do an HTTP request to the repository index. */
    lua_pushfstring(L, "Connected to shop at %s", url);
    return 1;
}
