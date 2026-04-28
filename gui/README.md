# LXCLUA GUI - 跨平台图形用户界面库

## 概述

LXCLUA GUI是一个为LXCLUA虚拟机设计的跨平台GUI解决方案，提供完整的Windows原生桌面应用程序开发能力。

**当前支持平台**: Windows (10/11)

## 特性

### 核心功能
- ✅ 原生Windows控件支持（按钮、文本框、标签等16种控件）
- ✅ 完整的事件处理系统（鼠标、键盘、窗口事件）
- ✅ 窗口管理（创建、显示、隐藏、移动、缩放、透明度）
- ✅ 模态/非模态对话框支持
- ✅ 文件/文件夹选择对话框
- ✅ 颜色选择器和字体选择器
- ✅ 进度条和滑块控件
- ✅ 列表框、组合框、树形视图、列表视图
- ✅ 定时器支持
- ✅ 剪贴板操作
- ✅ 文件拖放支持
- ✅ Unicode/UTF-8完整支持

### 控件类型

| 控件 | Lua函数 | 说明 |
|------|---------|------|
| 按钮 | `createButton` | 标准按钮，支持点击回调 |
| 标签 | `createLabel` | 静态文本标签 |
| 单行文本框 | `createEdit` | 单行文本输入 |
| 多行文本框 | `createTextbox` | 多行只读文本显示 |
| 复选框 | `createCheckbox` | 复选按钮 |
| 单选按钮 | `createRadio` | 单选按钮 |
| 分组框 | `createGroupbox` | 控件分组容器 |
| 列表框 | `createListbox` | 列表选择控件 |
| 组合框 | `createCombobox` | 下拉选择控件 |
| 进度条 | `createProgressbar` | 进度指示器 |
| 滑块 | `createSlider` | 数值滑块 |
| 标签页 | `createTab` | 标签页控件 |
| 树形视图 | `createTreeview` | 层级数据展示 |
| 列表视图 | `createListview` | 表格数据展示 |

## 快速开始

### 基本示例

```lua
local gui = require("gui")

-- 1. 初始化GUI系统
gui.init()

-- 2. 创建主窗口
local win = gui.createWindow({
    title = "我的应用",
    width = 800,
    height = 600,
    centerScreen = true
})

-- 3. 创建一个按钮
local btn = win:createButton({
    text = "点击我",
    x = 10, y = 10, width = 100, height = 30,
    onClick = function(self, event)
        gui.messageBox("Hello from LXCLUA GUI!", "提示", "ok")
    end
})

-- 4. 显示窗口并进入消息循环
win:show()
gui.run()

-- 5. 清理资源（可选，程序退出时自动处理）
gui.cleanup()
```

### 创建输入表单

```lua
local gui = require("gui")
gui.init()

local win = gui.createWindow({title="登录", width=400, height=300, centerScreen=true})

-- 用户名标签和输入框
win:createLabel({text="用户名:", x=20, y=30, width=60, height=22})
local editUser = win:createEdit({x=90, y=28, width=250, height=24})

-- 密码标签和输入框
win:createLabel({text="密码:", x=20, y=65, width=60, height=22})
local editPass = win:createEdit({x=90, y=63, width=250, height=24})

-- 记住密码复选框
local chkRemember = win:createCheckbox({
    text = "记住密码",
    x = 20, y = 100, width=100, height=22
})

-- 登录按钮
win:createButton({
    text = "登录",
    x = 150, y = 140, width=100, height=32,
    onClick = function(self, event)
        local user = editUser:getText()
        local pass = editPass:getText()
        
        if #user == 0 or #pass == 0 then
            gui.messageBox("请输入用户名和密码", "错误", "ok|icon_error")
            return
        end
        
        -- 处理登录逻辑...
        gui.messageBox(string.format("欢迎, %s!", user), "登录成功", "ok|icon_info")
        win:close()
    end
})

-- 取消按钮
win:createButton({
    text = "取消",
    x = 260, y = 140, width=80, height=32,
    onClick = function(self, event)
        win:close()
    end
})

win:show()
gui.run()
```

### 文件操作对话框

```lua
-- 打开文件
local path = gui.fileDialog("选择文件", "Lua脚本\0*.lua\0所有文件\0*.*\0")
if path then
    print("选择的文件: " .. path)
end

-- 保存文件
local savePath = gui.fileDialog("保存文件", "文本文件\0*.txt\0", true)
if savePath then
    print("保存到: " .. savePath)
end

-- 选择文件夹
local folder = gui.folderDialog("选择目标目录")
if folder then
    print("选择的文件夹: " .. folder)
end
```

## API参考

### 系统管理函数

#### `gui.init()`
初始化GUI系统。**必须在使用其他GUI函数前调用。**
- **返回**: 成功返回true，失败返回nil和错误信息

#### `gui.run()`
进入主消息循环。此函数会阻塞直到所有窗口关闭或调用`gui.exit()`。
- **返回**: 退出代码

#### `gui.exit([code])`
退出消息循环。
- **参数**: code (可选) - 退出代码，默认0

#### `gui.cleanup()`
清理GUI系统并释放所有资源。

#### `gui.version()`
获取版本字符串。

#### `gui.getLastError()`
获取最后一次操作的错误信息。

### 窗口函数

#### `gui.createWindow(config) -> window`
创建新窗口。

**config表格字段**:
```lua
{
    title = "窗口标题",          -- string (可选, 默认"LXCLUA Window")
    x = 100,                     -- number (可选, 默认CW_USEDEFAULT)
    y = 100,                     -- number (可选)
    width = 800,                 -- number (可选, 默认800)
    height = 600,                -- number (可选, 默认600)
    centerScreen = true,         -- boolean (可选, 是否居中)
    resizable = true,            -- boolean (可选, 可调整大小)
    topmost = false,             -- boolean (可选, 置顶)
    opacity = 255,               -- number (可选, 不透明度0-255)
    bgColor = {r=255,g=255,b=255,a=255}  -- table (可选, 背景色)
}
```

#### 窗口对象方法
- `window:show([cmd_show])` - 显示窗口
- `window:hide()` - 隐藏窗口
- `window:close()` - 关闭窗口
- `window:setTitle(title)` - 设置标题
- `window:getTitle() -> string` - 获取标题
- `window:setRect(rect)` - 设置位置和尺寸
- `window:getRect() -> table` - 获取位置和尺寸
- `window:setOpacity(opacity)` - 设置不透明度
- `window:center([relativeToParent])` - 居中显示
- `window:enable(enabled)` - 启用/禁用
- `window:setModal(modal)` - 设置模态
- `window:flash([count])` - 闪烁提醒

### 控件创建函数

所有控件通过窗口对象的以下方法创建：

```lua
local control = window:createXxx({
    text = "控件文本",           -- string (可选)
    x = 10, y = 10,              -- position
    width = 100, height = 30,    -- size
    visible = true,              -- boolean (可选, 默认true)
    enabled = true,              -- boolean (可选, 默认true)
    fontSize = 14,               -- number (可选, 字体大小)
    fontName = "Microsoft YaHei UI", -- string (可选, 字体名称)
    bgColor = {...},             -- table (可选, 背景色)
    fgColor = {...},             -- table (可选, 前景色)
    anchor = 0,                  -- number (可选, 锚点标志)
    onClick = function(...) end, -- function (可选, 点击回调)
    onChange = function(...) end -- function (可选, 值改变回调)
})
```

**可用的创建方法**:
- `window:createButton(props)` - 按钮
- `window:createLabel(props)` - 标签
- `window:createEdit(props)` - 单行文本框
- `window:createTextbox(props)` - 多行文本框
- `window:createCheckbox(props)` - 复选框
- `window:createRadio(props)` - 单选按钮
- `window:createGroupbox(props)` - 分组框
- `window:createListbox(props)` - 列表框
- `window:createCombobox(props)` - 组合框
- `window:createProgressbar(props)` - 进度条
- `window:createSlider(props)` - 滑块
- `window:createTab(props)` - 标签页
- `window:createTreeview(props)` - 树形视图
- `window:createListview(props)` - 列表视图

#### 控件对象方法
- `control:setText(text)` - 设置文本
- `control:getText() -> string` - 获取文本
- `control:setRect(rect)` - 设置位置和尺寸
- `control:getRect() -> table` - 获取位置和尺寸
- `control:setVisible(visible)` - 显示/隐藏
- `control:setEnabled(enabled)` - 启用/禁用
- `control:setFocus()` - 设置焦点
- `control:toFront()` - 置顶

### 对话框函数

#### `gui.messageBox(message, [title], [type]) -> result`
显示消息框。

**type参数**:
- `"ok"` - 确定
- `"okcancel"` - 确定/取消
- `"yesno"` - 是/否
- `"yesnocancel"` - 是/否/取消
- `"retrycancel"` - 重试/取消
- 添加 `"icon_error"`, `"icon_warning"`, `"icon_info"`, `"icon_question"` 显示图标

**返回值**: 点击的按钮ID

#### `gui.fileDialog([title], [filter], [saveMode], [multiSelect]) -> path|string|nil`
文件选择对话框。

**filter格式**: `"描述1\0*.ext1\0描述2\0*.ext2\0\0"`

#### `gui.folderDialog([title]) -> path|nil`
文件夹选择对话框。

#### `gui.colorDialog([initialColor]) -> color|nil`
颜色选择对话框。

### 其他函数

#### `gui.clipboardCopy(text)`
复制文本到剪贴板。

#### `gui.clipboardPaste() -> text|nil`
从剪贴板粘贴文本。

#### `gui.sleep(ms)`
延迟执行（毫秒）。

## 编译说明

### Windows (MSVC)

将以下源文件添加到项目：

```
gui/gui_core.h
gui/gui_windows.h
gui/gui_windows.c
gui/gui_controls.c
gui/lguilib.c
```

链接库：
```
comctl32.lib
shell32.lib
comdlg32.lib
ole32.lib
uuid.lib
```

预处理器定义：
```
GUI_PLATFORM_WINDOWS
_UNICODE
UNICODE
```

### CMake集成示例

```cmake
# 添加GUI库
set(GUI_SOURCES
    gui/gui_core.h
    gui/gui_windows.h
    gui/gui_windows.c
    gui/gui_controls.c
    gui/lguilib.c
)

add_library(lxclua_gui ${GUI_SOURCES})
target_link_libraries(lxclua_gui PRIVATE comctl32 shell32 comdlg32 ole32 uuid)
target_compile_definitions(lxclua_gui PRIVATE GUI_PLATFORM_WINDOWS _UNICODE UNICODE)
```

## 架构设计

```
┌─────────────────────────────────────┐
│            Lua脚本层                 │
│     (gui_demo.lua / 用户代码)       │
├─────────────────────────────────────┤
│          Lua绑定层 (lguilib.c)      │
│   提供Lua API，桥接C和Lua           │
├─────────────────────────────────────┤
│      GUI核心实现层                   │
│  ┌───────────┬──────────────────┐   │
│  │gui_windows.c│ gui_controls.c │   │
│  │(窗口/消息循环)│ (控件/对话框)  │   │
│  └───────────┴──────────────────┘   │
├─────────────────────────────────────┤
│        平台抽象层 (gui_core.h)       │
│    数据结构、常量、类型定义          │
├─────────────────────────────────────┤
│        Windows API (Win32/GDI+)     │
└─────────────────────────────────────┘
```

## 未来计划

- [ ] Linux平台支持 (GTK/Qt)
- [ ] macOS平台支持 (Cocoa)
- [ ] 更多高级控件（图表、WebView）
- [ ] 主题/皮肤系统
- [ ] 国际化(i18n)支持
- [ ] DPI感知和高DPI支持
- [ ] 触摸屏手势支持
- [ ] 无障碍访问支持

## 许可证

遵循LXCLUA主项目的许可证协议。

## 作者

**DiferLine** - LXCLUA核心开发者

---

*最后更新: 2026-04-18*
