-- covers: OP_SETTABUP, OP_GETTABUP, OP_NEWTABLE, OP_SETFIELD, OP_GETFIELD, OP_CONCAT, namespace块定义, 嵌套名字空间, using namespace引入

print("=== 011 namespace START ===")

namespace Math {
  Math.add = function(a, b) return a + b end
  Math.mul = function(a, b) return a * b end
}

namespace Greet {
  Greet.say = function(name) return "Hello, " .. name end
  Greet.Nested = {}
  Greet.Nested.quad = function(x) return x * x * x * x end
}

print("ns_add: " .. Math.add(5, 6))
print("ns_mul: " .. Math.mul(5, 6))
print("ns_hello: " .. Greet.say("Alice"))
print("nested_quad: " .. Greet.Nested.quad(2))

using namespace Math
print("using_add: " .. add(3, 4))

print("=== 011 namespace END ===")
