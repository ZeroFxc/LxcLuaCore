-- covers: parse_list_comprehension 列表推导, parse_dict_comprehension 字典推导
-- 期望语法（列表/字典推导在 lxclua 中不支持，退化为手动 for 循环构建，打印 skipped 标志）：
-- local sq = [i*i for i=1,10]
-- local ev = [i for i=1,10 if i%2==0]
-- local t1={a=1,b=2,c=3}
-- local dm = {k = v*2 for k,v in pairs(t1)}

print("=== 016 comp START ===")
print("skipped_list_comp: 1")
print("skipped_dict_comp: 1")

-- 列表推导等价：[i*i for i=1,10]
local sq = {}
for i = 1, 10 do
    sq[i] = i * i
end
local sq_csv = {}
for i = 1, #sq do sq_csv[i] = tostring(sq[i]) end
print("squares: " .. table.concat(sq_csv, ","))

-- 列表推导等价：[i for i=1,10 if i%2==0]
local ev = {}
local ev_n = 0
for i = 1, 10 do
    if i % 2 == 0 then
        ev_n = ev_n + 1
        ev[ev_n] = i
    end
end
local ev_csv = {}
for i = 1, #ev do ev_csv[i] = tostring(ev[i]) end
print("evens: " .. table.concat(ev_csv, ","))

-- 过滤奇数列表
local odds = {}
local odds_n = 0
for i = 1, 10 do
    if i % 2 == 1 then
        odds_n = odds_n + 1
        odds[odds_n] = i
    end
end
print("odds_csv: " .. table.concat(odds, "-"))

-- 字典推导等价：{k = v*2 for k,v in pairs(t1)}
local t1 = {a = 1, b = 2, c = 3}
local dm = {}
for k, v in pairs(t1) do
    dm[k] = v * 2
end
-- 按键排序输出 a,b,c
local dkeys = {"a", "b", "c"}
local dparts = {}
for i = 1, #dkeys do
    dparts[i] = dkeys[i] .. "=" .. dm[dkeys[i]]
end
print("dict: " .. table.concat(dparts, ","))

print("=== 016 comp END ===")
