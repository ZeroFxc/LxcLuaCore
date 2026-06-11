local parser = require("nativeparser")
local nv = require("nativevm")
local code = parser.compile([[
  local cnt = 0
  for i = 1, 500 do
    cnt = cnt + (i * 2 - i / 2 + i % 7)
  end
  return cnt
]])
print("ncode =", #code)
for i, inst in ipairs(code) do
  print(string.format("[%3d] %s", i, nv.disasm(inst)))
end
