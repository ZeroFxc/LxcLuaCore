# LXCLUA-NCore LParser 原生 Async/Await 语法糖

## 📖 概述

LXCLUA-NCore 的 **lparser.c**（Lua 核心解析器）直接支持 **async/await** 语法糖，无需外部编译器（如 llexer_compiler）。这是最底层的语法支持，在代码解析阶段就完成转换。

### 架构层次

```
┌─────────────────────────────────────────────────────┐
│                  用户代码                            │
│                                                    │
│   async function fetchData(id)                     │
│       local resp = await(http_get(id))             │
│       return json.decode(resp.body)                │
│   end                                              │
└──────────────────────┬──────────────────────────────┘
                       │
                       ▼
┌─────────────────────────────────────────────────────┐
│              lparser.c (编译时)                      │
│                                                    │
│  1. 识别 TK_ASYNC / TK_AWAIT 关键字                 │
│  2. 解析函数声明和表达式                             │
│  3. 生成字节码:                                     │
│     - async function → __async_wrap(function)      │
│     - await(expr)    → coroutine.yield(Promise)     │
└──────────────────────┬──────────────────────────────┘
                       │
                       ▼
┌─────────────────────────────────────────────────────┐
│              laio.c (运行时)                         │
│                                                    │
│  __async_wrap(func)                                │
│    → 创建 AsyncFunction 对象                        │
│    → 调用时启动协程                                  │
│    → 返回 Promise                                   │
│                                                    │
│  aio_run_async()                                    │
│    → 创建协程执行用户函数                            │
│    → 处理 LUA_YIELD (await)                        │
│    → 捕获 yield 出来的 Promise                      │
│    → 注册 laio_promise_settled 回调                 │
│    → Promise 完成时恢复协程                          │
└──────────────────────┬──────────────────────────────┘
                       │
                       ▼
┌─────────────────────────────────────────────────────┐
│           lpromise.c (Promise 核心)                 │
│                                                    │
│  Promise 状态管理                                   │
│  then/catch/finally 链式调用                        │
│  all/race/any/allSettled 组合操作                   │
└─────────────────────────────────────────────────────┘
```

## 🎯 支持的语法形式

### 1. 全局异步函数声明

```lua
-- 源代码（lparser 直接解析）
async function fetchData(url)
    local resp = await(asyncio.http_get(url))
    return json.decode(resp.body)
end

-- 使用
local promise = fetchData("http://api.example.com")
local data = promise:await_sync()
```

**生成的字节码等价于：**
```lua
fetchData = __async_wrap(function(url)
    coroutine.yield(asyncio.http_get(url))  -- await 的实际实现
    return json.decode(resp.body)
end)
```

### 2. 局部异步函数声明

```lua
local async function processItem(item)
    await(asyncio.sleep(0.1))
    return item * 2
end

local result = processItem(21):await_sync()  -- → 42
```

### 3. 多参数异步函数

```lua
async function calculate(a, b, c)
    await(asyncio.sleep(0.05))
    return a + b + c
end

local p = calculate(10, 20, 30):await_sync()  -- → 60
```

### 4. Await 表达式

```lua
async function example()
    -- 基本用法
    local data = await(asyncio.http_get(url))

    -- 链式调用
    local processed = await(
        asyncio.http_get(url)
            :done(function(r) return transform(r) end)
    )

    -- 组合操作
    local results = await(asyncio.all(p1, p2, p3))

    return results
end
```

**Await 的实现原理：**

`await(expression)` 被 lparser 编译为：
```lua
coroutine.yield(expression)  -- 单目操作符 OPR_AWAIT
```

当 `expression` 是一个 Promise 时：
1. 协程 yield，将 Promise 传给调用者
2. `aio_run_async` 捕获这个 Promise
3. 注册 `laio_promise_settled` 回调
4. Promise 完成时，回调恢复协程并传入结果值
5. `await` 表达式的返回值就是 Promise 的结果

## 🔧 关键组件详解

### 1. __async_wrap 全局函数

**位置**: [laio.c](../src/utils/laio.c) - `luaopen_asyncio()` 函数末尾

**功能**: 将普通函数包装为 AsyncFunction

**签名**: `__async_wrap(func) -> AsyncFunction`

**实现**:
```c
static int laio_async_wrap_global(lua_State *L) {
    // 1. 检查参数是函数
    luaL_checktype(L, 1, LUA_TFUNCTION);

    // 2. 保存函数引用到注册表（防止 GC）
    lua_pushvalue(L, 1);
    int func_ref = luaL_ref(L, LUA_REGISTRYINDEX);

    // 3. 创建 userdata 存储元数据
    typedef struct { int func_ref; event_loop *loop; } async_wrap_data;
    async_wrap_data *awd = lua_newuserdata(L, sizeof(async_wrap_data));
    awd->func_ref = func_ref;
    awd->loop = aio_get_default_loop(L);

    // 4. 设置 AsyncFunction 元表（带 __call 和 __gc）
    if (luaL_newmetatable(L, "asyncio.async_function") != 0) {
        lua_pushcfunction(L, laio_async_call);  // __call 元方法
        lua_setfield(L, -2, "__call");
        lua_pushcfunction(L, laio_async_gc);    // __gc 元方法
        lua_setfield(L, -2, "__gc");
    }
    lua_setmetatable(L, -2);

    return 1;  // 返回 AsyncFunction 对象
}
```

**注册为全局函数**:
```c
lua_pushcfunction(L, laio_async_wrap_global);
lua_setglobal(L, "__async_wrap");
```

### 2. OP_ASYNCWRAP 操作码

**位置**: [lopcodes.h](../src/core/lopcodes.h)

**定义**:
```c
OP_ASYNCWRAP,/*	A B	R[A] := async_wrap(R[B])	*/
```

**VM 实现** ([lvm.c](../src/vm/lvm.c)):
```c
vmcase(OP_ASYNCWRAP) {
    // 1. 确保栈空间足够
    while (L->top.p < base + cl->p->maxstacksize)
         setnilvalue(s2v(L->top.p++));
    luaD_checkstack(L, 1);
    updatebase(ci);

    // 2. 获取要包装的函数寄存器
    int b = GETARG_B(i);

    // 3. 调用全局 __async_wrap 函数
    lua_getglobal(L, "__async_wrap");
    if (ttisfunction(s2v(L->top.p - 1))) {
        updatebase(ci);

        // 4. 将函数参数压栈
        TValue *rb = s2v(base + b);
        setobj2s(L, L->top.p, rb);
        L->top.p++;

        // 5. 调用 __async_wrap(rb)
        lua_call(L, 1, 1);  // 1 参数，1 返回值
        updatebase(ci);

        // 6. 将结果存入目标寄存器
        StkId ra = RA(i);
        setobj2s(L, ra, s2v(L->top.p - 1));
        L->top.p--;
    }
    vmnext();
}
```

### 3. Await 表达式处理

**位置**: [lparser.c](../src/compiler/lparser.c) - `subexpr()` 函数

**识别**:
```c
// TK_AWAIT 被识别为单目操作符 OPR_AWAIT
case TK_AWAIT: return OPR_AWAIT;  // 第 4785 行
```

**代码生成**:
```c
if (uop == OPR_AWAIT) {
    FuncState *fs = ls->fs;
    expdesc f;

    // 1. 获取 coroutine 函数
    singlevaraux(fs, luaS_newliteral(ls->L, "coroutine"), &f, 1);
    if (f.k == VVOID) {
        // 如果 coroutine 不是局部变量，从全局环境获取
        expdesc key;
        singlevaraux(fs, ls->envn, &f, 1);
        codestring(&key, luaS_newliteral(ls->L, "coroutine"));
        luaK_indexed(fs, &f, &key);
    }

    // 2. 获取 coroutine.yield 方法
    luaK_exp2anyreg(fs, &f);
    expdesc key;
    codestring(&key, luaS_newliteral(ls->L, "yield"));
    luaK_indexed(fs, &f, &key);

    // 3. 生成函数调用字节码：coroutine.yield(await_expr)
    luaK_exp2nextreg(fs, &f);
    int func_reg = f.u.info;          // coroutine.yield 函数寄存器

    luaK_exp2nextreg(fs, v);
    int arg_reg = v->u.info;          // await 参数寄存器

    // 4. OP_CALL: 调用 coroutine.yield(expr)
    init_exp(v, VCALL, luaK_codeABC(fs, OP_CALL, func_reg, 2, 2));
    fs->freereg = func_reg + 1;
}
```

### 4. Yield 处理与 Promise 协作

**位置**: [laio.c](../src/utils/laio.c) - `aio_run_async()` 函数

**关键逻辑**:
```c
int status = lua_resume(co_L, L, nargs > 0 ? nargs - 1 : 0, &nres);

if (status == LUA_OK || status == LUA_YIELD) {
    if (status == LUA_OK) {
        // 函数正常完成 → resolve result_p
        ...
    } else {
        /* LUA_YIELD: 协程挂起（正在 await Promise） */
        int yield_results = lua_gettop(co_L);

        if (yield_results > 0) {
            // 检查 yield 出来的值是否是 Promise
            promise **pp = (promise **)luaL_testudata(co_L, -1, PROMISE_METATABLE);

            if (pp && *pp) {
                // 是 Promise！注册回调等待它完成
                promise *yielded_p = *pp;
                ctx->waiting_p = promise_retain(yielded_p);

                // 注册 settle 回调
                yielded_p->aco_ctx = ctx;
                yielded_p->on_settled = laio_promise_settled;

                lua_pop(co_L, 1);  // 清理协程栈
            } else {
                // 不是 Promise：当作普通值立即恢复协程
                lua_resume(co_L, L, 0, &nres);
                ...
            }
        }
    }
}
```

### 5. Promise Settled 回调

**位置**: [laio.c](../src/utils/laio.c) - `laio_promise_settled()` 函数

**功能**: 当 await 的 Promise 完成时，恢复协程并传入结果

```c
static void laio_promise_settled(promise *p) {
    coroutine_context *ctx = (coroutine_context *)p->aco_ctx;

    // 1. 获取 Promise 结果
    int nargs = 0;
    int state = promise_get_state(p);

    if (state == PROMISE_FULFILLED) {
        promise_get_result(p, ctx->co);  // 压入结果到协程栈
        nargs = 1;
    } else {  // REJECTED
        lua_pushnil(ctx->co);  // 失败时传 nil
        nargs = 1;
    }

    // 2. 恢复协程执行（传入 Promise 结果作为 yield 的返回值）
    int nres;
    int status = lua_resume(ctx->co, ctx->L_main, nargs, &nres);

    if (status == LUA_OK) {
        // 协程完成 → resolve 外层 result_p
        ...
    } else if (status == LUA_YIELD) {
        // 再次 yield（嵌套 await）→ 继续等待
        // 重复上述流程...
    } else {
        // 出错 → reject 外层 result_p
        ...
    }
}
```

## 📊 数据流图

### 完整的 Async/Await 执行流程

```
用户代码                    lparser.c              laio.c (运行时)
   │                           │                      │
   ▼                           ▼                      ▼
async function fn(x)          识别 TK_ASYNC          │
    await(sleep(x))     ──►  解析函数体             │
    return x*2            │  生成字节码:             │
end                        │  ┌──────────────────┐   │
                           │  │ R[A]=__async_wrap│   │
                           │  │   (function body)│   │
                           │  └──────────────────┘   │
                           │                         │
调用 fn(21)                │                         │
   │                       │                         ▼
   ▼                       │               __async_wrap 接收函数
fn(21) 被调用              │               创建 AsyncFunction 对象
   │                       │               设置 __call 元方法
   ▼                       │                         │
__call 元方法触发           │                         ▼
   │                       │               aio_run_async 启动
   ▼                       │               创建协程 ctx
执行函数体                  │               设置 __aco_ctx__
   │                       │               lua_resume(协程)
   ▼                       │                         │
遇到 await(sleep(21))      │                         ▼
   │                       │               lua_resume 返回 LUA_YIELD
   ▼                       │               协程栈顶: Promise 对象
coroutine.yield(Promise)   │                         │
   │                       │                         ▼
   ├────────────────────────┤◄────────────────────────┘
   │                       │         捕获 Promise
   ▼                       │         ctx->waiting_p = Promise
Promise 开始执行 sleep     │         Promise->on_settled = callback
   │                       │                         │
   ▼                       │                         │
sleep 完成                 │                         ▼
Promise resolve(true)      │               laio_promise_settled 触发
   │                       │               获取结果: true
   ▼                       │               lua_resume(协程, true)
协程恢复执行               │               await 返回 true
   │                       │                         │
   ▼                       │                         │
return 21 * 2              │                         ▼
   │                       │               lua_resume 返回 LUA_OK
   ▼                       │               获取返回值: 42
函数完成                   │               promise_resolve(result_p, 42)
   │                       │                         │
   ▼                       │                         ▼
返回 Promise              ◄─────────────────────────┘
result_p = fulfilled(42)
```

## 🆚 与其他方式的对比

### 方式 1: LParser 原生语法糖（本文档）

**优点**:
- ✅ **零预编译步骤**：直接写 `async function` / `await`
- ✅ **性能最优**：编译时就生成优化字节码
- ✅ **IDE 友好**：语法高亮、错误检查开箱即用
- ✅ **官方支持**：核心解析器原生集成

**缺点**:
- ❌ 需要修改 Lua 核心解析器（已由 LXCLUA-NCore 完成）

### 方式 2: LLexer Compiler 外部编译

**优点**:
- ✅ 不修改核心解析器
- ✅ 可选启用/禁用
- ✅ 支持代码混淆

**缺点**:
- ❌ 需要预编译步骤
- ❌ IDE 无法提供实时语法检查
- ❌ 调试困难（看到的是脱糖后的代码）

### 方式 3: 运行时 API（asyncio.wrap / asyncio.wait）

**优点**:
- ✅ 标准 Lua 语法，任何编辑器都支持
- ✅ 无需特殊工具链
- ✅ 兼容性最好

**缺点**:
- ❌ 代码较冗长
- ❌ 不够"现代"

## 🧪 测试验证

### 测试文件

- `test_native_async_await.lua` - 原生语法糖测试套件
- `test_parser_features.lua` - Parser 功能测试

### 运行测试

```bash
cd e:\Soft\Proje\LXCLUA-NCore\lua
lua tests/test_native_async_await.lua
```

### 测试覆盖范围

1. **基础功能**
   - ✅ `__async_wrap` 注册验证
   - ✅ 全局/局部 `async function` 声明
   - ✅ 多参数支持

2. **Await 表达式**
   - ✅ `await(Promise)` 基本用法
   - ✅ 多个 await 顺序执行
   - ✅ await 返回值传递

3. **完整流程**
   - ✅ 异步数据获取模拟
   - ✅ 嵌套 async 函数
   - ✅ 错误处理和传播

4. **混合使用**
   - ✅ 与运行时 API (`asyncio.wait`) 混合
   - ✅ Promise 链式调用

5. **边界情况**
   - ✅ 空 async 函数
   - ✅ 无 await 的 async 函数
   - ✅ 性能基准测试

## ⚙️ 配置选项

### 默认行为

加载 `asyncio` 库后自动注册 `__async_wrap`：

```lua
require("asyncio")  -- 自动注册 _G.__async_wrap
```

之后可以直接使用原生语法。

### 手动注册（如果需要）

如果不想加载整个 asyncio 库，可以手动注册：

```lua
-- 最小化注册
function _G.__async_wrap(func)
    -- 简单实现：直接返回原函数（不支持真正的 async/await）
    return function(...)
        return func(...)
    end
end
```

> ⚠️ 注意：简化版本不支持 `await`，仅用于语法兼容性测试。

## 🔍 调试技巧

### 查看 __async_wrap 是否生效

```lua
print(type(__async_wrap))  --> "function"
print(_G.__async_wrap ~= nil)  --> true
```

### 检查 async 函数类型

```lua
async function test()
    return 42
end

print(type(test))  --> "function" (但带有 __call 元方法的 userdata)
print(tostring(test))  --> "AsyncFunction" (如果有 __tostring)
```

### 跟踪 Promise 状态

```lua
async function debugExample()
    local p = asyncio.sleep(1)
    print("Before await:", p.state)  --> "pending"
    local r = await(p)
    print("After await:", r)  --> true (sleep 的返回值)
    return r
end

local resultP = debugExample()
print("Result Promise:", resultP.state)  --> 初始可能是 "pending"
local final = resultP:await_sync()
print("Final:", final)  --> true
```

### 错误定位

如果在 async 函数中出错：

```lua
async function buggy()
    await(asyncio.sleep(0.01))
    error("something went wrong!")
end

local p = buggy()
local result = p:await_sync()
print(type(result), result)  --> "string", "something went wrong!"
```

## 🚀 最佳实践

### 1. 总是在 async 函数中使用 await

```lua
✅ 正确:
async function good()
    local data = await(fetchData())
    return process(data)
end

❌ 错误:
async function bad()
    local p = fetchData()  -- 得到 Promise 但没有 await
    return p  -- 返回 Promise 而不是数据
end
```

### 2. 使用 :await_sync() 在顶层获取结果

```lua
async function inner()
    await(asyncio.sleep(0.1))
    return "done"
end

-- 在非 async 上下文中
local p = inner()
local result = p:await_sync(1000)  -- 同步等待
print(result)  --> "done"
```

### 3. 错误处理使用 pcall

```lua
async function safeOperation()
    local ok, result = pcall(function()
        local data = await(riskyCall())
        return transform(data)
    end)

    if not ok then
        print("Error:", result)
        return {error=true, message=result}
    end

    return {error=false, data=result}
end
```

### 4. 并发使用 all()

```lua
async function fetchAll(urls)
    local promises = {}
    for i, url in ipairs(urls) do
        promises[i] = http_get(url)
    end
    return await(asyncio.all(table.unpack(promises)))
end
```

## 📚 相关文档

- [ASYNC_AWAIT_SUGAR.md](ASYNC_AWAIT_SUGAR.md) - 完整 API 文档
- [lpromise.h](../src/utils/lpromise.h) - Promise C API
- [laio.h](../src/utils/laio.h) - AsyncIO C API
- [lopcodes.h](../src/core/lopcodes.h) - 操作码定义

---

**版本**: LXCLUA-NCore v1.0+
**最后更新**: 2026-04-06
**维护者**: DiferLine
