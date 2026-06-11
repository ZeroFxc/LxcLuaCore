-- 简单测试 true 在中缀调用中的行为
local s = { str = "hello" }
function s:set(v)
    self.str = v
    return self
end

-- 先测试 nil
s set nil
print("After nil: " .. tostring(s.str))

-- 再测试 true
s set true
print("After true: " .. tostring(s.str))