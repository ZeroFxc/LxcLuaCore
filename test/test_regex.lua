-- 测试 str_gsub 替换
local r, n = string.gsub('hello world', '([a-z]+)', '$1!')
print('r=' .. r, 'n=' .. n)
assert(r == 'hello! world!', 'str_gsub $1 failed')
print('str_gsub $1: OK')

-- 测试 str_gsub 函数替换
local r2, n2 = string.gsub('hello world', '([a-z]+)', function(m) return m:upper() end)
print('r2=' .. r2, 'n2=' .. n2)
assert(r2 == 'HELLO WORLD', 'str_gsub function failed')
print('str_gsub function: OK')

-- 测试 str_gsub 表替换
local r3 = string.gsub('hello', '([a-z]+)', {hello='HELLO'})
print('r3=' .. r3)
assert(r3 == 'HELLO', 'str_gsub table failed')
print('str_gsub table: OK')

print('全部 str_gsub 测试通过')