jit.off()
class A {}
class B extends A {}
local b = B()
print("b is A:", b is A)
print("b is B:", b is B)
local a = b as A
print("type(a):", type(a))
print("a:", a)