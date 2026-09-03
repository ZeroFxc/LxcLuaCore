-- 测试：类能否 extends 接口
interface IFly {
  require function fly()
  require function glide()
}

class Bird extends IFly {
  function fly() return "flying" end
  function glide() return "gliding" end
}

local b = Bird()
print("fly:", b:fly())
print("instanceof IFly:", b instanceof IFly)
print("Bird.__parent == IFly:", Bird.__parent == IFly)
