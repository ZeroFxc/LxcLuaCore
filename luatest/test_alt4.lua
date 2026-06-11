local s = { str = "hello" }
function s:set(v)
    self.str = v
end
s set nil
local x = 1
s set true
print(s.str)