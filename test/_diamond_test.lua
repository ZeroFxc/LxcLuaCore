-- 钻石继承测试: A -> (B, C) -> D
class A {
  function init(self)
    self.log = (self.log or "") .. "A "
  end
  function who(self)
    return "A"
  end
  function greet(self)
    return "hello from A"
  end
}

class B extends A {
  function who(self)
    return "B"
  end
}

class C extends A {
  -- 不覆盖 who，继承 A 的
  function cOnly(self)
    return "C only"
  end
}

class D extends B, C {
}

local d = D()
print("who:", d:who())          -- 期望 B（声明在前优先）
print("greet:", d:greet())      -- 期望 A（唯一来源）
print("cOnly:", d:cOnly())      -- 期望 C only
print("mro:")
for i, c in ipairs(D.__mro) do
  print("  " .. i .. ": " .. tostring(c.__name or "?"))
end

-- 检查方法是否只有一份（同一个函数对象）
print("D.greet == A.greet:", D.__methods.greet == A.__methods.greet)

-- super 链测试：B 覆盖 greet，C 也覆盖，看 super 走到哪
class A2 {
  function greet(self) return "A2" end
}
class B2 extends A2 {
  function greet(self) return "B2->" .. super.greet(self) end
}
class C2 extends A2 {
  function greet(self) return "C2->" .. super.greet(self) end
}
class D2 extends B2, C2 {
  function greet(self) return "D2->" .. super.greet(self) end
}
local d2 = D2()
print("chain:", d2:greet())     -- 期望 D2->B2->C2->A2（协作式 MRO）
