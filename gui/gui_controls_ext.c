/**
 * @file gui_controls_ext.c
 * @brief LXCLUA GUI 控件扩展功能 - 数据操作与状态管理
 * @author DiferLine
 * @version 1.0.0
 */

#include "gui_core.h"

#if defined(GUI_PLATFORM_WINDOWS)
    #include "gui_windows.h"
#elif defined(GUI_PLATFORM_LINUX)
    #include "gui_linux.h"
#endif

/* ============================================================
 * 平台特定实现
 * ============================================================ */
#if defined(GUI_PLATFORM_WINDOWS)

/* ============================================================
 * 列表框操作
 * ============================================================ */

/**
 * @brief 向列表框添加一项文本
 * @param control 列表框控件指针
 * @param text 要添加的文本
 * @return 成功返回GUI_OK，失败返回错误码
 */
int gui_listbox_add_item(GuiControl_Win *control, const char *text) {
    if (!control || !control->hwnd) return GUI_ERR_INVALID_PARAM;
    
    wchar_t wtext[4096];
    gui_utf8_to_wide(text, wtext, 4096);
    
    SendMessageW(control->hwnd, LB_ADDSTRING, 0, (LPARAM)wtext);
    return GUI_OK;
}

/**
 * @brief 向列表框指定位置插入一项
 * @param control 列表框控件指针
 * @param index 插入位置（-1表示末尾）
 * @param text 要插入的文本
 * @return 成功返回GUI_OK，失败返回错误码
 */
int gui_listbox_insert_item(GuiControl_Win *control, int index, const char *text) {
    if (!control || !control->hwnd) return GUI_ERR_INVALID_PARAM;
    
    wchar_t wtext[4096];
    gui_utf8_to_wide(text, wtext, 4096);
    
    if (index < 0) index = -1;
    SendMessageW(control->hwnd, LB_INSERTSTRING, (WPARAM)index, (LPARAM)wtext);
    return GUI_OK;
}

/**
 * @brief 删除列表框中指定项
 * @param control 列表框控件指针
 * @param index 要删除的索引
 * @return 成功返回GUI_OK，失败返回错误码
 */
int gui_listbox_remove_item(GuiControl_Win *control, int index) {
    if (!control || !control->hwnd) return GUI_ERR_INVALID_PARAM;
    
    SendMessageW(control->hwnd, LB_DELETESTRING, (WPARAM)index, 0);
    return GUI_OK;
}

/**
 * @brief 清空列表框所有项
 * @param control 列表框控件指针
 * @return 成功返回GUI_OK，失败返回错误码
 */
int gui_listbox_clear(GuiControl_Win *control) {
    if (!control || !control->hwnd) return GUI_ERR_INVALID_PARAM;
    
    SendMessageW(control->hwnd, LB_RESETCONTENT, 0, 0);
    return GUI_OK;
}

/**
 * @brief 获取列表框当前选中项索引
 * @param control 列表框控件指针
 * @return 选中索引（从0开始），无选中返回LB_ERR(-1)
 */
int gui_listbox_get_selected_index(GuiControl_Win *control) {
    if (!control || !control->hwnd) return -1;
    
    return (int)SendMessageW(control->hwnd, LB_GETCURSEL, 0, 0);
}

/**
 * @brief 设置列表框当前选中项
 * @param control 列表框控件指针
 * @param index 要选中的索引（-1取消选择）
 * @return 成功返回GUI_OK，失败返回错误码
 */
int gui_listbox_set_selected_index(GuiControl_Win *control, int index) {
    if (!control || !control->hwnd) return GUI_ERR_INVALID_PARAM;
    
    SendMessageW(control->hwnd, LB_SETCURSEL, (WPARAM)index, 0);
    return GUI_OK;
}

/**
 * @brief 获取列表框中的项数
 * @param control 列表框控件指针
 * @return 项目数量
 */
int gui_listbox_get_count(GuiControl_Win *control) {
    if (!control || !control->hwnd) return 0;
    
    return (int)SendMessageW(control->hwnd, LB_GETCOUNT, 0, 0);
}

/**
 * @brief 获取列表框指定索引处的文本
 * @param control 列表框控件指针
 * @param index 索引
 * @return 文本字符串（静态缓冲区）
 */
const char* gui_listbox_get_item_text(GuiControl_Win *control, int index) {
    static char text_buffer[4096];
    if (!control || !control->hwnd) { text_buffer[0] = '\0'; return text_buffer; }
    
    wchar_t wtext[4096];
    int len = (int)SendMessageW(control->hwnd, LB_GETTEXTLEN, (WPARAM)index, 0);
    if (len <= 0 || len >= 4096) { text_buffer[0] = '\0'; return text_buffer; }
    
    SendMessageW(control->hwnd, LB_GETTEXT, (WPARAM)index, (LPARAM)wtext);
    gui_wide_to_utf8(wtext, text_buffer, 4096);
    return text_buffer;
}

/**
 * @brief 查找列表框中指定文本的索引
 * @param control 列表框控件指针
 * @param text 要查找的文本
 * @return 找到返回索引，未找到返回LB_ERR(-1)
 */
int gui_listbox_find_item(GuiControl_Win *control, const char *text) {
    if (!control || !control->hwnd) return -1;
    
    wchar_t wtext[4096];
    gui_utf8_to_wide(text, wtext, 4096);
    
    return (int)SendMessageW(control->hwnd, LB_FINDSTRINGEXACT, (WPARAM)-1, (LPARAM)wtext);
}

/* ============================================================
 * 下拉框(ComboBox) 操作
 * ============================================================ */

/**
 * @brief 向下拉框添加一项文本
 * @param control 下拉框控件指针
 * @param text 要添加的文本
 * @return 成功返回GUI_OK，失败返回错误码
 */
int gui_combobox_add_item(GuiControl_Win *control, const char *text) {
    if (!control || !control->hwnd) return GUI_ERR_INVALID_PARAM;
    
    wchar_t wtext[4096];
    gui_utf8_to_wide(text, wtext, 4096);
    
    SendMessageW(control->hwnd, CB_ADDSTRING, 0, (LPARAM)wtext);
    return GUI_OK;
}

/**
 * @brief 向下拉框指定位置插入一项
 * @param control 下拉框控件指针
 * @param index 插入位置（-1表示末尾）
 * @param text 要插入的文本
 * @return 成功返回GUI_OK，失败返回错误码
 */
int gui_combobox_insert_item(GuiControl_Win *control, int index, const char *text) {
    if (!control || !control->hwnd) return GUI_ERR_INVALID_PARAM;
    
    wchar_t wtext[4096];
    gui_utf8_to_wide(text, wtext, 4096);
    
    if (index < 0) index = -1;
    SendMessageW(control->hwnd, CB_INSERTSTRING, (WPARAM)index, (LPARAM)wtext);
    return GUI_OK;
}

/**
 * @brief 删除下拉框中指定项
 * @param control 下拉框控件指针
 * @param index 要删除的索引
 * @return 成功返回GUI_OK，失败返回错误码
 */
int gui_combobox_remove_item(GuiControl_Win *control, int index) {
    if (!control || !control->hwnd) return GUI_ERR_INVALID_PARAM;
    
    SendMessageW(control->hwnd, CB_DELETESTRING, (WPARAM)index, 0);
    return GUI_OK;
}

/**
 * @brief 清空下拉框所有项
 * @param control 下拉框控件指针
 * @return 成功返回GUI_OK，失败返回错误码
 */
int gui_combobox_clear(GuiControl_Win *control) {
    if (!control || !control->hwnd) return GUI_ERR_INVALID_PARAM;
    
    SendMessageW(control->hwnd, CB_RESETCONTENT, 0, 0);
    return GUI_OK;
}

/**
 * @brief 获取下拉框当前选中项索引
 * @param control 下拉框控件指针
 * @return 选中索引（从0开始），无选中返回CB_ERR(-1)
 */
int gui_combobox_get_selected_index(GuiControl_Win *control) {
    if (!control || !control->hwnd) return -1;
    
    return (int)SendMessageW(control->hwnd, CB_GETCURSEL, 0, 0);
}

/**
 * @brief 设置下拉框当前选中项
 * @param control 下拉框控件指针
 * @param index 要选中的索引（-1取消选择）
 * @return 成功返回GUI_OK，失败返回错误码
 */
int gui_combobox_set_selected_index(GuiControl_Win *control, int index) {
    if (!control || !control->hwnd) return GUI_ERR_INVALID_PARAM;
    
    SendMessageW(control->hwnd, CB_SETCURSEL, (WPARAM)index, 0);
    return GUI_OK;
}

/**
 * @brief 获取下拉框中的项数
 * @param control 下拉框控件指针
 * @return 项目数量
 */
int gui_combobox_get_count(GuiControl_Win *control) {
    if (!control || !control->hwnd) return 0;
    
    return (int)SendMessageW(control->hwnd, CB_GETCOUNT, 0, 0);
}

/**
 * @brief 获取下拉框指定索引处的文本
 * @param control 下拉框控件指针
 * @param index 索引
 * @return 文本字符串（静态缓冲区）
 */
const char* gui_combobox_get_item_text(GuiControl_Win *control, int index) {
    static char text_buffer[4096];
    if (!control || !control->hwnd) { text_buffer[0] = '\0'; return text_buffer; }
    
    wchar_t wtext[4096];
    SendMessageW(control->hwnd, CB_GETLBTEXT, (WPARAM)index, (LPARAM)wtext);
    gui_wide_to_utf8(wtext, text_buffer, 4096);
    return text_buffer;
}

/**
 * @brief 设置下拉框编辑框中的文本
 * @param control 下拉框控件指针
 * @param text 文本内容
 * @return 成功返回GUI_OK，失败返回错误码
 */
int gui_combobox_set_edit_text(GuiControl_Win *control, const char *text) {
    if (!control || !control->hwnd) return GUI_ERR_INVALID_PARAM;
    
    wchar_t wtext[4096];
    gui_utf8_to_wide(text, wtext, 4096);
    
    SetWindowTextW(control->hwnd, wtext);
    return GUI_OK;
}

/* ============================================================
 * 复选框(CheckBox) 操作
 * ============================================================ */

/**
 * @brief 获取复选框是否被选中
 * @param control 复选框控件指针
 * @return 1=选中, 0=未选中, 错误返回-1
 */
int gui_checkbox_get_checked(GuiControl_Win *control) {
    if (!control || !control->hwnd) return -1;
    
    return (SendMessageW(control->hwnd, BM_GETCHECK, 0, 0) == BST_CHECKED) ? 1 : 0;
}

/**
 * @brief 设置复选框选中状态
 * @param control 复选框控件指针
 * @param checked 1=选中, 0=未选中
 * @return 成功返回GUI_OK，失败返回错误码
 */
int gui_checkbox_set_checked(GuiControl_Win *control, int checked) {
    if (!control || !control->hwnd) return GUI_ERR_INVALID_PARAM;
    
    SendMessageW(control->hwnd, BM_SETCHECK, checked ? BST_CHECKED : BST_UNCHECKED, 0);
    return GUI_OK;
}

/* ============================================================
 * 单选按钮(RadioButton) 操作
 * ============================================================ */

/**
 * @brief 获取单选按钮是否被选中
 * @param control 单选按钮控件指针
 * @return 1=选中, 0=未选中, 错误返回-1
 */
int gui_radio_get_checked(GuiControl_Win *control) {
    if (!control || !control->hwnd) return -1;
    
    return (SendMessageW(control->hwnd, BM_GETCHECK, 0, 0) == BST_CHECKED) ? 1 : 0;
}

/**
 * @brief 设置单选按钮选中状态
 * @param control 单选按钮控件指针
 * @param checked 1=选中, 0=未选中
 * @return 成功返回GUI_OK，失败返回错误码
 */
int gui_radio_set_checked(GuiControl_Win *control, int checked) {
    if (!control || !control->hwnd) return GUI_ERR_INVALID_PARAM;
    
    SendMessageW(control->hwnd, BM_SETCHECK, checked ? BST_CHECKED : BST_UNCHECKED, 0);
    return GUI_OK;
}

/* ============================================================
 * 进度条(ProgressBar) 操作
 * ============================================================ */

/**
 * @brief 设置进度条的当前位置
 * @param control 进度条控件指针
 * @param value 进度值（0-100范围）
 * @return 成功返回GUI_OK，失败返回错误码
 */
int gui_progressbar_set_value(GuiControl_Win *control, int value) {
    if (!control || !control->hwnd) return GUI_ERR_INVALID_PARAM;
    
    PBM_SETPOS;
    SendMessageW(control->hwnd, PBM_SETPOS, (WPARAM)value, 0);
    return GUI_OK;
}

/**
 * @brief 获取进度条的当前位置
 * @param control 进度条控件指针
 * @return 当前进度值
 */
int gui_progressbar_get_value(GuiControl_Win *control) {
    if (!control || !control->hwnd) return 0;
    
    return (int)SendMessageW(control->hwnd, PBM_GETPOS, 0, 0);
}

/**
 * @brief 设置进度条的范围
 * @param control 进度条控件指针
 * @param min_value 最小值
 * @param max_value 最大值
 * @return 成功返回GUI_OK，失败返回错误码
 */
int gui_progressbar_set_range(GuiControl_Win *control, int min_value, int max_value) {
    if (!control || !control->hwnd) return GUI_ERR_INVALID_PARAM;
    
    SendMessageW(control->hwnd, PBM_SETRANGE32, (WPARAM)min_value, (LPARAM)max_value);
    return GUI_OK;
}

/* ============================================================
 * 滑块(Slider/TrackBar) 操作
 * ============================================================ */

/**
 * @brief 设置滑块的当前位置
 * @param control 滑块控件指针
 * @param value 滑块值
 * @return 成功返回GUI_OK，失败返回错误码
 */
int gui_slider_set_value(GuiControl_Win *control, int value) {
    if (!control || !control->hwnd) return GUI_ERR_INVALID_PARAM;
    
    SendMessageW(control->hwnd, TBM_SETPOS, TRUE, (LPARAM)value);
    return GUI_OK;
}

/**
 * @brief 获取滑块的当前位置
 * @param control 滑块控件指针
 * @return 当前滑块值
 */
int gui_slider_get_value(GuiControl_Win *control) {
    if (!control || !control->hwnd) return 0;
    
    return (int)SendMessageW(control->hwnd, TBM_GETPOS, 0, 0);
}

/**
 * @brief 设置滑块的范围
 * @param control 滑块控件指针
 * @param min_value 最小值
 * @param max_value 最大值
 * @return 成功返回GUI_OK，失败返回错误码
 */
int gui_slider_set_range(GuiControl_Win *control, int min_value, int max_value) {
    if (!control || !control->hwnd) return GUI_ERR_INVALID_PARAM;
    
    SendMessageW(control->hwnd, TBM_SETRANGE, TRUE, MAKELPARAM(min_value, max_value));
    return GUI_OK;
}

/**
 * @brief 设置滑块的刻度频率
 * @param control 滑块控件指针
 * @param freq 刻度间隔
 * @return 成功返回GUI_OK，失败返回错误码
 */
int gui_slider_set_tick_freq(GuiControl_Win *control, int freq) {
    if (!control || !control->hwnd) return GUI_ERR_INVALID_PARAM;
    
    SendMessageW(control->hwnd, TBM_SETTICFREQ, (WPARAM)freq, 0);
    return GUI_OK;
}

/* ============================================================
 * 多行文本框(TextBox) 操作
 * ============================================================ */

/**
 * @brief 向多行文本框追加文本
 * @param control 文本框控件指针
 * @param text 要追加的文本
 * @return 成功返回GUI_OK，失败返回错误码
 */
int gui_textbox_append_text(GuiControl_Win *control, const char *text) {
    if (!control || !control->hwnd || !text) return GUI_ERR_INVALID_PARAM;
    
    /* 获取当前文本长度 */
    int len = GetWindowTextLengthW(control->hwnd);
    
    /* 选中文本末尾 */
    SendMessageW(control->hwnd, EM_SETSEL, (WPARAM)len, (LPARAM)len);
    
    /* 替换选区（即追加） */
    wchar_t wtext[4096];
    gui_utf8_to_wide(text, wtext, 4096);
    SendMessageW(control->hwnd, EM_REPLACESEL, FALSE, (LPARAM)wtext);
    
    return GUI_OK;
}

/**
 * @brief 清空多行文本框内容
 * @param control 文本框控件指针
 * @return 成功返回GUI_OK，失败返回错误码
 */
int gui_textbox_clear(GuiControl_Win *control) {
    if (!control || !control->hwnd) return GUI_ERR_INVALID_PARAM;
    
    SetWindowTextW(control->hwnd, L"");
    return GUI_OK;
}

/**
 * @brief 获取多行文本框的总行数
 * @param control 文本框控件指针
 * @return 行数
 */
int gui_textbox_get_line_count(GuiControl_Win *control) {
    if (!control || !control->hwnd) return 0;
    
    return (int)SendMessageW(control->hwnd, EM_GETLINECOUNT, 0, 0);
}

#else /* GUI_PLATFORM_LINUX - 转发到gui_linux.c中的实现 */

/* 以下函数在gui_linux.c中已实现Linux版本，此处提供兼容性包装 */
/* 注意：Linux平台直接使用GuiControl_Linux类型 */

int gui_listbox_add_item(GuiControl_Linux *control, const char *text) { return gui_listbox_add_item(control, text); }
int gui_listbox_insert_item(GuiControl_Linux *control, int index, const char *text) { return gui_listbox_insert_item(control, index, text); }
int gui_listbox_remove_item(GuiControl_Linux *control, int index) { return gui_listbox_remove_item(control, index); }
int gui_listbox_clear(GuiControl_Linux *control) { return gui_listbox_clear(control); }
int gui_listbox_get_selected_index(GuiControl_Linux *control) { return gui_listbox_get_selected_index(control); }
int gui_listbox_set_selected_index(GuiControl_Linux *control, int index) { return gui_listbox_set_selected_index(control, index); }
int gui_listbox_get_count(GuiControl_Linux *control) { return gui_listbox_get_count(control); }
const char* gui_listbox_get_item_text(GuiControl_Linux *control, int index) { return gui_listbox_get_item_text(control, index); }

int gui_combobox_add_item(GuiControl_Linux *control, const char *text) { return gui_combobox_add_item(control, text); }
int gui_combobox_insert_item(GuiControl_Linux *control, int index, const char *text) { return gui_combobox_insert_item(control, index, text); }
int gui_combobox_remove_item(GuiControl_Linux *control, int index) { return gui_combobox_remove_item(control, index); }
int gui_combobox_clear(GuiControl_Linux *control) { return gui_combobox_clear(control); }
int gui_combobox_get_selected_index(GuiControl_Linux *control) { return gui_combobox_get_selected_index(control); }
int gui_combobox_set_selected_index(GuiControl_Linux *control, int index) { return gui_combobox_set_selected_index(control, index); }
int gui_combobox_get_count(GuiControl_Linux *control) { return gui_combobox_get_count(control); }
const char* gui_combobox_get_item_text(GuiControl_Linux *control, int index) { return gui_combobox_get_item_text(control, index); }

int gui_checkbox_get_checked(GuiControl_Linux *control) { return gui_checkbox_get_checked(control); }
int gui_checkbox_set_checked(GuiControl_Linux *control, int checked) { return gui_checkbox_set_checked(control, checked); }
int gui_radio_get_checked(GuiControl_Linux *control) { return gui_radio_get_checked(control); }
int gui_radio_set_checked(GuiControl_Linux *control, int checked) { return gui_radio_set_checked(control, checked); }

int gui_progressbar_set_value(GuiControl_Linux *control, int value) { return gui_progressbar_set_value(control, value); }
int gui_progressbar_get_value(GuiControl_Linux *control) { return gui_progressbar_get_value(control); }
int gui_progressbar_set_range(GuiControl_Linux *control, int min_val, int max_val) { return gui_progressbar_set_range(control, min_val, max_val); }

int gui_slider_set_value(GuiControl_Linux *control, int value) { return gui_slider_set_value(control, value); }
int gui_slider_get_value(GuiControl_Linux *control) { return gui_slider_get_value(control); }
int gui_slider_set_range(GuiControl_Linux *control, int min_val, int max_val) { return gui_slider_set_range(control, min_val, max_val); }
int gui_slider_set_tick_freq(GuiControl_Linux *control, int freq) { return gui_slider_set_tick_freq(control, freq); }

int gui_textbox_append_text(GuiControl_Linux *control, const char *text) { return gui_textbox_append_text(control, text); }
int gui_textbox_clear(GuiControl_Linux *control) { return gui_textbox_clear(control); }
int gui_textbox_get_line_count(GuiControl_Linux *control) { return gui_textbox_get_line_count(control); }

#endif /* GUI_PLATFORM_WINDOWS */
