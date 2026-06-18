-- 测试枚举功能完整覆盖
print("=== 1. 基本数值枚举 ===")
enum MyEnum do
    OPTION_1,
    OPTION_2,
    OPTION_3
end
assert(OPTION_1 == 1)
assert(OPTION_2 == 2)
assert(OPTION_3 == 3)
print("OPTION_1:", OPTION_1, " OK")

print("=== 2. 带显式赋值的枚举 ===")
enum MyEnum2 begin
    OPT_A = 0,
    OPT_B,
    OPT_C = 5,
    OPT_D
end
assert(OPT_A == 0)
assert(OPT_B == 1)
assert(OPT_C == 5)
assert(OPT_D == 6)
print("OPT_A=0 OPT_B=1 OPT_C=5 OPT_D=6 OK")

print("=== 3. 枚举名访问成员 ===")
assert(MyEnum2.OPT_A == 0)
assert(MyEnum2.OPT_B == 1)
print("MyEnum2.OPT_A:", MyEnum2.OPT_A, " OK")

print("=== 4. 匿名枚举 ===")
enum begin
    ANON_1,
    ANON_2,
    ANON_3
end
assert(ANON_1 == 1)
assert(ANON_2 == 2)
print("ANON_1:", ANON_1, " OK")

print("=== 5. enum class（scoped enum）===")
enum class ScopedEnum begin
    S_OPT_1,
    S_OPT_2,
    S_OPT_3
end
assert(S_OPT_1 == nil)
assert(ScopedEnum.S_OPT_1 == 1)
assert(ScopedEnum.S_OPT_2 == 2)
print("S_OPT_1 is nil:", S_OPT_1 == nil, " ScopedEnum.S_OPT_1:", ScopedEnum.S_OPT_1, " OK")

print("=== 6. 大括号语法 ===")
enum BraceEnum {
    B_1,
    B_2,
    B_3 = 10,
    B_4
}
assert(B_1 == 1)
assert(B_2 == 2)
assert(B_3 == 10)
assert(B_4 == 11)
print("B_1=1 B_2=2 B_3=10 B_4=11 OK")

print("=== 7. 反射方法 :names() ===")
local n = MyEnum2:names()
assert(n[1] == "OPT_A")
assert(n[2] == "OPT_B")
assert(n[3] == "OPT_C")
assert(n[4] == "OPT_D")
print("names:", n[1], n[2], n[3], n[4], " OK")

print("=== 8. 反射方法 :values() ===")
local vals = MyEnum2:values()
assert(vals[1] == 0)
assert(vals[2] == 1)
assert(vals[3] == 5)
assert(vals[4] == 6)
print("values:", vals[1], vals[2], vals[3], vals[4], " OK")

print("=== 9. 反射方法 :kvmap() 遍历 ===")
local kv = MyEnum2:kvmap()
assert(kv.OPT_A == 0)
assert(kv.OPT_B == 1)
assert(kv.OPT_C == 5)
assert(kv.OPT_D == 6)
-- 测试 for k, v in enum:kvmap() 遍历
for k, v in MyEnum2:kvmap() do
    if k == "OPT_A" then assert(v == 0)
    elseif k == "OPT_B" then assert(v == 1)
    elseif k == "OPT_C" then assert(v == 5)
    elseif k == "OPT_D" then assert(v == 6)
    end
end
print("kvmap遍历 OK")

print("=== 10. 反射方法 :vkmap() ===")
local vk = MyEnum2:vkmap()
assert(vk[0] == "OPT_A")
assert(vk[1] == "OPT_B")
assert(vk[5] == "OPT_C")
assert(vk[6] == "OPT_D")
print("vkmap[0]:", vk[0], " vkmap[1]:", vk[1], " vkmap[5]:", vk[5], " OK")

print("=== 11. 成员数量 _nmembers ===")
assert(MyEnum2._nmembers == 4)
assert(MyEnum._nmembers == 3)
assert(ScopedEnum._nmembers == 3)
print("MyEnum2._nmembers:", MyEnum2._nmembers, " OK")

print()
print("全部测试通过！")