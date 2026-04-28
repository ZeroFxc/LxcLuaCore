#ifndef gui_linux_h
#define gui_linux_h

/**
 * @file gui_linux.h
 * @brief LXCLUA GUI Linux平台实现 - GTK3原生GUI接口
 * @author DiferLine
 * @version 1.0.0
 * 
 * 本文件提供Linux平台(GTK3)的GUI功能实现，包括窗口管理、控件创建、
 * 事件处理等核心功能的GTK3 API封装。
 */

#include "gui_core.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 仅在Linux平台编译 */
#if defined(GUI_PLATFORM_LINUX)

#include <gtk/gtk.h>

/* ============================================================
 * Linux特定常量定义
 * ============================================================ */
#define GUI_LINUX_MAX_CLASS_NAME  256     /**< 最大类名长度 */
#define GUI_LINUX_MAX_TITLE_LEN   1024    /**< 最大标题长度 */
#define GUI_LINUX_MAX_TEXT_LEN    65535   /**< 最大文本长度 */
#define GUI_LINUX_MAX_CONTROLS    65536   /**< 单窗口最大控件数 */
#define GUI_LINUX_MAX_WINDOWS     256     /**< 最大同时打开窗口数 */
#define GUI_LINUX_TIMER_RES       1       /**< 定时器精度（毫秒） */

/* ============================================================
 * 控件内部数据结构（Linux/GTK3平台）
 * ============================================================ */
typedef struct GuiControl_Linux {
    GtkWidget *widget;            /**< GTK控件指针 */
    GuiControlType type;          /**< 控件类型 */
    unsigned int id;              /**< 控件ID */
    GuiControlProps props;        /**< 控件属性 */
    GuiEventCallback on_click;    /**< 点击回调 */
    GuiEventCallback on_change;   /**< 值改变回调 */
    GuiEventCallback on_focus;    /**< 获得焦点回调 */
    void *user_data;              /**< 用户自定义数据（Lua回调ID） */
    struct GuiControl_Linux *next;/**< 链表下一节点 */
} GuiControl_Linux;

/* ============================================================
 * 窗口内部数据结构（Linux/GTK3平台）
 * ============================================================ */
typedef struct GuiWindow_Linux {
    GtkWidget *window;            /**< GtkWindow指针 */
    GtkWidget *fixed_container;   /**< GtkFixed容器（绝对定位） */
    GuiWindowConfig config;       /**< 窗口配置 */
    GuiControl_Linux *controls;   /**< 控件链表头 */
    int control_count;            /**< 控件数量 */
    unsigned int next_ctrl_id;    /**< 下一个可用控件ID */
    
    /* 事件回调（C函数指针） */
    GuiEventCallback on_create;
    GuiEventCallback on_destroy;
    GuiEventCallback on_close;
    GuiEventCallback on_size;
    GuiEventCallback on_move;
    GuiEventCallback on_paint;
    GuiEventCallback on_keydown;
    GuiEventCallback on_keyup;
    GuiEventCallback on_char;
    GuiEventCallback on_dropfiles;
    GuiEventCallback on_timer;
    
    /* Lua回调ID（每个事件类型独立存储） */
    int cb_id_create;
    int cb_id_destroy;
    int cb_id_close;
    int cb_id_size;
    int cb_id_move;
    int cb_id_paint;
    int cb_id_keydown;
    int cb_id_keyup;
    int cb_id_char;
    int cb_id_dropfiles;
    int cb_id_timer;
    
    void *user_data;
    int is_modal;
    int is_closing;
    struct GuiWindow_Linux *next;
} GuiWindow_Linux;

/* ============================================================
 * GUI系统全局状态结构体（Linux/GTK3平台）
 * ============================================================ */
typedef struct GuiSystemState_Linux {
    GuiSystemState state;         /**< 系统状态 */
    GuiWindow_Linux *windows;     /**< 窗口链表头 */
    int window_count;             /**< 窗口数量 */
    GuiLoopMode loop_mode;        /**< 消息循环模式 */
    int is_running;               /**< 消息循环是否运行中 */
    
    /* 字体缓存 */
    PangoFontDescription *default_font;
    PangoFontDescription *title_font;
    PangoFontDescription *mono_font;
    
    /* 主题相关 */
    int is_dark_mode;
    GuiColor theme_bg;
    GuiColor theme_fg;
    GuiColor theme_accent;
    
    /* CSS provider for styling */
    GtkCssProvider *css_provider;
} GuiSystemState_Linux;

/* ============================================================
 * 函数声明：系统初始化与清理
 * ============================================================ */

/**
 * @brief 初始化Linux GUI系统（GTK3）
 * 
 * 初始化GTK子系统，包括：
 * - 调用gtk_init初始化GTK库
 * - 设置默认字体和主题
 * - 初始化CSS样式提供器
 * 
 * @param argc 命令行参数个数（通常传NULL则自动处理）
 * @param argv 命令行参数数组
 * @return 成功返回GUI_OK，失败返回错误码
 */
int gui_init(int *argc, char ***argv);

/**
 * @brief 清理GUI系统并释放所有资源
 * 
 * 销毁所有窗口，释放字体和CSS资源，重置系统状态。
 * 
 * @return 成功返回GUI_OK，失败返回错误码
 */
int gui_cleanup(void);

/**
 * @brief 检查GUI系统是否已初始化
 * @return 已初始化返回1，否则返回0
 */
int gui_is_initialized(void);

/* ============================================================
 * 函数声明：窗口管理
 * ============================================================ */

/**
 * @brief 创建窗口
 * 
 * 根据配置创建GTK3窗口，使用GtkFixed容器支持绝对定位布局。
 * 
 * @param config 窗口配置（标题、尺寸、位置等）
 * @return 成功返回窗口指针，失败返回NULL
 */
GuiWindow_Linux* gui_create_window(GuiWindowConfig *config);

/**
 * @brief 销毁窗口并释放所有关联控件
 * @param window 窗口指针
 * @return 成功返回GUI_OK
 */
int gui_destroy_window(GuiWindow_Linux *window);

/**
 * @brief 显示窗口
 * @param window 窗口指针
 * @return 成功返回GUI_OK
 */
int gui_show_window(GuiWindow_Linux *window);

/**
 * @brief 隐藏窗口
 * @param window 窗口指针
 * @return 成功返回GUI_OK
 */
int gui_hide_window(GuiWindow_Linux *window);

/**
 * @brief 设置窗口标题
 * @param window 窗口指针
 * @param title 新标题
 * @return 成功返回GUI_OK
 */
int gui_set_window_title(GuiWindow_Linux *window, const char *title);

/**
 * @brief 获取窗口标题
 * @param window 窗口指针
 * @return 标题字符串（内部缓冲区）
 */
const char* gui_get_window_title(GuiWindow_Linux *window);

/**
 * @brief 设置窗口位置和尺寸
 * @param window 窗口指针
 * @param rect 新的位置和尺寸
 * @return 成功返回GUI_OK
 */
int gui_set_window_rect(GuiWindow_Linux *window, GuiRect *rect);

/**
 * @brief 获取窗口位置和尺寸
 * @param window 窗口指针
 * @return 位置和尺寸
 */
GuiRect gui_get_window_rect(GuiWindow_Linux *window);

/**
 * @brief 设置窗口透明度
 * @param window 窗口指针
 * @param opacity 透明度(0.0-1.0)
 * @return 成功返回GUI_OK
 */
int gui_set_window_opacity(GuiWindow_Linux *window, double opacity);

/**
 * @brief 将窗口居中显示
 * @param window 窗口指针
 * @return 成功返回GUI_OK
 */
int gui_center_window(GuiWindow_Linux *window);

/**
 * @brief 设置窗口启用/禁用状态
 * @param window 窗口指针
 * @param enabled 1=启用, 0=禁用
 * @return 成功返回GUI_OK
 */
int gui_enable_window(GuiWindow_Linux *window, int enabled);

/**
 * @brief 设置窗口背景色
 * @param window 窗口指针
 * @param color 颜色
 * @return 成功返回GUI_OK
 */
int gui_set_window_bgcolor(GuiWindow_Linux *window, GuiColor color);

/**
 * @brief 使窗口闪烁提醒用户注意
 * @param window 窗口指针
 * @param count 闪烁次数
 * @return 成功返回GUI_OK
 */
int gui_flash_window(GuiWindow_Linux *window, unsigned int count);

/* ============================================================
 * 函数声明：控件管理
 * ============================================================ */

/**
 * @brief 在指定窗口中创建控件
 * 
 * 根据控件类型创建对应的GTK3 Widget，添加到窗口的GtkFixed容器中。
 * 支持的类型：按钮、标签、输入框、多行文本框、复选框、单选按钮、
 * 分组框、列表框、下拉框、进度条、滑块、标签页等。
 * 
 * @param window 父窗口指针
 * @param type 控件类型
 * @param props 控件属性（位置、尺寸、文本、颜色等）
 * @return 成功返回控件指针，失败返回NULL
 */
GuiControl_Linux* gui_create_control(GuiWindow_Linux *window,
                                      GuiControlType type,
                                      GuiControlProps *props);

/**
 * @brief 设置控件文本
 * @param control 控件指针
 * @param text 文本内容
 * @return 成功返回GUI_OK
 */
int gui_set_control_text(GuiControl_Linux *control, const char *text);

/**
 * @brief 获取控件文本
 * @param control 控件指针
 * @return 文本字符串（内部缓冲区）
 */
const char* gui_get_control_text(GuiControl_Linux *control);

/**
 * @brief 设置控件位置和尺寸
 * @param control 控件指针
 * @param rect 新的位置和尺寸
 * @return 成功返回GUI_OK
 */
int gui_set_control_rect(GuiControl_Linux *control, GuiRect *rect);

/**
 * @brief 获取控件位置和尺寸
 * @param control 控件指针
 * @return 位置和尺寸
 */
GuiRect gui_get_control_rect(GuiControl_Linux *control);

/**
 * @brief 设置控件可见性
 * @param control 控件指针
 * @param visible 1=可见, 0=隐藏
 * @return 成功返回GUI_OK
 */
int gui_set_control_visible(GuiControl_Linux *control, int visible);

/**
 * @brief 设置控件启用/禁用状态
 * @param control 控件指针
 * @param enabled 1=启用, 0=禁用
 * @return 成功返回GUI_OK
 */
int gui_set_control_enabled(GuiControl_Linux *control, int enabled);

/**
 * @brief 让控件获得焦点
 * @param control 控件指针
 * @return 成功返回GUI_OK
 */
int gui_set_control_focus(GuiControl_Linux *control);

/**
 * @brief 将控件置于最前（Z-order置顶）
 * @param control 控件指针
 * @return 成功返回GUI_OK
 */
int gui_control_to_front(GuiControl_Linux *control);

/**
 * @brief 通过GtkWidget查找对应的GuiControl_Linux
 * @param window 父窗口
 * @param widget GTK widget指针
 * @return 找到返回控件指针，未找到返回NULL
 */
GuiControl_Linux* gui_find_control_by_widget(GuiWindow_Linux *window, GtkWidget *widget);

/* ============================================================
 * 函数声明：事件回调注册
 * ============================================================ */

/**
 * @brief 注册窗口事件回调
 * @param window 窗口指针
 * @param event_type 事件类型
 * @param callback 回调函数
 * @return 成功返回GUI_OK
 */
int gui_register_event_callback(GuiWindow_Linux *window,
                                GuiEventType event_type,
                                GuiEventCallback callback);

/**
 * @brief 设置窗口事件的Lua回调ID（独立存储）
 * @param window 窗口指针
 * @param event_type 事件类型
 * @param cb_id Lua回调ID
 * @return 成功返回GUI_OK
 */
int gui_set_event_callback_id(GuiWindow_Linux *window,
                              GuiEventType event_type,
                              int cb_id);

/**
 * @brief 注册控件事件回调
 * @param control 控件指针
 * @param event_type 事件类型
 * @param callback 回调函数
 * @return 成功返回GUI_OK
 */
int gui_register_control_callback(GuiControl_Linux *control,
                                  GuiEventType event_type,
                                  GuiEventCallback callback);

/* ============================================================
 * 函数声明：消息循环
 * ============================================================ */

/**
 * @brief 进入GTK主消息循环（阻塞式）
 * @return 退出代码
 */
int gui_run_main_loop(void);

/**
 * @brief 处理一次GTK消息（非阻塞式）
 * @return 处理了消息返回1，无消息返回0
 */
int gui_process_events(void);

/**
 * @brief 退出GTK主消息循环
 * @param exit_code 退出代码
 * @return 成功返回GUI_OK
 */
int gui_exit_main_loop(int exit_code);

/**
 * @brief 设置消息循环模式
 * @param mode 循环模式
 * @return 成功返回GUI_OK
 */
int gui_set_loop_mode(GuiLoopMode mode);

/* ============================================================
 * 函数声明：对话框
 * ============================================================ */

/**
 * @brief 显示消息对话框
 * @param message 消息内容
 * @param title 对话框标题
 * @param type_str 类型字符串（"ok","okcancel","yesno","yesnocancel",
 *                   "retrycancel","abortretryignore"等，可组合"|icon_error"
 *                   "|icon_warning"|icon_question"|icon_info"）
 * @return 点击的按钮ID（与Windows MessageBox兼容值）
 */
int gui_message_box(const char *message, const char *title, const char *type_str);

/* ============================================================
 * 函数声明：扩展控件操作
 * ============================================================ */

/** 列表框操作 */
int gui_listbox_add_item(GuiControl_Linux *control, const char *text);
int gui_listbox_insert_item(GuiControl_Linux *control, int index, const char *text);
int gui_listbox_remove_item(GuiControl_Linux *control, int index);
int gui_listbox_clear(GuiControl_Linux *control);
int gui_listbox_get_selected_index(GuiControl_Linux *control);
int gui_listbox_set_selected_index(GuiControl_Linux *control, int index);
int gui_listbox_get_count(GuiControl_Linux *control);
const char* gui_listbox_get_item_text(GuiControl_Linux *control, int index);

/** 下拉框操作 */
int gui_combobox_add_item(GuiControl_Linux *control, const char *text);
int gui_combobox_insert_item(GuiControl_Linux *control, int index, const char *text);
int gui_combobox_remove_item(GuiControl_Linux *control, int index);
int gui_combobox_clear(GuiControl_Linux *control);
int gui_combobox_get_selected_index(GuiControl_Linux *control);
int gui_combobox_set_selected_index(GuiControl_Linux *control, int index);
int gui_combobox_get_count(GuiControl_Linux *control);
const char* gui_combobox_get_item_text(GuiControl_Linux *control, int index);

/** 复选框/单选按钮操作 */
int gui_checkbox_get_checked(GuiControl_Linux *control);
int gui_checkbox_set_checked(GuiControl_Linux *control, int checked);
int gui_radio_get_checked(GuiControl_Linux *control);
int gui_radio_set_checked(GuiControl_Linux *control, int checked);

/** 进度条操作 */
int gui_progressbar_set_value(GuiControl_Linux *control, int value);
int gui_progressbar_get_value(GuiControl_Linux *control);
int gui_progressbar_set_range(GuiControl_Linux *control, int min_val, int max_val);

/** 滑块操作 */
int gui_slider_set_value(GuiControl_Linux *control, int value);
int gui_slider_get_value(GuiControl_Linux *control);
int gui_slider_set_range(GuiControl_Linux *control, int min_val, int max_val);
int gui_slider_set_tick_freq(GuiControl_Linux *control, int freq);

/** 多行文本框操作 */
int gui_textbox_append_text(GuiControl_Linux *control, const char *text);
int gui_textbox_clear(GuiControl_Linux *control);
int gui_textbox_get_line_count(GuiControl_Linux *control);

/* ============================================================
 * 全局变量与辅助函数
 * ============================================================ */

/** 全局GUI状态 */
extern GuiSystemState_Linux g_gui_state_linux;

/** 最后的错误信息 */
extern char g_last_error_linux[1024];

/** 设置最后错误信息 */
void gui_set_error_linux(const char *fmt, ...);

/** 获取最后错误信息 */
const char* gui_get_last_error(void);

/** 事件分发函数（由GTK信号处理器调用） */
void gui_dispatch_event(GuiWindow_Linux *window, GuiEventType type, GuiEventData *data);

/** UTF-8工具函数（Linux原生UTF-8，直接传递） */
const char* gui_to_platform_string(const char *utf8_str);
char* gui_from_platform_string(const char *platform_str);

#endif /* GUI_PLATFORM_LINUX */

#ifdef __cplusplus
}
#endif

#endif /* gui_linux_h */
