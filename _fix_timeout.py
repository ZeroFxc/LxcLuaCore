import re

with open('laio.c', 'r', encoding='utf-8') as f:
    content = f.read()

new_impl = """static int laio_with_timeout(lua_State *L) {
    promise **pp = (promise **)luaL_checkudata(L, 1, PROMISE_METATABLE);
    if (!pp || !*pp) return luaL_error(L, "Invalid Promise object");

    int timeout_ms = luaL_checkinteger(L, 2);
    if (timeout_ms <= 0) {
        promise_retain(*pp);
        promise **result = (promise **)lua_newuserdata(L, sizeof(promise *));
        *result = *pp;
        luaL_getmetatable(L, PROMISE_METATABLE);
        lua_setmetatable(L, -2);
        return 1;
    }

    event_loop *loop = (*pp)->loop ? (*pp)->loop : aio_get_default_loop(L);

    promise *timeout_p = promise_new(L, loop);

    promise **orig_ud = (promise **)lua_newuserdata(L, sizeof(promise *));
    *orig_ud = promise_retain(*pp);
    luaL_getmetatable(L, PROMISE_METATABLE);
    lua_setmetatable(L, -2);

    promise **tout_ud = (promise **)lua_newuserdata(L, sizeof(promise *));
    *tout_ud = timeout_p;
    luaL_getmetatable(L, PROMISE_METATABLE);
    lua_setmetatable(L, -2);

    promise *race_p = promise_race(L, loop);
    lua_pop(L, 2);

    if (!race_p) {
        promise_release(timeout_p);
        return luaL_error(L, "withTimeout: failed to create race");
    }

    double timeout_sec = timeout_ms / 1000.0;

    typedef struct { promise *p; lua_State *L_main; } _wt_tctx;
    _wt_tctx *_wrc = (_wt_tctx *)calloc(1, sizeof(_wt_tctx));
    _wrc->p = timeout_p;
    _wrc->L_main = L;

    ev_timer _wt;
    memset(&_wt, 0, sizeof(_wt));
    _wt.timeout = ev_loop_now(loop) + timeout_sec;
    _wt.data = _wrc;
    _wt.callback = laio_wt_timer_callback;

    ev_timer_start(loop, &_wt);

    promise **rp = (promise **)lua_newuserdata(L, sizeof(promise *));
    *rp = race_p;
    luaL_getmetatable(L, PROMISE_METATABLE);
    lua_setmetatable(L, -2);
    return 1;
}
"""

# Find exact boundaries
start_idx = content.find('static int laio_with_timeout(lua_State *L)')
if start_idx < 0:
    print('ERROR: function not found!')
    exit(1)

# Find the end: look for the closing } followed by blank line and setInterval comment
# Search forward from start for the pattern
search_from = start_idx + 30

# Find "setInterval / clearInterval" comment which comes right after
next_section = content.find('setInterval / clearInterval', search_from)
if next_section < 0:
    print('ERROR: next section not found')
    exit(1)

# Go back to find the start of the comment line (/* ...)
line_start = content.rfind('\n/*', start_idx, next_section)
if line_start < 0:
    # Try without the /* on its own line
    line_start = content.rfind('\n', start_idx, next_section)

print(f'Function starts at offset {start_idx}')
print(f'Next section at {next_section}, comment line starts at ~{line_start}')

# Replace from start_idx to line_start (keeping the comment)
new_content = content[:start_idx] + new_impl + content[line_start:]
with open('laio.c', 'w', encoding='utf-8') as f:
    f.write(new_content)

old_size = len(content)
new_size = len(new_content)
print(f'Done! Removed {old_size - new_size} bytes ({(old_size - new_size)//1024}KB)')
