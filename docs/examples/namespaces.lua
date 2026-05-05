-- docs/examples/namespaces.lua
-- This file tests namespaces

namespace MyLib {
    function test(x)
        return x * 2
    end
}

local res1 = MyLib::test(10)
print(res1) -- 20

using namespace MyLib;
local res2 = test(20)
print(res2) -- 40
