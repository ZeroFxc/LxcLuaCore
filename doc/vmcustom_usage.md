# vmcustom — 自定义 Opcode 扩展系统 使用教程

## 概述

`vmcustom` 是一个 Lua C 扩展库，为 Lua VM 提供**可动态编程的指令集扩展能力**。包含两套系统：

| 系统 | 位置 | 用途 |
|------|------|------|
| **主 VM OP_CUSTOM** | 直接挂载到 `lvm.c` 执行循环 | 在 Lua 字节码流中插入自定义 C/Lua 处理逻辑 |
| **微型 VM（vm语言）** | 独立的寄存器机解释器 | 运行用户自定义 bytecode，可脱离主 VM 执行 |

---

## API 速查表

| 函数 | 说明 | 签名 |
|------|------|------|
| `vm.setop(op, handler)` | 注册 OP_CUSTOM 处理器 | `(number, function) -> ()` |
| `vm.getop(op)` | 获取已注册的处理器 | `(number) -> function\|nil` |
| `vm.delop(op)` | 删除处理器 | `(number) -> ()` |
| `vm.listops()` | 列出所有已注册 opcode 号 | `() -> table` |
| `vm.opcount()` | 返回已注册数量 | `() -> number` |
| `vm.makeinst(op)` | 构造一条 OP_CUSTOM 字节码指令 | `(number) -> number` |
| `vm.getinstop(inst)` | 从指令中提取用户 opcode 号 | `(number) -> number\|nil` |
| `vm.execmini(bc, nregs)` | 执行 MiniVM bytecode | `(table, number) -> ()` |
| `vm.setuserminiop(op, handler)` | 注册 MiniVM 自定义指令 | `(number, function) -> ()` |
| `vm.compile(code)` | 编译 vm语言 源码 | `(string) -> table` |

### 常量

| 常量 | 值 | 说明 |
|------|----|------|
| `MAX_CUSTOM_OPS` | 256 | OP_CUSTOM 最大用户 opcode 数量 (0-255) |
| `MINIVM_USER_BASE` | 16 | MiniVM 用户自定义指令起始值 |
| `MINIVM_MAX_OPS` | 128 | MiniVM 最大指令数 |
| `MINI_NOP` ~ `MINI_HALT` | 0-15 | MiniVM 内置指令 opcode |

---

## 第一部分：主 VM OP_CUSTOM 处理器

### 1.1 基本概念

OP_CUSTOM 是插入到 Lua 主 VM 字节码中的一条**钩子指令**。当 VM 执行到该指令时，会调用你通过 `vm.setop()` 注册的处理器函数。

```
Lua 字节码流:
  MOVE R1, R2
  ADD R3, R1, R4
  OP_CUSTOM(Ax=42)   ← 触发你的处理器
  CALL R3, 1, 1
```

### 1.2 注册自定义 opcode 处理器

```lua
local vm = require("vmcustom")

-- 定义处理器（Lua 函数）
local function my_handler(L)
    local nargs = select("#", ...)
    print("my_handler called with " .. nargs .. " args")
    return 0  -- 返回 0 表示无返回值
end

-- 注册为 opcode 0
vm.setop(0, my_handler)
```

### 1.3 处理器函数规范

处理器签名：`function(Lua_State) -> number`

- **参数**：由 VM 传递给处理器（当前 RA 寄存器开始的值）
- **返回值**：`n >= 0` 表示有 n 个返回值写入 RA 开始的位置；`n < 0` 表示 yield

### 1.4 管理处理器

```lua
-- 查询
local handler = vm.getop(0)
print(type(handler))  --> "function"

-- 列出所有
local ops = vm.listops()
for _, op in ipairs(ops) do
    print("registered opcode:", op)
end

-- 计数
print(vm.opcount())  --> 1

-- 删除
vm.delop(0)
print(vm.getop(0))  --> nil
```

### 1.5 构造和解析指令

```lua
-- 构造一条 OP_CUSTOM 指令的二进制值（64位整数）
local inst = vm.makeinst(42)

-- 从指令中提取用户 opcode 号
local op = vm.getinstop(inst)
print(op)  --> 42

-- 非 OP_CUSTOM 指令返回 nil
print(vm.getinstop(0))  --> nil
```

### 1.6 典型应用

```lua
-- [[ 热补丁：运行时替换行为 ]]
-- 初始实现（C 函数，高性能）
vm.setop(0, my_c_function)

-- 运行时替换为新实现
vm.delop(0)
vm.setop(0, better_implementation)
-- 所有后续 OP_CUSTOM(0) 走新逻辑，无需重启
```

---

## 第二部分：微型 VM 解释器

### 2.1 内置指令集

MiniVM 是一个带 16 个内部寄存器的寄存器机，内置 16 条指令：

| 指令 | opcode | 语义 | 示例 |
|------|--------|------|------|
| `NOP` | 0 | 空操作 | `{op=0}` |
| `MOV` | 1 | R[A] := R[B] | `{op=1, a=2, b=1}` |
| `LOADK` | 2 | R[A] := K[B] | `{op=2, a=1, b=42}` |
| `ADD` | 3 | R[A] := R[B] + R[C] | `{op=3, a=3, b=1, c=2}` |
| `SUB` | 4 | R[A] := R[B] - R[C] | `{op=4, a=3, b=1, c=2}` |
| `MUL` | 5 | R[A] := R[B] * R[C] | `{op=5, a=3, b=1, c=2}` |
| `DIV` | 6 | R[A] := R[B] / R[C] | `{op=6, a=3, b=1, c=2}` |
| `EQ` | 7 | R[A] := R[B] == R[C] | `{op=7, a=10, b=1, c=2}` |
| `LT` | 8 | R[A] := R[B] < R[C] | `{op=8, a=10, b=1, c=2}` |
| `JMP` | 9 | pc += k | `{op=9, k=3}` |
| `JT` | 10 | if R[A] then pc += k | `{op=10, a=1, k=2}` |
| `JF` | 11 | if not R[A] then pc += k | `{op=11, a=1, k=2}` |
| `CALL` | 12 | call R[A] with arguments | `{op=12, a=1, b=3}` |
| `RET` | 13 | 停止执行 | `{op=13}` |
| `PRINT` | 14 | 打印 R[A] | `{op=14, a=1}` |
| `HALT` | 15 | 停止执行 | `{op=15}` |

### 2.2 bytecode 格式

每条指令是一个 table：

```lua
{ op = opcode, a = 0, b = 0, c = 0, k = 0 }
```

- `op` — 操作码（0-127）
- `a` — 参数 A（通常是目标寄存器）
- `b` — 参数 B
- `c` — 参数 C
- `k` — 标志/跳转偏移

### 2.3 执行示例

```lua
local vm = require("vmcustom")

-- 计算 R3 := 10 + 3
local bytecode = {
    { op = vm.MINI_LOADK, a = 1, b = 10, c = 0, k = 0 },   -- R1 = 10
    { op = vm.MINI_LOADK, a = 2, b = 3,  c = 0, k = 0 },   -- R2 = 3
    { op = vm.MINI_ADD,   a = 3, b = 1,  c = 2, k = 0 },   -- R3 = R1 + R2
    { op = vm.MINI_HALT,  a = 0, b = 0,  c = 0, k = 0 },   -- 停止
}

vm.execmini(bytecode, 8)  -- 第二个参数是寄存器数量
```

### 2.4 条件跳转

```lua
-- 循环 10 次
local loop = {
    { op = vm.MINI_LOADK, a = 1, b = 0,  c = 0, k = 0 },  -- R1 = 0 (counter)
    { op = vm.MINI_LOADK, a = 2, b = 1,  c = 0, k = 0 },  -- R2 = 1 (increment)
    { op = vm.MINI_LOADK, a = 3, b = 10, c = 0, k = 0 },  -- R3 = 10 (limit)
    { op = vm.MINI_ADD,   a = 1, b = 1,   c = 2, k = 0 }, -- R1 += 1
    { op = vm.MINI_LT,    a = 4, b = 1,   c = 3, k = 0 }, -- R4 = R1 < R3
    { op = vm.MINI_JT,    a = 4, b = 0,   c = 0, k = -2 },-- if R4: goto ADD
    { op = vm.MINI_HALT,  a = 0, b = 0,   c = 0, k = 0 },
}

vm.execmini(loop, 8)
```

### 2.5 计算斐波那契

```lua
-- 计算 Fib(10)，结果存入 R2
local fib = {
    { op = vm.MINI_LOADK, a = 1, b = 0,  c = 0, k = 0  },  -- R1 = 0 (a)
    { op = vm.MINI_LOADK, a = 2, b = 1,  c = 0, k = 0  },  -- R2 = 1 (b)
    { op = vm.MINI_LOADK, a = 3, b = 10, c = 0, k = 0  },  -- R3 = 10 (n)
    { op = vm.MINI_LOADK, a = 4, b = 0,  c = 0, k = 0  },  -- R4 = 0 (i)
    { op = vm.MINI_ADD,   a = 5, b = 1, c = 2, k = 0   },  -- R5 = a + b
    { op = vm.MINI_MOV,   a = 1, b = 2, c = 0, k = 0   },  -- a = b
    { op = vm.MINI_MOV,   a = 2, b = 5, c = 0, k = 0   },  -- b = R5
    { op = vm.MINI_LOADK, a = 6, b = 1, c = 0, k = 0   },  -- R6 = 1
    { op = vm.MINI_ADD,   a = 4, b = 4, c = 6, k = 0   },  -- i += 1
    { op = vm.MINI_LT,    a = 7, b = 4, c = 3, k = 0   },  -- R7 = i < n
    { op = vm.MINI_JT,    a = 7, b = 0, c = 0, k = -6  },  -- if R7: goto ADD
    { op = vm.MINI_HALT,  a = 0, b = 0, c = 0, k = 0   },
}

vm.execmini(fib, 16)
```

---

## 第三部分：MiniVM 自定义指令

### 3.1 注册用户指令

用户 opecode 范围：**16 ~ 127**。

处理器签名：`function(vm_ref, a, b, c, k) -> jump`

```lua
-- 注册 opcode 16：累加计数器
local counter = 0
vm.setuserminiop(16, function(vm, a, b, c, k)
    counter = counter + 1
    return 1  -- 返回 1 表示执行下一条指令
end)

local bc = {
    { op = 16, a = 0, b = 0, c = 0, k = 0 },
    { op = 16, a = 0, b = 0, c = 0, k = 0 },
    { op = vm.MINI_HALT },
}
vm.execmini(bc, 4)
print(counter)  --> 2
```

### 3.2 控制跳转

返回值控制 PC 偏移：
- `1` — 执行下一条指令（正常）
- `n` — 跳转 n 条指令
- `-1` — 停止

```lua
-- 自定义条件跳转
vm.setuserminiop(17, function(vm, a, b, c, k)
    if some_condition(a, b) then
        return k  -- 条件成立时跳转 k 条
    end
    return 1  -- 否则继续
end)
```

### 3.3 范围检查

```lua
-- 有效范围：16 ~ 127
-- 无效：
vm.setuserminiop(0, func)    -- 报错（< 16）
vm.setuserminiop(200, func)  -- 报错（>= 128）
```

---

## 第四部分：vm语言编译器

### 4.1 语法

每行一条指令，格式：`opcode a b c`

```lua
local code = [[
2 1 10 0    # LOADK R1, 10
2 2 3  0    # LOADK R2, 3
3 3 1  2    # ADD R3, R1, R2
]]

local bytecode = vm.compile(code)
-- bytecode = {
--   { op=2, a=1, b=10, c=0, k=0 },
--   { op=2, a=2, b=3,  c=0, k=0 },
--   { op=3, a=3, b=1,  c=2, k=0 },
-- }
```

### 4.2 注释

`#` 或 `;` 开头的行被视为注释：

```lua
local code = [[
# 初始化计数器
2 1 0  0     # R1 = 0
; 循环体
3 1 1  1     # R1 += 1
]]

vm.compile(code)
```

---

## 第五部分：两套系统联动

### 5.1 MiniVM 发射 OP_CUSTOM 指令

```lua
-- 在 MiniVM 中注册一条指令，运行时构造 OP_CUSTOM 指令
vm.setuserminiop(16, function(vm, a, b, c, k)
    local inst = vm.makeinst(b)  -- 用 b 字段作为用户 opcode 号
    -- inst 可以直接写入 Lua 函数原型中...
    return 1
end)
```

### 5.2 完整工作流

```lua
local vm = require("vmcustom")

-- 步骤1：注册主 VM 处理器
vm.setop(42, function(L)
    print("opcode 42 被触发！")
    return 0
end)

-- 步骤2：构造指令
local inst = vm.makeinst(42)

-- 步骤3：验证
print(vm.getinstop(inst))  --> 42

-- 步骤4：也可以从 MiniVM 中发射
vm.setuserminiop(16, function(mv, a, b, c, k)
    local inst = vm.makeinst(b)
    return 1
end)

local bc = {
    { op = vm.MINI_LOADK, a = 1, b = 42, c = 0, k = 0 },
    { op = 16,  a = 0, b = 42, c = 0, k = 0 },
    { op = vm.MINI_HALT },
}
vm.execmini(bc, 8)
```

---

## 第六部分：高级用法

### 6.1 动态热补丁

```lua
-- 运行时无缝替换指令处理逻辑
local function old_handler(L)
    return 0
end

local function new_handler(L)
    return 0
end

vm.setop(0, old_handler)
-- ... system runs ...
vm.delop(0)
vm.setop(0, new_handler)  -- 热更新完成
```

### 6.2 运行时查询

```lua
-- 检查哪些 opcode 已被占用
local registered = vm.listops()
for _, op in ipairs(registered) do
    local handler = vm.getop(op)
    print(string.format("opcode %d: %s", op, type(handler)))
end
```

### 6.3 错误处理

```lua
-- 范围保护
local ok, err = pcall(function()
    vm.setop(999, function() end)
end)
-- false, "custom opcode 999 out of range (0 ~ 255)"

-- MiniVM 错误隔离
local ok, err = pcall(function()
    vm.setuserminiop(0, function() end)
end)
-- false, "user opcode 0 out of range (16 ~ 127)"
```

---

## 架构总览

```
┌─────────────────────────────────────────────┐
│                 Lua 用户代码                  │
│  vm.setop() / vm.execmini() / vm.compile()   │
└──────────┬──────────────────┬────────────────┘
           │                  │
     ┌─────▼──────┐   ┌───────▼────────┐
     │ lvm.c      │   │  lvmustom.c     │
     │ OP_CUSTOM  │   │  MiniVM 解释器  │
     │ 调度钩子   │   │  (寄存器机)     │
     └─────┬──────┘   └───────┬────────┘
           │                  │
     ┌─────▼──────────────────▼────────┐
     │   custom_op_handlers[] 数组      │
     │   (lua_CFunction 指针, 256项)    │
     └─────────────────────────────────┘
```