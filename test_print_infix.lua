local s = { str = "hello" }
function s:set(v)
    self.str = v
end
s set nil
print(s.str == nil)
s set true
print(s.str)