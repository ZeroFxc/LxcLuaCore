-- covers: OP_NEWTABLE,OP_SETLIST,OP_SETTABLE,OP_SETI,OP_SETFIELD,OP_GETTABLE,OP_GETI,OP_GETFIELD,OP_GETTABUP,OP_LEN,OP_CONCAT,OP_FORPREP,OP_FORLOOP,OP_TFORPREP,OP_TFORCALL,OP_TFORLOOP,OP_CALL,OP_RETURN
local t = {10, 20, 30, 40, 50, name="tbl", extra=99}
print("=== 002 tables START ===")
print("len_t: " .. tostring(#t))
print("t_1: " .. tostring(t[1]))
print("t_3: " .. tostring(t[3]))
print("t_5: " .. tostring(t[5]))
print("name: " .. t.name)
print("extra: " .. tostring(t.extra))
t[1] = 100
t.extra = 123
print("after_t_1: " .. tostring(t[1]))
print("after_extra: " .. tostring(t.extra))
local s = 0
for _, v in ipairs(t) do s = s + v end
print("ipairs_sum: " .. tostring(s))
local ks = {}
for k, _ in pairs(t) do ks[#ks + 1] = tostring(k) end
table.sort(ks)
print("pairs_keys: " .. table.concat(ks, ","))
local m = {{1, 2}, {3, 4}}
print("m2_2: " .. tostring(m[2][2]))
print("=== 002 tables END ===")
