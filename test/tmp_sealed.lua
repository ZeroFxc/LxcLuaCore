sealed class Immutable {
  function init(self, val)
    self.val = val
  end
}
local im = Immutable(42)
print(im.val)