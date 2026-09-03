# core/lzio.c + lzio.h — 带缓冲的输入流（ZIO）与字节码流解密

> 职责：为编译器/加载器提供缓冲输入流（ZIO + Mbuffer），把任意 `lua_Reader`
> 包装成逐字节可读的流；并在此之上实现 **AES-CTR 字节码流解密**（LXCLUA 扩展）。

---

## 一、特性介绍

1. **缓冲输入流**：`ZIO` 持有 `reader` 回调 + 缓冲指针（`p`/`n`），`zgetc` 逐字节
   读取，缓冲耗尽时 `luaZ_fill` 调 `reader` 续杯；`EOZ(-1)` 表示流结束。
2. **`Mbuffer` 可变缓冲**：`luaZ_resizebuffer`/`luaZ_freebuffer` 等，供解析器累积源码。
3. **字节码流解密（LXCLUA 扩展）**：`ZIO` 内嵌 `AES_ctx` + 16 字节 keystream。
   密钥由 `SHA256(timestamp || "NirithySalt")` 前 16 字节导出（AES-128），
   以 CTR 方式逐字节异或解密——即 `luaZ_read_decrypt` 生成密钥流块、
   IV 自增、`b ^= keystream[idx]`。
4. **透明分派**：`zgetc` 宏按 `z->encrypted` 决定走明文 `*(z->p++)` 还是
   `luaZ_read_decrypt`；`luaZ_read` 批量读取时同样按字节解密。

---

## 二、关键数据结构与属性

```c
struct Zio {
  size_t n;             /* 缓冲中剩余未读字节 */
  const char *p;        /* 当前读取位置 */
  lua_Reader reader;    /* 续杯回调 */
  void *data;           /* reader 用户数据 */
  lua_State *L;         /* 供 reader 使用 */
  /* —— 解密状态（LXCLUA 扩展）—— */
  int encrypted;        /* 是否启用解密 */
  struct AES_ctx ctx;   /* AES-128 上下文（含 IV） */
  uint8_t keystream[16];/* 当前密钥流块 */
  int keystream_idx;    /* 块内消费位置，==16 触发换块 */
};

typedef struct Mbuffer { char *buffer; size_t n; size_t buffsize; } Mbuffer;
```

| 宏 | 说明 |
|---|---|
| `EOZ` | `-1`，流结束 |
| `zgetc(z)` | 读一字节（自动分派明文/解密，缓冲空则 `luaZ_fill`） |
| `zungetc(z)` | 回退一字节（`n++, p--`；不处理解密语义） |
| `luaZ_initbuffer / luaZ_buffer / luaZ_sizebuffer / luaZ_bufflen` | Mbuffer 基本操作 |
| `luaZ_buffremove / luaZ_resetbuffer / luaZ_resizebuffer / luaZ_freebuffer` | Mbuffer 维护 |

---

## 三、关键函数（准确签名）

```c
/* 初始化流：绑定 reader 与数据，encrypted 置 0 */
void luaZ_init (lua_State *L, ZIO *z, lua_Reader reader, void *data);

/* 启用解密：由时间戳派生 AES-128 密钥，装入 ctx 与 IV */
void luaZ_init_decrypt (ZIO *z, uint64_t timestamp, const uint8_t *iv);

/* 缓冲续杯：调 reader，返回新缓冲首字节或 EOZ（加密时返回解密后首字节） */
int luaZ_fill (ZIO *z);

/* 解密读取一字节（CTR：必要时生成密钥流块并使 IV 自增） */
int luaZ_read_decrypt (ZIO *z);

/* 批量读 n 字节到 b；返回未读够的字节数（0 = 全部读到） */
size_t luaZ_read (ZIO *z, void *b, size_t n);

/* 返回当前缓冲中连续 n 字节的地址并消费；不足返回 NULL */
const void *luaZ_getaddr (ZIO *z, size_t n);
```

调用方式要点：

- 读取入口优先用 `zgetc`/`luaZ_read`，勿直接操作 `p`/`n`（会绕过分派与解密）。
- `luaZ_getaddr` 只能返回**当前缓冲内**的连续块，跨缓冲返回 `NULL`——
  且不解密（返回原始缓冲地址），加密流上慎用。
- `luaZ_read_decrypt` 假设 `z->n > 0`，只移动 `p`、不减 `n`；
  `luaZ_read` 自行维护 `n`（源码注释明确指出此点）。

---

## 四、内部流程（解密读取）

```
zgetc / luaZ_read
  └─ encrypted?
       否 → 直接 *(z->p++)
       是 → luaZ_read_decrypt:
            keystream_idx >= 16 ?
              memcpy(keystream, ctx.Iv, 16)
              AES_ECB_encrypt(&ctx, keystream)   -- E(I) 作为密钥流
              ctx.Iv++（128 位大端自增）
              idx = 0
            b ^= keystream[idx++]
```

`luaZ_init_decrypt` 的密钥派生：`SHA256(timestamp(8B) || "NirithySalt"(11B))`
取前 16 字节作为 AES-128 密钥，`AES_init_ctx_iv` 装载密钥与外部提供的 IV。

---

## 五、运行验证

ZIO 属编译/加载内部路径，无直接 Lua 侧接口；其行为由"加密字节码能被正常加载执行"
间接覆盖（见 `utils/encrypt_bytecode.c` 与 `core/lundump.c` 文档）。本文件
`lxclua.exe` 加载普通脚本、`luac -l` 反汇编均正常（见 `lopcodes.md` 实测），
说明 ZIO 明文路径工作正常。解密路径需配套加密字节码端到端验证，归入
`lundump.md`/`encrypt_bytecode.md`。

---

## 六、与其他模块的关系

- 被 `llex.c`（词法扫描源码流）与 `lundump.c`（字节码装载）消费。
- AES 实现来自 `utils/aes.c`（`AES_ctx`/`AES_init_ctx_iv`/`AES_ECB_encrypt`），
  SHA-256 来自 `utils/sha256.c`。
- 与 `core/ldump.c` 的加密写出、`utils/encrypt_bytecode.c` 的时间戳密钥约定配套。
