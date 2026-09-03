-- LXCLUA: map (使用 map 字面量语法 [key=val, ...])
local m = [ a = 1, b = 2 ]
print("map a", m:get("a"))
print("map b", m:get("b"))
print("map size", map.size(m))
for k, v in map.keys(m), map.values(m), 1 do
  if k ~= nil then
    print("map kv", k, v)
  end
end
