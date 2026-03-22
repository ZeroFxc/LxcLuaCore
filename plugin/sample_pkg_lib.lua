-- This is a pure Lua library designed to be installed alongside a .plugin file
local M = {}

function M.greet(name)
    return "Hello from pure Lua library sample_pkg_lib! Nice to meet you, " .. tostring(name) .. "."
end

function M.calculate(a, b)
    return a + b
end

return M
