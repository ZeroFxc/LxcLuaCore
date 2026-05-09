jit.on()
local _env = { a = 1, b = 2 }
local function test()
    local a = _env.a
    local b = _env.b
    _env.a = 10
    _env.b = 20
    return a, b, _env.a, _env.b
end
print(test())
