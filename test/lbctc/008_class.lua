-- covers: OP_NEWTABLE, OP_SETTABLE, OP_GETFIELD, OP_CLOSURE, OP_CALL, OP_TAILCALL, OP_SELF, OP_GETTABUP, OP_SETTABUP, OP_SETMETHOD, OP_GETSUPER, class构造, 实例化, 字段读写, self方法, static成员

print("=== 008 class START ===")

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

  public function intro(self)
    return self.name .. " is " .. self.age .. " years old"
  end

  static function count()
    return Person._count
  end
}

assert(Person)
print("class_defined: 1")
local a = Person.new("Alice", 25)
print("p1_name: " .. a.name)
print("p1_age: " .. a.age)
print("p1_greet: " .. a:greet())
local b = Person("Bob", 40)
print("p2_name: " .. b.name)
print("p2_age: " .. b.age)
print("p2_intro: " .. b:intro())
print("person_count: " .. Person.count())
print("species: " .. Person.species)

print("=== 008 class END ===")
