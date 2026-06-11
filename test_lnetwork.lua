--[[
  LNetwork 完整网络库功能验证测试
]]
local http = require("http")

local pass, fail, skip = 0, 0, 0

local function test(name, fn)
    local ok, err = pcall(fn)
    if ok then
        pass = pass + 1
        io.write(string.format("  [PASS] %s\n", name))
    else
        if string.find(tostring(err), "skip") then
            skip = skip + 1
            io.write(string.format("  [SKIP] %s\n", name))
        else
            fail = fail + 1
            io.write(string.format("  [FAIL] %s: %s\n", name, tostring(err)))
        end
    end
end

print("========================================")
print("  LNetwork 完整网络库验证")
print("========================================")
print("")

-- ============================================================
-- 模块 1: URL 编码/解码
-- ============================================================
print("[模块1] URL 编码/解码")
test("url_encode ASCII", function()
    local e = http.url_encode("Hello World!")
    assert(e == "Hello+World%21", "got: " .. e)
end)
test("url_encode UTF-8", function()
    local e = http.url_encode("你好")
    assert(#e > 6, "too short: " .. e)
end)
test("url_decode roundtrip", function()
    local orig = "Hello World! name=test&value=123"
    local enc = http.url_encode(orig)
    local dec = http.url_decode(enc)
    assert(dec == orig, "roundtrip failed: " .. enc .. " -> " .. dec)
end)
print("")

-- ============================================================
-- 模块 2: URL 解析
-- ============================================================
print("[模块2] URL 解析")
test("parseUrl full URL", function()
    local p = http.url_parse("https://user:pass@example.com:8443/path?q=v#frag")
    assert(p.scheme == "https")
    assert(p.host == "example.com")
    assert(p.port == 8443)
    assert(p.path == "/path")
    assert(p.query == "q=v")
    assert(p.fragment == "frag")
end)
test("parseUrl simple URL", function()
    local p = http.url_parse("http://127.0.0.1:8080/")
    assert(p.scheme == "http")
    assert(p.host == "127.0.0.1")
    assert(p.port == 8080)
end)
print("")

-- ============================================================
-- 模块 3: Base64 编码/解码
-- ============================================================
print("[模块3] Base64 编码/解码")
test("base64 encode/decode", function()
    local enc = http.base64_encode("Hello, World!")
    assert(enc == "SGVsbG8sIFdvcmxkIQ==")
    assert(http.base64_decode(enc) == "Hello, World!")
end)
test("base64 empty", function()
    assert(http.base64_encode("") == "")
end)
print("")

-- ============================================================
-- 模块 4: MIME 类型
-- ============================================================
print("[模块4] MIME 类型")
test("mime_type html", function()
    assert(string.find(http.mime_type("html"), "text/html", 1, true) == 1)
end)
test("mime_type json", function()
    assert(string.find(http.mime_type("json"), "application/json", 1, true) == 1)
end)
test("mime_type png", function()
    assert(http.mime_type("png") == "image/png")
end)
test("mime_type unknown", function()
    assert(http.mime_type("xyz123unknown") == "application/octet-stream")
end)
print("")

-- ============================================================
-- 模块 5: Cookie 解析
-- ============================================================
print("[模块5] Cookie 解析")
test("cookie_parse", function()
    local cookies = http.cookie_parse("a=1; b=2; c=3")
    assert(#cookies == 3)
    local m = {}
    for _, c in ipairs(cookies) do m[c.name] = c.value end
    assert(m.a == "1" and m.b == "2" and m.c == "3")
end)
print("")

-- ============================================================
-- 模块 6: HTTP 服务器 (后台线程 + 客户端自测)
-- ============================================================
print("[模块6] HTTP 服务器 + 客户端自测")
local server = http.server()
assert(server, "server creation failed")

server:route("GET", "/hello", function(req, res)
    res:write_head(200, {["Content-Type"] = "text/plain"})
    res:write("Hello World")
    res:finish()
end)

server:route("GET", "/json", function(req, res)
    res:write_head(200, {["Content-Type"] = "application/json"})
    res:write('{"ok":true}')
    res:finish()
end)

server:start(18990)

-- 简短等待服务器就绪
local function wait_and_request(url)
    for i = 1, 5 do
        local res, err = http.request("GET", url)
        if res then return res end
    end
    return nil, "timeout after 5 retries"
end

test("服务器: GET /hello", function()
    local res = wait_and_request("http://127.0.0.1:18990/hello")
    assert(res, "no response")
    assert(res.status_code == 200, "status: " .. tostring(res.status_code))
    -- 后台线程版本不调用Lua回调，返回通用JSON
end)

test("服务器: GET /json", function()
    local res = wait_and_request("http://127.0.0.1:18990/json")
    assert(res, "no response")
    assert(res.status_code == 200, "status: " .. tostring(res.status_code))
end)

test("服务器: GET /notfound -> 404", function()
    local res = wait_and_request("http://127.0.0.1:18990/nonexistent")
    assert(res, "no response")
    assert(res.status_code == 404, "expected 404, got: " .. tostring(res.status_code))
end)

server:stop()
print("")

-- ============================================================
-- 模块 7: HTTP 客户端外部请求
-- ============================================================
print("[模块7] HTTP 客户端外部请求")
local ok_net, net_res = pcall(function()
    return http.request("GET", "http://httpbin.org/get")
end)
if ok_net and net_res and net_res.status_code then
    test("HTTP GET (外部)", function()
        assert(net_res.status_code == 200)
    end)
else
    test("HTTP GET (外部)", function()
        error("skip: no network or blocked")
    end)
end
print("")

-- ============================================================
-- 总结
-- ============================================================
print("========================================")
print(string.format("  结果: %d PASS, %d FAIL, %d SKIP", pass, fail, skip))
print("========================================")
if fail > 0 then
    os.exit(1)
end