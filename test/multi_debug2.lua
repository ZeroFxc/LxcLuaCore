-- 简单多继承调试2
print("=== multi_debug2 START ===")

class A {
  function fa(self)
    return "A"
  end
}

class B {
  function fb(self)
    return "B"
  end
}

-- 测试单继承（向后兼容）
class C1 : A {
}
local c1 = C1()
print("c1.fa: " .. (c1.fa and c1:fa() or "nil"))

-- 测试多继承（两个父类）
class C2 extends A, B {
}
local c2 = C2()
print("c2.fa: " .. type(c2.fa))
print("c2.fb: " .. type(c2.fb))

-- 检查 C2 的父类
local mt = getmetatable(C2)
if mt then
  print("C2.__parent: " .. type(mt.__parent))
  if mt.__parent then
    print("C2.__parent name: " .. tostring(mt.__parent))
  end
  -- 检查 __methods
  print("C2.__methods: " .. type(mt.__methods))
end

-- 测试多继承（只有一个父类，但用 extends 语法）
class C3 extends A {
}
local c3 = C3()
print("c3.fa: " .. (c3.fa and c3:fa() or "nil"))

print("=== multi_debug2 END ===")