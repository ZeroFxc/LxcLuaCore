-- 测试 label ::name:: 与命名空间 obj::member 的歧义处理
-- 验证 suffixedexp 不会把 ::name:: 标签错当字段选择器，也不误杀命名空间访问

function check_eq(actual, expected, label)
    if actual ~= expected then
        error(label .. " FAIL: expected " .. tostring(expected) .. " got " .. tostring(actual))
    end
end

-- ============================================================
-- Part A: namespace 语法 — namespace 块 + :: 调用 + using namespace
-- ============================================================

-- A1: namespace 块定义 + :: 调用
namespace MathLib {
    function add(a, b)
        return a + b
    end
    function mul(a, b)
        return a * b
    end
    PI = 3.14159
}

check_eq(MathLib::add(10, 20), 30, "A1: MathLib::add(10,20) namespace call")
check_eq(MathLib::mul(3, 7), 21, "A2: MathLib::mul(3,7) namespace call")
check_eq(MathLib::PI, 3.14159, "A3: MathLib::PI namespace field access")

-- A4: using namespace 语法
using namespace MathLib;
check_eq(add(5, 6), 11, "A4: using namespace then direct func call")
check_eq(mul(2, 9), 18, "A5: using namespace then direct mul call")
check_eq(PI, 3.14159, "A6: using namespace then direct field access")

-- A7: 嵌套 namespace 块定义 + :: 链式调用
namespace Outer {
    namespace Inner {
        function deep(x)
            return x * 10
        end
    }
}
check_eq(Outer::Inner::deep(7), 70, "A7: Outer::Inner::deep(7) nested namespace call")

-- A8: using namespace 单层 + :: 链式调用
using namespace Outer;
check_eq(Inner::deep(3), 30, "A8: using namespace Outer, then Inner::deep(3)")

-- A9: namespace 与 obj::member 的 C++ 风格字段访问共存
local ns = {
    value = 42,
    nested = {
        deep_val = 99
    }
}
check_eq(ns::value, 42, "A9: ns::value C++ field access")
check_eq(ns::nested::deep_val, 99, "A10: ns::nested::deep_val chained field access")

-- A11: namespace method 内用 :: 访问 self
ns.fn = function(self) return self::value end
check_eq(ns:fn(), 42, "A11: ns:fn() uses :: inside method to access self")

-- A12: using 导入命名空间成员（无 namespace 关键字）
using Outer::Inner;
check_eq(Inner::deep(5), 50, "A12: using Outer::Inner, then Inner::deep(5)")

-- ============================================================
-- Part B: Label 在表达式语句后（不应被 suffixedexp 吃掉 ::）
-- ============================================================
local count = 0
function inc()
    count = count + 1
    return count
end

-- B1: 函数调用后紧跟 label（无分号，核心场景）
inc()
::label1::
check_eq(count, 1, "B1: label after func call")

-- B2: 赋值后紧跟 label
local x = 10
::label2::
check_eq(x, 10, "B2: label after assignment")

-- B3: return 后紧跟 label
local function test_return()
    return 42
::label3::
end
check_eq(test_return(), 42, "B3: label after return")

-- B4: 多个函数调用后紧跟 label
inc()
inc()
::label4::
check_eq(count, 3, "B4: label after multiple calls")

-- B5: 混合场景: 先命名空间，再 label
local a = ns::value
::label5::
check_eq(a, 42, "B5: namespace then label")

-- B6: label 紧跟命名空间链
local b = ns::nested::deep_val
::label6::
check_eq(b, 99, "B6: chained namespace then label")

-- B7: 独立 label
::label7::
local y = 5
check_eq(y, 5, "B7: standalone label")

-- B8: 连续 label
::label8::
::label9::
check_eq(true, true, "B8: consecutive labels")

-- ============================================================
-- Part C: goto 与 namespace 混合
-- ============================================================
::start_goto::
local val = ns::value
check_eq(val, 42, "C1: namespace inside goto block")

print("\n=== 所有测试通过 ===")