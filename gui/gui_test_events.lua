--[[
    @file gui_test_events.lua
    @brief LXCLUA GUI 全面功能测试 v5 (大间距版)
    @author DiferLine
]]

local gui = require("gui")
gui.init()
print("=== GUI 功能测试 v5 ===")

local outputBox = nil
local lblComboInfo = nil
local lblSliderVal = nil
local lblRadio = nil

local win = gui.createWindow({
    title = "LXCLUA GUI - 全功能测试 v5",
    width = 1020, height = 850,
    centerScreen = true,
    resizable = true,
    bgColor = {r=245, g=248, b=250, a=255}
})

-- 提前创建被闭包引用的控件
outputBox = win:createTextbox({
    text="=== LXCLUA GUI 功能测试日志 ===\n系统已初始化，等待交互...\n\n",
    x=540, y=310, width=450, height=280
})
lblComboInfo = win:createLabel({text="选中: --", x=735, y=58, width=110, height=20})
lblSliderVal = win:createLabel({text="值: 70 | 70%", x=340, y=490, width=120, height=20})
lblRadio = win:createLabel({text="主题: 默认(浅色)", x=800, y=218, width=150, height=20})

-- ══════════════════════════════════════════════════════
-- 左侧列 (x: 18~510)
-- ══════════════════════════════════════════════════════

-- 区域1: 按钮事件 (y=14~108)
win:createLabel({text="【按钮事件】", x=18, y=16, width=130, height=22, fontSize=13})

local clickCount = 0
local lblCount = win:createLabel({text="点击次数: 0", x=18, y=42, width=140, height=20})

local btnMain = win:createButton({
    text = "点击测试!", x=18, y=68, width=145, height=36,
    onClick = function(self, event)
        clickCount = clickCount + 1
        lblCount:setText("点击次数: " .. clickCount)
        outputBox:appendText("[按钮] 第" .. clickCount .. "次点击!\n")
        if clickCount % 2 == 1 then self:setText("再点一次!") else self:setText("点击测试!") end
        if clickCount >= 5 then win:flash(2) end
    end
})

local btnMsg = win:createButton({
    text = "消息框测试", x=173, y=68, width=115, height=36,
    onClick = function(self, event)
        local r = gui.messageBox("请选择一个操作:\n\n确定=OK\n取消=Cancel\n中止/重试/忽略", "对话框测试", "abortretrycancel|icon_question")
        local names = {[3]="中止(IDABORT)", [4]="重试(IDRETRY)", [5]="忽略(IDIGNORE)", [2]="取消(IDCANCEL)", [1]="确定(IDOK)", [0]="无"}
        outputBox:appendText("[对话框] 返回值 " .. r .. " = " .. (names[r] or "未知") .. "\n")
    end
})

-- 区域2: 列表框 (y=122~385)
win:createLabel({text="【列表框】", x=18, y=124, width=80, height=22, fontSize=13})

local listBox = win:createListbox({
    x=18, y=150, width=210, height=165,
    onChange = function(self, event)
        local idx = self:getSelectedIndex()
        if idx >= 0 then
            outputBox:appendText("[列表] 选中#"..idx..": "..self:getItemText(idx).."\n")
            if lblListInfo then lblListInfo:setText("选中: #"..idx.." "..self:getItemText(idx)) end
        end
    end
})

for _, item in ipairs({"Lua", "Python", "C/C++", "Rust", "Go", "Java", "JavaScript", "TypeScript"}) do
    listBox:addItem(item)
end

lblListInfo = win:createLabel({text="总数:8 | 选中:无", x=18, y=320, width=210, height=20})

local editNew = win:createEdit({text="新项...", x=18, y=346, width=130, height=26})
win:createButton({text="+添加", x=153, y=345, width=68, height=28,
    onClick = function(self)
        local t = editNew:getText(); if #t>0 then listBox:addItem(t); editNew:setText(""); lblListInfo:setText("总数:"..listBox:getCount()) end
    end
})
win:createButton({text="-删除", x=226, y=345, width=62, height=28,
    onClick = function(self)
        local i = listBox:getSelectedIndex(); if i>=0 then listBox:removeItem(i); lblListInfo:setText("总数:"..listBox:getCount()) end
    end
})
win:createButton({text="清空", x=226, y=379, width=62, height=24,
    onClick = function(self) listBox:clear(); lblListInfo:setText("总数:0") end
})

-- 区域5: 进度条 & 滑块 (y=420~520)
win:createLabel({text="【进度条 & 滑块】", x=18, y=422, width=155, height=22, fontSize=13})

local progress = win:createProgressbar({x=18, y=448, width=310, height=24})
progress:setRange(0, 100)
progress:setValue(45)

local lblProg = win:createLabel({text="进度: 45%", x=335, y=450, width=78, height=20})

win:createButton({text="随机设置", x=420, y=445, width=80, height=28,
    onClick = function(self)
        local v = math.random(0, 100)
        progress:setValue(v); lblProg:setText("进度: "..v.."%")
        outputBox:appendText("[进度条] 设为 "..v.."%\n")
    end
})

win:createButton({text="动画演示", x=336, y=477, width=80, height=28,
    onClick = function(self)
        outputBox:appendText("[进度条] 开始动画...\n")
        for i = 0, 100, 5 do progress:setValue(i); lblProg:setText("进度: "..i.."%"); gui.sleep(30) end
        outputBox:appendText("[进度条] 完成!\n")
    end
})

win:createLabel({text="音量:", x=18, y=486, width=44, height=20})

local slider = win:createSlider({
    x=65, y=482, width=300, height=32,
    onChange = function(self, event)
        local v = self:getValue()
        lblSliderVal:setText("值: "..v.." | "..v.."%")
        outputBox:appendText("[滑块] 值变为: "..v.."\n")
    end
})
slider:setRange(0, 100)
slider:setValue(70)
slider:setTickFreq(10)

-- ══════════════════════════════════════════════════════
-- 右侧列 (x: 540~990) - 每个区块间隔≥35px
-- ══════════════════════════════════════════════════════

-- 右侧区域1: 下拉框 (y=12~60) - 紧凑高度
win:createLabel({text="【下拉框】", x=540, y=16, width=80, height=22, fontSize=13})

local combo = win:createCombobox({
    x=540, y=42, width=175, height=28,
    onChange = function(self, event)
        local idx = self:getSelectedIndex()
        if idx >= 0 then
            outputBox:appendText("[下拉框] 选中: "..self:getItemText(idx).."\n")
            lblComboInfo:setText("选中: "..self:getItemText(idx))
        end
    end
})

for _, item in ipairs({"Windows", "Linux", "macOS", "Android", "iOS"}) do
    combo:addItem(item)
end
combo:setSelectedIndex(0)

-- 右侧区域2: 复选框 (y=98~138) - 与下拉框间隔38px
win:createLabel({text="【复选框】", x=540, y=100, width=85, height=22, fontSize=13})

local chk1 = win:createCheckbox({text="启用日志", x=540, y=124, width=95, height=24,
    onClick = function(self) outputBox:appendText("[复选框] 日志:"..tostring(self:getChecked()).."\n") end
})
chk1:setChecked(true)

local chk2 = win:createCheckbox({text="自动保存", x=650, y=124, width=90, height=24,
    onClick = function(self) outputBox:appendText("[复选框] 自动保存:"..tostring(self:getChecked()).."\n") end
})

local chk3 = win:createCheckbox({text="暗色模式", x=758, y=124, width=92, height=24,
    onClick = function(self) outputBox:appendText("[复选框] 暗色:"..tostring(self:getChecked()).."\n") end
})

win:createButton({text="反选全部", x=862, y=122, width=78, height=26,
    onClick = function(self)
        chk1:setChecked(not chk1:getChecked())
        chk2:setChecked(not chk2:getChecked())
        chk3:setChecked(not chk3:getChecked())
        outputBox:appendText("[复选框] 已反选全部\n")
    end
})

-- 右侧区域3: 主题选择 (y=172~212) - 与复选框间隔34px
win:createLabel({text="【主题选择】", x=540, y=174, width=100, height=22, fontSize=13})

local themes = {
    light = {r=245, g=248, b=250, a=255},
    dark = {r=30, g=30, b=30, a=255},
    highcontrast = {r=0, g=0, b=128, a=255}
}

local rad1 = win:createRadio({text="默认(浅色)", x=540, y=198, width=95, height=22,
    onClick = function(self)
        win:setBgColor(themes.light)
        lblRadio:setText("主题: 默认(浅色)")
        outputBox:appendText("[主题] 切换到: 默认(浅色)\n")
    end
})
rad1:setChecked(true)

local rad2 = win:createRadio({text="深色模式", x=645, y=198, width=88, height=22,
    onClick = function(self)
        win:setBgColor(themes.dark)
        lblRadio:setText("主题: 深色模式")
        outputBox:appendText("[主题] 切换到: 深色\n")
    end
})

local rad3 = win:createRadio({text="高对比度", x=743, y=198, width=85, height=22,
    onClick = function(self)
        win:setBgColor(themes.highcontrast)
        lblRadio:setText("主题: 高对比度")
        outputBox:appendText("[主题] 切换到: 高对比度\n")
    end
})

-- 右侧区域4: 窗口事件 (y=248~292) - 与主题间隔36px
win:createLabel({text="【窗口事件】", x=540, y=250, width=100, height=22, fontSize=13})

local resizeCnt = 0
local moveCnt = 0
local lblWinEvt = win:createLabel({text="resize:0 | move:0", x=540, y=274, width=280, height=38})

win:onEvent("size", function(self, event)
    resizeCnt = resizeCnt + 1
    lblWinEvt:setText(string.format("resize:%d | move:%d | %dx%d",
        resizeCnt, moveCnt, event.width, event.height))
end)

win:onEvent("move", function(self, event)
    moveCnt = moveCnt + 1
    lblWinEvt:setText(string.format("resize:%d | move:%d | (%d,%d)",
        resizeCnt, moveCnt, event.x, event.y))
end)

win:onEvent("keydown", function(self, event)
    if event.key == 27 or event.key == 13 or event.key == 9 or event.key > 111 then
        outputBox:appendText("[键盘] key="..event.key.."\n")
    end
end)

-- 右侧区域6: 日志控制按钮 (日志框下方 y=600~630)
win:createButton({text="清空", x=540, y=600, width=60, height=26,
    onClick = function(self) outputBox:clear(); outputBox:appendText("日志已清空\n") end
})
win:createButton({text="复制", x=607, y=600, width=60, height=26,
    onClick = function(self) outputBox:appendText("[剪贴板] "..outputBox:getText():sub(1,50).."...\n") end
})
win:createButton({text="文件", x=674, y=600, width=60, height=26,
    onClick = function(self) outputBox:appendText("[文件] 文件对话框(Linux GTK)\n") end
})
win:createButton({text="目录", x=741, y=600, width=60, height=26,
    onClick = function(self) outputBox:appendText("[目录] 目录对话框(Linux GTK)\n") end
})
win:createButton({text="颜色", x=808, y=600, width=60, height=26,
    onClick = function(self) gui.messageBox("颜色选择器需要额外实现", "提示", "ok|icon_info") end
})
win:createButton({text="字体", x=875, y=600, width=60, height=26,
    onClick = function(self) gui.messageBox("字体选择器演示", "提示", "ok|icon_info") end
})
win:createButton({text="关于", x=942, y=600, width=52, height=26,
    onClick = function(self)
        gui.messageBox("LXCLUA GUI v1.0\n跨平台GUI系统\nWindows(Win32) / Linux(GTK3)\n\nBuilt with love by DiferLine",
            "关于", "ok|icon_information")
    end
})

-- 底部状态栏 (y=650~688)
win:createGroupbox({text="", x=10, y=652, width=992, height=3})

local lblStatus = win:createLabel({
    text="就绪 | 控件数~30 | 点击各控件进行交互测试 | 窗口拖动/缩放会触发事件",
    x=18, y=661, width=780, height=22
})

win:createButton({text="退出程序", x=900, y=656, width=96, height=30,
    onClick = function(self)
        if gui.messageBox("确定退出?\nGUI系统将被清理。", "确认", "yesno|icon_question")==6 then
            win:close()
        end
    end
})

-- 启动
outputBox:appendText("[系统] 所有控件创建完成，布局: 大间距双栏\n")
win:show()
btnMain:setFocus()

print("进入消息循环...")
local code = gui.run()
print("退出代码:", code)
gui.cleanup()
print("测试完成!")
