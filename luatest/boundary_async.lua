-- ================================================================
-- 异步/事件库边界测试: leventloop, laio (promise)
-- ================================================================
local passed, failed = 0, 0
local function check(name, ok, msg)
    if ok then passed = passed + 1
    else failed = failed + 1; print("  FAIL: " .. (msg or name)) end
end

-- ================================================================
-- 1. EventLoop
-- ================================================================
do
    local ok_el, el = pcall(require, "leventloop")
    if ok_el then
        check("leventloop loaded", type(el) == "table")
        -- 检查关键函数是否存在
        for _, fn in ipairs({"add_task", "add_timer", "run", "stop", "sleep"}) do
            check("leventloop." .. fn, type(el[fn]) == "function", fn .. " missing")
        end

        -- run 零时间循环
        local ok = pcall(function() el.run(0) end)
        check("leventloop run(0)", ok)

        -- add_task 空函数
        local fired = false
        local ok2 = pcall(function()
            el.add_task(function() fired = true end)
        end)
        if ok2 then
            el.run(0.01)
        end
        check("leventloop add_task", ok2)
    else
        print("  SKIP: leventloop not available (" .. tostring(el) .. ")")
    end
end

-- ================================================================
-- 2. AsyncIO (laio)
-- ================================================================
do
    local ok_aio, aio = pcall(require, "laio")
    if ok_aio then
        check("laio loaded", type(aio) == "table")

        for _, fn in ipairs({"run_async", "await", "sleep", "http_get", "read_file"}) do
            check("laio." .. fn, type(aio[fn]) == "function", fn .. " missing")
        end

        -- sleep 异步
        local ok = pcall(function()
            aio.run_async(function()
                aio.sleep(0.001)
            end)
        end)
        check("laio sleep", ok)

        -- await 简单值
        local ok2 = pcall(function()
            aio.run_async(function()
                local result = aio.await(42)
            end)
        end)
        check("laio await literal", ok2)

        -- http_get (会失败但不应该崩溃)
        local ok3 = pcall(function()
            aio.run_async(function()
                local ok_r, result = pcall(aio.http_get, "http://127.0.0.1:1/nonexist")
            end)
        end)
        check("laio http_get fail", ok3, "should not crash on bad URL")
    else
        print("  SKIP: laio not available (" .. tostring(aio) .. ")")
    end
end

-- ================================================================
-- 3. Promise
-- ================================================================
do
    local ok_p, promise = pcall(require, "lpromise")
    if ok_p then
        check("promise loaded", type(promise) == "table")

        for _, fn in ipairs({"new", "resolve", "reject", "all", "race", "then", "catch"}) do
            check("promise." .. fn, type(promise[fn]) == "function", fn .. " missing")
        end

        -- 创建并 resolve
        local ok = pcall(function()
            local p = promise.new(function(resolve, reject)
                resolve("ok")
            end)
        end)
        check("promise new+resolve", ok)

        -- 创建并 reject
        local ok2 = pcall(function()
            local p = promise.new(function(resolve, reject)
                reject("fail")
            end)
        end)
        check("promise new+reject", ok2)

        -- Promise.all 空数组
        local ok3 = pcall(function()
            local p = promise.all({})
        end)
        check("promise all empty", ok3)

        -- Promise.all 单元素
        local ok4 = pcall(function()
            local p = promise.all({promise.resolve(1)})
        end)
        check("promise all single", ok4)

        -- 链式 then
        local ok5 = pcall(function()
            local p = promise.resolve(1):then_(function(v) return v + 1 end)
        end)
        check("promise then chain", ok5)

        -- catch 链
        local ok6 = pcall(function()
            local p = promise.reject("err"):catch(function(e) return "caught" end)
        end)
        check("promise catch", ok6)
    else
        print("  SKIP: promise not available (" .. tostring(promise) .. ")")
    end
end

print(string.format("\n=== 异步库完成: %d 通过, %d 失败 ===", passed, failed))
if failed > 0 then os.exit(1) end