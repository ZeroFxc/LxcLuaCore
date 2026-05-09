jit.on()

function make_counter()
  local i = 0
  return function()
    i = i + 1
    return i
  end
end

local c = make_counter()
print(c())
print(c())
