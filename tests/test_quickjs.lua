local qjs = require("quickjs")

print("QuickJS module:", qjs)
local rt = qjs.new_runtime()
print("QuickJS runtime:", rt)
local ctx = rt:new_context()
print("QuickJS context:", ctx)

local result = ctx:eval("1 + 2 * 3")
print("1 + 2 * 3 =", result:tonumber())
assert(result:tonumber() == 7, "Eval math test failed")

local str_result = ctx:eval("'Hello ' + 'World!'")
print("String result:", str_result:tostring())
assert(str_result:tostring() == "Hello World!", "Eval string test failed")

local success, err = pcall(function()
    ctx:eval("invalid_javascript_code()")
end)
print("Error handling (success=" .. tostring(success) .. "):", err)
assert(success == false, "Error handling failed (should have thrown exception)")

print("QuickJS integration test completed successfully.")
