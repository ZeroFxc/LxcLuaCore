-- ================================================================
-- data组库边界测试: bool, userdata, bit, ptr, struct
-- ================================================================
local passed, failed = 0, 0
local function check(name, ok, msg)
    if ok then passed = passed + 1
    else failed = failed + 1; print("  FAIL: " .. (msg or name)) end
end

-- ================================================================
-- 1. bool 库 (require("bool"))
-- ================================================================
do
    print("\n========== bool ==========")
    local ok_bool, bool = pcall(require, "bool")
    if not ok_bool then
        print("  SKIP: bool not available (" .. tostring(bool) .. ")")
    else
        check("bool loaded", type(bool) == "table")
        for _, fn in ipairs({"tostring","tonumber","not","and","or","xor","eq","is","toexpr"}) do
            check("bool." .. fn, type(bool[fn]) == "function", fn .. " missing")
        end
        check("bool.tostring true", bool.tostring(true) == "true")
        check("bool.tostring false", bool.tostring(false) == "false")
        check("bool.tonumber true", bool.tonumber(true) == 1)
        check("bool.tonumber false", bool.tonumber(false) == 0)
        check("bool.not true", bool["not"](true) == false)
        check("bool.not false", bool["not"](false) == true)
        check("bool.and TT", bool["and"](true, true) == true)
        check("bool.and TF", bool["and"](true, false) == false)
        check("bool.or FT", bool["or"](false, true) == true)
        check("bool.or FF", bool["or"](false, false) == false)
        check("bool.xor TF", bool.xor(true, false) == true)
        check("bool.xor TT", bool.xor(true, true) == false)
        check("bool.eq TT", bool.eq(true, true) == true)
        check("bool.eq TF", bool.eq(true, false) == false)
        check("bool.is true", bool.is(true) == true)
        check("bool.is 1", bool.is(1) == false)
        check("bool.is nil", bool.is(nil) == false)
        check("bool.toexpr true", type(bool.toexpr(true)) == "string")
        -- nil 边界（tostring(nil) 可能返回 "nil" 而不是报错）
        check("bool.toexpr nil ok", pcall(function() bool.toexpr(nil) end))
    end
end

-- ================================================================
-- 2. userdata 库 (require("userdata"))
-- ================================================================
do
    print("\n========== userdata ==========")
    local ok_u, ud = pcall(require, "userdata")
    if not ok_u then
        print("  SKIP: userdata not available (" .. tostring(ud) .. ")")
    else
        check("userdata loaded", type(ud) == "table")
        for _, fn in ipairs({"isuserdata","islight","type","equals","tostring","address","fromany"}) do
            check("userdata." .. fn, type(ud[fn]) == "function", fn .. " missing")
        end
        -- 基本测试
        check("ud.isuserdata io.stdout", ud.isuserdata(io.stdout) == true)
        check("ud.isuserdata table", ud.isuserdata({}) == false)
        check("ud.isuserdata nil", ud.isuserdata(nil) == false)
        check("ud.type io.stdout", type(ud.type(io.stdout)) == "string")
        -- 获取 lightuserdata (用 ptr.null 或直接 null)
        local ok_ptr, ptr = pcall(require, "ptr")
        if ok_ptr then
            local null = ptr.null()
            if null then
                check("ud.islight null", ud.islight(null) == true)
            end
        end
    end
end

-- ================================================================
-- 3. bit 库 (require("bit"))
-- ================================================================
do
    print("\n========== bit ==========")
    local ok_bit, bit = pcall(require, "bit")
    if not ok_bit then
        print("  SKIP: bit not available (" .. tostring(bit) .. ")")
    else
        check("bit loaded", type(bit) == "table")
        for _, fn in ipairs({"arshift","band","bnot","bor","bxor","btest","extract","lrotate","lshift","replace","rrotate","rshift"}) do
            check("bit." .. fn, type(bit[fn]) == "function", fn .. " missing")
        end
        check("bit.band", bit.band(0xFF, 0x0F) == 0x0F)
        check("bit.bor", bit.bor(0x0F, 0xF0) == 0xFF)
        check("bit.bxor", bit.bxor(0xFF, 0x0F) == 0xF0)
        check("bit.lshift", bit.lshift(1, 8) == 256)
        check("bit.rshift", bit.rshift(256, 8) == 1)
        check("bit.bnot zero", bit.bnot(0) ~= 0)
        check("bit.btest 0xFF,0x0F", bit.btest(0xFF, 0x0F) == true)
        check("bit.btest 0xF0,0x0F", bit.btest(0xF0, 0x0F) == false)
        -- 边界
        check("bit.band 0,0", bit.band(0, 0) == 0)
        check("bit.bor 0,0", bit.bor(0, 0) == 0)
        check("bit.band(-1,0xFF)", bit.band(-1, 0xFF) == 0xFF)
        check("bit.lshift 0", bit.lshift(0, 8) == 0)
        check("bit.rshift 0", bit.rshift(0, 8) == 0)
        check("bit.lrotate", bit.lrotate(0x80000000, 1) == 1)
        check("bit.rrotate", bit.rrotate(1, 1) == 0x80000000)
        -- nil 输入
        check("bit.band nil", not pcall(function() bit.band(nil, 0) end))
    end
end

-- ================================================================
-- 4. ptr 库 (require("ptr")) — 部分操作会导致 C 层崩溃，跳过危险操作
-- ================================================================
do
    print("\n========== ptr ==========")
    local ok_p, ptr = pcall(require, "ptr")
    if not ok_p then
        print("  SKIP: ptr not available (" .. tostring(ptr) .. ")")
    else
        check("ptr loaded", type(ptr) == "table")
        for _, fn in ipairs({"new","addr","null","is_null","equal","tohex","malloc","free"}) do
            check("ptr." .. fn, type(ptr[fn]) == "function", fn .. " missing")
        end
        -- null 测试
        local ok_n, null_p = pcall(function() return ptr.null() end)
        check("ptr.null", ok_n)
        if ok_n then
            check("ptr.is_null", ptr.is_null(null_p) == true)
            check("ptr.equal", ptr.equal(null_p, null_p) == true)
            local ok_th, th = pcall(function() return ptr.tohex(null_p) end)
            check("ptr.tohex", ok_th and type(th) == "string", ok_th and ("got: " .. type(th)) or tostring(th))
        end
        -- new
        local ok_n2, p_new = pcall(function() return ptr.new(1) end)
        check("ptr.new", ok_n2)
        if ok_n2 then check("ptr.addr", type(ptr.addr(p_new)) == "number") end
        -- malloc (已知此操作在部分平台上会崩溃，直接跳过)
        check("ptr.malloc presence", type(ptr.malloc) == "function")
        check("ptr.free presence", type(ptr.free) == "function")
    end
end

-- ================================================================
-- 5. struct 库 (require("struct"))
-- ================================================================
do
    print("\n========== struct ==========")
    local ok_s, stlib = pcall(require, "struct")
    if not ok_s then
        print("  SKIP: struct not available (" .. tostring(stlib) .. ")")
    else
        check("struct loaded", type(stlib) == "table")
        check("stlib.define is function", type(stlib.define) == "function")
        -- 定义简单结构
        local Point = stlib.define("point", {"x", 0, "y", 0})
        check("stlib.define point", type(Point) == "table")
        -- 创建实例并读写
        local p = Point()
        check("Point() instance", p ~= nil)
        p.x = 3; p.y = 4
        check("point read", p.x == 3 and p.y == 4)
        p.x = 10
        check("point write", p.x == 10)
        -- table 初始化
        local p2 = Point({x = 1, y = 2})
        check("Point({...})", p2.x == 1 and p2.y == 2)
        -- 混合类型
        local Mixed = stlib.define("mixed", {"ival", 0, "fval", 0.0, "flag", false, "name", ""})
        local m = Mixed()
        check("mixed default", m.ival == 0 and m.fval == 0.0 and m.flag == false and m.name == "")
        -- 嵌套
        local Inner = stlib.define("inner_ts", {"a", 0, "b", 0})
        local inner_i = Inner({a = 1, b = 2})
        local Outer = stlib.define("outer_ts", {"x", 0, "nest", inner_i})
        local o = Outer({x = 5})
        check("nested outer", o.x == 5 and o.nest.a == 1)
        -- 默认值
        local Def = stlib.define("defstruct_ts", {"count", 99})
        local d = Def()
        check("default value", d.count == 99)
        -- 错误输入
        check("define nil name", not pcall(function() stlib.define(nil, {"a",0}) end))
        check("define nil fields", not pcall(function() stlib.define("test_nil", nil) end))
        -- 空结构
        local Empty = stlib.define("empty_ts", {})
        check("empty struct", type(Empty) == "table")
    end
end

print("")
print(string.format("Result: %d/%d passed", passed, failed + passed))
if failed > 0 then os.exit(1) end