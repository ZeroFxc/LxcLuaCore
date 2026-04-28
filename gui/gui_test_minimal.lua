local gui = require("gui")
gui.init()

local win = gui.createWindow({
    title="Linux GUI 最小测试",
    width=400, height=300,
    centerScreen=true
})

win:show()

-- 测试1: 创建按钮并调用方法
print("=== 测试1: 按钮方法 ===")
local btn = win:createButton({text="测试按钮", x=10, y=10, width=100, height=30})
print("btn type:", type(btn))
print("btn.setText exists:", btn.setText ~= nil)
print("btn.getText exists:", btn.getText ~= nil)

if btn.setText then
    btn:setText("修改后")
    print("setText OK, text:", btn:getText())
else
    print("ERROR: setText is nil!")
end

-- 测试2: 创建标签
print("\n=== 测试2: 标签方法 ===")
local lbl = win:createLabel({text="测试标签", x=10, y=50, width=100, height=20})
print("lbl.setText exists:", lbl.setText ~= nil)
if lbl.setText then
    lbl:setText("中文测试")
    print("setLabel OK")
else
    print("ERROR: label setText is nil!")
end

-- 测试3: 创建下拉框
print("\n=== 测试3: 下拉框方法 ===")
local combo = win:createCombobox({x=10, y=80, width=150, height=24})
print("combo.addItem exists:", combo.addItem ~= nil)
print("combo.getSelectedIndex exists:", combo.getSelectedIndex ~= nil)
if combo.addItem then
    combo:addItem("选项1")
    combo:addItem("选项2")
    print("addItem OK")
else
    print("ERROR: addItem is nil!")
end
if combo.getSelectedIndex then
    local idx = combo:getSelectedIndex()
    print("getSelectedIndex OK:", idx)
else
    print("ERROR: getSelectedIndex is nil!")
end

-- 测试4: 复选框
print("\n=== 测试4: 复选框方法 ===")
local chk = win:createCheckbox({text="复选框", x=10, y=110, width=80, height=20})
print("chk.getChecked exists:", chk.getChecked ~= nil)
print("chk.setChecked exists:", chk.setChecked ~= nil)
if chk.getChecked then
    local v = chk:getChecked()
    print("getChecked OK:", v)
else
    print("ERROR: getChecked is nil!")
end

-- 测试5: 滑块
print("\n=== 测试5: 滑块方法 ===")
local slider = win:createSlider({x=10, y=140, width=200, height=28})
print("slider.getValue exists:", slider.getValue ~= nil)
print("slider.setValue exists:", slider.setValue ~= nil)
if slider.getValue then
    slider:setValue(50)
    local v = slider:getValue()
    print("getValue/setValue OK:", v)
else
    print("ERROR: getValue is nil!")
end

-- 测试6: 文本框
print("\n=== 测试6: 文本框方法 ===")
local tb = win:createTextbox({x=10, y=180, width=200, height=60})
print("tb.appendText exists:", tb.appendText ~= nil)
if tb.appendText then
    tb:appendText("测试文本\n")
    print("appendText OK")
else
    print("ERROR: appendText is nil!")
end

print("\n=== 所有测试完成，进入消息循环 ===")
gui.run()
