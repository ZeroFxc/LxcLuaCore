-- 测试 export 在模块内的效果
package.preload["test"] = function()
    export version = 2
    export function add(a, b)
        return a + b
    end
end

local mod = require("test")
print("version:", mod.version)
print("add(1,2):", mod.add(1, 2))