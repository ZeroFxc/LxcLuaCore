local test = { value = 10 }
function test:add(n) self.value = self.value + n; return self end
test add 5
print(test:get())