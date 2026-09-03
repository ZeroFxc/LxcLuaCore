-- 06_async_example.lua
-- 异步编程示例 (需要 asyncio 模块支持)
-- 运行: lxclua.exe -e "require('asyncio')" examples/06_async_example.lua

local async = require("asyncio")

-- 异步休眠
io.write("Waiting 1 second...\n")
async.sleep(1.0)  -- 异步等待 1 秒
io.write("Done!\n")

-- 异步文件读取
local content = async.read("README.md")
io.write("File size: ", tostring(#content), " bytes\n")

-- 异步 HTTP GET
local response = async.http_get("https://httpbin.org/get")
io.write("HTTP status: ", tostring(response.status_code), "\n")
io.write("Response body length: ", tostring(#response.body), "\n")

-- 定时器
local timer_id = async.set_interval(1.0, function()
  io.write("Tick...\n")
end, 3)  -- 执行 3 次

-- 并行执行
local results = async.parallel(
  function() return async.sleep(0.5) end,
  function() return async.http_get("https://httpbin.org/get") end
)
io.write("Parallel results: ", tostring(#results), " tasks completed\n")