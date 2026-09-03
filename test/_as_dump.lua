-- dump bytecode for the as operator test
jit.off()

class A {}
class B extends A {}
local b = B()

-- 使用 debug 库获取 main 函数的字节码
local function dump_func(f, name)
  local i = 0
  while true do
    local success, op, pc = pcall(function()
      local info = debug.getinfo(f, "L")
      return "ok"
    end)
    if not success then break end
    i = i + 1
    if i > 50 then break end
  end
end

-- 直接测试 as 运算符
local a = b as A
print("RESULT_TYPE:", type(a))
print("RESULT:", a)
print("RESULT_IS_NIL:", a == nil)
print("RESULT_IS_BOOL:", type(a) == "boolean")