#include "plugin.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#ifdef _WIN32
#include <direct.h>
#define MKDIR(path) _mkdir(path)
#else
#include <sys/stat.h>
#include <sys/types.h>
#define MKDIR(path) mkdir(path, 0755)
#endif

/* Utility to get the plugin install directory */
void get_plugin_dir(char *buffer, size_t size) {
#if defined(_WIN32)
    const char *appdata = getenv("APPDATA");
    if (appdata) {
        snprintf(buffer, size, "%s\\lxclua\\plugins\\", appdata);
    } else {
        snprintf(buffer, size, "C:\\lxclua\\plugins\\");
    }
#else
    const char *home = getenv("HOME");
    if (home) {
        snprintf(buffer, size, "%s/.lxclua/plugins/", home);
    } else {
        snprintf(buffer, size, "/usr/local/lib/lxclua/plugins/");
    }
#endif
}

/* Recursive mkdir */
static void recursive_mkdir(const char *dir) {
    char tmp[512];
    char *p = NULL;
    size_t len;

    snprintf(tmp, sizeof(tmp), "%s", dir);
    len = strlen(tmp);
    if (tmp[len - 1] == '/' || tmp[len - 1] == '\\') {
        tmp[len - 1] = 0;
    }

    for (p = tmp + 1; *p; p++) {
        if (*p == '/' || *p == '\\') {
            char sep = *p;
            *p = 0;
            MKDIR(tmp);
            *p = sep;
        }
    }
    MKDIR(tmp);
}

int plugin_install(lua_State *L) {
    const char *pkg_name = luaL_checkstring(L, 1);

    printf("Starting installation for package: %s\n", pkg_name);

    char plugin_dir[512];
    get_plugin_dir(plugin_dir, sizeof(plugin_dir));
    recursive_mkdir(plugin_dir);

    /* Check if the package name is a local file path */
    FILE *local_file = fopen(pkg_name, "rb");
    if (local_file) {
        printf("Found local file, copying to plugin directory...\n");

        /* Get the filename from the path */
        const char *filename = pkg_name;
        const char *p = pkg_name;
        while (*p) {
            if (*p == '/' || *p == '\\') {
                filename = p + 1;
            }
            p++;
        }

        char filepath[1024];
        snprintf(filepath, sizeof(filepath), "%s%s", plugin_dir, filename);

        FILE *dest_file = fopen(filepath, "wb");
        if (dest_file) {
            char buffer[4096];
            size_t bytes;
            while ((bytes = fread(buffer, 1, sizeof(buffer), local_file)) > 0) {
                fwrite(buffer, 1, bytes, dest_file);
            }
            fclose(dest_file);
            fclose(local_file);
            printf("Local file %s installed successfully to %s\n", pkg_name, filepath);
            lua_pushboolean(L, 1);
            return 1;
        } else {
            printf("Failed to write to %s\n", filepath);
            fclose(local_file);
            lua_pushboolean(L, 0);
            return 1;
        }
    }

    char filepath[1024];
    snprintf(filepath, sizeof(filepath), "%s%s.plugin", plugin_dir, pkg_name);

    /* Safely fetch using Lua's http module */
    lua_getglobal(L, "require");
    lua_pushstring(L, "http");
    if (lua_pcall(L, 1, 1, 0) == LUA_OK) {
        lua_getfield(L, -1, "get");
        char url[512];
        snprintf(url, sizeof(url), "https://shop.lxclua.org/pkg/%s.plugin", pkg_name);
        lua_pushstring(L, url);

        if (lua_pcall(L, 1, 2, 0) == LUA_OK) {
            /* http.get returns status_code, body */
            if (lua_isnumber(L, -2) && lua_isstring(L, -1)) {
                int status = lua_tointeger(L, -2);
                if (status >= 200 && status < 300) {
                    size_t len;
                    const char *body = lua_tolstring(L, -1, &len);

                    FILE *fp = fopen(filepath, "wb");
                    if (fp) {
                        fwrite(body, 1, len, fp);
                        fclose(fp);
                        printf("Package %s installed successfully to %s\n", pkg_name, filepath);
                        lua_pop(L, 2); /* pop status_code, body */
                        lua_pushboolean(L, 1);
                        return 1;
                    } else {
                        printf("Failed to write to %s\n", filepath);
                    }
                } else {
                    printf("HTTP Error: %d\n", status);
                }
            } else if (lua_isnil(L, -2) && lua_isstring(L, -1)) {
                printf("HTTP Request failed: %s\n", lua_tostring(L, -1));
            } else {
                printf("Invalid response from http.get\n");
            }
            lua_pop(L, 2); /* pop status_code, body */
        } else {
            printf("HTTP Request failed: %s\n", lua_tostring(L, -1));
        }
    } else {
        printf("Failed to load http module: %s\n", lua_tostring(L, -1));
    }

    printf("Failed to install package: %s\n", pkg_name);
    lua_pushboolean(L, 0);
    return 1;
}
