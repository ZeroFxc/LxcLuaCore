class Person {
  function init(self, name, age)
    self.name = name
    self.age = age
  end

  static function new(...)
    return Person(...)
  end

  function greet(self)
    return "Hello, I am " .. self.name
  end
}

-- Debug: check what Person.new is
local p = Person
print("type Person: " .. type(p))
local n = p.new
print("type p.new: " .. type(n))

-- Try calling directly
local a = Person("Alice", 25)
print("direct name: " .. a.name)
