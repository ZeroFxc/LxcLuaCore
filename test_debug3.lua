local s = { str = "hello" }
function s:set(v)
    self.str = v
end
s set nil
print("nil ok")
s set true
print("true ok")