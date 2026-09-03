-- covers: OP_NEWTABLE, OP_SETTABLE, OP_CLOSURE, OP_CALL, trait方法混入, use trait, implements interface, require声明

print("=== 010 trait START ===")

trait Additive {
  function add(self, a, b) return a + b end
  function sub(self, a, b) return a - b end
}

interface ShapeArea {
  require function area()
  require function describe()
}

class Rectangle implements ShapeArea use Additive {
  function init(self, w, h)
    self.w = w
    self.h = h
  end
  function area(self)
    return self.w * self.h
  end
  function describe(self)
    return "Rectangle(" .. self.w .. " x " .. self.h .. ")"
  end
}

local r = Rectangle(5, 6)
print("has_trait: 1")
print("trait_add: " .. r:add(5, 3))
print("trait_sub: " .. r:sub(5, 3))
print("has_interface: 1")
print("impl_area: " .. r:area())
print("impl_say: " .. r:describe())

print("=== 010 trait END ===")
