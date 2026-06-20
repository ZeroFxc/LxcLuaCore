--[[
  map 容器类型测试
  测试：字面量、读写、迭代、标准库、类型隔离、语法冲突
]]

local passed = 0
local failed = 0

local function test(name, fn)
  local ok, err = pcall(fn)
  if ok then
    passed = passed + 1
    print(string.format("[PASS] %s", name))
  else
    failed = failed + 1
    print(string.format("[FAIL] %s: %s", name, tostring(err)))
  end
end

-- ============================================================
-- 1. 基本字面量和类型
-- ============================================================
test("空map字面量", function()
  local m = []
  assert(type(m) == "map", "type should be 'map'")
  assert(ismap(m), "ismap should return true")
  assert(not istable(m), "map should not be a table")
  assert(#m == 0, "empty map should have length 0")
end)

test("map字面量 name sugar", function()
  local m = [a = 1, b = 2, c = 3]
  assert(#m == 3, "map should have 3 entries")
  assert(m["a"] == 1)
  assert(m["b"] == 2)
  assert(m["c"] == 3)
end)

test("map字面量 [expr] key", function()
  local m = [ [1 + 2] = "three", [true] = "bool", [3.14] = "pi"]
  assert(#m == 3)
  assert(m[3] == "three")
  assert(m[true] == "bool")
  assert(m[3.14] == "pi")
end)

test("map字面量混合", function()
  local m = [name = "test", [42] = "answer", [true] = "yes"]
  assert(#m == 3)
  assert(m["name"] == "test")
  assert(m[42] == "answer")
  assert(m[true] == "yes")
end)

-- ============================================================
-- 2. 读写操作
-- ============================================================
test("map读写", function()
  local m = []
  m["key"] = "value"
  assert(m["key"] == "value")
  m[123] = 456
  assert(m[123] == 456)
  m[true] = false
  assert(m[true] == false)
end)

test("map覆盖写入", function()
  local m = [a = 1]
  m["a"] = 100
  assert(m["a"] == 100)
  assert(#m == 1)
end)

test("map不存在的键返回nil", function()
  local m = [a = 1]
  assert(m["nonexistent"] == nil)
end)

test("map用nil键", function()
  local m = []
  m[nil] = "nil_key"
  assert(m[nil] == "nil_key")
end)

-- ============================================================
-- 3. 迭代器
-- ============================================================
test("mpairs迭代", function()
  local m = [a = 1, b = 2, c = 3]
  local count = 0
  local sum = 0
  for k, v in mpairs(m) do
    count = count + 1
    sum = sum + v
  end
  assert(count == 3, "should iterate 3 entries")
  assert(sum == 6, "sum should be 6")
end)

test("mpairs传入table报错", function()
  local ok = pcall(function()
    for k, v in mpairs({a = 1}) do end
  end)
  assert(not ok, "mpairs on table should error")
end)

test("pairs对map仍然可用（回退到next）", function()
  local m = [a = 1, b = 2]
  local count = 0
  for k, v in pairs(m) do
    count = count + 1
  end
  assert(count == 2, "pairs should work on map")
end)

test("ipairs对map报错", function()
  local ok = pcall(function()
    for k, v in ipairs([a=1]) do end
  end)
  assert(not ok, "ipairs on map should error")
end)

-- ============================================================
-- 4. map标准库
-- ============================================================
test("map.keys", function()
  local m = [a = 1, b = 2]
  local keys = map.keys(m)
  assert(#keys == 2)
end)

test("map.values", function()
  local m = [a = 1, b = 2]
  local vals = map.values(m)
  assert(#vals == 2)
end)

test("map.size", function()
  local m = [a = 1, b = 2, c = 3]
  assert(map.size(m) == 3)
end)

test("map.has", function()
  local m = [a = 1]
  assert(map.has(m, "a"))
  assert(not map.has(m, "b"))
end)

test("map.get", function()
  local m = [a = 1]
  assert(map.get(m, "a") == 1)
  assert(map.get(m, "b", 999) == 999)
  assert(map.get(m, "b") == nil)
end)

test("map.set", function()
  local m = [a = 1]
  local r = map.set(m, "b", 2)
  assert(r == m, "set should return map itself")
  assert(m["b"] == 2)
end)

test("map.remove", function()
  local m = [a = 1, b = 2]
  assert(map.remove(m, "a") == true)
  assert(#m == 1)
  assert(map.remove(m, "nonexistent") == false)
end)

test("map.clear", function()
  local m = [a = 1, b = 2, c = 3]
  map.clear(m)
  assert(#m == 0)
end)

test("map.copy", function()
  local m1 = [a = 1, b = 2]
  local m2 = map.copy(m1)
  m2["a"] = 100
  assert(m1["a"] == 1, "copy should be deep")
  assert(m2["a"] == 100)
end)

test("map.merge", function()
  local m1 = [a = 1]
  local m2 = [b = 2, c = 3]
  map.merge(m1, m2)
  assert(#m1 == 3)
  assert(m1["b"] == 2)
  assert(m1["c"] == 3)
end)

-- ============================================================
-- 5. 类型隔离
-- ============================================================
test("map和table类型隔离", function()
  local m = []
  local t = {}
  assert(ismap(m))
  assert(not ismap(t))
  assert(istable(t))
  assert(not istable(m))
  assert(type(m) == "map")
  assert(type(t) == "table")
end)

test("table库函数不接受map", function()
  local m = [a = 1]
  local ok = pcall(function()
    table.insert(m, 42)
  end)
  assert(not ok, "table.insert on map should error")
end)

test("map库函数不接受table", function()
  local t = {a = 1}
  local ok = pcall(function()
    map.keys(t)
  end)
  assert(not ok, "map.keys on table should error")
end)

-- ============================================================
-- 6. 语法冲突测试
-- ============================================================
test("map中 [expr] key 与表访问不冲突", function()
  local t = {x = 10}
  local m = [ [t["x"]] = "from_table" ]
  assert(m[10] == "from_table")
end)

test("嵌套map", function()
  local inner = [a = 1]
  local outer = [data = inner, [42] = "num"]
  assert(outer["data"] == inner)
  assert(outer["data"]["a"] == 1)
  assert(outer[42] == "num")
end)

test("map作为值的键", function()
  local m1 = []
  local m2 = [ [m1] = "map_as_key" ]
  -- map作为键，使用引用相等
  assert(m2[m1] == "map_as_key")
end)

test("各种类型作为键", function()
  local m = []
  m["string"] = 1
  m[123] = 2
  m[3.14] = 3
  m[true] = 4
  m[false] = 5
  assert(#m == 5)
  assert(m["string"] == 1)
  assert(m[123] == 2)
  assert(m[3.14] == 3)
  assert(m[true] == 4)
  assert(m[false] == 5)
end)

-- ============================================================
-- 7. GC 测试
-- ============================================================
test("map GC回收", function()
  -- 创建大量map，触发GC
  for i = 1, 1000 do
    local m = [x = i, y = i * 2]
  end
  collectgarbage("collect")
  -- 不崩溃即为通过
  assert(true)
end)

test("map嵌套GC", function()
  for i = 1, 100 do
    local m = [inner = [a = 1, b = 2], value = i]
  end
  collectgarbage("collect")
  assert(true)
end)

-- ============================================================
-- 8. 长度操作
-- ============================================================
test("map长度操作", function()
  local m = [a = 1, b = 2, c = 3]
  assert(#m == 3)
  m["c"] = nil  -- 注意：设置为nil不会删除键
  -- 实际上 map.set 会设置 nil 值，但不会删除
  assert(#m == 3)
end)

-- ============================================================
-- 9. 大量数据测试
-- ============================================================
test("大量数据插入和读取", function()
  local m = []
  for i = 1, 1000 do
    m[i] = i * 2
  end
  assert(#m == 1000)
  for i = 1, 1000 do
    assert(m[i] == i * 2)
  end
end)

test("大量数据删除和清空", function()
  local m = []
  for i = 1, 500 do
    m[i] = i
  end
  assert(#m == 500)
  for i = 1, 250 do
    map.remove(m, i)
  end
  assert(#m == 250)
  map.clear(m)
  assert(#m == 0)
end)

-- ============================================================
-- 10. 边界和特殊键
-- ============================================================
test("空字符串键", function()
  local m = []
  m[""] = "empty"
  assert(m[""] == "empty")
  assert(#m == 1)
end)

test("数字0作为键", function()
  local m = []
  m[0] = "zero"
  assert(m[0] == "zero")
end)

test("负数键", function()
  local m = []
  m[-1] = "neg"
  assert(m[-1] == "neg")
end)

test("类型检查严格性", function()
  -- 数字和字符串键区分
  local m = []
  m[1] = "int"
  m["1"] = "string"
  assert(m[1] == "int")
  assert(m["1"] == "string")
  assert(#m == 2)
end)

test("map引用作为键", function()
  local m1 = []
  local m2 = []
  local m = []
  m[m1] = m1
  m[m2] = m2
  assert(m[m1] == m1)
  assert(m[m2] == m2)
  assert(#m == 2)
end)

-- ============================================================
-- 11. JIT 编译测试
-- ============================================================
test("JIT map读写循环", function()
  local m = [a = 1, b = 2]
  local sum = 0
  for i = 1, 1000 do
    sum = sum + m["a"] + m["b"]
    m["c"] = i
    sum = sum + m["c"]
  end
  assert(sum > 0, "JIT should not crash")
end)

test("JIT map创建循环", function()
  local count = 0
  for i = 1, 100 do
    local m = [x = i, y = i * 2]
    count = count + m["x"] + m["y"]
  end
  assert(count > 0, "JIT map creation should not crash")
end)

-- ============================================================
-- 12. 序列化兼容性
-- ============================================================
test("map的tostring行为", function()
  local m = [a = 1]
  local s = tostring(m)
  assert(type(s) == "string")
  -- map的tostring应该包含地址信息，与table/function/userdata一致
  assert(string.find(s, "map") ~= nil)
end)

-- ============================================================
-- 结果汇总
-- ============================================================
print("")
print(string.format("===== 结果: %d 通过, %d 失败 =====", passed, failed))
if failed > 0 then
  os.exit(1)
end