-- docs/examples/oop.lua
-- This file tests OOP extensions

interface Drawable
    function draw()
end

class Shape implements Drawable
    function draw()
        print("Drawing shape")
    end
end

class Circle extends Shape
    private radius = 0

    function __init__(r)
        self.radius = r
    end

    function draw()
        print("Drawing circle: " .. self.radius)
    end

    static function create(r)
        return Circle(r)
    end
end

-- Instantiation
local c = Circle.create(10)
c:draw()

local c2 = Circle(20)
c2:draw()

-- Getters / Setters
class Person
    private _age = 0

    public get age()
        return self._age
    end

    public set age(v)
        if v >= 0 then self._age = v end
    end
end

local p = Person()
p.age = 25
print("Person age: " .. p.age)
