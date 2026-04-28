-- 完整测试
print("=== Test 1: NEWTABLE + LOADI + MOVE ===")
local t1
asm(
    newreg r;
    newreg val;
    NEWTABLE r 0 0;
    LOADI val 42;
    MOVE $t1 r
)
print("t1 =", t1)
print("type(t1) =", type(t1))

print("\n=== Test 2: LOADI + MOVE ===")
local x
asm(
    newreg r;
    LOADI r 999;
    MOVE $x r
)
print("x =", x)

print("\n=== Test 3: Multiple asm blocks ===")
local a, b, c
asm(
    newreg r;
    LOADI r 10;
    MOVE $a r;
)
asm(
    newreg r;
    LOADI r 20;
    MOVE $b r;
)
asm(
    newreg r;
    LOADI r 30;
    MOVE $c r;
)
print("a =", a, "b =", b, "c =", c)

print("\n=== Test 4: Second NEWTABLE ===")
local t2
asm(
    newreg r;
    newreg val;
    NEWTABLE r 0 0;
    LOADI val 100;
    MOVE $t2 r
)
print("t2 =", t2)
print("type(t2) =", type(t2))

print("\n=== All tests passed! ===")