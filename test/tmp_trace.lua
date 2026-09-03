class Animal { function init(self, n) self.name = n end }
class Cat extends Animal { function init(self, n) super.init(self, n) end }
local cat = Cat("Kitty")
local animal = cat as Animal
print(type(animal))
print(animal.name)