-- 测试 string.gmatch 正则功能
print("===== string.gmatch 测试 =====")

-- 测试 1: 基本迭代
local t = {}
for w in string.gmatch("hello world", "[a-z]+") do
  t[#t+1] = w
end
assert(t[1]=="hello" and t[2]=="world", "gmatch basic failed")
print("  [OK] 基本迭代")

-- 测试 2: 带捕获组
local t = {}
for a,b in string.gmatch("hello 123 world 456", "([a-z]+)\\s+(\\d+)") do
  t[#t+1] = a .. b
end
assert(t[1]=="hello123" and t[2]=="world456", "gmatch capture failed")
print("  [OK] 捕获组")

-- 测试 3: ^ 锚定 (只匹配一次)
local n = 0
for _ in string.gmatch("hello hello", "^hello") do n=n+1 end
assert(n==1, "gmatch anchor failed")
print("  [OK] ^锚定(只匹配一次)")

print("")
print("===== JIT OFF 性能 =====")
local t = os.clock()
for i=1,10000 do
  for w in string.gmatch("hello world", "[a-z]+") do end
end
print("10000次: " .. (os.clock()-t) .. "s")

jit.regex.on()
print("===== JIT ON 性能 =====")
local t = os.clock()
for i=1,10000 do
  for w in string.gmatch("hello world", "[a-z]+") do end
end
print("10000次: " .. (os.clock()-t) .. "s")
jit.regex.off()

print(">>> string.gmatch 全部测试通过")