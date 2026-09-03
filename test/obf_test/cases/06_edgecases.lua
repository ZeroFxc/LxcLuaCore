-- Test: edge cases - empty function, tail recursion, goto, nested functions, OOP
-- Empty function
local function noop() end
print(noop())

-- Tail recursion
local function tail_sum(n, acc)
    if n == 0 then return acc end
    return tail_sum(n - 1, acc + n)
end
print(tail_sum(100, 0))

-- goto and labels
do
    local i = 0
    ::loop::
    i = i + 1
    if i < 3 then goto loop end
    print("goto result:", i)
end

-- goto for break out of nested loop
for i = 1, 5 do
    for j = 1, 5 do
        if i == 2 and j == 3 then
            print("break at", i, j)
            goto done
        end
    end
end
::done::
print("after goto")

-- OOP with metatables
local Animal = {}
Animal.__index = Animal

function Animal.new(name, sound)
    return setmetatable({name = name, sound = sound}, Animal)
end

function Animal:speak()
    return self.name .. " says " .. self.sound
end

function Animal:get_name()
    return self.name
end

local dog = Animal.new("Dog", "Woof")
local cat = Animal.new("Cat", "Meow")
print(dog:speak())
print(cat:speak())
print(dog:get_name())

-- Inheritance
local Cat = setmetatable({}, {__index = Animal})
Cat.__index = Cat
setmetatable(Cat, {__index = Animal})

function Cat.new(name)
    return setmetatable({name = name, sound = "Meow", lives = 9}, Cat)
end

function Cat:lose_life()
    self.lives = self.lives - 1
    return self.lives
end

local kitty = Cat.new("Whiskers")
print(kitty:speak())
print(kitty:lose_life())
print(kitty:lose_life())

-- Deeply nested function calls
local function f1(x) return x + 1 end
local function f2(x) return f1(x) * 2 end
local function f3(x) return f2(x) - 3 end
local function f4(x) return f3(x) / 4 end
print(f4(15))

-- Coroutines (simplified)
local co = coroutine.create(function()
    for i = 1, 3 do
        coroutine.yield(i * 10)
    end
end)

print(coroutine.resume(co))
print(coroutine.resume(co))
print(coroutine.resume(co))
print(coroutine.resume(co))
