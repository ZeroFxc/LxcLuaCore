-- covers: OP_VARARG,OP_GETVARG,OP_VARARGPREP,OP_LOADK,OP_CALL,OP_RETURN
function f(a, b, ...)
  local n = select("#", ...)
  local t = {...}
  local first = select(1, ...) or -1
  local third = select(3, ...) or -1
  local sum = a + b
  for i = 1, n do sum = sum + t[i] end
  return n, first, third, sum, table.concat(t, "|")
end
print("=== 005 vararg START ===")
local n, fv, tv, s, cat = f(1, 2, 10, 20, 30, 40)
print("n: " .. tostring(n))
print("first: " .. tostring(fv))
print("third: " .. tostring(tv))
print("sum: " .. tostring(s))
print("concat: " .. cat)
n, fv, tv, s, cat = f(7, 8)
print("n_empty: " .. tostring(n))
print("first_empty: " .. tostring(fv))
print("sum_empty: " .. tostring(s))
function g(...) return ... end
local r1, r2, r3 = g(111, 222, 333)
print("r1: " .. tostring(r1))
print("r2: " .. tostring(r2))
print("r3: " .. tostring(r3))
print("=== 005 vararg END ===")
