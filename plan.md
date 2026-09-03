# Plan: LXCLUA-NCore 文档重构（基于真实代码阅读 + 运行验证）

## 目标
重构 `docs/`：为 `src/` 下每个 .c 文件建立一份「读过源码、跑过验证」的文档，
包含：职责/特性介绍、关键数据结构与属性、关键函数（准确签名与调用方式）、
语法类文件的语法解释、模块间关系。所有示例代码先运行再写入文档。

## 环境检查
- [x] OS: Windows 10 (win32)，git-bash 可用
- [x] 工具链: llvm-mingw (E:\Env\llvm-mingw-20260616-ucrt-x86_64)，clang 22.1.8
- [x] 引擎可执行: `lxclua.exe` 运行正常（`_VERSION` = Lua 5.5）
  - **坑**: PATH 里 Git 自带旧 `libwinpthread-1.dll` 抢先加载导致 exit 127；
    运行时必须把 `/e/Env/llvm-mingw-20260616-ucrt-x86_64/bin` 放 PATH 最前。
    统一入口: `tools_path.sh`（本目录）。
- [x] luac.exe / lbcdump（bin）可用（待验证）
- [x] 现有文档: docs/ 9 篇主题文档（工作区中旧文档已删）；本次新增 docs/source/ 逐文件文档并重构索引。

## 步骤（按模块顺序，每个文件：读码 → 运行验证 → 写文档）
1. 文档骨架 + 验证脚本 — 产出: docs/source/ 目录、run_lua.sh
2. core/   (19 个 .c) — lobject/lstring/ltable/lgc/ldo/lvm 依赖链
3. compiler/ (12 个 .c) — llex、lparser、last_*、lcodegen、lasm、lbctc；含语法解释
4. vm/      (7 个 .c) — lvm 主循环、lvmpro、lvmlib、lvmustom、lnativevm/parser、lbytecode
5. stdlib/  (25 个 .c) — 每个库逐个运行示例验证后写
6. utils/   (20 个 .c) — 加密/异步/网络/大整数等
7. bin/     (5 个 .c) — lua/luac/luaccheck/lbcdump/lquickjs CLI
8. lspsrv/  (10 个 .c)
9. lua2wasm/ (11 个 .c)
10. wasm/   (17 个 .c) — m3_* 为 vendored wasm3，写简明文档
11. 重构 docs/README.md 总索引 + 与主题文档交叉引用
12. 全量校验: 链接有效性、示例全部重跑一遍

## 文档模板（每篇）
- 文件与职责（一句话）
- 特性介绍（该文件提供的能力）
- 关键数据结构/属性（字段级说明）
- 关键函数（准确签名 + 调用方式 + 前置条件）
- 语法解释（仅语法相关章节）
- 运行验证（实际跑过的命令与输出）
- 与其他模块的关系

## 当前进度
- [x] 环境确认、引擎运行验证（PATH 坑已解，run_lua.sh 可用）
- [x] 骨架与验证脚本（docs/source/ 9 个模块目录）
- [x] **core/ 完成 19/19**（全部含运行验证）+ core/README.md 索引
- [x] compiler/llex.md 完成 + compiler/README.md（模块索引 + 已验证语法清单）
- [ ] compiler 剩余 11 个文件（lparser 15426 行优先）
- [ ] vm/stdlib/utils/bin/lspsrv/lua2wasm/wasm 未开始（多会话任务）

## 发现与决策
- lxclua.exe 依赖正确 PATH（见上）。所有示例运行统一经 run_lua.sh。
- vendored 第三方代码（wasm/m3_*）只写简明概览，不做逐函数深读。
- 文档语言：中文；代码标识符保持原文。
- 0b/0o 数字字面量、math.type()、unused-local 告警均为引擎扩展。
- luaS_copystruct 实现在 stdlib/lstruct.c。
- lstring.c 驻留/扩容持 g->lock（递归锁）。
- **子代理配额耗尽（quota exhausted），改为串行亲自处理，优先 core 基础链。**

## 下一步
compiler 模块：llex.c（词法/全部字面量规则）→ lparser（语法全表）→
last_*（AST）→ lcodegen → lasm/llexerlib → lbctc。
