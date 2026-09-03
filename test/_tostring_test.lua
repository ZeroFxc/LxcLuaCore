class Animal {
  function init(n) self.name = n end
}

print(Animal)            -- 打印类本身
print(tostring(Animal))
print("classname key:", Animal.__classname)

local a = Animal("cat")
print(a)                 -- 打印实例
print(tostring(a))
