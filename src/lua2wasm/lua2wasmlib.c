/**
 * @file lua2wasmlib.c
 * @brief lua2wasm Lua C 模块入口 — 在 Lua 中调用 Lua→WASM 编译器
 *
 * 提供以下 Lua API:
 *   local lua2wasm = require("lua2wasm")
 *   local wat = lua2wasm.compile(source)            -- 编译 Lua 源码为 WAT 文本
 *   local wasm = lua2wasm.compile_wasm(source)      -- 编译 Lua 源码为 WASM 二进制
 *   local wasm = lua2wasm.assemble(wat)             -- WAT 文本转 WASM 二进制
 *   local wasm = lua2wasm.assemble_ex(wat, no_dce)  -- WAT 转 WASM（可选关闭 DCE）
 *
 * 典型的端到端流程：
 *   1. lua2wasm.compile() 将 Lua 源码编译为 WAT
 *   2. lua2wasm.assemble() 将 WAT 汇编为 WASM 二进制
 *   3. wasm3 模块加载并执行 WASM 二进制
 *
 *   local lua2wasm = require("lua2wasm")
 *   local wasm3 = require("wasm3")
 *   local wasm_bytes = lua2wasm.compile_wasm("return 1 + 2")
 *   local env = wasm3.newEnvironment()
 *   local runtime = env:newRuntime()
 *   local module = env:parseModule(wasm_bytes)
 *   runtime:loadModule(module)
 */

#include "lexer.h"
#include "parser.h"
#include "codegen.h"
#include "wat2wasm.h"
#include "wat_builder.h"
#include "xalloc.h"

#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"

#include <stdlib.h>
#include <string.h>

/* ============================================================
 * 辅助函数：将 lua2wasm 编译管线的结果写到临时缓冲区
 * ============================================================ */

/**
 * @brief 编译 Lua 源码字符串，返回 WAT 文本
 * @param source Lua 源码（以 \0 结尾的 C 字符串）
 * @param out_wat 输出：WAT 文本的 malloc 缓冲区（调用者需 free）
 * @param errbuf 错误信息缓冲区
 * @param errcap 错误信息缓冲区大小
 * @return 0 成功，非 0 失败
 */
static int compile_source(const char *source, char **out_wat,
                          char *errbuf, size_t errcap) {
    *out_wat = NULL;

    /* 词法分析 */
    TokenList toks = lex(source);
    if (!toks.ok) {
        snprintf(errbuf, errcap, "lex error: %s", toks.err);
        return -1;
    }

    /* 语法分析 */
    NodePool pool;
    node_pool_init(&pool);
    ParseResult pr = parse(&toks, &pool);
    if (!pr.ok) {
        snprintf(errbuf, errcap, "parse error: %s", pr.error);
        parse_result_free(&pr);
        node_pool_free(&pool);
        tokenlist_free(&toks);
        return -1;
    }

    /* 代码生成 */
    WatBuilder w;
    wat_init(&w);
    char cg_err[256] = {0};
    if (!codegen_module(&pr, "input", 0, &w, cg_err, sizeof(cg_err))) {
        snprintf(errbuf, errcap, "codegen error: %s", cg_err);
        wat_free(&w);
        parse_result_free(&pr);
        node_pool_free(&pool);
        tokenlist_free(&toks);
        return -1;
    }

    /* 移交 WAT 缓冲区所有权给调用者 */
    *out_wat = w.buf;
    w.buf = NULL; /* 防止 wat_free 释放 */

    /* 清理 */
    wat_free(&w);
    parse_result_free(&pr);
    node_pool_free(&pool);
    tokenlist_free(&toks);

    return 0;
}

/**
 * @brief 将 WAT 文本汇编为 WASM 二进制
 * @param wat WAT 文本
 * @param wat_len WAT 文本长度（不含 \0）
 * @param dce 是否进行死代码消除（1=开，0=关）
 * @param out_bytes 输出：WASM 二进制缓冲区（调用者需 free）
 * @param out_len 输出：缓冲区长度
 * @param errbuf 错误信息缓冲区
 * @param errcap 错误信息缓冲区大小
 * @return 0 成功，非 0 失败
 */
static int assemble_wat(const char *wat, size_t wat_len, int dce,
                        uint8_t **out_bytes, size_t *out_len,
                        char *errbuf, size_t errcap) {
    return wat_assemble(wat, wat_len, dce, out_bytes, out_len, errbuf, errcap);
}

/* ============================================================
 * Lua C 函数
 * ============================================================ */

/**
 * @brief lua2wasm.compile(source) → WAT 文本
 * 将 Lua 源码字符串编译为 WAT (WebAssembly Text) 格式。
 * @param L Lua 状态机
 * @return 成功返回 WAT 字符串，失败抛出错误
 */
static int l_lua2wasm_compile(lua_State *L) {
    size_t src_len;
    const char *source = luaL_checklstring(L, 1, &src_len);

    /* 确保源码以 \0 结尾（lex 要求 C 字符串） */
    char *src_copy = (char *)xmalloc(src_len + 1);
    memcpy(src_copy, source, src_len);
    src_copy[src_len] = '\0';

    char *wat_out = NULL;
    char errbuf[512] = {0};

    int rc = compile_source(src_copy, &wat_out, errbuf, sizeof(errbuf));
    free(src_copy);

    if (rc != 0) {
        free(wat_out);
        return luaL_error(L, "%s", errbuf);
    }

    lua_pushstring(L, wat_out);
    free(wat_out);
    return 1;
}

/**
 * @brief lua2wasm.compile_wasm(source) → WASM 二进制
 * 将 Lua 源码直接编译为 WASM 二进制（含 WAT 汇编步骤）。
 * 相当于 compile() + assemble()。
 * @param L Lua 状态机
 * @return 成功返回 WASM 二进制字符串，失败抛出错误
 */
static int l_lua2wasm_compile_wasm(lua_State *L) {
    size_t src_len;
    const char *source = luaL_checklstring(L, 1, &src_len);

    char *src_copy = (char *)xmalloc(src_len + 1);
    memcpy(src_copy, source, src_len);
    src_copy[src_len] = '\0';

    char *wat_out = NULL;
    char errbuf[512] = {0};

    int rc = compile_source(src_copy, &wat_out, errbuf, sizeof(errbuf));
    free(src_copy);

    if (rc != 0) {
        free(wat_out);
        return luaL_error(L, "%s", errbuf);
    }

    /* 将 WAT 汇编为 WASM 二进制 */
    uint8_t *wasm_bytes = NULL;
    size_t wasm_len = 0;
    int asm_rc = assemble_wat(wat_out, strlen(wat_out), 1 /* dce=on */,
                              &wasm_bytes, &wasm_len,
                              errbuf, sizeof(errbuf));
    free(wat_out);

    if (asm_rc != 0) {
        free(wasm_bytes);
        return luaL_error(L, "assemble error: %s", errbuf);
    }

    lua_pushlstring(L, (const char *)wasm_bytes, wasm_len);
    free(wasm_bytes);
    return 1;
}

/**
 * @brief lua2wasm.assemble(wat, no_dce) → WASM 二进制
 * 将 WAT 文本汇编为 WASM 二进制字节。
 * @param L Lua 状态机
 *   - 参数 1: WAT 文本（字符串）
 *   - 参数 2: no_dce（可选布尔值，true 表示关闭死代码消除，默认 false=开启 DCE）
 * @return 成功返回 WASM 二进制字符串，失败抛出错误
 */
static int l_lua2wasm_assemble(lua_State *L) {
    size_t wat_len;
    const char *wat = luaL_checklstring(L, 1, &wat_len);
    int no_dce = 0;
    if (lua_gettop(L) >= 2 && !lua_isnil(L, 2)) {
        no_dce = lua_toboolean(L, 2);
    }

    uint8_t *wasm_bytes = NULL;
    size_t wasm_len = 0;
    char errbuf[512] = {0};

    int rc = assemble_wat(wat, wat_len, no_dce ? 0 : 1,
                          &wasm_bytes, &wasm_len,
                          errbuf, sizeof(errbuf));
    if (rc != 0) {
        free(wasm_bytes);
        return luaL_error(L, "assemble error: %s", errbuf);
    }

    lua_pushlstring(L, (const char *)wasm_bytes, wasm_len);
    free(wasm_bytes);
    return 1;
}

/* ============================================================
 * 模块注册
 * ============================================================ */

static const struct luaL_Reg lua2wasm_methods[] = {
    {"compile",       l_lua2wasm_compile},
    {"wcompile",  l_lua2wasm_compile_wasm},
    {"assemble",      l_lua2wasm_assemble},
    {NULL, NULL}
};

/**
 * @brief 模块入口：require("lua2wasm")
 * 被 lxclua 在启动时自动调用（通过 linit.c 注册）。
 * @param L Lua 状态机
 * @return 返回模块表
 */
int luaopen_lua2wasm(lua_State *L) {
    luaL_newlib(L, lua2wasm_methods);
    return 1;
}