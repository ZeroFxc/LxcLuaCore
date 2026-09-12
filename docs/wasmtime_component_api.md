# wasmtime Component Model（组件）Lua 绑定说明

绑定模块：`src/wasm/wmt_component.c`（约 30KB，已编入 `make mingw` 与 Android.mk）。
入口注册于 `lwasmtime.c`：`newComponent`、`newComponentLinker` + 4 个元表。

---

## 一、Lua API 一览

```lua
local w = require("wasmtime")

-- 1) 从组件二进制创建 component
local comp = w.newComponent(engine, bytes)   -- bytes: 组件 .wasm 的字节串

-- 2) 创建组件链接器
local linker = w.newComponentLinker(engine)

-- 3) 定义 WASI preview2 接口（可选，组件 import WASI 时必须）
linker:addWasiP2()

-- 4) 实例化
local inst, err = linker:instantiate(store, comp)

-- 5) 取导出函数（支持多级路径：先取嵌套 instance，再取其中函数）
local f = inst:getExport("add")                  -- 顶层导出
local run = inst:getExport("wasi:cli/run@0.2.0", "run")  -- 嵌套 instance 导出

-- 6) 调用（参数/返回值自动按组件类型转换）
local r = f:call(20, 22)          -- -> 42
```

### WASI preview2 配置（配合 `wmt_wasi.c` 的 newWasi）

```lua
local wasi = w.newWasi{ inheritStdout = true, argv = {...}, env = {...},
                        preopenDirs = { {path=".", guest="/"} } }
store:setWasi(wasi)
linker:addWasiP2()
```

### 组件实例导出结构

`inst:getExport(...)` 按导出层级查找：
- 顶层导出：`inst:getExport("add")`
- 嵌套 instance 导出（preview2 命令组件标准布局）：
  `inst:getExport("wasi:cli/run@0.2.0", "run")`
- 内部实现：逐级用上一级 `export_index` 作为 instance 定位参数下钻。

---

## 二、类型转换支持（Lua ↔ component_val 双向）

| 组件类型 | Lua 输入 | Lua 输出 | 验证状态 |
| --- | --- | --- | --- |
| bool | `true` / `false` | boolean | ✅ 双向 |
| s8/s16/s32/s64, u8/u16/u32/u64 | number | number | ✅ 双向 |
| f32 / f64 | number | number | ✅ 双向 |
| char | 1 字符 string | string | ✅ |
| string | string（UTF-8） | string | ✅ 双向（含中文） |
| list | 数组 table `{1,2,3}` | 数组 table | ✅ 双向（含空 list） |
| record | `{field=value}` | `{field=value}` | ✅ 双向 |
| tuple | 数组 table | 数组 table | ✅ 参数 |
| enum | 字符串 case 名 | 字符串 | ✅ 双向 |
| flags | `{flag=true}` | `{flag=true}` | ✅ 双向 |
| option | `value` / `nil` | `value` / `nil` | ✅ 双向 |
| variant | `{name, payload}`、`{name=payload}`、`name`(无载荷) | 单键 map（无载荷为 `{name=true}`） | ✅ 双向 |
| result | `{ok=v}` / `{err=v}` | `{ok=v}` / `{err=v}` | ✅ 双向 |
| resource | 不支持（报错） | — | 明确未绑定 |
| map / future / stream | 不支持（报错） | — | 明确未绑定 |

### 传参约定细节

- **variant 有载荷**：`{caseName, payload}`（数组形式）或 `{caseName=payload}`（单键 map）。
- **variant 无载荷**：直接传 case 名字符串，如 `"none"`。
- **result**：`{ok=值}` 或 `{err=值}`；返回同样结构。
- **option**：`nil` 表示 `none`。

---

## 三、验证状态（截至 2026-09-04）

### 手写 WAT 组件（wasm-tools 1.257.0 编译，WASI 0.2.0 接口）

| 测试 | 覆盖 | 结果 |
| --- | --- | --- |
| `test_component_run.lua` | 最小组件加载/实例化/调用 | add(20,22)=42 ✅ |
| `test_component_wasi.lua` | preview2 WASI stdout（手写 hello 组件） | 输出 "Hello, world!" ✅ |
| `test_component_wasi_env.lua` | preview1 fd_write 实测 + preview2 环境变量读取 | 7/7 ✅ |
| `test_component_types.lua` | 值类型 17 项（bool/s32/u32/f32/f64/char/option/enum/flags/record/tuple） | 17/17 ✅ |
| `test_component_str.lua` | string/list 参数方向 6 项（含 UTF-8 中文） | 6/6 ✅ |
| `test_component_vr.lua` | variant/result 双向转换 | ✅ |
| `test_component_listret.lua` | list 返回方向 | ✅ |

### 真实 Rust 组件（cargo-component 0.21.1 + wit-bindgen 0.41，`wasm32-unknown-unknown`）

| 测试 | 覆盖 | 结果 |
| --- | --- | --- |
| `test_component_rust.lua` | 21 项：标量/string/list/record/result/variant/enum/flags/option，全部真实 wit-bindgen ABI | 21/21 ✅ |

### 关键 ABI 结论（手写 core 模块 canon ABI）

- **list/string 返回方向**：core 函数签名 `(param i32 i32) (result i32)`，返回值是**指向 `[ptr, len]` 8 字节结构的指针**（"返回指针"模式）；参数方向才是平铺 `(ptr, len)` 两参数。此模式在 wasm-tools 校验与 wasmtime 运行时均验证通过。
- 复杂类型（record/enum/flags）作为导出函数签名时，必须先在组件中 `(type $t (record ...))` + `(export $t2 "name" (type $t))` 导出类型，且函数签名引用**导出后的类型索引**。
- canon lift 对 string/list/复杂类型需要 `(memory)` 与 `(realloc)`。
- 组件导出名必须 **kebab-case**。

---

## 四、已知边界：wasmtime v48 与 WASI preview2 版本

**wasmtime v48 内置的 WASI preview2 实现为 0.2.0 早期版本（2024 年发布线）。**

- ✅ 手写 WAT 组件按 `wasi:*@0.2.0` 接口生成 → v48 完美运行。
- ✅ **preview2 宿主资源访问实测**（`test_component_wasi_env.lua`）：标准输出（"Hello, world!"）、**环境变量读取（newWasi{env={A=1,B=2,C=3}} → 组件读到 3 个）** 均可用；`addWasiP2()` 一并提供全部 preview2 0.2.0 接口（含 filesystem/sockets/clocks）。
- ✅ `wasm32-unknown-unknown` 生成的纯计算组件（不 import 任何 WASI）→ v48 完美运行。
- ❌ **2025 年后的 Rust 工具链**（rustc `wasm32-wasip1`/`wasm32-wasip2` target）生成的组件 import `wasi:*@0.2.3` / `@0.2.9`（环境/io/poll 等），v48 报 `matching implementation was not found in the linker`。这是 wit-bindgen 已知问题（bytecodealliance/wit-bindgen#1407）：**guest 工具链 wasi 接口版本高于 host 的 wasmtime 实现时 linker 失败**，不是绑定代码问题。
- ❌ **preview1 模块的标准流/文件写（fd_write 到 stdout/stderr/文件、path_open）在 v48 下返回 `ENOSYS(48)`**（已实测确认，无论 inherit 还是文件重定向）。这是 v48 preview2 架构对 preview1 模块的运行时限制；`random_get` 等纯计算接口正常。**该问题不会因 component API 绑定而消失**——preview1 模块本身无法在 v48 上做 IO，只能：
  - 将 preview1 模块改造成 preview2 组件（本项目已验证完整路径），或
  - 升级宿主 wasmtime（超出本项目绑定范畴）。

**建议**：
- 用 preview2 完整 WASI 能力（env/fs/stdio）时，组件按 **WASI 0.2.0** 接口生成（手写 WAT + wasm-tools 方式，或 2024 年工具链），即可在 v48 上获得完整宿主资源访问。
- 若必须跑 2025 年 Rust 工具链组件，需要升级宿主 wasmtime（非本项目绑定范畴）。

---

## 五、测试素材与复现

### 目录
- 测试脚本：`test/wasmtest/test_component_*.lua`
- 组件二进制/源：`test/wasmtest/_*.wasm`、`_*.wat`
- 生成器：`_mktypes.py`（值类型）、`_mkstr.py`（string/list）、`_mkvr.py`（variant/result）、`_lt.py`（list/string 返回）、`_mkenv.py`（preview2 env 组件）
- 真实组件工程：`test/wasmtest/rust-demo/`（Cargo.toml + wit/world.wit + src/lib.rs）

### 组件编译工具
- **wasm-tools 1.257.0**（手写 WAT → 组件）：`wasm-tools parse x.wat -o x.wasm`
  - 下载：`https://github.com/bytecodealliance/wasm-tools/releases/download/v1.257.0/wasm-tools-1.257.0-x86_64-windows.zip`
  - 注意：PowerShell `Expand-Archive` 解压失败时用 `python -c "import zipfile; zipfile.ZipFile('x.zip').extractall('d')"`
- **cargo-component 0.21.1**（Rust 组件）：`cargo component build --release --target wasm32-unknown-unknown`
  - 依赖：`rustup target add wasm32-wasip1 wasm32-wasip2 wasm32-unknown-unknown`
  - 注意：要能在 wasmtime v48 上运行，Rust 组件必须用 **wasm32-unknown-unknown**（无 WASI import）；wasip1/wasip2 target 生成的组件受 WASI 版本鸿沟限制（见第四节）。

### 回归
```bash
cd test/wasmtest
..\..\lxclua.exe test_wasmtime.lua            # 7 项
..\..\lxclua.exe test_wasmtime_complex.lua    # 47 项
..\..\lxclua.exe test_linker.lua              # 17 项
..\..\lxclua.exe test_wat_wasi_trap.lua       # 19 项
..\..\lxclua.exe test_component_run.lua       # add=42
..\..\lxclua.exe test_component_wasi.lua      # preview2 stdout
..\..\lxclua.exe test_component_wasi_env.lua  # preview1 fd_write ENOSYS + preview2 env
..\..\lxclua.exe test_component_types.lua     # 17 项
..\..\lxclua.exe test_component_str.lua       # 6 项
..\..\lxclua.exe test_component_vr.lua        # variant/result
..\..\lxclua.exe test_component_listret.lua   # list 返回
..\..\lxclua.exe test_component_rust.lua      # Rust 组件 21 项
```
