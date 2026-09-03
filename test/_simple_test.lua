local native = {}
native.compile = function(s) return s end

local code = native.compile([[
    .program test
    ret 0
]])
print(code)
