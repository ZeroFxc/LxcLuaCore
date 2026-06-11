local test = { value = 10 }
function test:add(n)
    self.value = self.value + n
    return self
end
function test:get()
    return self.value
end

test add 5
print("After add: " .. test:get())

-- Test assignment with infix
local result = test get
print("result type: " .. type(result))
print("result: " .. tostring(result))