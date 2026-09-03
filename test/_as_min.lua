class A {}
class B extends A {}
local b = B()
local a = b as A
print("type(a):", type(a))
print("a:", a)
print("a == nil:", a == nil)