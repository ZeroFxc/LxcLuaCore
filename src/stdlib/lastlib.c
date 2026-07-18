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
    {NULL, NULL}
};

/*
** 模块入口：luaopen_ast
*/
LUAMOD_API int luaopen_ast(lua_State *L) {
    luaL_newlib(L, astlib);
    return 1;
}