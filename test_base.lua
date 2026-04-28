-- 基础测试：不使用混淆
local tcc = require "tcc"

print("=== 基础TCC测试（无混淆）===")

local code = [[
function add(a, b)
    return a + b
end
return add(3, 5)
]]

local c_code = tcc.compile(code, {obfuscate = false}, "test_base")
if c_code then
    print("编译成功! C代码长度: " .. #c_code)
    
    local f = io.open("tests/test_base.c", "w")
    f:write(c_code)
    f:close()
    
    local ret = os.execute("gcc -O2 -shared -fPIC -o tests/test_base.so tests/test_base.c -I. 2>&1")
    print("GCC返回: " .. tostring(ret))
    
    if ret == true or ret == 0 then
        package.cpath = "./tests/?.so;" .. package.cpath
        local ok, res = pcall(require, "test_base")
        if ok then
            print("加载成功! 结果=" .. tostring(res))
        else
            print("加载失败: " .. tostring(res))
        end
    else
        print("GCC编译失败")
    end
else
    print("tcc.compile 返回 nil")
end

print("\n=== 测试完成 ===")
