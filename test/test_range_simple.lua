-- 简单测试范围操作符
local r = 1..5
print("type:", type(r))
print("value:", r)
if type(r) == "table" then
  print("#r:", #r)
  for i, v in ipairs(r) do
    print("  [" .. i .. "] = " .. v)
  end
end
print("done")