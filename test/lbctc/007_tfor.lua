-- covers: OP_TFORPREP,OP_TFORCALL,OP_TFORLOOP
print("=== 007 tfor START ===")
local a = {}
for _, v in ipairs({10, 20, 30, 40}) do a[#a + 1] = v * 2 end
print("ipairs_dbl: " .. table.concat(a, ","))
local t = {foo = 1, bar = 2, baz = 3}
local ks = {}
for k in pairs(t) do ks[#ks + 1] = k end
table.sort(ks)
print("pairs_keys: " .. table.concat(ks, ","))
local s = "hello:world:foo:bar"
local ws = {}
for w in string.gmatch(s, "[^:]+") do ws[#ws + 1] = w end
print("gmatch: " .. table.concat(ws, "/"))
print("=== 007 tfor END ===")
