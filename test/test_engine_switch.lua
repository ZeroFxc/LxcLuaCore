-- 验证双引擎真正切换：Lua 模式用 %d，PCRE2 用 \d，互不兼容
print("=== 引擎切换验证 ===")

-- 1. 默认原版 Lua 引擎
print("1. 默认状态 (PCRE2=" .. tostring(jit.regex.pcre2.status()) .. ")")
print("   Lua %d 匹配:", string.match("abc123", "(%d+)"))
print("   PCRE2 \\d 匹配:", string.match("abc123", "(\\d+)"))

-- 2. 切换到 PCRE2
jit.regex.pcre2.on()
print("2. PCRE2 开启 (PCRE2=" .. tostring(jit.regex.pcre2.status()) .. ")")
print("   Lua %d 匹配:", string.match("abc123", "(%d+)"))
print("   PCRE2 \\d 匹配:", string.match("abc123", "(\\d+)"))

-- 3. 切回原版 Lua
jit.regex.pcre2.off()
print("3. 切回原版 (PCRE2=" .. tostring(jit.regex.pcre2.status()) .. ")")
print("   Lua %d 匹配:", string.match("abc123", "(%d+)"))
print("   PCRE2 \\d 匹配:", string.match("abc123", "(\\d+)"))

print("=== 完成 ===")