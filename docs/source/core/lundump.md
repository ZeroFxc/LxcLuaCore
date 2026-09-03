# core/lundump.c — 字节码装载（双格式：LXCLUA 分段加密 / 标准 5.5 转码）

> 职责：`luaU_undump` 把字节码流重建为 `Proto` 树。支持两种格式：
> ① LXCLUA 自有分段加密格式（校验签名、解操作码置换、解字符串替换）；
> ② 标准 Lua 5.5 二进制（32 位指令**转码**为 64 位指令集），
> 由 `force_standard` 或头部探测决定走哪条路径。

---

## 一、特性介绍

1. **头部探测分派**（`checkHeader`）：签名后读 `b1,b2`——
   `b1==8 && b2==8` 且未强制标准 → XCLUA 格式（校验 `0x5678` 整数与
   `370.5` 浮点探针）；否则按标准 5.5 头校验（version `0x55`、
   `int` 尺寸、`LUAC_INT(-0x5678)`、Instruction `0x12345678`、
   `lua_Integer`、`lua_Number(-370.5)`）。
2. **分段装载**（`loadSegmented`）：读 6 段长度 → 整块载入内存 →
   从 `meta` 段提取 `timestamp` → 逐段验 **HMAC-SHA256**（密钥
   `SHA256(timestamp)`）+ 全局签名 → 通过后切换为内存读取模式
   （`mem_base/mem_offset`）按偏移重建每个原型。
3. **操作码反置换**（`loadCode`）：校验合并映射表 SHA-256 → 读加密指令流
   → 时间戳 XOR 解密 → 64 位小端重组 → 先用第三映射表的反向表、
   再用 `opcode_map`（存的是反向映射）两次还原操作码。
4. **字符串解密**（`loadStringN`）：每串独立时间戳 + 256 替换表
   （先验映射表 SHA-256，失败报 `string map integrity verification failed`）
   → 时间戳 XOR（可叠加 `str_encrypt_key`）→ 反向替换表还原；
   长串（≥0xFF）再验内容 SHA-256（`string content integrity verification failed`）。
5. **反导入校验**（`loadUpvalues`）：`0x99` 标记分支验证 16 字节
   时间戳加密数据（解密后不得含 0，否则 `invalid upvalue validation data`）
   与时间戳 SHA-256（`invalid upvalue SHA-256 validation data`）；
   兼容 `>0x70`、`>0` 两级旧格式跳过逻辑。
6. **虚假数据消化**（`loadDebug`）：末尾读 `fake_debug_count` 并跳过
   对应 (pc, line) 对——与 `ldump.c` 写入的 2 组假数据配对。
7. **标准格式转码**（`transcodeInstruction`）：32 位指令 → 64 位：
   操作码映射 `0-47 不变；48-85 +1；86→OP_EXTRAARG(103)`（为本引擎
   插入的 `OP_SPACESHIP` 让位）；逐格式重组参数；`OP_EXTRAARG` 跟随
   `SETLIST/NEWTABLE` 时把标准 `(Ax<<10)|vC` 拆为本引擎更宽的
   `vC(20)/Ax` 字段。字符串用复用表 `S->h`（索引引用），`fixed` 模式
   长串直接 `luaS_newextlstr` 零拷贝。
8. **错误模型**：一切格式错误经 `error()` 抛
   `"%s: bad binary format (%s)"`（`LUA_ERRSYNTAX`）。
9. **VMP 钩点**：`lundump_vmp_hook_point()`。

---

## 二、关键函数（准确签名）

```c
/* 装载入口。force_standard=1 强制按标准 5.5 格式解析。
   返回主闭包（已锚定在栈上）。name 以 '@'/'=' 开头时去掉前缀作错误名。 */
LClosure *luaU_undump (lua_State *L, ZIO *Z, const char *name,
                       int force_standard);
```

装载路径分派（源码流程）：

```
checkHeader:
  XCLUA:  b1==8 && b2==8 → 校验 8/0x5678/370.5
  标准:   version==0x55 → int/Instruction/lua_Integer/lua_Number 探针
读顶层 upvalue 数 → 建 LClosure
is_standard ? loadFunction_Standard(递归) : loadSegmented(扁平重建)
断言 nupvalues == sizeupvalues → luai_verifycode
```

`LoadState` 双模式读取：`mem_base != NULL` 时从内存段读（分段装载阶段），
否则从 `ZIO` 流读；两者都带截断检查（`truncated chunk`）。

---

## 三、错误消息清单（实测可见）

| 触发条件 | 消息（`bad binary format (...)` 括号内） |
|---|---|
| 签名不符 | `not a binary chunk` |
| format 字节 | `format mismatch` |
| LUAC_DATA 不符 | `corrupted chunk` |
| 版本/尺寸/探针不符 | `version mismatch` / `int size mismatch` / `float size mismatch` / `integer format mismatch` / `float format mismatch` 等 |
| 段数 ≠ 6 | `invalid segment count` |
| 段 HMAC 失败 | `<name> segment integrity verification failed` |
| 全局签名失败 | `global integrity verification failed` |
| 操作码映射哈希 | `OPcode map integrity verification failed` |
| 字符串映射哈希 | `string map integrity verification failed` |
| 长串内容哈希 | `string content integrity verification failed` |
| 反导入数据 | `invalid upvalue validation data` / `invalid upvalue SHA-256 validation data` |
| 流截断 | `truncated chunk` / `truncated fixed buffer` |

---

## 四、运行验证（实测输出）

与 `ldump.md` 共用脚本 `v_dump.lua`：

```
R2 roundtrip result:	hello-roundtrip:15:96   -- 分段加密格式完整往返
R4 tamper rejected:	true                    -- 篡改字节后拒载
R5 strip roundtrip:	hello-roundtrip:15:96	smaller:	true
```

另（`ldo.md` 验证 D6/D7）：`load(bc, "x", "t")` 对二进制块报
`attempt to load a binary chunk (mode is 't')`；`load(bc, "x", "b")` 正常
加载执行——模式门控在 `f_parser`，实际解析在本文件。

---

## 五、与其他模块的关系

- 写端对应：`ldump.c`（段/字段逐一镜像）。
- 标准格式来源：外部 `luac`（5.5 官方）产物；`luaO_flatten`/VM 码表注册
  `luaO_registerVMCode` 在 `utils/lobfuscate.c`。
- `Nirithy==` 壳的解码发生在装载之前：`lauxlib.c` 的缓冲/文件读取路径
  （`luaL_loadbuffer`/文件 reader），解壳后才进入 `f_parser → luaU_undump`。
- `\x1bEnc` AES 流加密（另一种外层）由 `ldo.c f_parser` 分派、`lzio.c` 解密，
  与本文件的段内防护是**不同层次**的保护。
