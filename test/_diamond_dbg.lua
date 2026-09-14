-- 单继承链验证 compute_mro 基础正确性
class A {
  function greet(self) return "A" end
}
class B extends A {
}
class C extends A {
}

print("A mro len:", #A.__mro)
print("B mro:")
for i, c in ipairs(B.__mro) do io.write((c.__name or "?") .. " ") end
print()
print("C mro:")
for i, c in ipairs(C.__mro) do io.write((c.__name or "?") .. " ") end
print()

-- 关键：B.__mro 与 C.__mro 是不是同一个表（共享会导致 merge 错乱）
print("B.__mro == C.__mro:", B.__mro == C.__mro)
print("B.__mro == A.__mro:", B.__mro == A.__mro)

-- 两个无共同父类的类多继承
class X { function fx(self) return "x" end }
class Y { function fy(self) return "y" end }
class Z extends X, Y {}
print("Z mro:")
for i, c in ipairs(Z.__mro) do io.write((c.__name or "?") .. " ") end
print()
