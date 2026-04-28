/**
 * @file gui_linux.c
 * @brief LXCLUA GUI Linux平台实现 - GTK3原生GUI核心实现
 * @author DiferLine
 * @version 1.0.0
 * 
 * 本文件实现了Linux平台下基于GTK3的GUI系统，包括：
 * - 窗口创建与管理（GtkWindow + GtkFixed绝对定位布局）
 * - 控件创建与操作（按钮、标签、输入框、列表框、下拉框、滑块等）
 * - 事件处理（GTK信号桥接到统一回调系统）
 * - 消息循环（gtk_main / gtk_main_iteration）
 */

#include "gui_linux.h"

#if defined(GUI_PLATFORM_LINUX)

#include <stdarg.h>
#include <unistd.h>
#include <pango/pangocairo.h>

/* ============================================================
 * 全局状态变量
 * ============================================================ */
GuiSystemState_Linux g_gui_state_linux = {0};
char g_last_error_linux[1024] = {0};

static int g_exit_code = 0;
static int g_should_exit = 0;

/* ============================================================
 * GTK信号处理器前向声明（桥接GTK信号→统一事件系统）
 * ============================================================ */
static gboolean gui_on_window_delete_event(GtkWidget *widget, GdkEvent *event, gpointer user_data);
static void gui_on_window_destroy(GtkWidget *widget, gpointer user_data);
static gboolean gui_on_window_configure(GtkWidget *widget, GdkEventConfigure *event, gpointer user_data);
static gboolean gui_on_window_key_press(GtkWidget *widget, GdkEventKey *event, gpointer user_data);
static gboolean gui_on_window_key_release(GtkWidget *widget, GdkEventKey *event, gpointer user_data);
static void gui_on_button_clicked(GtkWidget *widget, gpointer user_data);
static void gui_on_toggle_toggled(GtkWidget *widget, gpointer user_data);
static void gui_on_entry_changed(GtkWidget *widget, gpointer user_data);
static void gui_on_entry_activate(GtkWidget *widget, gpointer user_data);
static void gui_on_combo_changed(GtkWidget *widget, gpointer user_data);
static void gui_on_slider_changed(GtkWidget *widget, gpointer user_data);
static void gui_on_listbox_row_activated(GtkListBox *box, GtkListBoxRow *row, gpointer user_data);
static void gui_on_listbox_row_selected(GtkListBox *box, GtkListBoxRow *row, gpointer user_data);

/* ============================================================
 * 错误处理辅助函数
 * ============================================================ */
void gui_set_error_linux(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    vsnprintf(g_last_error_linux, sizeof(g_last_error_linux) - 1, fmt, args);
    va_end(args);
}

const char* gui_get_last_error(void) {
    return g_last_error_linux;
}

/* UTF-8工具函数（Linux原生UTF-8，无需转换） */
const char* gui_to_platform_string(const char *utf8_str) {
    return utf8_str ? utf8_str : "";
}

char* gui_from_platform_string(const char *platform_str) {
    if (!platform_str) return NULL;
    size_t len = strlen(platform_str);
    char *result = (char*)malloc(len + 1);
    if (result) {
        memcpy(result, platform_str, len + 1);
    }
    return result;
}

int gui_is_initialized(void) {
    return (g_gui_state_linux.state == GUI_STATE_INITIALIZED ||
            g_gui_state_linux.state == GUI_STATE_RUNNING);
}

/* ============================================================
 * GTK颜色辅助函数：GuiColor → GdkRGBA
 * ============================================================ */
static GdkRGBA gui_color_to_gdk(GuiColor c) {
    GdkRGBA rgba;
    rgba.red = c.r / 255.0;
    rgba.green = c.g / 255.0;
    rgba.blue = c.b / 255.0;
    rgba.alpha = c.a / 255.0;
    return rgba;
}

/* ============================================================
 * 字体辅助函数（CJK安全 - 自动回退到系统中文字体）
 * ============================================================ */

/** 获取支持中文的Pango字体描述（自动检测系统可用字体） */
static PangoFontDescription* gui_create_cjk_font(int size_pt, const char *custom_name) {
    const char *cjk_fonts[] = {
        "Noto Sans CJK SC",
        "WenQuanYi Micro Hei",
        "WenQuanYi Zen Hei",
        "Droid Sans Fallback",
        "AR PL UMing CN",
        "Microsoft YaHei",
        "SimHei",
        "Sans", /* 最终回退 */
        NULL
    };
    
    if (custom_name && custom_name[0]) {
        char buf[128];
        snprintf(buf, sizeof(buf), "%s %d", custom_name, size_pt > 0 ? size_pt : 10);
        return pango_font_description_from_string(buf);
    }
    
    /* 尝试找到可用的CJK字体 */
    for (int i = 0; cjk_fonts[i]; i++) {
        PangoFontMap *fontmap = pango_cairo_font_map_get_default();
        PangoContext *ctx = pango_font_map_create_context(fontmap);
        PangoFontDescription *desc = pango_font_description_from_string(cjk_fonts[i]);
        if (size_pt > 0) pango_font_description_set_size(desc, size_pt * PANGO_SCALE);
        
        PangoFont *font = pango_context_load_font(ctx, desc);
        if (font) {
            g_object_unref(font);
            pango_font_description_free(desc);
            g_object_unref(ctx);
            /* 找到可用字体，返回新的副本 */
            return gui_create_cjk_font(size_pt, cjk_fonts[i]);
        }
        pango_font_description_free(desc);
        g_object_unref(ctx);
    }
    
    /* 最终回退：使用默认Sans字体，让Pango/fontconfig处理回退 */
    PangoFontDescription *desc = pango_font_description_from_string("Sans");
    if (size_pt > 0) pango_font_description_set_size(desc, size_pt * PANGO_SCALE);
    return desc;
}

/** 为widget设置CJK安全的字体（不破坏fontconfig回退链） */
static void gui_widget_set_font_safe(GtkWidget *widget, int size_pt, const char *font_name) {
    if (!widget) return;
    PangoFontDescription *desc = gui_create_cjk_font(size_pt, font_name);
    gtk_widget_override_font(widget, desc); /* GTK3.16+ 仍然可用，只是deprecated但功能正常 */
    pango_font_description_free(desc);
}

/* ============================================================
 * 系统初始化与清理
 * ============================================================ */
int gui_init(int *argc, char ***argv) {
    if (g_gui_state_linux.state != GUI_STATE_UNINITIALIZED) {
        return GUI_OK;
    }
    
    memset(&g_gui_state_linux, 0, sizeof(g_gui_state_linux));
    
    /* 初始化GTK */
    if (!gtk_init_check(argc, argv)) {
        gui_set_error_linux("GTK3初始化失败");
        return GUI_ERR_WINDOW_CREATE;
    }
    
    /* 创建默认字体（CJK安全） */
    g_gui_state_linux.default_font = gui_create_cjk_font(10, NULL);
    g_gui_state_linux.title_font = gui_create_cjk_font(11, "Sans Bold");
    g_gui_state_linux.mono_font = gui_create_cjk_font(10, "Monospace");
    
    /* 默认主题色 */
    g_gui_state_linux.theme_bg = (GuiColor){240, 240, 240, 255};
    g_gui_state_linux.theme_fg = (GuiColor){0, 0, 0, 255};
    g_gui_state_linux.theme_accent = (GuiColor){0, 120, 215, 255};
    
    g_gui_state_linux.state = GUI_STATE_INITIALIZED;
    g_should_exit = 0;
    g_exit_code = 0;
    
    return GUI_OK;
}

int gui_cleanup(void) {
    /* 销毁所有窗口 */
    GuiWindow_Linux *win = g_gui_state_linux.windows;
    while (win) {
        GuiWindow_Linux *next = win->next;
        gui_destroy_window(win);
        win = next;
    }
    
    /* 释放字体 */
    if (g_gui_state_linux.default_font) {
        pango_font_description_free(g_gui_state_linux.default_font);
        g_gui_state_linux.default_font = NULL;
    }
    if (g_gui_state_linux.title_font) {
        pango_font_description_free(g_gui_state_linux.title_font);
        g_gui_state_linux.title_font = NULL;
    }
    if (g_gui_state_linux.mono_font) {
        pango_font_description_free(g_gui_state_linux.mono_font);
        g_gui_state_linux.mono_font = NULL;
    }
    
    memset(&g_gui_state_linux, 0, sizeof(g_gui_state_linux));
    g_gui_state_linux.state = GUI_STATE_UNINITIALIZED;
    
    return GUI_OK;
}

/* ============================================================
 * 窗口管理实现
 * ============================================================ */
GuiWindow_Linux* gui_create_window(GuiWindowConfig *config) {
    if (!gui_is_initialized()) {
        gui_set_error_linux("GUI未初始化");
        return NULL;
    }
    if (!config) {
        gui_set_error_linux("配置为NULL");
        return NULL;
    }
    
    GuiWindow_Linux *window = (GuiWindow_Linux*)calloc(1, sizeof(GuiWindow_Linux));
    if (!window) {
        gui_set_error_linux("内存分配失败");
        return NULL;
    }
    
    memcpy(&window->config, config, sizeof(GuiWindowConfig));
    
    /* 创建GTK窗口 */
    window->window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    if (!window->window) {
        free(window);
        gui_set_error_linux("GTK窗口创建失败");
        return NULL;
    }
    
    /* 设置标题 */
    if (config->title) {
        gtk_window_set_title(GTK_WINDOW(window->window), config->title);
    }
    
    /* 设置尺寸和位置 */
    gint w = config->width > 0 ? config->width : 800;
    gint h = config->height > 0 ? config->height : 600;
    gtk_window_set_default_size(GTK_WINDOW(window->window), w, h);
    
    /* 可调整大小 */
    gtk_window_set_resizable(GTK_WINDOW(window->window), config->resizable ? TRUE : FALSE);
    
    /* 居中显示 */
    if (config->center_screen) {
        gtk_window_set_position(GTK_WINDOW(window->window), GTK_WIN_POS_CENTER);
    } else if (config->x >= 0 && config->y >= 0) {
        gtk_window_move(GTK_WINDOW(window->window), config->x, config->y);
    }
    
    /* 置顶 */
    if (config->topmost) {
        gtk_window_set_keep_above(GTK_WINDOW(window->window), TRUE);
    }
    
    /* 创建固定定位容器 */
    window->fixed_container = gtk_fixed_new();
    gtk_container_add(GTK_CONTAINER(window->window), window->fixed_container);
    gtk_widget_show(window->fixed_container);
    
    /* 设置窗口默认CJK字体（继承给所有子控件） */
    if (g_gui_state_linux.default_font) {
        gtk_widget_override_font(window->window, g_gui_state_linux.default_font);
    }
    
    /* 设置背景色 */
    if (config->bgcolor.a > 0) {
        GdkRGBA rgba = {(double)config->bgcolor.r/255, (double)config->bgcolor.g/255,
                         (double)config->bgcolor.b/255, (double)config->bgcolor.a/255};
        gtk_widget_override_background_color(window->window, GTK_STATE_FLAG_NORMAL, &rgba);
    }
    
    /* 连接窗口级信号 */
    g_signal_connect(window->window, "delete-event",
                     G_CALLBACK(gui_on_window_delete_event), window);
    g_signal_connect(window->window, "destroy",
                     G_CALLBACK(gui_on_window_destroy), window);
    g_signal_connect(window->window, "configure-event",
                     G_CALLBACK(gui_on_window_configure), window);
    g_signal_connect(window->window, "key-press-event",
                     G_CALLBACK(gui_on_window_key_press), window);
    g_signal_connect(window->window, "key-release-event",
                     G_CALLBACK(gui_on_window_key_release), window);
    
    /* 加入全局窗口链表 */
    window->next = g_gui_state_linux.windows;
    g_gui_state_linux.windows = window;
    g_gui_state_linux.window_count++;
    
    return window;
}

int gui_destroy_window(GuiWindow_Linux *window) {
    if (!window) return GUI_OK;
    
    window->is_closing = 1;
    
    /* 分发销毁事件 */
    GuiEventData data = {0};
    gui_dispatch_event(window, GUI_EVENT_DESTROY, &data);
    
    /* 销毁所有控件 */
    GuiControl_Linux *ctrl = window->controls;
    while (ctrl) {
        GuiControl_Linux *next_ctrl = ctrl->next;
        if (ctrl->widget && !gtk_widget_in_destruction(ctrl->widget)) {
            gtk_widget_destroy(ctrl->widget);
        }
        free(ctrl);
        ctrl = next_ctrl;
    }
    
    /* 从链表中移除 */
    GuiWindow_Linux **pp = &g_gui_state_linux.windows;
    while (*pp && *pp != window) pp = &(*pp)->next;
    if (*pp) {
        *pp = window->next;
        g_gui_state_linux.window_count--;
    }
    
    /* 销毁GTK窗口 */
    if (window->window && !gtk_widget_in_destruction(window->window)) {
        gtk_widget_destroy(window->window);
    }
    
    free(window);
    return GUI_OK;
}

int gui_show_window(GuiWindow_Linux *window) {
    if (!window || !window->window) return GUI_ERR_INVALID_PARAM;
    gtk_widget_show_all(window->window);
    
    GuiEventData data = {0};
    gui_dispatch_event(window, GUI_EVENT_CREATE, &data);
    
    return GUI_OK;
}

int gui_hide_window(GuiWindow_Linux *window) {
    if (!window || !window->window) return GUI_ERR_INVALID_PARAM;
    gtk_widget_hide(window->window);
    return GUI_OK;
}

int gui_set_window_title(GuiWindow_Linux *window, const char *title) {
    if (!window || !window->window) return GUI_ERR_INVALID_PARAM;
    gtk_window_set_title(GTK_WINDOW(window->window), title ? title : "");
    if (window->config.title) free((void*)window->config.title);
    window->config.title = title ? strdup(title) : NULL;
    return GUI_OK;
}

const char* gui_get_window_title(GuiWindow_Linux *window) {
    if (!window || !window->window) return "";
    const char *t = gtk_window_get_title(GTK_WINDOW(window->window));
    return t ? t : "";
}

int gui_set_window_rect(GuiWindow_Linux *window, GuiRect *rect) {
    if (!window || !rect) return GUI_ERR_INVALID_PARAM;
    gtk_window_resize(GTK_WINDOW(window->window), rect->width, rect->height);
    gtk_window_move(GTK_WINDOW(window->window), rect->x, rect->y);
    return GUI_OK;
}

GuiRect gui_get_window_rect(GuiWindow_Linux *window) {
    GuiRect r = {0};
    if (window && window->window) {
        gint x, y, w, h;
        gtk_window_get_position(GTK_WINDOW(window->window), &x, &y);
        gtk_window_get_size(GTK_WINDOW(window->window), &w, &h);
        r.x = x; r.y = y; r.width = w; r.height = h;
    }
    return r;
}

int gui_set_window_opacity(GuiWindow_Linux *window, double opacity) {
    if (!window || !window->window) return GUI_ERR_INVALID_PARAM;
    gtk_widget_set_opacity(window->window, opacity);
    return GUI_OK;
}

int gui_center_window(GuiWindow_Linux *window) {
    if (!window || !window->window) return GUI_ERR_INVALID_PARAM;
    gtk_window_set_position(GTK_WINDOW(window->window), GTK_WIN_POS_CENTER);
    return GUI_OK;
}

int gui_enable_window(GuiWindow_Linux *window, int enabled) {
    if (!window || !window->window) return GUI_ERR_INVALID_PARAM;
    gtk_widget_set_sensitive(window->window, enabled ? TRUE : FALSE);
    return GUI_OK;
}

int gui_set_window_bgcolor(GuiWindow_Linux *window, GuiColor color) {
    if (!window || !window->window) return GUI_ERR_INVALID_PARAM;
    window->config.bgcolor = color;
    
    GdkRGBA rgba = {(double)color.r/255, (double)color.g/255,
                     (double)color.b/255, (double)color.a/255};
    gtk_widget_override_background_color(window->window, GTK_STATE_FLAG_NORMAL, &rgba);
    gtk_widget_queue_draw(window->window);
    return GUI_OK;
}

int gui_flash_window(GuiWindow_Linux *window, unsigned int count) {
    if (!window || !window->window) return GUI_ERR_INVALID_PARAM;
    gtk_window_set_urgency_hint(GTK_WINDOW(window->window), TRUE);
    return GUI_OK;
}

/* ============================================================
 * 控件创建实现
 * ============================================================ */
GuiControl_Linux* gui_create_control(GuiWindow_Linux *window,
                                      GuiControlType type,
                                      GuiControlProps *props) {
    if (!window || !window->fixed_container) return NULL;
    if (!props) return NULL;
    
    GuiControl_Linux *control = (GuiControl_Linux*)calloc(1, sizeof(GuiControl_Linux));
    if (!control) return NULL;
    
    control->type = type;
    control->id = window->next_ctrl_id++;
    memcpy(&control->props, props, sizeof(GuiControlProps));
    
    GtkWidget *widget = NULL;
    const char *text = props->text ? props->text : "";
    
    switch (type) {
        case GUI_CTRL_BUTTON:
            widget = gtk_button_new_with_label(text);
            break;
            
        case GUI_CTRL_LABEL:
            widget = gtk_label_new(text);
            gtk_label_set_xalign(GTK_LABEL(widget), 0.0f);
            gtk_label_set_yalign(GTK_LABEL(widget), 0.5f);
            gtk_widget_set_halign(widget, GTK_ALIGN_START);
            break;
            
        case GUI_CTRL_EDIT:
            widget = gtk_entry_new();
            if (text[0]) gtk_entry_set_text(GTK_ENTRY(widget), text);
            gtk_entry_set_max_length(GTK_ENTRY(widget), 65535);
            break;
            
        case GUI_CTRL_TEXTBOX: {
            widget = gtk_scrolled_window_new(NULL, NULL);
            gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(widget),
                GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
            GtkTextView *tv = gtk_text_view_new();
            gtk_text_view_set_wrap_mode(tv, GTK_WRAP_WORD_CHAR);
            if (text[0]) {
                GtkTextBuffer *buf = gtk_text_view_get_buffer(tv);
                gtk_text_buffer_set_text(buf, text, -1);
            }
            gtk_container_add(GTK_CONTAINER(widget), GTK_WIDGET(tv));
            gtk_widget_show(GTK_WIDGET(tv));
            /* 将内层textView作为实际操作对象标记 */
            g_object_set_data(G_OBJECT(widget), "inner-textview", tv);
            break;
        }
        
        case GUI_CTRL_CHECKBOX:
            widget = gtk_check_button_new_with_label(text);
            break;
            
        case GUI_CTRL_RADIOBUTTON:
            widget = gtk_radio_button_new_with_label(NULL, text);
            break;
            
        case GUI_CTRL_GROUPBOX:
            widget = gtk_frame_new(text);
            break;
            
        case GUI_CTRL_LISTBOX: {
            widget = gtk_scrolled_window_new(NULL, NULL);
            gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(widget),
                GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
            GtkListBox *lb = gtk_list_box_new();
            gtk_container_add(GTK_CONTAINER(widget), GTK_WIDGET(lb));
            gtk_widget_show(GTK_WIDGET(lb));
            g_object_set_data(G_OBJECT(widget), "inner-listbox", lb);
            break;
        }
        
        case GUI_CTRL_COMBOBOX:
            widget = gtk_combo_box_text_new_with_entry();
            /* 填充预置项在addItem中完成 */
            break;
            
        case GUI_CTRL_PROGRESSBAR:
            widget = gtk_progress_bar_new();
            gtk_progress_bar_set_show_text(GTK_PROGRESS_BAR(widget), TRUE);
            gtk_widget_set_size_request(widget, -1, 24);
            break;
            
        case GUI_CTRL_SLIDER:
            widget = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL,
                0, 100, 1);
            gtk_scale_set_value_pos(GTK_SCALE(widget), GTK_POS_RIGHT);
            gtk_widget_set_size_request(widget, -1, 28);
            break;
            
        case GUI_CTRL_TABCONTROL:
            widget = gtk_notebook_new();
            break;
        
        default:
            gui_set_error_linux("不支持的控件类型: %d", type);
            free(control);
            return NULL;
    }
    
    if (!widget) {
        free(control);
        return NULL;
    }
    
    control->widget = widget;

    /* 设置字体（CJK安全方式 - 使用PangoFontDescription） */
    if (props->font_size > 0 || props->font_name) {
        gui_widget_set_font_safe(widget, props->font_size, props->font_name);
        /* 对于容器控件（TextBox/ListBox），也设置内部子控件的字体 */
        if (type == GUI_CTRL_TEXTBOX) {
            GtkTextView *tv = GTK_TEXT_VIEW(g_object_get_data(G_OBJECT(widget), "inner-textview"));
            if (tv) gui_widget_set_font_safe(GTK_WIDGET(tv), props->font_size, props->font_name);
        } else if (type == GUI_CTRL_LISTBOX) {
            GtkListBox *lb = GTK_LIST_BOX(g_object_get_data(G_OBJECT(widget), "inner-listbox"));
            if (lb) gui_widget_set_font_safe(GTK_WIDGET(lb), props->font_size, props->font_name);
        }
    }
    
    /* 设置可见性和启用状态 */
    if (!props->visible) gtk_widget_hide(widget);
    if (!props->enabled) gtk_widget_set_sensitive(widget, FALSE);
    
    /* 放入固定容器（绝对定位） */
    gtk_fixed_put(GTK_FIXED(window->fixed_container), widget,
                   props->rect.x, props->rect.y);
    gtk_widget_set_size_request(widget, props->rect.width, props->rect.height);
    gtk_widget_show(widget);
    
    /* 连接控件信号 */
    switch (type) {
        case GUI_CTRL_BUTTON:
            g_signal_connect(widget, "clicked",
                G_CALLBACK(gui_on_button_clicked), control);
            break;
        case GUI_CTRL_CHECKBOX:
        case GUI_CTRL_RADIOBUTTON:
            g_signal_connect(widget, "toggled",
                G_CALLBACK(gui_on_toggle_toggled), control);
            break;
        case GUI_CTRL_EDIT:
            g_signal_connect(widget, "changed",
                G_CALLBACK(gui_on_entry_changed), control);
            g_signal_connect(widget, "activate",
                G_CALLBACK(gui_on_entry_activate), control);
            break;
        case GUI_CTRL_COMBOBOX:
            g_signal_connect(widget, "changed",
                G_CALLBACK(gui_on_combo_changed), control);
            break;
        case GUI_CTRL_SLIDER:
            g_signal_connect(widget, "value-changed",
                G_CALLBACK(gui_on_slider_changed), control);
            break;
        case GUI_CTRL_LISTBOX: {
            GtkListBox *lb = GTK_LIST_BOX(g_object_get_data(G_OBJECT(widget), "inner-listbox"));
            if (lb) {
                /* 双击行触发事件 */
                g_signal_connect(lb, "row-activated",
                    G_CALLBACK(gui_on_listbox_row_activated), control);
                /* 选中行改变时也触发事件（用于单选） */
                g_signal_connect(lb, "row-selected",
                    G_CALLBACK(gui_on_listbox_row_selected), control);
            }
            break;
        }
        default:
            break;
    }
    
    /* 加入窗口的控件链表 */
    control->next = window->controls;
    window->controls = control;
    window->control_count++;
    
    return control;
}

/* ============================================================
 * 控件属性操作
 * ============================================================ */
int gui_set_control_text(GuiControl_Linux *control, const char *text) {
    if (!control || !control->widget) return GUI_ERR_INVALID_PARAM;
    
    switch (control->type) {
        case GUI_CTRL_BUTTON:
            gtk_button_set_label(GTK_BUTTON(control->widget), text ? text : "");
            break;
        case GUI_CTRL_LABEL:
            gtk_label_set_text(GTK_LABEL(control->widget), text ? text : "");
            break;
        case GUI_CTRL_EDIT:
            gtk_entry_set_text(GTK_ENTRY(control->widget), text ? text : "");
            break;
        case GUI_CTRL_CHECKBOX:
        case GUI_CTRL_RADIOBUTTON:
            gtk_button_set_label(GTK_BUTTON(control->widget), text ? text : "");
            break;
        case GUI_CTRL_GROUPBOX:
            gtk_frame_set_label(GTK_FRAME(control->widget), text ? text : "");
            break;
        default:
            break;
    }
    return GUI_OK;
}

const char* gui_get_control_text(GuiControl_Linux *control) {
    static char buf[4096];
    buf[0] = '\0';
    if (!control || !control->widget) return buf;
    
    switch (control->type) {
        case GUI_CTRL_BUTTON: {
            const char *t = gtk_button_get_label(GTK_BUTTON(control->widget));
            if (t) strncpy(buf, t, sizeof(buf)-1);
            break;
        }
        case GUI_CTRL_LABEL: {
            const char *t = gtk_label_get_text(GTK_LABEL(control->widget));
            if (t) strncpy(buf, t, sizeof(buf)-1);
            break;
        }
        case GUI_CTRL_EDIT: {
            const char *t = gtk_entry_get_text(GTK_ENTRY(control->widget));
            if (t) strncpy(buf, t, sizeof(buf)-1);
            break;
        }
        case GUI_CTRL_CHECKBOX:
        case GUI_CTRL_RADIOBUTTON: {
            const char *t = gtk_button_get_label(GTK_BUTTON(control->widget));
            if (t) strncpy(buf, t, sizeof(buf)-1);
            break;
        }
        default:
            break;
    }
    return buf;
}

int gui_set_control_rect(GuiControl_Linux *control, GuiRect *rect) {
    if (!control || !control->widget || !rect) return GUI_ERR_INVALID_PARAM;
    
    GtkWidget *parent = gtk_widget_get_parent(control->widget);
    if (parent && GTK_IS_FIXED(parent)) {
        gtk_fixed_move(GTK_FIXED(parent), control->widget, rect->x, rect->y);
    }
    gtk_widget_set_size_request(control->widget, rect->width, rect->height);
    control->props.rect = *rect;
    return GUI_OK;
}

GuiRect gui_get_control_rect(GuiControl_Linux *control) {
    GuiRect r = {0};
    if (control) r = control->props.rect;
    return r;
}

int gui_set_control_visible(GuiControl_Linux *control, int visible) {
    if (!control || !control->widget) return GUI_ERR_INVALID_PARAM;
    if (visible)
        gtk_widget_show(control->widget);
    else
        gtk_widget_hide(control->widget);
    return GUI_OK;
}

int gui_set_control_enabled(GuiControl_Linux *control, int enabled) {
    if (!control || !control->widget) return GUI_ERR_INVALID_PARAM;
    gtk_widget_set_sensitive(control->widget, enabled ? TRUE : FALSE);
    return GUI_OK;
}

int gui_set_control_focus(GuiControl_Linux *control) {
    if (!control || !control->widget) return GUI_ERR_INVALID_PARAM;
    gtk_widget_grab_focus(control->widget);
    return GUI_OK;
}

int gui_control_to_front(GuiControl_Linux *control) {
    if (!control || !control->widget) return GUI_ERR_INVALID_PARAM;
    gdk_window_raise(gtk_widget_get_window(control->widget));
    return GUI_OK;
}

GuiControl_Linux* gui_find_control_by_widget(GuiWindow_Linux *window, GtkWidget *widget) {
    if (!window || !widget) return NULL;
    GuiControl_Linux *ctrl = window->controls;
    while (ctrl) {
        if (ctrl->widget == widget) return ctrl;
        ctrl = ctrl->next;
    }
    return NULL;
}

/* ============================================================
 * 事件回调注册
 * ============================================================ */
int gui_register_event_callback(GuiWindow_Linux *window,
                                GuiEventType event_type,
                                GuiEventCallback callback) {
    if (!window) return GUI_ERR_INVALID_PARAM;
    
    switch (event_type) {
        case GUI_EVENT_CREATE:     window->on_create = callback; break;
        case GUI_EVENT_DESTROY:    window->on_destroy = callback; break;
        case GUI_EVENT_CLOSE:      window->on_close = callback; break;
        case GUI_EVENT_SIZE:       window->on_size = callback; break;
        case GUI_EVENT_MOVE:       window->on_move = callback; break;
        case GUI_EVENT_PAINT:      window->on_paint = callback; break;
        case GUI_EVENT_KEYDOWN:    window->on_keydown = callback; break;
        case GUI_EVENT_KEYUP:      window->on_keyup = callback; break;
        case GUI_EVENT_CHAR:       window->on_char = callback; break;
        case GUI_EVENT_DROPFILES:  window->on_dropfiles = callback; break;
        case GUI_EVENT_TIMER:      window->on_timer = callback; break;
        default: return GUI_ERR_INVALID_PARAM;
    }
    return GUI_OK;
}

int gui_set_event_callback_id(GuiWindow_Linux *window,
                              GuiEventType event_type,
                              int cb_id) {
    if (!window) return GUI_ERR_INVALID_PARAM;
    
    switch (event_type) {
        case GUI_EVENT_CREATE:     window->cb_id_create = cb_id; break;
        case GUI_EVENT_DESTROY:    window->cb_id_destroy = cb_id; break;
        case GUI_EVENT_CLOSE:      window->cb_id_close = cb_id; break;
        case GUI_EVENT_SIZE:       window->cb_id_size = cb_id; break;
        case GUI_EVENT_MOVE:       window->cb_id_move = cb_id; break;
        case GUI_EVENT_PAINT:      window->cb_id_paint = cb_id; break;
        case GUI_EVENT_KEYDOWN:    window->cb_id_keydown = cb_id; break;
        case GUI_EVENT_KEYUP:      window->cb_id_keyup = cb_id; break;
        case GUI_EVENT_CHAR:       window->cb_id_char = cb_id; break;
        case GUI_EVENT_DROPFILES:  window->cb_id_dropfiles = cb_id; break;
        case GUI_EVENT_TIMER:      window->cb_id_timer = cb_id; break;
        default: return GUI_ERR_INVALID_PARAM;
    }
    return GUI_OK;
}

int gui_register_control_callback(GuiControl_Linux *control,
                                  GuiEventType event_type,
                                  GuiEventCallback callback) {
    if (!control) return GUI_ERR_INVALID_PARAM;
    
    switch (event_type) {
        case GUI_EVENT_CLICK:   control->on_click = callback; break;
        case GUI_EVENT_CHANGE:  control->on_change = callback; break;
        case GUI_EVENT_FOCUS:   control->on_focus = callback; break;
        default: return GUI_ERR_INVALID_PARAM;
    }
    return GUI_OK;
}

/* ============================================================
 * 事件分发函数
 * ============================================================ */
void gui_dispatch_event(GuiWindow_Linux *window, GuiEventType type, GuiEventData *data) {
    if (!window) return;
    data->type = type;
    
    void *cb_data = NULL;
    GuiEventCallback cb = NULL;
    
    switch (type) {
        case GUI_EVENT_CREATE:
            cb = window->on_create; cb_data = (void*)(intptr_t)window->cb_id_create; break;
        case GUI_EVENT_DESTROY:
            cb = window->on_destroy; cb_data = (void*)(intptr_t)window->cb_id_destroy; break;
        case GUI_EVENT_CLOSE:
            cb = window->on_close; cb_data = (void*)(intptr_t)window->cb_id_close; break;
        case GUI_EVENT_SIZE:
            cb = window->on_size; cb_data = (void*)(intptr_t)window->cb_id_size; break;
        case GUI_EVENT_MOVE:
            cb = window->on_move; cb_data = (void*)(intptr_t)window->cb_id_move; break;
        case GUI_EVENT_PAINT:
            cb = window->on_paint; cb_data = (void*)(intptr_t)window->cb_id_paint; break;
        case GUI_EVENT_KEYDOWN:
            cb = window->on_keydown; cb_data = (void*)(intptr_t)window->cb_id_keydown; break;
        case GUI_EVENT_KEYUP:
            cb = window->on_keyup; cb_data = (void*)(intptr_t)window->cb_id_keyup; break;
        case GUI_EVENT_CHAR:
            cb = window->on_char; cb_data = (void*)(intptr_t)window->cb_id_char; break;
        case GUI_EVENT_DROPFILES:
            cb = window->on_dropfiles; cb_data = (void*)(intptr_t)window->cb_id_dropfiles; break;
        case GUI_EVENT_TIMER:
            cb = window->on_timer; cb_data = (void*)(intptr_t)window->cb_id_timer; break;
        default:
            return;
    }
    
    if (cb && cb_data) {
        cb(window, data, cb_data);
    }
}

/* ============================================================
 * GTK信号处理器（桥接GTK信号→统一事件系统）
 * ============================================================ */

/* 窗口关闭信号 */
static gboolean gui_on_window_delete_event(GtkWidget *widget, GdkEvent *event,
                                            gpointer user_data) {
    GuiWindow_Linux *window = (GuiWindow_Linux*)user_data;
    GuiEventData data = {0};
    
    if (window->on_close) {
        void *cb_data = (void*)(intptr_t)window->cb_id_close;
        window->on_close(window, &data, cb_data);
    }
    
    return FALSE; /* 返回FALSE允许GTK继续销毁窗口 */
}

/* 窗口销毁信号 */
static void gui_on_window_destroy(GtkWidget *widget, gpointer user_data) {
    GuiWindow_Linux *window = (GuiWindow_Linux*)user_data;
    window->is_closing = 1;
    
    if (g_gui_state_linux.window_count <= 1) {
        g_should_exit = 1;
    }
}

/* 窗口配置改变信号（resize/move） */
static gboolean gui_on_window_configure(GtkWidget *widget, GdkEventConfigure *event,
                                         gpointer user_data) {
    GuiWindow_Linux *window = (GuiWindow_Linux*)user_data;
    
    static int last_w = 0, last_h = 0, last_x = 0, last_y = 0;
    
    if (event->width != last_w || event->height != last_h) {
        last_w = event->width; last_h = event->height;
        GuiEventData data = {0};
        data.size.width = event->width;
        data.size.height = event->height;
        gui_dispatch_event(window, GUI_EVENT_SIZE, &data);
    }
    
    if (event->x != last_x || event->y != last_y) {
        last_x = event->x; last_y = event->y;
        GuiEventData data = {0};
        data.position.x = event->x;
        data.position.y = event->y;
        gui_dispatch_event(window, GUI_EVENT_MOVE, &data);
    }
    
    return FALSE;
}

/* 键盘按下信号 */
static gboolean gui_on_window_key_press(GtkWidget *widget, GdkEventKey *event,
                                         gpointer user_data) {
    GuiWindow_Linux *window = (GuiWindow_Linux*)user_data;
    GuiEventData data = {0};
    data.keyboard.key = (unsigned int)event->keyval;
    data.keyboard.flags = (unsigned int)event->state;
    gui_dispatch_event(window, GUI_EVENT_KEYDOWN, &data);
    
    /* 字符输入事件 */
    gunichar ch = gdk_keyval_to_unicode(event->keyval);
    if (ch >= 32 && ch < 0xF700) {
        GuiEventData chdata = {0};
        chdata.character.ch = (unsigned int)ch;
        gui_dispatch_event(window, GUI_EVENT_CHAR, &chdata);
    }
    
    return FALSE;
}

/* 键盘释放信号 */
static gboolean gui_on_window_key_release(GtkWidget *widget, GdkEventKey *event,
                                           gpointer user_data) {
    GuiWindow_Linux *window = (GuiWindow_Linux*)user_data;
    GuiEventData data = {0};
    data.keyboard.key = (unsigned int)event->keyval;
    data.keyboard.flags = (unsigned int)event->state;
    gui_dispatch_event(window, GUI_EVENT_KEYUP, &data);
    return FALSE;
}

/* 按钮点击信号 */
static void gui_on_button_clicked(GtkWidget *widget, gpointer user_data) {
    GuiControl_Linux *control = (GuiControl_Linux*)user_data;
    GuiEventData data = {0};
    data.type = GUI_EVENT_CLICK;
    if (control->on_click) {
        control->on_click(control, &data, control->user_data);
    }
}

/* 复选框/单选按钮切换信号 */
static void gui_on_toggle_toggled(GtkWidget *widget, gpointer user_data) {
    GuiControl_Linux *control = (GuiControl_Linux*)user_data;
    GuiEventData data = {0};
    data.type = GUI_EVENT_CHANGE;
    if (control->on_change) {
        control->on_change(control, &data, control->user_data);
    }
    if (control->on_click) {
        control->on_click(control, &data, control->user_data);
    }
}

/* 输入框内容改变信号 */
static void gui_on_entry_changed(GtkWidget *widget, gpointer user_data) {
    GuiControl_Linux *control = (GuiControl_Linux*)user_data;
    GuiEventData data = {0};
    data.type = GUI_EVENT_CHANGE;
    if (control->on_change) {
        control->on_change(control, &data, control->user_data);
    }
}

/* 输入框回车信号 */
static void gui_on_entry_activate(GtkWidget *widget, gpointer user_data) {
    GuiControl_Linux *control = (GuiControl_Linux*)user_data;
    GuiEventData data = {0};
    data.type = GUI_EVENT_CLICK;
    if (control->on_click) {
        control->on_click(control, &data, control->user_data);
    }
}

/* 下拉框选择改变信号 */
static void gui_on_combo_changed(GtkWidget *widget, gpointer user_data) {
    GuiControl_Linux *control = (GuiControl_Linux*)user_data;
    GuiEventData data = {0};
    data.type = GUI_EVENT_SELECT;
    if (control->on_change) {
        control->on_change(control, &data, control->user_data);
    }
}

/* 滑块值改变信号 */
static void gui_on_slider_changed(GtkWidget *widget, gpointer user_data) {
    GuiControl_Linux *control = (GuiControl_Linux*)user_data;
    GuiEventData data = {0};
    data.type = GUI_EVENT_CHANGE;
    if (control->on_change) {
        control->on_change(control, &data, control->user_data);
    }
}

/* 列表框行双击激活事件 */
static void gui_on_listbox_row_activated(GtkListBox *box, GtkListBoxRow *row, gpointer user_data) {
    GuiControl_Linux *control = (GuiControl_Linux*)user_data;
    GuiEventData data = {0};
    data.type = GUI_EVENT_CLICK;
    if (control->on_click) {
        control->on_click(control, &data, control->user_data);
    }
}

/* 列表框行选中改变事件 */
static void gui_on_listbox_row_selected(GtkListBox *box, GtkListBoxRow *row, gpointer user_data) {
    GuiControl_Linux *control = (GuiControl_Linux*)user_data;
    /* row为NULL表示取消选中，忽略 */
    if (!row) return;
    GuiEventData data = {0};
    data.type = GUI_EVENT_SELECT;
    if (control->on_change) {
        control->on_change(control, &data, control->user_data);
    }
}

/* ============================================================
 * 消息循环
 * ============================================================ */
int gui_run_main_loop(void) {
    if (!gui_is_initialized()) return GUI_ERR_NOT_INITIALIZED;
    g_gui_state_linux.state = GUI_STATE_RUNNING;
    g_gui_state_linux.is_running = 1;
    g_should_exit = 0;
    
    gtk_main();
    
    g_gui_state_linux.is_running = 0;
    g_gui_state_linux.state = GUI_STATE_INITIALIZED;
    return g_exit_code;
}

int gui_process_events(void) {
    if (!gui_is_initialized()) return GUI_ERR_NOT_INITIALIZED;
    
    if (g_should_exit) {
        g_gui_state_linux.is_running = 0;
        return 0;
    }
    
    if (gtk_events_pending()) {
        gtk_main_iteration();
        return 1;
    }
    return 0;
}

int gui_exit_main_loop(int exit_code) {
    g_should_exit = 1;
    g_exit_code = exit_code;
    if (g_gui_state_linux.is_running) {
        gtk_main_quit();
    }
    return GUI_OK;
}

int gui_set_loop_mode(GuiLoopMode mode) {
    g_gui_state_linux.loop_mode = mode;
    return GUI_OK;
}

/* ============================================================
 * 对话框
 * ============================================================ */
int gui_message_box(const char *message, const char *title, const char *type_str) {
    if (!message) message = "";
    if (!title) title = "";
    
    GtkMessageType msg_type = GTK_MESSAGE_INFO;
    GtkButtonsType btn_type = GTK_BUTTONS_OK;
    
    /* 解析类型字符串 */
    if (type_str) {
        if (strstr(type_str, "yesno") || strstr(type_str, "yes_no"))
            btn_type = GTK_BUTTONS_YES_NO;
        else if (strstr(type_str, "okcancel") || strstr(type_str, "ok_cancel"))
            btn_type = GTK_BUTTONS_OK_CANCEL;
        else if (strstr(type_str, "retrycancel"))
            btn_type = GTK_BUTTONS_YES_NO; /* GTK无RETRY/CANCEL，用YES/NO近似 */
        
        if (strstr(type_str, "icon_error") || strstr(type_str, "error"))
            msg_type = GTK_MESSAGE_ERROR;
        else if (strstr(type_str, "icon_warning") || strstr(type_str, "warning"))
            msg_type = GTK_MESSAGE_WARNING;
        else if (strstr(type_str, "icon_question") || strstr(type_str, "question"))
            msg_type = GTK_MESSAGE_QUESTION;
    }
    
    GtkWidget *dialog = gtk_message_dialog_new(NULL,
        GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
        msg_type, btn_type, "%s", message);
    gtk_window_set_title(GTK_WINDOW(dialog), title);
    
    gint result = gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);
    
    /* 映射GTK返回值到Windows兼容值 */
    switch (result) {
        case GTK_RESPONSE_OK:         return 1;  /* IDOK */
        case GTK_RESPONSE_CANCEL:     return 2;  /* IDCANCEL */
        case GTK_RESPONSE_YES:        return 6;  /* IDYES */
        case GTK_RESPONSE_NO:         return 7;  /* IDNO */
        case GTK_RESPONSE_CLOSE:      return 2;  /* IDCANCEL */
        case GTK_RESPONSE_DELETE_EVENT:return 2;  /* IDCANCEL */
        default:                      return 0;
    }
}

/* ============================================================
 * 扩展控件操作 - 列表框
 * ============================================================ */
static GtkListBox* gui_get_listbox_inner(GuiControl_Linux *control) {
    if (!control || !control->widget) return NULL;
    GtkWidget *scroll = control->widget;
    return GTK_LIST_BOX(g_object_get_data(G_OBJECT(scroll), "inner-listbox"));
}

int gui_listbox_add_item(GuiControl_Linux *control, const char *text) {
    GtkListBox *lb = gui_get_listbox_inner(control);
    if (!lb) return GUI_ERR_INVALID_PARAM;
    GtkWidget *label = gtk_label_new(text ? text : "");
    gtk_widget_set_halign(label, GTK_ALIGN_START);
    gtk_list_box_insert(lb, label, -1);
    gtk_widget_show(label);
    return GUI_OK;
}

int gui_listbox_insert_item(GuiControl_Linux *control, int index, const char *text) {
    GtkListBox *lb = gui_get_listbox_inner(control);
    if (!lb) return GUI_ERR_INVALID_PARAM;
    GtkWidget *label = gtk_label_new(text ? text : "");
    gtk_widget_set_halign(label, GTK_ALIGN_START);
    gtk_list_box_insert(lb, label, index);
    gtk_widget_show(label);
    return GUI_OK;
}

int gui_listbox_remove_item(GuiControl_Linux *control, int index) {
    GtkListBox *lb = gui_get_listbox_inner(control);
    if (!lb) return GUI_ERR_INVALID_PARAM;
    GtkListBoxRow *row = gtk_list_box_get_row_at_index(lb, index);
    if (row) gtk_widget_destroy(GTK_WIDGET(row));
    return GUI_OK;
}

int gui_listbox_clear(GuiControl_Linux *control) {
    GtkListBox *lb = gui_get_listbox_inner(control);
    if (!lb) return GUI_ERR_INVALID_PARAM;
    GList *children = gtk_container_get_children(GTK_CONTAINER(lb));
    GList *iter;
    for (iter = children; iter; iter = iter->next) {
        gtk_widget_destroy(GTK_WIDGET(iter->data));
    }
    g_list_free(children);
    return GUI_OK;
}

int gui_listbox_get_selected_index(GuiControl_Linux *control) {
    GtkListBox *lb = gui_get_listbox_inner(control);
    if (!lb) return -1;
    GtkListBoxRow *row = gtk_list_box_get_selected_row(lb);
    if (!row) return -1;
    return gtk_list_box_row_get_index(row);
}

int gui_listbox_set_selected_index(GuiControl_Linux *control, int index) {
    GtkListBox *lb = gui_get_listbox_inner(control);
    if (!lb) return GUI_ERR_INVALID_PARAM;
    GtkListBoxRow *row = gtk_list_box_get_row_at_index(lb, index);
    if (row) gtk_list_box_select_row(lb, row);
    return GUI_OK;
}

int gui_listbox_get_count(GuiControl_Linux *control) {
    GtkListBox *lb = gui_get_listbox_inner(control);
    if (!lb) return 0;
    return g_list_length(gtk_container_get_children(GTK_CONTAINER(lb)));
}

const char* gui_listbox_get_item_text(GuiControl_Linux *control, int index) {
    static char buf[1024];
    buf[0] = '\0';
    GtkListBox *lb = gui_get_listbox_inner(control);
    if (!lb) return buf;
    GtkListBoxRow *row = gtk_list_box_get_row_at_index(lb, index);
    if (!row) return buf;
    GList *children = gtk_container_get_children(GTK_CONTAINER(row));
    if (children && GTK_IS_LABEL(children->data)) {
        const char *t = gtk_label_get_text(GTK_LABEL(children->data));
        if (t) strncpy(buf, t, sizeof(buf)-1);
    }
    g_list_free(children);
    return buf;
}

/* ============================================================
 * 扩展控件操作 - 下拉框
 * ============================================================ */
int gui_combobox_add_item(GuiControl_Linux *control, const char *text) {
    if (!control || !control->widget) return GUI_ERR_INVALID_PARAM;
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(control->widget), text ? text : "");
    return GUI_OK;
}

int gui_combobox_insert_item(GuiControl_Linux *control, int index, const char *text) {
    if (!control || !control->widget) return GUI_ERR_INVALID_PARAM;
    gtk_combo_box_text_insert_text(GTK_COMBO_BOX_TEXT(control->widget), index, text ? text : "");
    return GUI_OK;
}

int gui_combobox_remove_item(GuiControl_Linux *control, int index) {
    if (!control || !control->widget) return GUI_ERR_INVALID_PARAM;
    gtk_combo_box_text_remove(GTK_COMBO_BOX_TEXT(control->widget), index);
    return GUI_OK;
}

int gui_combobox_clear(GuiControl_Linux *control) {
    if (!control || !control->widget) return GUI_ERR_INVALID_PARAM;
    /* GTK ComboBoxText没有直接的clear方法，需要重建或逐个删除 */
    GtkTreeModel *model = gtk_combo_box_get_model(GTK_COMBO_BOX(control->widget));
    if (model) gtk_list_store_clear(GTK_LIST_STORE(model));
    return GUI_OK;
}

int gui_combobox_get_selected_index(GuiControl_Linux *control) {
    if (!control || !control->widget) return -1;
    return gtk_combo_box_get_active(GTK_COMBO_BOX(control->widget));
}

int gui_combobox_set_selected_index(GuiControl_Linux *control, int index) {
    if (!control || !control->widget) return GUI_ERR_INVALID_PARAM;
    gtk_combo_box_set_active(GTK_COMBO_BOX(control->widget), index);
    return GUI_OK;
}

int gui_combobox_get_count(GuiControl_Linux *control) {
    if (!control || !control->widget) return 0;
    GtkTreeModel *model = gtk_combo_box_get_model(GTK_COMBO_BOX(control->widget));
    if (!model) return 0;
    return gtk_tree_model_iter_n_children(model, NULL);
}

const char* gui_combobox_get_item_text(GuiControl_Linux *control, int index) {
    static char buf[1024];
    buf[0] = '\0';
    if (!control || !control->widget) return buf;
    GtkTreeModel *model = gtk_combo_box_get_model(GTK_COMBO_BOX(control->widget));
    if (!model) return buf;
    GtkTreeIter iter;
    if (gtk_tree_model_iter_nth_child(model, &iter, NULL, index)) {
        GValue val = G_VALUE_INIT;
        gtk_tree_model_get_value(model, &iter, 0, &val);
        const char *t = g_value_get_string(&val);
        if (t) strncpy(buf, t, sizeof(buf)-1);
        g_value_unset(&val);
    }
    return buf;
}

/* ============================================================
 * 扩展控件操作 - 复选框/单选
 * ============================================================ */
int gui_checkbox_get_checked(GuiControl_Linux *control) {
    if (!control || !control->widget) return 0;
    return gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(control->widget)) ? 1 : 0;
}

int gui_checkbox_set_checked(GuiControl_Linux *control, int checked) {
    if (!control || !control->widget) return GUI_ERR_INVALID_PARAM;
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(control->widget), checked ? TRUE : FALSE);
    return GUI_OK;
}

int gui_radio_get_checked(GuiControl_Linux *control) {
    if (!control || !control->widget) return 0;
    return gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(control->widget)) ? 1 : 0;
}

int gui_radio_set_checked(GuiControl_Linux *control, int checked) {
    if (!control || !control->widget) return GUI_ERR_INVALID_PARAM;
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(control->widget), checked ? TRUE : FALSE);
    return GUI_OK;
}

/* ============================================================
 * 扩展控件操作 - 进度条
 * ============================================================ */
int gui_progressbar_set_value(GuiControl_Linux *control, int value) {
    if (!control || !control->widget) return GUI_ERR_INVALID_PARAM;
    gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(control->widget),
        value / 100.0);
    return GUI_OK;
}

int gui_progressbar_get_value(GuiControl_Linux *control) {
    if (!control || !control->widget) return 0;
    return (int)(gtk_progress_bar_get_fraction(GTK_PROGRESS_BAR(control->widget)) * 100.0);
}

int gui_progressbar_set_range(GuiControl_Linux *control, int min_val, int max_val) {
    if (!control || !control->widget) return GUI_ERR_INVALID_PARAM;
    /* GTK进度条默认0-1范围，通过fraction映射 */
    return GUI_OK;
}

/* ============================================================
 * 扩展控件操作 - 滑块
 * ============================================================ */
int gui_slider_set_value(GuiControl_Linux *control, int value) {
    if (!control || !control->widget) return GUI_ERR_INVALID_PARAM;
    gtk_range_set_value(GTK_RANGE(control->widget), (double)value);
    return GUI_OK;
}

int gui_slider_get_value(GuiControl_Linux *control) {
    if (!control || !control->widget) return 0;
    return (int)gtk_range_get_value(GTK_RANGE(control->widget));
}

int gui_slider_set_range(GuiControl_Linux *control, int min_val, int max_val) {
    if (!control || !control->widget) return GUI_ERR_INVALID_PARAM;
    gtk_range_set_range(GTK_RANGE(control->widget), (double)min_val, (double)max_val);
    return GUI_OK;
}

int gui_slider_set_tick_freq(GuiControl_Linux *control, int freq) {
    if (!control || !control->widget) return GUI_ERR_INVALID_PARAM;
    /* GTK Scale使用draw-value和value-pos代替tick marks */
    return GUI_OK;
}

/* ============================================================
 * 扩展控件操作 - 多行文本框
 * ============================================================ */
static GtkTextView* gui_get_textview_inner(GuiControl_Linux *control) {
    if (!control || !control->widget) return NULL;
    return GTK_TEXT_VIEW(g_object_get_data(G_OBJECT(control->widget), "inner-textview"));
}

int gui_textbox_append_text(GuiControl_Linux *control, const char *text) {
    GtkTextView *tv = gui_get_textview_inner(control);
    if (!tv) return GUI_ERR_INVALID_PARAM;
    GtkTextBuffer *buf = gtk_text_view_get_buffer(tv);
    GtkTextIter end;
    gtk_text_buffer_get_end_iter(buf, &end);
    gtk_text_buffer_insert(buf, &end, text ? text : "", -1);
    return GUI_OK;
}

int gui_textbox_clear(GuiControl_Linux *control) {
    GtkTextView *tv = gui_get_textview_inner(control);
    if (!tv) return GUI_ERR_INVALID_PARAM;
    GtkTextBuffer *buf = gtk_text_view_get_buffer(tv);
    gtk_text_buffer_set_text(buf, "", -1);
    return GUI_OK;
}

int gui_textbox_get_line_count(GuiControl_Linux *control) {
    GtkTextView *tv = gui_get_textview_inner(control);
    if (!tv) return 0;
    GtkTextBuffer *buf = gtk_text_view_get_buffer(tv);
    return gtk_text_buffer_get_line_count(buf);
}

#endif /* GUI_PLATFORM_LINUX */
