local function do_nothing()
end

local function with_ret()
    return 1
end

for i = 1, 5 do
    do_nothing()
end

local a = do_nothing()
print("inline test passed", a)
