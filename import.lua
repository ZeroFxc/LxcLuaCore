-- import.lua 模块
local import_func = function(str)
    print("import called with: " .. str)
    return str
end

-- 关键：需要将 import 设置为全局变量
_G.import = import_func