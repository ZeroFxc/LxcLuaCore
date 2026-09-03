# core/ — 核心运行时（文档索引）

19 个 .c 文件全部完成逐文件文档（读码 + 运行验证）。

| 文件 | 文档 | 主题 |
|---|---|---|
| lobject.c | [lobject.md](lobject.md) | 值表示（TValue/15 类型）、算术分发、数字解析（0b/0o）、格式化 |
| lstring.c | [lstring.md](lstring.md) | 短串驻留（加锁）、外部长串、字符串缓存、userdata 分配 |
| ltable.c | [ltable.md](ltable.md) | 表双段结构、共享锁、**表访问日志子系统** |
| lfunc.c | [lfunc.md](lfunc.md) | 闭包/原型/UpVal、**调用队列（sleep/wake）**、**热替换**、字节码哈希 |
| lmem.c | [lmem.md](lmem.md) | 分配器协议、紧急 GC 重试、**小对象内存池（未接线）** |
| lzio.c | [lzio.md](lzio.md) | 缓冲流、**AES-CTR 字节码流解密** |
| lopcodes.c | [lopcodes.md](lopcodes.md) | **64 位乱序指令编码**、全部扩展指令清单 |
| linit.c | [linit.md](linit.md) | 39 库注册全表 |
| ltm.c | [ltm.md](ltm.md) | 27 元方法（扩展 `__mindex`/`__type`）、变参辅助 |
| lmap.c | [lmap.md](lmap.md) | Map 纯哈希容器（`[]` 字面量、nil 是值不是删除） |
| lstate.c | [lstate.md](lstate.md) | global_State 全字段、全局锁、自定义 opcode 表、关键字注册表 |
| ldo.c | [ldo.md](ldo.md) | 调用协议、**async 直路径**、**OP_AWAIT 恢复**、`\x1bEnc` 分派、VMP 钩点 |
| ldebug.c | [ldebug.md](ldebug.md) | 调试 API（扩展 k/T/h 选项）、**luaB_hotfix**、符号执行 |
| lgc.c | [lgc.md](lgc.md) | 三色 GC（扩展类型全覆盖、全局锁、池联动） |
| lapi.c | [lapi.md](lapi.md) | 公共 C API 全清单（OOP/命名空间/切片/指针/锁表/**TCC 运行时桥**） |
| lauxlib.c | [lauxlib.md](lauxlib.md) | 辅助层、**Nirithy== 壳解码**、**JSON 文件装载** |
| ldump.c | [ldump.md](ldump.md) | 分段加密字节码写出（操作码置换/字符串加密/HMAC 签名/反导入） |
| lundump.c | [lundump.md](lundump.md) | 双格式装载（分段加密校验 / 标准 5.5 转码 64 位） |
| lcode.c | [lcode.md](lcode.md) | 表达式代码生成（**管道/范围/三元/插值** 翻译） |

## 模块级关键结论

- **线程安全改造**贯穿全局：`g->lock`（全局）、`Table.lock`（读写锁）、
  `GCdebt` 原子化、驻留表加锁。
- **保护体系三层**：① 外层壳 `Nirithy==`（AES-128-CTR + 自定义 b64）→
  ② `\x1bEnc` 流解密（时间戳派生密钥）→ ③ 段内防护（操作码双重置换、
  字符串替换加密、HMAC-SHA256 双签名、反导入垃圾、虚假调试信息）。
- **类型系统** 15 种对外类型全部打通：创建（lapi）→ GC（lgc）→ 释放
  （各模块）→ 语言层语法（class/struct/map/namespace 等）。
- 验证脚本在本工作区 `v_*.lua`（经 `run_lua.sh` 可重放）。
