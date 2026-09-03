-- LXCLUA: struct (字段带默认值)
struct Point {
  x: 0
  y: 0
}

local p = Point{x = 10, y = 20}
print("p.x", p.x)
print("p.y", p.y)
p.x = 100
print("p.x2", p.x)
