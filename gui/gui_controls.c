/**
 * @file gui_controls.c
 * @brief LXCLUA GUI 控件系统与对话框（跨平台）
 * @author DiferLine
 * @version 1.0.0
 */

#include "gui_core.h"

#if defined(GUI_PLATFORM_WINDOWS)
    #include "gui_windows.h"
    #include <shlobj.h>
    #include <commdlg.h>
#elif defined(GUI_PLATFORM_LINUX)
    #include "gui_linux.h"
#endif

/* ============================================================
 * 平台特定实现：Windows使用原生API，Linux提供空桩或GTK实现
 * ============================================================ */
#if defined(GUI_PLATFORM_WINDOWS)

/* 引用 gui_windows.h 中已声明的全局变量和函数 */

/* ============================================================
 * UTF-8/宽字符 转换辅助函数
 * ============================================================ */
void gui_utf8_to_wide(const char *src, wchar_t *dst, int dst_count) {
    if (src && dst && dst_count > 0)
        MultiByteToWideChar(CP_UTF8, 0, src, -1, dst, dst_count);
    else if (dst && dst_count > 0)
        dst[0] = L'\0';
}

void gui_wide_to_utf8(const wchar_t *src, char *dst, int dst_count) {
    if (src && dst && dst_count > 0)
        WideCharToMultiByte(CP_UTF8, 0, src, -1, dst, dst_count, NULL, NULL);
    else if (dst && dst_count > 0)
        dst[0] = '\0';
}

/* ============================================================
 * 控件创建内部函数：获取Windows控件类名和样式（始终返回UTF-8）
 * ============================================================ */
static void gui_get_control_class_style(GuiControlType type,
                                        const char **class_name,
                                        DWORD *style,
                                        DWORD *ex_style) {
    *ex_style = WS_EX_CLIENTEDGE;

    switch (type) {
        case GUI_CTRL_BUTTON:
            *class_name = "BUTTON";
            *style = WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON;
            *ex_style = 0;
            break;

        case GUI_CTRL_LABEL:
            *class_name = "STATIC";
            *style = WS_CHILD | WS_VISIBLE | SS_LEFT;
            *ex_style = 0;
            break;

        case GUI_CTRL_EDIT:
            *class_name = "EDIT";
            *style = WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | ES_LEFT;
            break;

        case GUI_CTRL_TEXTBOX:
            *class_name = "EDIT";
            *style = WS_CHILD | WS_VISIBLE | ES_MULTILINE | ES_AUTOVSCROLL |
                     ES_READONLY | WS_VSCROLL;
            break;

        case GUI_CTRL_CHECKBOX:
            *class_name = "BUTTON";
            *style = WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX;
            *ex_style = 0;
            break;

        case GUI_CTRL_RADIOBUTTON:
            *class_name = "BUTTON";
            *style = WS_CHILD | WS_VISIBLE | BS_AUTORADIOBUTTON;
            *ex_style = 0;
            break;

        case GUI_CTRL_GROUPBOX:
            *class_name = "BUTTON";
            *style = WS_CHILD | WS_VISIBLE | BS_GROUPBOX;
            *ex_style = 0;
            break;

        case GUI_CTRL_LISTBOX:
            *class_name = "LISTBOX";
            *style = WS_CHILD | WS_VISIBLE | LBS_NOTIFY | LBS_NOINTEGRALHEIGHT |
                     WS_VSCROLL | WS_BORDER;
            *ex_style = WS_EX_CLIENTEDGE;
            break;

        case GUI_CTRL_COMBOBOX:
            *class_name = "COMBOBOX";
            *style = WS_CHILD | WS_VISIBLE | CBS_DROPDOWN | CBS_AUTOHSCROLL;
            *ex_style = WS_EX_CLIENTEDGE;
            break;

        case GUI_CTRL_PROGRESSBAR:
            /* msctls_progress32 */
            *class_name = "msctls_progress32";
            *style = WS_CHILD | WS_VISIBLE | PBS_SMOOTH;
            *ex_style = 0;
            break;

        case GUI_CTRL_SLIDER:
            /* msctls_trackbar32 */
            *class_name = "msctls_trackbar32";
            *style = WS_CHILD | WS_VISIBLE | TBS_AUTOTICKS | TBS_HORZ;
            *ex_style = 0;
            break;

        case GUI_CTRL_TABCONTROL:
            /* SysTabControl32 */
            *class_name = "SysTabControl32";
            *style = WS_CHILD | WS_VISIBLE | TCS_BUTTONS | TCS_MULTILINE;
            *ex_style = 0;
            break;

        case GUI_CTRL_TREEVIEW:
            /* SysTreeView32 */
            *class_name = "SysTreeView32";
            *style = WS_CHILD | WS_VISIBLE | TVS_HASLINES | TVS_LINESATROOT |
                     TVS_HASBUTTONS | TVS_SHOWSELALWAYS;
            *ex_style = 0;
            break;

        case GUI_CTRL_LISTVIEW:
            /* SysListView32 */
            *class_name = "SysListView32";
            *style = WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_SINGLESEL |
                     LVS_SHOWSELALWAYS;
            *ex_style = 0;
            break;

        case GUI_CTRL_RICHEDIT:
            /* RichEdit20W */
            *class_name = "RichEdit20W";
            *style = WS_CHILD | WS_VISIBLE | ES_MULTILINE | ES_AUTOVSCROLL |
                     ES_READONLY | WS_VSCROLL;
            *ex_style = WS_EX_CLIENTEDGE;
            break;

        case GUI_CTRL_STATUSBAR:
            /* msctls_statusbar32 */
            *class_name = "msctls_statusbar32";
            *style = WS_CHILD | WS_VISIBLE | SBARS_SIZEGRIP;
            *ex_style = 0;
            break;

        default:
            *class_name = "STATIC";
            *style = WS_CHILD | WS_VISIBLE;
            *ex_style = 0;
            break;
    }
}

/* ============================================================
 * 控件创建与管理函数实现
 * ============================================================ */
GuiControl_Win* gui_create_control(GuiWindow_Win *window,
                                   GuiControlType type,
                                   const GuiControlProps *props) {
    if (!window || !window->hwnd || !props) {
        gui_set_error("无效参数");
        return NULL;
    }

    GuiControl_Win *control = (GuiControl_Win*)calloc(1, sizeof(GuiControl_Win));
    if (!control) {
        gui_set_error("内存分配失败");
        return NULL;
    }

    const char *class_name;
    DWORD style, ex_style;
    gui_get_control_class_style(type, &class_name, &style, &ex_style);

    if (!props->visible) style &= ~WS_VISIBLE;
    if (!props->enabled) style |= WS_DISABLED;

    control->id = window->next_ctrl_id++;

    wchar_t wclass[64], wtext[4096];
    gui_utf8_to_wide(class_name, wclass, 64);
    gui_utf8_to_wide(props->text ? props->text : "", wtext, 4096);

    control->hwnd = CreateWindowExW(
        ex_style, wclass, wtext, style,
        props->rect.x, props->rect.y,
        props->rect.width, props->rect.height,
        window->hwnd,
        (HMENU)(UINT_PTR)control->id,
        g_gui_state.hinstance,
        NULL
    );

    if (!control->hwnd) {
        gui_set_error("控件创建失败，错误码：%lu", GetLastError());
        free(control);
        return NULL;
    }

    control->type = type;
    control->props = *props;

    if (props->text) control->props.text = gui_alloc_string(props->text);
    control->props.font_name = gui_alloc_string(props->font_name ? props->font_name : "Microsoft YaHei UI");

    HFONT font_to_use = g_gui_state.default_font;
    if (props->font) {
        font_to_use = (HFONT)props->font;
    } else if (props->font_size > 0 && props->font_name) {
        wchar_t wfontname[LF_FACESIZE];
        gui_utf8_to_wide(props->font_name, wfontname, LF_FACESIZE);
        font_to_use = CreateFont(
            -props->font_size, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE,
            wfontname
        );
        control->props.font = font_to_use;
    }

    SendMessage(control->hwnd, WM_SETFONT, (WPARAM)font_to_use, TRUE);

    switch (type) {
        case GUI_CTRL_EDIT:
            SendMessage(control->hwnd, EM_LIMITTEXT, GUI_MAX_TEXT_LENGTH, 0);
            break;
        case GUI_CTRL_PROGRESSBAR:
            SendMessage(control->hwnd, PBM_SETRANGE, 0, MAKELPARAM(0, 100));
            break;
        case GUI_CTRL_SLIDER:
            SendMessage(control->hwnd, TBM_SETRANGE, TRUE, MAKELPARAM(0, 100));
            SendMessage(control->hwnd, TBM_SETTICFREQ, 10, 0);
            break;
        case GUI_CTRL_LISTVIEW:
            ListView_SetExtendedListViewStyle(
                control->hwnd,
                LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | LVS_EX_HEADERDRAGDROP
            );
            break;
        default: break;
    }

    control->next = window->controls;
    window->controls = control;
    window->control_count++;

    return control;
}

GuiControl_Win* gui_find_control_by_id(GuiWindow_Win *window, unsigned int id) {
    if (!window) return NULL;
    GuiControl_Win *ctrl = window->controls;
    while (ctrl) { if (ctrl->id == id) return ctrl; ctrl = ctrl->next; }
    return NULL;
}

GuiControl_Win* gui_find_control_by_hwnd(GuiWindow_Win *window, HWND hwnd) {
    if (!window || !hwnd) return NULL;
    GuiControl_Win *ctrl = window->controls;
    while (ctrl) { if (ctrl->hwnd == hwnd) return ctrl; ctrl = ctrl->next; }
    return NULL;
}

int gui_destroy_control(GuiControl_Win *control) {
    if (!control || !control->hwnd) return GUI_ERR_INVALID_PARAM;

    GuiWindow_Win *parent_window = NULL;
    EnterCriticalSection(&g_gui_state.cs);
    GuiWindow_Win *win = g_gui_state.windows;
    while (win) {
        GuiControl_Win *prev = NULL, *curr = win->controls;
        while (curr) {
            if (curr == control) {
                parent_window = win;
                if (prev) prev->next = curr->next; else win->controls = curr->next;
                win->control_count--;
                break;
            }
            prev = curr; curr = curr->next;
        }
        if (parent_window) break;
        win = win->next;
    }
    LeaveCriticalSection(&g_gui_state.cs);

    DestroyWindow(control->hwnd);
    if (control->props.text) gui_free_string((char*)control->props.text);
    if (control->props.font_name) gui_free_string((char*)control->props.font_name);
    if (control->props.font &&
        control->props.font != g_gui_state.default_font &&
        control->props.font != g_gui_state.title_font &&
        control->props.font != g_gui_state.mono_font) {
        DeleteObject((HFONT)control->props.font);
    }
    free(control);
    return GUI_OK;
}

int gui_set_control_text(GuiControl_Win *control, const char *text) {
    if (!control || !control->hwnd || !text) return GUI_ERR_INVALID_PARAM;
    wchar_t wtext[4096];
    gui_utf8_to_wide(text, wtext, 4096);
    SetWindowTextW(control->hwnd, wtext);
    if (control->props.text) gui_free_string((char*)control->props.text);
    control->props.text = gui_alloc_string(text);
    return GUI_OK;
}

const char* gui_get_control_text(GuiControl_Win *control) {
    if (!control || !control->hwnd) return "";
    static char text_buffer[4096];
    wchar_t wtext[4096];
    GetWindowTextW(control->hwnd, wtext, 4096);
    gui_wide_to_utf8(wtext, text_buffer, 4096);
    return text_buffer;
}

int gui_set_control_rect(GuiControl_Win *control, const GuiRect *rect) {
    if (!control || !control->hwnd || !rect) return GUI_ERR_INVALID_PARAM;
    MoveWindow(control->hwnd, rect->x, rect->y, rect->width, rect->height, TRUE);
    control->props.rect = *rect;
    return GUI_OK;
}

int gui_get_control_rect(GuiControl_Win *control, GuiRect *rect) {
    if (!control || !control->hwnd || !rect) return GUI_ERR_INVALID_PARAM;
    GetWindowRect(control->hwnd, (RECT*)rect);
    HWND parent = GetParent(control->hwnd);
    if (parent) {
        ScreenToClient(parent, (POINT*)&rect->x);
        ScreenToClient(parent, ((POINT*)rect) + 1);
        rect->width -= rect->x; rect->height -= rect->y;
    }
    return GUI_OK;
}

int gui_set_control_visible(GuiControl_Win *control, int visible) {
    if (!control || !control->hwnd) return GUI_ERR_INVALID_PARAM;
    ShowWindow(control->hwnd, visible ? SW_SHOW : SW_HIDE);
    control->props.visible = visible;
    return GUI_OK;
}

int gui_set_control_enabled(GuiControl_Win *control, int enabled) {
    if (!control || !control->hwnd) return GUI_ERR_INVALID_PARAM;
    EnableWindow(control->hwnd, enabled ? TRUE : FALSE);
    control->props.enabled = enabled;
    return GUI_OK;
}

int gui_set_focus(GuiControl_Win *control) {
    if (!control || !control->hwnd) return GUI_ERR_INVALID_PARAM;
    SetFocus(control->hwnd);
    return GUI_OK;
}

int gui_bring_control_to_front(GuiControl_Win *control) {
    if (!control || !control->hwnd) return GUI_ERR_INVALID_PARAM;
    BringWindowToTop(control->hwnd);
    return GUI_OK;
}

/* ============================================================
 * 对话框函数实现（Unicode安全）
 * ============================================================ */
int gui_message_box(GuiWindow_Win *parent,
                    const char *message,
                    const char *title,
                    unsigned int type) {
    HWND hwnd_parent = parent ? parent->hwnd : NULL;
    wchar_t wmsg[4096], wtitle[256];
    gui_utf8_to_wide(message, wmsg, 4096);
    gui_utf8_to_wide(title, wtitle, 256);
    return MessageBoxW(hwnd_parent, wmsg, wtitle, type);
}

char* gui_file_dialog(GuiWindow_Win *parent,
                      const char *title,
                      const char *filter,
                      int save_mode,
                      int multi_select) {
    OPENFILENAMEW ofn;
    wchar_t filename[MAX_PATH * 100];
    wchar_t wfilter[512], wtitle[256];

    memset(&ofn, 0, sizeof(ofn));
    memset(filename, 0, sizeof(filename));

    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = parent ? parent->hwnd : NULL;
    ofn.lpstrFile = filename;
    ofn.nMaxFile = MAX_PATH * 100;

    if (filter) {
        gui_utf8_to_wide(filter, wfilter, 512);
    } else {
        wcscpy(wfilter, L"All Files\0*.*\0\0");
    }
    ofn.lpstrFilter = wfilter;
    ofn.nFilterIndex = 1;

    if (title) gui_utf8_to_wide(title, wtitle, 256); else wcscpy(wtitle, L"");
    ofn.lpstrTitle = wtitle;
    ofn.Flags = OFN_EXPLORER | OFN_HIDEREADONLY | OFN_OVERWRITEPROMPT;
    if (multi_select) ofn.Flags |= OFN_ALLOWMULTISELECT;

    BOOL result = save_mode ? GetSaveFileNameW(&ofn) : GetOpenFileNameW(&ofn);
    if (!result) return NULL;

    if (multi_select) {
        wchar_t *dir = filename;
        wchar_t *file = dir + wcslen(dir) + 1;
        if (*file == L'\0') {
            char buf[MAX_PATH * 100];
            gui_wide_to_utf8(dir, buf, MAX_PATH * 100);
            return gui_alloc_string(buf);
        }

        size_t total_len = 0;
        wchar_t *p = file;
        while (*p) {
            size_t flen = wcslen(p);
            total_len += wcslen(dir) + 1 + flen + 1;
            p += flen + 1;
        }

        char *result_str = (char*)malloc(total_len);
        if (!result_str) return NULL;

        char *dest = result_str;
        p = file; int first = 1;
        while (*p) {
            if (!first) *dest++ = ';';
            first = 0;
            char tmp_dir[MAX_PATH], tmp_file[MAX_PATH];
            gui_wide_to_utf8(dir, tmp_dir, MAX_PATH);
            gui_wide_to_utf8(p, tmp_file, MAX_PATH);
            size_t dlen = strlen(tmp_dir), flen = strlen(tmp_file);
            memcpy(dest, tmp_dir, dlen); dest += dlen; *dest++ = '\\';
            memcpy(dest, tmp_file, flen); dest += flen;
            p += wcslen(p) + 1;
        }
        *dest = '\0';
        return result_str;
    }

    char buf[MAX_PATH * 100];
    gui_wide_to_utf8(filename, buf, MAX_PATH * 100);
    return gui_alloc_string(buf);
}

char* gui_folder_dialog(GuiWindow_Win *parent, const char *title) {
    BROWSEINFOW bi;
    wchar_t path[MAX_PATH], wtitle[256];
    memset(&bi, 0, sizeof(bi));

    bi.hwndOwner = parent ? parent->hwnd : NULL;
    bi.pszDisplayName = path;
    if (title) gui_utf8_to_wide(title, wtitle, 256); else wcscpy(wtitle, L"");
    bi.lpszTitle = wtitle;
    bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;

    LPITEMIDLIST pidl = SHBrowseForFolderW(&bi);
    if (!pidl) return NULL;

    SHGetPathFromIDListW(pidl, path);

    IMalloc *malloc_iface = NULL;
    SHGetMalloc(&malloc_iface);
    if (malloc_iface) {
        malloc_iface->lpVtbl->Free(malloc_iface, pidl);
        malloc_iface->lpVtbl->Release(malloc_iface);
    }

    char buf[MAX_PATH];
    gui_wide_to_utf8(path, buf, MAX_PATH);
    return gui_alloc_string(buf);
}

int gui_color_dialog(GuiWindow_Win *parent,
                     GuiColor initial_color,
                     GuiColor *result_color) {
    CHOOSECOLOR cc;
    COLORREF custom_colors[16] = {0};
    memset(&cc, 0, sizeof(cc));
    cc.lStructSize = sizeof(cc);
    cc.hwndOwner = parent ? parent->hwnd : NULL;
    cc.rgbResult = RGB(initial_color.r, initial_color.g, initial_color.b);
    cc.lpCustColors = custom_colors;
    cc.Flags = CC_RGBINIT | CC_FULLOPEN;

    if (!ChooseColor(&cc)) return 0;
    if (result_color) {
        result_color->r = GetRValue(cc.rgbResult);
        result_color->g = GetGValue(cc.rgbResult);
        result_color->b = GetBValue(cc.rgbResult);
        result_color->a = 255;
    }
    return 1;
}

int gui_font_dialog(GuiWindow_Win *parent,
                    char *font_name,
                    int *font_size) {
    CHOOSEFONT cf;
    LOGFONTW lf;
    memset(&cf, 0, sizeof(cf));
    memset(&lf, 0, sizeof(lf));
    cf.lStructSize = sizeof(cf);
    cf.hwndOwner = parent ? parent->hwnd : NULL;
    cf.lpLogFont = &lf;
    cf.Flags = CF_INITTOLOGFONTSTRUCT | CF_EFFECTS | CF_SCREENFONTS;
    cf.iPointSize = font_size ? *font_size * 10 : 140;

    if (font_name && *font_name)
        gui_utf8_to_wide(font_name, lf.lfFaceName, LF_FACESIZE);

    if (!ChooseFontW(&cf)) return 0;

    if (font_name) {
        gui_wide_to_utf8(lf.lfFaceName, font_name, LF_FACESIZE);
        font_name[LF_FACESIZE - 1] = '\0';
    }
    if (font_size) *font_size = cf.iPointSize / 10;
    return 1;
}

/* ============================================================
 * 定时器函数实现
 * ============================================================ */
unsigned int gui_create_timer(GuiWindow_Win *window,
                              unsigned int interval,
                              GuiEventCallback callback) {
    if (!window || !window->hwnd || !interval) return 0;
    unsigned int timer_id = SetTimer(window->hwnd, window->next_ctrl_id++, interval, NULL);
    if (!timer_id) gui_set_error("定时器创建失败");
    return timer_id;
}

int gui_destroy_timer(GuiWindow_Win *window, unsigned int timer_id) {
    if (!window || !window->hwnd || !timer_id) return GUI_ERR_INVALID_PARAM;
    KillTimer(window->hwnd, timer_id);
    return GUI_OK;
}

/* ============================================================
 * 绘图与图形函数实现
 * ============================================================ */
int gui_invalidate(void *handle) {
    if (!handle) return GUI_ERR_INVALID_PARAM;
    HWND hwnd = NULL;
    GuiWindow_Win *win = (GuiWindow_Win*)handle;
    GuiControl_Win *ctrl = (GuiControl_Win*)handle;

    EnterCriticalSection(&g_gui_state.cs);
    GuiWindow_Win *w = g_gui_state.windows;
    while (w) {
        if (w == win) { hwnd = w->hwnd; break; }
        GuiControl_Win *c = w->controls;
        while (c) {
            if (c == ctrl) { hwnd = c->hwnd; break; }
            c = c->next;
        }
        if (hwnd) break;
        w = w->next;
    }
    LeaveCriticalSection(&g_gui_state.cs);

    if (!hwnd) return GUI_ERR_INVALID_PARAM;
    InvalidateRect(hwnd, NULL, TRUE);
    return GUI_OK;
}

int gui_set_bgcolor(void *handle, GuiColor color) {
    if (!handle) return GUI_ERR_INVALID_PARAM;
    HWND hwnd = NULL;
    GuiControl_Win *ctrl = NULL;
    GuiWindow_Win *win = (GuiWindow_Win*)handle;

    EnterCriticalSection(&g_gui_state.cs);
    GuiWindow_Win *w = g_gui_state.windows;
    while (w) {
        if (w == win) { hwnd = w->hwnd; break; }
        GuiControl_Win *c = w->controls;
        while (c) {
            if (c == ctrl || c == (GuiControl_Win*)handle) { hwnd = c->hwnd; ctrl = c; break; }
            c = c->next;
        }
        if (hwnd) break;
        w = w->next;
    }
    LeaveCriticalSection(&g_gui_state.cs);

    if (!hwnd) return GUI_ERR_INVALID_PARAM;
    HBRUSH brush = CreateSolidBrush(gui_color_to_ref(color));
    SetClassLongPtr(hwnd, GCLP_HBRBACKGROUND, (LONG_PTR)brush);
    if (ctrl) ctrl->props.bgcolor = color; else win->config.bgcolor = color;
    InvalidateRect(hwnd, NULL, TRUE);
    return GUI_OK;
}

int gui_set_fgcolor(void *handle, GuiColor color) {
    if (!handle) return GUI_ERR_INVALID_PARAM;
    HWND hwnd = NULL;
    GuiControl_Win *ctrl = NULL;
    GuiWindow_Win *win = (GuiWindow_Win*)handle;

    EnterCriticalSection(&g_gui_state.cs);
    GuiWindow_Win *w = g_gui_state.windows;
    while (w) {
        if (w == win) { hwnd = w->hwnd; break; }
        GuiControl_Win *c = w->controls;
        while (c) {
            if (c == ctrl || c == (GuiControl_Win*)handle) { hwnd = c->hwnd; ctrl = c; break; }
            c = c->next;
        }
        if (hwnd) break;
        w = w->next;
    }
    LeaveCriticalSection(&g_gui_state.cs);

    if (!hwnd) return GUI_ERR_INVALID_PARAM;
    if (ctrl) ctrl->props.fgcolor = color;
    return GUI_OK;
}

/* ============================================================
 * 剪贴板操作函数实现
 * ============================================================ */
int gui_clipboard_copy(const char *text) {
    if (!text) return GUI_ERR_INVALID_PARAM;
    if (!OpenClipboard(NULL)) { gui_set_error("无法打开剪贴板"); return GUI_ERR_UNKNOWN; }
    EmptyClipboard();

    size_t wlen = strlen(text);
    size_t wbuf_size = MultiByteToWideChar(CP_UTF8, 0, text, -1, NULL, 0);
    HGLOBAL mem = GlobalAlloc(GMEM_MOVEABLE, wbuf_size * sizeof(wchar_t));
    if (!mem) { CloseClipboard(); return GUI_ERR_MEMORY; }

    wchar_t *wstr = (wchar_t*)GlobalLock(mem);
    MultiByteToWideChar(CP_UTF8, 0, text, -1, wstr, (int)wbuf_size);
    GlobalUnlock(mem);
    SetClipboardData(CF_UNICODETEXT, mem);
    CloseClipboard();
    return GUI_OK;
}

char* gui_clipboard_paste(void) {
    if (!IsClipboardFormatAvailable(CF_UNICODETEXT) &&
        !IsClipboardFormatAvailable(CF_TEXT)) return NULL;
    if (!OpenClipboard(NULL)) return NULL;

    char *result = NULL;
    HGLOBAL mem = GetClipboardData(CF_UNICODETEXT);
    if (mem) {
        wchar_t *wstr = (wchar_t*)GlobalLock(mem);
        if (wstr) {
            int len = WideCharToMultiByte(CP_UTF8, 0, wstr, -1, NULL, 0, NULL, NULL);
            if (len > 0) {
                result = (char*)malloc(len);
                if (result) WideCharToMultiByte(CP_UTF8, 0, wstr, -1, result, len, NULL, NULL);
            }
            GlobalUnlock(mem);
        }
    }
    CloseClipboard();
    return result;
}

#else /* GUI_PLATFORM_LINUX - 空桩实现（Linux版在gui_linux.c中已有完整实现） */

void gui_utf8_to_wide(const char *src, void *dst, int dst_count) { (void)src; (void)dst; (void)dst_count; }
void gui_wide_to_utf8(const void *src, char *dst, int dst_count) { (void)src; (void)dst; (void)dst_count; }

const char* gui_open_file_dialog(const char *title, const char *filter, const char *default_path) {
    (void)title; (void)filter; (void)default_path;
    gui_set_error_linux("Linux文件对话框尚未实现");
    return NULL;
}
const char* gui_save_file_dialog(const char *title, const char *filter, const char *default_name) {
    (void)title; (void)filter; (void)default_name;
    gui_set_error_linux("Linux保存对话框尚未实现");
    return NULL;
}
const char* gui_select_folder_dialog(const char *title, const char *default_path) {
    (void)title; (void)default_path;
    gui_set_error_linux("Linux文件夹选择对话框尚未实现");
    return NULL;
}
int gui_copy_to_clipboard(void *window_handle, const char *text) {
    (void)window_handle; (void)text;
    gui_set_error_linux("Linux剪贴板操作尚未实现");
    return GUI_ERR_UNKNOWN;
}
char* gui_paste_from_clipboard(void *window_handle) {
    (void)window_handle;
    gui_set_error_linux("Linux剪贴板操作尚未实现");
    return NULL;
}

#endif /* GUI_PLATFORM_WINDOWS / GUI_PLATFORM_LINUX */
