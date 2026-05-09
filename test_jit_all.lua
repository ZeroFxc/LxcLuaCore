jit.on()

class Vehicle
  function get_wheels() return 4 end
end

class Car extends Vehicle
  function get_type() return "Sedan" end
end

local my_car = Car()
print("Car wheels: " .. my_car.get_wheels())
print("Car type: " .. my_car.get_type())

function make_counter()
  local i = 0
  return function()
    i = i + 1
    return i
  end
end

local c = make_counter()
print("Counter: " .. c())
print("Counter: " .. c())
