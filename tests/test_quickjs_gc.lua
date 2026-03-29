local qjs = require("quickjs")

local function get_value()
    local rt = qjs.new_runtime()
    local ctx = rt:new_context()
    return ctx:eval("'Hello GC World!'")
end

local val = get_value()
collectgarbage()
collectgarbage()
print("If it survives GC: " .. val:tostring())
