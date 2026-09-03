-- 测试 string.gfind 正则功能
print("===== string.gfind 测试 =====")

-- 测试 1: 基本迭代 (返回位置)
local t = {}
for s,e in string.gfind("hello world", "[a-z]+") do
  t[#t+1] = string.sub("hello world", s, e)
end
assert(t[1]=="hello" and t[2]=="world", "gfind basic failed")
print("  [OK] 基本迭代(返回位置)")

-- 测试 2: 带捕获组
local t = {}
for s,e,c1,c2 in string.gfind("hello 123 world 456", "([a-z]+)\\s+(\\d+)") do
  t[#t+1] = c1 .. c2
end
assert(t[1]=="hello123" and t[2]=="world456", "gfind capture failed")
print("  [OK] 捕获组")

print("")
print("===== JIT OFF 性能 =====")
local t = os.clock()
for i=1,10000 do
  for s,e in string.gfind("hello world", "[a-z]+") do end
end
print("10000次: " .. (os.clock()-t) .. "s")

jit.regex.on()
print("===== JIT ON 性能 =====")
local t = os.clock()
for i=1,10000 do
  for s,e in string.gfind("hello world", "[a-z]+") do end
end
print("10000次: " .. (os.clock()-t) .. "s")
jit.regex.off()

print(">>> string.gfind 全部测试通过")