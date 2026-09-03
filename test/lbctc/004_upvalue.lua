-- covers: OP_CLOSURE,OP_GETUPVAL,OP_SETUPVAL,OP_ADD,OP_CONCAT
print("=== 004 upvalue START ===")
function make_counter(init, label)
  local n = init or 0
  local tag = label or "C"
  return function(delta)
    n = n + delta
    return tag .. "#" .. n
  end
end
local c1 = make_counter(10, "A")
local c2 = make_counter(100, "B")
print("c1_1: " .. c1(1))
print("c1_2: " .. c1(2))
print("c2_1: " .. c2(7))
print("c1_3: " .. c1(3))
print("c2_2: " .. c2(9))
function outer(seed)
  local x = seed
  return function(step)
    return function(mul)
      x = x + step
      return x * mul
    end
  end
end
local gen = outer(5)
local inn = gen(2)
print("inn_3: " .. tostring(inn(3)))
print("inn_4: " .. tostring(inn(4)))
print("=== 004 upvalue END ===")
