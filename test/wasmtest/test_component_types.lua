-- 测试 component 值类型双向转换（bool/s32/u32/f32/f64/char/option/enum/flags/record/tuple）
local w = require("wasmtime")

local f = io.open("_types.wasm", "rb")
if not f then print("找不到 _types.wasm"); return end
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
        print(string.format("[OK] %-18s got=%s", name, tostring(got)))
    else
        print(string.format("[FAIL] %-18s got=%s want=%s", name, tostring(got), tostring(want)))
    end
end

-- bool
local bn = inst:getExport("bool-not")
check("bool_not(true)", bn:call(true), false)
check("bool_not(false)", bn:call(false), true)

-- s32
local as = inst:getExport("add-s32")
check("add_s32(-5,3)", as:call(-5, 3), -2)

-- u32
local au = inst:getExport("add-u32")
check("add_u32(20,22)", au:call(20, 22), 42)

-- f32 / f64
local mf = inst:getExport("mul-f32")
local r = mf:call(2.5, 4.0)
check("mul_f32(2.5,4)", math.abs(r - 10.0) < 1e-6 and 10.0 or r, 10.0)
local af = inst:getExport("add-f64")
check("add_f64(1.5,2.25)", af:call(1.5, 2.25), 3.75)

-- char
local cn = inst:getExport("char-next")
check("char_next(65)", cn:call(65), 66)

-- option<u32>
local op = inst:getExport("option-plus10")
check("option some(5)", op:call(5), 15)      -- 传整数 => Some
check("option none", op:call(nil), 0)         -- 传 nil => None

-- enum
local ep = inst:getExport("enum-passthru")
check("enum red", ep:call("red"), "red")
check("enum green", ep:call("green"), "green")

-- flags
local fc = inst:getExport("flags-count")
check("flags {a,c}", fc:call{a=true, c=true}, 2)
check("flags {b}", fc:call{b=true}, 1)
check("flags {}", fc:call{}, 0)

-- record{a,b}
local rs = inst:getExport("record-sum")
check("record{a=10,b=32}", rs:call{a=10, b=32}, 42)

-- tuple<u32,u32> 求和（单参数表）
local ts = inst:getExport("tuple-sum")
check("tuple{1,2}", ts:call{1, 2}, 3)
check("tuple{10,32}", ts:call{10, 32}, 42)

print(string.format("=== 通过 %d 项 ===", npass))
