-- 测试 string.match 正则功能
print("===== string.match 测试 =====")

-- 测试 1: 捕获组
local r1,r2,r3 = string.match("hello 123 world", "([a-z]+)\\s+(\\d+)\\s+([a-z]+)")
assert(r1=="hello" and r2=="123" and r3=="world", "match capture failed")
print("  [OK] 捕获组")

-- 测试 2: 无匹配
local r = string.match("hello", "xyz")
assert(r==nil, "match no-match failed")
print("  [OK] 无匹配")

-- 测试 3: 单个捕获
local r = string.match("hello world", "([a-z]+)")
assert(r=="hello", "match single capture failed")
print("  [OK] 单个捕获")

-- 测试 4: 全匹配返回
local r = string.match("hello world", "[a-z]+")
assert(r=="hello", "match full match failed")
print("  [OK] 全匹配返回")

print("")
print("===== JIT OFF 性能 =====")
local t = os.clock()
for i=1,10000 do
  string.match("hello 123 world", "([a-z]+)\\s+(\\d+)\\s+([a-z]+)")
end
print("10000次: " .. (os.clock()-t) .. "s")

jit.regex.on()
print("===== JIT ON 性能 =====")
local t = os.clock()
for i=1,10000 do
  string.match("hello 123 world", "([a-z]+)\\s+(\\d+)\\s+([a-z]+)")
end
print("10000次: " .. (os.clock()-t) .. "s")
jit.regex.off()

print(">>> string.match 全部测试通过")