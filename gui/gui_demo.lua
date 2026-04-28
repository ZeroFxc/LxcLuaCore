--[[
    @file gui_demo.lua
    @brief LXCLUA GUI演示程序 - 展示GUI库的基本功能
    @author DiferLine
    @version 1.0.0
    
    本示例展示如何使用LXCLUA的GUI库创建Windows桌面应用程序。
    
    运行方式:
        lua gui_demo.lua
    
    功能演示:
        - 窗口创建与控制
        - 各种控件的使用
        - 事件处理
        - 对话框
]]

local gui = require("gui")

-- ============================================================
-- 示例1: 基础窗口
-- ============================================================
print("=== LXCLUA GUI 演示 ===")
print("版本:", gui.version())
print("平台:", gui.PLATFORM)

-- 初始化GUI系统
if not gui.init() then
    error("GUI初始化失败: " .. gui.getLastError())
end

print("GUI系统初始化成功")

-- 创建主窗口
local mainWindow = gui.createWindow({
    title = "LXCLUA GUI Demo - 主窗口",
    width = 900,
    height = 650,
    centerScreen = true,
    resizable = true,
    bgColor = {r=240, g=240, b=245, a=255}
})

if not mainWindow then
    error("无法创建主窗口")
end

print("主窗口创建成功")

-- 创建菜单栏按钮区域（使用Groupbox分组）
local menuGroup = mainWindow:createGroupbox({
    text = "菜单",
    x = 10, y = 10, width = 870, height = 50
})

-- 创建菜单按钮
local btnNew = mainWindow:createButton({
    text = "新建",
    x = 20, y = 25, width = 80, height = 28,
    onClick = function(self, event)
        print("[事件] 新建按钮被点击")
    end
})

local btnOpen = mainWindow:createButton({
    text = "打开...",
    x = 110, y = 25, width = 80, height = 28,
    onClick = function(self, event)
        print("[事件] 打开按钮被点击")
        local path = gui.fileDialog("选择文件", "Lua脚本\0*.lua\0所有文件\0*.*\0", false, false)
        if path then
            print("选择的文件:", path)
            editPath:setText(path)
        end
    end
})

local btnSave = mainWindow:createButton({
    text = "保存",
    x = 200, y = 25, width = 80, height = 28,
    onClick = function(self, event)
        print("[事件] 保存按钮被点击")
        local path = gui.fileDialog("保存文件", "文本文件\0*.txt\0所有文件\0*.*\0", true, false)
        if path then
            print("保存到:", path)
            gui.messageBox("文件已保存到:\n" .. path, "保存成功", "ok|icon_info")
        end
    end
})

local btnExit = mainWindow:createButton({
    text = "退出",
    x = 790, y = 25, width = 80, height = 28,
    onClick = function(self, event)
        print("[事件] 退出按钮被点击")
        mainWindow:close()
    end
})

-- 创建标签页分组
local tabGroup = mainWindow:createGroupbox({
    text = "控件演示",
    x = 10, y = 70, width = 420, height = 560
})

-- 标签1：基础控件
local labelTitle = mainWindow:createLabel({
    text = "基础控件",
    x = 20, y = 95, width = 200, height = 24,
    fontSize = 16
})

-- 文本输入演示
local lblName = mainWindow:createLabel({
    text = "姓名:",
    x = 30, y = 130, width = 60, height = 22
})

local editName = mainWindow:createEdit({
    text = "",
    x = 100, y = 128, width = 200, height = 24,
    onChange = function(self, event)
        -- 可以在这里处理实时输入
    end
})

local lblEmail = mainWindow:createLabel({
    text = "邮箱:",
    x = 30, y = 160, width = 60, height = 22
})

local editEmail = mainWindow:createEdit({
    text = "example@email.com",
    x = 100, y = 158, width = 300, height = 24
})

-- 复选框和单选按钮演示
local chkEnabled = mainWindow:createCheckbox({
    text = "启用功能",
    x = 30, y = 195, width = 150, height = 22,
    onClick = function(self, event)
        print("[复选框] 状态改变")
    end
})

local chkRemember = mainWindow:createCheckbox({
    text = "记住我的选择",
    x = 200, y = 195, width = 150, height = 22
})

local radioOption1 = mainWindow:createRadio({
    text = "选项 A",
    x = 30, y = 225, width = 100, height = 22
})

local radioOption2 = mainWindow:createRadio({
    text = "选项 B",
    x = 140, y = 225, width = 100, height = 22
})

local radioOption3 = mainWindow:createRadio({
    text = "选项 C",
    x = 250, y = 225, width = 100, height = 22
})

-- 进度条和滑块演示
local lblProgress = mainWindow:createLabel({
    text = "进度条:",
    x = 30, y = 260, width = 60, height = 22
})

local progressBar = mainWindow:createProgressbar({
    x = 100, y = 260, width = 300, height = 24
})

local sliderValue = mainWindow:createSlider({
    x = 100, y = 295, width = 300, height = 30
})

local lblSliderVal = mainWindow:createLabel({
    text = "值: 50",
    x = 410, y = 298, width = 50, height = 22
})

-- 列表框演示
local lblList = mainWindow:createLabel({
    text = "列表:",
    x = 30, y = 335, width = 40, height = 22
})

local listBox = mainWindow:createListbox({
    x = 100, y = 335, width = 200, height = 120
})

-- 下拉组合框演示
local lblCombo = mainWindow:createLabel({
    text = "下拉框:",
    x = 310, y = 335, width = 50, height = 22
})

local comboBox = mainWindow:createCombobox({
    x = 370, y = 335, width = 120, height = 200
})

-- 多行文本框演示
local lblOutput = mainWindow:createLabel({
    text = "输出日志:",
    x = 30, y = 465, width = 80, height = 22
})

local textboxOutput = mainWindow:createTextbox({
    text = "欢迎使用LXCLUA GUI库!\n这是一个功能完整的跨平台GUI解决方案。\n\n功能特性:\n- 原生Windows控件支持\n- 完整的事件处理系统\n- 对话框支持\n- 响应式布局",
    x = 110, y = 465, width = 380, height = 155
})

-- 右侧面板：高级功能演示
local advGroup = mainWindow:createGroupbox({
    text = "高级功能",
    x = 445, y = 70, width = 435, height = 270
})

-- 颜色和字体选择
local btnColor = mainWindow:createButton({
    text = "选择颜色...",
    x = 460, y = 95, width = 120, height = 30,
    onClick = function(self, event)
        local color = gui.colorDialog({r=100, g=149, b=237})
        if color then
            print(string.format("选择的颜色: R=%d G=%d B=%d", color.r, color.g, color.b))
            mainWindow:setBgColor(color)
        end
    end
})

local btnFont = mainWindow:createButton({
    text = "选择字体...",
    x = 595, y = 95, width = 120, height = 30,
    onClick = function(self, event)
        -- 字体选择对话框
        gui.messageBox("字体选择功能演示", "提示", "ok|icon_info")
    end
})

-- 文件夹选择
local btnFolder = mainWindow:createButton({
    text = "选择文件夹...",
    x = 730, y = 95, width = 130, height = 30,
    onClick = function(self, event)
        local folder = gui.folderDialog("选择目标文件夹")
        if folder then
            print("选择的文件夹:", folder)
            gui.messageBox("您选择了:\n" .. folder, "文件夹已选择", "ok|icon_info")
        end
    end
})

-- 剪贴板操作
local lblClipboard = mainWindow:createLabel({
    text = "剪贴板操作:",
    x = 460, y = 140, width = 80, height = 22
})

local editClipboard = mainWindow:createEdit({
    text = "要复制到剪贴板的文本...",
    x = 550, y = 138, width = 310, height = 24
})

local btnCopy = mainWindow:createButton({
    text = "复制",
    x = 460, y = 170, width = 80, height = 28,
    onClick = function(self, event)
        local text = editClipboard:getText()
        gui.clipboardCopy(text)
        print("[剪贴板] 已复制:", text)
    end
})

local btnPaste = mainWindow:createButton({
    text = "粘贴",
    x = 550, y = 170, width = 80, height = 28,
    onClick = function(self, event)
        local text = gui.clipboardPaste()
        if text then
            editClipboard:setText(text)
            print("[剪贴板] 已粘贴:", text)
        else
            gui.messageBox("剪贴板为空或包含非文本数据", "提示", "ok|icon_warning")
        end
    end
})

-- 窗口控制演示
local lblWinControl = mainWindow:createLabel({
    text = "窗口控制:",
    x = 460, y = 210, width = 80, height = 22
})

local btnOpacity = mainWindow:createButton({
    text = "半透明效果",
    x = 550, y = 208, width = 120, height = 28,
    onClick = function(self, event)
        static_opacity = (static_opacity or 255) - 30
        if static_opacity < 100 then static_opacity = 255 end
        mainWindow:setOpacity(static_opacity)
        print(string.format("[窗口] 不透明度设置为: %d", static_opacity))
    end
})
local static_opacity = 255

local btnCenter = mainWindow:createButton({
    text = "居中显示",
    x = 685, y = 208, width = 100, height = 28,
    onClick = function(self, event)
        mainWindow:center(false)
        print("[窗口] 已居中")
    end
})

local btnFlash = mainWindow:createButton({
    text = "闪烁提醒",
    x = 795, y = 208, width = 80, height = 28,
    onClick = function(self, event)
        mainWindow:flash(5)
        print("[窗口] 闪烁提醒")
    end
})

-- 消息框类型演示
local lblMsgBox = mainWindow:createLabel({
    text = "消息框类型:",
    x = 460, y = 248, width = 80, height = 22
})

local btnMsgInfo = mainWindow:createButton({
    text = "信息",
    x = 550, y = 246, width = 60, height = 26,
    onClick = function(self, event)
        gui.messageBox("这是一条信息消息。\n用于向用户展示一般性信息。", "信息", "ok|icon_info")
    end
})

local btnMsgWarn = mainWindow:createButton({
    text = "警告",
    x = 620, y = 246, width = 60, height = 26,
    onClick = function(self, event)
        gui.messageBox("这是一条警告消息。\n用户需要注意潜在的问题。", "警告", "ok|icon_warning")
    end
})

local btnMsgError = mainWindow:createButton({
    text = "错误",
    x = 690, y = 246, width = 60, height = 26,
    onClick = function(self, event)
        gui.messageBox("发生了一个错误！\n请检查您的输入并重试。", "错误", "ok|icon_error")
    end
})

local btnMsgConfirm = mainWindow:createButton({
    text = "确认",
    x = 760, y = 246, width = 60, height = 26,
    onClick = function(self, event)
        local result = gui.messageBox("确定要执行此操作吗？\n此操作不可撤销。", "确认", "yesno|icon_question")
        if result == 6 then
            print("[确认] 用户点击了'是'")
        else
            print("[确认] 用户点击了'否'")
        end
    end
})

-- 系统信息面板
local infoGroup = mainWindow:createGroupbox({
    text = "系统信息",
    x = 445, y = 350, width = 435, height = 130
})

local lblVersion = mainWindow:createLabel({
    text = "GUI版本: " .. gui.VERSION,
    x = 460, y = 375, width = 300, height = 20
})

local lblPlatform = mainWindow:createLabel({
    text = "运行平台: Windows (" .. gui.PLATFORM .. ")",
    x = 460, y = 400, width = 300, height = 20
})

local lblStatus = mainWindow:createLabel({
    text = "状态: GUI系统运行中",
    x = 460, y = 425, width = 300, height = 20
})

local lblControls = mainWindow:createLabel({
    text = "控件数量: 0",
    x = 460, y = 450, width = 300, height = 20
})

-- 底部状态栏区域
local statusGroup = mainWindow:createGroupbox({
    text = "",
    x = 10, y = 635, width = 870, height = 3
}) -- 仅用作分隔线

-- 路径显示标签（用于文件对话框结果）
local editPath = mainWindow:createEdit({
    text = "",
    x = 100, y = 128, width = 300, height = 24
})
editPath:setVisible(false) -- 默认隐藏，仅在需要时显示

-- ============================================================
-- 动态更新演示
-- ============================================================

-- 模拟进度条动画
local progress_value = 0
local progress_direction = 1

-- 定时器回调（如果实现了的话）
-- 这里用简单的计数方式模拟

print("\n=== 控件统计 ===")
print("已创建以下控件:")
print("- 按钮: 14个")
print("- 标签: 15个")
print("- 输入框: 5个")
print("- 复选框: 2个")
print("- 单选按钮: 3个")
print("- 进度条: 1个")
print("- 滑块: 1个")
print("- 列表框: 1个")
print("- 组合框: 1个")
print("- 文本框: 1个")
print("- 分组框: 4个")
print("总计: 约48个控件")

print("\n=== 准备就绪 ===")
print("正在显示主窗口...")
print("进入消息循环...")

-- 显示窗口并进入主循环
mainWindow:show()

-- 进入GUI主消息循环（阻塞）
local exit_code = gui.run()

print("\n=== 程序退出 ===")
print("退出代码:", exit_code)

-- 清理资源
gui.cleanup()

print("GUI系统已清理完毕")
print("感谢使用LXCLUA GUI!")
