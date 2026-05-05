-- docs/examples/error_handling.lua
-- This file tests error handling extensions

try
    error("Something wrong")
catch(e)
    print("Caught error: " .. e)
finally
    print("Cleanup")
end
