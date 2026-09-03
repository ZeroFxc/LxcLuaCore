class Person {
  static species = "Human"
  static _count = 0

  function init(self, name, age)
    self.name = name
    self.age = age
    Person._count = Person._count + 1
  end

  static function new(...)
    return Person(...)
  end

  function greet(self)
    return "Hello, I am " .. self.name
  end
}

local a = Person.new("Alice", 25)
print("p1_name: " .. a.name)
print("p1_age: " .. a.age)
print("p1_greet: " .. a:greet())
print("count: " .. Person._count)
print("species: " .. Person.species)
