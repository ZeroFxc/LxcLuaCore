local unpack = unpack or table.unpack
if not _G.unpack then _G.unpack = unpack end
local loadstring = loadstring or load
if not _G.loadstring then _G.loadstring = loadstring end
local math_pow = math.pow or function(a, b) return a ^ b end
if not math.pow then math.pow = math_pow end
local setfenv = setfenv or function(f, t) return f end
if not _G.setfenv then _G.setfenv = setfenv end
local getfenv = getfenv or function(f) return _G end
if not _G.getfenv then _G.getfenv = getfenv end
local State = require("state")
local core = require("core")

local function load_and_run(ir_table)
    -- Normalize the loaded prototype
    local function normalize(proto)
        proto.is_closure = true
        for _, p in ipairs(proto.protos) do
            normalize(p)
        end
    end
    normalize(ir_table)

    local state = State.new(_G)

    -- Main chunk is treated as a function taking varargs
    local main_closure = {
        is_closure = true,
        code = ir_table.code,
        constants = ir_table.constants,
        protos = ir_table.protos,
        numparams = ir_table.numparams,
        is_vararg = ir_table.is_vararg,
        maxstacksize = ir_table.maxstacksize,
        upvalues = {}
    }

    state:push_frame(main_closure, 0, 0)

    return core.run(state)
end

return { load_and_run = load_and_run }
