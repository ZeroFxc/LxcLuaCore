-- 覆盖: 泛型类, clone() 深拷贝, as 安全转换, singleton 单例

print("=== 017 oop_advanced START ===")

-- =====================================================
-- Test 1: 泛型类定义
-- =====================================================
class Box {
  function init(self, val)
    self.val = val
  end
  function get(self)
    return self.val
  end
  function set(self, val)
    self.val = val
  end
}

local b1 = Box(10)
assert(b1:get() == 10)
b1:set(20)
assert(b1:get() == 20)
print("box_generic: 1")

-- =====================================================
-- Test 2: clone() 深拷贝 - 基本类型
-- =====================================================
class Point {
  function init(self, x, y)
    self.x = x
    self.y = y
  end
}

local p1 = Point(1, 2)
local p2 = p1:clone()
assert(p2.x == 1)
assert(p2.y == 2)
p2.x = 10
assert(p1.x == 1)  -- 原对象不受影响
assert(p2.x == 10)
print("clone_basic: 1")

-- =====================================================
-- Test 3: clone() 深拷贝 - 嵌套对象
-- =====================================================
class Container {
  function init(self)
    self.items = {}
  end
  function add(self, item)
    self.items[#self.items + 1] = item
  end
}

local c1 = Container()
c1:add("hello")
c1:add("world")
local c2 = c1:clone()
assert(#c2.items == 2)
assert(c2.items[1] == "hello")
assert(c2.items[2] == "world")
c2:add("extra")
assert(#c1.items == 2)  -- 原对象不受影响
assert(#c2.items == 3)
print("clone_nested: 1")

-- =====================================================
-- Test 4: clone() 深拷贝 - 子类对象
-- =====================================================
class Animal {
  function init(self, name)
    self.name = name
  end
}
class Dog extends Animal {
  function init(self, name, breed)
    super.init(self, name)
    self.breed = breed
  end
}

local d1 = Dog("Buddy", "Golden")
local d2 = d1:clone()
assert(d2.name == "Buddy")
assert(d2.breed == "Golden")
assert(isinstance(d2, Dog))
assert(isinstance(d2, Animal))
print("clone_subclass: 1")

-- =====================================================
-- Test 5: as 安全转换 - 成功
-- =====================================================
class Cat extends Animal {
  function init(self, name)
    super.init(self, name)
  end
  function meow(self)
    return "meow"
  end
}

local cat = Cat("Kitty")
-- 安全转换: Cat -> Animal
local animal = cat as Animal
assert(animal ~= nil)
assert(animal.name == "Kitty")
print("as_success: 1")

-- =====================================================
-- Test 6: as 安全转换 - 失败
-- =====================================================
local cat2 = Cat("Whiskers")
-- 尝试将 Cat 转为 Dog (失败)
local dog = cat2 as Dog
assert(dog == nil)
print("as_fail: 1")

-- =====================================================
-- Test 7: as 安全转换 - 转换回原类型
-- =====================================================
local animal2 = cat2 as Animal
assert(animal2 ~= nil)
local cat3 = animal2 as Cat
assert(cat3 ~= nil)
assert(cat3:meow() == "meow")
print("as_roundtrip: 1")

-- =====================================================
-- Test 8: singleton 单例
-- =====================================================
singleton class AppConfig {
  function init(self)
    self.version = "1.0.0"
    self.debug = true
  end
}

local cfg1 = AppConfig()
local cfg2 = AppConfig()
assert(cfg1 == cfg2)  -- 同一实例
assert(cfg1.version == "1.0.0")
assert(cfg2.debug == true)
cfg1.debug = false
assert(cfg2.debug == false)  -- 修改同步
print("singleton_same: 1")

-- =====================================================
-- Test 9: singleton 与方法
-- =====================================================
singleton class Counter {
  function init(self)
    self.count = 0
  end
  function increment(self)
    self.count = self.count + 1
    return self.count
  end
  function getCount(self)
    return self.count
  end
}

assert(Counter():getCount() == 0)
assert(Counter():increment() == 1)
assert(Counter():increment() == 2)
assert(Counter():getCount() == 2)
print("singleton_counter: 2")

-- =====================================================
-- Test 10: 多重继承 + as 组合
-- =====================================================
class Flyable {
  function fly(self)
    return "flying"
  end
}
class Swimmable {
  function swim(self)
    return "swimming"
  end
}
class Duck extends Animal, Flyable, Swimmable {
  function init(self, name)
    super.init(self, name)
  end
}

local duck = Duck("Donald")
assert(duck:fly() == "flying")
assert(duck:swim() == "swimming")
assert(duck is Duck)
assert(duck is Animal)
assert(duck is Flyable)
assert(duck is Swimmable)
-- as 与多继承类
assert((duck as Flyable) ~= nil)
assert((duck as Swimmable) ~= nil)
print("multi_inherit_as: 1")

-- =====================================================
-- Test 11: clone() 与多继承类
-- =====================================================
local duck2 = duck:clone()
assert(duck2.name == "Donald")
assert(duck2:fly() == "flying")
assert(duck2:swim() == "swimming")
assert(duck2 is Duck)
assert(duck2 is Flyable)
assert(duck2 is Swimmable)
print("clone_multi: 1")

print("=== 017 oop_advanced END ===")