-- 测试 component string/list 参数方向（realloc 复制）
local w = require("wasmtime")

local f = io.open("_str.wasm", "rb")
if not f then print("找不到 _str.wasm"); return end
local bytes = f:read("*a")
f:close()

local engine = w.newEngine()
local store = w.newStore(engine)
local c = w.newComponent(engine, bytes)
assert(type(c) == "userdata", "newComponent 失败")

local linker = w.newComponentLinker(engine)
local inst, ierr = linker:instantiate(store, c)
assert(inst, "instantiate 失败: " .. tostring(ierr))

local npass = 0
local function check(name, got, want)
    if got == want then
        npass = npass + 1
        print(string.format("[OK] %-22s got=%s", name, tostring(got)))
    else
        print(string.format("[FAIL] %-22s got=%s want=%s", name, tostring(got), tostring(want)))
    end
end

-- string 参数（UTF-8 字节长度）
local sl = inst:getExport("string-len")
check('string-len("hello")', sl:call("hello"), 5)
check('string-len("")', sl:call(""), 0)
check('string-len("中文")', sl:call("中文"), 6)  -- UTF-8 每汉字 3 字节

-- list<u32> 参数
local ls = inst:getExport("list-sum")
check("list-sum({1,2,3,4})", ls:call{1, 2, 3, 4}, 10)
check("list-sum({})", ls:call{}, 0)
check("list-sum({7,8})", ls:call{7, 8}, 15)

print(string.format("=== 通过 %d 项 ===", npass))
