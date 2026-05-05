local unpack = unpack or table.unpack
local opcodes = {}
local function safe_cmp_lt(a, b)
    if a == nil or b == nil then return false end
    local ta, tb = type(a), type(b)
    if ta == tb then
        if ta == "number" then return a < b end
        if ta == "string" then return a < b end
    end
    if ta == "string" and tb == "number" then return false end
    if ta == "number" and tb == "string" then return false end
    if ta == "number" and tb == "table" then return false end
    if ta == "table" and tb == "number" then return false end
    if ta == "string" and tb == "table" then return false end
    if ta == "table" and tb == "string" then return false end
    local ok, result = pcall(function() return a < b end)
    if ok then return result end
    return ta > tb
end

local function safe_cmp_le(a, b)
    if a == nil or b == nil then return false end
    local ta, tb = type(a), type(b)
    if ta == tb then
        if ta == "number" then return a <= b end
        if ta == "string" then return a <= b end
    end
    if ta == "string" and tb == "number" then return false end
    if ta == "number" and tb == "string" then return false end
    if ta == "number" and tb == "table" then return false end
    if ta == "table" and tb == "number" then return false end
    if ta == "string" and tb == "table" then return false end
    if ta == "table" and tb == "string" then return false end
    local ok, result = pcall(function() return a <= b end)
    if ok then return result end
    return ta > tb
end

local function get_rk(frame, inst_val)
    if inst_val >= 256 then
        return frame.func.constants[inst_val - 256 + 1]
    else
        return frame.state.stack[frame.base + inst_val]
    end
end

local function dispatch_meta_closure(frame, mm, args, nresults, dest_a)
    local new_base = math.max(frame.base + (frame.func.maxstacksize or 1), frame.state.top + 1)
    frame.state:push_frame(mm, new_base, nresults)
    for i, arg in ipairs(args) do
        frame.state.stack[new_base + i - 1] = arg
    end
    frame.last_call_nresults = nresults
    frame.last_call_a = new_base - frame.base
    frame.meta_result_a = dest_a
    frame.state.executing_call = true
end

local function dispatch_meta_cmp(frame, mm, b, c, event, cmp_a)
    local new_base = math.max(frame.base + (frame.func.maxstacksize or 1), frame.state.top + 1)
    frame.state:push_frame(mm, new_base, 1)
    frame.state.stack[new_base] = b
    frame.state.stack[new_base + 1] = c
    frame.last_call_nresults = 1
    frame.last_call_a = new_base - frame.base
    frame.pending_meta_cmp = event
    frame.pending_cmp_a = cmp_a
    frame.state.executing_call = true
end

local function try_binary_metamethod(frame, inst, b, c, event)
    local mt = getmetatable(b)
    if mt then
        local mm = rawget(mt, event)
        if mm then
            if type(mm) == "table" and mm.is_closure then
                dispatch_meta_closure(frame, mm, {b, c}, 1, inst.a)
                return true
            elseif type(mm) == "function" or type(mm) == "userdata" then
                frame.state.stack[frame.base + inst.a] = mm(b, c)
                return true
            end
        end
    end
    mt = getmetatable(c)
    if mt then
        local mm = rawget(mt, event)
        if mm then
            if type(mm) == "table" and mm.is_closure then
                dispatch_meta_closure(frame, mm, {b, c}, 1, inst.a)
                return true
            elseif type(mm) == "function" or type(mm) == "userdata" then
                frame.state.stack[frame.base + inst.a] = mm(b, c)
                return true
            end
        end
    end
    return false
end

local function try_cmp_metamethod(frame, inst, b, c, event)
    local mt = getmetatable(b)
    if mt then
        local mm = rawget(mt, event)
        if mm then
            if type(mm) == "table" and mm.is_closure then
                dispatch_meta_cmp(frame, mm, b, c, event, inst.a)
                return true
            elseif type(mm) == "function" or type(mm) == "userdata" then
                return false, mm(b, c)
            end
        end
    end
    local mt2 = getmetatable(c)
    if mt2 ~= mt then
        local mm = rawget(mt2, event)
        if mm then
            if type(mm) == "table" and mm.is_closure then
                dispatch_meta_cmp(frame, mm, b, c, event, inst.a)
                return true
            elseif type(mm) == "function" or type(mm) == "userdata" then
                return false, mm(b, c)
            end
        end
    end
    return false, nil
end

function opcodes.VOP_MOVE(frame, inst)
    frame.state.stack[frame.base + inst.a] = frame.state.stack[frame.base + inst.b]
end

function opcodes.VOP_LOADK(frame, inst)
    local val = frame.func.constants[inst.bx + 1]
    frame.state.stack[frame.base + inst.a] = val
end

function opcodes.VOP_LOADBOOL(frame, inst)
    frame.state.stack[frame.base + inst.a] = (inst.b ~= 0)
    if inst.c ~= 0 then
        frame.pc = frame.pc + 1
    end
end

function opcodes.VOP_LOADNIL(frame, inst)
    for i = inst.a, inst.b do
        frame.state.stack[frame.base + i] = nil
    end
end

function opcodes.VOP_GETUPVAL(frame, inst)
    local uv = frame.func.upvalues[inst.b + 1]
    if not uv then
        error("Upvalue " .. tostring(inst.b + 1) .. " is nil")
    end
    if uv.closed then
        frame.state.stack[frame.base + inst.a] = uv.value
    else
        frame.state.stack[frame.base + inst.a] = frame.state.stack[uv.id]
    end
end

function opcodes.VOP_GETGLOBAL(frame, inst)
    local key = frame.func.constants[inst.bx + 1]
    frame.state.stack[frame.base + inst.a] = frame.state.globals[key]
end

function opcodes.VOP_GETTABLE(frame, inst)
    local t = frame.state.stack[frame.base + inst.b]
    local k = get_rk(frame, inst.c)
    local v = rawget(t, k)
    if v ~= nil then
        frame.state.stack[frame.base + inst.a] = v
        return
    end
    local mt = getmetatable(t)
    if not mt then
        frame.state.stack[frame.base + inst.a] = nil
        return
    end
    local index = rawget(mt, "__index")
    if not index then
        frame.state.stack[frame.base + inst.a] = nil
        return
    end
    if type(index) == "table" and index.is_closure then
        dispatch_meta_closure(frame, index, {t, k}, 1, inst.a)
    elseif type(index) == "function" or type(index) == "userdata" then
        frame.state.stack[frame.base + inst.a] = index(t, k)
    elseif type(index) == "table" then
        frame.state.stack[frame.base + inst.a] = rawget(index, k)
    else
        frame.state.stack[frame.base + inst.a] = nil
    end
end

function opcodes.VOP_SETGLOBAL(frame, inst)
    local key = frame.func.constants[inst.bx + 1]
    local val = frame.state.stack[frame.base + inst.a]
    frame.state.globals[key] = val
end

function opcodes.VOP_SETUPVAL(frame, inst)
    local uv = frame.func.upvalues[inst.b + 1]
    local val = frame.state.stack[frame.base + inst.a]
    if uv.closed then
        uv.value = val
    else
        frame.state.stack[uv.id] = val
    end
end

function opcodes.VOP_SETTABLE(frame, inst)
    local t = frame.state.stack[frame.base + inst.a]
    local k = get_rk(frame, inst.b)
    local v = get_rk(frame, inst.c)
    if k == nil then
        error(string.format("VOP_SETTABLE: key nil, a=%d, b=%d, c=%d, tbl=%s",
            inst.a, inst.b, inst.c, tostring(t)))
    end
    if t == nil then
        error(string.format("VOP_SETTABLE: table nil, a=%d, b=%d, c=%d",
            inst.a, inst.b, inst.c))
    end
    local mt = getmetatable(t)
    if mt then
        local newindex = rawget(mt, "__newindex")
        if newindex then
            if type(newindex) == "table" and newindex.is_closure then
                dispatch_meta_closure(frame, newindex, {t, k, v}, 0, inst.a)
                return
            elseif type(newindex) == "function" or type(newindex) == "userdata" then
                newindex(t, k, v)
                return
            elseif type(newindex) == "table" then
                rawset(newindex, k, v)
                return
            end
        end
    end
    rawset(t, k, v)
end

function opcodes.VOP_NEWTABLE(frame, inst)
    frame.state.stack[frame.base + inst.a] = {}
end

function opcodes.VOP_SELF(frame, inst)
    local obj = frame.state.stack[frame.base + inst.b]
    frame.state.stack[frame.base + inst.a + 1] = obj
    local k = get_rk(frame, inst.c)
    local v = rawget(obj, k)
    if v ~= nil then
        frame.state.stack[frame.base + inst.a] = v
        return
    end
    local mt = getmetatable(obj)
    if not mt then
        frame.state.stack[frame.base + inst.a] = nil
        return
    end
    local index = rawget(mt, "__index")
    if not index then
        frame.state.stack[frame.base + inst.a] = nil
        return
    end
    if type(index) == "table" and index.is_closure then
        dispatch_meta_closure(frame, index, {obj, k}, 1, inst.a)
    elseif type(index) == "function" or type(index) == "userdata" then
        frame.state.stack[frame.base + inst.a] = index(obj, k)
    elseif type(index) == "table" then
        frame.state.stack[frame.base + inst.a] = rawget(index, k)
    else
        frame.state.stack[frame.base + inst.a] = nil
    end
end

function opcodes.VOP_ADD(frame, inst)
    local b = get_rk(frame, inst.b)
    local c = get_rk(frame, inst.c)
    if type(b) == "string" then b = tonumber(b) end
    if type(c) == "string" then c = tonumber(c) end
    if not try_binary_metamethod(frame, inst, b, c, "__add") then
        frame.state.stack[frame.base + inst.a] = b + c
    end
end

function opcodes.VOP_SUB(frame, inst)
    local b = get_rk(frame, inst.b)
    local c = get_rk(frame, inst.c)
    if type(b) == "string" then b = tonumber(b) end
    if type(c) == "string" then c = tonumber(c) end
    if not try_binary_metamethod(frame, inst, b, c, "__sub") then
        frame.state.stack[frame.base + inst.a] = b - c
    end
end

function opcodes.VOP_MUL(frame, inst)
    local b = get_rk(frame, inst.b)
    local c = get_rk(frame, inst.c)
    if type(b) == "string" then b = tonumber(b) end
    if type(c) == "string" then c = tonumber(c) end
    if not try_binary_metamethod(frame, inst, b, c, "__mul") then
        frame.state.stack[frame.base + inst.a] = b * c
    end
end

function opcodes.VOP_DIV(frame, inst)
    local b = get_rk(frame, inst.b)
    local c = get_rk(frame, inst.c)
    if type(b) == "string" then b = tonumber(b) end
    if type(c) == "string" then c = tonumber(c) end
    if not try_binary_metamethod(frame, inst, b, c, "__div") then
        frame.state.stack[frame.base + inst.a] = b / c
    end
end

function opcodes.VOP_MOD(frame, inst)
    local b = get_rk(frame, inst.b)
    local c = get_rk(frame, inst.c)
    if type(b) == "string" then b = tonumber(b) end
    if type(c) == "string" then c = tonumber(c) end
    if not try_binary_metamethod(frame, inst, b, c, "__mod") then
        frame.state.stack[frame.base + inst.a] = b % c
    end
end

function opcodes.VOP_POW(frame, inst)
    local b = get_rk(frame, inst.b)
    local c = get_rk(frame, inst.c)
    if type(b) == "string" then b = tonumber(b) end
    if type(c) == "string" then c = tonumber(c) end
    if not try_binary_metamethod(frame, inst, b, c, "__pow") then
        frame.state.stack[frame.base + inst.a] = b ^ c
    end
end

function opcodes.VOP_UNM(frame, inst)
    local b = frame.state.stack[frame.base + inst.b]
    local mt = getmetatable(b)
    if mt then
        local mm = rawget(mt, "__unm")
        if mm then
            if type(mm) == "table" and mm.is_closure then
                dispatch_meta_closure(frame, mm, {b}, 1, inst.a)
                return
            elseif type(mm) == "function" or type(mm) == "userdata" then
                frame.state.stack[frame.base + inst.a] = mm(b)
                return
            end
        end
    end
    frame.state.stack[frame.base + inst.a] = -b
end

function opcodes.VOP_NOT(frame, inst)
    frame.state.stack[frame.base + inst.a] = not frame.state.stack[frame.base + inst.b]
end

function opcodes.VOP_LEN(frame, inst)
    local b = frame.state.stack[frame.base + inst.b]
    local mt = getmetatable(b)
    if mt then
        local mm = rawget(mt, "__len")
        if mm then
            if type(mm) == "table" and mm.is_closure then
                dispatch_meta_closure(frame, mm, {b}, 1, inst.a)
                return
            elseif type(mm) == "function" or type(mm) == "userdata" then
                frame.state.stack[frame.base + inst.a] = mm(b)
                return
            end
        end
    end
    frame.state.stack[frame.base + inst.a] = #b
end

function opcodes.VOP_CONCAT(frame, inst)
    local res = ""
    for i = inst.b, inst.c do
        res = res .. tostring(frame.state.stack[frame.base + i])
    end
    frame.state.stack[frame.base + inst.a] = res
end

function opcodes.VOP_JMP(frame, inst)
    frame.pc = frame.pc + inst.sbx
end

function opcodes.VOP_EQ(frame, inst)
    local b = get_rk(frame, inst.b)
    local c = get_rk(frame, inst.c)
    local handled, result = try_cmp_metamethod(frame, inst, b, c, "__eq")
    if handled then
        return
    end
    if result == nil then
        result = (b == c)
    end
    if result ~= (inst.a ~= 0) then
        frame.pc = frame.pc + 1
    end
end

function opcodes.VOP_LT(frame, inst)
    local b = get_rk(frame, inst.b)
    local c = get_rk(frame, inst.c)
    local handled, result = try_cmp_metamethod(frame, inst, b, c, "__lt")
    if handled then
        return
    end
    if result == nil then
        result = safe_cmp_lt(b, c)
    end
    if result ~= (inst.a ~= 0) then
        frame.pc = frame.pc + 1
    end
end

function opcodes.VOP_LE(frame, inst)
    local b = get_rk(frame, inst.b)
    local c = get_rk(frame, inst.c)
    local handled, result = try_cmp_metamethod(frame, inst, b, c, "__le")
    if handled then
        return
    end
    if result == nil then
        result = safe_cmp_le(b, c)
    end
    if result ~= (inst.a ~= 0) then
        frame.pc = frame.pc + 1
    end
end

function opcodes.VOP_TEST(frame, inst)
    local val = frame.state.stack[frame.base + inst.a]
    local condition = not not val
    if condition == (inst.c ~= 0) then
    else
        frame.pc = frame.pc + 1
    end
end

function opcodes.VOP_TESTSET(frame, inst)
    local val = frame.state.stack[frame.base + inst.b]
    local condition = not not val
    if condition == (inst.c ~= 0) then
        frame.state.stack[frame.base + inst.a] = val
    else
        frame.pc = frame.pc + 1
    end
end

function opcodes.VOP_CALL(frame, inst)
    local a = frame.base + inst.a
    local func = frame.state.stack[a]
    local nparams = inst.b - 1
    local nresults = inst.c - 1

    local args = {}
    if nparams == -1 then
        nparams = frame.state.top - a
    end
    for i = 1, nparams do
        args[i] = frame.state.stack[a + i]
    end

    if type(func) == "function" or type(func) == "userdata" then
        frame.last_call_nresults = nresults
        frame.last_call_a = inst.a
        local results = {func(unpack(args, 1, nparams))}
        if nresults == -1 then
            nresults = #results
            frame.state.top = a + nresults - 1
        end
        for i = 1, nresults do
            frame.state.stack[a + i - 1] = results[i]
        end
    elseif type(func) == "table" and func.is_closure then
        local new_base = a + 1
        frame.last_call_nresults = nresults
        frame.last_call_a = inst.a
        frame.state:push_frame(func, new_base, nresults)
        for i = 1, nparams do
            frame.state.stack[new_base + i - 1] = args[i]
        end
        frame.state.executing_call = true
    else
        error(string.format("Attempt to call a non-function value, type=%s, val=%s, a=%d, base=%d",
            type(func), tostring(func), inst.a, frame.base))
    end
end

function opcodes.VOP_TAILCALL(frame, inst)
    opcodes.VOP_CALL(frame, {a = inst.a, b = inst.b, c = 0})
    if frame.state.executing_call then
        frame.state:pop_frame()
    else
        local a = frame.base + inst.a
        local nresults = frame.state.top - a + 1
        local results = {}
        for i = 1, nresults do
            results[i] = frame.state.stack[a + i - 1]
        end
        frame.return_values = results
        frame.nresults = nresults
        frame.state.executing_return = true
    end
end

function opcodes.VOP_RETURN(frame, inst)
    local a = frame.base + inst.a
    local nresults = inst.b - 1

    for id, uv in pairs(frame.state.open_upvalues) do
        if id >= frame.base then
            uv.value = frame.state.stack[id]
            uv.closed = true
            frame.state.open_upvalues[id] = nil
        end
    end

    local results = {}
    if nresults == -1 then
        nresults = frame.state.top - a
    end
    for i = 1, nresults do
        results[i] = frame.state.stack[a + i - 1]
    end

    frame.return_values = results
    frame.nresults = nresults
    frame.state.executing_return = true
end

function opcodes.VOP_FORLOOP(frame, inst)
    local a = frame.base + inst.a
    local step = frame.state.stack[a + 2]
    local idx = frame.state.stack[a] + step
    local limit = frame.state.stack[a + 1]

    frame.state.stack[a] = idx
    if (step > 0 and idx <= limit) or (step <= 0 and idx >= limit) then
        frame.pc = frame.pc + inst.sbx
        frame.state.stack[a + 3] = idx
    end
end

function opcodes.VOP_FORPREP(frame, inst)
    local a = frame.base + inst.a
    local init = frame.state.stack[a]
    local limit = frame.state.stack[a + 1]
    local step = frame.state.stack[a + 2]

    if type(init) == "string" then init = tonumber(init) end
    if type(limit) == "string" then limit = tonumber(limit) end
    if type(step) == "string" then step = tonumber(step) end

    frame.state.stack[a] = init - step
    frame.state.stack[a + 1] = limit
    frame.state.stack[a + 2] = step
    frame.pc = frame.pc + inst.sbx
end

function opcodes.VOP_TFORLOOP(frame, inst)
    local a = frame.base + inst.a
    local func = frame.state.stack[a]
    local state = frame.state.stack[a + 1]
    local ctrl = frame.state.stack[a + 2]

    local results = {func(state, ctrl)}
    for i = 1, inst.c do
        frame.state.stack[a + 2 + i] = results[i]
    end

    if frame.state.stack[a + 3] ~= nil then
        frame.state.stack[a + 2] = frame.state.stack[a + 3]
    else
        frame.pc = frame.pc + 1
    end
end

function opcodes.VOP_SETLIST(frame, inst)
    local a = frame.base + inst.a
    local t = frame.state.stack[a]
    local n = inst.b
    local c = inst.c

    if n == 0 then
        n = frame.state.top - a
    end

    if c == 0 then
        error("VOP_SETLIST with c == 0 not fully supported")
    end

    local offset = (c - 1) * 50
    for i = 1, n do
        t[offset + i] = frame.state.stack[a + i]
    end
end

function opcodes.VOP_CLOSE(frame, inst)
    local a = frame.base + inst.a
    for id, uv in pairs(frame.state.open_upvalues) do
        if id >= a then
            uv.value = frame.state.stack[id]
            uv.closed = true
            frame.state.open_upvalues[id] = nil
        end
    end
end

function opcodes.VOP_CLOSURE(frame, inst)
    local proto = frame.func.protos[inst.bx + 1]
    local closure = {
        is_closure = true,
        protos = proto.protos,
        constants = proto.constants,
        code = proto.code,
        numparams = proto.numparams,
        is_vararg = proto.is_vararg,
        maxstacksize = proto.maxstacksize,
        upvalues = {},
        nups = proto.nups or proto.sizeupvalues or #proto.upvalues or 0
    }

    if inst.upval_insts then
        for _, upval_inst in ipairs(inst.upval_insts) do
            if upval_inst.op == "VOP_MOVE" then
                local id = frame.base + upval_inst.b
                if not frame.state.open_upvalues[id] then
                    frame.state.open_upvalues[id] = { id = id, closed = false }
                end
                table.insert(closure.upvalues, frame.state.open_upvalues[id])
            elseif upval_inst.op == "VOP_GETUPVAL" then
                table.insert(closure.upvalues, frame.func.upvalues[upval_inst.b + 1])
            else
                error("Unknown pseudo opcode for upvalue capture: " .. tostring(upval_inst.op))
            end
        end
    end

    frame.state.stack[frame.base + inst.a] = closure
end

function opcodes.VOP_VARARG(frame, inst)
    local a = frame.base + inst.a
    local n = inst.b - 1

    local varargs = frame.func.varargs or {}
    if n == -1 then
        n = #varargs
        frame.state.top = a + n - 1
    end

    for i = 1, n do
        frame.state.stack[a + i - 1] = varargs[i]
    end
end

return opcodes