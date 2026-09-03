-- 覆盖: override, 操作符重载, 静态构造, 接口继承, is class, 反射, SEALED

print("=== 015 oop_extended START ===")

-- Test 1: override keyword
class Animal {
  function init(self, name)
    self.name = name
  end

  function speak(self)
    return "generic sound"
  end
}

class Dog extends Animal {
  override function speak(self)
    return "woof! my name is " .. self.name
  end
}

local d = Dog("Buddy")
assert(d:speak() == "woof! my name is Buddy")
print("override_test: 1")

-- Test 2: override with init and super
class Vehicle {
  function init(self, model)
    self.model = model
  end

  function getType(self)
    return "vehicle"
  end
}

class Car extends Vehicle {
  override function init(self, model, doors)
    super.init(self, model)
    self.doors = doors
  end

  function getDoors(self)
    return self.doors
  end
}

local c = Car("Tesla", 4)
assert(c.model == "Tesla")
assert(c.doors == 4)
assert(c:getType() == "vehicle")
print("override_init_test: 1")

-- Test 3: 操作符重载
class Vector {
  function init(self, x, y)
    self.x = x
    self.y = y
  end
  function __add(self, other)
    return Vector(self.x + other.x, self.y + other.y)
  end
  function __tostring(self)
    return "Vector(" .. self.x .. ", " .. self.y .. ")"
  end
}

local v1 = Vector(1, 2)
local v2 = Vector(3, 4)
local v3 = v1 + v2
assert(v3.x == 4)
assert(v3.y == 6)
print("v3_x: " .. v3.x)
print("v3_y: " .. v3.y)

-- Test 4: 静态构造函数
class Config {
  static version = ""
  static function init()
    Config.version = "1.0.0"
  end
}
assert(Config.version == "1.0.0")
print("config_version: " .. Config.version)

-- Test 5: 接口继承
interface Drawable
  require function draw()
end

interface ColorDrawable extends Drawable
  require function getColor()
end

class Circle implements ColorDrawable {
  function init(self, r)
    self.r = r
  end
  function draw(self)
    return "drawing circle"
  end
  function getColor(self)
    return "red"
  end
}
local cr = Circle(5)
assert(cr:draw() == "drawing circle")
assert(cr:getColor() == "red")
print("circle_draw: " .. cr:draw())
print("circle_color: " .. cr:getColor())

-- Test 6: is 运算符
assert(cr is Circle)
assert(cr is Drawable)
print("c_is_Circle: 1")
print("c_is_Drawable: 1")

-- Test 7: 反射 API
assert(classname(classof(cr)) == "Circle")
assert(isclass(Circle))
assert(isobject(cr))
assert(isinstance(cr, Circle))
print("classof_c: " .. classname(classof(cr)))
print("isclass: 1")
print("isobject: 1")
print("c_instanceof_Circle: 1")

-- Test 8: SEALED
sealed class Immutable {
  function init(self, val)
    self.val = val
  end
}
local im = Immutable(42)
assert(im.val == 42)
print("im_val: " .. im.val)

print("=== 015 oop_extended END ===")