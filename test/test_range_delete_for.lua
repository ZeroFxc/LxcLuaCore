-- 测试 range 表达式、delete 语句、for in 直接迭代

print("=== 测试1: range 正向迭代 ===")
local sum = 0
for i in range(1, 5) do
    print("  i =", i)
    sum = sum + i
end
assert(sum == 15, "range(1,5) sum should be 15, got " .. sum)
print("  通过: sum =", sum)

print("=== 测试2: range 反向迭代 ===")
local rev = {}
for i in range(5, 1, -1) do
    rev[#rev + 1] = i
end
assert(#rev == 5, "should have 5 elements")
assert(rev[1] == 5 and rev[2] == 4 and rev[3] == 3 and rev[4] == 2 and rev[5] == 1, "reverse order wrong")
print("  通过: " .. table.concat(rev, ", "))

print("=== 测试3: range 步长为2 ===")
local count = 0
for i in range(0, 10, 2) do
    count = count + 1
end
assert(count == 6, "range(0,10,2) should have 6 elements, got " .. count)
print("  通过: count =", count)

print("=== 测试4: for in 直接迭代表 (pairs) ===")
local t = {a = 1, b = 2, c = 3}
local keys = {}
local vals = 0
for k, v in t do
    keys[#keys + 1] = k
    vals = vals + v
end
assert(vals == 6, "values sum should be 6, got " .. vals)
assert(#keys == 3, "should have 3 keys, got " .. #keys)
print("  通过: keys count =", #keys, ", vals sum =", vals)

print("=== 测试5: for in 直接迭代数组 (ipairs) ===")
local arr = {10, 20, 30}
local arr_sum = 0
for i, v in arr do
    arr_sum = arr_sum + v
end
assert(arr_sum == 60, "array sum should be 60, got " .. arr_sum)
print("  通过: arr_sum =", arr_sum)

print("=== 测试6: delete t.key ===")
local dt = {key = "hello", other = "world"}
delete dt.key
assert(dt.key == nil, "dt.key should be nil after delete")
assert(dt.other == "world", "dt.other should still exist")
print("  通过: dt.key =", dt.key, ", dt.other =", dt.other)

print("=== 测试7: delete t['key'] ===")
local dt2 = {x = 100, y = 200}
delete dt2["x"]
assert(dt2.x == nil, "dt2.x should be nil after delete")
assert(dt2.y == 200, "dt2.y should still exist")
print("  通过: dt2.x =", dt2.x, ", dt2.y =", dt2.y)

print("=== 测试8: delete 嵌套访问 ===")
local dt3 = {a = {b = {c = 42}}}
delete dt3.a.b.c
assert(dt3.a.b.c == nil, "dt3.a.b.c should be nil after delete")
assert(dt3.a.b ~= nil, "dt3.a.b should still exist")
print("  通过: dt3.a.b.c =", dt3.a.b.c)

print("\n所有测试通过!")