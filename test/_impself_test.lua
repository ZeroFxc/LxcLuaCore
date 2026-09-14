class P {
  function init(n)
    self.name = n
  end
  -- 测试1：直接写裸标识符 name（不写 self.）
  function getName()
    return name
  end
}

local p = P("tom")
print("getName:", p:getName())
