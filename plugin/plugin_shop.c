#include "plugin.h"

int plugin_shop_connect(lua_State *L) {
    const char *url = luaL_optstring(L, 1, "https://shop.lxclua.org/index.json");

    lua_getglobal(L, "require");
    lua_pushstring(L, "http");
    if (lua_pcall(L, 1, 1, 0) == LUA_OK) {
        lua_getfield(L, -1, "get");
        lua_pushstring(L, url);

        if (lua_pcall(L, 1, 2, 0) == LUA_OK) {
            if (lua_isnumber(L, -2) && lua_isstring(L, -1)) {
                int status = lua_tointeger(L, -2);
                if (status >= 200 && status < 300) {
                    lua_pushstring(L, lua_tostring(L, -1));
                    return 1;
                } else {
                    lua_pushfstring(L, "{\"error\": \"HTTP Error: %d\"}", status);
                    return 1;
                }
            } else if (lua_isnil(L, -2) && lua_isstring(L, -1)) {
                lua_pushfstring(L, "{\"error\": \"HTTP Request failed: %s\"}", lua_tostring(L, -1));
                return 1;
            } else {
                lua_pushfstring(L, "{\"error\": \"Invalid response from http.get\"}");
                return 1;
            }
        } else {
            lua_pushfstring(L, "{\"error\": \"failed to execute http request: %s\"}", lua_tostring(L, -1));
            return 1;
        }
    } else {
        lua_pushfstring(L, "{\"error\": \"failed to load http module: %s\"}", lua_tostring(L, -1));
        return 1;
    }

    lua_pushstring(L, "{\"plugins\": []}");
    return 1;
}
