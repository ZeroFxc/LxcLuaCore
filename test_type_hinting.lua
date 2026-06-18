-- 测试类型提示完整功能
print("=== 1. local var: type ===")

local var: string = "hello world"
print(var)

-- 测试类型不匹配警告
local bad: number = "this should warn"
print("bad:", bad)

-- 测试类型推断（无标注时从表达式推断）
local inferred = 42
$getproptype(inferred)

-- 测试类型传播
local propagated = inferred
$getproptype(propagated)

print("=== 2. 函数参数类型 ===")

local function add(a: number, b: number): number
    return a + b
end
print(add(1, 2))

-- 测试参数类型不匹配
local result = add("wrong", 2)  -- 应该警告
print("add result:", result)

print("=== 3. 多返回值类型 ===")
local function get_status(): (bool, string)
    return true, "OK!"
end
local ok, msg = get_status()
print(ok, msg)

print("=== 4. $type 命名类型 ===")
$type Point = { x: number, y: number }
local p: Point = { x = 1, y = 2 }
print(p.x, p.y)
$getproptype(p)

print("=== 5. $type 复杂函数类型 ===")
$type LogCallback = function(msg: string): void
function write_env(log: LogCallback)
    log("test message")
end
write_env(print)
$getproptype(write_env)

print("=== 6. $declare 全局声明 ===")
$declare global_var: string
global_var = "declared global"
print(global_var)

print("=== 7. $declare function 函数声明 ===")
$declare function tonumber(str: string, base: ?number): number
$getproptype(tonumber)
local num = tonumber("42")
print("tonumber:", num)

print("=== 8. 联合类型 ===")
local nullable: ?string = nil
print("nullable:", nullable)
nullable = "hello"
print("nullable:", nullable)
$getproptype(nullable)

local union: string|int = "hello"
print("union str:", union)
union = 42
print("union int:", union)
$getproptype(union)

print("=== 9. void 返回类型 ===")
local function do_nothing(): void
    return
end
do_nothing()

print("=== 10. $getproptype 类型内省 ===")
local x = "hello"
$getproptype(x)

print("\n所有测试完成!")