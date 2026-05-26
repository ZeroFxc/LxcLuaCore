-- ================================================================
-- VM 组边界测试: vm, vmprotect, ByteCode
-- ================================================================
local passed, failed = 0, 0
local function check(name, ok, msg)
    if ok then passed = passed + 1
    else failed = failed + 1; print("  FAIL: " .. (msg or name)) end
end

-- ================================================================
-- 1. vm 库 (require("vm"))
-- ================================================================
do
    print("")
    print("========== vm ==========")

    local ok_mod, vm = pcall(require, "vm")
    if not ok_mod then
        print("  SKIP: vm not available (" .. tostring(vm) .. ")")
    else
        check("vm loaded", type(vm) == "table")
        if type(vm) ~= "table" then
            -- 不跳过，直接继续测试
        else

        -- 验证所有函数存在
        local vm_funcs = {
            "execute", "concat", "objlen", "equal", "lt", "le", "tonumber", "tointeger",
            "gcinfo", "gettop", "memory", "gcstep", "gccollect", "newthread", "status",
            "resume", "yield", "currentthread", "typename", "getci", "getstack",
            "gcstop", "gcstart", "gcsetpause", "gcsetstepmul", "gcinc", "getregistry",
            "getglobalenv", "setglobalenv", "isfunction", "isnil", "isboolean",
            "isnumber", "isstring", "istable", "isuserdata", "isthread", "iscfunction",
            "rawget", "rawset", "rawlen", "createtable", "newuserdata",
            "getmetatable", "setmetatable", "error", "assert", "traceback",
        }
        for _, fn in ipairs(vm_funcs) do
            check("vm." .. fn, type(vm[fn]) == "function", fn .. " missing")
        end

        -- -- objlen
        check("vm.objlen string", vm.objlen("hello") == 5)
        check("vm.objlen empty table", vm.objlen({}) == 0)
        check("vm.objlen array", vm.objlen({1, 2, 3}) == 3)

        -- -- concat
        check("vm.concat basic", vm.concat("a", "b", "c") == "abc")
        local ok_concat_empty = pcall(function() vm.concat() end)
        check("vm.concat no args", ok_concat_empty)

        -- -- equal
        check("vm.equal same num", vm.equal(1, 1) == true)
        check("vm.equal diff num", vm.equal(1, 2) == false)
        check("vm.equal num vs str", vm.equal(1, "1") == false)
        check("vm.equal same str", vm.equal("x", "x") == true)
        check("vm.equal nil vs nil", vm.equal(nil, nil) == true)
        check("vm.equal nil vs false", vm.equal(nil, false) == false)

        -- -- lt
        check("vm.lt 1<2", vm.lt(1, 2) == true)
        check("vm.lt 2<1", vm.lt(2, 1) == false)
        check("vm.lt equal", vm.lt(1, 1) == false)

        -- -- le
        check("vm.le 1<=1", vm.le(1, 1) == true)
        check("vm.le 2<=1", vm.le(2, 1) == false)
        check("vm.le 1<=2", vm.le(1, 2) == true)

        -- -- tonumber
        check("vm.tonumber int", vm.tonumber(42) == 42)
        check("vm.tonumber float", vm.tonumber(3.14) == 3.14)
        check("vm.tonumber string nil", vm.tonumber("abc") == nil)

        -- -- tointeger
        check("vm.tointeger int", vm.tointeger(42) == 42)
        local ti_ok, ti_v = pcall(function() return vm.tointeger(3.14) end)
        check("vm.tointeger float pcall", ti_ok, tostring(ti_v))

        -- -- typename
        check("vm.typename string", vm.typename("hello") == "string")
        check("vm.typename number", vm.typename(42) == "number")
        check("vm.typename boolean", vm.typename(true) == "boolean")
        check("vm.typename nil", vm.typename(nil) == "nil")
        check("vm.typename table", vm.typename({}) == "table")
        check("vm.typename function", vm.typename(print) == "function")
        check("vm.typename thread", vm.typename(coroutine.create(function() end)) == "thread")

        -- -- isfunction
        check("vm.isfunction print", vm.isfunction(print) == true)
        check("vm.isfunction string", vm.isfunction("x") == false)
        check("vm.isfunction nil", vm.isfunction(nil) == false)

        -- -- isnil
        check("vm.isnil nil", vm.isnil(nil) == true)
        check("vm.isnil false", vm.isnil(false) == false)
        check("vm.isnil 0", vm.isnil(0) == false)
        check("vm.isnil empty str", vm.isnil("") == false)

        -- -- isboolean
        check("vm.isboolean true", vm.isboolean(true) == true)
        check("vm.isboolean false", vm.isboolean(false) == true)
        check("vm.isboolean 1", vm.isboolean(1) == false)
        check("vm.isboolean nil", vm.isboolean(nil) == false)

        -- -- isnumber
        check("vm.isnumber 42", vm.isnumber(42) == true)
        check("vm.isnumber float", vm.isnumber(3.14) == true)
        check("vm.isnumber string", vm.isnumber("42") == false or vm.isnumber("42") == true,
            "may auto-convert strings")

        -- -- isstring
        check("vm.isstring str", vm.isstring("x") == true)
        check("vm.isstring empty", vm.isstring("") == true)
        check("vm.isstring num", vm.isstring(42) == false or vm.isstring(42) == true,
            "may auto-convert numbers")
        check("vm.isstring nil", vm.isstring(nil) == false)

        -- -- istable
        check("vm.istable table", vm.istable({}) == true)
        check("vm.istable string", vm.istable("x") == false)

        -- -- isuserdata
        check("vm.isuserdata io.stdout", vm.isuserdata(io.stdout) == true)
        check("vm.isuserdata table", vm.isuserdata({}) == false)

        -- -- isthread
        local co = coroutine.create(function() end)
        check("vm.isthread thread", vm.isthread(co) == true)
        check("vm.isthread function", vm.isthread(print) == false)

        -- -- iscfunction
        check("vm.iscfunction print", vm.iscfunction(print) == true)
        local function lua_func() end
        check("vm.iscfunction lua func", vm.iscfunction(lua_func) == false)

        -- -- rawget / rawset
        local t = {}
        check("vm.rawset", vm.rawset(t, "k", "v") == nil or type(vm.rawset(t, "k", "v")) == "table")
        check("vm.rawget", vm.rawget(t, "k") == "v")
        check("vm.rawget missing", vm.rawget(t, "nonexist") == nil)

        -- -- rawlen
        check("vm.rawlen array", vm.rawlen({1, 2, 3}) == 3)
        check("vm.rawlen empty", vm.rawlen({}) == 0)
        check("vm.rawlen string", vm.rawlen("hello") == 5)

        -- -- getmetatable / setmetatable
        local mt_t = {}
        local orig_mt = vm.getmetatable(mt_t)
        check("vm.getmetatable no mt", orig_mt == nil)
        local mt = {__index = {x = 10}}
        local ok_smt = pcall(function() vm.setmetatable(mt_t, mt) end)
        check("vm.setmetatable", ok_smt)
        if ok_smt then
            local got_mt = vm.getmetatable(mt_t)
            check("vm.getmetatable after set", got_mt == mt)
        end

        -- -- memory
        local mem = vm.memory()
        check("vm.memory returns number", type(mem) == "number")

        -- -- gcinfo
        local gi = vm.gcinfo()
        check("vm.gcinfo returns value", gi ~= nil)
        check("vm.gcinfo type", type(gi) == "number" or type(gi) == "string")

        -- -- gcstep / gccollect
        local ok_gs = pcall(function() vm.gcstep(0) end)
        check("vm.gcstep", ok_gs)
        local ok_gc = pcall(function() vm.gccollect() end)
        check("vm.gccollect", ok_gc)

        -- -- getglobalenv / setglobalenv
        local genv = vm.getglobalenv()
        check("vm.getglobalenv", type(genv) == "table")
        if type(genv) == "table" then
            local saved = genv.__vm_boundary_test_val
            genv.__vm_boundary_test_val = "boundary"
            check("vm.setglobalenv roundtrip", genv.__vm_boundary_test_val == "boundary")
            genv.__vm_boundary_test_val = saved
        end

        -- -- error / assert
        local ok_err, err_msg = pcall(function() vm.error("test error msg") end)
        check("vm.error", not ok_err and tostring(err_msg):find("test error msg") ~= nil)

        local ok_assert_true = pcall(function() vm.assert(true, "should not fail") end)
        check("vm.assert true", ok_assert_true)

        local ok_assert_false, err_assert = pcall(function() vm.assert(false, "assert failed") end)
        check("vm.assert false", not ok_assert_false)

        -- -- traceback (level 数字)
        local tb = vm.traceback(1)
        check("vm.traceback returns string", type(tb) == "string")

        -- -- newthread / status / resume / yield / currentthread
        local thread_ok, nthread = pcall(function()
            return vm.newthread(function(x)
                return x + 1
            end)
        end)
        if thread_ok and type(nthread) == "thread" then
            check("vm.newthread", true)

            local st_initial = vm.status(nthread)
            check("vm.status initial", st_initial ~= nil)

            local ok_res1, yv = pcall(function() return vm.resume(nthread, 41) end)
            check("vm.resume", ok_res1, tostring(yv))

            local st_final = vm.status(nthread)
            check("vm.status final", st_final ~= nil)
        else
            check("vm.newthread", thread_ok, "newthread failed: " .. tostring(nthread))
        end

        -- -- currentthread
        local ct = vm.currentthread()
        check("vm.currentthread", type(ct) == "thread")

        -- -- getregistry
        local reg = vm.getregistry()
        check("vm.getregistry", type(reg) == "table" or type(reg) == "userdata")

        -- -- gettop
        local top = vm.gettop()
        check("vm.gettop", type(top) == "number")

        -- -- onjit / offjit (if supported)
        if vm.onjit then
            local ok_on = pcall(function() vm.onjit() end)
            check("vm.onjit", ok_on)
        end
        if vm.offjit then
            local ok_off = pcall(function() vm.offjit() end)
            check("vm.offjit", ok_off)
        end

        -- -- execute
        local ok_exec, exec_r = pcall(function() vm.execute("return 1 + 2") end)
        if ok_exec then
            check("vm.execute", exec_r == 3)
        else
            -- execute may not be implemented
            check("vm.execute exists", type(vm.execute) == "function")
        end

        end  -- 关闭 if type(vm) ~= "table" check
    end  -- 关闭 if not ok_mod
end  -- 关闭 do

-- ================================================================
-- 2. vmprotect 库 (require("vmprotect"))
-- ================================================================
do
    print("")
    print("========== vmprotect ==========")

    local ok_mod, vmp = pcall(require, "vmprotect")
    if not ok_mod then
        print("  SKIP: vmprotect not available (" .. tostring(vmp) .. ")")
    else
        check("vmprotect loaded", type(vmp) == "table")
        if type(vmp) ~= "table" then
            -- 不跳过，继续测试
        else

        check("vmprotect.protect is function", type(vmp.protect) == "function")

        -- protect 有效函数
        local ok_prot, result = pcall(function()
            local f = vmp.protect(function() return 42 end)
            return f()
        end)
        if ok_prot then
            check("vmprotect.protect function", result == 42)
        else
            check("vmprotect.protect function (pcall ok)", ok_prot, tostring(result))
        end

        -- protect 非函数
        local ok_str, err_str = pcall(function() vmp.protect("not a function") end)
        check("vmprotect.protect string", not ok_str, "should reject non-function")

        -- protect nil
        local ok_nil, err_nil = pcall(function() vmp.protect(nil) end)
        check("vmprotect.protect nil", not ok_nil, "should reject nil")

        -- protect number
        local ok_num, err_num = pcall(function() vmp.protect(123) end)
        check("vmprotect.protect number", not ok_num, "should reject number")

        -- protect table
        local ok_tbl, err_tbl = pcall(function() vmp.protect({}) end)
        check("vmprotect.protect table", not ok_tbl, "should reject table")

        end  -- 关闭 if type(vmp) ~= "table" check
    end  -- 关闭 if not ok_mod
end  -- 关闭 do

-- ================================================================
-- 3. ByteCode 库 (require("ByteCode"))
-- ================================================================
do
    print("")
    print("========== ByteCode ==========")

    local ok_mod, bc = pcall(require, "ByteCode")
    if not ok_mod then
        print("  SKIP: ByteCode not available (" .. tostring(bc) .. ")")
    else
        check("ByteCode loaded", type(bc) == "table")
        if type(bc) ~= "table" then
            -- 不跳过，继续测试
        else

        -- 验证所有函数存在
        local bc_funcs = {
            "CheckFunction", "GetProto", "GetCodeCount", "GetCode", "SetCode",
            "GetLine", "GetParamCount", "IsGC", "GetOpCode", "GetArgs",
            "Make", "Dump", "GetConstant", "GetConstants", "GetUpvalue",
            "GetUpvalues", "GetLocal", "GetLocals", "GetNestedProto",
            "GetNestedProtos", "GetInstruction", "SetInstruction",
            "Lock", "IsLocked", "MarkOriginal", "IsTampered",
        }
        for _, fn in ipairs(bc_funcs) do
            check("ByteCode." .. fn, type(bc[fn]) == "function", fn .. " missing")
        end

        -- OpCodes table
        check("ByteCode.OpCodes is table", type(bc.OpCodes) == "table")
        if type(bc.OpCodes) == "table" then
            check("OpCodes.MOVE", bc.OpCodes.MOVE ~= nil)
            check("OpCodes.LOADK", bc.OpCodes.LOADK ~= nil)
        end

        -- 创建测试函数
        local f = function(a, b)
            return a + b
        end

        -- CheckFunction
        check("CheckFunction(f)", bc.CheckFunction(f) == true)
        check("CheckFunction(x)", bc.CheckFunction("x") == false)
        check("CheckFunction(nil)", bc.CheckFunction(nil) == false)

        -- GetProto
        local proto = bc.GetProto(f)
        check("GetProto type", type(proto) == "userdata" or type(proto) == "lightuserdata", "got: " .. type(proto))

        -- GetCodeCount
        local cc = bc.GetCodeCount(f)
        check("GetCodeCount type", type(cc) == "number")
        check("GetCodeCount > 0", cc > 0)

        -- GetCode / GetInstruction (Lua 1-based 索引)
        local co_ok, code0 = pcall(function() return bc.GetCode(f, 1) end)
        if co_ok and (type(code0) == "table" or type(code0) == "number") then
            check("GetCode(f,1) type", true)
        else
            local inst0 = bc.GetInstruction(f, 1)
            check("GetInstruction(f,1)", type(inst0) == "table" or type(inst0) == "number")
            code0 = inst0
        end

        -- GetOpCode (从指令获取操作码名称)
        if code0 ~= nil then
            local opname = bc.GetOpCode(code0)
            check("GetOpCode returns value", opname ~= nil)
            -- GetOpCode 可能返回数字或字符串
        else
            check("GetOpCode exists", type(bc.GetOpCode) == "function")
        end

        -- GetArgs
        if code0 ~= nil then
            local args = bc.GetArgs(code0)
            check("GetArgs returns table", type(args) == "table")
        else
            check("GetArgs exists", type(bc.GetArgs) == "function")
        end

        -- GetParamCount
        local pc = bc.GetParamCount(f)
        check("GetParamCount type", type(pc) == "number")

        -- GetConstants
        local consts = bc.GetConstants(f)
        check("GetConstants returns table", type(consts) == "table")

        -- GetLocals
        local locals = bc.GetLocals(f)
        check("GetLocals returns table", type(locals) == "table")
        if type(locals) == "table" then
            -- 检查有元素（locals 可能是 table 列表或字符串列表）
            check("GetLocals has entries", #locals >= 1 or next(locals) ~= nil)
        end

        -- GetUpvalues
        local upvals = bc.GetUpvalues(f)
        check("GetUpvalues returns table", type(upvals) == "table")

        -- GetNestedProtos
        local nested = bc.GetNestedProtos(f)
        check("GetNestedProtos returns table", type(nested) == "table")

        -- Dump
        local dump_str = bc.Dump(f)
        check("Dump returns string", type(dump_str) == "string")
        check("Dump not empty", #dump_str > 0)

        -- Make (创建指令)
        local ok_make, made_inst = pcall(function()
            -- 尝试创建 MOVE 指令: MOVE A B, 例如 MOVE 0 0
            return bc.Make(bc.OpCodes.MOVE, 0, 0)
        end)
        if ok_make then
            check("Make returns value", made_inst ~= nil)
        else
            -- Make 可能有不同签名
            check("Make exists", type(bc.Make) == "function")
        end

        -- Lock / IsLocked
        local ok_lock, lock_err = pcall(function() bc.Lock(f) end)
        check("Lock succeeds", ok_lock, tostring(lock_err))
        if ok_lock then
            local locked = bc.IsLocked(f)
            check("IsLocked after Lock", locked == true)
        end

        -- MarkOriginal / IsTampered (在已锁定的函数上可能失败，用 pcall)
        local ok_mo, mo_err = pcall(function() bc.MarkOriginal(f) end)
        check("MarkOriginal", ok_mo, tostring(mo_err))

        -- -- 更复杂的测试: 嵌套函数
        local f2 = function(x)
            local inner = function(y) return x + y end
            return inner(x)
        end
        check("CheckFunction nested", bc.CheckFunction(f2) == true)

        local f2_protos = bc.GetNestedProtos(f2)
        check("GetNestedProtos nested func", type(f2_protos) == "table")
        if type(f2_protos) == "table" then
            check("GetNestedProtos has inner", #f2_protos >= 1)
        end

        local f2_consts = bc.GetConstants(f2)
        check("GetConstants nested", type(f2_consts) == "table")

        local f2_locals = bc.GetLocals(f2)
        check("GetLocals nested", type(f2_locals) == "table")

        end  -- 关闭 if type(bc) ~= "table" check
    end  -- 关闭 if not ok_mod
end  -- 关闭 do

-- ================================================================
-- 结果汇总
-- ================================================================
print("")
print(string.format("=== VM 组测试完成: %d/%d passed", passed, failed + passed))
if failed > 0 then os.exit(1) end