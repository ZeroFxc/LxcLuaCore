
(component
  (core module $M
    (func (export "add") (param i32 i32) (result i32)
      local.get 0
      local.get 1
      i32.add)
    (func (export "mul") (param i32 i32) (result i32)
      local.get 0
      local.get 1
      i32.mul))
  (core instance $i (instantiate $M))
  (func (export "add")
    (param "a" u32)
    (param "b" u32)
    (result u32)
    (canon lift (core func $i "add")
      (param i32 i32) (result i32)))
  (func (export "mul")
    (param "a" u32)
    (param "b" u32)
    (result u32)
    (canon lift (core func $i "mul")
      (param i32 i32) (result i32)))
)
