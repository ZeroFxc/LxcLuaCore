#ifndef gui_core_h
#define gui_core_h

/**
 * @file gui_core.h
 * @brief LXCLUA GUI核心定义 - 跨平台GUI抽象层头文件
 * @author DiferLine
 * @version 1.0.0
 * 
 * 本文件定义了GUI系统的核心数据结构、常量和平台抽象接口。
 * 支持Windows(Win32)和Linux(GTK3)双平台原生GUI实现。
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ============================================================
 * 平台检测与条件包含
 * ============================================================ */
#if defined(_WIN32) || defined(_WIN64)
    #define GUI_PLATFORM_WINDOWS  1
    #define GUI_CURRENT_PLATFORM  "Windows"
    #include <windows.h>
    #include <commctrl.h>
    #include <windowsx.h>
#elif defined(__linux__)
    #define GUI_PLATFORM_LINUX    1
    #define GUI_CURRENT_PLATFORM  "Linux"
    #include <gtk/gtk.h>
#elif defined(__APPLE__)
    #define GUI_PLATFORM_MACOS    1
    #define GUI_CURRENT_PLATFORM  "macOS"
#else
    #error "Unsupported platform for LXCLUA GUI"
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================
 * 版本信息
 * ============================================================ */
#define GUI_VERSION_MAJOR    1
#define GUI_VERSION_MINOR    0
#define GUI_VERSION_PATCH    0
#define GUI_VERSION_STRING   "1.0.0"

/* ============================================================
 * 错误码定义（跨平台通用）
 * ============================================================ */
#define GUI_OK                  0       /**< 操作成功 */
#define GUI_ERR_UNKNOWN        -1      /**< 未知错误 */
#define GUI_ERR_INVALID_PARAM  -2      /**< 参数无效 */
#define GUI_ERR_MEMORY         -3      /**< 内存分配失败 */
#define GUI_ERR_WINDOW_CREATE  -4      /**< 窗口创建失败 */
#define GUI_ERR_CLASS_REGISTER -5      /**< 窗口类注册失败 */
#define GUI_ERR_NOT_INITIALIZED -6     /**< 系统未初始化 */

/* ============================================================
 * 窗口样式常量（跨平台抽象值）
 * ============================================================ */
#define GUI_WS_OVERLAPPED       0x00000001  /**< 标准重叠窗口（标题栏+边框+系统菜单+最小化框+可调大小） */
#define GUI_WS_POPUP            0x00000002  /**< 弹出式窗口（无标题栏） */
#define GUI_WS_CHILD            0x00000004  /**< 子窗口 */
#define GUI_WS_TOOL             0x00000008  /**< 工具窗口 */
#define GUI_WS_FULLSCREEN       0x00000010  /**< 全屏模式 */

/* ============================================================
 * 控件类型枚举（跨平台）
 * ============================================================ */
typedef enum {
    GUI_CTRL_NONE = 0,          /**< 无/未知控件 */
    GUI_CTRL_BUTTON,            /**< 按钮 */
    GUI_CTRL_LABEL,             /**< 标签（静态文本） */
    GUI_CTRL_EDIT,              /**< 文本输入框 */
    GUI_CTRL_TEXTBOX,           /**< 多行文本框 */
    GUI_CTRL_CHECKBOX,          /**< 复选框 */
    GUI_CTRL_RADIOBUTTON,       /**< 单选按钮 */
    GUI_CTRL_GROUPBOX,          /**< 分组框 */
    GUI_CTRL_LISTBOX,           /**< 列表框 */
    GUI_CTRL_COMBOBOX,          /**< 下拉组合框 */
    GUI_CTRL_PROGRESSBAR,       /**< 进度条 */
    GUI_CTRL_SLIDER,            /**< 滑块 */
    GUI_CTRL_TABCONTROL,        /**< 标签页控件 */
    GUI_CTRL_TREEVIEW,          /**< 树形视图 */
    GUI_CTRL_LISTVIEW,          /**< 列表视图 */
    GUI_CTRL_RICHEDIT,          /**< 富文本编辑器 */
    GUI_CTRL_STATUSBAR,         /**< 状态栏 */
    GUI_CTRL_TOOLBAR,           /**< 工具栏 */
    GUI_CTRL_MENU,              /**< 菜单 */
    GUI_CTRL_CUSTOM             /**< 自定义控件 */
} GuiControlType;

/* ============================================================
 * 对齐方式枚举
 * ============================================================ */
typedef enum {
    GUI_ALIGN_LEFT = 0,         /**< 左对齐 */
    GUI_ALIGN_CENTER,           /**< 居中对齐 */
    GUI_ALIGN_RIGHT             /**< 右对齐 */
} GuiAlignment;

/* ============================================================
 * 锚点布局枚举（用于响应式布局）
 * ============================================================ */
typedef enum {
    GUI_ANCHOR_NONE = 0,        /**< 无锚点 */
    GUI_ANCHOR_TOP = (1 << 0),  /**< 顶部锚点 */
    GUI_ANCHOR_BOTTOM = (1 << 1),/**< 底部锚点 */
    GUI_ANCHOR_LEFT = (1 << 2),  /**< 左侧锚点 */
    GUI_ANCHOR_RIGHT = (1 << 3)  /**< 右侧锚点 */
} GuiAnchor;

/* ============================================================
 * 事件类型枚举（跨平台）
 * ============================================================ */
typedef enum {
    GUI_EVENT_NONE = 0,         /**< 无事件 */
    GUI_EVENT_CREATE,           /**< 创建事件 */
    GUI_EVENT_DESTROY,          /**< 销毁事件 */
    GUI_EVENT_CLOSE,            /**< 关闭事件 */
    GUI_EVENT_SIZE,             /**< 尺寸改变事件 */
    GUI_EVENT_MOVE,             /**< 移动事件 */
    GUI_EVENT_PAINT,            /**< 绘制事件 */
    GUI_EVENT_CLICK,            /**< 点击事件 */
    GUI_EVENT_DBLCLICK,         /**< 双击事件 */
    GUI_EVENT_MOUSEDOWN,        /**< 鼠标按下事件 */
    GUI_EVENT_MOUSEUP,          /**< 鼠标释放事件 */
    GUI_EVENT_MOUSEMOVE,        /**< 鼠标移动事件 */
    GUI_EVENT_MOUSEENTER,       /**< 鼠标进入事件 */
    GUI_EVENT_MOUSELEAVE,       /**< 鼠标离开事件 */
    GUI_EVENT_KEYDOWN,          /**< 键盘按下事件 */
    GUI_EVENT_KEYUP,            /**< 键盘释放事件 */
    GUI_EVENT_CHAR,             /**< 字符输入事件 */
    GUI_EVENT_FOCUS,            /**< 获得焦点事件 */
    GUI_EVENT_KILLFOCUS,        /**< 失去焦点事件 */
    GUI_EVENT_CHANGE,           /**< 值改变事件 */
    GUI_EVENT_SELECT,           /**< 选择事件 */
    GUI_EVENT_SCROLL,           /**< 滚动事件 */
    GUI_EVENT_TIMER,            /**< 定时器事件 */
    GUI_EVENT_DROPFILES,        /**< 文件拖放事件 */
    GUI_EVENT_CUSTOM            /**< 自定义事件 */
} GuiEventType;

/* ============================================================
 * 颜色结构体（RGBA格式）
 * ============================================================ */
typedef struct GuiColor {
    unsigned char r;            /**< 红色分量 (0-255) */
    unsigned char g;            /**< 绿色分量 (0-255) */
    unsigned char b;            /**< 蓝色分量 (0-255) */
    unsigned char a;            /**< 透明度 (0-255, 255=不透明) */
} GuiColor;

/* ============================================================
 * 尺寸/位置结构体
 * ============================================================ */
typedef struct GuiRect {
    int x;                      /**< X坐标 */
    int y;                      /**< Y坐标 */
    int width;                  /**< 宽度 */
    int height;                 /**< 高度 */
} GuiRect;

/* ============================================================
 * 点结构体
 * ============================================================ */
typedef struct GuiPoint {
    int x;                      /**< X坐标 */
    int y;                      /**< Y坐标 */
} GuiPoint;

/* ============================================================
 * 事件数据联合体（用于传递事件相关信息）
 * ============================================================ */
typedef struct GuiEventData {
    GuiEventType type;          /**< 事件类型 */
    
    union {
        /* 尺寸数据（用于SIZE事件） */
        struct {
            int width;
            int height;
        } size;
        
        /* 位置数据（用于MOVE事件） */
        struct {
            int x;
            int y;
        } position;
        
        /* 鼠标数据（用于鼠标相关事件） */
        struct {
            int x;
            int y;
            unsigned int buttons;  /**< 按键状态 */
        } mouse;
        
        /* 键盘数据（用于键盘相关事件） */
        struct {
            unsigned int key;      /**< 虚拟键码 */
            unsigned int flags;    /**< 标志位 */
            int repeat_count;      /**< 重复次数 */
        } keyboard;
        
        /* 字符数据（用于CHAR事件） */
        struct {
            unsigned int ch;       /**< Unicode字符码 */
        } character;
        
        /* 定时器数据（用于TIMER事件） */
        struct {
            unsigned int id;       /**< 定时器ID */
        } timer;
        
        /* 自定义数据指针 */
        void *custom_data;
    };
} GuiEventData;

/* ============================================================
 * 事件回调函数类型定义
 * ============================================================ */
typedef void (*GuiEventCallback)(void *sender, GuiEventData *event_data, void *user_data);

/* ============================================================
 * 控件属性结构体
 * ============================================================ */
typedef struct GuiControlProps {
    GuiRect rect;                /**< 控件位置和尺寸 */
    const char *text;            /**< 控件文本 */
    GuiColor bgcolor;            /**< 背景颜色 */
    GuiColor fgcolor;            /**< 前景色（文字颜色） */
    GuiColor bordercolor;        /**< 边框颜色 */
    int visible;                 /**< 是否可见 (0/1) */
    int enabled;                 /**< 是否启用 (0/1) */
    int tab_stop;                /**< 是否可Tab聚焦 (0/1) */
    unsigned int anchor;         /**< 锚点布局标志 */
    void *font;                  /**< 字体句柄（平台相关） */
    int font_size;               /**< 字体大小 */
    const char *font_name;       /**< 字体名称 */
} GuiControlProps;

/* ============================================================
 * 窗口配置结构体
 * ============================================================ */
typedef struct GuiWindowConfig {
    const char *title;           /**< 窗口标题 */
    int x;                       /**< X坐标（使用-1表示默认） */
    int y;                       /**< Y坐标 */
    int width;                   /**< 宽度 */
    int height;                  /**< 高度 */
    unsigned int style;          /**< 窗口样式 */
    GuiColor bgcolor;            /**< 背景颜色 */
    int resizable;               /**< 可调整大小 (0/1) */
    int has_menu;                /**< 有菜单栏 (0/1) */
    int has_statusbar;           /**< 有状态栏 (0/1) */
    int center_screen;           /**< 居中显示 (0/1) */
    int topmost;                 /**< 置顶显示 (0/1) */
    void *icon;                  /**< 图标句柄（平台相关） */
    void *menu;                  /**< 菜单句柄（平台相关） */
} GuiWindowConfig;

/* ============================================================
 * 消息循环模式
 * ============================================================ */
typedef enum {
    GUI_LOOP_BLOCKING,           /**< 阻塞式消息循环（标准模式） */
    GUI_LOOP_NONBLOCKING,        /**< 非阻塞式消息循环（适用于游戏等） */
    GUI_LOOP_CUSTOM              /**< 自定义消息循环 */
} GuiLoopMode;

/* ============================================================
 * GUI系统状态
 * ============================================================ */
typedef enum {
    GUI_STATE_UNINITIALIZED = 0, /**< 未初始化 */
    GUI_STATE_INITIALIZED,       /**< 已初始化 */
    GUI_STATE_RUNNING,           /**< 运行中 */
    GUI_STATE_EXITING            /**< 正在退出 */
} GuiSystemState;

#ifdef __cplusplus
}
#endif

#endif /* gui_core_h */
