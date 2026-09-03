# core/ldump.c + lundump.h — 字节码序列化（写出侧，全防护格式）

> 职责：把 `Proto` 树序列化为 LXCLUA 自定义的**分段加密字节码**。包含
> 操作码双重置换、字符串替换加密、HMAC 完整性签名、反导入垃圾数据、
> 虚假调试信息，以及可选的控制流混淆联动。

---

## 一、特性介绍

1. **分段格式**：全部函数原型扁平化编号后，按 6 段写出——
   `meta / code / const / upval / protoref / debug`；父子关系经
   `protoref` 段的 ID 重建，段内各原型用偏移索引。
2. **操作码双重置换**：每个函数 `dumpCode` 生成两张随机置换表
   （Fisher-Yates + CSPRNG 的 `opcode_map`；以时间戳为种的线性同余
   `third_opcode_map`），指令操作码两次替换后按 64 位小端写出，
   再对整段字节流做时间戳 XOR；反向映射表与合并映射的
   **SHA-256** 一并写入供装载校验。
3. **字符串替换加密**：每个字符串独立时间戳 + 256 字节随机替换表
   （映射表本身带 SHA-256 校验）+ 时间戳字节轮转 XOR；若同时启用
   `OBFUSCATE_STR_ENCRYPT` 与 `OBFUSCATE_VM_PROTECT`，再叠加 VM 保护
   码表的 `encrypt_key` 轮转 XOR。短串（<0xFF）与长串路径分离，
   长串额外附内容 SHA-256。
4. **HMAC-SHA256 双层签名**：以 `SHA256(base_timestamp)` 为密钥，
   每段一个独立签名 + 覆盖全部段数据的全局签名。
5. **反导入/抗分析**：upvalue 段写 `0x99` 标记后附 15 组随机
   upvalue 假数据、16 字节时间戳加密验证数据（0 字节会改 1）、
   10 组基于映射表的混淆字节、时间戳 SHA-256；调试段额外写 2 组
   虚假 (pc, line)。
6. **头部伪装**：版本号字节 = `LUAC_VERSION` 高 4 位 + 时间戳低 4 位
   随机化；随后 `LUAC_FORMAT`、`LUAC_DATA`、三个尺寸字节 (8,8,8)、
   整数校验 `0x5678`、浮点校验 `370.5`。
7. **混淆联动**：`luaU_dump_obfuscated` 在写段前对每个原型调
   `luaO_flatten`（控制流扁平化等，见 `utils/lobfuscate.c`），
   种子以线性同余演进；混淆标志 `OBFUSCATE_CFF/
   BLOCK_SHUFFLE/BOGUS_BLOCKS/STATE_ENCODE`。
8. **VMP 钩点**：`luaU_dump` 入口 `ldump_vmp_hook_point()`。

---

## 二、字节码容器布局（实测 + 源码）

```
\x1B "Lua"                      -- LUA_SIGNATURE
version 字节（高4位固定，低4位随机）
LUAC_FORMAT 字节
LUAC_DATA（6 字节）
8, 8, 8                         -- Instruction/Integer/Number 尺寸
0x5678 (int64 LE)  370.5 (double LE)
顶层 upvalue 数（1 字节）
分段区:
  段数 = 6
  6 × 段长度（7 位变长 size 编码）
  for 每段: 段数据 + HMAC-SHA256(32)
  全局签名(32)
```

`meta` 段每个原型：`5×段内偏移、时间戳、numparams、is_vararg、
maxstacksize、difierline_mode、0x1337C0DE 填充、linedefined、
lastlinedefined、source 串、difierline_magicnum、difierline_data、
vm_code_table（若 VM 保护：size/encrypt_key/seed/code[]/reverse_map）`。

`code` 段每个原型：`指令数、反向映射表、第三映射表、合并映射 SHA-256、
加密指令流长度、加密指令流`。

变长整数（`dumpSize`）：每字节 7 位，**最后一字节置 0x80**（注意与
标准 5.5 相反：标准是续位在高字节）。

---

## 三、关键函数（准确签名）

```c
/* 标准入口：无混淆，默认防护全开。返回 0 成功 / 错误码失败。
   writer 逐块回调；内部用 CSPRNG（time(NULL) 种子） */
int luaU_dump (lua_State *L, const Proto *f, lua_Writer w, void *data,
               int strip);

/* 混淆入口：
   obfuscate_flags: OBFUSCATE_NONE(0)/CFF(1)/BLOCK_SHUFFLE(2)/
                    BOGUS_BLOCKS(4)/STATE_ENCODE(8)（可组合）
   seed: 0 = 用时间作种；log_path: NULL = 不输出混淆日志 */
int luaU_dump_obfuscated (lua_State *L, const Proto *f, lua_Writer w,
                          void *data, int strip, int obfuscate_flags,
                          unsigned int seed, const char *log_path);
```

调用方式要点：

- `Lua` 侧经 `string.dump(f [, opts])`（`stdlib/lstrlib.c`）；opts 表支持
  `strip / obfuscate / seed / envelop / log`，默认 `envelop=1` 会再包一层
  `Nirithy==` 壳（时间戳 + CSPRNG IV + AES-128-CTR + 自定义 base64，
  字母表 `9876543210zyx...BA-_`）。`luac` 直接输出裸二进制则无壳。
- 写失败经 `writer` 返回非 0 → `D->status` 置错并终止后续输出。
- `strip=1` 时 `debug` 段三个数组计数写 0，但**虚假调试对仍会写**。

---

## 四、运行验证（实测输出）

脚本 `v_dump.lua`（`run_lua.sh`）：

```
R1 dump size:	8065	header:	78	105      -- 'N','i'：string.dump 带 Nirithy== 壳
R2 roundtrip result:	hello-roundtrip:15:96  -- dump→load→执行 往返一致
R3 dumps differ:	false	(size:	8065	vs	8065)
R4 tamper rejected:	true
R4 reason:	... syntax error                    -- 改 1 字节后壳解码失败→拒载
R5 strip roundtrip:	hello-roundtrip:15:96	smaller:	true
```

结论：嵌套函数 + upvalue + 短/长串常量的加密往返正确；`strip` 缩小体积；
篡改任一字节即无法加载。注意 R3 同一秒两次 `string.dump` 结果相同
（壳层时间戳粒度为秒，同秒内随机源一致时输出可复现）——防复制依赖
的是装载端校验而非每次不同。壳解码在 `lauxlib.c` 的 load/loadfile 路径
（见 lauxlib 文档）。

---

## 五、与其他模块的关系

- 装载对端：`lundump.c`（格式逐段对应）。
- 壳层：`stdlib/lstrlib.c`（`Nirithy==` 编码）与 `core/lauxlib.c`（解码）。
- 混淆原语：`utils/lobfuscate.c`（`luaO_flatten`、`VMCodeTable`、
  `OBFUSCATE_*`）、`utils/csprng.c`、`utils/sha256.c`、`utils/aes.c`。
- `string.dump` 注册在 `lstrlib`；`luac.exe` 的 `-o` 直接调 `luaU_dump`
  （可选混淆参数见 `bin/luac.md`）。
