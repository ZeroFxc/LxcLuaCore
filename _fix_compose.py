import re

with open('lpromise.c', 'r', encoding='utf-8') as f:
    c = f.read()

# Fix promise_race: replace the loop that calls compose_on_settled with inline handling
old_race_loop = '''    int found_settled = 0;
    for (int i = 0; i < count; i++) {
        promises[i]->aco_ctx = ctx;
        if (promises[i]->state != PROMISE_PENDING) {
            compose_on_settled(promises[i]);
            found_settled = 1;
            break;
        } else {
            promises[i]->on_settled = compose_on_settled;
            promise_retain(promises[i]);
        }
    }

    if (found_settled || count == 0) {
        if (!ctx->done) ctx->done = 1;
        for (int i = 0; i < count; i++) {
            if (promises[i]->state == PROMISE_PENDING) {
                promises[i]->aco_ctx = NULL;
                promises[i]->on_settled = NULL;
                promise_release(promises[i]);
            }
        }
        free(promises);
        free(ctx);
    } else {
        for (int i = 0; i < count; i++) promise_release(promises[i]);
    }'''

new_race_loop = '''    int found_settled = -1;
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
    free(promises);'''

c = c.replace(old_race_loop, new_race_loop)

# Fix promise_all_settled
old_as_loop = '''    int pending_count = 0;
    for (int i = 0; i < count; i++) {
        promises[i]->aco_ctx = ctx;
        if (promises[i]->state != PROMISE_PENDING) {
            compose_on_settled(promises[i]);
        } else {
            promises[i]->on_settled = compose_on_settled;
            promise_retain(promises[i]);
            pending_count++;
        }
    }

    if (pending_count == 0 && !ctx->done) {
        ctx->done = 1;
        free(promises);
        free(ctx);
    } else if (!ctx->done) {
        for (int i = 0; i < count; i++) promise_release(promises[i]);
    }'''

new_as_loop = '''    int pending_count = 0;
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
    free(promises);'''

c = c.replace(old_as_loop, new_as_loop)

# Fix promise_any
old_any_loop = '''    int pending_count = 0;
    for (int i = 0; i < count; i++) {
        promises[i]->aco_ctx = ctx;
        if (promises[i]->state != PROMISE_PENDING) {
            compose_on_settled(promises[i]);
            if (ctx->done) break;
        } else {
            promises[i]->on_settled = compose_on_settled;
            promise_retain(promises[i]);
            pending_count++;
        }
    }

    if ((pending_count == 0 && !ctx->done) || ctx->done) {
        if (!ctx->done) ctx->done = 1;
        for (int i = 0; i < count; i++) {
            if (promises[i]->state == PROMISE_PENDING) {
                promises[i]->aco_ctx = NULL;
                promises[i]->on_settled = NULL;
                promise_release(promises[i]);
            }
        }
        free(promises);
        free(ctx);
    } else {
        for (int i = 0; i < count; i++) promise_release(promises[i]);
    }'''

new_any_loop = '''    int found_fulfilled = -1;
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
    free(promises);'''

c = c.replace(old_any_loop, new_any_loop)

with open('lpromise.c', 'w', encoding='utf-8') as f:
    f.write(c)

print('Fixed all 3 compose functions')
