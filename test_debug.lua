local s = { str = "hello" }
function s:set(v)
    self.str = v
    return self
end

s set nil
print("nil ok: " .. tostring(s.str))

s set true
print("true ok: " .. tostring(s.str))

s set false
print("false ok: " .. tostring(s.str))