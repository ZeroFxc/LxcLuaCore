#include "plugin.h"
#include <string.h>
#include <stdio.h>
#include <ctype.h>

/*
** A simple parser for a `.plugin` file format:
** plugin "name" {
**    version = "1.0.0",
** }
*/

/* Helper to skip whitespace */
static const char *skip_ws(const char *s) {
    while (*s && isspace((unsigned char)*s)) {
        s++;
    }
    return s;
}

/* Helper to parse a quoted string */
static const char *parse_string(const char *s, char *out, size_t out_len) {
    if (*s != '\"') return s;
    s++;
    size_t i = 0;
    while (*s && *s != '\"') {
        if (i < out_len - 1) {
            out[i++] = *s;
        }
        s++;
    }
    out[i] = '\0';
    if (*s == '\"') s++;
    return s;
}

int plugin_parse(lua_State *L) {
    const char *data = luaL_checkstring(L, 1);

    char name[64] = {0};

    const char *p = skip_ws(data);

    /* Create metadata table */
    lua_newtable(L);

    /* Parse `plugin "name"` */
    if (strncmp(p, "plugin", 6) == 0) {
        p = skip_ws(p + 6);
        p = parse_string(p, name, sizeof(name));
        p = skip_ws(p);

        if (name[0] != '\0') {
            lua_pushstring(L, name);
            lua_setfield(L, -2, "name");
        }

        /* Parse `{ ... }` block */
        if (*p == '{') {
            p = skip_ws(p + 1);
            while (*p && *p != '}') {
                /* Parse key */
                char key[64] = {0};
                size_t k_len = 0;
                while (*p && !isspace((unsigned char)*p) && *p != '=' && *p != '}') {
                    if (k_len < sizeof(key) - 1) {
                        key[k_len++] = *p;
                    }
                    p++;
                }
                key[k_len] = '\0';
                p = skip_ws(p);

                if (*p == '=') {
                    p = skip_ws(p + 1);
                    char value[256] = {0};
                    p = parse_string(p, value, sizeof(value));

                    if (key[0] != '\0' && value[0] != '\0') {
                        lua_pushstring(L, value);
                        lua_setfield(L, -2, key);
                    }
                }

                p = skip_ws(p);
                if (*p == ',') {
                    p = skip_ws(p + 1);
                }
            }
            if (*p == '}') {
                p++; /* Skip '}' */
            }
        }
    }

    /* Push remaining data as the second return value (Lua script body) */
    lua_pushstring(L, p);

    return 2; /* Return table, and the remaining lua code */
}
