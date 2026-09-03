class Animal {
  function init(self, n)
    self.name = n
  end
}

class Dog extends Animal {
  function init(self, n, b)
    super.init(self, n)
    self.breed = b
  end
}

local d1 = Dog("Buddy", "Golden")
print("d1 metatable:", getmetatable(d1))
print("Dog:", Dog)

local d2 = d1:clone()
print("d2.name:", d2.name)
print("d2.breed:", d2.breed)
print("d2 metatable:", getmetatable(d2))
print("isinstance d2 Dog:", isinstance(d2, Dog))
print("isinstance d2 Animal:", isinstance(d2, Animal))