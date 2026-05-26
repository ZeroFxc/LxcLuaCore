-- ================================================================
-- 网络/文件系统/系统库边界测试: http, fs, process, thread, patch
-- ================================================================
local passed, failed = 0, 0
local function check(name, ok, msg)
    if ok then passed = passed + 1
    else failed = failed + 1; print("  FAIL: " .. (msg or name)) end
end

-- ================================================================
-- 1. fs (文件系统)
-- ================================================================
do
    local fs = require("fs")
    check("fs loaded", type(fs) == "table")

    for _, fn in ipairs({"ls","isdir","isfile","mkdir","rm","exists","stat","currentdir",
                          "chdir","abs","basename","dirname","set_permissions"}) do
        check("fs." .. fn, type(fs[fn]) == "function", fn .. " missing")
    end

    -- currentdir
    local cwd = fs.currentdir()
    check("fs.currentdir", type(cwd) == "string" and #cwd > 0)

    -- exists 不存在的路径
    check("fs.exists nonexist", fs.exists("/nonexistent_path_xyz") == false)

    -- exists 当前目录
    check("fs.exists cwd", fs.exists(cwd) == true)

    -- isdir/isfile
    check("fs.isdir cwd", fs.isdir(cwd) == true)
    check("fs.isfile cwd", fs.isfile(cwd) == false)

    -- abs 路径
    local abs = fs.abs(".")
    check("fs.abs", type(abs) == "string" and #abs > 0)

    -- basename/dirname
    local bn = fs.basename("/a/b/c.txt")
    check("fs.basename", bn == "c.txt")
    local dn = fs.dirname("/a/b/c.txt")
    check("fs.dirname", dn == "/a/b")

    -- stat 当前目录
    local st = fs.stat(".")
    check("fs.stat cwd", type(st) == "table" or type(st) == "userdata")
    if type(st) == "table" then
        check("fs.stat has size", st.size ~= nil)
    end

    -- 不存在的目录
    local ok, err = pcall(function() fs.ls("/nonexistent_path_xyz") end)
    check("fs.ls nonexist", not ok, "should error on bad path")

    -- 空路径
    local ok2, err2 = pcall(function() fs.exists("") end)
    check("fs.exists empty", ok2 == false or err2 ~= nil)
end

-- ================================================================
-- 2. http (HTTP 客户端/服务器)
-- ================================================================
do
    local ok_http, http = pcall(require, "http")
    if ok_http then
        check("http loaded", type(http) == "table")

        for _, fn in ipairs({"get", "post", "server", "client", "socket"}) do
            check("http." .. fn, type(http[fn]) == "function", fn .. " missing")
        end

        -- get 不存在的主机 (不应崩溃)
        local ok, err = pcall(function()
            http.get("http://127.0.0.1:9/nonexist", 1)
        end)
        check("http.get bad url", ok, "should not crash")

        -- socket 创建
        local ok2, sock = pcall(http.socket)
        if ok2 and sock then
            check("http.socket", type(sock) == "userdata" or type(sock) == "table")

            -- 空 bind
            local ok3 = pcall(function() sock:bind("0.0.0.0", 0) end)
            check("socket bind", ok3)

            local ok4 = pcall(function() sock:close() end)
            check("socket close", ok4)
        end
    else
        print("  SKIP: http not available (" .. tostring(http) .. ")")
    end
end

-- ================================================================
-- 3. process
-- ================================================================
do
    local ok_proc, proc = pcall(require, "process")
    if ok_proc then
        check("process loaded", type(proc) == "table")

        for _, fn in ipairs({"open", "getpid"}) do
            check("process." .. fn, type(proc[fn]) == "function", fn .. " missing")
        end

        -- getpid
        local pid = proc.getpid()
        check("process.getpid", type(pid) == "number" and pid > 0)
    else
        print("  SKIP: process not available (" .. tostring(proc) .. ")")
    end
end

-- ================================================================
-- 4. thread
-- ================================================================
do
    local ok_thread, thread = pcall(require, "thread")
    if ok_thread then
        check("thread loaded", type(thread) == "table")

        for _, fn in ipairs({"create", "createx", "channel", "pick", "on", "over", "self", "current"}) do
            check("thread." .. fn, type(thread[fn]) == "function", fn .. " missing")
        end

        -- self/current
        check("thread.self", type(thread.self()) == "userdata")
        check("thread.current", type(thread.current()) == "userdata")

        -- channel 创建
        local ch = thread.channel()
        check("thread.channel", type(ch) == "userdata")

        -- 发送/接收
        if ch and ch.send and ch.recv then
            ch:send("hello")
            local v = ch:recv()
            check("thread channel send/recv", v == "hello")

            -- 空发送
            ch:send(nil)
            local v2 = ch:recv()
            check("thread channel nil", v2 == nil)
        end
    else
        print("  SKIP: thread not available (" .. tostring(thread) .. ")")
    end
end

-- ================================================================
-- 5. patch (内存补丁)
-- ================================================================
do
    local ok_patch, patch = pcall(require, "patch")
    if ok_patch then
        check("patch loaded", type(patch) == "table")

        for _, fn in ipairs({"get_arch", "alloc", "free", "read", "write", "to_num", "to_ptr"}) do
            check("patch." .. fn, type(patch[fn]) == "function", fn .. " missing")
        end

        -- get_arch
        local arch = patch.get_arch()
        check("patch.get_arch", type(arch) == "number")

        -- alloc/free (小内存分配)
        local ptr = patch.alloc(64)
        if type(ptr) == "userdata" or type(ptr) == "number" then
            check("patch alloc", ptr ~= nil and ptr ~= 0)
            if ptr then
                local ok = pcall(function() patch.free(ptr) end)
                check("patch free", ok)
            end
        end
    else
        print("  SKIP: patch not available (" .. tostring(patch) .. ")")
    end
end

print(string.format("\n=== 网络/系统库完成: %d 通过, %d 失败 ===", passed, failed))
if failed > 0 then os.exit(1) end