class Person {
  static species = "Human"

  function init(self, name, age)
    self.name = name
    self.age = age
  end

  function greet(self)
    return "Hello, I am " .. self.name
  end
}

local a = Person("Alice", 25)
print("p1_name: " .. a.name)
print("p1_greet: " .. a:greet())
print("species: " .. Person.species)
