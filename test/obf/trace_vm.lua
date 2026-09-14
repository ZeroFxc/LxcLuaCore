-- 调试：追踪 VM_PROTECT 执行过程
print("=== VM_PROTECT 执行追踪 ===")

-- 1. 简单函数
local src1 = "return 42"
local f1 = load(src1)
local bc1 = string.dump(f1, {strip=false, obfuscate=128})
io.open("_trace1.luac", "wb"):write(bc1):close()

-- 加载并执行
local f1b = loadfile("_trace1.luac", "bt")
print("1. 简单函数加载: type=" .. type(f1b))

local ok, res = pcall(f1b)
print("   pcall ok=" .. tostring(ok) .. ", type=" .. type(res))
if type(res) == "function" then
    print("   返回了函数! 尝试调用它...")
    local ok2, res2 = pcall(res)
    print("   调用结果: ok=" .. tostring(ok2) .. ", type=" .. type(res2))
    if type(res2) == "function" then
        local ok3, res3 = pcall(res2)
        print("   再调用: ok=" .. tostring(ok3) .. ", type=" .. type(res3))
    end
end

-- 2. 嵌套函数
local src2 = "local function outer(x) local function inner(y) return y * 2 end; return inner(x) + inner(x + 1) end; return outer(5)"
local f2 = load(src2)
local bc2 = string.dump(f2, {strip=false, obfuscate=128})
io.open("_trace2.luac", "wb"):write(bc2):close()

local f2b = loadfile("_trace2.luac", "bt")
print("\n2. 嵌套函数加载: type=" .. type(f2b))

local ok, res = pcall(f2b)
print("   pcall ok=" .. tostring(ok) .. ", type=" .. type(res) .. ", val=" .. tostring(res))

-- 3. 检查difierline_mode
print("\n3. 检查标志位")
local f3 = load("return 42")
local bc3 = string.dump(f3, {strip=false, obfuscate=128})
io.open("_trace3.luac", "wb"):write(bc3):close()

-- 用luaccheck查看
print("   运行: luaccheck _trace3.luac")
os.execute("luaccheck.exe _trace3.luac 2>&1")

print("\n=== 完成 ===")