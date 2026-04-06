/**
 * @file lpromise.c
 * @brief LXCLUA Promise/Future 异步原语实现
 * 完整实现 Promise/A+ 规范，支持链式调用和组合操作
 */

#define lpromise_c
#define LUA_LIB

#include "lpromise.h"
#include "lauxlib.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

/*
** =====================================================================
** 内部常量与工具宏
** =====================================================================
*/

#define PROMISE_METATABLE "asyncio.promise"
#define REACTION_MAX_DEPTH 64   /**< 最大链式调用深度，防止栈溢出 */
#define PROMISE_TINTEGER 100     /**< 整数结果类型标记（避免与 Lua 原生类型冲突） */

/*
** =====================================================================
** 内部函数声明
** =====================================================================*/

static void trigger_reactions(promise *p, lua_State *L);
static void execute_reaction(promise_reaction *reaction, promise *parent, lua_State *L);
static promise *create_child_promise(promise *parent);
static void push_promise_result(promise *p, lua_State *L);

/*
** =====================================================================
** 内存管理与生命周期
** =====================================================================*/

promise *promise_new(lua_State *L, event_loop *loop) {
    promise *p = (promise *)calloc(1, sizeof(promise));
    if (!p) return NULL;

    p->state = PROMISE_PENDING;
    p->result_type = LUA_TNIL;
    p->reactions = NULL;
    p->reaction_count = 0;
    p->loop = loop;
    p->aco_ctx = NULL;  /* 异步/await 协程上下文 */
    p->on_settled = NULL;  /* settle 回调 */
    p->tag = NULL;
    p->ref_count = 1;

    return p;
}

promise *promise_retain(promise *p) {
    if (p) {
        p->ref_count++;
    }
    return p;
}

void promise_release(promise *p) {
    if (!p) return;

    p->ref_count--;
    if (p->ref_count > 0) return;

    /* 释放结果值引用 */
    if (p->state != PROMISE_PENDING) {
        /* 需要释放注册表中的引用 */
        /* 注意：这里假设 L 在某个地方可访问，实际使用时需要传入或存储 */
    }

    /* 释放反应队列 */
    promise_reaction *r = p->reactions;
    while (r) {
        promise_reaction *next = r->next;
        if (r->child) promise_release(r->child);
        free(r);
        r = next;
    }

    free(p);
}

/*
** =====================================================================
** 核心状态转换
** =====================================================================*/

int promise_resolve(promise *p, lua_State *L) {
    if (!p || !L || p->state != PROMISE_PENDING) return -1;

    p->state = PROMISE_FULFILLED;

    /* 从栈顶获取结果值并保存 */
    int top = lua_gettop(L);
    if (top > 0) {
        int need_pop = 1;  /* 大多数类型需要手动 pop */
        p->result_type = lua_type(L, -1);
        switch (p->result_type) {
            case LUA_TNIL:
                need_pop = 0;
                break;
            case LUA_TBOOLEAN:
                p->result.boolean_val = lua_toboolean(L, -1);
                break;
            case LUA_TNUMBER:
                if (lua_isinteger(L, -1)) {
                    p->result.int_val = lua_tointeger(L, -1);
                    p->result_type = PROMISE_TINTEGER;
                } else {
                    p->result.num_val = lua_tonumber(L, -1);
                }
                break;
            case LUA_TSTRING:
                p->result.str_ref = luaL_ref(L, LUA_REGISTRYINDEX);
                need_pop = 0;  /* luaL_ref 已消费 */
                break;
            case LUA_TTABLE:
                p->result.tbl_ref = luaL_ref(L, LUA_REGISTRYINDEX);
                need_pop = 0;  /* luaL_ref 已消费 */
                break;
            case LUA_TFUNCTION:
                p->result.func_ref = luaL_ref(L, LUA_REGISTRYINDEX);
                need_pop = 0;  /* luaL_ref 已消费 */
                break;
            case LUA_TUSERDATA:
            case LUA_TLIGHTUSERDATA:
                p->result.data = lua_touserdata(L, -1);
                break;
            default:
                p->result_type = LUA_TNIL;
                need_pop = 0;
                break;
        }
        /* promise_resolve 不消费栈值：调用者负责清理 */
        (void)need_pop;
    }

    /* 触发所有已注册的反应 */
    trigger_reactions(p, L);

    /* 通知 async/await：Promise 已 settle，可能需要恢复协程 */
    if (p->on_settled) {
        p->on_settled(p);
    }

    return 0;
}

int promise_reject(promise *p, lua_State *L) {
    if (!p || !L || p->state != PROMISE_PENDING) return -1;

    p->state = PROMISE_REJECTED;

    /* 从栈顶获取拒绝原因 */
    int top = lua_gettop(L);
    if (top > 0) {
        int need_pop = 1;
        p->result_type = lua_type(L, -1);
        switch (p->result_type) {
            case LUA_TSTRING:
                p->result.str_ref = luaL_ref(L, LUA_REGISTRYINDEX);
                need_pop = 0;
                break;
            case LUA_TNUMBER:
                if (lua_isinteger(L, -1)) {
                    p->result.int_val = lua_tointeger(L, -1);
                    p->result_type = PROMISE_TINTEGER;
                } else {
                    p->result.num_val = lua_tonumber(L, -1);
                }
                break;
            default:
                lua_pushliteral(L, "Rejected");
                p->result.str_ref = luaL_ref(L, LUA_REGISTRYINDEX);
                need_pop = 0;  /* push+ref 模式，ref 消费了 push 的值 */
                break;
        }
        /* promise_reject 不消费栈值：调用者负责清理 */
        (void)need_pop;
    }

    /* 触发所有 catch 反应 */
    trigger_reactions(p, L);

    /* 通知 async/await：Promise 已 settle，可能需要恢复协程 */
    if (p->on_settled) {
        p->on_settled(p);
    }

    return 0;
}

int promise_get_state(const promise *p) {
    return p ? p->state : PROMISE_REJECTED;
}

int promise_is_settled(const promise *p) {
    return p ? (p->state != PROMISE_PENDING) : 1;
}

/*
** =====================================================================
** 结果获取
** =====================================================================*/

static void push_promise_result(promise *p, lua_State *L) {
    if (!p || !L) {
        lua_pushnil(L);
        return;
    }

    switch (p->result_type) {
        case LUA_TNIL:
            lua_pushnil(L);
            break;
        case LUA_TBOOLEAN:
            lua_pushboolean(L, p->result.boolean_val);
            break;
        case PROMISE_TINTEGER:
            lua_pushinteger(L, p->result.int_val);
            break;
        case LUA_TNUMBER:
            lua_pushnumber(L, p->result.num_val);
            break;
        case LUA_TSTRING:
            if (p->result.str_ref != LUA_NOREF && p->result.str_ref != LUA_REFNIL) {
                lua_rawgeti(L, LUA_REGISTRYINDEX, p->result.str_ref);
            } else {
                lua_pushliteral(L, "");
            }
            break;
        case LUA_TTABLE:
            if (p->result.tbl_ref != LUA_NOREF && p->result.tbl_ref != LUA_REFNIL) {
                lua_rawgeti(L, LUA_REGISTRYINDEX, p->result.tbl_ref);
            } else {
                lua_newtable(L);
            }
            break;
        case LUA_TFUNCTION:
            if (p->result.func_ref != LUA_NOREF && p->result.func_ref != LUA_REFNIL) {
                lua_rawgeti(L, LUA_REGISTRYINDEX, p->result.func_ref);
            } else {
                lua_pushnil(L);
            }
            break;
        default:
            lua_pushnil(L);
            break;
    }
}

int promise_get_result(promise *p, lua_State *L) {
    if (!p || !L) {
        lua_pushnil(L);
        return 1;
    }

    if (p->state == PROMISE_PENDING) {
        lua_pushnil(L);
        return 1;
    }

    if (p->state == PROMISE_REJECTED) {
        push_promise_result(p, L);
        return 1;
    }

    push_promise_result(p, L);
    return 1;
}

/*
** =====================================================================
** 反应队列管理（内部）
** =====================================================================*/

static void add_reaction(promise *p, promise_reaction *reaction) {
    reaction->next = p->reactions;
    p->reactions = reaction;
    p->reaction_count++;
}

static void trigger_reactions(promise *p, lua_State *L) {
    if (p->state == PROMISE_PENDING || !p->reactions) return;

    /* 复制反应列表并清空原列表（防止在执行过程中修改） */
    promise_reaction *reactions = p->reactions;
    p->reactions = NULL;
    p->reaction_count = 0;

    /* 按顺序执行每个反应 */
    promise_reaction *r = reactions;
    while (r) {
        promise_reaction *next = r->next;
        execute_reaction(r, p, L);
        free(r);
        r = next;
    }
}

static void execute_reaction(promise_reaction *reaction, promise *parent, lua_State *L) {
    if (!reaction || !parent || !L) return;

    promise *child = reaction->child;
    if (!child) return;

    if (parent->state == PROMISE_FULFILLED && reaction->type == 0) {
        /* then 回调：Promise 已完成 */
        if (reaction->handler_ref != LUA_NOREF && reaction->handler_ref != LUA_REFNIL) {
            /* 调用回调函数 */
            lua_rawgeti(L, LUA_REGISTRYINDEX, reaction->handler_ref);
            push_promise_result(parent, L);  /* 参数：父 Promise 的结果 */

            int status = lua_pcall(L, 1, 1, 0);
            if (status == LUA_OK) {
                promise_resolve(child, L);
            } else {
                promise_reject(child, L);
            }

            lua_pop(L, 1);  /* 清理返回值 */
        } else {
            /* 无回调，透传结果 */
            push_promise_result(parent, L);
            promise_resolve(child, L);
            lua_pop(L, 1);
        }
    }
    else if (parent->state == PROMISE_REJECTED && (reaction->type == 1 || reaction->type == 2)) {
        /* catch 或 finally 回调：Promise 被拒绝 */
        if (reaction->handler_ref != LUA_NOREF && reaction->handler_ref != LUA_REFNIL) {
            lua_rawgeti(L, LUA_REGISTRYINDEX, reaction->handler_ref);
            
            if (reaction->type == 2) {
                /* finally: 无参数 */
                int status = lua_pcall(L, 0, 0, 0);
                if (status != LUA_OK) {
                    lua_pop(L, 1);
                }
                /* finally 不改变结果，直接传递父 Promise 的状态 */
                if (parent->state == PROMISE_REJECTED) {
                    push_promise_result(parent, L);
                    promise_reject(child, L);
                    lua_pop(L, 1);
                } else {
                    push_promise_result(parent, L);
                    promise_resolve(child, L);
                    lua_pop(L, 1);
                }
            } else {
                /* catch: 传入错误原因 */
                push_promise_result(parent, L);
                
                int status = lua_pcall(L, 1, 1, 0);
                if (status == LUA_OK) {
                    promise_resolve(child, L);
                } else {
                    promise_reject(child, L);
                }
                lua_pop(L, 1);
            }
        } else {
            /* 无 catch 回调，继续传播拒绝 */
            push_promise_result(parent, L);
            promise_reject(child, L);
            lua_pop(L, 1);
        }
    }
    else if (parent->state == PROMISE_FULFILLED && reaction->type == 2) {
        /* finally 在 fulfilled 时调用 */
        if (reaction->handler_ref != LUA_NOREF && reaction->handler_ref != LUA_REFNIL) {
            lua_rawgeti(L, LUA_REGISTRYINDEX, reaction->handler_ref);
            lua_pcall(L, 0, 0, 0);
            lua_pop(L, 1);  /* 忽略 finally 返回值 */
        }
        /* 透传原始结果 */
        push_promise_result(parent, L);
        promise_resolve(child, L);
        lua_pop(L, 1);
    }

    promise_release(child);
}

static promise *create_child_promise(promise *parent) {
    promise *child = promise_new(NULL, parent ? parent->loop : NULL);
    if (child && parent) {
        child->loop = parent->loop;
    }
    return child;
}

/*
** =====================================================================
** 链式调用 API 实现
** =====================================================================*/

promise *promise_then(promise *p, lua_State *L) {
    if (!p || !L) return NULL;

    promise *child = create_child_promise(p);
    if (!child) return NULL;

    promise_reaction *reaction = (promise_reaction *)calloc(1, sizeof(promise_reaction));
    if (!reaction) {
        promise_release(child);
        return NULL;
    }

    reaction->type = 0;  /* then */
    reaction->handler_ref = LUA_NOREF;
    reaction->child = promise_retain(child);
    reaction->next = NULL;

    /* 如果栈上有函数，保存引用 */
    if (lua_isfunction(L, -1)) {
        reaction->handler_ref = luaL_ref(L, LUA_REGISTRYINDEX);
    } else {
        lua_pop(L, 1);  /* 移除非函数值 */
    }

    add_reaction(p, reaction);

    /* 如果 Promise 已经 settled，立即触发 */
    if (p->state != PROMISE_PENDING) {
        trigger_reactions(p, L);
    }

    return child;
}

promise *promise_catch(promise *p, lua_State *L) {
    if (!p || !L) return NULL;

    promise *child = create_child_promise(p);
    if (!child) return NULL;

    promise_reaction *reaction = (promise_reaction *)calloc(1, sizeof(promise_reaction));
    if (!reaction) {
        promise_release(child);
        return NULL;
    }

    reaction->type = 1;  /* catch */
    reaction->handler_ref = LUA_NOREF;
    reaction->child = promise_retain(child);
    reaction->next = NULL;

    if (lua_isfunction(L, -1)) {
        reaction->handler_ref = luaL_ref(L, LUA_REGISTRYINDEX);
    } else {
        lua_pop(L, 1);
    }

    add_reaction(p, reaction);

    if (p->state != PROMISE_PENDING) {
        trigger_reactions(p, L);
    }

    return child;
}

promise *promise_finally(promise *p, lua_State *L) {
    if (!p || !L) return NULL;

    promise *child = create_child_promise(p);
    if (!child) return NULL;

    promise_reaction *reaction = (promise_reaction *)calloc(1, sizeof(promise_reaction));
    if (!reaction) {
        promise_release(child);
        return NULL;
    }

    reaction->type = 2;  /* finally */
    reaction->handler_ref = LUA_NOREF;
    reaction->child = promise_retain(child);
    reaction->next = NULL;

    if (lua_isfunction(L, -1)) {
        reaction->handler_ref = luaL_ref(L, LUA_REGISTRYINDEX);
    } else {
        lua_pop(L, 1);
    }

    add_reaction(p, reaction);

    if (p->state != PROMISE_PENDING) {
        trigger_reactions(p, L);
    }

    return child;
}

/*
** =====================================================================
** 静态工具函数实现
** =====================================================================*/

promise *promise_resolved(lua_State *L, event_loop *loop) {
    promise *p = promise_new(L, loop);
    if (p && L) {
        promise_resolve(p, L);
    }
    return p;
}

promise *promise_rejected(lua_State *L, event_loop *loop) {
    promise *p = promise_new(L, loop);
    if (p && L) {
        promise_reject(p, L);
    }
    return p;
}

promise *promise_wrap_value(lua_State *L, event_loop *loop) {
    if (!L) return NULL;


    /* 检查是否已经是 Promise（通过元表判断） */
    if (lua_getmetatable(L, -1)) {
        if (lua_getfield(L, -1, "__name") == LUA_TSTRING) {
            const char *name = lua_tostring(L, -1);
            if (name && strcmp(name, "Promise") == 0) {
                lua_pop(L, 2);
                /* 假设 userdata 中包含 promise* */
                promise **pp = (promise **)lua_touserdata(L, -1);
                if (pp && *pp) {
                    return promise_retain(*pp);
                }
            }
        }
        lua_pop(L, 2);
    }

    /* 包装为 fulfilled Promise */
    return promise_resolved(L, loop);
}

/*
** =====================================================================
** 组合操作内部辅助函数
** =====================================================================*/

typedef struct {
    promise **promises;
    int count;
    int resolved_count;
    int rejected_count;
    promise *parent;
    lua_State *L;           /**< Lua 状态（用于构建结果） */
    int mode;               /* 0=all, 1=race, 2=all_settled, 3=any */
    int done;               /* 是否已完成（防止重复处理） */
} compose_ctx;

static void compose_on_settled(promise *child);

/*
** =====================================================================
** 组合操作 API 实现
** =====================================================================*/

promise *promise_all(lua_State *L, event_loop *loop) {
    if (!L) return NULL;

    int count = lua_gettop(L);
    if (count == 0) {
        lua_newtable(L);
        return promise_resolved(L, loop);
    }

    promise **promises = (promise **)malloc(sizeof(promise *) * count);
    if (!promises) return NULL;

    for (int i = 0; i < count; i++) {
        promises[count - 1 - i] = promise_wrap_value(L, loop);
        if (!promises[count - 1 - i]) {
            for (int j = 0; j < i; j++) promise_release(promises[count - 1 - j]);
            free(promises);
            return NULL;
        }
        lua_pop(L, 1);
    }

    promise *parent = promise_new(L, loop);
    if (!parent) {
        for (int i = 0; i < count; i++) promise_release(promises[i]);
        free(promises);
        return NULL;
    }

    /* 暂时：同步收集所有已 settle 的结果 */
    compose_ctx *ctx = (compose_ctx *)calloc(1, sizeof(compose_ctx));
    ctx->promises = promises;
    ctx->count = count;
    ctx->resolved_count = 0;
    ctx->parent = parent;
    ctx->L = L;
    ctx->mode = 0;
    ctx->done = 0;

    int pending_count = 0;
    int settled_count = 0;

    for (int i = 0; i < count; i++) {
        if (promises[i]->state != PROMISE_PENDING) {
            settled_count++;
            if (promises[i]->state == PROMISE_REJECTED) {
                /* 任一失败 → 立即拒绝 */
                push_promise_result(promises[i], L);
                promise_reject(parent, L);
                free(ctx);
                free(promises);
                return parent;
            }
        } else {
            promises[i]->aco_ctx = ctx;
            promises[i]->on_settled = compose_on_settled;
            promise_retain(promises[i]);
            pending_count++;
        }
    }

    /* 全部已同步 settle → 直接 resolve */
    if (settled_count == count) {
        ctx->done = 1;
        lua_newtable(L);
        for (int i = 0; i < count; i++) {
            push_promise_result(promises[i], L);
            lua_rawseti(L, -2, i + 1);
        }
        promise_resolve(parent, L);
        free(ctx);
        free(promises);
        return parent;
    }

    /* 有 pending 的：同步已 settle 的数量传给 ctx，compose_on_settled 将继续计数 */
    ctx->resolved_count = settled_count;
    /* promises 数组由 ctx 持有，在 compose_on_settled 最后释放（通过 ctx->promises） */
    /* 不在这里 free！ */
    return parent;
}

/**
 * @brief 组合操作统一 settle 回调（通过 child->aco_ctx 获取上下文）
 *
 * 从 child promise 的 aco_ctx 字段获取 compose_ctx，
 * 根据 mode 分发到对应的处理逻辑
 */
static void compose_on_settled(promise *child) {
    if (!child || !child->aco_ctx) return;

    compose_ctx *ctx = (compose_ctx *)child->aco_ctx;
    lua_State *L = ctx->L;

    if (ctx->done || !ctx->parent) return;
    if (ctx->parent->state != PROMISE_PENDING) { ctx->done = 1; return; }

    switch (ctx->mode) {
        case 0: { /* all */
            if (child->state == PROMISE_REJECTED) {
                ctx->done = 1;
                push_promise_result(child, L);
                promise_reject(ctx->parent, L);
                return;
            }
            ctx->resolved_count++;
            if (ctx->resolved_count == ctx->count) {
                ctx->done = 1;
                lua_newtable(L);
                for (int i = 0; i < ctx->count; i++) {
                    push_promise_result(ctx->promises[i], L);
                    lua_rawseti(L, -2, i + 1);
                }
                promise_resolve(ctx->parent, L);
            }
            break;
        }
        case 1: { /* race */
            ctx->done = 1;
            if (child->state == PROMISE_FULFILLED) {
                push_promise_result(child, L);
                promise_resolve(ctx->parent, L);
            } else {
                push_promise_result(child, L);
                promise_reject(ctx->parent, L);
            }
            break;
        }
        case 2: { /* all_settled */
            ctx->resolved_count++;
            if (ctx->resolved_count == ctx->count) {
                ctx->done = 1;
                lua_newtable(L);
                for (int i = 0; i < ctx->count; i++) {
                    lua_newtable(L);
                    const char *status_str = (ctx->promises[i]->state == PROMISE_FULFILLED)
                                              ? "fulfilled" : "rejected";
                    lua_pushstring(L, status_str);
                    lua_setfield(L, -2, "status");
                    push_promise_result(ctx->promises[i], L);
                    if (ctx->promises[i]->state == PROMISE_FULFILLED) {
                        lua_setfield(L, -2, "value");
                    } else {
                        lua_setfield(L, -2, "reason");
                    }
                    lua_rawseti(L, -2, i + 1);
                }
                promise_resolve(ctx->parent, L);
            }
            break;
        }
        case 3: { /* any */
            if (child->state == PROMISE_FULFILLED) {
                ctx->done = 1;
                push_promise_result(child, L);
                promise_resolve(ctx->parent, L);
                return;
            }
            ctx->rejected_count++;
            if (ctx->rejected_count == ctx->count) {
                ctx->done = 1;
                lua_pushliteral(L, "All promises were rejected");
                promise_reject(ctx->parent, L);
            }
            break;
        }
    }

    if (ctx->done) {
        free(ctx->promises);
        free(ctx);
    }
}

promise *promise_race(lua_State *L, event_loop *loop) {
    if (!L) return NULL;

    int count = lua_gettop(L);
    if (count == 0) {
        lua_pushliteral(L, "No promises to race");
        return promise_rejected(L, loop);
    }

    promise **promises = (promise **)malloc(sizeof(promise *) * count);
    if (!promises) return NULL;

    for (int i = 0; i < count; i++) {
        promises[count - 1 - i] = promise_wrap_value(L, loop);
        if (!promises[count - 1 - i]) {
            for (int j = 0; j < i; j++) promise_release(promises[count - 1 - j]);
            free(promises);
            return NULL;
        }
        lua_pop(L, 1);
    }

    promise *parent = promise_new(L, loop);
    if (!parent) {
        for (int i = 0; i < count; i++) promise_release(promises[i]);
        free(promises);
        return NULL;
    }

    compose_ctx *ctx = (compose_ctx *)calloc(1, sizeof(compose_ctx));
    if (!ctx) {
        promise_release(parent);
        for (int i = 0; i < count; i++) promise_release(promises[i]);
        free(promises);
        return NULL;
    }

    ctx->promises = promises;
    ctx->count = count;
    ctx->resolved_count = 0;
    ctx->parent = parent;
    ctx->L = L;
    ctx->mode = 1;  /* race */
    ctx->done = 0;

    int found_settled = -1;
    for (int i = 0; i < count; i++) {
        if (promises[i]->state != PROMISE_PENDING) {
            found_settled = i;
            break;
        }
    }

    if (found_settled >= 0) {
        /* 已有 settled 的：直接用它的结果 */
        ctx->done = 1;
        if (promises[found_settled]->state == PROMISE_FULFILLED) {
            push_promise_result(promises[found_settled], L);
            promise_resolve(parent, L);
        } else {
            push_promise_result(promises[found_settled], L);
            promise_reject(parent, L);
        }
        free(ctx);
        for (int i = 0; i < count; i++) promise_release(promises[i]);
        free(promises);
        return parent;
    }

    /* 全部 pending：注册回调 */
    for (int i = 0; i < count; i++) {
        promises[i]->aco_ctx = ctx;
        promises[i]->on_settled = compose_on_settled;
        promise_retain(promises[i]);
    }
    /* promises 数组由 ctx 持有，在 compose_on_settled 最后释放（通过 ctx->promises） */
    /* 不在这里 free！ */

    return parent;
}

promise *promise_all_settled(lua_State *L, event_loop *loop) {
    if (!L) return NULL;

    int count = lua_gettop(L);
    if (count == 0) {
        lua_newtable(L);
        return promise_resolved(L, loop);
    }

    promise **promises = (promise **)malloc(sizeof(promise *) * count);
    if (!promises) return NULL;

    for (int i = 0; i < count; i++) {
        promises[count - 1 - i] = promise_wrap_value(L, loop);
        if (!promises[count - 1 - i]) {
            for (int j = 0; j < i; j++) promise_release(promises[count - 1 - j]);
            free(promises);
            return NULL;
        }
        lua_pop(L, 1);
    }

    promise *parent = promise_new(L, loop);
    if (!parent) {
        for (int i = 0; i < count; i++) promise_release(promises[i]);
        free(promises);
        return NULL;
    }

    compose_ctx *ctx = (compose_ctx *)calloc(1, sizeof(compose_ctx));
    if (!ctx) {
        promise_release(parent);
        for (int i = 0; i < count; i++) promise_release(promises[i]);
        free(promises);
        return NULL;
    }

    ctx->promises = promises;
    ctx->count = count;
    ctx->resolved_count = 0;
    ctx->parent = parent;
    ctx->L = L;
    ctx->mode = 2;  /* all_settled */
    ctx->done = 0;

    int pending_count = 0;
    int settled_count = 0;
    for (int i = 0; i < count; i++) {
        if (promises[i]->state != PROMISE_PENDING) {
            settled_count++;
        } else {
            promises[i]->aco_ctx = ctx;
            promises[i]->on_settled = compose_on_settled;
            promise_retain(promises[i]);
            pending_count++;
        }
    }

    if (settled_count == count) {
        /* 全部已 settle */
        ctx->done = 1;
        lua_newtable(L);
        for (int i = 0; i < count; i++) {
            lua_newtable(L);
            const char *status_str = (promises[i]->state == PROMISE_FULFILLED)
                                      ? "fulfilled" : "rejected";
            lua_pushstring(L, status_str);
            lua_setfield(L, -2, "status");
            push_promise_result(promises[i], L);
            if (promises[i]->state == PROMISE_FULFILLED) {
                lua_setfield(L, -2, "value");
            } else {
                lua_setfield(L, -2, "reason");
            }
            lua_rawseti(L, -2, i + 1);
        }
        promise_resolve(parent, L);
        free(ctx);
        free(promises);
        return parent;
    }
    /* 有 pending 的：同步已 settle 的数量传给 ctx，compose_on_settled 将继续计数 */
    ctx->resolved_count = settled_count;
    /* promises 数组由 ctx 持有，在 compose_on_settled 最后释放（通过 ctx->promises） */
    /* 不在这里 free！ */

    return parent;
}

promise *promise_any(lua_State *L, event_loop *loop) {
    if (!L) return NULL;

    int count = lua_gettop(L);
    if (count == 0) {
        lua_pushliteral(L, "No promises provided");
        return promise_rejected(L, loop);
    }

    promise **promises = (promise **)malloc(sizeof(promise *) * count);
    if (!promises) return NULL;

    for (int i = 0; i < count; i++) {
        promises[count - 1 - i] = promise_wrap_value(L, loop);
        if (!promises[count - 1 - i]) {
            for (int j = 0; j < i; j++) promise_release(promises[count - 1 - j]);
            free(promises);
            return NULL;
        }
        lua_pop(L, 1);
    }

    promise *parent = promise_new(L, loop);
    if (!parent) {
        for (int i = 0; i < count; i++) promise_release(promises[i]);
        free(promises);
        return NULL;
    }

    compose_ctx *ctx = (compose_ctx *)calloc(1, sizeof(compose_ctx));
    if (!ctx) {
        promise_release(parent);
        for (int i = 0; i < count; i++) promise_release(promises[i]);
        free(promises);
        return NULL;
    }

    ctx->promises = promises;
    ctx->count = count;
    ctx->resolved_count = 0;
    ctx->rejected_count = 0;
    ctx->parent = parent;
    ctx->L = L;
    ctx->mode = 3;  /* any */
    ctx->done = 0;

    int found_fulfilled = -1;
    int rejected_count = 0;
    for (int i = 0; i < count; i++) {
        if (promises[i]->state != PROMISE_PENDING) {
            if (promises[i]->state == PROMISE_FULFILLED) {
                found_fulfilled = i;
                break;
            } else {
                rejected_count++;
            }
        }
    }

    if (found_fulfilled >= 0) {
        ctx->done = 1;
        push_promise_result(promises[found_fulfilled], L);
        promise_resolve(parent, L);
        free(ctx);
        for (int i = 0; i < count; i++) promise_release(promises[i]);
        free(promises);
        return parent;
    }

    if (rejected_count == count) {
        /* 全部 rejected */
        ctx->done = 1;
        lua_pushliteral(L, "All promises were rejected");
        promise_reject(parent, L);
        free(ctx);
        for (int i = 0; i < count; i++) promise_release(promises[i]);
        free(promises);
        return parent;
    }

    /* 有 pending 且有 fulfilled 可能：注册回调 */
    for (int i = 0; i < count; i++) {
        if (promises[i]->state == PROMISE_PENDING) {
            promises[i]->aco_ctx = ctx;
            promises[i]->on_settled = compose_on_settled;
            promise_retain(promises[i]);
        }
    }
    /* 同步已 reject 的数量传给 ctx */
    ctx->rejected_count = rejected_count;
    /* promises 数组由 ctx 持有，在 compose_on_settled 最后释放（通过 ctx->promises） */
    /* 不在这里 free！ */

    return parent;
}

/*
** =====================================================================
** 高级 API 实现
** =====================================================================*/

void promise_set_tag(promise *p, const char *tag) {
    if (p) {
        p->tag = tag;
    }
}

const char *promise_get_tag(const promise *p) {
    return p ? p->tag : NULL;
}

int promise_await_sync(promise *p, lua_State *L, int timeout_ms) {
    if (!p || !L) return -1;

    if (p->state != PROMISE_PENDING) {
        promise_get_result(p, L);
        return 0;
    }

    double start = ev_time();
    while (p->state == PROMISE_PENDING) {
        if (timeout_ms >= 0) {
            double elapsed = (ev_time() - start) * 1000;
            if (elapsed >= timeout_ms) return -1;
        }

#ifdef _WIN32
        Sleep(1);
#else
        usleep(1000);
#endif

        /* 让事件循环有机会处理任务（如果关联了事件循环） */
        if (p->loop) {
            int loop_state = ev_loop_get_state(p->loop);
            if (loop_state == EV_LOOP_RUNNING) {
                ev_loop_iterate(p->loop, 0);
            } else if (loop_state == EV_LOOP_STOPPED) {
                /* 事件循环未启动，执行单次迭代以推进异步操作 */
                ev_loop_iterate(p->loop, 0);
            }
        }
    }

    promise_get_result(p, L);
    return 0;
}

int promise_cancel(promise *p, lua_State *L) {
    if (!p || !L) return -1;

    if (p->state != PROMISE_PENDING) return -1;

    lua_pushliteral(L, "Promise cancelled");
    promise_reject(p, L);
    lua_pop(L, 1);

    return 0;
}
