-- 测试 Lua 模式 → PCRE2 转换层：用 Lua 正则语法 (%d, %w 等) 在 PCRE2 引擎下运行
print("=== Lua→PCRE2 转换层测试 ===")

-- 开启 PCRE2
jit.regex.pcre2.on()
print("PCRE2 已开启: " .. tostring(jit.regex.pcre2.status()))

-- 1. 基础字符类
print("1. %d 匹配数字:", string.match("abc123", "(%d+)"))
print("2. %w 匹配单词:", string.match("hello_world 123", "(%w+)"))
print("3. %s 匹配空白:", string.match("a b c", "(%s)"))
print("4. %a 匹配字母:", string.match("abc123", "(%a+)"))
print("5. %l 匹配小写:", string.match("AbcDef", "(%l+)"))
print("6. %u 匹配大写:", string.match("AbcDef", "(%u+)"))
print("7. %x 匹配十六进制:", string.match("FF00XX", "(%x+)"))
print("8. %p 匹配标点:", string.match("abc,def", "(%p)"))
print("9. %% 匹配百分号:", string.match("50%40", "(%%d)"))  -- %%d → 字面 %d

-- 2. 懒惰量词 -
print("10. 懒惰量词 .- :", string.match("abc123def", "(.-)(%d+)"))

-- 3. 捕获组和反向引用
print("11. 捕获组反向引用:", string.match("abab", "(%a%a)(%1)"))

-- 4. 锚点
print("12. ^ 锚点:", string.match("hello world", "^(%w+)"))

-- 5. string.find 带位置
local s, e = string.find("hello 123 world", "(%d+)")
print("13. string.find:", s, e)

-- 6. string.gsub 替换
print("14. string.gsub:", string.gsub("abc 123 def", "(%d+)", "XXX"))

-- 7. string.gmatch 遍历
print("15. string.gmatch:")
for w in string.gmatch("hello world lua", "(%w+)") do
    print("    " .. w)
end

print("=== 完成 ===")