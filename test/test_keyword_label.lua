-- 测试 ::continue:: 标签（关键字作为标签名）
print("=== 测试 ::continue:: 标签 ===")

for i = 1, 10 do
    if i == 5 then
        goto continue
    end
    print(i)
    ::continue::
end

print("=== 测试 ::break:: 标签 ===")

for i = 1, 10 do
    if i == 8 then
        goto break
    end
    ::break::
end
print("break 标签后的代码")

print("=== 测试 ::goto:: 标签 ===")

goto goto_label
print("这行不应该打印")
::goto_label::
print("goto 标签跳转成功")

print("\n所有测试通过!")