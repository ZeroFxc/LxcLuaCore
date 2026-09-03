local s = { str = "hello" }
function s:set(v)
    self.str = v
end
print(s.str == "hello")
s set true
print(s.str)