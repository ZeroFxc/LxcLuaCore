local x = 5
-- 测试 -a 链
print("-a only:", [ x -gt 10 -a x -lt 20 ])  -- false AND true = false
print("-o only:", [ x -gt 10 -o x -eq 5 ])    -- false OR true = true
print("-a -o:", [ x -gt 10 -a x -lt 20 -o x -eq 5 ])  -- (false AND true) OR true = true