class Person {
  function init(self, name, age)
    self.name = name
    self.age = age
  end

  static function create(name, age)
    return Person(name, age)
  end

  function greet(self)
    return "Hello, I am " .. self.name
  end
}

local a = Person.create("Alice", 25)
print("type a: " .. type(a))
print("a.name: " .. tostring(a.name))
print("a.age: " .. tostring(a.age))
print("a:greet(): " .. a:greet())
