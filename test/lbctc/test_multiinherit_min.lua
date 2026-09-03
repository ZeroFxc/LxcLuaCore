class A {
  function fa(self)
    return "A"
  end
}

class B {
  function fb(self)
    return "B"
  end
}

class C extends A, B {
  function fc(self)
    return "C"
  end
}

local c = C()
print("fa: " .. c:fa())
print("fb: " .. c:fb())
print("fc: " .. c:fc())
