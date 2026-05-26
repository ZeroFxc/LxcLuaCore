-- 测试 setTimeout 是否可用
local q = require("quickjs")
local r = q.newRuntime()
local c = r:newContext()

-- 检查 setTimeout 是否存在
local t = c:eval("typeof setTimeout")
print("typeof setTimeout =", t)

-- 测试 setTimeout 回调
c:eval([[
  var fired = false;
  setTimeout(function() {
    print("JS: setTimeout fired!");
    fired = true;
  }, 100);
  print("JS: setTimeout scheduled, delay=100ms");
]])

-- 驱动事件循环，等待回调触发
print("Lua: pumping event loop...")
c:loop()

-- 检查回调是否执行
local fired = c:eval("fired")
print("Lua: fired =", fired)