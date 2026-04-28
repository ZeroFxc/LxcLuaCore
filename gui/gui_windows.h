#ifndef gui_windows_h
#define gui_windows_h

/**
 * @file gui_windows.h
 * @brief LXCLUA GUI Windows平台实现 - Windows原生GUI接口
 * @author DiferLine
 * @version 1.0.0
 * 
 * 本文件提供Windows平台的GUI功能实现，包括窗口管理、控件创建、
 * 事件处理等核心功能的Windows API封装。
 */

#include "gui_core.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 仅在Windows平台编译 */
#if defined(GUI_PLATFORM_WINDOWS)

#include <windows.h>
#include <commctrl.h>

/* ============================================================
 * Windows特定常量定义
 * ============================================================ */
#define GUI_MAX_CLASS_NAME       256     /**< 最大窗口类名长度 */
#define GUI_MAX_TITLE_LENGTH     1024    /**< 最大标题长度 */
#define GUI_MAX_TEXT_LENGTH      65535   /**< 最大文本长度 */
#define GUI_MAX_CONTROLS         65536   /**< 单窗口最大控件数 */
#define GUI_MAX_WINDOWS          256     /**< 最大同时打开窗口数 */
#define GUI_TIMER_RESOLUTION     1       /**< 定时器精度（毫秒） */

/* ============================================================
 * 控件内部数据结构（Windows平台）
 * ============================================================ */
typedef struct GuiControl_Win {
    HWND hwnd;                   /**< 控件窗口句柄 */
    GuiControlType type;         /**< 控件类型 */
    unsigned int id;             /**< 控件ID */
    GuiControlProps props;       /**< 控件属性 */
    GuiEventCallback on_click;   /**< 点击回调 */
    GuiEventCallback on_change;  /**< 值改变回调 */
    GuiEventCallback on_focus;   /**< 获得焦点回调 */
    void *user_data;             /**< 用户自定义数据 */
    struct GuiControl_Win *next; /**< 链表下一节点 */
} GuiControl_Win;

/* ============================================================
 * 窗口内部数据结构（Windows平台）
 * ============================================================ */
typedef struct GuiWindow_Win {
    HWND hwnd;                   /**< 窗口句柄 */
    WNDPROC orig_wndproc;        /**< 原始窗口过程（用于子类化） */
    GuiWindowConfig config;      /**< 窗口配置 */
    GuiControl_Win *controls;    /**< 控件链表头 */
    int control_count;           /**< 控件数量 */
    unsigned int next_ctrl_id;   /**< 下一个可用控件ID */
    
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
    
    /* Lua回调ID（每个事件类型独立存储，避免覆盖） */
    int cb_id_create;            /**< 创建事件Lua回调ID */
    int cb_id_destroy;           /**< 销毁事件Lua回调ID */
    int cb_id_close;             /**< 关闭事件Lua回调ID */
    int cb_id_size;               /**< 尺寸改变事件Lua回调ID */
    int cb_id_move;               /**< 移动事件Lua回调ID */
    int cb_id_paint;              /**< 绘制事件Lua回调ID */
    int cb_id_keydown;            /**< 键盘按下事件Lua回调ID */
    int cb_id_keyup;              /**< 键盘释放事件Lua回调ID */
    int cb_id_char;               /**< 字符输入事件Lua回调ID */
    int cb_id_dropfiles;          /**< 文件拖放事件Lua回调ID */
    int cb_id_timer;              /**< 定时器事件Lua回调ID */
    
    void *user_data;             /**< 用户数据 */
    int is_modal;                /**< 是否模态窗口 */
    int is_closing;              /**< 是否正在关闭 */
    struct GuiWindow_Win *next;  /**< 链表下一节点 */
} GuiWindow_Win;

/* ============================================================
 * GUI系统全局状态结构体（Windows平台）
 * ============================================================ */
typedef struct GuiSystemState_Win {
    HINSTANCE hinstance;         /**< 应用程序实例句柄 */
    GuiSystemState state;        /**< 系统状态 */
    GuiWindow_Win *windows;      /**< 窗口链表头 */
    int window_count;            /**< 窗口数量 */
    GuiLoopMode loop_mode;       /**< 消息循环模式 */
    int is_running;              /**< 消息循环是否运行中 */
    CRITICAL_SECTION cs;         /**< 临界区（线程安全） */
    HWND default_parent;         /**< 默认父窗口 */
    
    /* 字体缓存 */
    HFONT default_font;          /**< 默认字体 */
    HFONT title_font;            /**< 标题字体 */
    HFONT mono_font;             /**< 等宽字体 */
    
    /* 主题相关 */
    int is_dark_mode;            /**< 是否暗色模式 */
    GuiColor theme_bg;           /**< 主题背景色 */
    GuiColor theme_fg;           /**< 主题前景色 */
    GuiColor theme_accent;       /**< 主题强调色 */
} GuiSystemState_Win;

/* ============================================================
 * 函数声明：系统初始化与清理
 * ============================================================ */

/**
 * @brief 初始化GUI系统
 * 
 * 初始化Windows GUI子系统，包括：
 * - 初始化通用控件库（Common Controls）
 * - 创建临界区保证线程安全
 * - 设置默认字体和主题
 * 
 * @param hinstance 应用程序实例句柄（通常为GetModuleHandle(NULL)返回值）
 * @return 成功返回GUI_OK，失败返回错误码
 * @note 必须在使用任何其他GUI函数之前调用
 */
int gui_init(HINSTANCE hinstance);

/**
 * @brief 清理GUI系统并释放所有资源
 * 
 * 执行以下操作：
 * - 销毁所有已创建的窗口
 * - 释放所有GDI对象（字体、画笔等）
 * - 删除临界区
 * - 重置系统状态
 * 
 * @return 成功返回GUI_OK，失败返回错误码
 */
int gui_cleanup(void);

/**
 * @brief 获取GUI系统当前状态
 * 
 * @return 当前系统状态枚举值
 */
GuiSystemState gui_get_state(void);

/**
 * @brief 检查GUI系统是否已初始化
 * 
 * @return 1表示已初始化，0表示未初始化
 */
int gui_is_initialized(void);

/* ============================================================
 * 函数声明：窗口管理
 * ============================================================ */

/**
 * @brief 创建新窗口
 * 
 * 根据配置创建一个新的顶级窗口或子窗口。
 * 
 * @param config 窗口配置结构体指针
 * @param parent 父窗口句柄（NULL表示顶级窗口）
 * @return 成功返回窗口句柄指针，失败返回NULL
 * @note 调用者负责在不需要时使用gui_destroy_window销毁窗口
 */
GuiWindow_Win* gui_create_window(const GuiWindowConfig *config, HWND parent);

/**
 * @brief 销毁指定窗口及其所有子控件
 * 
 * @param window 要销毁的窗口指针
 * @return 成功返回GUI_OK，失败返回错误码
 */
int gui_destroy_window(GuiWindow_Win *window);

/**
 * @brief 显示窗口
 * 
 * @param window 窗口指针
 * @param cmd_show 显示命令（如SW_SHOW, SW_HIDE等）
 * @return 成功返回GUI_OK，失败返回错误码
 */
int gui_show_window(GuiWindow_Win *window, int cmd_show);

/**
 * @brief 隐藏窗口
 * 
 * @param window 窗口指针
 * @return 成功返回GUI_OK，失败返回错误码
 */
int gui_hide_window(GuiWindow_Win *window);

/**
 * @brief 将窗口置于前台并激活
 * 
 * @param window 窗口指针
 * @return 成功返回GUI_OK，失败返回错误码
 */
int gui_bring_to_front(GuiWindow_Win *window);

/**
 * @brief 设置窗口标题
 * 
 * @param window 窗口指针
 * @param title 新标题字符串
 * @return 成功返回GUI_OK，失败返回错误码
 */
int gui_set_window_title(GuiWindow_Win *window, const char *title);

/**
 * @brief 获取窗口标题
 * 
 * @param window 窗口指针
 * @return 标题字符串指针（不要free，由系统管理）
 */
const char* gui_get_window_title(GuiWindow_Win *window);

/**
 * @brief 设置窗口位置和大小
 * 
 * @param window 窗口指针
 * @param rect 新的位置和尺寸
 * @return 成功返回GUI_OK，失败返回错误码
 */
int gui_set_window_rect(GuiWindow_Win *window, const GuiRect *rect);

/**
 * @brief 获取窗口位置和大小
 * 
 * @param window 窗口指针
 * @param rect 输出参数，用于接收位置和尺寸
 * @return 成功返回GUI_OK，失败返回错误码
 */
int gui_get_window_rect(GuiWindow_Win *window, GuiRect *rect);

/**
 * @brief 设置窗口不透明度
 * 
 * @param window 窗口指针
 * @param opacity 不透明度（0-255，255=完全不透明）
 * @return 成功返回GUI_OK，失败返回错误码
 */
int gui_set_window_opacity(GuiWindow_Win *window, unsigned char opacity);

/**
 * @brief 居中显示窗口（相对于屏幕或父窗口）
 * 
 * @param window 窗口指针
 * @param relative_to_parent 是否相对于父窗口居中（0=屏幕，1=父窗口）
 * @return 成功返回GUI_OK，失败返回错误码
 */
int gui_center_window(GuiWindow_Win *window, int relative_to_parent);

/**
 * @brief 启用/禁用窗口
 * 
 * @param window 窗口指针
 * @param enabled 1=启用，0=禁用
 * @return 成功返回GUI_OK，失败返回错误码
 */
int gui_enable_window(GuiWindow_Win *window, int enabled);

/**
 * @brief 设置窗口为模态对话框
 * 
 * @param window 窗口指针
 * @param modal 1=模态，0=非模态
 * @return 成功返回GUI_OK，失败返回错误码
 */
int gui_set_modal(GuiWindow_Win *window, int modal);

/**
 * @brief 闪烁窗口任务栏图标（提醒用户）
 * 
 * @param window 窗口指针
 * @param count 闪烁次数
 * @return 成功返回GUI_OK，失败返回错误码
 */
int gui_flash_window(GuiWindow_Win *window, unsigned int count);

/* ============================================================
 * 函数声明：控件创建与管理
 * ============================================================ */

/**
 * @brief 在窗口中创建控件
 * 
 * 根据指定的类型和属性创建一个子控件。
 * 
 * @param window 父窗口指针
 * @param type 控件类型
 * @param props 控件属性
 * @return 成功返回控件指针，失败返回NULL
 */
GuiControl_Win* gui_create_control(GuiWindow_Win *window, 
                                   GuiControlType type,
                                   const GuiControlProps *props);

/**
 * @brief 通过ID查找控件
 * 
 * @param window 父窗口指针
 * @param id 控件ID
 * @return 找到返回控件指针，未找到返回NULL
 */
GuiControl_Win* gui_find_control_by_id(GuiWindow_Win *window, unsigned int id);

/**
 * @brief 通过句柄查找控件
 * 
 * @param window 父窗口指针
 * @param hwnd 控件窗口句柄
 * @return 找到返回控件指针，未找到返回NULL
 */
GuiControl_Win* gui_find_control_by_hwnd(GuiWindow_Win *window, HWND hwnd);

/**
 * @brief 销毁指定控件
 * 
 * @param control 要销毁的控件指针
 * @return 成功返回GUI_OK，失败返回错误码
 */
int gui_destroy_control(GuiControl_Win *control);

/**
 * @brief 设置控件文本
 * 
 * @param control 控件指针
 * @param text 新文本内容
 * @return 成功返回GUI_OK，失败返回错误码
 */
int gui_set_control_text(GuiControl_Win *control, const char *text);

/**
 * @brief 获取控件文本
 * 
 * @param control 控件指针
 * @return 文本字符串指针（不要free，由系统管理）
 */
const char* gui_get_control_text(GuiControl_Win *control);

/**
 * @brief 设置控件位置和大小
 * 
 * @param control 控件指针
 * @param rect 新位置和尺寸
 * @return 成功返回GUI_OK，失败返回错误码
 */
int gui_set_control_rect(GuiControl_Win *control, const GuiRect *rect);

/**
 * @brief 获取控件位置和大小
 * 
 * @param control 控件指针
 * @param rect 输出参数
 * @return 成功返回GUI_OK，失败返回错误码
 */
int gui_get_control_rect(GuiControl_Win *control, GuiRect *rect);

/**
 * @brief 显示/隐藏控件
 * 
 * @param control 控件指针
 * @param visible 1=可见，0=隐藏
 * @return 成功返回GUI_OK，失败返回错误码
 */
int gui_set_control_visible(GuiControl_Win *control, int visible);

/**
 * @brief 启用/禁用控件
 * 
 * @param control 控件指针
 * @param enabled 1=启用，0=禁用
 * @return 成功返回GUI_OK，失败返回错误码
 */
int gui_set_control_enabled(GuiControl_Win *control, int enabled);

/**
 * @brief 设置控件焦点
 * 
 * @param control 控件指针
 * @return 成功返回GUI_OK，失败返回错误码
 */
int gui_set_focus(GuiControl_Win *control);

/**
 * @brief 将控件置于Z序顶部
 * 
 * @param control 控件指针
 * @return 成功返回GUI_OK，失败返回错误码
 */
int gui_bring_control_to_front(GuiControl_Win *control);

/* ============================================================
 * 函数声明：消息循环与事件处理
 * ============================================================ */

/**
 * @brief 进入主消息循环（阻塞模式）
 * 
 * 此函数会阻塞直到所有窗口关闭或调用gui_exit_loop。
 * 
 * @return 退出代码
 */
int gui_run_main_loop(void);

/**
 * @brief 处理一条消息（非阻塞模式）
 * 
 * 在非阻塞模式下，需要在一帧循环中反复调用此函数。
 * 
 * @return 1表示还有消息待处理，0表示无消息
 */
int gui_process_messages(void);

/**
 * @brief 退出消息循环
 * 
 * 导致gui_run_main_loop返回。
 * 
 * @param exit_code 退出代码
 * @return 成功返回GUI_OK，失败返回错误码
 */
int gui_exit_loop(int exit_code);

/**
 * @brief 设置消息循环模式
 * 
 * @param mode 循环模式
 * @return 成功返回GUI_OK，失败返回错误码
 */
int gui_set_loop_mode(GuiLoopMode mode);

/**
 * @brief 注册窗口事件回调
 * 
 * @param window 窗口指针
 * @param event_type 事件类型
 * @param callback 回调函数
 * @return 成功返回GUI_OK，失败返回错误码
 */
int gui_register_event_callback(GuiWindow_Win *window,
                                GuiEventType event_type,
                                GuiEventCallback callback);

/**
 * @brief 设置窗口事件的Lua回调ID（独立存储，避免覆盖）
 *
 * @param window 窗口指针
 * @param event_type 事件类型
 * @param cb_id Lua回调ID
 * @return 成功返回GUI_OK，失败返回错误码
 */
int gui_set_event_callback_id(GuiWindow_Win *window,
                              GuiEventType event_type,
                              int cb_id);

/**
 * @brief 注册控件事件回调
 * 
 * @param control 控件指针
 * @param event_type 事件类型
 * @param callback 回调函数
 * @return 成功返回GUI_OK，失败返回错误码
 */
int gui_register_control_callback(GuiControl_Win *control,
                                  GuiEventType event_type,
                                  GuiEventCallback callback);

/* ============================================================
 * 函数声明：对话框
 * ============================================================ */

/**
 * @brief 显示消息框
 * 
 * @param parent 父窗口（可为NULL）
 * @param message 消息文本
 * @param title 标题
 * @param type 消息框类型（MB_OK, MB_YESNO等）
 * @return 用户点击的按钮ID
 */
int gui_message_box(GuiWindow_Win *parent, 
                    const char *message, 
                    const char *title,
                    unsigned int type);

/**
 * @brief 显示文件选择对话框
 * 
 * @param parent 父窗口（可为NULL）
 * @param title 对话框标题
 * @param filter 文件过滤器（如"Text Files\0*.txt\0All Files\0*.*\0"）
 * @param save_mode 1=保存模式，0=打开模式
 * @param multi_select 1=允许多选，0=单选
 * @return 选中的文件路径（多选用分号分隔），需用gui_free_string释放
 */
char* gui_file_dialog(GuiWindow_Win *parent,
                      const char *title,
                      const char *filter,
                      int save_mode,
                      int multi_select);

/**
 * @brief 显示文件夹选择对话框
 * 
 * @param parent 父窗口（可为NULL）
 * @param title 对话框标题
 * @return 选中的文件夹路径，需用gui_free_string释放
 */
char* gui_folder_dialog(GuiWindow_Win *parent, const char *title);

/**
 * @brief 显示颜色选择对话框
 * 
 * @param parent 父窗口（可为NULL）
 * @param initial_color 初始颜色
 * @param result_color 输出参数，用户选择的颜色
 * @return 1=确定，0=取消
 */
int gui_color_dialog(GuiWindow_Win *parent,
                     GuiColor initial_color,
                     GuiColor *result_color);

/**
 * @brief 显示字体选择对话框
 * 
 * @param parent 父窗口（可为NULL）
 * @param font_name 输入输出参数，字体名称缓冲区（至少LF_FACESIZE字节）
 * @param font_size 输入输出参数，字体大小
 * @return 1=确定，0=取消
 */
int gui_font_dialog(GuiWindow_Win *parent,
                    char *font_name,
                    int *font_size);

/* ============================================================
 * 函数声明：定时器
 * ============================================================ */

/**
 * @brief 创建定时器
 * 
 * @param window 所属窗口
 * @param interval 间隔时间（毫秒）
 * @param callback 到期回调
 * @return 成功返回定时器ID（>0），失败返回0
 */
unsigned int gui_create_timer(GuiWindow_Win *window,
                              unsigned int interval,
                              GuiEventCallback callback);

/**
 * @brief 销毁定时器
 * 
 * @param window 所属窗口
 * @param timer_id 定时器ID
 * @return 成功返回GUI_OK，失败返回错误码
 */
int gui_destroy_timer(GuiWindow_Win *window, unsigned int timer_id);

/* ============================================================
 * 函数声明：绘图与图形
 * ============================================================ */

/**
 * @brief 强制重绘窗口或控件
 * 
 * @param handle 可以是窗口指针或控件指针（转换为void*）
 * @return 成功返回GUI_OK，失败返回错误码
 */
int gui_invalidate(void *handle);

/**
 * @brief 设置窗口或控件的背景颜色
 * 
 * @param handle 窗口或控件指针
 * @param color 新背景色
 * @return 成功返回GUI_OK，失败返回错误码
 */
int gui_set_bgcolor(void *handle, GuiColor color);

/**
 * @brief 设置窗口或控件的前景色（文字颜色）
 * 
 * @param handle 窗口或控件指针
 * @param color 新前景色
 * @return 成功返回GUI_OK，失败返回错误码
 */
int gui_set_fgcolor(void *handle, GuiColor color);

/* ============================================================
 * 函数声明：剪贴板操作
 * ============================================================ */

/**
 * @brief 将文本复制到剪贴板
 * 
 * @param text 要复制的文本
 * @return 成功返回GUI_OK，失败返回错误码
 */
int gui_clipboard_copy(const char *text);

/**
 * @brief 从剪贴板粘贴文本
 * 
 * @return 剪贴板中的文本（需用gui_free_string释放），失败返回NULL
 */
char* gui_clipboard_paste(void);

/* ============================================================
 * 函数声明：实用工具函数
 * ============================================================ */

/**
 * @brief 分配字符串内存（用于返回给Lua的字符串）
 * 
 * @param str 要复制的字符串
 * @return 新分配的字符串副本
 * @note 使用gui_free_string释放
 */
char* gui_alloc_string(const char *str);

/**
 * @brief 释放通过gui_alloc_string等函数分配的字符串
 * 
 * @param str 要释放的字符串
 */
void gui_free_string(char *str);

/**
 * @brief 获取最后一个错误信息
 * 
 * @return 错误信息字符串
 */
const char* gui_get_last_error(void);

/**
 * @brief 将GuiColor转换为COLORREF（Windows格式）
 * 
 * @param color GUI颜色结构
 * @return COLORREF值
 */
COLORREF gui_color_to_ref(GuiColor color);

/**
 * @brief 将COLORREF转换为GuiColor
 * 
 * @param ref COLORREF值
 * @return GUI颜色结构
 */
GuiColor gui_ref_to_color(COLORREF ref);

/**
 * @brief 获取系统默认GUI状态指针（供内部使用）
 * 
 * @return 系统状态指针
 */
GuiSystemState_Win* gui_get_system_state(void);

/**
 * @brief 设置最后一个错误信息（供内部跨文件使用）
 * @param fmt 格式化字符串
 * @param ... 可变参数
 * @return 错误信息字符串指针
 */
const char* gui_set_error(const char *fmt, ...);

/**
 * @brief GUI系统全局状态变量（供内部跨文件使用）
 */
extern GuiSystemState_Win g_gui_state;

/* ============================================================
 * 列表框(ListBox) 扩展API
 * ============================================================ */
int gui_listbox_add_item(GuiControl_Win *control, const char *text);
int gui_listbox_insert_item(GuiControl_Win *control, int index, const char *text);
int gui_listbox_remove_item(GuiControl_Win *control, int index);
int gui_listbox_clear(GuiControl_Win *control);
int gui_listbox_get_selected_index(GuiControl_Win *control);
int gui_listbox_set_selected_index(GuiControl_Win *control, int index);
int gui_listbox_get_count(GuiControl_Win *control);
const char* gui_listbox_get_item_text(GuiControl_Win *control, int index);
int gui_listbox_find_item(GuiControl_Win *control, const char *text);

/* ============================================================
 * 下拉框(ComboBox) 扩展API
 * ============================================================ */
int gui_combobox_add_item(GuiControl_Win *control, const char *text);
int gui_combobox_insert_item(GuiControl_Win *control, int index, const char *text);
int gui_combobox_remove_item(GuiControl_Win *control, int index);
int gui_combobox_clear(GuiControl_Win *control);
int gui_combobox_get_selected_index(GuiControl_Win *control);
int gui_combobox_set_selected_index(GuiControl_Win *control, int index);
int gui_combobox_get_count(GuiControl_Win *control);
const char* gui_combobox_get_item_text(GuiControl_Win *control, int index);
int gui_combobox_set_edit_text(GuiControl_Win *control, const char *text);

/* ============================================================
 * 复选框(CheckBox) API
 * ============================================================ */
int gui_checkbox_get_checked(GuiControl_Win *control);
int gui_checkbox_set_checked(GuiControl_Win *control, int checked);

/* ============================================================
 * 单选按钮(RadioButton) API
 * ============================================================ */
int gui_radio_get_checked(GuiControl_Win *control);
int gui_radio_set_checked(GuiControl_Win *control, int checked);

/* ============================================================
 * 进度条(ProgressBar) API
 * ============================================================ */
int gui_progressbar_set_value(GuiControl_Win *control, int value);
int gui_progressbar_get_value(GuiControl_Win *control);
int gui_progressbar_set_range(GuiControl_Win *control, int min_value, int max_value);

/* ============================================================
 * 滑块(Slider/TrackBar) API
 * ============================================================ */
int gui_slider_set_value(GuiControl_Win *control, int value);
int gui_slider_get_value(GuiControl_Win *control);
int gui_slider_set_range(GuiControl_Win *control, int min_value, int max_value);
int gui_slider_set_tick_freq(GuiControl_Win *control, int freq);

/* ============================================================
 * 多行文本框(TextBox) API
 * ============================================================ */
int gui_textbox_append_text(GuiControl_Win *control, const char *text);
int gui_textbox_clear(GuiControl_Win *control);
int gui_textbox_get_line_count(GuiControl_Win *control);

/* ============================================================
 * UTF-8/宽字符 转换辅助函数（供内部跨文件使用）
 * ============================================================ */
void gui_utf8_to_wide(const char *src, wchar_t *dst, int dst_count);
void gui_wide_to_utf8(const wchar_t *src, char *dst, int dst_count);

#endif /* GUI_PLATFORM_WINDOWS */

#ifdef __cplusplus
}
#endif

#endif /* gui_windows_h */
