#define gui_windows_c
#define LUA_LIB

/**
 * @file gui_windows.c
 * @brief LXCLUA GUI Windows平台实现 - 核心功能实现
 * @author DiferLine
 * @version 1.0.0
 * 
 * 本文件实现了Windows平台GUI系统的核心功能，包括：
 * - 系统初始化与清理
 * - 窗口生命周期管理
 * - 控件创建与管理
 * - 消息循环与事件分发
 * - 对话框系统
 */

#include "gui_windows.h"
#include <shlobj.h>
#include <commdlg.h>
#include <richedit.h>
#include <string.h>
#include <malloc.h>

/* ============================================================
 * 全局状态变量
 * ============================================================ */
GuiSystemState_Win g_gui_state = {0};
static char g_last_error[512] = {0};
static int g_exit_code = 0;
static volatile LONG g_should_exit = 0;

/* ============================================================
 * 内部辅助函数声明
 * ============================================================ */
static LRESULT CALLBACK gui_window_proc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
static void gui_dispatch_event(GuiWindow_Win *window, GuiEventType type, GuiEventData *data);
static void gui_free_control_list(GuiControl_Win **head);
const char* gui_set_error(const char *fmt, ...);

/* ============================================================
 * 错误处理函数
 * ============================================================ */
const char* gui_set_error(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    vsnprintf(g_last_error, sizeof(g_last_error) - 1, fmt, args);
    va_end(args);
    return g_last_error;
}

const char* gui_get_last_error(void) {
    return g_last_error;
}

/* ============================================================
 * 内存管理函数
 * ============================================================ */
char* gui_alloc_string(const char *str) {
    if (!str) return NULL;
    size_t len = strlen(str) + 1;
    char *result = (char*)malloc(len);
    if (result) {
        memcpy(result, str, len);
    }
    return result;
}

void gui_free_string(char *str) {
    if (str) free(str);
}

/* ============================================================
 * 颜色转换函数
 * ============================================================ */
COLORREF gui_color_to_ref(GuiColor color) {
    return RGB(color.r, color.g, color.b);
}

GuiColor gui_ref_to_color(COLORREF ref) {
    GuiColor color;
    color.r = GetRValue(ref);
    color.g = GetGValue(ref);
    color.b = GetBValue(ref);
    color.a = 255;
    return color;
}

/* ============================================================
 * 系统初始化与清理
 * ============================================================ */
int gui_init(HINSTANCE hinstance) {
    if (g_gui_state.state != GUI_STATE_UNINITIALIZED) {
        return GUI_OK; /* 已经初始化过 */
    }

    memset(&g_gui_state, 0, sizeof(g_gui_state));
    
    /* 初始化临界区 */
    InitializeCriticalSection(&g_gui_state.cs);
    
    /* 保存实例句柄 */
    g_gui_state.hinstance = hinstance ? hinstance : GetModuleHandle(NULL);
    
    /* 初始化通用控件库（启用视觉样式） */
    INITCOMMONCONTROLSEX icc;
    icc.dwSize = sizeof(icc);
    icc.dwICC = ICC_WIN95_CLASSES | ICC_DATE_CLASSES | ICC_USEREX_CLASSES |
                 ICC_COOL_CLASSES | ICC_STANDARD_CLASSES | ICC_BAR_CLASSES;
    InitCommonControlsEx(&icc);
    
    /* 加载RichEdit库 */
    LoadLibrary(TEXT("riched20.dll"));
    
    /* 创建默认字体 */
    g_gui_state.default_font = CreateFont(
        -14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE,
        TEXT("Microsoft YaHei UI")
    );
    
    /* 创建标题字体 */
    g_gui_state.title_font = CreateFont(
        -16, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE,
        TEXT("Microsoft YaHei UI")
    );
    
    /* 创建等宽字体 */
    g_gui_state.mono_font = CreateFont(
        -14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, FIXED_PITCH | FF_DONTCARE,
        TEXT("Consolas")
    );
    
    /* 设置默认主题颜色 */
    g_gui_state.is_dark_mode = 0;
    g_gui_state.theme_bg = (GuiColor){255, 255, 255, 255};
    g_gui_state.theme_fg = (GuiColor){0, 0, 0, 255};
    g_gui_state.theme_accent = (GuiColor){0, 120, 215, 255};
    
    /* 设置系统状态为已初始化 */
    g_gui_state.state = GUI_STATE_INITIALIZED;
    g_gui_state.loop_mode = GUI_LOOP_BLOCKING;
    g_gui_state.is_running = 0;
    
    return GUI_OK;
}

int gui_cleanup(void) {
    EnterCriticalSection(&g_gui_state.cs);
    
    /* 销毁所有窗口 */
    GuiWindow_Win *win = g_gui_state.windows;
    while (win) {
        GuiWindow_Win *next = win->next;
        gui_destroy_window(win);
        win = next;
    }
    
    /* 释放字体资源 */
    if (g_gui_state.default_font) {
        DeleteObject(g_gui_state.default_font);
        g_gui_state.default_font = NULL;
    }
    if (g_gui_state.title_font) {
        DeleteObject(g_gui_state.title_font);
        g_gui_state.title_font = NULL;
    }
    if (g_gui_state.mono_font) {
        DeleteObject(g_gui_state.mono_font);
        g_gui_state.mono_font = NULL;
    }
    
    /* 重置状态 */
    g_gui_state.state = GUI_STATE_UNINITIALIZED;
    g_gui_state.window_count = 0;
    g_gui_state.windows = NULL;
    
    LeaveCriticalSection(&g_gui_state.cs);
    
    /* 删除临界区 */
    DeleteCriticalSection(&g_gui_state.cs);
    
    return GUI_OK;
}

GuiSystemState gui_get_state(void) {
    return g_gui_state.state;
}

int gui_is_initialized(void) {
    return g_gui_state.state != GUI_STATE_UNINITIALIZED;
}

GuiSystemState_Win* gui_get_system_state(void) {
    return &g_gui_state;
}

/* ============================================================
 * 窗口过程函数
 * ============================================================ */
static LRESULT CALLBACK gui_window_proc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    GuiWindow_Win *window = (GuiWindow_Win*)GetWindowLongPtr(hwnd, GWLP_USERDATA);
    
    if (!window) {
        return DefWindowProc(hwnd, msg, wParam, lParam);
    }
    
    GuiEventData event_data;
    memset(&event_data, 0, sizeof(event_data));
    
    switch (msg) {
        case WM_CREATE:
            event_data.type = GUI_EVENT_CREATE;
            gui_dispatch_event(window, GUI_EVENT_CREATE, &event_data);
            break;
            
        case WM_CLOSE:
            event_data.type = GUI_EVENT_CLOSE;
            gui_dispatch_event(window, GUI_EVENT_CLOSE, &event_data);
            
            if (!window->is_closing) {
                DestroyWindow(hwnd);
            }
            return 0;
            
        case WM_DESTROY:
            event_data.type = GUI_EVENT_DESTROY;
            gui_dispatch_event(window, GUI_EVENT_DESTROY, &event_data);
            
            /* 从全局列表中移除此窗口 */
            EnterCriticalSection(&g_gui_state.cs);
            if (g_gui_state.windows == window) {
                g_gui_state.windows = window->next;
            } else {
                GuiWindow_Win *prev = g_gui_state.windows;
                while (prev && prev->next != window) prev = prev->next;
                if (prev) prev->next = window->next;
            }
            g_gui_state.window_count--;
            
            /* 如果是最后一个窗口且处于运行状态，退出消息循环 */
            if (g_gui_state.window_count <= 0 && g_gui_state.is_running) {
                PostQuitMessage(0);
            }
            LeaveCriticalSection(&g_gui_state.cs);
            
            /* 释放控件列表 */
            gui_free_control_list(&window->controls);
            window->control_count = 0;
            
            /* 释放窗口配置中的字符串 */
            if (window->config.title) {
                gui_free_string((char*)window->config.title);
                window->config.title = NULL;
            }
            
            free(window);
            SetWindowLongPtr(hwnd, GWLP_USERDATA, 0);
            break;
            
        case WM_SIZE: {
            event_data.type = GUI_EVENT_SIZE;
            event_data.size.width = LOWORD(lParam);
            event_data.size.height = HIWORD(lParam);
            gui_dispatch_event(window, GUI_EVENT_SIZE, &event_data);
            
            /* 处理锚点布局 */
            GuiControl_Win *ctrl = window->controls;
            while (ctrl) {
                if (ctrl->props.anchor != GUI_ANCHOR_NONE) {
                    GuiRect parent_rect;
                    gui_get_window_rect(window, &parent_rect);
                    
                    /* 这里可以实现更复杂的响应式布局逻辑 */
                    /* 目前仅支持基本的锚点重定位 */
                }
                ctrl = ctrl->next;
            }
            break;
        }
        
        case WM_MOVE:
            event_data.type = GUI_EVENT_MOVE;
            event_data.position.x = LOWORD(lParam);
            event_data.position.y = HIWORD(lParam);
            gui_dispatch_event(window, GUI_EVENT_MOVE, &event_data);
            break;
            
        case WM_PAINT:
            event_data.type = GUI_EVENT_PAINT;
            gui_dispatch_event(window, GUI_EVENT_PAINT, &event_data);
            break; /* 让DefWindowProc完成绘制 */
            
        case WM_KEYDOWN:
            event_data.type = GUI_EVENT_KEYDOWN;
            event_data.keyboard.key = (unsigned int)wParam;
            event_data.keyboard.flags = (unsigned int)lParam;
            event_data.keyboard.repeat_count = (lParam & 0xFFFF);
            gui_dispatch_event(window, GUI_EVENT_KEYDOWN, &event_data);
            break;
            
        case WM_KEYUP:
            event_data.type = GUI_EVENT_KEYUP;
            event_data.keyboard.key = (unsigned int)wParam;
            event_data.keyboard.flags = (unsigned int)lParam;
            gui_dispatch_event(window, GUI_EVENT_KEYUP, &event_data);
            break;
            
        case WM_CHAR:
            event_data.type = GUI_EVENT_CHAR;
            event_data.character.ch = (unsigned int)wParam;
            gui_dispatch_event(window, GUI_EVENT_CHAR, &event_data);
            break;
            
        case WM_TIMER:
            event_data.type = GUI_EVENT_TIMER;
            event_data.timer.id = (unsigned int)wParam;
            gui_dispatch_event(window, GUI_EVENT_TIMER, &event_data);
            return 0;
            
        case WM_DROPFILES: {
            HDROP drop = (HDROP)wParam;
            event_data.type = GUI_EVENT_DROPFILES;
            event_data.custom_data = drop;
            gui_dispatch_event(window, GUI_EVENT_DROPFILES, &event_data);
            DragFinish(drop);
            return 0;
        }
        
        case WM_COMMAND: {
            HWND ctrl_hwnd = (HWND)lParam;
            unsigned int ctrl_id = LOWORD(wParam);
            unsigned int code = HIWORD(wParam);
            
            if (ctrl_hwnd) {
                GuiControl_Win *control = gui_find_control_by_hwnd(window, ctrl_hwnd);
                if (control) {
                    /* 按钮通知 */
                    if (control->type == GUI_CTRL_BUTTON ||
                        control->type == GUI_CTRL_CHECKBOX ||
                        control->type == GUI_CTRL_RADIOBUTTON ||
                        control->type == GUI_CTRL_GROUPBOX) {
                        if (code == BN_CLICKED || code == BN_DBLCLK) {
                            event_data.type = (code == BN_DBLCLK) ? GUI_EVENT_DBLCLICK : GUI_EVENT_CLICK;
                            event_data.mouse.x = (short)LOWORD(GetMessagePos());
                            event_data.mouse.y = (short)HIWORD(GetMessagePos());
                            if (control->on_click) {
                                control->on_click(control, &event_data, control->user_data);
                            }
                        }
                    }
                    /* 编辑框通知 */
                    else if (control->type == GUI_CTRL_EDIT || control->type == GUI_CTRL_TEXTBOX) {
                        if (code == EN_CHANGE) {
                            event_data.type = GUI_EVENT_CHANGE;
                            if (control->on_change) {
                                control->on_change(control, &event_data, control->user_data);
                            }
                        } else if (code == EN_SETFOCUS) {
                            event_data.type = GUI_EVENT_FOCUS;
                            if (control->on_focus) {
                                control->on_focus(control, &event_data, control->user_data);
                            }
                        }
                    }
                    /* 列表框通知 */
                    else if (control->type == GUI_CTRL_LISTBOX) {
                        if (code == LBN_SELCHANGE || code == LBN_DBLCLK) {
                            event_data.type = GUI_EVENT_SELECT;
                            event_data.mouse.x = (short)LOWORD(GetMessagePos());
                            event_data.mouse.y = (short)HIWORD(GetMessagePos());
                            if (control->on_change) {
                                control->on_change(control, &event_data, control->user_data);
                            }
                        }
                    }
                    /* 下拉框通知 */
                    else if (control->type == GUI_CTRL_COMBOBOX) {
                        if (code == CBN_SELCHANGE || code == CBN_CLOSEUP) {
                            event_data.type = GUI_EVENT_SELECT;
                            event_data.mouse.x = (short)LOWORD(GetMessagePos());
                            event_data.mouse.y = (short)HIWORD(GetMessagePos());
                            if (control->on_change) {
                                control->on_change(control, &event_data, control->user_data);
                            }
                        } else if (code == CBN_EDITCHANGE) {
                            event_data.type = GUI_EVENT_CHANGE;
                            if (control->on_change) {
                                control->on_change(control, &event_data, control->user_data);
                            }
                        }
                    }
                }
            }
            break;
        }
        
        /* 滑块/滚动条消息（用于TrackBar/Slider拖动） */
        case WM_HSCROLL:
        case WM_VSCROLL: {
            HWND ctrl_hwnd = (HWND)lParam;
            if (ctrl_hwnd && ctrl_hwnd != window->hwnd) {
                GuiControl_Win *control = gui_find_control_by_hwnd(window, ctrl_hwnd);
                if (control) {
                    unsigned int scroll_code = LOWORD(wParam);
                    
                    /* 只在拖动结束时或跟踪中触发 */
                    if (scroll_code == SB_THUMBPOSITION || 
                        scroll_code == SB_THUMBTRACK ||
                        scroll_code == SB_ENDSCROLL) {
                        event_data.type = GUI_EVENT_CHANGE;
                        if (control->on_change) {
                            control->on_change(control, &event_data, control->user_data);
                        }
                    }
                }
            }
            break;
        }
        
        case WM_NOTIFY: {
            NMHDR *nmhdr = (NMHDR*)lParam;
            GuiControl_Win *control = gui_find_control_by_hwnd(window, nmhdr->hwndFrom);
            if (control) {
                switch (nmhdr->code) {
                    case TTN_SHOW:
                    case TTN_POP:
                        /* 工具提示通知 */
                        break;
                        
                    default:
                        break;
                }
            }
            break;
        }
        
        default:
            break;
    }
    
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

/* ============================================================
 * 事件分发函数
 * ============================================================ */
static void gui_dispatch_event(GuiWindow_Win *window, GuiEventType type, GuiEventData *data) {
    data->type = type;
    
    /* 获取对应事件类型的Lua回调ID（每个事件独立存储，避免覆盖） */
    void *cb_data = NULL;
    
    switch (type) {
        case GUI_EVENT_CREATE:
            if (window->on_create) cb_data = (void*)(intptr_t)window->cb_id_create;
            if (window->on_create && cb_data) window->on_create(window, data, cb_data);
            break;
        case GUI_EVENT_DESTROY:
            if (window->on_destroy) cb_data = (void*)(intptr_t)window->cb_id_destroy;
            if (window->on_destroy && cb_data) window->on_destroy(window, data, cb_data);
            break;
        case GUI_EVENT_CLOSE:
            if (window->on_close) cb_data = (void*)(intptr_t)window->cb_id_close;
            if (window->on_close && cb_data) window->on_close(window, data, cb_data);
            break;
        case GUI_EVENT_SIZE:
            if (window->on_size) cb_data = (void*)(intptr_t)window->cb_id_size;
            if (window->on_size && cb_data) window->on_size(window, data, cb_data);
            break;
        case GUI_EVENT_MOVE:
            if (window->on_move) cb_data = (void*)(intptr_t)window->cb_id_move;
            if (window->on_move && cb_data) window->on_move(window, data, cb_data);
            break;
        case GUI_EVENT_PAINT:
            if (window->on_paint) cb_data = (void*)(intptr_t)window->cb_id_paint;
            if (window->on_paint && cb_data) window->on_paint(window, data, cb_data);
            break;
        case GUI_EVENT_KEYDOWN:
            if (window->on_keydown) cb_data = (void*)(intptr_t)window->cb_id_keydown;
            if (window->on_keydown && cb_data) window->on_keydown(window, data, cb_data);
            break;
        case GUI_EVENT_KEYUP:
            if (window->on_keyup) cb_data = (void*)(intptr_t)window->cb_id_keyup;
            if (window->on_keyup && cb_data) window->on_keyup(window, data, cb_data);
            break;
        case GUI_EVENT_CHAR:
            if (window->on_char) cb_data = (void*)(intptr_t)window->cb_id_char;
            if (window->on_char && cb_data) window->on_char(window, data, cb_data);
            break;
        case GUI_EVENT_DROPFILES:
            if (window->on_dropfiles) cb_data = (void*)(intptr_t)window->cb_id_dropfiles;
            if (window->on_dropfiles && cb_data) window->on_dropfiles(window, data, cb_data);
            break;
        case GUI_EVENT_TIMER:
            if (window->on_timer) cb_data = (void*)(intptr_t)window->cb_id_timer;
            if (window->on_timer && cb_data) window->on_timer(window, data, cb_data);
            break;
        default:
            break;
    }
}

/* ============================================================
 * 控件链表管理
 * ============================================================ */
static void gui_free_control_list(GuiControl_Win **head) {
    GuiControl_Win *current = *head;
    while (current) {
        GuiControl_Win *next = current->next;
        
        /* 释放文本内存 */
        if (current->props.text) {
            gui_free_string((char*)current->props.text);
        }
        if (current->props.font_name) {
            gui_free_string((char*)current->props.font_name);
        }
        
        free(current);
        current = next;
    }
    *head = NULL;
}

/* ============================================================
 * 窗口管理函数实现
 * ============================================================ */
GuiWindow_Win* gui_create_window(const GuiWindowConfig *config, HWND parent) {
    if (!gui_is_initialized()) {
        gui_set_error("GUI系统未初始化");
        return NULL;
    }
    
    if (!config) {
        gui_set_error("窗口配置不能为NULL");
        return NULL;
    }
    
    /* 分配窗口结构体 */
    GuiWindow_Win *window = (GuiWindow_Win*)calloc(1, sizeof(GuiWindow_Win));
    if (!window) {
        gui_set_error("内存分配失败");
        return NULL;
    }
    
    /* 注册窗口类（如果尚未注册） */
    static int class_registered = 0;
    WNDCLASSEX wc = {0};
    wc.cbSize = sizeof(WNDCLASSEX);
    wc.style = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
    wc.lpfnWndProc = gui_window_proc;
    wc.hInstance = g_gui_state.hinstance;
    wc.hIcon = config->icon ? (HICON)config->icon : LoadIcon(NULL, IDI_APPLICATION);
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszClassName = TEXT("LXCLuaGuiWindow");
    
    if (!class_registered) {
        if (!RegisterClassEx(&wc)) {
            gui_set_error("窗口类注册失败，错误码：%lu", GetLastError());
            free(window);
            return NULL;
        }
        class_registered = 1;
    }
    
    /* 计算窗口位置和尺寸 */
    int x = config->x;
    int y = config->y;
    int width = config->width > 0 ? config->width : 800;
    int height = config->height > 0 ? config->height : 600;
    
    /* 如果要求居中显示 */
    if (config->center_screen || (x == CW_USEDEFAULT && y == CW_USEDEFAULT)) {
        int screen_width = GetSystemMetrics(SM_CXSCREEN);
        int screen_height = GetSystemMetrics(SM_CYSCREEN);
        x = (screen_width - width) / 2;
        y = (screen_height - height) / 2;
    }
    
    /* 创建窗口 */
    DWORD style = config->style ? config->style : GUI_WS_OVERLAPPED;
    DWORD ex_style = 0;
    
    if (config->topmost) {
        ex_style |= WS_EX_TOPMOST;
    }
    if (config->has_statusbar) {
        /* 预留状态栏空间 */
        height += 23;
    }
    
    wchar_t wtitle[GUI_MAX_TITLE_LENGTH];
    wchar_t wclass[64];
    gui_utf8_to_wide(config->title ? config->title : "LXCLUA Window", wtitle, GUI_MAX_TITLE_LENGTH);
    gui_utf8_to_wide("LXCLuaGuiWindow", wclass, 64);
    
    window->hwnd = CreateWindowExW(
        ex_style,
        wclass,
        wtitle,
        style,
        x, y, width, height,
        parent,
        config->menu ? (HMENU)config->menu : NULL,
        g_gui_state.hinstance,
        NULL
    );
    
    if (!window->hwnd) {
        gui_set_error("窗口创建失败，错误码：%lu", GetLastError());
        free(window);
        return NULL;
    }
    
    /* 将窗口指针保存到窗口数据中 */
    SetWindowLongPtr(window->hwnd, GWLP_USERDATA, (LONG_PTR)window);
    
    /* 复制配置 */
    window->config = *config;
    if (config->title) {
        window->config.title = gui_alloc_string(config->title);
    }
    
    /* 初始化控件ID计数器 */
    window->next_ctrl_id = 1000;
    
    /* 设置不透明度 */
    if (config->bgcolor.a < 255) {
        /* 使用分层窗口实现透明效果 */
        LONG_PTR style_ex = GetWindowLongPtr(window->hwnd, GWL_EXSTYLE);
        SetWindowLongPtr(window->hwnd, GWL_EXSTYLE, style_ex | WS_EX_LAYERED);
        SetLayeredWindowAttributes(window->hwnd, 0, config->bgcolor.a, LWA_ALPHA);
    }
    
    /* 启用拖放文件支持 */
    DragAcceptFiles(window->hwnd, TRUE);
    
    /* 添加到全局窗口列表 */
    EnterCriticalSection(&g_gui_state.cs);
    window->next = g_gui_state.windows;
    g_gui_state.windows = window;
    g_gui_state.window_count++;
    LeaveCriticalSection(&g_gui_state.cs);
    
    return window;
}

int gui_destroy_window(GuiWindow_Win *window) {
    if (!window || !window->hwnd) {
        return GUI_ERR_INVALID_PARAM;
    }
    
    window->is_closing = 1;
    DestroyWindow(window->hwnd);
    
    return GUI_OK;
}

int gui_show_window(GuiWindow_Win *window, int cmd_show) {
    if (!window || !window->hwnd) {
        return GUI_ERR_INVALID_PARAM;
    }
    
    ShowWindow(window->hwnd, cmd_show ? cmd_show : SW_SHOW);
    UpdateWindow(window->hwnd);
    
    return GUI_OK;
}

int gui_hide_window(GuiWindow_Win *window) {
    if (!window || !window->hwnd) {
        return GUI_ERR_INVALID_PARAM;
    }
    
    ShowWindow(window->hwnd, SW_HIDE);
    
    return GUI_OK;
}

int gui_bring_to_front(GuiWindow_Win *window) {
    if (!window || !window->hwnd) {
        return GUI_ERR_INVALID_PARAM;
    }
    
    SetForegroundWindow(window->hwnd);
    BringWindowToTop(window->hwnd);
    
    return GUI_OK;
}

int gui_set_window_title(GuiWindow_Win *window, const char *title) {
    if (!window || !window->hwnd || !title) {
        return GUI_ERR_INVALID_PARAM;
    }
    
#ifdef UNICODE
    wchar_t wtitle[GUI_MAX_TITLE_LENGTH];
    MultiByteToWideChar(CP_UTF8, 0, title, -1, wtitle, GUI_MAX_TITLE_LENGTH);
    SetWindowTextW(window->hwnd, wtitle);
#else
    SetWindowTextA(window->hwnd, title);
#endif
    
    /* 更新配置 */
    if (window->config.title) {
        gui_free_string((char*)window->config.title);
    }
    window->config.title = gui_alloc_string(title);
    
    return GUI_OK;
}

const char* gui_get_window_title(GuiWindow_Win *window) {
    if (!window || !window->hwnd) {
        return "";
    }
    
    static char title_buffer[GUI_MAX_TITLE_LENGTH];
#ifdef UNICODE
    wchar_t wtitle[GUI_MAX_TITLE_LENGTH];
    GetWindowTextW(window->hwnd, wtitle, GUI_MAX_TITLE_LENGTH);
    WideCharToMultiByte(CP_UTF8, 0, wtitle, -1, title_buffer, GUI_MAX_TITLE_LENGTH, NULL, NULL);
#else
    GetWindowTextA(window->hwnd, title_buffer, GUI_MAX_TITLE_LENGTH);
#endif
    
    return title_buffer;
}

int gui_set_window_rect(GuiWindow_Win *window, const GuiRect *rect) {
    if (!window || !window->hwnd || !rect) {
        return GUI_ERR_INVALID_PARAM;
    }
    
    MoveWindow(window->hwnd, rect->x, rect->y, rect->width, rect->height, TRUE);
    
    return GUI_OK;
}

int gui_get_window_rect(GuiWindow_Win *window, GuiRect *rect) {
    if (!window || !window->hwnd || !rect) {
        return GUI_ERR_INVALID_PARAM;
    }
    
    RECT win_rect;
    GetWindowRect(window->hwnd, &win_rect);
    
    rect->x = win_rect.left;
    rect->y = win_rect.top;
    rect->width = win_rect.right - win_rect.left;
    rect->height = win_rect.bottom - win_rect.top;
    
    return GUI_OK;
}

int gui_set_window_opacity(GuiWindow_Win *window, unsigned char opacity) {
    if (!window || !window->hwnd) {
        return GUI_ERR_INVALID_PARAM;
    }
    
    LONG_PTR style_ex = GetWindowLongPtr(window->hwnd, GWL_EXSTYLE);
    if (!(style_ex & WS_EX_LAYERED)) {
        SetWindowLongPtr(window->hwnd, GWL_EXSTYLE, style_ex | WS_EX_LAYERED);
    }
    
    SetLayeredWindowAttributes(window->hwnd, 0, opacity, LWA_ALPHA);
    
    return GUI_OK;
}

int gui_center_window(GuiWindow_Win *window, int relative_to_parent) {
    if (!window || !window->hwnd) {
        return GUI_ERR_INVALID_PARAM;
    }
    
    GuiRect rect;
    gui_get_window_rect(window, &rect);
    
    int target_width, target_height, target_x, target_y;
    
    if (relative_to_parent && GetParent(window->hwnd)) {
        RECT parent_rect;
        GetWindowRect(GetParent(window->hwnd), &parent_rect);
        target_width = parent_rect.right - parent_rect.left;
        target_height = parent_rect.bottom - parent_rect.top;
        target_x = parent_rect.left;
        target_y = parent_rect.top;
    } else {
        target_width = GetSystemMetrics(SM_CXSCREEN);
        target_height = GetSystemMetrics(SM_CYSCREEN);
        target_x = 0;
        target_y = 0;
    }
    
    int new_x = target_x + (target_width - rect.width) / 2;
    int new_y = target_y + (target_height - rect.height) / 2;
    
    MoveWindow(window->hwnd, new_x, new_y, rect.width, rect.height, TRUE);
    
    return GUI_OK;
}

int gui_enable_window(GuiWindow_Win *window, int enabled) {
    if (!window || !window->hwnd) {
        return GUI_ERR_INVALID_PARAM;
    }
    
    EnableWindow(window->hwnd, enabled ? TRUE : FALSE);
    
    return GUI_OK;
}

int gui_set_modal(GuiWindow_Win *window, int modal) {
    if (!window) {
        return GUI_ERR_INVALID_PARAM;
    }
    
    window->is_modal = modal;
    
    return GUI_OK;
}

int gui_flash_window(GuiWindow_Win *window, unsigned int count) {
    if (!window || !window->hwnd) {
        return GUI_ERR_INVALID_PARAM;
    }
    
    FLASHWINFO fi;
    fi.cbSize = sizeof(fi);
    fi.hwnd = window->hwnd;
    fi.dwFlags = FLASHW_ALL | FLASHW_TIMERNOFG;
    fi.uCount = count;
    fi.dwTimeout = 0;
    FlashWindowEx(&fi);
    
    return GUI_OK;
}

/* ============================================================
 * 消息循环函数实现
 * ============================================================ */
int gui_run_main_loop(void) {
    if (!gui_is_initialized()) {
        gui_set_error("GUI系统未初始化");
        return GUI_ERR_NOT_INITIALIZED;
    }
    
    g_gui_state.state = GUI_STATE_RUNNING;
    g_gui_state.is_running = 1;
    g_should_exit = 0;
    g_exit_code = 0;
    
    MSG msg;
    while (!g_should_exit) {
        /* PeekMessage用于非阻塞模式检测 */
        if (g_gui_state.loop_mode == GUI_LOOP_NONBLOCKING) {
            while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
                if (msg.message == WM_QUIT) {
                    g_exit_code = (int)msg.wParam;
                    g_gui_state.is_running = 0;
                    g_gui_state.state = GUI_STATE_INITIALIZED;
                    return g_exit_code;
                }
                
                TranslateMessage(&msg);
                DispatchMessage(&msg);
            }
            
            /* 在非阻塞模式下返回，让调用者可以执行其他任务 */
            return GUI_OK;
        } else {
            /* 阻塞模式：使用GetMessage */
            int result = GetMessage(&msg, NULL, 0, 0);
            
            if (result <= 0) {
                /* WM_QUIT或错误 */
                g_exit_code = (result == -1) ? 1 : (int)msg.wParam;
                break;
            }
            
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
    }
    
    g_gui_state.is_running = 0;
    g_gui_state.state = GUI_STATE_INITIALIZED;
    
    return g_exit_code;
}

int gui_process_messages(void) {
    if (!gui_is_initialized()) {
        return GUI_ERR_NOT_INITIALIZED;
    }
    
    MSG msg;
    int processed = 0;
    
    while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
        processed = 1;
        
        if (msg.message == WM_QUIT) {
            g_exit_code = (int)msg.wParam;
            g_gui_state.is_running = 0;
            return 0;
        }
        
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    
    return processed;
}

int gui_exit_loop(int exit_code) {
    g_should_exit = 1;
    g_exit_code = exit_code;
    
    if (g_gui_state.is_running) {
        PostQuitMessage(exit_code);
    }
    
    return GUI_OK;
}

int gui_set_loop_mode(GuiLoopMode mode) {
    g_gui_state.loop_mode = mode;
    return GUI_OK;
}

/* ============================================================
 * 事件回调注册
 * ============================================================ */
int gui_register_event_callback(GuiWindow_Win *window,
                                GuiEventType event_type,
                                GuiEventCallback callback) {
    if (!window) {
        return GUI_ERR_INVALID_PARAM;
    }

    switch (event_type) {
        case GUI_EVENT_CREATE:
            window->on_create = callback;
            break;
        case GUI_EVENT_DESTROY:
            window->on_destroy = callback;
            break;
        case GUI_EVENT_CLOSE:
            window->on_close = callback;
            break;
        case GUI_EVENT_SIZE:
            window->on_size = callback;
            break;
        case GUI_EVENT_MOVE:
            window->on_move = callback;
            break;
        case GUI_EVENT_PAINT:
            window->on_paint = callback;
            break;
        case GUI_EVENT_KEYDOWN:
            window->on_keydown = callback;
            break;
        case GUI_EVENT_KEYUP:
            window->on_keyup = callback;
            break;
        case GUI_EVENT_CHAR:
            window->on_char = callback;
            break;
        case GUI_EVENT_DROPFILES:
            window->on_dropfiles = callback;
            break;
        case GUI_EVENT_TIMER:
            window->on_timer = callback;
            break;
        default:
            return GUI_ERR_INVALID_PARAM;
    }

    return GUI_OK;
}

int gui_set_event_callback_id(GuiWindow_Win *window,
                              GuiEventType event_type,
                              int cb_id) {
    if (!window) {
        return GUI_ERR_INVALID_PARAM;
    }

    switch (event_type) {
        case GUI_EVENT_CREATE:
            window->cb_id_create = cb_id;
            break;
        case GUI_EVENT_DESTROY:
            window->cb_id_destroy = cb_id;
            break;
        case GUI_EVENT_CLOSE:
            window->cb_id_close = cb_id;
            break;
        case GUI_EVENT_SIZE:
            window->cb_id_size = cb_id;
            break;
        case GUI_EVENT_MOVE:
            window->cb_id_move = cb_id;
            break;
        case GUI_EVENT_PAINT:
            window->cb_id_paint = cb_id;
            break;
        case GUI_EVENT_KEYDOWN:
            window->cb_id_keydown = cb_id;
            break;
        case GUI_EVENT_KEYUP:
            window->cb_id_keyup = cb_id;
            break;
        case GUI_EVENT_CHAR:
            window->cb_id_char = cb_id;
            break;
        case GUI_EVENT_DROPFILES:
            window->cb_id_dropfiles = cb_id;
            break;
        case GUI_EVENT_TIMER:
            window->cb_id_timer = cb_id;
            break;
        default:
            return GUI_ERR_INVALID_PARAM;
    }

    return GUI_OK;
}

int gui_register_control_callback(GuiControl_Win *control,
                                  GuiEventType event_type,
                                  GuiEventCallback callback) {
    if (!control) {
        return GUI_ERR_INVALID_PARAM;
    }
    
    switch (event_type) {
        case GUI_EVENT_CLICK:
            control->on_click = callback;
            break;
        case GUI_EVENT_CHANGE:
            control->on_change = callback;
            break;
        case GUI_EVENT_FOCUS:
            control->on_focus = callback;
            break;
        default:
            return GUI_ERR_INVALID_PARAM;
    }
    
    return GUI_OK;
}
