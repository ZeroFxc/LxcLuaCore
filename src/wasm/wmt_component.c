/**
 * @file wmt_component.c
 * @brief wasmtime Lua 绑定 —— Component Model（组件模型）
 *
 * 支持加载 preview2 WASI 组件（component 二进制）：
 *   wasmtime.newComponent(engine, bytes)         → component
 *   wasmtime.newComponentLinker(engine)          → comp_linker
 *   comp_linker:addWasiP2()                      → 定义 WASI preview2 imports
 *   comp_linker:instantiate(store, component)    → comp_instance
 *   comp_instance:getExport(name)                → comp_func
 *   comp_func:call(args...)                      → 结果
 *
 * 值转换覆盖 component 模型主要类型：bool/s8-u64/f32/f64/char/string/
 * list/record/tuple/variant/enum/option/result/flags。
 * resource / map / future / stream 暂不绑定（报错）。
 *
 * 共享类型见 lwasmtime.h。
 */
#include "lwasmtime.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* ============================================================
 * 值转换：component_val → Lua
 * ============================================================ */

static void wmt_comp_val_to_lua(lua_State *L, const wasmtime_component_val_t *val) {
    switch (val->kind) {
        case WASMTIME_COMPONENT_BOOL:
            lua_pushboolean(L, val->of.boolean);
            break;
        case WASMTIME_COMPONENT_S8:   lua_pushinteger(L, val->of.s8); break;
        case WASMTIME_COMPONENT_U8:   lua_pushinteger(L, val->of.u8); break;
        case WASMTIME_COMPONENT_S16:  lua_pushinteger(L, val->of.s16); break;
        case WASMTIME_COMPONENT_U16:  lua_pushinteger(L, val->of.u16); break;
        case WASMTIME_COMPONENT_S32:  lua_pushinteger(L, val->of.s32); break;
        case WASMTIME_COMPONENT_U32:  lua_pushinteger(L, val->of.u32); break;
        case WASMTIME_COMPONENT_S64:  lua_pushinteger(L, (lua_Integer)val->of.s64); break;
        case WASMTIME_COMPONENT_U64:  lua_pushinteger(L, (lua_Integer)val->of.u64); break;
        case WASMTIME_COMPONENT_F32:  lua_pushnumber(L, (lua_Number)val->of.f32); break;
        case WASMTIME_COMPONENT_F64:  lua_pushnumber(L, (lua_Number)val->of.f64); break;
        case WASMTIME_COMPONENT_CHAR:
            lua_pushinteger(L, val->of.character);
            break;
        case WASMTIME_COMPONENT_STRING:
            lua_pushlstring(L, val->of.string.data, val->of.string.size);
            break;
        case WASMTIME_COMPONENT_ENUM:
            lua_pushlstring(L, val->of.enumeration.data, val->of.enumeration.size);
            break;
        case WASMTIME_COMPONENT_LIST: {
            lua_newtable(L);
            for (size_t i = 0; i < val->of.list.size; i++) {
                wmt_comp_val_to_lua(L, &val->of.list.data[i]);
                lua_rawseti(L, -2, (lua_Integer)i + 1);
            }
            break;
        }
        case WASMTIME_COMPONENT_RECORD: {
            lua_newtable(L);
            for (size_t i = 0; i < val->of.record.size; i++) {
                const wasmtime_component_valrecord_entry_t *e = &val->of.record.data[i];
                wmt_comp_val_to_lua(L, &e->val);
                lua_pushlstring(L, e->name.data, e->name.size);
                lua_insert(L, -2);
                lua_settable(L, -3);
            }
            break;
        }
        case WASMTIME_COMPONENT_TUPLE: {
            lua_newtable(L);
            for (size_t i = 0; i < val->of.tuple.size; i++) {
                wmt_comp_val_to_lua(L, &val->of.tuple.data[i]);
                lua_rawseti(L, -2, (lua_Integer)i + 1);
            }
            break;
        }
        case WASMTIME_COMPONENT_VARIANT: {
            lua_newtable(L);
            if (val->of.variant.val) {
                wmt_comp_val_to_lua(L, val->of.variant.val);
                lua_pushlstring(L, val->of.variant.discriminant.data,
                                val->of.variant.discriminant.size);
                lua_insert(L, -2);
                lua_settable(L, -3);
            } else {
                /* 无载荷 variant：存 {case=true}，避免 Lua 表空（nil 值不存储） */
                lua_pushlstring(L, val->of.variant.discriminant.data,
                                val->of.variant.discriminant.size);
                lua_pushboolean(L, 1);
                lua_settable(L, -3);
            }
            break;
        }
        case WASMTIME_COMPONENT_OPTION:
            if (val->of.option) {
                wmt_comp_val_to_lua(L, val->of.option);
            } else {
                lua_pushnil(L);
            }
            break;
        case WASMTIME_COMPONENT_RESULT: {
            lua_newtable(L);
            if (val->of.result.is_ok) {
                if (val->of.result.val) wmt_comp_val_to_lua(L, val->of.result.val);
                else lua_pushnil(L);
                lua_setfield(L, -2, "ok");
            } else {
                if (val->of.result.val) wmt_comp_val_to_lua(L, val->of.result.val);
                else lua_pushnil(L);
                lua_setfield(L, -2, "err");
            }
            break;
        }
        case WASMTIME_COMPONENT_FLAGS: {
            lua_newtable(L);
            for (size_t i = 0; i < val->of.flags.size; i++) {
                const wasm_name_t *f = &val->of.flags.data[i];
                lua_pushlstring(L, f->data, f->size);
                lua_pushboolean(L, 1);
                lua_settable(L, -3);
            }
            break;
        }
        default:
            lua_pushnil(L);
            break;
    }
}

/* ============================================================
 * 值转换：Lua → component_val（按 valtype 递归）
 * 失败返回 -1（并清理 out 已分配的内容），成功返回 0
 * ============================================================ */

static int wmt_comp_val_from_lua(lua_State *L, int idx,
                                 const wasmtime_component_valtype_t *ty,
                                 wasmtime_component_val_t *out) {
    memset(out, 0, sizeof(*out));

    switch (ty->kind) {
        case WASMTIME_COMPONENT_VALTYPE_BOOL:
            out->kind = WASMTIME_COMPONENT_BOOL;
            out->of.boolean = lua_toboolean(L, idx);
            return 0;
        case WASMTIME_COMPONENT_VALTYPE_S8:
            out->kind = WASMTIME_COMPONENT_S8;
            out->of.s8 = (int8_t)lua_tointeger(L, idx);
            return 0;
        case WASMTIME_COMPONENT_VALTYPE_U8:
            out->kind = WASMTIME_COMPONENT_U8;
            out->of.u8 = (uint8_t)lua_tointeger(L, idx);
            return 0;
        case WASMTIME_COMPONENT_VALTYPE_S16:
            out->kind = WASMTIME_COMPONENT_S16;
            out->of.s16 = (int16_t)lua_tointeger(L, idx);
            return 0;
        case WASMTIME_COMPONENT_VALTYPE_U16:
            out->kind = WASMTIME_COMPONENT_U16;
            out->of.u16 = (uint16_t)lua_tointeger(L, idx);
            return 0;
        case WASMTIME_COMPONENT_VALTYPE_S32:
            out->kind = WASMTIME_COMPONENT_S32;
            out->of.s32 = (int32_t)lua_tointeger(L, idx);
            return 0;
        case WASMTIME_COMPONENT_VALTYPE_U32:
            out->kind = WASMTIME_COMPONENT_U32;
            out->of.u32 = (uint32_t)lua_tointeger(L, idx);
            return 0;
        case WASMTIME_COMPONENT_VALTYPE_S64:
            out->kind = WASMTIME_COMPONENT_S64;
            out->of.s64 = (int64_t)lua_tointeger(L, idx);
            return 0;
        case WASMTIME_COMPONENT_VALTYPE_U64:
            out->kind = WASMTIME_COMPONENT_U64;
            out->of.u64 = (uint64_t)lua_tointeger(L, idx);
            return 0;
        case WASMTIME_COMPONENT_VALTYPE_F32:
            out->kind = WASMTIME_COMPONENT_F32;
            out->of.f32 = (float32_t)lua_tonumber(L, idx);
            return 0;
        case WASMTIME_COMPONENT_VALTYPE_F64:
            out->kind = WASMTIME_COMPONENT_F64;
            out->of.f64 = (float64_t)lua_tonumber(L, idx);
            return 0;
        case WASMTIME_COMPONENT_VALTYPE_CHAR:
            out->kind = WASMTIME_COMPONENT_CHAR;
            out->of.character = (uint32_t)lua_tointeger(L, idx);
            return 0;
        case WASMTIME_COMPONENT_VALTYPE_STRING: {
            size_t len;
            const char *s = lua_tolstring(L, idx, &len);
            if (!s) return -1;
            out->kind = WASMTIME_COMPONENT_STRING;
            wasm_byte_vec_new_uninitialized(&out->of.string, len);
            if (len > 0 && out->of.string.data)
                memcpy(out->of.string.data, s, len);
            return 0;
        }
        case WASMTIME_COMPONENT_VALTYPE_LIST: {
            if (!lua_istable(L, idx)) return -1;
            wasmtime_component_valtype_t ety;
            wasmtime_component_list_type_element(ty->of.list, &ety);
            size_t n = lua_rawlen(L, idx);
            wasmtime_component_val_t *elems = (wasmtime_component_val_t*)
                calloc(n ? n : 1, sizeof(*elems));
            if (!elems) return -1;
            out->kind = WASMTIME_COMPONENT_LIST;
            out->of.list.size = n;
            out->of.list.data = elems;
            for (size_t i = 0; i < n; i++) {
                lua_rawgeti(L, idx, (lua_Integer)i + 1);
                if (wmt_comp_val_from_lua(L, -1, &ety, &elems[i]) != 0) {
                    lua_pop(L, 1);
                    wasmtime_component_val_delete(out);
                    return -1;
                }
                lua_pop(L, 1);
            }
            return 0;
        }
        case WASMTIME_COMPONENT_VALTYPE_RECORD: {
            if (!lua_istable(L, idx)) return -1;
            size_t n = wasmtime_component_record_type_field_count(ty->of.record);
            wasmtime_component_valrecord_entry_t *entries =
                (wasmtime_component_valrecord_entry_t*)
                calloc(n ? n : 1, sizeof(*entries));
            if (!entries) return -1;
            out->kind = WASMTIME_COMPONENT_RECORD;
            out->of.record.size = n;
            out->of.record.data = entries;
            for (size_t i = 0; i < n; i++) {
                const char *fname; size_t fnamelen;
                wasmtime_component_valtype_t fty;
                if (!wasmtime_component_record_type_field_nth(
                        ty->of.record, i, &fname, &fnamelen, &fty)) {
                    wasmtime_component_val_delete(out);
                    return -1;
                }
                wasm_byte_vec_new_uninitialized(&entries[i].name, fnamelen);
                if (fnamelen > 0 && entries[i].name.data)
                    memcpy(entries[i].name.data, fname, fnamelen);
                lua_pushlstring(L, fname, fnamelen);
                lua_gettable(L, idx);
                if (wmt_comp_val_from_lua(L, -1, &fty, &entries[i].val) != 0) {
                    lua_pop(L, 1);
                    wasmtime_component_val_delete(out);
                    return -1;
                }
                lua_pop(L, 1);
            }
            return 0;
        }
        case WASMTIME_COMPONENT_VALTYPE_TUPLE: {
            if (!lua_istable(L, idx)) return -1;
            size_t n = wasmtime_component_tuple_type_types_count(ty->of.tuple);
            wasmtime_component_val_t *items = (wasmtime_component_val_t*)
                calloc(n ? n : 1, sizeof(*items));
            if (!items) return -1;
            out->kind = WASMTIME_COMPONENT_TUPLE;
            out->of.tuple.size = n;
            out->of.tuple.data = items;
            for (size_t i = 0; i < n; i++) {
                wasmtime_component_valtype_t tty;
                if (!wasmtime_component_tuple_type_types_nth(
                        ty->of.tuple, i, &tty)) {
                    wasmtime_component_val_delete(out);
                    return -1;
                }
                lua_rawgeti(L, idx, (lua_Integer)i + 1);
                if (wmt_comp_val_from_lua(L, -1, &tty, &items[i]) != 0) {
                    lua_pop(L, 1);
                    wasmtime_component_val_delete(out);
                    return -1;
                }
                lua_pop(L, 1);
            }
            return 0;
        }
        case WASMTIME_COMPONENT_VALTYPE_VARIANT: {
            size_t ncase = wasmtime_component_variant_type_case_count(ty->of.variant);
            const char *cname = NULL; size_t cnamelen = 0;
            int payload_present = 0;
            int payload_idx = idx;
            char keybuf[64];

            if (lua_type(L, idx) == LUA_TSTRING) {
                cname = lua_tolstring(L, idx, &cnamelen);
            } else if (lua_istable(L, idx)) {
                /* 数组形式 { name, payload } */
                lua_rawgeti(L, idx, 1);
                if (lua_isstring(L, -1)) {
                    cname = lua_tolstring(L, -1, &cnamelen);
                    payload_present = 1;
                    payload_idx = -1; /* 栈顶保留为 name，payload 是下一元素 */
                    /* 取 payload：rawgeti(idx,2) 放到栈顶 */
                    lua_pop(L, 1);
                    lua_rawgeti(L, idx, 2);
                    if (lua_isnil(L, -1)) { payload_present = 0; lua_pop(L, 1); }
                    else payload_idx = -1;
                } else {
                    lua_pop(L, 1);
                    /* 单键 map 形式 { [name]=payload } */
                    lua_pushnil(L);
                    if (lua_next(L, idx)) {
                        if (lua_type(L, -2) == LUA_TSTRING) {
                            cname = lua_tolstring(L, -2, &cnamelen);
                            payload_present = 1;
                            payload_idx = -1; /* 栈顶是 payload */
                        } else {
                            lua_pop(L, 2);
                        }
                    } else {
                        lua_pop(L, 1);
                    }
                }
            } else {
                return -1;
            }

            int found = -1;
            wasmtime_component_valtype_t pty;
            for (size_t i = 0; i < ncase; i++) {
                const char *cn; size_t cnl; bool hasp;
                if (wasmtime_component_variant_type_case_nth(
                        ty->of.variant, i, &cn, &cnl, &hasp, &pty)) {
                    if (cname && cnl == cnamelen &&
                        memcmp(cn, cname, cnamelen) == 0) {
                        found = (int)i;
                        if (!hasp) payload_present = 0;
                        break;
                    }
                }
            }
            if (found < 0) {
                if (lua_istable(L, idx)) lua_settop(L, idx); /* 清理临时栈 */
                return -1;
            }

            out->kind = WASMTIME_COMPONENT_VARIANT;
            wasm_byte_vec_new_uninitialized(&out->of.variant.discriminant, cnamelen);
            if (cnamelen > 0 && out->of.variant.discriminant.data)
                memcpy(out->of.variant.discriminant.data, cname, cnamelen);
            out->of.variant.val = NULL;
            if (payload_present) {
                wasmtime_component_val_t tmp;
                int rc = wmt_comp_val_from_lua(L, payload_idx, &pty, &tmp);
                if (rc != 0) {
                    wasm_byte_vec_delete(&out->of.variant.discriminant);
                    if (lua_istable(L, idx)) lua_settop(L, idx);
                    return -1;
                }
                out->of.variant.val = wasmtime_component_val_new(&tmp);
                if (lua_istable(L, idx)) lua_settop(L, idx);
            }
            (void)keybuf;
            return 0;
        }
        case WASMTIME_COMPONENT_VALTYPE_ENUM: {
            if (lua_type(L, idx) != LUA_TSTRING) return -1;
            size_t len; const char *s = lua_tolstring(L, idx, &len);
            size_t n = wasmtime_component_enum_type_names_count(ty->of.enum_);
            int found = 0;
            for (size_t i = 0; i < n; i++) {
                const char *nm; size_t nml;
                if (wasmtime_component_enum_type_names_nth(
                        ty->of.enum_, i, &nm, &nml)) {
                    if (nml == len && memcmp(nm, s, len) == 0) { found = 1; break; }
                }
            }
            if (!found) return -1;
            out->kind = WASMTIME_COMPONENT_ENUM;
            wasm_byte_vec_new_uninitialized(&out->of.enumeration, len);
            if (len > 0 && out->of.enumeration.data)
                memcpy(out->of.enumeration.data, s, len);
            return 0;
        }
        case WASMTIME_COMPONENT_VALTYPE_OPTION: {
            out->kind = WASMTIME_COMPONENT_OPTION;
            if (lua_isnil(L, idx)) {
                out->of.option = NULL;
                return 0;
            }
            wasmtime_component_valtype_t ity;
            wasmtime_component_option_type_ty(ty->of.option, &ity);
            wasmtime_component_val_t tmp;
            if (wmt_comp_val_from_lua(L, idx, &ity, &tmp) != 0) return -1;
            out->of.option = wasmtime_component_val_new(&tmp);
            return 0;
        }
        case WASMTIME_COMPONENT_VALTYPE_RESULT: {
            out->kind = WASMTIME_COMPONENT_RESULT;
            if (!lua_istable(L, idx)) return -1;
            /* 支持 { ok = v } 或 { err = v }，或布尔 {ok=true}/{err=true} */
            out->of.result.is_ok = 0;
            out->of.result.val = NULL;

            wasmtime_component_valtype_t oty, ety;
            bool has_ok = wasmtime_component_result_type_ok(ty->of.result, &oty);
            bool has_err = wasmtime_component_result_type_err(ty->of.result, &ety);

            lua_getfield(L, idx, "ok");
            if (!lua_isnil(L, -1)) {
                out->of.result.is_ok = 1;
                if (has_ok) {
                    wasmtime_component_val_t tmp;
                    if (wmt_comp_val_from_lua(L, -1, &oty, &tmp) != 0) {
                        lua_pop(L, 1);
                        return -1;
                    }
                    out->of.result.val = wasmtime_component_val_new(&tmp);
                }
                lua_pop(L, 1);
                return 0;
            }
            lua_pop(L, 1);

            lua_getfield(L, idx, "err");
            if (!lua_isnil(L, -1)) {
                out->of.result.is_ok = 0;
                if (has_err) {
                    wasmtime_component_val_t tmp;
                    if (wmt_comp_val_from_lua(L, -1, &ety, &tmp) != 0) {
                        lua_pop(L, 1);
                        return -1;
                    }
                    out->of.result.val = wasmtime_component_val_new(&tmp);
                }
                lua_pop(L, 1);
                return 0;
            }
            lua_pop(L, 1);
            return -1;
        }
        case WASMTIME_COMPONENT_VALTYPE_FLAGS: {
            out->kind = WASMTIME_COMPONENT_FLAGS;
            if (!lua_istable(L, idx)) return -1;
            size_t n = wasmtime_component_flags_type_names_count(ty->of.flags);
            /* 收集被置位的 flag 名 */
            size_t cnt = 0;
            for (size_t i = 0; i < n; i++) {
                const char *nm; size_t nml;
                wasmtime_component_flags_type_names_nth(ty->of.flags, i, &nm, &nml);
                lua_pushlstring(L, nm, nml);
                int on = lua_toboolean(L, -1);
                lua_pop(L, 1);
                /* 用 rawget 取表内值 */
                lua_pushlstring(L, nm, nml);
                lua_gettable(L, idx);
                int v = lua_toboolean(L, -1);
                lua_pop(L, 1);
                if (v) cnt++;
            }
            wasm_name_t *flags = (wasm_name_t*)calloc(cnt ? cnt : 1, sizeof(*flags));
            if (!flags) return -1;
            out->of.flags.size = cnt;
            out->of.flags.data = flags;
            size_t wi = 0;
            for (size_t i = 0; i < n; i++) {
                const char *nm; size_t nml;
                wasmtime_component_flags_type_names_nth(ty->of.flags, i, &nm, &nml);
                lua_pushlstring(L, nm, nml);
                lua_gettable(L, idx);
                if (lua_toboolean(L, -1)) {
                    wasm_byte_vec_new_uninitialized(&flags[wi], nml);
                    if (nml > 0 && flags[wi].data)
                        memcpy(flags[wi].data, nm, nml);
                    wi++;
                }
                lua_pop(L, 1);
            }
            return 0;
        }
        default:
            /* resource / map / future / stream 暂不支持 */
            return -1;
    }
}

/* ============================================================
 * Component 对象
 * ============================================================ */

static int wmt_component_gc(lua_State *L) {
    wmt_Component *wc = (wmt_Component*)luaL_checkudata(L, 1, WMT_COMPONENT);
    if (wc->component) {
        wasmtime_component_delete(wc->component);
        wc->component = NULL;
    }
    if (wc->engine_ref != LUA_NOREF) {
        luaL_unref(L, LUA_REGISTRYINDEX, wc->engine_ref);
        wc->engine_ref = LUA_NOREF;
    }
    return 0;
}

/**
 * @brief wasmtime.newComponent(engine, bytes) → component
 * 从 component 二进制（preview2 组件）创建组件对象。
 */
int l_new_component(lua_State *L) {
    wmt_Engine *we = (wmt_Engine*)luaL_checkudata(L, 1, WMT_ENGINE);
    size_t len;
    const char *buf = luaL_checklstring(L, 2, &len);

    wasmtime_component_t *comp = NULL;
    wasmtime_error_t *err = wasmtime_component_new(
        we->engine, (const uint8_t*)buf, len, &comp);
    if (err) {
        wasm_name_t msg;
        wasmtime_error_message(err, &msg);
        lua_pushnil(L);
        lua_pushlstring(L, msg.data, msg.size);
        wasm_byte_vec_delete(&msg);
        wasmtime_error_delete(err);
        return 2;
    }

    wmt_Component *wc = (wmt_Component*)lua_newuserdata(L, sizeof(wmt_Component));
    wc->component = comp;
    wc->engine = we->engine;
    lua_pushvalue(L, 1);
    wc->engine_ref = luaL_ref(L, LUA_REGISTRYINDEX);
    luaL_getmetatable(L, WMT_COMPONENT);
    lua_setmetatable(L, -2);
    return 1;
}

/* ============================================================
 * Component Linker
 * ============================================================ */

static int wmt_component_linker_gc(lua_State *L) {
    wmt_ComponentLinker *wl = (wmt_ComponentLinker*)
        luaL_checkudata(L, 1, WMT_COMPONENT_LINKER);
    if (wl->linker) {
        wasmtime_component_linker_delete(wl->linker);
        wl->linker = NULL;
    }
    if (wl->engine_ref != LUA_NOREF) {
        luaL_unref(L, LUA_REGISTRYINDEX, wl->engine_ref);
        wl->engine_ref = LUA_NOREF;
    }
    return 0;
}

/**
 * @brief wasmtime.newComponentLinker(engine) → comp_linker
 */
int l_new_component_linker(lua_State *L) {
    wmt_Engine *we = (wmt_Engine*)luaL_checkudata(L, 1, WMT_ENGINE);
    wasmtime_component_linker_t *linker = wasmtime_component_linker_new(we->engine);
    if (!linker) return luaL_error(L, "newComponentLinker: failed");

    wmt_ComponentLinker *wl = (wmt_ComponentLinker*)
        lua_newuserdata(L, sizeof(wmt_ComponentLinker));
    wl->linker = linker;
    wl->engine = we->engine;
    lua_pushvalue(L, 1);
    wl->engine_ref = luaL_ref(L, LUA_REGISTRYINDEX);
    luaL_getmetatable(L, WMT_COMPONENT_LINKER);
    lua_setmetatable(L, -2);
    return 1;
}

/**
 * @brief comp_linker:addWasiP2() → comp_linker
 * 定义所有 WASI preview2 接口（配置取自 store:setWasi）。
 */
static int l_comp_linker_add_wasi(lua_State *L) {
    wmt_ComponentLinker *wl = (wmt_ComponentLinker*)
        luaL_checkudata(L, 1, WMT_COMPONENT_LINKER);
    wasmtime_error_t *err = wasmtime_component_linker_add_wasip2(wl->linker);
    if (err) {
        wasm_name_t msg;
        wasmtime_error_message(err, &msg);
        lua_pushnil(L);
        lua_pushlstring(L, msg.data, msg.size);
        wasm_byte_vec_delete(&msg);
        wasmtime_error_delete(err);
        return 2;
    }
    lua_pushvalue(L, 1);
    return 1;
}

/**
 * @brief comp_linker:instantiate(store, component) → comp_instance | nil, err
 */
static int l_comp_linker_instantiate(lua_State *L) {
    wmt_ComponentLinker *wl = (wmt_ComponentLinker*)
        luaL_checkudata(L, 1, WMT_COMPONENT_LINKER);
    wmt_Store *ws = (wmt_Store*)luaL_checkudata(L, 2, WMT_STORE);
    wmt_Component *wc = (wmt_Component*)luaL_checkudata(L, 3, WMT_COMPONENT);

    wasmtime_component_instance_t inst;
    wasmtime_error_t *err = wasmtime_component_linker_instantiate(
        wl->linker, wasmtime_store_context(ws->store), wc->component, &inst);
    if (err) {
        wasm_name_t msg;
        wasmtime_error_message(err, &msg);
        lua_pushnil(L);
        lua_pushlstring(L, msg.data, msg.size);
        wasm_byte_vec_delete(&msg);
        wasmtime_error_delete(err);
        return 2;
    }

    wmt_ComponentInstance *wi = (wmt_ComponentInstance*)
        lua_newuserdata(L, sizeof(wmt_ComponentInstance));
    wi->instance = inst;
    wi->store = ws->store;
    lua_pushvalue(L, 2);
    wi->store_ref = luaL_ref(L, LUA_REGISTRYINDEX);
    luaL_getmetatable(L, WMT_COMPONENT_INSTANCE);
    lua_setmetatable(L, -2);
    return 1;
}

/* ============================================================
 * Component Instance
 * ============================================================ */

static int wmt_component_instance_gc(lua_State *L) {
    wmt_ComponentInstance *wi = (wmt_ComponentInstance*)
        luaL_checkudata(L, 1, WMT_COMPONENT_INSTANCE);
    if (wi->store_ref != LUA_NOREF) {
        luaL_unref(L, LUA_REGISTRYINDEX, wi->store_ref);
        wi->store_ref = LUA_NOREF;
    }
    return 0;
}

/**
 * @brief comp_instance:getExport(name, ...) → comp_func | nil
 * 支持多级路径访问嵌套 instance 导出（如 wasi:cli/run 下的 run）：
 *   inst:getExport("wasi:cli/run@0.2.0", "run")
 * 每一级 name 依次沿 instance 嵌套查询，最后一级必须解析为 func。
 */
static int l_comp_instance_get_export(lua_State *L) {
    wmt_ComponentInstance *wi = (wmt_ComponentInstance*)
        luaL_checkudata(L, 1, WMT_COMPONENT_INSTANCE);
    int n = lua_gettop(L);
    if (n < 2) return 0;
    wasmtime_context_t *ctx = wasmtime_store_context(wi->store);

    size_t namelen;
    const char *name = luaL_checklstring(L, 2, &namelen);
    wasmtime_component_export_index_t *idx =
        wasmtime_component_instance_get_export_index(
            &wi->instance, ctx, NULL, name, namelen);
    if (!idx) return 0;

    /* 后续路径段：逐级下钻（上一级 index 作为 instance 定位） */
    for (int k = 3; k <= n; k++) {
        size_t l;
        const char *nm = luaL_checklstring(L, k, &l);
        wasmtime_component_export_index_t *next =
            wasmtime_component_instance_get_export_index(
                &wi->instance, ctx, idx, nm, l);
        wasmtime_component_export_index_delete(idx);
        if (!next) return 0;
        idx = next;
    }

    wasmtime_component_func_t f;
    if (!wasmtime_component_instance_get_func(&wi->instance, ctx, idx, &f)) {
        wasmtime_component_export_index_delete(idx);
        return 0;
    }
    wasmtime_component_export_index_delete(idx);

    wmt_ComponentFunc *wf = (wmt_ComponentFunc*)
        lua_newuserdata(L, sizeof(wmt_ComponentFunc));
    wf->func = f;
    wf->store = wi->store;
    lua_pushvalue(L, 1);
    wf->store_ref = luaL_ref(L, LUA_REGISTRYINDEX);
    luaL_getmetatable(L, WMT_COMPONENT_FUNC);
    lua_setmetatable(L, -2);
    return 1;
}

/* ============================================================
 * Component Func
 * ============================================================ */

static int wmt_component_func_gc(lua_State *L) {
    wmt_ComponentFunc *wf = (wmt_ComponentFunc*)
        luaL_checkudata(L, 1, WMT_COMPONENT_FUNC);
    if (wf->store_ref != LUA_NOREF) {
        luaL_unref(L, LUA_REGISTRYINDEX, wf->store_ref);
        wf->store_ref = LUA_NOREF;
    }
    return 0;
}

/**
 * @brief comp_func:call(args...) → result | nil, err
 * 参数个数与类型由函数签名决定；结果按 signature 返回。
 */
static int l_comp_func_call(lua_State *L) {
    wmt_ComponentFunc *wf = (wmt_ComponentFunc*)
        luaL_checkudata(L, 1, WMT_COMPONENT_FUNC);
    wasmtime_context_t *ctx = wasmtime_store_context(wf->store);

    wasmtime_component_func_type_t *fty =
        wasmtime_component_func_type(&wf->func, ctx);
    if (!fty) return luaL_error(L, "component func: unable to get type");

    size_t nparams = wasmtime_component_func_type_param_count(fty);
    size_t nargs = (size_t)(lua_gettop(L) - 1);
    if (nargs < nparams) {
        wasmtime_component_func_type_delete(fty);
        return luaL_error(L, "component func: expected %d arguments, got %d",
                          (int)nparams, (int)nargs);
    }

    wasmtime_component_val_t *args = (wasmtime_component_val_t*)
        calloc(nparams ? nparams : 1, sizeof(*args));
    if (!args) {
        wasmtime_component_func_type_delete(fty);
        return luaL_error(L, "component func: out of memory");
    }

    for (size_t i = 0; i < nparams; i++) {
        wasmtime_component_valtype_t pty;
        const char *pname = NULL; size_t pnamelen = 0;
        if (!wasmtime_component_func_type_param_nth(
                fty, i, &pname, &pnamelen, &pty)) {
            for (size_t j = 0; j < i; j++)
                wasmtime_component_val_delete(&args[j]);
            free(args);
            wasmtime_component_func_type_delete(fty);
            return luaL_error(L, "component func: bad param type");
        }
        if (wmt_comp_val_from_lua(L, (int)i + 2, &pty, &args[i]) != 0) {
            for (size_t j = 0; j <= i; j++)
                wasmtime_component_val_delete(&args[j]);
            free(args);
            wasmtime_component_func_type_delete(fty);
            return luaL_error(L, "component func: argument %d type mismatch",
                              (int)(i + 1));
        }
    }

    wasmtime_component_valtype_t rty;
    bool has_result = wasmtime_component_func_type_result(fty, &rty);
    size_t nresults = has_result ? 1 : 0;
    wasmtime_component_val_t results[1];
    memset(results, 0, sizeof(results));

    wasmtime_error_t *err = wasmtime_component_func_call(
        &wf->func, ctx, args, nparams, results, nresults);

    for (size_t i = 0; i < nparams; i++)
        wasmtime_component_val_delete(&args[i]);
    free(args);
    wasmtime_component_func_type_delete(fty);

    if (err) {
        wasm_name_t msg;
        wasmtime_error_message(err, &msg);
        lua_pushnil(L);
        lua_pushlstring(L, msg.data, msg.size);
        wasm_byte_vec_delete(&msg);
        wasmtime_error_delete(err);
        return 2;
    }

    if (has_result) {
        wmt_comp_val_to_lua(L, &results[0]);
        wasmtime_component_val_delete(&results[0]);
        return 1;
    }
    return 0;
}

/* ============================================================
 * comp_func:getType() — 类型反射
 * ============================================================ */

static const char *wmt_comp_kind_str(wasmtime_component_valtype_kind_t k) {
    switch (k) {
        case WASMTIME_COMPONENT_VALTYPE_BOOL:   return "bool";
        case WASMTIME_COMPONENT_VALTYPE_S8:     return "s8";
        case WASMTIME_COMPONENT_VALTYPE_U8:     return "u8";
        case WASMTIME_COMPONENT_VALTYPE_S16:    return "s16";
        case WASMTIME_COMPONENT_VALTYPE_U16:    return "u16";
        case WASMTIME_COMPONENT_VALTYPE_S32:    return "s32";
        case WASMTIME_COMPONENT_VALTYPE_U32:    return "u32";
        case WASMTIME_COMPONENT_VALTYPE_S64:    return "s64";
        case WASMTIME_COMPONENT_VALTYPE_U64:    return "u64";
        case WASMTIME_COMPONENT_VALTYPE_F32:    return "f32";
        case WASMTIME_COMPONENT_VALTYPE_F64:    return "f64";
        case WASMTIME_COMPONENT_VALTYPE_CHAR:   return "char";
        case WASMTIME_COMPONENT_VALTYPE_STRING: return "string";
        case WASMTIME_COMPONENT_VALTYPE_LIST:   return "list";
        case WASMTIME_COMPONENT_VALTYPE_RECORD: return "record";
        case WASMTIME_COMPONENT_VALTYPE_TUPLE:  return "tuple";
        case WASMTIME_COMPONENT_VALTYPE_VARIANT:return "variant";
        case WASMTIME_COMPONENT_VALTYPE_ENUM:   return "enum";
        case WASMTIME_COMPONENT_VALTYPE_OPTION: return "option";
        case WASMTIME_COMPONENT_VALTYPE_RESULT: return "result";
        case WASMTIME_COMPONENT_VALTYPE_FLAGS:  return "flags";
        case WASMTIME_COMPONENT_VALTYPE_BORROW: return "borrow";
        case WASMTIME_COMPONENT_VALTYPE_OWN:    return "own";
        default: return "unknown";
    }
}

/**
 * @brief comp_func:getType() → params, result
 * params: 参数类型字符串数组；result: 返回值类型字符串或 nil。
 */
static int l_comp_func_get_type(lua_State *L) {
    wmt_ComponentFunc *wf = (wmt_ComponentFunc*)
        luaL_checkudata(L, 1, WMT_COMPONENT_FUNC);
    wasmtime_context_t *ctx = wasmtime_store_context(wf->store);

    wasmtime_component_func_type_t *fty =
        wasmtime_component_func_type(&wf->func, ctx);
    if (!fty) return luaL_error(L, "component func: unable to get type");

    size_t nparams = wasmtime_component_func_type_param_count(fty);
    lua_createtable(L, (int)nparams, 0);
    for (size_t i = 0; i < nparams; i++) {
        wasmtime_component_valtype_t pty;
        const char *pname = NULL; size_t pnamelen = 0;
        if (wasmtime_component_func_type_param_nth(
                fty, i, &pname, &pnamelen, &pty)) {
            lua_pushstring(L, wmt_comp_kind_str(pty.kind));
        } else {
            lua_pushstring(L, "?");
        }
        lua_rawseti(L, -2, (lua_Integer)i + 1);
    }

    wasmtime_component_valtype_t rty;
    if (wasmtime_component_func_type_result(fty, &rty)) {
        lua_pushstring(L, wmt_comp_kind_str(rty.kind));
    } else {
        lua_pushnil(L);
    }

    wasmtime_component_func_type_delete(fty);
    return 2;
}

/* ============================================================
 * comp_func:callAsync() — 组件异步调用 + component future
 * ============================================================ */

static void wmt_cfuture_release(lua_State *L, wmt_ComponentFuture *fu) {
    if (fu->future) {
        wasmtime_call_future_delete(fu->future);
        fu->future = NULL;
    }
    if (fu->args) {
        for (size_t i = 0; i < fu->nargs; i++)
            wasmtime_component_val_delete(&fu->args[i]);
        free(fu->args);
        fu->args = NULL;
    }
    if (fu->results) {
        for (size_t i = 0; i < fu->nresults; i++)
            wasmtime_component_val_delete(&fu->results[i]);
        free(fu->results);
        fu->results = NULL;
    }
    if (fu->trap) {
        wasm_trap_delete(fu->trap);
        fu->trap = NULL;
    }
    if (fu->error) {
        wasmtime_error_delete(fu->error);
        fu->error = NULL;
    }
    if (fu->store_ref != LUA_NOREF && fu->store_ref != 0) {
        luaL_unref(L, LUA_REGISTRYINDEX, fu->store_ref);
        fu->store_ref = LUA_NOREF;
    }
}

/**
 * @brief comp_func:callAsync(...) → component_future | nil, trap, msg
 * 异步调用组件函数。同一 store 同时只能有一个存活 future。
 * args/results 由 future 持有，须在 future 删除后释放。
 */
static int l_comp_func_call_async(lua_State *L) {
    wmt_ComponentFunc *wf = (wmt_ComponentFunc*)
        luaL_checkudata(L, 1, WMT_COMPONENT_FUNC);
    wasmtime_context_t *ctx = wasmtime_store_context(wf->store);

    wasmtime_component_func_type_t *fty =
        wasmtime_component_func_type(&wf->func, ctx);
    if (!fty) return luaL_error(L, "component func: unable to get type");

    size_t nparams = wasmtime_component_func_type_param_count(fty);
    size_t nargs = (size_t)(lua_gettop(L) - 1);
    if (nargs < nparams) {
        wasmtime_component_func_type_delete(fty);
        return luaL_error(L, "component func: expected %d arguments, got %d",
                          (int)nparams, (int)nargs);
    }

    wasmtime_component_val_t *args = (wasmtime_component_val_t*)
        calloc(nparams ? nparams : 1, sizeof(*args));
    if (!args) {
        wasmtime_component_func_type_delete(fty);
        return luaL_error(L, "component func: out of memory");
    }

    size_t converted = 0;
    for (size_t i = 0; i < nparams; i++) {
        wasmtime_component_valtype_t pty;
        const char *pname = NULL; size_t pnamelen = 0;
        if (!wasmtime_component_func_type_param_nth(
                fty, i, &pname, &pnamelen, &pty)) {
            goto conv_fail;
        }
        if (wmt_comp_val_from_lua(L, (int)i + 2, &pty, &args[i]) != 0) {
            goto conv_fail;
        }
        converted++;
    }
    goto conv_ok;

conv_fail:
    for (size_t j = 0; j < converted; j++)
        wasmtime_component_val_delete(&args[j]);
    free(args);
    wasmtime_component_func_type_delete(fty);
    return luaL_error(L, "component func: argument conversion failed");

conv_ok:
    ;
    wasmtime_component_valtype_t rty;
    bool has_result = wasmtime_component_func_type_result(fty, &rty);
    size_t nresults = has_result ? 1 : 0;

    wasmtime_component_val_t *results = (wasmtime_component_val_t*)
        calloc(nresults ? nresults : 1, sizeof(*results));
    if (!results) {
        for (size_t j = 0; j < nparams; j++)
            wasmtime_component_val_delete(&args[j]);
        free(args);
        wasmtime_component_func_type_delete(fty);
        return luaL_error(L, "component func: out of memory");
    }

    wmt_ComponentFuture *fu = (wmt_ComponentFuture*)
        lua_newuserdata(L, sizeof(wmt_ComponentFuture));
    memset(fu, 0, sizeof(wmt_ComponentFuture));
    fu->store_ref = LUA_NOREF;
    fu->args = args;
    fu->nargs = nparams;
    fu->results = results;
    fu->nresults = nresults;

    fu->future = wasmtime_component_func_call_async(
        &wf->func, ctx, args, nparams, results, nresults,
        &fu->error);

    wasmtime_component_func_type_delete(fty);

    if (fu->future == NULL) {
        fu->done = 1;
    } else if (wasmtime_call_future_poll(fu->future)) {
        wasmtime_call_future_delete(fu->future);
        fu->future = NULL;
        fu->done = 1;
    }

    /* 同步完成且有 trap/error → 转 nil, trap, msg */
    if (fu->done && (fu->trap || fu->error)) {
        if (fu->trap) {
            lua_pushnil(L);
            wmt_Trap *wt = (wmt_Trap*)lua_newuserdata(L, sizeof(wmt_Trap));
            wt->trap = fu->trap;
            fu->trap = NULL;
            luaL_getmetatable(L, WMT_TRAP);
            lua_setmetatable(L, -2);

            wasm_name_t msg = {0};
            wasm_trap_message(wt->trap, &msg);
            size_t msz = msg.size;
            while (msz > 0 && msg.data[msz - 1] == '\0') msz--;
            lua_pushlstring(L, msg.data, msz);
            wasm_byte_vec_delete(&msg);
        } else {
            wasm_name_t msg = {0};
            wasmtime_error_message(fu->error, &msg);
            lua_pushnil(L);
            lua_pushnil(L);
            lua_pushlstring(L, msg.data, msg.size);
            wasm_byte_vec_delete(&msg);
        }
        wmt_cfuture_release(L, fu);
        lua_pop(L, 1);
        return 3;
    }

    /* 保护 store 生命周期 */
    fu->store_ref = LUA_NOREF;
    if (wf->store_ref != LUA_NOREF) {
        lua_rawgeti(L, LUA_REGISTRYINDEX, wf->store_ref);
        fu->store_ref = luaL_ref(L, LUA_REGISTRYINDEX);
    }

    luaL_getmetatable(L, WMT_CFUTURE);
    lua_setmetatable(L, -2);
    return 1;
}

/* component future 方法 */

static int l_cfuture_poll(lua_State *L) {
    wmt_ComponentFuture *fu = (wmt_ComponentFuture*)
        luaL_checkudata(L, 1, WMT_CFUTURE);
    if (!fu->done && fu->future) {
        if (wasmtime_call_future_poll(fu->future)) {
            wasmtime_call_future_delete(fu->future);
            fu->future = NULL;
            fu->done = 1;
        }
    }
    lua_pushboolean(L, fu->done);
    return 1;
}

static int l_cfuture_done(lua_State *L) {
    wmt_ComponentFuture *fu = (wmt_ComponentFuture*)
        luaL_checkudata(L, 1, WMT_CFUTURE);
    lua_pushboolean(L, fu->done);
    return 1;
}

static int wmt_cfuture_push_result(lua_State *L, wmt_ComponentFuture *fu) {
    if (fu->trap) {
        lua_pushnil(L);
        wmt_Trap *wt = (wmt_Trap*)lua_newuserdata(L, sizeof(wmt_Trap));
        wt->trap = fu->trap;
        fu->trap = NULL;
        luaL_getmetatable(L, WMT_TRAP);
        lua_setmetatable(L, -2);

        wasm_name_t msg = {0};
        wasm_trap_message(wt->trap, &msg);
        size_t msz = msg.size;
        while (msz > 0 && msg.data[msz - 1] == '\0') msz--;
        lua_pushlstring(L, msg.data, msz);
        wasm_byte_vec_delete(&msg);
        return 3;
    }
    if (fu->error) {
        wasm_name_t msg = {0};
        wasmtime_error_message(fu->error, &msg);
        lua_pushnil(L);
        lua_pushnil(L);
        lua_pushlstring(L, msg.data, msg.size);
        wasm_byte_vec_delete(&msg);
        return 3;
    }
    int retc = 0;
    for (size_t i = 0; i < fu->nresults; i++) {
        wmt_comp_val_to_lua(L, &fu->results[i]);
        retc++;
    }
    if (retc == 0) return 0;
    return retc;
}

static int l_cfuture_result(lua_State *L) {
    wmt_ComponentFuture *fu = (wmt_ComponentFuture*)
        luaL_checkudata(L, 1, WMT_CFUTURE);
    if (!fu->done) {
        lua_pushnil(L);
        return 1;
    }
    return wmt_cfuture_push_result(L, fu);
}

static int l_cfuture_wait(lua_State *L) {
    wmt_ComponentFuture *fu = (wmt_ComponentFuture*)
        luaL_checkudata(L, 1, WMT_CFUTURE);
    lua_Integer timeout_ms = -1;
    if (!lua_isnoneornil(L, 2)) {
        timeout_ms = luaL_checkinteger(L, 2);
    }

    clock_t start = clock();
    while (!fu->done) {
        if (fu->future) {
            if (wasmtime_call_future_poll(fu->future)) {
                wasmtime_call_future_delete(fu->future);
                fu->future = NULL;
                fu->done = 1;
                break;
            }
        } else {
            fu->done = 1;
            break;
        }
        if (timeout_ms >= 0) {
            double elapsed_ms = (double)(clock() - start) * 1000.0 / CLOCKS_PER_SEC;
            if (elapsed_ms > (double)timeout_ms) {
                lua_pushnil(L);
                lua_pushnil(L);
                lua_pushliteral(L, "component future wait timeout");
                return 3;
            }
        }
    }
    return wmt_cfuture_push_result(L, fu);
}

static int l_cfuture_delete(lua_State *L) {
    wmt_ComponentFuture *fu = (wmt_ComponentFuture*)
        luaL_checkudata(L, 1, WMT_CFUTURE);
    wmt_cfuture_release(L, fu);
    return 0;
}

static int wmt_cfuture_gc(lua_State *L) {
    wmt_ComponentFuture *fu = (wmt_ComponentFuture*)
        luaL_checkudata(L, 1, WMT_CFUTURE);
    wmt_cfuture_release(L, fu);
    return 0;
}

/* ============================================================
 * 方法表
 * ============================================================ */

const struct luaL_Reg wmt_component_methods[] = {
    {"__gc", wmt_component_gc},
    {NULL, NULL}
};

const struct luaL_Reg wmt_component_linker_methods[] = {
    {"addWasiP2",    l_comp_linker_add_wasi},
    {"instantiate",  l_comp_linker_instantiate},
    {"__gc", wmt_component_linker_gc},
    {NULL, NULL}
};

const struct luaL_Reg wmt_component_instance_methods[] = {
    {"getExport", l_comp_instance_get_export},
    {"__gc", wmt_component_instance_gc},
    {NULL, NULL}
};

const struct luaL_Reg wmt_component_func_methods[] = {
    {"call",       l_comp_func_call},
    {"callAsync",  l_comp_func_call_async},
    {"getType",    l_comp_func_get_type},
    {"__gc", wmt_component_func_gc},
    {NULL, NULL}
};

const struct luaL_Reg wmt_component_future_methods[] = {
    {"poll",   l_cfuture_poll},
    {"done",   l_cfuture_done},
    {"result", l_cfuture_result},
    {"wait",   l_cfuture_wait},
    {"delete", l_cfuture_delete},
    {"__gc",   wmt_cfuture_gc},
    {NULL, NULL}
};
