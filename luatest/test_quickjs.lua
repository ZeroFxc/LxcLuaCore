-- ================================================================
-- quickjs 模块完整使用示例
-- 演示所有 API 的用法：eval、evalModule、registerModule、JSValue 操作等
-- ================================================================

local quickjs = require("quickjs")

print("========== quickjs 模块完整测试 ==========\n")

-- ================================================================
-- 1. 创建运行时和上下文
-- ================================================================
local rt = quickjs.newRuntime()
local ctx = rt:newContext()

print("[1] 运行时和上下文创建成功")

-- ================================================================
-- 2. 查看已注册的模块（调试用）
-- ================================================================
local modules = ctx:dumpModules()
print("[2] 当前注册模块:", table.concat(modules, ", "))

-- ================================================================
-- 3. ctx:eval() — 执行全局脚本（不包含 import/export）
-- ================================================================
print("\n--- [3] ctx:eval() 全局脚本 ---")

-- 基本表达式
local r1 = ctx:eval("1 + 2")
print("  1 + 2 =", r1)

-- 执行多行脚本，返回最后表达式的值
local r2 = ctx:eval([[
  let x = 10;
  let y = 20;
  x * y
]])
print("  x * y =", r2)

-- 在全局作用域中定义函数
ctx:eval([[
  function add(a, b) {
    return a + b;
  }
  var globalVersion = "1.0.0";
]])

-- 通过 getGlobal() 访问全局对象，调用刚才定义的 JS 函数
local js_global = ctx:getGlobal()
local sum = js_global.add(3, 4)
print("  js_global.add(3, 4) =", sum)
print("  js_global.globalVersion =", js_global.globalVersion)

-- ================================================================
-- 4. ctx:evalModule() — 一步执行 ES 模块（推荐！）
--    返回的是模块命名空间，直接 .导出名 访问
-- ================================================================
print("\n--- [4] ctx:evalModule() ES 模块 ---")

local math_mod = ctx:evalModule([[
  export function multiply(a, b) {
    return a * b;
  }
  export const PI = 3.1415926;
  export let counter = 0;
  export function increment() {
    counter++;
    return counter;
  }
]], "math_module.js")

-- 直接通过命名空间访问导出
print("  math_mod.PI =", math_mod.PI)
print("  math_mod.multiply(6, 7) =", math_mod.multiply(6, 7))
print("  math_mod.increment() =", math_mod.increment())
print("  math_mod.increment() =", math_mod.increment())

-- ================================================================
-- 5. ctx:registerModule() + ctx:evalModule() — 模块间 import
--    先注册依赖模块，然后主模块中 import 即可使用
-- ================================================================
print("\n--- [5] import 跨模块引用 ---")

-- 注册一个工具模块
ctx:registerModule("utils", [[
  export function greet(name) {
    return "Hello, " + name + "!";
  }
  export function add(a, b) {
    return a + b;
  }
]])

-- 注册另一个模块
ctx:registerModule("./config.js", [[
  export const APP_NAME = "LuaQuickJS";
  export const VERSION = "2.0.0";
  export const DEBUG = true;
]])

-- 查看注册表
print("  已注册模块:", table.concat(ctx:dumpModules(), ", "))

-- 主模块中 import 以上两个依赖
local app_mod = ctx:evalModule([[
  import { greet, add } from "utils";
  import { APP_NAME, VERSION, DEBUG } from "./config.js";

  export function run() {
    const configStr = APP_NAME + " v" + VERSION + (DEBUG ? " [DEBUG]" : "");
    return greet("User") + " | " + configStr;
  }

  export function calcSum(arr) {
    let total = 0;
    for (let v of arr) {
      total = add(total, v);
    }
    return total;
  }

  export { APP_NAME, VERSION };
]], "app.js")

print("  app_mod.APP_NAME =", app_mod.APP_NAME)
print("  app_mod.VERSION =", app_mod.VERSION)
print("  app_mod.run() =", app_mod.run())

-- JS 函数可以接受 Lua 传过去的数组(table)
local js_array_total = app_mod.calcSum({1, 2, 3, 4, 5})
print("  app_mod.calcSum({1,2,3,4,5}) =", js_array_total)

-- ================================================================
-- 6. ctx:compile() + ctx:evalBinary() — 编译缓存模式
--    适合相同代码多次执行，编译一次即可
-- ================================================================
print("\n--- [6] compile + evalBinary 字节码缓存 ---")

local bytecode = ctx:compile([[
  export function factorial(n) {
    if (n <= 1) return 1;
    return n * factorial(n - 1);
  }
]], "factorial.js", true)  -- 第4个参数 true = module 模式

local fac_mod = ctx:evalBinary(bytecode)
print("  factorial(5) =", fac_mod.factorial(5))
print("  factorial(10) =", fac_mod.factorial(10))

-- 字节码可以重复使用
local fac_mod2 = ctx:evalBinary(bytecode)
print("  factorial(3) (第二次执行) =", fac_mod2.factorial(3))

-- ================================================================
-- 7. JSValue userdata 的操作
--    - 可以像 Lua table 一样读写属性
--    - 可以调用 JS 对象的方法
--    - :tostring() / :tonumber() 做类型转换
-- ================================================================
print("\n--- [7] JSValue 操作 ---")

-- 创建一个 JS 对象
ctx:eval([[
  var person = {
    name: "DifierLine",
    age: 25,
    sayHello: function(greeting) {
      return greeting + ", I'm " + this.name + ", " + this.age + " years old.";
    }
  };
]])

local person = js_global.person
print("  person.name =", person.name)
print("  person.age =", person.age)

-- 调用 JS 对象方法: Lua 的 : 语法把 person 作为 self 传入，
-- l_value_call 会自动检测第一个 JSValue 参数并设为 JS 的 this
local msg = person:sayHello("Hi")
print("  person:sayHello('Hi') =", msg)

-- 修改 JS 对象属性
person.age = 26
print("  修改后 person.age =", person.age)

-- 验证修改生效（在 JS 侧读取）
local age2 = ctx:eval("person.age")
print("  JS 侧读取 person.age =", age2)

-- ================================================================
-- 8. JS 调用 Lua 函数（Lua 函数作为 JS 回调）
-- ================================================================
print("\n--- [8] JS 调用 Lua 回调 ---")

-- 定义一个 Lua 函数，传给 JS
local function lua_handler(a, b)
  print("  [Lua 回调] 收到参数: a=" .. a .. ", b=" .. b)
  return a + b
end

ctx:eval([[
  globalThis.callLua = function(fn, x, y) {
    return fn(x, y);
  };
]])

-- 调用 JS 函数，传入 Lua 函数作为参数
local result = js_global.callLua(lua_handler, 100, 200)
print("  JS 调用 Lua 函数结果:", result)

-- ================================================================
-- 9. 错误处理
-- ================================================================
print("\n--- [9] 错误处理 ---")

local ok, err = pcall(function()
  ctx:eval("this is invalid javascript !!!")
end)
print("  语法错误捕获:", err)

local ok2, err2 = pcall(function()
  ctx:evalModule([[
    import { noSuchExport } from "nonexistent_module";
  ]])
end)
print("  import 不存在的模块:", err2)

-- ================================================================
-- 10. 完整示例: Lua 中跑一个 JS 计算器服务
-- ================================================================
print("\n--- [10] JS 计算器服务 ---")

ctx:registerModule("calculator", [[
  export function evaluate(expr) {
    try {
      return eval(expr);
    } catch (e) {
      return "Error: " + e.message;
    }
  }

  export function statistics(nums) {
    if (!nums || nums.length === 0) {
      return { min: 0, max: 0, sum: 0, avg: 0 };
    }
    let min = nums[0], max = nums[0], sum = 0;
    for (let n of nums) {
      if (n < min) min = n;
      if (n > max) max = n;
      sum += n;
    }
    return {
      min: min,
      max: max,
      sum: sum,
      avg: sum / nums.length
    };
  }
]])

local calc = ctx:evalModule([[
  import { evaluate, statistics } from "calculator";
  export { evaluate, statistics };
]])

print("  calc.evaluate('(100 + 200) * 3') =", calc.evaluate("(100 + 200) * 3"))
print("  calc.evaluate('Math.sqrt(144)') =", calc.evaluate("Math.sqrt(144)"))

local stats = calc.statistics({15, 8, 42, 23, 16, 4})
print("  统计数据:")
print("    min =", stats.min)
print("    max =", stats.max)
print("    sum =", stats.sum)
print("    avg =", stats.avg)

-- ================================================================
-- 11. 异步：setTimeout / Promise（如果安装了事件循环模块）
--    注意: 仅在你编译了 leventloop 模块时可用，否则跳过
-- ================================================================

print("\n========== 全部测试完成 ==========")
print("")
print("API 速查表:")
print("  quickjs.newRuntime()                    -- 创建 JS 运行时")
print("  rt:newContext()                         -- 创建 JS 上下文")
print("")
print("  ctx:eval(script, file, is_module)      -- 执行全局/模块脚本")
print("  ctx:evalModule(script, file)           -- 一步执行 ES 模块, 返回命名空间 (推荐)")
print("  ctx:compile(script, file, is_mod)      -- 编译为字节码")
print("  ctx:evalBinary(bytecode)               -- 执行字节码")
print("  ctx:registerModule(name, code)         -- 注册模块 (使 import 生效)")
print("  ctx:getGlobal()                        -- 获取 JS 全局对象")
print("  ctx:dumpModules()                      -- 列出所有注册的模块名")
print("")
print("  js_value.tostring / js_value:tonumber() -- JSValue -> Lua 类型转换")
print("  js_value.property / js_value:method()   -- 访问 JS 对象属性/方法")