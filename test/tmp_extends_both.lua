class Animal {
  function init(self)
    self.name = "animal"
  end
}

class Dog extends Animal {
  function init(self)
    super.init(self)
  end
}

interface Drawable
  require function draw()
end

interface ColorDrawable extends Drawable
  require function getColor()
end

print("ok")