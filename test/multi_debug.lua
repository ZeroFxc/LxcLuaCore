-- 简单多继承调试
print("=== multi_debug START ===")

class A {
  function fa(self)
    return "A"
  end
}

class B {
  function fb(self)
    return "B"
  end
}

-- 测试1: 基本多继承
class C extends A, B {
}

-- 检查 C 的元表
local mt = getmetatable(C)
print("C has metatable: " .. (mt ~= nil and "yes" or "no"))
if mt then
  print("C.__index type: " .. type(mt.__index))
  print("C.__parent type: " .. type(mt.__parent))
  if mt.__parent then
    print("C.__parent type: " .. type(mt.__parent))
    -- 检查 __parent 是否是表
    if type(mt.__parent) == "table" then
      local parent_mt = getmetatable(mt.__parent)
      print("parent has metatable: " .. (parent_mt ~= nil and "yes" or "no"))
    end
  end
end

-- 直接调用 C 查看
local c = C()
print("c type: " .. type(c))
print("c fa: " .. type(c.fa))
print("c fb: " .. type(c.fb))

print("=== multi_debug END ===")