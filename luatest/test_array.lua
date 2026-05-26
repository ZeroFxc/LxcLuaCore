-- 测试 JS 数组在 Lua 中的迭代
local quickjs = require("quickjs")
local rt = quickjs.newRuntime()
local ctx = rt:newContext()

-- 用户的原始测试代码
local arrRes = ctx:eval([[
function testArray() {
    let arr = [5, 2, 9, 1, 7]
    arr.sort((a,b)=>a-b)
    let sum = arr.reduce((a,b)=>a+b, 0)
    arr.push(10)
    let evenArr = arr.filter(n=>n%2===0)
    return {
        sortArr: arr,
        total: sum,
        evenList: evenArr
    }
}
testArray()
]])

-- 测试属性访问 (用户原始需求)
print("数组总和:", arrRes.total)

-- 测试 # 长度操作符 (新增 __len)
print("sortArr 长度:", #arrRes.sortArr)
print("evenList 长度:", #arrRes.evenList)

-- 测试 ipairs 迭代 (新增 __ipairs)
-- 用户错误: for _,v in pairs(arrRes.sortArr) do print(v) end
-- 正确:    for i,v in ipairs(arrRes.sortArr) do print(i,v) end
print("")
print("--- ipairs 遍历 sortArr ---")
for i, v in ipairs(arrRes.sortArr) do
    print("  [" .. i .. "] =", v)
end

print("")
print("--- ipairs 遍历 evenList ---")
for i, v in ipairs(arrRes.evenList) do
    print("  [" .. i .. "] =", v)
end

-- 测试 pairs 遍历对象属性 (新增 __pairs)
print("")
print("--- pairs 遍历 arrRes (对象) ---")
for k, v in pairs(arrRes) do
    print("  " .. k .. " =", v)
end

-- 测试多维数组迭代
print("")
local mtx = ctx:eval([[ [ [1,2],[3,4] ], [ [5,6],[7,8] ] ]])
print("矩阵长度:", #mtx)
for i, row in ipairs(mtx) do
    for j, cell in ipairs(row) do
        print("  mtx[" .. i .. "][" .. j .. "] =", cell)
    end
end

-- 测试索引访问 (__index 已支持数字索引)
print("")
print("--- 索引访问 ---")
print("sortArr[1] =", arrRes.sortArr[1])
print("sortArr[3] =", arrRes.sortArr[3])
print("sortArr[6] =", arrRes.sortArr[6])

print("")
print("=== 全部测试完成 ===")