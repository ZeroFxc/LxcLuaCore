print("=== START ===")

interface Drawable
  require function draw()
end

interface ColorDrawable extends Drawable
  require function getColor()
end

print("=== END ===")