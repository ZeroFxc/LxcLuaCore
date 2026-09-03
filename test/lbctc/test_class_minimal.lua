class Person {
  function init(self, name)
    self.name = name
  end
  function greet(self)
    return "Hello " .. self.name
  end
}
local a = Person("Alice")
print("name: " .. a.name)
print("greet: " .. a:greet())
