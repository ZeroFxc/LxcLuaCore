local function sum(n)
    local s = 0
    for i = 1, n do s = s + i end
    return s
end

-- dump bytecode using string.dump
local dumped = string.dump(sum)
print("bytecode size:", #dumped)

-- proto is at offset 12 in the dump (Lua 5.4 format)
-- but let's just try getting the code via debug
local info = debug.getinfo(sum, "S")
print("what:", info.what, "source:", info.source, "linedefined:", info.linedefined)