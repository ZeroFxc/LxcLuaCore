local code_compile8 = native.compile([[
    .program  sum_1_to_100
    ret @sum
]])
local nv_c8 = native.new(code_compile8, 256)
print("hello")
