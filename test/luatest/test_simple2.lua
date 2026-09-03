local holder = { val = 0 }
function holder:add(n)
    return n
end
local v = holder add 100 + 20
print(v)