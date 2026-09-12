/**
 * @file wmt_wasi.c
 * @brief wasmtime Lua 绑定 —— WASI 系统接口
 *
 * 让 WASM 模块访问宿主资源（文件、环境变量、标准 IO、网络）。
 * 使用 wasmtime v48 preview2 WASI 架构：
 *   1. wasmtime.newWasi{...} → wasi 配置对象（wmt_Wasi 包装 wasi_config_t*）
 *   2. store:setWasi(wasi)   → 配置应用到 store 上下文（所有权转移）
 *   3. linker:defineWasi()   → 注册 WASI imports 到 linker
 * 共享类型见 lwasmtime.h。
 */
#include "lwasmtime.h"

/**
 * @brief wasi 配置对象 __gc —— 若 config 尚未转移给 context 则释放
 */
static int wmt_wasi_gc(lua_State *L) {
    wmt_Wasi *ww = (wmt_Wasi*)luaL_checkudata(L, 1, WMT_WASI);
    if (ww->config) {
        wasi_config_delete(ww->config);
        ww->config = NULL;
    }
    return 0;
}

/**
 * @brief 解析 preopenDirs 表并逐项 preopen
 * 支持两种元素形态：
 *   { host=".", guest="/" }   或  { ".", "/" }   （host 在前，guest 在后）
 * @return 0 成功，-1 失败
 */
static int wmt_wasi_preopen(lua_State *L, int tbl_idx, wasi_config_t *cfg) {
    int n = (int)lua_rawlen(L, tbl_idx);
    for (int i = 0; i < n; i++) {
        lua_rawgeti(L, tbl_idx, i + 1);
        const char *host = NULL, *guest = NULL;
        if (lua_istable(L, -1)) {
            /* 表元素：支持字段形式或数组形式 */
            lua_getfield(L, -1, "host");
            if (lua_isstring(L, -1)) host = lua_tostring(L, -1);
            lua_pop(L, 1);
            lua_getfield(L, -1, "guest");
            if (lua_isstring(L, -1)) guest = lua_tostring(L, -1);
            lua_pop(L, 1);
            if (!host) {
                lua_rawgeti(L, -1, 1);
                if (lua_isstring(L, -1)) host = lua_tostring(L, -1);
                lua_pop(L, 1);
            }
            if (!guest) {
                lua_rawgeti(L, -1, 2);
                if (lua_isstring(L, -1)) guest = lua_tostring(L, -1);
                lua_pop(L, 1);
            }
        } else if (lua_isstring(L, -1)) {
            /* 单个字符串：host 与 guest 同名 */
            host = guest = lua_tostring(L, -1);
        }
        lua_pop(L, 1); /* 弹出元素 */

        if (!host || !guest) return -1;
        wasi_config_preopen_dir(cfg, host, guest, true);
    }
    return 0;
}

/**
 * @brief wasmtime.newWasi([config]) → wasi 配置对象
 *
 * config 可选表字段：
 *   argv        = { "prog", "arg1", ... }        程序参数
 *   env         = { "KEY=VALUE", ... }           环境变量
 *   inheritArgv = true                           继承宿主 argv
 *   inheritEnv  = true                           继承宿主环境变量
 *   stdin       = "data string"                  以字节作为 stdin
 *   inheritStdin / inheritStdout / inheritStderr = true  继承宿主标准流
 *   preopenDirs = { {host=".", guest="/"}, ... } 预打开目录
 *   network     = true                           继承宿主网络访问
 *
 * @return wasi userdata（需 store:setWasi + linker:defineWasi 使用）
 */
int l_new_wasi(lua_State *L) {
    wasi_config_t *cfg = wasi_config_new();
    if (!cfg) {
        return luaL_error(L, "newWasi: failed to create wasi config");
    }

    int failed = 0;
    if (lua_istable(L, 1)) {
        /* argv */
        lua_getfield(L, 1, "argv");
        if (lua_istable(L, -1)) {
            int n = (int)lua_rawlen(L, -1);
            if (n > 0) {
                const char **argv = (const char**)malloc((size_t)n * sizeof(char*));
                if (argv) {
                    int valid = 1;
                    for (int i = 0; i < n; i++) {
                        lua_rawgeti(L, -1, i + 1);
                        if (lua_isstring(L, -1)) argv[i] = lua_tostring(L, -1);
                        else valid = 0;
                        lua_pop(L, 1);
                    }
                    if (valid) wasi_config_set_argv(cfg, (size_t)n, argv);
                    else failed = 1;
                    free(argv);
                } else failed = 1;
            }
        }
        lua_pop(L, 1);
        if (failed) goto fail;

        /* env: 每项 "KEY=VALUE" */
        lua_getfield(L, 1, "env");
        if (lua_istable(L, -1)) {
            int n = (int)lua_rawlen(L, -1);
            if (n > 0) {
                const char **names = (const char**)malloc((size_t)n * sizeof(char*));
                const char **values = (const char**)malloc((size_t)n * sizeof(char*));
                if (names && values) {
                    int valid = 1, cnt = 0;
                    for (int i = 0; i < n; i++) {
                        lua_rawgeti(L, -1, i + 1);
                        const char *kv = lua_tostring(L, -1);
                        if (kv) {
                            const char *eq = strchr(kv, '=');
                            if (eq && eq != kv) {
                                /* 拷贝出 name 段（临时缓冲） */
                                size_t nlen = (size_t)(eq - kv);
                                char *nname = (char*)malloc(nlen + 1);
                                if (nname) {
                                    memcpy(nname, kv, nlen);
                                    nname[nlen] = '\0';
                                    names[cnt] = nname;
                                    values[cnt] = eq + 1;
                                    cnt++;
                                } else { valid = 0; }
                            } else valid = 0;
                        } else valid = 0;
                        lua_pop(L, 1);
                    }
                    if (valid && cnt > 0) {
                        wasi_config_set_env(cfg, (size_t)cnt, names, values);
                    } else failed = 1;
                    for (int i = 0; i < cnt; i++) free((char*)names[i]);
                    free(names); free(values);
                } else failed = 1;
            }
        }
        lua_pop(L, 1);
        if (failed) goto fail;

        /* inheritArgv / inheritEnv */
        lua_getfield(L, 1, "inheritArgv");
        if (lua_toboolean(L, -1)) wasi_config_inherit_argv(cfg);
        lua_pop(L, 1);

        lua_getfield(L, 1, "inheritEnv");
        if (lua_toboolean(L, -1)) wasi_config_inherit_env(cfg);
        lua_pop(L, 1);

        /* stdin: 字符串字节 */
        lua_getfield(L, 1, "stdin");
        if (lua_isstring(L, -1)) {
            size_t slen;
            const char *sdata = lua_tolstring(L, -1, &slen);
            wasm_byte_vec_t bin;
            wasm_byte_vec_new_uninitialized(&bin, slen);
            if (slen > 0 && bin.data) memcpy(bin.data, sdata, slen);
            wasi_config_set_stdin_bytes(cfg, &bin);
            /* set_stdin_bytes 拷贝数据，vec 可立即释放 */
            wasm_byte_vec_delete(&bin);
        }
        lua_pop(L, 1);

        /* 标准流继承 */
        lua_getfield(L, 1, "inheritStdin");
        if (lua_toboolean(L, -1)) wasi_config_inherit_stdin(cfg);
        lua_pop(L, 1);
        lua_getfield(L, 1, "inheritStdout");
        if (lua_toboolean(L, -1)) wasi_config_inherit_stdout(cfg);
        lua_pop(L, 1);
        lua_getfield(L, 1, "inheritStderr");
        if (lua_toboolean(L, -1)) wasi_config_inherit_stderr(cfg);
        lua_pop(L, 1);

        /* 标准流重定向到宿主文件（覆盖同名继承选项） */
        lua_getfield(L, 1, "stdinFile");
        if (lua_isstring(L, -1)) {
            if (!wasi_config_set_stdin_file(cfg, lua_tostring(L, -1))) failed = 1;
        }
        lua_pop(L, 1);
        lua_getfield(L, 1, "stdoutFile");
        if (lua_isstring(L, -1)) {
            if (!wasi_config_set_stdout_file(cfg, lua_tostring(L, -1))) failed = 1;
        }
        lua_pop(L, 1);
        lua_getfield(L, 1, "stderrFile");
        if (lua_isstring(L, -1)) {
            if (!wasi_config_set_stderr_file(cfg, lua_tostring(L, -1))) failed = 1;
        }
        lua_pop(L, 1);
        if (failed) goto fail;

        /* preopenDirs */
        lua_getfield(L, 1, "preopenDirs");
        if (lua_istable(L, -1)) {
            if (wmt_wasi_preopen(L, -1, cfg) != 0) failed = 1;
        }
        lua_pop(L, 1);
        if (failed) goto fail;

        /* network */
        lua_getfield(L, 1, "network");
        if (lua_toboolean(L, -1)) {
            wasi_config_inherit_network(cfg);
            wasi_config_allow_ip_name_lookup(cfg, true);
        }
        lua_pop(L, 1);
    }

    wmt_Wasi *ww = (wmt_Wasi*)lua_newuserdata(L, sizeof(wmt_Wasi));
    ww->config = cfg;
    luaL_getmetatable(L, WMT_WASI);
    lua_setmetatable(L, -2);
    return 1;

fail:
    wasi_config_delete(cfg);
    return luaL_error(L, "newWasi: invalid configuration");
}

/* ============================================================
 * 方法表
 * ============================================================ */

const struct luaL_Reg wmt_wasi_methods[] = {
    {"__gc", wmt_wasi_gc},
    {NULL, NULL}
};
