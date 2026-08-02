/*
** lastlib.c - AST 节点增删改查库
** 操作 Lua table 格式的 AST（astparser 序列化后的表示）
*/

#define lastlib_c
#define LUA_LIB

#include <string.h>
#include <stdlib.h>

#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"

#include "last_unparse.h"
#include "last_parse.h"     /* luaY_parse_ast */
#include "last_serialize.h" /* ast_serialize_to_lua / ast_deserialize_from_lua */
#include "lparser.h"        /* Dyndata */
#include "lzio.h"           /* Mbuffer / luaZ_initbuffer / luaZ_freebuffer */
#include "lmem.h"           /* luaM_newvector / luaM_free (由 lauxlib.h / lua.h 间接 include 的也可) */
#include "lcodegen.h"       /* luaY_codegen_chunk */
#include "lfunc.h"          /* luaF_newLclosure / luaF_initupvals */
#include "lstate.h"         /* LClosure */

/* astparser_runner CClosure 回调（定义于 lparser.c），用于包装 inputmode=ast 生成的 Proto* */
extern int astparser_runner(lua_State *L);

/* AST 子字段名称列表 - ast.find 和 ast.walk 遍历这些字段 */
static const char *child_fields[] = {
    "body", "arms", "cases", "else_body", "catch_body", "finally_body",
    "default_body", "expr", "cond", "values", "targets", "params",
    "callee", "args", "key", "value", "lhs", "rhs", "operand",
    "table", "recv", "entries", "start", "end", "step",
    "var", "vars", "exprs", NULL
};

/*
** 创建节点辅助函数
** kind: 节点类型字符串
** line: 行号
** fields_idx: fields 表的栈索引（可选，传入0表示无fields）
** 返回一个新表，栈顶为结果表
*/
static int create_node(lua_State *L, const char *kind, int line, int fields_idx) {
    lua_newtable(L);  /* 结果表 */

    lua_pushstring(L, kind);
    lua_setfield(L, -2, "kind");

    lua_pushinteger(L, line);
    lua_setfield(L, -2, "line");

    /* 拷贝 fields 表中的键值对 */
    if (fields_idx > 0 && lua_istable(L, fields_idx)) {
        lua_pushnil(L);
        while (lua_next(L, fields_idx)) {
            /* key 在 -2, value 在 -1 */
            lua_pushvalue(L, -2);
            lua_pushvalue(L, -2);
            lua_settable(L, -5);
            lua_pop(L, 1);
        }
    }

    return 1;
}

/*
** ast.stmt(kind, line, fields) -> table
** 创建语句节点
*/
static int ast_stmt(lua_State *L) {
    const char *kind = luaL_checkstring(L, 1);
    int line = (int)luaL_checkinteger(L, 2);
    return create_node(L, kind, line, lua_gettop(L) >= 3 ? 3 : 0);
}

/*
** ast.expr(kind, line, fields) -> table
** 创建表达式节点
*/
static int ast_expr(lua_State *L) {
    const char *kind = luaL_checkstring(L, 1);
    int line = (int)luaL_checkinteger(L, 2);
    return create_node(L, kind, line, lua_gettop(L) >= 3 ? 3 : 0);
}

/*
** 递归查找辅助函数
** node_idx: 当前节点的绝对栈索引
** kind: 目标节点类型
** result_idx: 结果数组的绝对栈索引
*/
static void find_rec(lua_State *L, int node_idx, const char *kind, int result_idx) {
    /* 检查当前节点是否匹配 */
    lua_getfield(L, node_idx, "kind");
    if (lua_isstring(L, -1)) {
        if (strcmp(lua_tostring(L, -1), kind) == 0) {
            lua_pushvalue(L, node_idx);
            lua_rawseti(L, result_idx, luaL_len(L, result_idx) + 1);
        }
    }
    lua_pop(L, 1);

    /* 遍历子字段 */
    int i;
    for (i = 0; child_fields[i] != NULL; i++) {
        lua_getfield(L, node_idx, child_fields[i]);
        if (!lua_istable(L, -1)) {
            lua_pop(L, 1);
            continue;
        }
        int child_idx = lua_gettop(L);
        lua_getfield(L, child_idx, "kind");
        if (!lua_isnil(L, -1)) {
            /* 单个节点（有 kind 字段） */
            lua_pop(L, 1);
            find_rec(L, child_idx, kind, result_idx);
        } else {
            lua_pop(L, 1);
            /* 数组（无 kind 字段，按整数索引遍历） */
            int len = (int)luaL_len(L, child_idx);
            int j;
            for (j = 1; j <= len; j++) {
                lua_rawgeti(L, child_idx, j);
                if (lua_istable(L, -1)) {
                    find_rec(L, lua_gettop(L), kind, result_idx);
                }
                lua_pop(L, 1);
            }
        }
        lua_pop(L, 1);  /* 弹出子表 */
    }
}

/*
** ast.find(node, kind) -> table
** 递归查找匹配 kind 的子节点，返回所有匹配节点列表
*/
static int ast_find(lua_State *L) {
    luaL_checktype(L, 1, LUA_TTABLE);
    const char *kind = luaL_checkstring(L, 2);

    lua_newtable(L);  /* 结果数组 */
    int result_idx = lua_gettop(L);

    find_rec(L, 1, kind, result_idx);

    return 1;
}

/*
** 递归遍历辅助函数
** node_idx: 当前节点的绝对栈索引
** cb_ref: callback 在注册表中的引用
** depth: 当前深度
*/
static void walk_rec(lua_State *L, int node_idx, int cb_ref, int depth) {
    /* 调用 callback(node, depth) */
    lua_rawgeti(L, LUA_REGISTRYINDEX, cb_ref);
    lua_pushvalue(L, node_idx);
    lua_pushinteger(L, depth);
    lua_call(L, 2, 0);

    /* 遍历子字段 */
    int i;
    for (i = 0; child_fields[i] != NULL; i++) {
        lua_getfield(L, node_idx, child_fields[i]);
        if (!lua_istable(L, -1)) {
            lua_pop(L, 1);
            continue;
        }
        int child_idx = lua_gettop(L);
        lua_getfield(L, child_idx, "kind");
        if (!lua_isnil(L, -1)) {
            /* 单个节点 */
            lua_pop(L, 1);
            walk_rec(L, child_idx, cb_ref, depth + 1);
        } else {
            lua_pop(L, 1);
            /* 数组 */
            int len = (int)luaL_len(L, child_idx);
            int j;
            for (j = 1; j <= len; j++) {
                lua_rawgeti(L, child_idx, j);
                if (lua_istable(L, -1)) {
                    walk_rec(L, lua_gettop(L), cb_ref, depth + 1);
                }
                lua_pop(L, 1);
            }
        }
        lua_pop(L, 1);  /* 弹出子表 */
    }
}

/*
** ast.walk(node, callback)
** 深度优先遍历 AST，callback 接收 (node, depth) 参数
*/
static int ast_walk(lua_State *L) {
    luaL_checktype(L, 1, LUA_TTABLE);
    luaL_checktype(L, 2, LUA_TFUNCTION);

    lua_pushvalue(L, 2);
    int cb_ref = luaL_ref(L, LUA_REGISTRYINDEX);

    walk_rec(L, 1, cb_ref, 0);

    luaL_unref(L, LUA_REGISTRYINDEX, cb_ref);
    return 0;
}

/*
** ast.set_field(node, key, value)
** 修改节点字段：node[key] = value
*/
static int ast_set_field(lua_State *L) {
    luaL_checktype(L, 1, LUA_TTABLE);
    lua_pushvalue(L, 2);
    lua_pushvalue(L, 3);
    lua_settable(L, 1);
    return 0;
}

/*
** ast.get_field(node, key) -> value
** 读取节点字段：return node[key]
*/
static int ast_get_field(lua_State *L) {
    luaL_checktype(L, 1, LUA_TTABLE);
    lua_pushvalue(L, 2);
    lua_gettable(L, 1);
    return 1;
}

/*
** ast.insert(block, index, stmt)
** 插入语句到 block.body：table.insert(block.body, index, stmt)
*/
static int ast_insert(lua_State *L) {
    luaL_checktype(L, 1, LUA_TTABLE);
    int pos = (int)luaL_checkinteger(L, 2);
    luaL_checktype(L, 3, LUA_TTABLE);

    lua_getfield(L, 1, "body");
    if (!lua_istable(L, -1)) {
        return luaL_error(L, "block must have a 'body' field (table)");
    }

    lua_getglobal(L, "table");
    lua_getfield(L, -1, "insert");
    lua_pushvalue(L, -3);     /* body */
    lua_pushinteger(L, pos);
    lua_pushvalue(L, 3);      /* stmt */
    lua_call(L, 3, 0);

    lua_pop(L, 2);  /* 弹出 table 全局和 body */
    return 0;
}

/*
** ast.remove(block, index) -> removed_stmt
** 从 block.body 删除语句：table.remove(block.body, index)
*/
static int ast_remove(lua_State *L) {
    luaL_checktype(L, 1, LUA_TTABLE);
    int pos = (int)luaL_checkinteger(L, 2);

    lua_getfield(L, 1, "body");
    if (!lua_istable(L, -1)) {
        return luaL_error(L, "block must have a 'body' field (table)");
    }

    lua_getglobal(L, "table");
    lua_getfield(L, -1, "remove");
    lua_pushvalue(L, -3);     /* body */
    lua_pushinteger(L, pos);
    lua_call(L, 2, 1);        /* 返回被删除的元素 */

    lua_insert(L, -3);        /* 把结果移到 table 全局和 body 下面 */
    lua_pop(L, 2);            /* 弹出 table 全局和 body */
    return 1;
}

/*
** ast.replace(block, index, new_stmt)
** 替换 block.body 中的语句：block.body[index] = new_stmt
*/
static int ast_replace(lua_State *L) {
    luaL_checktype(L, 1, LUA_TTABLE);
    int pos = (int)luaL_checkinteger(L, 2);
    luaL_checktype(L, 3, LUA_TTABLE);

    lua_getfield(L, 1, "body");
    if (!lua_istable(L, -1)) {
        return luaL_error(L, "block must have a 'body' field (table)");
    }

    lua_pushinteger(L, pos);
    lua_pushvalue(L, 3);
    lua_settable(L, -3);

    lua_pop(L, 1);  /* 弹出 body */
    return 0;
}

/*
** 深拷贝辅助函数
** 将栈顶的值深拷贝，如果是表则递归拷贝。
** visited: visited 表的绝对索引，用于处理循环引用。
** 用拷贝后的值替换栈顶的原值。
*/
static void deep_copy_value(lua_State *L, int visited) {
    if (!lua_istable(L, -1)) return;

    /* 检查是否已经拷贝过（处理循环引用） */
    lua_pushvalue(L, -1);
    lua_rawget(L, visited);
    if (!lua_isnil(L, -1)) {
        lua_replace(L, -2);
        return;
    }
    lua_pop(L, 1);

    lua_newtable(L);  /* 原表, 新表 */

    /* visited[原表] = 新表 */
    lua_pushvalue(L, -2);
    lua_pushvalue(L, -2);
    lua_rawset(L, visited);

    /* 拷贝元表 */
    if (lua_getmetatable(L, -2)) {
        lua_setmetatable(L, -2);
    }

    /* 遍历原表的所有键值对 */
    int original_idx = lua_gettop(L) - 1;  /* 保存原表的绝对索引 */
    int new_table_idx = lua_gettop(L);     /* 保存新表的绝对索引 */
    lua_pushnil(L);  /* 原表, 新表, nil */
    while (lua_next(L, original_idx)) {  /* 原表, 新表, key, val */
        /* 深拷贝 key */
        lua_pushvalue(L, -2);  /* 原表, 新表, key, val, key_copy */
        deep_copy_value(L, visited);

        /* 深拷贝 val */
        lua_pushvalue(L, -2);  /* 原表, 新表, key, val, key_copy, val_copy */
        deep_copy_value(L, visited);

        /* new_table[key_copy] = val_copy */
        lua_settable(L, new_table_idx);

        /* 弹出 val，保留 key 供 lua_next 继续遍历 */
        lua_pop(L, 1);
    }

    /* 用新表替换原表 */
    lua_replace(L, -2);
}

/*
** ast.copy(node) -> copy
** 深拷贝 AST 节点
*/
static int ast_copy(lua_State *L) {
    luaL_checktype(L, 1, LUA_TTABLE);

    lua_newtable(L);  /* node, visited */
    int visited_idx = lua_gettop(L);

    lua_pushvalue(L, 1);  /* node, visited, node */
    deep_copy_value(L, visited_idx);
    /* 栈: node, visited, copy */

    return 1;
}

/*
** ast.unparse(ast_table) -> string
** 将 Lua table 格式的序列化 AST 反解析为 Lua 源码字符串（C 层实现）
*/
static int ast_unparse(lua_State *L) {
    return luaY_ast_unparse_from_table(L, 1);
}

/* 内存字符串 ZIO：已提前把整块数据填到 ZIO buffer，读完即 EOF */
static const char *astlib_mem_reader(lua_State *L, void *data, size_t *size) {
    (void)L; (void)data;
    *size = 0;
    return NULL;
}

/*
** parse_src_to_ast_table
** 功能：把源码字符串 parse 为 AstChunk，立即序列化为 Lua table，留在栈顶。
** 参数：src / len — 要解析的 Lua 源码（内存安全，内部拷贝）
** 返回：1=成功（栈顶是 AST table）；0=失败（栈顶是错误字符串，可直接当返回值）
** 注意：内部使用 last_parse.c 已验证过的同款内存 ZIO 初始化模式（buf+1 / len-1 / dummy reader），
**       保证和 string-mode astparser("...") 的解析路径完全一致，避免不同入口解析差异。
*/
static int parse_src_to_ast_table(lua_State *L, const char *src, size_t len) {
    int ok = 0;
    /* +2 NUL padding：防 zreader/lexer 在边界扫描时越界读 */
    size_t buf_cap = len + 2;
    char *buf = luaM_newvector(L, buf_cap, char);
    memcpy(buf, src, len);
    buf[len] = '\0';
    buf[len + 1] = '\0';

    ZIO ast_z;
    memset(&ast_z, 0, sizeof(ast_z));
    ast_z.L = L;
    ast_z.p = (len > 0) ? buf + 1 : buf;
    ast_z.n = (len > 0) ? len - 1 : 0;
    ast_z.reader = astlib_mem_reader;

    Mbuffer ast_buff;
    luaZ_initbuffer(L, &ast_buff);

    Dyndata ast_dyd;
    memset(&ast_dyd, 0, sizeof(ast_dyd));

    int firstchar = (len > 0) ? (unsigned char)buf[0] : '\n';

    /* 关 GC：与 string-mode astparser 路径保持一致，避免 parse 过程中临时对象误回收 */
    int old_gc = lua_gc(L, LUA_GCISRUNNING, 0);
    lua_gc(L, LUA_GCSTOP, 0);
    {
        AstChunk *chunk = luaY_parse_ast(L, &ast_z, &ast_buff, &ast_dyd, "parser", firstchar);
        if (chunk != NULL) {
            chunk->main_func->is_vararg = 1;
            /* 序列化到 Lua table（栈顶） */
            ast_serialize_to_lua(L, chunk);
            /*
             * 挂 metatable：让序列化后的 chunk/节点表支持 ast:xxx() 冒号语法。
             * 兼容写法：asttbl:unparse() 等价 ast.unparse(asttbl)，asttbl:walk(fn) 等价 ast.walk(asttbl, fn)
             * 通过 metatable.__index = ast 模块表实现。
             */
            {
                /* 取（或创建）缓存的 metatable：key = "ASTLIB_META_AST_TABLE" */
                lua_getfield(L, LUA_REGISTRYINDEX, "ASTLIB_META_AST_TABLE");
                if (!lua_istable(L, -1)) {
                    lua_pop(L, 1); /* 弹 nil */
                    /* 创建 metatable */
                    lua_newtable(L);
                    /* 取 ast 模块表：从 registry ASTLIB_MODULE_TABLE（luaopen_ast 设置） */
                    lua_getfield(L, LUA_REGISTRYINDEX, "ASTLIB_MODULE_TABLE");
                    if (lua_istable(L, -1)) {
                        lua_setfield(L, -2, "__index");  /* meta.__index = astmod */
                    } else {
                        /* luaopen_ast 尚未执行（极少见）；先放弃，不挂也能通过 ast.xxx(tbl) 调用 */
                        lua_pop(L, 1);
                    }
                    lua_pushvalue(L, -1);
                    lua_setfield(L, LUA_REGISTRYINDEX, "ASTLIB_META_AST_TABLE");
                }
                /* -1: metatable, -2: serialized AST tbl */
                lua_setmetatable(L, -2);
            }
            /* 释放 C AST 结构：serialize 之后不再需要 codegen，立即释放避免内存暴涨 */
            if (chunk->pool != NULL) {
                ast_pool_free(chunk->pool);
                luaM_free(L, chunk->pool);
            }
            ok = 1;
        } else {
            lua_pushliteral(L, "parser: internal luaY_parse_ast returned NULL");
        }
    }
    if (old_gc) lua_gc(L, LUA_GCRESTART, 0);

    luaZ_freebuffer(L, &ast_buff);
    luaM_free(L, buf);
    return ok;
}

/*
** ast_tbl_to_runner
** 功能：把栈上指定位置的 AST Lua table 反序列化 → codegen → 包装成 CClosure(astparser_runner, 2) 推到栈顶。
** 参数：L - Lua 状态机；tbl_idx - AST table 在栈上的索引
** 返回：1 = 成功（栈顶是包装好的 CClosure，可直接调用，调用时立即执行 inner）；
**       0 = 失败（栈顶是错误字符串，可作为返回值或包装成 (nil, err)）
** 说明：codegen 逻辑与 astparser_runner delay/default 分支完全一致，确保行为对齐。
*/
static int ast_tbl_to_runner(lua_State *L, int tbl_idx) {
    if (!lua_istable(L, tbl_idx)) {
        lua_pushliteral(L, "ast_tbl_to_runner: argument is not an AST table");
        return 0;
    }
    AstChunk *new_chunk = ast_deserialize_from_lua(L, tbl_idx);
    if (new_chunk == NULL) {
        lua_pushliteral(L, "ast_tbl_to_runner: deserialize AST table failed");
        return 0;
    }
    new_chunk->main_func->is_vararg = 1;
    Dyndata temp_dyd;
    memset(&temp_dyd, 0, sizeof(temp_dyd));
    Proto *new_p = luaY_codegen_chunk(L, new_chunk, &temp_dyd);
    ast_pool_free(new_chunk->pool);
    luaM_free(L, new_chunk->pool);
    if (new_p == NULL) {
        lua_pushliteral(L, "ast_tbl_to_runner: codegen AST to Proto failed");
        return 0;
    }
    /* 包装成 CClosure(astparser_runner, 2)：upvalue1=new_p, upvalue2=NULL（chunk=NULL 表示非延迟模式） */
    lua_pushlightuserdata(L, new_p);
    lua_pushlightuserdata(L, NULL);
    lua_pushcclosure(L, astparser_runner, 2);
    return 1;
}

/*
** ast.astparser(src_str, opts?) / 全局 astparser(src_str, opts?)
**  - 默认：等价 astparser("...") 的默认行为，返回 Lua Closure（运行时解析，loadbuffer 实现）
**  - opts.ast==true：解析并返回序列化的 AST Lua table（可 walk / copy / unparse / 修改再 load）
**  - opts.inputmode=="ast" 且 arg1 为 string：parse → serialize → deserialize → codegen → 返回 CClosure runner
**  - opts.inputmode=="ast" 且 arg1 为 table（AST）：直接 deserialize → codegen → 返回 CClosure runner
*/
static int ast_astparser(lua_State *L) {
    int arg1_is_tbl = lua_istable(L, 1);
    int want_ast = 0;
    int inputmode_ast = 0;
    if (lua_istable(L, 2)) {
        lua_getfield(L, 2, "ast");
        want_ast = lua_toboolean(L, -1);
        lua_pop(L, 1);
        lua_getfield(L, 2, "inputmode");
        if (lua_isstring(L, -1) && strcmp(lua_tostring(L, -1), "ast") == 0) {
            inputmode_ast = 1;
        }
        lua_pop(L, 1);
    }

    /* inputmode="ast" 且第一参数是 table：直接把 AST table 转成 runner CClosure */
    if (inputmode_ast && arg1_is_tbl) {
        if (ast_tbl_to_runner(L, 1)) return 1;
        return 1;  /* 错误信息在栈顶 */
    }

    /* 剩下的分支：第一参数必须是 string（arg1 非 string 且非 inputmode_ast+table = 错误） */
    if (!arg1_is_tbl) {
        size_t len;
        const char *src = luaL_checklstring(L, 1, &len);  /* 非 string 时报错（标准错误信息） */
        if (want_ast) {
            if (parse_src_to_ast_table(L, src, len)) return 1;  /* 返回 AST table */
            /* 失败：把错误字符串作为第二个返回值 */
            lua_pushnil(L);
            lua_insert(L, -2);
            return 2;
        }
        if (inputmode_ast) {
            /* string + inputmode="ast"：先 parse → table（留在栈顶）→ 转 CClosure runner */
            if (!parse_src_to_ast_table(L, src, len)) {
                /* parse_src_to_ast_table 返回 0 时栈顶是 err 字符串 */
                return 1;
            }
            /* 栈顶是 AST table */
            if (ast_tbl_to_runner(L, -1)) {
                lua_remove(L, -2);  /* 去掉 AST table，只留 runner 在栈顶 */
                return 1;
            }
            lua_remove(L, -2);  /* 去掉 AST table，只留 err */
            return 1;
        }
        /* 默认：返回 Lua Closure（或错误字符串，与 luaL_loadbufferx 语义一致） */
        int rc = luaL_loadbufferx(L, src, len, "@astparser", NULL);
        if (rc != LUA_OK) return 1;  /* 错误信息在栈顶 */
        return 1;                    /* Lua Closure 在栈顶 */
    }

    /* 到达此处：arg1 是 table 但没设 inputmode="ast" → 非法组合 */
    return luaL_error(L,
        "ast.astparser: arg1 as AST table requires opts.inputmode=\"ast\"; "
        "for source strings use ast.astparser(src_str, opts?)");
}

/*
** ast.parser(src_str, opts?) / 全局 parser(src_str, opts?)：
** - opts=nil 或 opts.ast==false: 等价于 load(src_str)，返回 Lua Closure / (nil, err)
** - opts.ast==true: 解析 src_str 为 AST 并序列化为 Lua table 返回（(tbl, nil) 成功 / (nil, err) 失败）
** - opts.inputmode=="ast" 且 arg1=string：parse → table → codegen → 返回 (runner, nil) 或 (nil, err)
** - opts.inputmode=="ast" 且 arg1=table（AST）：deserialize → codegen → 返回 (runner, nil) 或 (nil, err)
*/
static int ast_parser(lua_State *L) {
    int arg1_is_tbl = lua_istable(L, 1);
    int want_ast = 0;
    int inputmode_ast = 0;
    if (lua_istable(L, 2)) {
        lua_getfield(L, 2, "ast");
        want_ast = lua_toboolean(L, -1);
        lua_pop(L, 1);
        lua_getfield(L, 2, "inputmode");
        if (lua_isstring(L, -1) && strcmp(lua_tostring(L, -1), "ast") == 0) {
            inputmode_ast = 1;
        }
        lua_pop(L, 1);
    }

    /* inputmode="ast" 且 arg1=table：AST → runner CClosure（返回 (runner, nil) 或 (nil, err)） */
    if (inputmode_ast && arg1_is_tbl) {
        if (ast_tbl_to_runner(L, 1)) {
            lua_pushnil(L);  /* nil err → (runner, nil) */
            return 2;
        }
        /* 失败：栈顶 err → (nil, err) */
        lua_pushnil(L);
        lua_insert(L, -2);
        return 2;
    }

    /* 剩下分支：arg1 必须是 string */
    if (!arg1_is_tbl) {
        size_t len;
        const char *src = luaL_checklstring(L, 1, &len);
        if (want_ast) {
            if (parse_src_to_ast_table(L, src, len)) {
                /* AST table 在栈顶；加 nil 作第二返回值表示 "no error"（(tbl, nil)） */
                lua_pushnil(L);
                return 2;
            }
            /* 失败：(nil, err) */
            lua_pushnil(L);
            lua_insert(L, -2);
            return 2;
        }
        if (inputmode_ast) {
            /* string + inputmode="ast"：先 parse → table → runner */
            if (!parse_src_to_ast_table(L, src, len)) {
                /* 栈顶是 err → (nil, err) */
                lua_pushnil(L);
                lua_insert(L, -2);
                return 2;
            }
            /* 栈顶是 AST table → 转 runner */
            if (ast_tbl_to_runner(L, -1)) {
                lua_remove(L, -2);  /* 弹掉 AST table */
                lua_pushnil(L);     /* (runner, nil) */
                return 2;
            }
            /* runner 失败：栈顶 err → (nil, err) */
            lua_remove(L, -2);  /* 弹掉 AST table */
            lua_pushnil(L);
            lua_insert(L, -2);
            return 2;
        }
        /* 默认模式：等价 load(src_str)，成功返回函数；失败返回 (nil, err) */
        int rc = luaL_loadbufferx(L, src, len, "@parser", NULL);
        if (rc != LUA_OK) {
            lua_pushnil(L);
            lua_insert(L, -2);  /* swap: nil, err */
            return 2;
        }
        return 1;
    }

    /* arg1 是 table 但没设 inputmode="ast" */
    lua_pushnil(L);
    lua_pushliteral(L,
        "ast.parser: arg1 as AST table requires opts.inputmode=\"ast\"; "
        "for source strings use ast.parser(src_str, opts?)");
    return 2;
}

/* 模块函数注册表 */
static const luaL_Reg astlib[] = {
    {"stmt", ast_stmt},
    {"expr", ast_expr},
    {"find", ast_find},
    {"walk", ast_walk},
    {"set", ast_set_field},
    {"get", ast_get_field},
    {"insert", ast_insert},
    {"remove", ast_remove},
    {"replace", ast_replace},
    {"copy", ast_copy},
    {"unparse", ast_unparse},
    {"astparser", ast_astparser},
    {"parser", ast_parser},
    {NULL, NULL}
};

/*
** 模块入口：luaopen_ast
** 额外把 ast.astparser / ast.parser 注册到全局表，兼容 astparser(var) / parser(var) 形式。
*/
LUAMOD_API int luaopen_ast(lua_State *L) {
    luaL_newlib(L, astlib);

    /* 缓存 ast 模块表到 registry（AST 表 metatable.__index 要用，确保 ast:unparse() 等冒号语法可用）
     * 注意：只在 require("ast") 时存一次；后续 parse_src_to_ast_table 可直接取。 */
    lua_pushvalue(L, -1);
    lua_setfield(L, LUA_REGISTRYINDEX, "ASTLIB_MODULE_TABLE");

    /* 注册全局 astparser 和 parser（非字符串字面量的普通调用形式）。
     * 注意：仅当 require("ast") 时才会注册全局；如果用户从未 require 过 ast，
     * 直接调用全局 astparser(var) 会得到 nil（与当前行为一致）。 */
    lua_pushvalue(L, -1);
    lua_getfield(L, -1, "astparser");
    lua_setglobal(L, "astparser");

    lua_pushvalue(L, -2);
    lua_getfield(L, -1, "parser");
    lua_setglobal(L, "parser");

    lua_pop(L, 1);  /* 弹出复制的 ast 表 */

    return 1;
}