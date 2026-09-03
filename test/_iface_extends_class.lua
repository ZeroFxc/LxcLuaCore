-- 测试：接口能否 extends 类（反方向）
class Base {
  function hello() return "hello" end
}

interface IX extends Base {
}

print("IX created OK")
