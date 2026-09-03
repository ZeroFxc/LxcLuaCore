-- LXCLUA: namespace (调试版本)
namespace MathOps {
  function add(a, b) return a + b end
  function mul(a, b) return a * b end
}
print("ns type", type(MathOps))
for k, v in pairs(MathOps) do print("ns kv", k, type(v), v) end
local ns = MathOps
print("ns add?", ns.add)
print("ns mul?", ns.mul)
if ns.add then print(ns.add(1, 2)) end
if ns.mul then print(ns.mul(3, 4)) end
