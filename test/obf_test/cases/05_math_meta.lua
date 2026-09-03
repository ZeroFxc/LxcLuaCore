-- Test: math, bit operations, pcall, metatables
-- Math
print(math.abs(-42))
print(math.floor(3.7))
print(math.ceil(3.2))
print(math.max(1, 5, 3, 2))
print(math.min(1, 5, 3, 2))
print(math.sqrt(16))
print(math.pi)
print(math.huge)
print(math.floor(math.sqrt(144)))

-- Bit operations
print(0xFF & 0x0F)
print(0xF0 | 0x0F)
print(~0 & 0xFF)
print(1 << 4)
print(256 >> 2)
print(0xAA ~ 0x55)

-- pcall
local ok, err = pcall(function()
    error("intentional error")
end)
print(ok, err)

local ok2, result = pcall(function()
    return 42
end)
print(ok2, result)

local ok3 = pcall(function()
    local t = nil
    return t.field
end)
print(ok3)

-- Metatables
local mt = {
    __add = function(a, b) return setmetatable({val = a.val + b.val}, getmetatable(a)) end,
    __tostring = function(a) return "Vec(" .. a.val .. ")" end,
    __eq = function(a, b) return a.val == b.val end,
}

local function vec(v) return setmetatable({val = v}, mt) end

local v1 = vec(10)
local v2 = vec(20)
local v3 = v1 + v2
print(tostring(v3))
print(v1 == vec(10))
print(v1 == v2)

-- __index
local proto = {greet = function(self) return "Hi from " .. self.name end}
local obj = setmetatable({name = "obj1"}, {__index = proto})
print(obj:greet())

-- __call
local callable = setmetatable({}, {__call = function(self, x) return x * 2 end})
print(callable(21))
