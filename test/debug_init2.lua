-- Debug: test init with parameters
class Person
  function init(self, name, age)
    print("[INIT] self:", self)
    print("[INIT] name:", name)
    print("[INIT] age:", age)
    self.name = name
    self.age = age
  end
end

local a = Person("Alice", 25)
print("name:", a.name)
print("age:", a.age)