-- covers: OP_NEWMAP,OP_CALL,OP_GETPROP,OP_SETPROP,OP_MAPGET,OP_MAPSET
local m = [a = 1, b = 2, c = 3]
print("=== 003 maps START ===")
print("map_len: " .. tostring(#m))
print("map_a: " .. tostring(m["a"]))
print("map_b: " .. tostring(m["b"]))
print("map_c: " .. tostring(m["c"]))
print("map_missing: " .. tostring(m["x"]))
m["x"] = 99
print("map_after_x: " .. tostring(m["x"]))
print("map_len_after: " .. tostring(#m))
local ps = {}
for k, v in pairs(m) do ps[#ps + 1] = {k, v} end
table.sort(ps, function(a, b) return tostring(a[1]) < tostring(b[1]) end)
local parts = {}
for _, p in ipairs(ps) do parts[#parts + 1] = p[1] .. "=" .. p[2] end
print("map_pairs: " .. table.concat(parts, ","))
print("=== 003 maps END ===")
