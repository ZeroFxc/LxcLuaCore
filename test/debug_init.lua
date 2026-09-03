-- Minimal init test
class Person
  function init(self, name, age)
    self.name = name
    self.age = age
  end
end

local a = Person("Alice", 25)
print("name:", a.name)
print("age:", a.age)
print("Test passed!")