#define lguilib_c
#define LUA_LIB

/**
 * @file lguilib.c
 * @brief LXCLUA GUI库 - 跨平台Lua绑定接口(Windows/Linux)
 * @author DiferLine
 * @version 1.0.0
 */

#include "gui_core.h"

/* ============================================================
 * 平台特定头文件与类型别名（跨平台适配）
 * ============================================================ */
#if defined(GUI_PLATFORM_WINDOWS)
    #include "gui_windows.h"
    typedef GuiWindow_Win GuiWindow;
    typedef GuiControl_Win GuiControl;
#elif defined(GUI_PLATFORM_LINUX)
    #include "gui_linux.h"
    typedef GuiWindow_Linux GuiWindow;
    typedef GuiControl_Linux GuiControl;
#endif

#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"
#include <stdlib.h>
#include <string.h>

/* ============================================================
 * 内部辅助宏和类型定义
 * ============================================================ */
#define GUI_WINDOW_MT    "LXCLuaGui.Window"
#define GUI_CONTROL_MT   "LXCLuaGui.Control"
#define MAX_CALLBACKS    256

/* ============================================================
 * 回调管理结构体
 * ============================================================ */
typedef struct GuiCallbackEntry {
    int lua_ref;
    GuiEventType event_type;
    int is_valid;
} GuiCallbackEntry;

typedef struct GuiCallbackManager {
    GuiCallbackEntry callbacks[MAX_CALLBACKS];
    int count;
} GuiCallbackManager;

static GuiCallbackManager g_callback_manager = {0};

/* ============================================================
 * 全局Lua状态（用于回调调用）
 * ============================================================ */
static lua_State *g_main_lua_state = NULL;

/* 前向声明：所有窗口方法和控件方法 */
static int gui_l_window_show(lua_State *L);
static int gui_l_window_hide(lua_State *L);
static int gui_l_window_close(lua_State *L);
static int gui_l_window_set_title(lua_State *L);
static int gui_l_window_get_title(lua_State *L);
static int gui_l_window_set_rect(lua_State *L);
static int gui_l_window_get_rect(lua_State *L);
static int gui_l_window_set_opacity(lua_State *L);
static int gui_l_window_center(lua_State *L);
static int gui_l_window_enable(lua_State *L);
static int gui_l_window_set_modal(lua_State *L);
static int gui_l_window_flash(lua_State *L);
static int gui_l_window_set_bgcolor(lua_State *L);
static int gui_l_window_on_event(lua_State *L);
static int gui_l_create_button(lua_State *L);
static int gui_l_create_label(lua_State *L);
static int gui_l_create_edit(lua_State *L);
static int gui_l_create_textbox(lua_State *L);
static int gui_l_create_checkbox(lua_State *L);
static int gui_l_create_radio(lua_State *L);
static int gui_l_create_groupbox(lua_State *L);
static int gui_l_create_listbox(lua_State *L);
static int gui_l_create_combobox(lua_State *L);
static int gui_l_create_progressbar(lua_State *L);
static int gui_l_create_slider(lua_State *L);
static int gui_l_create_tab(lua_State *L);
static int gui_l_create_treeview(lua_State *L);
static int gui_l_create_listview(lua_State *L);
static int gui_l_ctrl_set_text(lua_State *L);
static int gui_l_ctrl_get_text(lua_State *L);
static int gui_l_ctrl_set_rect(lua_State *L);
static int gui_l_ctrl_get_rect(lua_State *L);
static int gui_l_ctrl_set_visible(lua_State *L);
static int gui_l_ctrl_set_enabled(lua_State *L);
static int gui_l_ctrl_set_focus(lua_State *L);
static int gui_l_ctrl_to_front(lua_State *L);

/* 控件扩展方法前向声明 */
static int gui_l_listbox_add_item(lua_State *L);
static int gui_l_listbox_insert_item(lua_State *L);
static int gui_l_listbox_remove_item(lua_State *L);
static int gui_l_listbox_clear(lua_State *L);
static int gui_l_listbox_get_selected_index(lua_State *L);
static int gui_l_listbox_set_selected_index(lua_State *L);
static int gui_l_listbox_get_count(lua_State *L);
static int gui_l_listbox_get_item_text(lua_State *L);
static int gui_l_combobox_add_item(lua_State *L);
static int gui_l_combobox_insert_item(lua_State *L);
static int gui_l_combobox_remove_item(lua_State *L);
static int gui_l_combobox_clear(lua_State *L);
static int gui_l_combobox_get_selected_index(lua_State *L);
static int gui_l_combobox_set_selected_index(lua_State *L);
static int gui_l_combobox_get_count(lua_State *L);
static int gui_l_combobox_get_item_text(lua_State *L);
static int gui_l_checkbox_get_checked(lua_State *L);
static int gui_l_checkbox_set_checked(lua_State *L);
static int gui_l_radio_get_checked(lua_State *L);
static int gui_l_radio_set_checked(lua_State *L);
static int gui_l_progressbar_set_value(lua_State *L);
static int gui_l_progressbar_get_value(lua_State *L);
static int gui_l_progressbar_set_range(lua_State *L);
static int gui_l_slider_set_value(lua_State *L);
static int gui_l_slider_get_value(lua_State *L);
static int gui_l_slider_set_range(lua_State *L);
static int gui_l_slider_set_tick_freq(lua_State *L);
static int gui_l_textbox_append_text(lua_State *L);
static int gui_l_textbox_clear(lua_State *L);
static int gui_l_textbox_get_line_count(lua_State *L);

/* 内部辅助函数前向声明 */
static void gui_push_window(lua_State *L, GuiWindow *window);
static void gui_push_control(lua_State *L, GuiControl *control);
static GuiWindow* gui_check_window(lua_State *L, int idx);
static GuiControl* gui_check_control(lua_State *L, int idx);
static int gui_register_lua_callback(lua_State *L, int callback_idx);
static void gui_call_callback(int callback_id, void *sender, GuiEventData *event_data);

/* 事件桥接函数 */
static void gui_window_event_bridge(void *sender, GuiEventData *event_data, void *user_data);
static void gui_control_event_bridge(void *sender, GuiEventData *event_data, void *user_data);

/* GUI库顶层函数（用于luaL_Reg） */
static int gui_l_gui_init(lua_State *L);
static int gui_l_gui_run(lua_State *L);
static int gui_l_gui_cleanup(lua_State *L);
static int gui_l_gui_sleep(lua_State *L);
static int gui_l_gui_messagebox(lua_State *L);
static int gui_l_gui_create_window(lua_State *L);
static int gui_l_window_gc(lua_State *L);

/* ============================================================
 * 元表定义
 * ============================================================ */
static luaL_Reg window_methods[] = {
    {"show", gui_l_window_show},
    {"hide", gui_l_window_hide},
    {"close", gui_l_window_close},
    {"setTitle", gui_l_window_set_title},
    {"getTitle", gui_l_window_get_title},
    {"setRect", gui_l_window_set_rect},
    {"getRect", gui_l_window_get_rect},
    {"setOpacity", gui_l_window_set_opacity},
    {"center", gui_l_window_center},
    {"enable", gui_l_window_enable},
    {"setModal", gui_l_window_set_modal},
    {"flash", gui_l_window_flash},
    {"setBgColor", gui_l_window_set_bgcolor},
    {"onEvent", gui_l_window_on_event},
    {"createButton", gui_l_create_button},
    {"createLabel", gui_l_create_label},
    {"createEdit", gui_l_create_edit},
    {"createTextbox", gui_l_create_textbox},
    {"createCheckbox", gui_l_create_checkbox},
    {"createRadio", gui_l_create_radio},
    {"createGroupbox", gui_l_create_groupbox},
    {"createListbox", gui_l_create_listbox},
    {"createCombobox", gui_l_create_combobox},
    {"createProgressbar", gui_l_create_progressbar},
    {"createSlider", gui_l_create_slider},
    {"createTab", gui_l_create_tab},
    {"createTreeview", gui_l_create_treeview},
    {"createListview", gui_l_create_listview},
    {NULL, NULL}
};

static luaL_Reg control_methods[] = {
    {"setText", gui_l_ctrl_set_text},
    {"getText", gui_l_ctrl_get_text},
    {"setRect", gui_l_ctrl_set_rect},
    {"getRect", gui_l_ctrl_get_rect},
    {"setVisible", gui_l_ctrl_set_visible},
    {"setEnabled", gui_l_ctrl_set_enabled},
    {"setFocus", gui_l_ctrl_set_focus},
    {"toFront", gui_l_ctrl_to_front},

    /* 列表框/下拉框共用方法 */
    {"addItem", gui_l_listbox_add_item},
    {"insertItem", gui_l_listbox_insert_item},
    {"removeItem", gui_l_listbox_remove_item},
    {"clear", gui_l_listbox_clear},
    {"getSelectedIndex", gui_l_listbox_get_selected_index},
    {"setSelectedIndex", gui_l_listbox_set_selected_index},
    {"getCount", gui_l_listbox_get_count},
    {"getItemText", gui_l_listbox_get_item_text},

    /* 复选框/单选按钮方法 */
    {"getChecked", gui_l_checkbox_get_checked},
    {"setChecked", gui_l_checkbox_set_checked},

    /* 进度条/滑块方法 */
    {"setValue", gui_l_progressbar_set_value},
    {"getValue", gui_l_progressbar_get_value},
    {"setRange", gui_l_progressbar_set_range},
    {"setTickFreq", gui_l_slider_set_tick_freq},

    /* 多行文本框方法 */
    {"appendText", gui_l_textbox_append_text},
    {NULL, NULL}
};

/* ============================================================
 * 辅助函数实现
 * ============================================================ */
static void gui_push_window(lua_State *L, GuiWindow *window) {
    GuiWindow **ud = (GuiWindow**)lua_newuserdatauv(L, sizeof(GuiWindow*), 0);
    *ud = window;
    if (luaL_newmetatable(L, GUI_WINDOW_MT)) {
        lua_pushvalue(L, -1);
        lua_setfield(L, -2, "__index");
        luaL_setfuncs(L, window_methods, 0);
        lua_pushcfunction(L, gui_l_window_gc);
        lua_setfield(L, -2, "__gc");
    }
    lua_setmetatable(L, -2);
}

static void gui_push_control(lua_State *L, GuiControl *control) {
    GuiControl **ud = (GuiControl**)lua_newuserdatauv(L, sizeof(GuiControl*), 0);
    *ud = control;
    if (luaL_newmetatable(L, GUI_CONTROL_MT)) {
        lua_pushvalue(L, -1);
        lua_setfield(L, -2, "__index");
        luaL_setfuncs(L, control_methods, 0);
    }
    lua_setmetatable(L, -2);
}

static GuiWindow* gui_check_window(lua_State *L, int idx) {
    GuiWindow **win = (GuiWindow**)luaL_checkudata(L, idx, GUI_WINDOW_MT);
    if (!win || !*win) luaL_argerror(L, idx, "invalid window object");
    return *win;
}

static GuiControl* gui_check_control(lua_State *L, int idx) {
    GuiControl **ctrl = (GuiControl**)luaL_checkudata(L, idx, GUI_CONTROL_MT);
    if (!ctrl || !*ctrl) luaL_argerror(L, idx, "invalid control object");
    return *ctrl;
}

/* ============================================================
 * 回调管理
 * ============================================================ */
static int gui_register_lua_callback(lua_State *L, int callback_idx) {
    if (!g_main_lua_state) g_main_lua_state = L;
    if (g_callback_manager.count >= MAX_CALLBACKS) return -1;
    
    luaL_checktype(L, callback_idx, LUA_TFUNCTION);
    lua_pushvalue(L, callback_idx);
    int ref = luaL_ref(L, LUA_REGISTRYINDEX);
    
    int id = g_callback_manager.count++;
    g_callback_manager.callbacks[id].lua_ref = ref;
    g_callback_manager.callbacks[id].is_valid = 1;
    return id;
}

static void gui_call_callback(int callback_id, void *sender, GuiEventData *event_data) {
    if (!g_main_lua_state || callback_id < 0 || callback_id >= MAX_CALLBACKS) return;
    if (!g_callback_manager.callbacks[callback_id].is_valid) return;
    
    lua_rawgeti(g_main_lua_state, LUA_REGISTRYINDEX,
                 g_callback_manager.callbacks[callback_id].lua_ref);
    
    if (!lua_isfunction(g_main_lua_state, -1)) {
        lua_pop(g_main_lua_state, 1);
        return;
    }
    
    if (sender) {
        /* 根据事件类型判断sender是窗口还是控件 */
        if (event_data && (event_data->type == GUI_EVENT_CLOSE ||
            event_data->type == GUI_EVENT_SIZE || event_data->type == GUI_EVENT_MOVE ||
            event_data->type == GUI_EVENT_KEYDOWN || event_data->type == GUI_EVENT_KEYUP ||
            event_data->type == GUI_EVENT_CHAR || event_data->type == GUI_EVENT_CREATE ||
            event_data->type == GUI_EVENT_DESTROY)) {
            /* 窗口事件：sender是GuiWindow* */
            GuiWindow *win = (GuiWindow*)sender;
            gui_push_window(g_main_lua_state, win);
        } else {
            /* 控件事件：sender是GuiControl* */
            GuiControl *ctrl = (GuiControl*)sender;
            gui_push_control(g_main_lua_state, ctrl);
        }
    } else {
        lua_pushnil(g_main_lua_state);
    }
    
    lua_newtable(g_main_lua_state);
    if (event_data) {
        const char *type_name = "unknown";
        switch (event_data->type) {
            case GUI_EVENT_CLICK: type_name = "click"; break;
            case GUI_EVENT_CHANGE: type_name = "change"; break;
            case GUI_EVENT_CLOSE: type_name = "close"; break;
            case GUI_EVENT_SIZE: type_name = "size"; break;
            case GUI_EVENT_MOVE: type_name = "move"; break;
            case GUI_EVENT_KEYDOWN: type_name = "keydown"; break;
            case GUI_EVENT_KEYUP: type_name = "keyup"; break;
            case GUI_EVENT_CHAR: type_name = "char"; break;
            default: type_name = "unknown"; break;
        }
        lua_pushstring(g_main_lua_state, type_name);
        lua_setfield(g_main_lua_state, -2, "type");
        
        lua_pushinteger(g_main_lua_state, event_data->mouse.x);
        lua_setfield(g_main_lua_state, -2, "x");
        lua_pushinteger(g_main_lua_state, event_data->mouse.y);
        lua_setfield(g_main_lua_state, -2, "y");
        lua_pushinteger(g_main_lua_state, event_data->size.width);
        lua_setfield(g_main_lua_state, -2, "width");
        lua_pushinteger(g_main_lua_state, event_data->size.height);
        lua_setfield(g_main_lua_state, -2, "height");
        lua_pushinteger(g_main_lua_state, event_data->keyboard.key);
        lua_setfield(g_main_lua_state, -2, "key");
        lua_pushinteger(g_main_lua_state, event_data->keyboard.flags);
        lua_setfield(g_main_lua_state, -2, "keyFlags");
        lua_pushinteger(g_main_lua_state, event_data->character.ch);
        lua_setfield(g_main_lua_state, -2, "char");
        lua_pushinteger(g_main_lua_state, event_data->timer.id);
        lua_setfield(g_main_lua_state, -2, "timerId");
    }
    
    if (lua_pcall(g_main_lua_state, 2, 0, 0) != LUA_OK) {
        const char *err = lua_tostring(g_main_lua_state, -1);
        fprintf(stderr, "GUI回调错误: %s\n", err ? err : "未知错误");
        lua_pop(g_main_lua_state, 1);
    }
}

/* 事件桥接函数 */
static void gui_window_event_bridge(void *sender, GuiEventData *event_data, void *user_data) {
    int cb_id = (int)(intptr_t)user_data;
    gui_call_callback(cb_id, sender, event_data);
}

static void gui_control_event_bridge(void *sender, GuiEventData *event_data, void *user_data) {
    int cb_id = (int)(intptr_t)user_data;
    GuiControl *ctrl = (GuiControl*)sender;
    gui_call_callback(cb_id, ctrl, event_data);
}

/* ============================================================
 * 属性读取辅助
 * ============================================================ */
static void gui_read_color(lua_State *L, int idx, GuiColor *color) {
    color->r = 240; color->g = 240; color->b = 240; color->a = 255;
    if (lua_istable(L, idx)) {
        lua_getfield(L, idx, "r"); color->r = (unsigned char)(lua_isinteger(L,-1)?lua_tointeger(L,-1):240); lua_pop(L,1);
        lua_getfield(L, idx, "g"); color->g = (unsigned char)(lua_isinteger(L,-1)?lua_tointeger(L,-1):240); lua_pop(L,1);
        lua_getfield(L, idx, "b"); color->b = (unsigned char)(lua_isinteger(L,-1)?lua_tointeger(L,-1):240); lua_pop(L,1);
        lua_getfield(L, idx, "a"); color->a = (unsigned char)(lua_isinteger(L,-1)?lua_tointeger(L,-1):255); lua_pop(L,1);
    }
}

static void gui_read_control_props(lua_State *L, int idx, GuiControlProps *props) {
    memset(props, 0, sizeof(GuiControlProps));
    props->visible = 1;
    props->enabled = 1;
    
    if (!lua_istable(L, idx)) return;
    
    lua_getfield(L, idx, "x"); props->rect.x = lua_isinteger(L,-1)?(int)lua_tointeger(L,-1):0; lua_pop(L,1);
    lua_getfield(L, idx, "y"); props->rect.y = lua_isinteger(L,-1)?(int)lua_tointeger(L,-1):0; lua_pop(L,1);
    lua_getfield(L, idx, "width"); props->rect.width = lua_isinteger(L,-1)?(int)lua_tointeger(L,-1):80; lua_pop(L,1);
    lua_getfield(L, idx, "height"); props->rect.height = lua_isinteger(L,-1)?(int)lua_tointeger(L,-1):24; lua_pop(L,1);
    lua_getfield(L, idx, "text"); props->text = lua_tostring(L, -1); lua_pop(L,1);
    lua_getfield(L, idx, "visible"); if (lua_type(L,-1)==LUA_TBOOLEAN) props->visible = lua_toboolean(L,-1); lua_pop(L,1);
    lua_getfield(L, idx, "enabled"); if (lua_type(L,-1)==LUA_TBOOLEAN) props->enabled = lua_toboolean(L,-1); lua_pop(L,1);
    lua_getfield(L, idx, "fontSize"); props->font_size = lua_isinteger(L,-1)?(int)lua_tointeger(L,-1):0; lua_pop(L,1);
    lua_getfield(L, idx, "fontName"); props->font_name = lua_tostring(L, -1); lua_pop(L,1);
    
    lua_getfield(L, idx, "bgcolor");
    if (lua_istable(L, -1)) gui_read_color(L, -1, &props->bgcolor);
    else props->bgcolor = (GuiColor){240,240,240,255};
    lua_pop(L, 1);
    
    lua_getfield(L, idx, "fgcolor");
    if (lua_istable(L, -1)) gui_read_color(L, -1, &props->fgcolor);
    else props->fgcolor = (GuiColor){0,0,0,255};
    lua_pop(L, 1);
}

/* 控件创建通用函数 */
static int gui_do_create_control(lua_State *L, GuiControlType type) {
    GuiWindow *win = gui_check_window(L, 1);
    luaL_checktype(L, 2, LUA_TTABLE);
    
    GuiControlProps props;
    gui_read_control_props(L, 2, &props);
    
#ifdef GUI_PLATFORM_WINDOWS
    GuiControl *control = gui_create_control(win, type, &props);
#else
    GuiControl *control = gui_create_control(win, type, &props);
#endif
    
    if (!control) {
        lua_pushnil(L);
        lua_pushstring(L, gui_get_last_error());
        return 2;
    }
    
    lua_getfield(L, 2, "onClick");
    if (lua_isfunction(L, -1)) {
        int cb_id = gui_register_lua_callback(L, -1);
        if (cb_id >= 0) {
            control->on_click = gui_control_event_bridge;
            control->user_data = (void*)(intptr_t)cb_id;
        }
    }
    lua_pop(L, 1);
    
    lua_getfield(L, 2, "onChange");
    if (lua_isfunction(L, -1)) {
        int cb_id = gui_register_lua_callback(L, -1);
        if (cb_id >= 0) {
            control->on_change = gui_control_event_bridge;
            control->user_data = (void*)(intptr_t)cb_id;
        }
    }
    lua_pop(L, 1);
    
    gui_push_control(L, control);
    return 1;
}

/* ============================================================
 * 库入口点：luaopen_gui
 * ============================================================ */
LUAMOD_API int luaopen_gui(lua_State *L) {
    g_main_lua_state = L;
    memset(&g_callback_manager, 0, sizeof(g_callback_manager));
    
#if defined(GUI_PLATFORM_WINDOWS)
    gui_init(GetModuleHandle(NULL));
#elif defined(GUI_PLATFORM_LINUX)
    gui_init(NULL, NULL);
#endif
    
    luaL_Reg guilib[] = {
        {"init", gui_l_gui_init},
        {"run", gui_l_gui_run},
        {"cleanup", gui_l_gui_cleanup},
        {"sleep", gui_l_gui_sleep},
        {"messageBox", gui_l_gui_messagebox},
        {"createWindow", gui_l_gui_create_window},
        {NULL, NULL}
    };
    
    luaL_newlib(L, guilib);
    return 1;
}

/* ============================================================
 * 窗口方法实现
 * ============================================================ */
static int gui_l_window_show(lua_State *L) {
    GuiWindow *win = gui_check_window(L, 1);
#ifdef GUI_PLATFORM_WINDOWS
    gui_show_window(win, SW_SHOW);
#else
    gui_show_window(win);
#endif
    return 0;
}
static int gui_l_window_hide(lua_State *L) {
    GuiWindow *win = gui_check_window(L, 1);
    gui_hide_window(win);
    return 0;
}
static int gui_l_window_close(lua_State *L) {
    GuiWindow *win = gui_check_window(L, 1);
    gui_destroy_window(win);
    return 0;
}
static int gui_l_window_set_title(lua_State *L) {
    GuiWindow *win = gui_check_window(L, 1);
    const char *title = luaL_checkstring(L, 2);
    gui_set_window_title(win, title);
    return 0;
}
static int gui_l_window_get_title(lua_State *L) {
    GuiWindow *win = gui_check_window(L, 1);
    lua_pushstring(L, gui_get_window_title(win));
    return 1;
}
static int gui_l_window_set_rect(lua_State *L) {
    GuiWindow *win = gui_check_window(L, 1);
    luaL_checktype(L, 2, LUA_TTABLE);
    GuiRect rect;
    lua_getfield(L, 2, "x"); rect.x = lua_isinteger(L,-1)?(int)lua_tointeger(L,-1):0; lua_pop(L,1);
    lua_getfield(L, 2, "y"); rect.y = lua_isinteger(L,-1)?(int)lua_tointeger(L,-1):0; lua_pop(L,1);
    lua_getfield(L, 2, "width"); rect.width = lua_isinteger(L,-1)?(int)lua_tointeger(L,-1):0; lua_pop(L,1);
    lua_getfield(L, 2, "height"); rect.height = lua_isinteger(L,-1)?(int)lua_tointeger(L,-1):0; lua_pop(L,1);
    gui_set_window_rect(win, &rect);
    return 0;
}
static int gui_l_window_get_rect(lua_State *L) {
    GuiWindow *win = gui_check_window(L, 1);
    GuiRect r;
#ifdef GUI_PLATFORM_WINDOWS
    gui_get_window_rect(win, &r);
#else
    r = gui_get_window_rect(win);
#endif
    lua_createtable(L, 0, 4);
    lua_pushinteger(L, r.x); lua_setfield(L, -2, "x");
    lua_pushinteger(L, r.y); lua_setfield(L, -2, "y");
    lua_pushinteger(L, r.width); lua_setfield(L, -2, "width");
    lua_pushinteger(L, r.height); lua_setfield(L, -2, "height");
    return 1;
}
static int gui_l_window_set_opacity(lua_State *L) {
    GuiWindow *win = gui_check_window(L, 1);
    double opacity = luaL_optnumber(L, 2, 1.0);
    gui_set_window_opacity(win, opacity);
    return 0;
}
static int gui_l_window_center(lua_State *L) {
    GuiWindow *win = gui_check_window(L, 1);
#ifdef GUI_PLATFORM_WINDOWS
    gui_center_window(win, 0);
#else
    gui_center_window(win);
#endif
    return 0;
}
static int gui_l_window_enable(lua_State *L) {
    GuiWindow *win = gui_check_window(L, 1);
    int enabled = lua_toboolean(L, 2);
    gui_enable_window(win, enabled);
    return 0;
}
static int gui_l_window_set_modal(lua_State *L) {
    return 0;
}
static int gui_l_window_flash(lua_State *L) {
    GuiWindow *win = gui_check_window(L, 1);
    unsigned int count = luaL_optinteger(L, 2, 3);
    gui_flash_window(win, count);
    return 0;
}
static int gui_l_window_set_bgcolor(lua_State *L) {
    GuiWindow *win = gui_check_window(L, 1);
    luaL_checktype(L, 2, LUA_TTABLE);
    GuiColor color;
    gui_read_color(L, 2, &color);
#ifdef GUI_PLATFORM_WINDOWS
    HBRUSH hBrush = CreateSolidBrush(RGB(color.r, color.g, color.b));
    if (hBrush) {
        SetClassLongPtr(win->hwnd, GCLP_HBRBACKGROUND, (LONG_PTR)hBrush);
        InvalidateRect(win->hwnd, NULL, TRUE);
    }
    lua_pushboolean(L, hBrush != NULL);
#else
    int result = gui_set_window_bgcolor(win, color);
    lua_pushboolean(L, result == 0);
#endif
    return 1;
}

/** 窗口事件注册: win:onEvent(eventType, callback) */
static int gui_l_window_on_event(lua_State *L) {
    GuiWindow *win = gui_check_window(L, 1);
    const char *event_name = luaL_checkstring(L, 2);
    
    if (!lua_isfunction(L, 3)) {
        luaL_argerror(L, 3, "expected function");
        return 0;
    }
    
    GuiEventType event_type = GUI_EVENT_NONE;
    if (strcmp(event_name, "close") == 0) event_type = GUI_EVENT_CLOSE;
    else if (strcmp(event_name, "size") == 0) event_type = GUI_EVENT_SIZE;
    else if (strcmp(event_name, "move") == 0) event_type = GUI_EVENT_MOVE;
    else if (strcmp(event_name, "paint") == 0) event_type = GUI_EVENT_PAINT;
    else if (strcmp(event_name, "keydown") == 0) event_type = GUI_EVENT_KEYDOWN;
    else if (strcmp(event_name, "keyup") == 0) event_type = GUI_EVENT_KEYUP;
    else if (strcmp(event_name, "char") == 0) event_type = GUI_EVENT_CHAR;
    else if (strcmp(event_name, "create") == 0) event_type = GUI_EVENT_CREATE;
    else if (strcmp(event_name, "destroy") == 0) event_type = GUI_EVENT_DESTROY;
    else if (strcmp(event_name, "dropfiles") == 0) event_type = GUI_EVENT_DROPFILES;
    else if (strcmp(event_name, "timer") == 0) event_type = GUI_EVENT_TIMER;
    else {
        luaL_error(L, "未知的事件类型: %s", event_name);
        return 0;
    }
    
    int cb_id = gui_register_lua_callback(L, 3);
    if (cb_id < 0) { lua_pushboolean(L, 0); return 1; }
    
    int result = gui_register_event_callback(win, event_type, gui_window_event_bridge);
    if (result == GUI_OK) {
        gui_set_event_callback_id(win, event_type, cb_id);
    }
    
    lua_pushboolean(L, 1);
    return 1;
}

/* ============================================================
 * 控件创建函数
 * ============================================================ */
static int gui_l_create_button(lua_State *L) { return gui_do_create_control(L, GUI_CTRL_BUTTON); }
static int gui_l_create_label(lua_State *L) { return gui_do_create_control(L, GUI_CTRL_LABEL); }
static int gui_l_create_edit(lua_State *L) { return gui_do_create_control(L, GUI_CTRL_EDIT); }
static int gui_l_create_textbox(lua_State *L) { return gui_do_create_control(L, GUI_CTRL_TEXTBOX); }
static int gui_l_create_checkbox(lua_State *L) { return gui_do_create_control(L, GUI_CTRL_CHECKBOX); }
static int gui_l_create_radio(lua_State *L) { return gui_do_create_control(L, GUI_CTRL_RADIOBUTTON); }
static int gui_l_create_groupbox(lua_State *L) { return gui_do_create_control(L, GUI_CTRL_GROUPBOX); }
static int gui_l_create_listbox(lua_State *L) { return gui_do_create_control(L, GUI_CTRL_LISTBOX); }
static int gui_l_create_combobox(lua_State *L) { return gui_do_create_control(L, GUI_CTRL_COMBOBOX); }
static int gui_l_create_progressbar(lua_State *L) { return gui_do_create_control(L, GUI_CTRL_PROGRESSBAR); }
static int gui_l_create_slider(lua_State *L) { return gui_do_create_control(L, GUI_CTRL_SLIDER); }
static int gui_l_create_tab(lua_State *L) { return gui_do_create_control(L, GUI_CTRL_TABCONTROL); }
static int gui_l_create_treeview(lua_State *L) { return gui_do_create_control(L, GUI_CTRL_TREEVIEW); }
static int gui_l_create_listview(lua_State *L) { return gui_do_create_control(L, GUI_CTRL_LISTVIEW); }

/* ============================================================
 * 控件基础方法
 * ============================================================ */
static int gui_l_ctrl_set_text(lua_State *L) {
    GuiControl *ctrl = gui_check_control(L, 1);
    const char *text = luaL_checkstring(L, 2);
    gui_set_control_text(ctrl, text);
    return 0;
}
static int gui_l_ctrl_get_text(lua_State *L) {
    GuiControl *ctrl = gui_check_control(L, 1);
    lua_pushstring(L, gui_get_control_text(ctrl));
    return 1;
}
static int gui_l_ctrl_set_rect(lua_State *L) {
    GuiControl *ctrl = gui_check_control(L, 1);
    luaL_checktype(L, 2, LUA_TTABLE);
    GuiRect rect;
    lua_getfield(L, 2, "x"); rect.x = lua_isinteger(L,-1)?(int)lua_tointeger(L,-1):0; lua_pop(L,1);
    lua_getfield(L, 2, "y"); rect.y = lua_isinteger(L,-1)?(int)lua_tointeger(L,-1):0; lua_pop(L,1);
    lua_getfield(L, 2, "width"); rect.width = lua_isinteger(L,-1)?(int)lua_tointeger(L,-1):0; lua_pop(L,1);
    lua_getfield(L, 2, "height"); rect.height = lua_isinteger(L,-1)?(int)lua_tointeger(L,-1):0; lua_pop(L,1);
    gui_set_control_rect(ctrl, &rect);
    return 0;
}
static int gui_l_ctrl_get_rect(lua_State *L) {
    GuiControl *ctrl = gui_check_control(L, 1);
    GuiRect r;
#ifdef GUI_PLATFORM_WINDOWS
    gui_get_control_rect(ctrl, &r);
#else
    r = gui_get_control_rect(ctrl);
#endif
    lua_createtable(L, 0, 4);
    lua_pushinteger(L, r.x); lua_setfield(L, -2, "x");
    lua_pushinteger(L, r.y); lua_setfield(L, -2, "y");
    lua_pushinteger(L, r.width); lua_setfield(L, -2, "width");
    lua_pushinteger(L, r.height); lua_setfield(L, -2, "height");
    return 1;
}
static int gui_l_ctrl_set_visible(lua_State *L) {
    GuiControl *ctrl = gui_check_control(L, 1);
    gui_set_control_visible(ctrl, lua_toboolean(L, 2));
    return 0;
}
static int gui_l_ctrl_set_enabled(lua_State *L) {
    GuiControl *ctrl = gui_check_control(L, 1);
    gui_set_control_enabled(ctrl, lua_toboolean(L, 2));
    return 0;
}
static int gui_l_ctrl_set_focus(lua_State *L) {
    GuiControl *ctrl = gui_check_control(L, 1);
#ifdef GUI_PLATFORM_WINDOWS
    gui_set_focus(ctrl);
#else
    gui_set_control_focus(ctrl);
#endif
    return 0;
}
static int gui_l_ctrl_to_front(lua_State *L) {
    GuiControl *ctrl = gui_check_control(L, 1);
#ifdef GUI_PLATFORM_WINDOWS
    gui_bring_control_to_front(ctrl);
#else
    gui_control_to_front(ctrl);
#endif
    return 0;
}

/* ============================================================
 * 列表框/下拉框共用方法（根据控件类型自动分发）
 * ============================================================ */
static int gui_l_listbox_add_item(lua_State *L) {
    GuiControl *ctrl = gui_check_control(L, 1);
    const char *text = luaL_checkstring(L, 2);
    int result;
    if (ctrl->type == GUI_CTRL_COMBOBOX)
        result = gui_combobox_add_item(ctrl, text);
    else
        result = gui_listbox_add_item(ctrl, text);
    lua_pushinteger(L, result);
    return 1;
}
static int gui_l_listbox_insert_item(lua_State *L) {
    GuiControl *ctrl = gui_check_control(L, 1);
    int index = luaL_checkinteger(L, 2);
    const char *text = luaL_checkstring(L, 3);
    int result;
    if (ctrl->type == GUI_CTRL_COMBOBOX)
        result = gui_combobox_insert_item(ctrl, index, text);
    else
        result = gui_listbox_insert_item(ctrl, index, text);
    lua_pushinteger(L, result);
    return 1;
}
static int gui_l_listbox_remove_item(lua_State *L) {
    GuiControl *ctrl = gui_check_control(L, 1);
    int index = luaL_checkinteger(L, 2);
    int result;
    if (ctrl->type == GUI_CTRL_COMBOBOX)
        result = gui_combobox_remove_item(ctrl, index);
    else
        result = gui_listbox_remove_item(ctrl, index);
    lua_pushinteger(L, result);
    return 1;
}
static int gui_l_listbox_clear(lua_State *L) {
    GuiControl *ctrl = gui_check_control(L, 1);
    int result;
    if (ctrl->type == GUI_CTRL_COMBOBOX)
        result = gui_combobox_clear(ctrl);
    else
        result = gui_listbox_clear(ctrl);
    lua_pushinteger(L, result);
    return 1;
}
static int gui_l_listbox_get_selected_index(lua_State *L) {
    GuiControl *ctrl = gui_check_control(L, 1);
    if (ctrl->type == GUI_CTRL_COMBOBOX)
        lua_pushinteger(L, gui_combobox_get_selected_index(ctrl));
    else
        lua_pushinteger(L, gui_listbox_get_selected_index(ctrl));
    return 1;
}
static int gui_l_listbox_set_selected_index(lua_State *L) {
    GuiControl *ctrl = gui_check_control(L, 1);
    int index = luaL_checkinteger(L, 2);
    int result;
    if (ctrl->type == GUI_CTRL_COMBOBOX)
        result = gui_combobox_set_selected_index(ctrl, index);
    else
        result = gui_listbox_set_selected_index(ctrl, index);
    lua_pushinteger(L, result);
    return 1;
}
static int gui_l_listbox_get_count(lua_State *L) {
    GuiControl *ctrl = gui_check_control(L, 1);
    if (ctrl->type == GUI_CTRL_COMBOBOX)
        lua_pushinteger(L, gui_combobox_get_count(ctrl));
    else
        lua_pushinteger(L, gui_listbox_get_count(ctrl));
    return 1;
}
static int gui_l_listbox_get_item_text(lua_State *L) {
    GuiControl *ctrl = gui_check_control(L, 1);
    int index = luaL_checkinteger(L, 2);
    if (ctrl->type == GUI_CTRL_COMBOBOX)
        lua_pushstring(L, gui_combobox_get_item_text(ctrl, index));
    else
        lua_pushstring(L, gui_listbox_get_item_text(ctrl, index));
    return 1;
}

/* ============================================================
 * 复选框/单选按钮方法
 * ============================================================ */
static int gui_l_checkbox_get_checked(lua_State *L) {
    GuiControl *ctrl = gui_check_control(L, 1);
    if (ctrl->type == GUI_CTRL_RADIOBUTTON)
        lua_pushboolean(L, gui_radio_get_checked(ctrl));
    else
        lua_pushboolean(L, gui_checkbox_get_checked(ctrl));
    return 1;
}
static int gui_l_checkbox_set_checked(lua_State *L) {
    GuiControl *ctrl = gui_check_control(L, 1);
    int checked = lua_toboolean(L, 2);
    if (ctrl->type == GUI_CTRL_RADIOBUTTON)
        gui_radio_set_checked(ctrl, checked);
    else
        gui_checkbox_set_checked(ctrl, checked);
    return 0;
}

/* ============================================================
 * 进度条/滑块方法
 * ============================================================ */
static int gui_l_progressbar_set_value(lua_State *L) {
    GuiControl *ctrl = gui_check_control(L, 1);
    int value = luaL_checkinteger(L, 2);
    if (ctrl->type == GUI_CTRL_SLIDER)
        gui_slider_set_value(ctrl, value);
    else
        gui_progressbar_set_value(ctrl, value);
    return 0;
}
static int gui_l_progressbar_get_value(lua_State *L) {
    GuiControl *ctrl = gui_check_control(L, 1);
    if (ctrl->type == GUI_CTRL_SLIDER)
        lua_pushinteger(L, gui_slider_get_value(ctrl));
    else
        lua_pushinteger(L, gui_progressbar_get_value(ctrl));
    return 1;
}
static int gui_l_progressbar_set_range(lua_State *L) {
    GuiControl *ctrl = gui_check_control(L, 1);
    int min_val = luaL_checkinteger(L, 2);
    int max_val = luaL_checkinteger(L, 3);
    if (ctrl->type == GUI_CTRL_SLIDER)
        gui_slider_set_range(ctrl, min_val, max_val);
    else
        gui_progressbar_set_range(ctrl, min_val, max_val);
    return 0;
}
static int gui_l_slider_set_tick_freq(lua_State *L) {
    GuiControl *ctrl = gui_check_control(L, 1);
    int freq = luaL_checkinteger(L, 2);
    gui_slider_set_tick_freq(ctrl, freq);
    return 0;
}

/* ============================================================
 * 多行文本框方法
 * ============================================================ */
static int gui_l_textbox_append_text(lua_State *L) {
    GuiControl *ctrl = gui_check_control(L, 1);
    const char *text = luaL_checkstring(L, 2);
    gui_textbox_append_text(ctrl, text);
    return 0;
}
static int gui_l_textbox_clear(lua_State *L) {
    GuiControl *ctrl = gui_check_control(L, 1);
    gui_textbox_clear(ctrl);
    return 0;
}
static int gui_l_textbox_get_line_count(lua_State *L) {
    GuiControl *ctrl = gui_check_control(L, 1);
    lua_pushinteger(L, gui_textbox_get_line_count(ctrl));
    return 1;
}

/* ============================================================
 * GUI库顶层静态函数实现（替代Lambda）
 * ============================================================ */
static int gui_l_window_gc(lua_State *L) { return 0; }

static int gui_l_gui_init(lua_State *L) {
#if defined(GUI_PLATFORM_WINDOWS)
    gui_init(GetModuleHandle(NULL));
#elif defined(GUI_PLATFORM_LINUX)
    gui_init(NULL, NULL);
#endif
    lua_pushboolean(L, 1); return 1;
}

static int gui_l_gui_run(lua_State *L) {
    int code = gui_run_main_loop();
    lua_pushinteger(L, code); return 1;
}

static int gui_l_gui_cleanup(lua_State *L) {
    gui_cleanup();
    lua_pushboolean(L, 1); return 1;
}

static int gui_l_gui_sleep(lua_State *L) {
    int ms = luaL_optinteger(L, 1, 10);
#if defined(_WIN32)
    Sleep(ms);
    gui_process_messages();
#else
    usleep(ms * 1000);
    gui_process_events();
#endif
    return 0;
}

static int gui_l_gui_messagebox(lua_State *L) {
    const char *msg = luaL_checkstring(L, 1);
    const char *title = luaL_optstring(L, 2, "提示");
    const char *type = luaL_optstring(L, 3, "ok");
    int r;
#if defined(GUI_PLATFORM_WINDOWS)
    r = gui_message_box(NULL, msg, title, (unsigned int)(size_t)type);
#else
    r = gui_message_box(msg, title, type);
#endif
    lua_pushinteger(L, r); return 1;
}

static int gui_l_gui_create_window(lua_State *L) {
    luaL_checktype(L, 1, LUA_TTABLE);

    GuiWindowConfig config = {0};
    config.title = "";
    config.x = -1; config.y = -1;
    config.width = 800; config.height = 600;
    config.resizable = 1;
    config.bgcolor = (GuiColor){245,248,250,255};

    lua_getfield(L, 1, "title"); config.title = lua_tostring(L, -1); lua_pop(L, 1);
    lua_getfield(L, 1, "width"); if (lua_isinteger(L,-1)) config.width = (int)lua_tointeger(L, -1); lua_pop(L, 1);
    lua_getfield(L, 1, "height"); if (lua_isinteger(L,-1)) config.height = (int)lua_tointeger(L, -1); lua_pop(L, 1);
    lua_getfield(L, 1, "centerScreen"); config.center_screen = lua_toboolean(L, -1); lua_pop(L, 1);
    lua_getfield(L, 1, "resizable"); config.resizable = lua_toboolean(L, -1); lua_pop(L, 1);
    lua_getfield(L, 1, "topmost"); config.topmost = lua_toboolean(L, -1); lua_pop(L, 1);

    lua_getfield(L, 1, "bgcolor");
    if (lua_istable(L, -1)) gui_read_color(L, -1, &config.bgcolor);
    lua_pop(L, 1);

    GuiWindow *win;
#ifdef GUI_PLATFORM_WINDOWS
    win = gui_create_window(&config, NULL);
#else
    win = gui_create_window(&config);
#endif

    if (!win) { lua_pushnil(L); lua_pushstring(L, gui_get_last_error()); return 2; }
    gui_push_window(L, win);
    return 1;
}
