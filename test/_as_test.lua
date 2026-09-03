class A {}
class B extends A {}
local b = B()
local a = b as A
print(type(a), a)