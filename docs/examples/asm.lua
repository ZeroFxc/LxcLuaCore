-- docs/examples/asm.lua
-- This file tests inline assembly

local sum = 0
asm(
    newreg cnt;
    newreg acc;
    LOADI cnt 5;
    LOADI acc 0;

    :loop;
    ADD acc acc cnt;
    SUBK cnt cnt #K 1;

    EQI cnt 0 0;
    JMP @loop;

    MOVE $sum acc;
)
print("Sum from asm: " .. sum) -- 15

local result
asm(
    newreg a;
    newreg b;
    LOADI a 10;
    LOADI b 20;
    ADD a a b;
    MOVE $result a;
)
print("Result from asm: " .. result) -- 30
