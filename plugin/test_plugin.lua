local plugin = require("plugin")

print("Testing plugin API")

local data = plugin.parse("mock data")
print("Parse output: " .. tostring(data))

local ok = plugin.install("test_pkg")
print("Install success: " .. tostring(ok))

plugin.shop_connect("https://test.shop.com")
