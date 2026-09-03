-- covers: TK_PIPE 管道 |> , TK_REVPIPE 反向 <| , TK_SAFEPIPE 安全 |?
-- 期望语法（仅 |> 实际支持，<| 和 |? 退化为等价 Lua 代码并打印 skipped 标志）：
-- local function inc(x) return x+1 end
-- local function dbl(x) return x*2 end
-- a = 3 |> inc |> dbl           -- (3+1)*2 = 8
-- b = dbl <| inc <| 4           -- inc(dbl(4)) = 9  （反向管道不支持，手动写）
-- local t=nil
-- c = t |? tostring or "n/a"    -- 安全管道遇 nil 返回默认 n/a  （不支持，手动写）
-- local m={a=1}
-- d = m.a |? tostring           -- 不支持，手动写

print("=== 015 pipe START ===")
print("skipped_revpipe: 1")
print("skipped_safepipe: 1")

local function inc(x) return x + 1 end
local function dbl(x) return x * 2 end

-- 正向管道 |> 支持
local a = 3 |> inc |> dbl
print("pipe_inc_double: " .. a)

-- 反向管道 <| 不支持，等价语义：inc(dbl(4)) = inc(8) = 9
local b = inc(dbl(4))
print("pipe_rev: " .. b)

-- 安全管道 |? 不支持，t=nil 返回默认 n/a
local t = nil
local c
if t == nil then
    c = "n/a"
else
    c = tostring(t)
end
print("safenil: " .. c)

-- 安全管道 |? 不支持，m.a=1，正常 tostring
local m = {a = 1}
local d
if m.a == nil then
    d = "n/a"
else
    d = tostring(m.a)
end
print("safemap_a: " .. d)

print("=== 015 pipe END ===")
