-- covers: OP_MULTIINHERIT, 多父类继承, extends 逗号分隔语法, 方法解析

print("=== 016 multi_inherit START ===")

-- 基本多继承测试
class A {
  function fa(self)
    return "A"
  end
  function common(self)
    return "A_common"
  end
}

class B {
  function fb(self)
    return "B"
  end
  function common(self)
    return "B_common"
  end
}

class C extends A, B {
  function fc(self)
    return "C"
  end
}

local c = C()
print("c_fa: " .. c:fa())
print("c_fb: " .. c:fb())
print("c_fc: " .. c:fc())

-- 方法覆盖：C 的 common 应覆盖父类
-- 多继承中，先继承的父类方法优先
print("c_common: " .. c:common())

-- 单继承向后兼容
class D : A {
  function fd(self)
    return "D"
  end
}
local d = D()
print("d_fa: " .. d:fa())
print("d_fd: " .. d:fd())

-- 使用 extends 关键字的多继承
class E extends A, B {
  function fe(self)
    return "E"
  end
}
local e = E()
print("e_fa: " .. e:fa())
print("e_fb: " .. e:fb())
print("e_fe: " .. e:fe())

-- 三父类继承
class F {
  function ff(self)
    return "F"
  end
}
class G extends A, B, F {
  function fg(self)
    return "G"
  end
}
local g = G()
print("g_fa: " .. g:fa())
print("g_fb: " .. g:fb())
print("g_ff: " .. g:ff())
print("g_fg: " .. g:fg())

-- 嵌套类中的多继承
class Outer {
  class InnerA {
    function fia(self)
      return "InnerA"
    end
  }
  class InnerB {
    function fib(self)
      return "InnerB"
    end
  }
  class InnerC extends InnerA, InnerB {
    function fic(self)
      return "InnerC"
    end
  }
}

local ic = Outer.InnerC()
print("ic_fia: " .. ic:fia())
print("ic_fib: " .. ic:fib())
print("ic_fic: " .. ic:fic())

print("=== 016 multi_inherit END ===")