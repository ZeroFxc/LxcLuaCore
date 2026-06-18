local r = 1..5
print("type:", type(r))
print("#r:", #r)
for i, v in ipairs(r) do
  print("  [" .. i .. "] = " .. tostring(v))
end
print("---")
local r2 = 10..20
print("#r2:", #r2)
for i, v in ipairs(r2) do
  print("  [" .. i .. "] = " .. tostring(v))
end