-- 聚焦回归：访问表登记、final self、静态 init 精确匹配、Trait 禁止实例化

local static_calls = 0

class DefectProbe {
  private function hidden()
    return self.value
  end

  protected function guarded()
    return self.value + 1
  end

  final function stable()
    return self.value + 2
  end

  static function helper()
    static_calls = static_calls + 1
  end
}

-- private/protected 方法必须只登记到对应访问表。
assert(type(DefectProbe.__privates.hidden) == "function")
assert(type(DefectProbe.__protected.guarded) == "function")
assert(DefectProbe.__methods.hidden == nil)
assert(DefectProbe.__methods.guarded == nil)

-- final 实例方法省略显式 self 时仍可通过冒号调用。
local probe = DefectProbe()
probe.value = 40
assert(probe:stable() == 42)

-- 普通静态方法不能被当作静态构造函数自动执行。
assert(static_calls == 0)
DefectProbe.helper()
assert(static_calls == 1)

local init_calls = 0
class StaticInitProbe {
  static function init()
    init_calls = init_calls + 1
  end
}
assert(init_calls == 1)

trait NoInstance {
  function mixed(self)
    return true
  end
}

-- Trait 仅用于混入，直接调用必须明确失败。
local ok, err = pcall(function()
  return NoInstance()
end)
assert(not ok)
assert(string.find(err, "cannot instantiate trait", 1, true) ~= nil)

print("class_defect_regression: ok")
