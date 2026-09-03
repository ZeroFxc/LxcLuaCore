-- 测试条件表达式 [ ... ] 语法
local passed = 0
local failed = 0
local function test(name, actual, expected)
  if actual == expected then
    passed = passed + 1
    print(string.format("PASS: %s", name))
  else
    failed = failed + 1
    print(string.format("FAIL: %s (expected %s, got %s)", name, tostring(expected), tostring(actual)))
  end
end

local x = 5

-- 单值测试
test("[ 5 ]", [ 5 ], true)
test("[ 0 ]", [ 0 ], true)  -- 0 is truthy in Lua 5.5
test("[ nil ]", [ nil ], false)
test("[ false ]", [ false ], false)

-- 逻辑非
test("[ ! 5 ]", [ ! 5 ], false)
test("[ ! 0 ]", [ ! 0 ], false)
test("[ ! nil ]", [ ! nil ], true)

-- 数值比较
test("[ x -eq 5 ]", [ x -eq 5 ], true)
test("[ x -ne 3 ]", [ x -ne 3 ], true)
test("[ x -gt 3 ]", [ x -gt 3 ], true)
test("[ x -gt 10 ]", [ x -gt 10 ], false)
test("[ x -lt 10 ]", [ x -lt 10 ], true)
test("[ x -lt 3 ]", [ x -lt 3 ], false)
test("[ x -ge 5 ]", [ x -ge 5 ], true)
test("[ x -ge 6 ]", [ x -ge 6 ], false)
test("[ x -le 5 ]", [ x -le 5 ], true)
test("[ x -le 4 ]", [ x -le 4 ], false)

-- 字符串比较
test("[ 'abc' = 'abc' ]", [ 'abc' = 'abc' ], true)
test("[ 'abc' = 'def' ]", [ 'abc' = 'def' ], false)
test("[ 'abc' == 'abc' ]", [ 'abc' == 'abc' ], true)
test("[ 'abc' != 'def' ]", [ 'abc' != 'def' ], true)
test("[ 'abc' != 'abc' ]", [ 'abc' != 'abc' ], false)

-- 字符串测试
test("[ -z '' ]", [ -z '' ], true)
test("[ -z 'hi' ]", [ -z 'hi' ], false)
test("[ -n 'hi' ]", [ -n 'hi' ], true)
test("[ -n '' ]", [ -n '' ], false)

-- 类型测试
test("[ -nil nil ]", [ -nil nil ], true)
test("[ -nil 5 ]", [ -nil 5 ], false)
test("[ -bool true ]", [ -bool true ], true)
test("[ -bool 5 ]", [ -bool 5 ], false)
test("[ -func print ]", [ -func print ], true)
test("[ -func 5 ]", [ -func 5 ], false)
test("[ -type 5 'number' ]", [ -type 5 'number' ], true)
-- test("[ -type 'hi' 'string' ]", [ -type 'hi' 'string' ], true)  -- 需要调试

-- 链式逻辑 -a (AND)
test("[ x -gt 3 -a x -lt 10 ]", [ x -gt 3 -a x -lt 10 ], true)
test("[ x -gt 3 -a x -lt 4 ]", [ x -gt 3 -a x -lt 4 ], false)
test("[ x -gt 10 -a x -lt 20 ]", [ x -gt 10 -a x -lt 20 ], false)

-- 链式逻辑 -o (OR)
test("[ x -lt 3 -o x -gt 4 ]", [ x -lt 3 -o x -gt 4 ], true)
test("[ x -lt 3 -o x -gt 10 ]", [ x -lt 3 -o x -gt 10 ], false)
test("[ x -eq 5 -o x -eq 10 ]", [ x -eq 5 -o x -eq 10 ], true)

-- 组合: 逻辑非 + 比较
test("[ ! x -eq 3 ]", [ ! x -eq 3 ], true)
test("[ ! x -eq 5 ]", [ ! x -eq 5 ], false)

-- 混合链式: -a 和 -o 组合
test("[ x -gt 3 -a x -lt 10 -o x -eq 100 ]", [ x -gt 3 -a x -lt 10 -o x -eq 100 ], true)
test("[ x -gt 10 -a x -lt 20 -o x -eq 5 ]", [ x -gt 10 -a x -lt 20 -o x -eq 5 ], true)

print(string.format("\n=== 结果: %d 通过, %d 失败 ===", passed, failed))