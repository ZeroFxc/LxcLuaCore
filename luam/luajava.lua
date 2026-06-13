-- 假的 luajava 模块，仅用于测试语法解析，不实现实际功能
local luajava = {}

-- 创建一个带 __index 和 __call 的假对象
local function fakeClass()
  local obj = {}
  local mt = {
    __index = function(t, k) return function(...) return {} end end,
    __call = function(t, ...) return {} end,
  }
  setmetatable(obj, mt)
  return obj
end

function luajava.bindClass(name)
  return fakeClass()
end

function luajava.new(...)
  return fakeClass()
end

function luajava.astable(t)
  return {}
end

return luajava