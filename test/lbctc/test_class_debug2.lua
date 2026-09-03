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

local a = Person.new("Alice", 25)
print("type a: " .. type(a))
print("a.name: " .. tostring(a.name))
print("a.age: " .. tostring(a.age))
