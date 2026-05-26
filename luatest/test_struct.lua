-- 隔离测试 struct
print("test start")
struct Point {
  int x;
  int y;
}
print("struct ok")
local p = Point()
print("Point() ok")
p.x = 3; p.y = 4
print("p.x=", p.x, "p.y=", p.y)