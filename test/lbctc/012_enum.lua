-- covers: enum, enum class, do...end, begin...end, {}, TK_ENUM, :names(), :values() 反射
-- 期望语法：
-- enum MyEnum do OPTION_1, OPTION_2, OPTION_3 end
-- enum MyEnum2 begin OPT_A=0, OPT_B, OPT_C=5, OPT_D end
-- enum class ScopedEnum { S_OPT_1, S_OPT_2, S_OPT_3 }
-- enum BraceEnum { B_1, B_2, B_3=10, B_4 }

print("=== 012 enum START ===")

enum MyEnum do
    OPTION_1,
    OPTION_2,
    OPTION_3
end
print("OPT_1: " .. OPTION_1)
print("OPT_2: " .. OPTION_2)

enum MyEnum2 begin
    OPT_A = 0,
    OPT_B,
    OPT_C = 5,
    OPT_D
end
print("OPT_A: " .. OPT_A)
print("OPT_B: " .. OPT_B)
print("OPT_C: " .. OPT_C)
print("OPT_D: " .. OPT_D)

print("by_name_OPT_A: " .. MyEnum2.OPT_A)

enum class ScopedEnum {
    S_OPT_1,
    S_OPT_2,
    S_OPT_3
}
print("SCOPED_S1: " .. ScopedEnum.S_OPT_1)
print("SCOPED_S2: " .. ScopedEnum.S_OPT_2)

enum BraceEnum {
    B_1,
    B_2,
    B_3 = 10,
    B_4
}
print("B_3: " .. B_3)
print("B_4: " .. B_4)

local nlist = MyEnum2:names()
print("names_csv: " .. table.concat(nlist, ","))

local vlist = MyEnum2:values()
local vcsv = {}
for i = 1, #vlist do vcsv[i] = tostring(vlist[i]) end
print("values_csv: " .. table.concat(vcsv, ","))

print("=== 012 enum END ===")
