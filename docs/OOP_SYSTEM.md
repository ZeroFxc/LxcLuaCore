# 面向对象系统

> 基于 `lclass.h`(425行) + `lobject.h` + `lua.h` 源码 + 实际运行测试验证

---

## 1. 类定义

### 1.1 基本语法

LXCLUA 的类使用 `{ }` 块语法，成员间用换行分隔，无需逗号：

```lua
class Animal {
  function init(self, name)
    self.name = name
  end
  function speak(self)
    return $"Animal {self.name} speaks"
  end
}

local a = Animal("Dog")
print(a:speak())  -- Animal Dog speaks
```

构造函数为 `init`。创建对象时 `ClassName(args)` 等价于调用 `init`。

### 1.2 类修饰符

基于 `lclass.h:22-28` 的 CLASS_FLAG 枚举：

| 修饰符 | 标志 | 说明 |
|--------|------|------|
| `abstract class` | `CLASS_FLAG_ABSTRACT` | 不能实例化，包含抽象方法 |
| `final class` | `CLASS_FLAG_FINAL` | 不能被继承 |
| `sealed class` | `CLASS_FLAG_SEALED` | 仅模块内可继承 |
| `singleton class` | `CLASS_FLAG_SINGLETON` | 全局唯一实例 |
| `interface` | `CLASS_FLAG_INTERFACE` | 接口类型 |
| `trait` | `CLASS_FLAG_TRAIT` | Trait 类型 |

```lua
abstract class Shape {
  abstract function area(self)
}
singleton class Config {
  function init(self) self.data = {} end
}
```

---

## 2. 继承

### 2.1 单继承

```lua
class Cat extends Animal {
  function init(self, name, color)
    super.init(self, name)
    self.color = color
  end
  function speak(self)
    return $"Cat {self.name} says meow"
  end
}
```

### 2.2 多继承（C3 线性化）

```lua
class D extends B, C {
  -- C3 线性化 MRO: D → B → C → A
}
```

MRO 存储在 `CLASS_KEY_MRO = "__mro"` 中，通过 `luaC_compute_mro` 计算。

### 2.3 instanceof

```lua
print(c instanceof Animal)  -- true
print(c instanceof Cat)     -- true
```

---

## 3. 接口

### 3.1 定义和实现

```lua
interface Flyable {
  function fly(self)
}

class Bird implements Flyable {
  function fly(self)
    return $"Bird flies"
  end
}
```

### 3.2 接口继承

```lua
interface Bird extends Animal {
  function fly(self)
}
```

---

## 4. 访问控制

基于 `lclass.h:31-35`：

| 级别 | 常量 | 说明 |
|------|------|------|
| `public` | `ACCESS_PUBLIC=0` | 任何地方可访问 |
| `protected` | `ACCESS_PROTECTED=1` | 子类可访问 |
| `private` | `ACCESS_PRIVATE=2` | 仅当前类 |

```lua
class Account {
  private balance = 0
  public function deposit(self, amount)
    self.balance += amount
  end
  protected function audit(self, msg)
    print($"[AUDIT] {msg}")
  end
}
```

---

## 5. 成员修饰符

基于 `lclass.h:38-47`：

| 修饰符 | 标志 | 说明 |
|--------|------|------|
| `static` | `MEMBER_STATIC` | 属于类而非实例 |
| `virtual` | `MEMBER_VIRTUAL` | 可被子类重写 (Token 已定义，解析器未实现) |
| `override` | `MEMBER_OVERRIDE` | 重写父类方法 (Token 已定义，解析器未实现) |
| `abstract` | `MEMBER_ABSTRACT` | 必须在子类实现 |
| `final` | `MEMBER_FINAL` | 不能被子类重写 |
| `const` | `MEMBER_CONST` | 常量成员 |

### 5.1 静态成员

```lua
class MathUtil {
  static PI = 3.14159
  static function square(x) return x * x end
}
print(MathUtil.PI)          -- 3.14159
print(MathUtil.square(5))   -- 25（注意：用 . 调用，不是 :）
```

### 5.2 虚方法与重写

```lua
class Base {
  virtual function greet(self) return "Hello" end
}
class Derived extends Base {
  override function greet(self) return "Hi" end
}
```

---

## 6. Trait/Mixin

```lua
trait Logger {
  function log(self, msg) print($"[LOG] {msg}") end
}

class Service {
  use Logger
  function process(self) self:log("processing") end
}
```

Trait 支持 `requires` 声明需要的方法：

```lua
trait Comparable {
  requires compare
  function isGreater(self, other)
    return self:compare(other) > 0
  end
}
```

---

## 7. 属性访问器 (Token 已定义，解析器未实现)

```lua
-- getter/setter 在 Token 枚举中定义但解析器尚未实现
```

---

## 8. 类元数据键

基于 `lclass.h:50-82`：

| 键 | 存储内容 |
|----|----------|
| `__classname` | 类名 |
| `__parent` | 父类引用 |
| `__parents` | 多父类数组 |
| `__methods` | 公开方法表 |
| `__statics` | 静态成员表 |
| `__privates` | 私有成员表 |
| `__protected` | 保护成员表 |
| `__interfaces` | 已实现接口列表 |
| `__flags` | 类修饰符标志 |
| `__abstracts` | 抽象方法表 |
| `__finals` | 最终方法表 |
| `__getters` / `__setters` | getter/setter 表 |
| `__mro` | C3 线性化方法解析顺序 |
| `__traits` | 已使用的 trait 表 |
| `__trait_requires` | trait 要求的方法 |
| `__typeparams` | 泛型类型参数 |
| `__typeargs` | 绑定的类型参数 |
| `__generic_base` | 泛型基类 |

### 对象元数据键

| 键 | 存储内容 |
|----|----------|
| `__class` | 对象所属类 |
| `__isobject` | 是对象标志 |
| `__obj_privates` | 对象私有数据 |

---

## 9. 通用示例

```lua
abstract class Animal {
  function init(self, name) self.name = name end
  abstract function makeSound(self)
}

interface Pet {
  function play(self)
}

class Dog extends Animal implements Pet {
  function init(self, name, breed)
    super.init(self, name)
    self.breed = breed
  end
  function makeSound(self) return "Woof!" end
  function play(self) return $"{self.name} plays fetch" end
}

local dog = Dog("Buddy", "Golden Retriever")
print(dog:makeSound())        -- Woof!
print(dog instanceof Animal)  -- true
print(dog instanceof Pet)     -- true
```