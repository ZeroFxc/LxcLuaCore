-- 完整服务器测试
local http = require("http")

print("创建服务器...")
local server = http.server()
if not server then
    print("失败: 无法创建服务器")
    os.exit(1)
end

print("添加路由...")
server:route("GET", "/hello", function(req, res)
    print("  服务器: 收到 GET /hello, url=" .. req.url)
    res:write_head(200, {["Content-Type"] = "text/plain"})
    res:write("Hello from LXCLUA server! Your IP: " .. (req.remote_ip or "?") .. "\n")
    res:finish()
end)

server:route("GET", "/json", function(req, res)
    print("  服务器: 收到 GET /json")
    res:write_head(200, {["Content-Type"] = "application/json"})
    res:write('{"status":"ok","platform":"LXCLUA-NCore"}')
    res:finish()
end)

print("启动服务器端口 18999...")
server:start(18999)
print("服务器已启动(后台线程)")

-- 自测连接
print("\n测试1: GET /hello")
local res, err = http.request("GET", "http://127.0.0.1:18999/hello")
if res then
    print("  状态码:", res.status_code)
    print("  响应体:", (res.body or ""):gsub("\n", "\\n"))
else
    print("  失败:", err)
end

print("\n测试2: GET /json")
res, err = http.request("GET", "http://127.0.0.1:18999/json")
if res then
    print("  状态码:", res.status_code)
    print("  响应体:", res.body)
else
    print("  失败:", err)
end

print("\n测试3: GET /notfound (无路由)")
res, err = http.request("GET", "http://127.0.0.1:18999/notfound")
if res then
    print("  状态码:", res.status_code)
    print("  响应体:", (res.body or ""):gsub("\n", "\\n"))
else
    print("  失败:", err)
end

print("\n停止服务器...")
server:stop()
print("服务器已停止")
print("\n全部测试完成!")