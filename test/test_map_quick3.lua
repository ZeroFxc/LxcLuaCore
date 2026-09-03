-- 模拟 test() 函数调用
local function test(name, fn)
  local ok, err = pcall(fn)
  if ok then print("PASS:", name) else print("FAIL:", name, err) end
end

test("map中 [expr] key 与表访问不冲突", function()
  local t = {x = 10}
  local m = [ [t["x"]] = "from_table" ]
  assert(m[10] == "from_table")
end)