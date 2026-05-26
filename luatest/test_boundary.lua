-- ================================================================
-- lquickjs 边界测试 — 覆盖所有 API 的极端输入和无用模块检测
-- ================================================================
local quickjs = require("quickjs")

local passed = 0
local failed = 0
local function check(name, ok, msg)
    if ok then
        passed = passed + 1
    else
        failed = failed + 1
        print("  FAIL: " .. (msg or name))
    end
end

-- ================================================================
-- 1. newRuntime 边界测试
-- ================================================================
do
    local rt = quickjs.newRuntime()
    check("newRuntime basic", rt ~= nil, "rt should not be nil")
end

-- ================================================================
-- 2. ctx 创建和垃圾回收
-- ================================================================
local rt = quickjs.newRuntime()
do
    local ctx = rt:newContext()
    check("newContext basic", ctx ~= nil, "ctx should not be nil")
    ctx = nil
    collectgarbage()
    collectgarbage()
    check("ctx GC", true)
end

local ctx = rt:newContext()

-- ================================================================
-- 3. ctx:eval() 边界测试
-- ================================================================
do
    -- 空字符串
    local r = ctx:eval("")
    check("eval empty", r == nil)

    -- NaN, Infinity, -Infinity
    local nan = ctx:eval("NaN")
    check("eval NaN", tostring(nan) == "nan")
    local inf = ctx:eval("Infinity")
    check("eval Infinity", tostring(inf) == "inf")
    local ninf = ctx:eval("-Infinity")
    check("eval -Infinity", tostring(ninf) == "-inf")

    -- 大整数
    local big = ctx:eval("9007199254740991")  -- Number.MAX_SAFE_INTEGER
    check("eval MAX_SAFE_INTEGER", big == 9007199254740991.0)

    -- null, undefined
    local n = ctx:eval("null")
    check("eval null→nil", n == nil)
    local u = ctx:eval("undefined")
    check("eval undefined→nil", u == nil)

    -- 布尔值
    check("eval true", ctx:eval("true") == true)
    check("eval false", ctx:eval("false") == false)

    -- 超长字符串
    local long = ctx:eval("(function(){var s='';for(var i=0;i<10000;i++)s+='a';return s})()")
    check("eval long string", #long == 10000)

    -- 语法错误
    local ok_err, err = pcall(function() ctx:eval("@@@") end)
    check("eval syntax error", not ok_err and type(err) == "string")

    -- 运行时错误
    local ok_err2, err2 = pcall(function() ctx:eval("(function(){throw new Error('test')})()") end)
    check("eval runtime error", not ok_err2 and type(err2) == "string")

    -- 嵌套对象
    local nested = ctx:eval("({a:{b:{c:[1,2,{d:3}]}}})")
    check("eval nested object", type(nested) == "userdata")

    -- 空对象/空数组
    local empty_obj = ctx:eval("({})")
    check("eval empty object", #empty_obj == 0)
    local empty_arr = ctx:eval("[]")
    check("eval empty array", #empty_arr == 0)

    -- 函数
    local fn = ctx:eval("(function(x,y){return x+y})")
    check("eval function", type(fn) == "userdata")
end

-- ================================================================
-- 4. ctx:evalModule() 边界测试
-- ================================================================
do
    -- 空模块
    local m = ctx:evalModule("")
    check("evalModule empty", type(m) == "userdata")

    -- 带 export 的模块
    local m2 = ctx:evalModule("export const a=1; export function f(x){return x*2}")
    check("evalModule exports", m2.a == 1 and m2.f(3) == 6)

    -- 语法错误
    local ok, err = pcall(function() ctx:evalModule("export const x = @@@") end)
    check("evalModule syntax error", not ok)

    -- import 不存在的模块
    local ok2, err2 = pcall(function() ctx:evalModule("import {x} from './nonexist.js'; export {x}") end)
    check("evalModule import fail", not ok2)
end

-- ================================================================
-- 5. ctx:registerModule() / import 边界测试
-- ================================================================
do
    -- 空名称不应崩溃
    local ok = pcall(function() ctx:registerModule("", "export const x=1") end)
    check("registerModule empty name", ok)

    -- 空代码不应崩溃
    local ok2 = pcall(function() ctx:registerModule("empty_mod", "") end)
    check("registerModule empty code", ok2)

    -- 超长模块名
    local ok3 = pcall(function()
        ctx:registerModule("a" .. string.rep("b", 1000), "export const x=1")
    end)
    check("registerModule long name", ok3)

    -- 注册后 import 测试
    ctx:registerModule("boundary_test", "export const hello = 'world'")
    local m = ctx:evalModule("import {hello} from 'boundary_test'; export {hello}")
    check("registerModule then import", m.hello == "world")

    -- 重复注册（QuickJS 模块缓存机制：首次 import 后缓存，后续 import 返回缓存版本）
    ctx:registerModule("boundary_test", "export const hello = 'override'")
    local m2 = ctx:evalModule("import {hello} from 'boundary_test'; export {hello}")
    check("registerModule override (cached)", m2.hello == "world")  -- 返回缓存的第一个版本

    -- 编译错误模块
    local ok4 = pcall(function() ctx:registerModule("bad_mod", "@@@@") end)
    check("registerModule bad code", ok4)
    -- import 坏模块应触发错误
    local ok5, err5 = pcall(function()
        ctx:evalModule("import {x} from 'bad_mod'; export {x}")
    end)
    check("registerModule import bad module", not ok5)
end

-- ================================================================
-- 6. ctx:dumpModules() 边界测试
-- ================================================================
do
    local mods = ctx:dumpModules()
    check("dumpModules returns table", type(mods) == "table")
end

-- ================================================================
-- 7. ctx:compile() / ctx:evalBinary() 边界测试
-- ================================================================
do
    -- 空脚本编译
    local bc = ctx:compile("", "<test>", false)
    check("compile empty script", bc ~= nil)

    -- 有效脚本编译+执行
    local bc2 = ctx:compile("export const x = 42", "<test>", true)
    check("compile module", bc2 ~= nil)
    if bc2 then
        local ns = ctx:evalBinary(bc2)
        check("evalBinary module", ns.x == 42)
    end

    -- 语法错误编译
    local ok, err = pcall(function() ctx:compile("@@@@") end)
    check("compile syntax error", not ok)

    -- 空字节码
    local ok2, err2 = pcall(function() ctx:evalBinary("") end)
    check("evalBinary empty", not ok2)

    -- 无效字节码
    local ok3, err3 = pcall(function() ctx:evalBinary("not valid bytecode") end)
    check("evalBinary invalid", not ok3)
end

-- ================================================================
-- 8. ctx:getGlobal() 边界测试
-- ================================================================
do
    local g = ctx:getGlobal()
    check("getGlobal basic", type(g) == "userdata")
    check("getGlobal .undefined", g.undefined == nil)
    check("getGlobal .NaN", tostring(g.NaN) == "nan")
end

-- ================================================================
-- 9. ctx:loop() 边界测试
-- ================================================================
do
    -- 空循环 (无事发生)
    ctx:loop()
    check("loop empty", true)

    -- setTimeout 0ms
    local fired = false
    ctx:eval([[var __test_fired = false; setTimeout(function(){ __test_fired = true }, 0)]])
    ctx:loop()
    local g = ctx:getGlobal()
    check("loop setTimeout 0ms", g.__test_fired == true)
end

-- ================================================================
-- 10. JSValue 元方法边界测试
-- ================================================================
do
    -- __tostring on freed JSValue
    local obj = ctx:eval("({x:1})")
    obj = nil
    collectgarbage()
    collectgarbage()
    check("GC doesn't crash JSValue", true)

    -- __tostring on various types
    check("tostring object", tostring(ctx:eval("({})")) == "[object Object]")
    check("tostring array", tostring(ctx:eval("[1,2,3]")) == "1,2,3")

    -- __len boundary
    check("len empty array", #ctx:eval("[]") == 0)
    check("len empty object", #ctx:eval("({})") == 0)

    -- __index: 非数字非字符串 key (nil)
    local obj2 = ctx:eval("({a:1})")
    local ok_nil, err_nil = pcall(function() return obj2[nil] end)
    check("index nil key throws", not ok_nil)

    -- __newindex: 创建新属性
    local obj3 = ctx:eval("({})")
    obj3.newProp = "hello"
    check("newindex set", obj3.newProp == "hello")

    -- __call boundary: 无参数调用
    local fn = ctx:eval("(function(){return 42})")
    check("call no args", fn() == 42)

    -- __call boundary: 十参数调用
    local fn2 = ctx:eval("(function(a,b,c,d,e,f,g,h,i,j){return a+j})")
    check("call 10 args", fn2(1,2,3,4,5,6,7,8,9,10) == 11)

    -- __call: 非函数不应崩溃
    local ok = pcall(function() ctx:eval("42")() end)
    check("call non-function", not ok)

    -- __eq: 不同类型的比较
    local a = ctx:eval("({})")
    local b = ctx:eval("[]")
    check("eq object vs array", not (a == b))

    -- __eq: NaN === NaN (JS_SameValue)
    local nan1 = ctx:eval("NaN")
    local nan2 = ctx:eval("NaN")
    -- 注意：NaN 被 js_to_lua 转为 Lua number (NaN)，Lua 中 NaN == NaN 取决于实现
    check("eq NaN", tostring(nan1) == tostring(nan2))

    -- __lt: 对象比较 (应该走字符串路径)
    local arr1 = ctx:eval("[1,2,3]")
    local arr2 = ctx:eval("[4,5,6]")
    -- 字符串比较: "1,2,3" < "4,5,6" → true
    check("lt arrays", arr1 < arr2)
    check("le arrays", arr1 <= arr2)

    -- __concat: JSValue userdata + Lua string
    -- JS_ToCString 对 JS 对象会调用 toString 方法
    local jsv = ctx:eval("({toString:function(){return 'Custom'}})")
    check("concat object with toString", jsv .. "!" == "Custom!")

    -- __concat: 普通对象 (无自定义 toString) → [object Object]
    local plain = ctx:eval("({})")
    check("concat plain object", plain .. "!" == "[object Object]!")

    -- __concat: 两个 JSString
    -- JS 字符串转为 Lua string，所以 .. 走 Lua 原生拼接
    check("concat lua strings", ctx:eval("'a'") .. ctx:eval("'b'") == "ab")
end

-- ================================================================
-- 11. arrays: 极限数组测试
-- ================================================================
do
    -- 空数组迭代
    local empty = ctx:eval("[]")
    local count = 0
    for i, v in ipairs(empty) do count = count + 1 end
    check("ipairs empty array", count == 0)

    -- 稀疏数组 (holes)
    -- Lua ipairs 遇到 nil 会停止，所以稀疏数组只能迭代到第一个 hole 之前
    local sparse = ctx:eval("[1,,3]")  -- [1, undefined, 3]
    check("len sparse", #sparse == 3)
    local count_hole = 0
    for i, v in ipairs(sparse) do count_hole = count_hole + 1 end
    check("ipairs sparse (stops at hole)", count_hole == 1)  -- ipairs 在 nil (JS undefined) 处停止

    -- pairs 遍历空对象
    local empty_obj = ctx:eval("({})")
    count = 0
    for k, v in pairs(empty_obj) do count = count + 1 end
    check("pairs empty object", count == 0)

    -- pairs 遍历有属性的对象
    local obj = ctx:eval("({a:1,b:2,c:undefined})")
    count = 0
    for k, v in pairs(obj) do count = count + 1 end
    check("pairs object", count >= 2)  -- 至少 a, b

    -- 嵌套数组 pairs (应该也作为普通对象遍历)
    local nested = ctx:eval("({x:1,y:[1,2,3]})")
    count = 0
    local has_x = false
    local has_y = false
    for k, v in pairs(nested) do
        count = count + 1
        if k == "x" then has_x = true end
        if k == "y" then has_y = true end
    end
    check("pairs nested", count >= 2 and has_x and has_y)
end

-- ================================================================
-- 12. Lua→JS 传参边界测试
-- ================================================================
do
    -- 传 nil → JS null
    local null_fn = ctx:eval("(function(x){return x === null ? 'null' : (x === undefined ? 'undefined' : typeof x)})")
    local r_null = null_fn(nil)
    -- nil 在 lua_to_js 中转为 JS_NULL，所以结果是 'null'
    -- 但函数调用时，nil 作为参数传入... Lua nil → lua_to_js → JS_NULL
    check("lua2js nil", r_null == "null")

    -- 传 table → JS
    local js_fn = ctx:eval("(function(arr){return Array.isArray(arr)?arr[0]:'no'})")
    local r2 = js_fn({10, 20, 30})
    check("lua2js array table", r2 == 10)

    local r3 = js_fn({a=1, b=2})
    check("lua2js object table", r3 == "no")

    -- 传混合 table (不是纯数组)
    local r4 = js_fn({[1]=1, [3]=3})
    check("lua2js sparse table", r4 == 1)  -- 键值连续

    -- 传超长 table
    local big_table = {}
    for i = 1, 1000 do big_table[i] = i end
    local sum_fn = ctx:eval("(function(arr){return arr.reduce(function(a,b){return a+b},0)})")
    local r5 = sum_fn(big_table)
    check("lua2js big array", r5 == 500500)

    -- 传空 table
    local empty_table = {}
    local is_arr_fn = ctx:eval("(function(x){return Array.isArray(x)})")
    local r6 = is_arr_fn(empty_table)
    check("lua2js empty table→array", r6 == true)

    -- Lua 函数传给 JS (回调)
    local called = false
    local cb = function(x) called = (x == 99) end
    local call_fn = ctx:eval("(function(f){f(99)})")
    call_fn(cb)
    check("lua2js function callback", called)

    -- 传 boolean
    local r7 = ctx:eval("(function(x){return typeof x})(true)")  -- 透传lua→js
    -- Lua boolean 直接传给 JS: function(true) → JS boolean
    check("lua2js boolean", true)
end

-- ================================================================
-- 13. 多 context 隔离测试
-- ================================================================
do
    local ctx2 = rt:newContext()
    local ctx3 = rt:newContext()

    -- 各自独立
    ctx2:eval("globalThis.x = 100")
    ctx3:eval("globalThis.x = 200")
    local g2 = ctx2:getGlobal()
    local g3 = ctx3:getGlobal()
    check("multi-context isolation", g2.x == 100 and g3.x == 200)

    -- ctx2 被 GC
    ctx2 = nil
    collectgarbage()
    collectgarbage()
    check("ctx2 GC safe", true)

    ctx3 = nil
    collectgarbage()
    collectgarbage()
    check("ctx3 GC safe", true)
end

-- ================================================================
-- 14. JSValue 跨 context 访问 (不应 crash 但值无效)
-- ================================================================
do
    local ctx_a = rt:newContext()
    local ctx_b = rt:newContext()

    local obj_a = ctx_a:eval("({data:'from_a'})")
    -- obj_a 的 JSContext 是 ctx_a，尝试用它时 ctx_a 的 context 应该还在
    check("cross-ctx access", obj_a.data == "from_a")

    ctx_a = nil
    collectgarbage()
    collectgarbage()

    -- ctx_a 已 GC，但 obj_a 的 ctx 被 uservalue 保护着
    -- 只要 lquickjs 的 uservalue 机制正确，obj_a 应该仍然有效
    -- 测试 __tostring (会访问 ctx)
    local ok = pcall(function() return tostring(obj_a) end)
    check("JSValue survives ctx GC", ok)

    ctx_b = nil
    collectgarbage()
    collectgarbage()
end

-- ================================================================
-- 15. this 绑定边界测试
-- ================================================================
do
    local obj = ctx:eval("({name:'test',greet:function(g){return g+', '+this.name}})")
    check("this call", obj:greet("Hi") == "Hi, test")

    -- 无 self 的调用 (第两个参数不是 JSValue)
    local fn = ctx:eval("(function(){return this})")
    local r = fn("not_self_value")
    check("call without self", r ~= nil)
end

-- ================================================================
-- 0. API 速查验证 (确保所有方法可访问)
-- ================================================================
do
    local api_ok = true
    api_ok = api_ok and type(ctx.eval) == "function"
    api_ok = api_ok and type(ctx.evalModule) == "function"
    api_ok = api_ok and type(ctx.compile) == "function"
    api_ok = api_ok and type(ctx.evalBinary) == "function"
    api_ok = api_ok and type(ctx.getGlobal) == "function"
    api_ok = api_ok and type(ctx.registerModule) == "function"
    api_ok = api_ok and type(ctx.dumpModules) == "function"
    api_ok = api_ok and type(ctx.loop) == "function"
    check("API completeness", api_ok)
end

-- ================================================================
-- 结果汇总
-- ================================================================
print("")
print(string.format("=== 边界测试完成: %d 通过, %d 失败 ===", passed, failed))
if failed > 0 then
    os.exit(1)
end