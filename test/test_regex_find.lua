-- 测试 string.find 正则功能
local err = 0

-- 测试 1: 纯文本匹配
local s,e = string.find("hello world", "world")
assert(s==7 and e==11, "find literal failed")
print("  [OK] 纯文本匹配")

-- 测试 2: PCRE2 正则匹配 (无捕获组)
local s,e = string.find("hello world", "[a-z]+\\s+[a-z]+")
assert(s==1 and e==11, "find regex failed")
print("  [OK] 正则匹配")

-- 测试 3: 带捕获组
local s,e,c1,c2 = string.find("hello world", "([a-z]+)\\s+([a-z]+)")
assert(s==1 and e==11 and c1=="hello" and c2=="world", "find capture failed")
print("  [OK] 捕获组")

-- 测试 4: ^ 锚定
local s,e = string.find("hello world", "^hello")
assert(s==1 and e==5, "find anchor ^ failed")
print("  [OK] ^锚定")

-- 测试 5: $ 锚定
local s,e = string.find("hello world", "world$")
assert(s==7 and e==11, "find anchor $ failed")
print("  [OK] $锚定")

-- 测试 6: 未匹配
local s,e = string.find("hello", "xyz")
assert(s==nil, "find no-match failed")
print("  [OK] 未匹配")

-- 测试 7: %d 数字匹配 (PCRE2 语法)
local s,e = string.find("abc123", "\\d+")
assert(s==4 and e==6, "find \\d+ failed")
print("  [OK] \\d数字匹配")

print("")
print("===== JIT OFF 测试 =====")
local t = os.clock()
for i=1,10000 do
  string.find("hello world", "([a-z]+)\\s+([a-z]+)")
end
local t_off = os.clock() - t
print("str_find 10000次: " .. t_off .. "s")

jit.regex.on()
print("===== JIT ON 测试 =====")
local t = os.clock()
for i=1,10000 do
  string.find("hello world", "([a-z]+)\\s+([a-z]+)")
end
local t_on = os.clock() - t
print("str_find 10000次: " .. t_on .. "s")
jit.regex.off()

print("")
print(">>> string.find 全部测试通过")