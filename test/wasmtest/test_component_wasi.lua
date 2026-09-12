-- 测试 preview2 WASI 组件：hello world（写入 stdout）
local w = require("wasmtime")

local f = io.open("_hello.wasm", "rb")
if not f then print("找不到 _hello.wasm"); return end
local bytes = f:read("*a")
f:close()
print("组件字节数:", #bytes)

local engine = w.newEngine()
local store = w.newStore(engine)

-- 创建 WASI 配置并应用到 store
local wasi = w.newWasi{inheritStdout = true}
print("newWasi:", type(wasi))
if type(wasi) ~= "userdata" then return end
local sok, serr = store:setWasi(wasi)
print("store:setWasi:", sok, serr)
if not sok then return end

local c = w.newComponent(engine, bytes)
print("newComponent:", type(c))
if type(c) ~= "userdata" then return end

local linker = w.newComponentLinker(engine)
local lok = linker:addWasiP2()
print("addWasiP2:", lok ~= nil)
if not lok then return end

local inst, ierr = linker:instantiate(store, c)
print("instantiate:", inst ~= nil, ierr)
if not inst then return end

-- 嵌套路径：wasi:cli/run@0.2.0 instance 下的 run func
local run = inst:getExport("wasi:cli/run@0.2.0", "run")
print("getExport run:", type(run))
if type(run) ~= "userdata" then return end

-- 调用 run（应该向 stdout 输出 Hello, world!）
local ok, res = pcall(function() return run:call() end)
print("run:call pcall:", ok)
if not ok then
    print("call 错误:", res)
    return
end
-- run 返回 result 类型（无 payload 的 result，即 {ok=nil} 或 {err=nil}）
print("run 返回值类型:", type(res))
if type(res) == "table" then
    print("run 返回值:", (next(res) ~= nil) and "非空" or "空表（成功）")
end
print("完成")
