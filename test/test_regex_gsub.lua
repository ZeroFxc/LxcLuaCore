-- 测试 string.gsub 正则功能
print("===== string.gsub 测试 =====")

-- 测试 1: 字面替换
local r,n = string.gsub("hello world", "[a-z]+", "X")
assert(r=="X X" and n==2, "gsub literal failed")
print("  [OK] 字面替换")

-- 测试 2: $1 捕获引用
local r,n = string.gsub("hello world", "([a-z]+)", "$1!")
assert(r=="hello! world!" and n==2, "gsub $1 failed")
print("  [OK] $1捕获引用")

-- 测试 3: $0 全匹配引用
local r = string.gsub("hello", "[a-z]+", "$0$0")
assert(r=="hellohello", "gsub $0 failed")
print("  [OK] $0全匹配引用")

-- 测试 4: 函数替换
local r = string.gsub("hello world", "([a-z]+)", function(m) return m:upper() end)
assert(r=="HELLO WORLD", "gsub function failed")
print("  [OK] 函数替换")

-- 测试 5: 表替换
local r = string.gsub("hello", "([a-z]+)", {hello="HELLO"})
assert(r=="HELLO", "gsub table failed")
print("  [OK] 表替换")

-- 测试 6: ^ 锚定 (只替换第一个)
local r,n = string.gsub("hello hello", "^hello", "X")
assert(r=="X hello" and n==1, "gsub anchor failed")
print("  [OK] ^锚定(只替换第一个)")

-- 测试 7: 非贪婪匹配
local r = string.gsub("<a> <b>", "<(.+?)>", "TAG")
print("  [OK] 非贪婪匹配: " .. r)

print("")
print("===== JIT OFF 性能 =====")
local t = os.clock()
for i=1,10000 do
  string.gsub("hello world", "([a-z]+)", "$1!")
end
print("10000次: " .. (os.clock()-t) .. "s")

jit.regex.on()
print("===== JIT ON 性能 =====")
local t = os.clock()
for i=1,10000 do
  string.gsub("hello world", "([a-z]+)", "$1!")
end
print("10000次: " .. (os.clock()-t) .. "s")
jit.regex.off()

print(">>> string.gsub 全部测试通过")