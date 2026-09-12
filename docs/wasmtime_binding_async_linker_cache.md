# wasmtime 绑定：Linker 深化 / 编译缓存 / Async（④⑤⑥）

对应 roadmap 的 ④⑤⑥ 三项，全部基于 wasmtime v48.0.1 C API 实现并通过测试。

## ④ Linker 深化（wmt_linker.c）

| Lua API | wasmtime C API | 说明 |
| --- | --- | --- |
| `linker:allowShadowing(bool)` | `wasmtime_linker_allow_shadowing` | 允许同名 import 重复定义覆盖（默认 false，重复定义报错） |
| `linker:define(store, module, name, item)` | `wasmtime_linker_define` | 定义任意 extern（function/global/memory/table userdata） |
| `linker:defineInstance(store, name, instance)` | `wasmtime_linker_define_instance` | 将实例的全部导出以 name 为模块名注册进 linker，实现模块间依赖注入 |

`define` 支持的外部项由 userdata 类型自动判别（`WMT_FUNC`/`WMT_GLOBAL`/`WMT_MEMORY`/`WMT_TABLE`），非合法类型抛 Lua 错误。

```lua
local linker = wasmtime.newLinker(engine)
linker:allowShadowing(true)                      -- 允许覆盖
linker:define(store, "env", "add", addFunc)      -- 用导出 func 作 extern
linker:define(store, "env", "mem", store:newMemory(1, 10))
linker:defineInstance(store, "a", instA)         -- 把模块 A 导出注入为模块名 "a"
local instB = linker:instantiate(store, modB)    -- 模块 B import "a" 的项可解析
```

## ⑤ 编译缓存（wmt_engine.c）

| Lua API | wasmtime C API |
| --- | --- |
| `wasmtime.newEngine({ cache = "路径.toml" })` | `wasmtime_config_cache_config_load` |

失败时（配置解析失败 / 目录非法）抛 `newEngine: cache config load failed: ...`。

v48 实测缓存配置文件为 TOML，**顶层必须是 `[cache]` 表，目录必须为绝对路径**（相对路径报 "Cache directory path has to be absolute"）：

```toml
[cache]
directory = "E:/path/to/cache_dir"
```

## ⑥ Async（wmt_async.c）

wasmtime 的 async 本质是协作式栈切换：wasm 在燃料耗尽 / epoch 到期 / async host function 让出时暂停，宿主通过 poll 推进。

**v48 C API 无 `async_support` config setter**（WASMTIME_FEATURE_ASYNC 编译特性下 store 直接支持 async 调用），实测 `func:callAsync` 直接可用。

| Lua API | wasmtime C API | 说明 |
| --- | --- | --- |
| `func:callAsync(...)` | `wasmtime_func_call_async` | 异步调用，返回 `wasmtime.future` |
| `future:poll()` | `wasmtime_call_future_poll` | 推进执行，true=已完成 |
| `future:done()` | — | 是否已完成（不推进） |
| `future:result()` | — | 完成后的结果（或 nil,trap,msg） |
| `future:wait([timeoutMs])` | — | 同步 poll 循环直至完成（可选超时） |
| `future:delete()` | `wasmtime_call_future_delete` | 显式释放（`__gc` 自动） |
| `func:callAsyncP(...)` | 同上 | 返回 `asyncio.promise`，可 `await(...)` |

**约束**（来自 async.h）：
- 同一 store 同时只能有一个存活 future（须先 delete 再发起下一次调用）。
- `args`/`results` 缓冲区须存活到 future 删除（绑定内部已处理）。
- 同步 `wait()` 对"等待外部事件的 async host function"会忙等——此类场景应分步 `future:poll()` 并配合事件循环。

### lpromise 集成（原生 await 语义）

`func:callAsyncP(...)` 内部创建 lpromise Promise（`asyncio.promise`），同步 poll future 至完成，用结果 `promise_resolve` / 错误 `promise_reject`，可直接配合项目原生 `async function` + `await`：

```lua
local asyncio = require("asyncio")
local async function run()
    local v = await(add:callAsyncP(30, 12))   -- v == 42
    return v
end
local p = run()
```

`await(expr)` 编译为 `coroutine.yield(expr)`，Promise 已 settled 时 `laio_promise_settled` 立即恢复协程并传入结果。

## 验证

`test/wasmtest/test_wasmtime_async.lua`（22/22）：
- ⑥ async：callAsync / done / poll / result / wait / trap（除零）/ callAsyncP+await
- ④ allowShadowing（默认拒绝、开启后允许）、define（func/memory、非法项报错）、defineInstance（A 注入 B，B:run(5)=11）
- ⑤ newEngine{cache=...}（含绝对路径 TOML）、错误路径报错

全量回归：test_wasmtime / complex / linker / wat_wasi_trap / component_*（run/wasi/types/str/vr/listret/rust/wasi_env）+ 新 async 测试，全部 exit=0。

---

# 追加：A 调用超时 / B trap 栈帧 / E component 深化

## A. 调用超时保护（func:callWithFuel，wmt_instance.c）

| Lua API | 说明 |
| --- | --- |
| `func:callWithFuel(fuel, ...)` | 调用前 `wasmtime_context_set_fuel(fuel)`，wasm 消耗完燃料即抛 OUT_OF_FUEL trap（nil, trap, msg），防止死循环 wasm 卡死宿主；调用后恢复燃料无上限 |

```lua
local engine = wasmtime.newEngine({ fuel = true })  -- 需开启 consume_fuel
local a, trap, msg = loopfn:callWithFuel(5000, 0)
-- a == nil, trap:code() == 11 (out_of_fuel)
```
实测：未开 fuel 的引擎上 `set_fuel` 静默降级（不限制、不报错）。

## B. trap 调用栈帧（trap:frames，wmt_instance.c）

| Lua API | wasmtime C API |
| --- | --- |
| `trap:frames()` | `wasm_trap_trace` + `wasmtime_frame_func_name/module_name` + `wasm_frame_func_index/offset` |

返回栈帧数组（由内到外），每帧 `{ module=, func=, funcIndex=, funcOffset=, moduleOffset= }`。func/module 名依赖调试信息，缺失时为 nil，offset 恒在。

## E. component 深化（wmt_component.c）

### E1 comp_func:getType() — 类型反射

| Lua API | 说明 |
| --- | --- |
| `comp_func:getType()` | 返回 `params, result`：参数类型字符串数组 + 返回值类型字符串或 nil |

kind → 字符串映射覆盖：bool / s8-u64 / f32 / f64 / char / string / list / record / tuple / variant / enum / option / result / flags / borrow / own。

### E2 comp_func:callAsync(...) — 组件异步调用

| Lua API | wasmtime C API |
| --- | --- |
| `comp_func:callAsync(...)` | `wasmtime_component_func_call_async`（WASMTIME_FEATURE_COMPONENT_MODEL_ASYNC） |

返回 `wasmtime.component_future`，方法与 core future 一致：`poll/done/result/wait([timeout])/delete`。注意 v48 该 API **只有 error_ret 无 trap_ret**（组件错误主要经 result(err) 传递）。args/results 由 future 持有到删除。

```lua
local fu = add:callAsync(10, 5)     -- component_future
local v  = fu:wait()                -- 15（纯计算组件同步完成）
```

## A/B/E 验证

`test/wasmtest/test_wasmtime_abe.lua`（25/25）：
- A：死循环模块 callWithFuel → OUT_OF_FUEL trap、燃料恢复后普通调用正常、无 fuel 引擎不崩溃
- B：除零 trap:frames() 返回数组、funcIndex/funcOffset/moduleOffset 齐全
- E：add:getType() = {u32,u32}→u32、greet = string→string；callAsync（标量 + list）wait/result 正确

全量回归（14 个测试）全部 exit=0。
