#!/usr/bin/env python3
"""
为 ldump.c 和 lundump.c 应用字节码结构重构：
1. 变长指令编码 (varint 替代固定8字节)
2. 寄存器重编号 (每个Proto独立置换表)
3. 常量间接引用 + 打乱
"""

import re

def read_file(path):
    with open(path, 'r', encoding='utf-8') as f:
        return f.read()

def write_file(path, content):
    with open(path, 'w', encoding='utf-8') as f:
        f.write(content)

# ============================================================
# 新的 DumpState 字段
# ============================================================
DUMPSTATE_ADD_FIELDS = """  int obfuscate_flags;  /* 混淆标志位 */
  unsigned int obfuscate_seed;  /* 混淆随机种子 */
  const char *log_path;  /* 调试日志输出路径 */
  Buffer *cur_buf;
  CSPRNG_State rng;  /* 密码学安全伪随机数生成器状态（替代srand/rand） */
  /* 结构重构：寄存器置换表和常量间接引用表 */
  int reg_perm[256];       /* 寄存器号正向置换表 */
  int reg_perm_inv[256];   /* 寄存器号逆向置换表（写入文件） */
  int *const_indir;         /* 常量间接引用表：文件位置 -> 原始下标 */
  int *const_indir_inv;     /* 常量间接引用逆表：原始下标 -> 文件位置 */
  int const_count;          /* 常量数量 */
} DumpState;"""

# ============================================================
# 新的 LoadState 字段
# ============================================================
LOADSTATE_ADD_FIELDS = """  int string_map[256];  /* 字符串映射表（用于动态加密解密） */
  /* 结构重构：寄存器置换表和常量间接引用表 */
  int reg_perm_inv[256];   /* 寄存器号逆向置换表 */
  int reg_perm[256];        /* 寄存器号正向置换表（从inv计算） */
  int *const_indir_inv;     /* 常量间接引用逆表 */
  int *const_indir;         /* 常量间接引用表（从inv计算） */
  int const_count;          /* 常量数量 */

  /* Standard Lua compatibility fields */"""

# ============================================================
# 变长编码辅助函数 (添加到 dumpCode 之前)
# ============================================================
VARINT_HELPERS_DUMP = """
/*
** 变长整数编码：将uint64_t编码为变长格式
** 每字节7位数据，最高位(bit7)为连续标志：1=还有后续字节，0=最后一个字节
** 小端序：低7位先输出
*/
static int encodeVarInt64(uint8_t *buf, uint64_t value) {
  int len = 0;
  do {
    uint8_t b = (uint8_t)(value & 0x7F);
    value >>= 7;
    if (value != 0) b |= 0x80;  /* 还有后续字节 */
    buf[len++] = b;
  } while (value != 0);
  return len;
}

/*
** 生成寄存器号置换表（每个Proto独立）
** 将寄存器号0..255随机打乱，防止反编译工具通过寄存器编号规律分析
*/
static void generateRegisterPerm(DumpState *D) {
  int i, j, temp;
  /* 初始化为顺序映射 */
  for (i = 0; i < 256; i++) {
    D->reg_perm[i] = i;
  }
  /* Fisher-Yates 洗牌（使用CSPRNG） */
  for (i = 255; i > 0; i--) {
    j = (int)csprng_range(&D->rng, (uint64_t)(i + 1));
    temp = D->reg_perm[i];
    D->reg_perm[i] = D->reg_perm[j];
    D->reg_perm[j] = temp;
  }
  /* 生成逆置换表 */
  for (i = 0; i < 256; i++) {
    D->reg_perm_inv[D->reg_perm[i]] = i;
  }
}

/*
** 生成常量间接引用表（每个Proto独立）
** 打乱常量表顺序，使得LOADK Bx引用的不是常量表的直接下标
*/
static void generateConstIndir(DumpState *D, int sizek) {
  int i, j, temp;
  D->const_count = sizek;
  if (sizek <= 0) {
    D->const_indir = NULL;
    D->const_indir_inv = NULL;
    return;
  }
  D->const_indir = (int *)luaM_malloc_(D->L, sizek * sizeof(int), 0);
  D->const_indir_inv = (int *)luaM_malloc_(D->L, sizek * sizeof(int), 0);
  /* 初始化为顺序映射 */
  for (i = 0; i < sizek; i++) {
    D->const_indir[i] = i;
  }
  /* Fisher-Yates 洗牌 */
  for (i = sizek - 1; i > 0; i--) {
    j = (int)csprng_range(&D->rng, (uint64_t)(i + 1));
    temp = D->const_indir[i];
    D->const_indir[i] = D->const_indir[j];
    D->const_indir[j] = temp;
  }
  /* const_indir[文件位置] = 原始下标 */
  /* const_indir_inv[原始下标] = 文件位置 */
  for (i = 0; i < sizek; i++) {
    D->const_indir_inv[D->const_indir[i]] = i;
  }
}

/*
** 将单条指令编码为变长格式
** 返回写入的字节数
** 编码格式：opcode(varint) a(varint) b(varint) c(varint) flags(varint)
** flags: bit0=k_flag, bit1-3=mode, bit4-7=reserved
*/
static int encodeInstruction(uint8_t *buf, Instruction inst,
                              int *reg_perm, int *const_indir_inv,
                              int sizek) {
  uint8_t *start = buf;
  OpCode op = GET_OPCODE(inst);
  enum OpMode mode = getOpMode(op);

  int a = 0, b = 0, c = 0, k = 0;
  int bx = 0, sbx = 0, ax = 0, sj = 0;
  int mode_idx = (int)mode;

  /* 标志：bit0=k, bit1-3=mode_idx */
  int flags = mode_idx << 1;

  switch (mode) {
    case iABC: {
      a = GETARG_A(inst);
      k = GETARG_k(inst);
      b = GETARG_B(inst);
      c = GETARG_C(inst);
      flags |= k;  /* bit0 = k */
      /* 寄存器重编号：A始终是寄存器，B/C仅在非k模式时是寄存器 */
      a = reg_perm[a & 0xFF];
      if (!k) b = reg_perm[b & 0xFF];
      if (!k) c = reg_perm[c & 0xFF];
      break;
    }
    case ivABC: {
      a = GETARG_A(inst);
      k = GETARG_k(inst);
      b = GETARG_vB(inst);
      c = GETARG_vC(inst);
      flags |= k;
      /* A是寄存器，B/C是值不是寄存器，不置换 */
      a = reg_perm[a & 0xFF];
      break;
    }
    case iABx: {
      a = GETARG_A(inst);
      bx = GETARG_Bx(inst);
      /* A是寄存器 */
      a = reg_perm[a & 0xFF];
      /* 常量间接引用：LOADK/LOADKX/NEWTABLE 的 Bx 引用常量表 */
      if ((op == OP_LOADK || op == OP_LOADKX || op == OP_NEWTABLE)
          && bx < sizek) {
        bx = const_indir_inv[bx];
      }
      b = bx;  /* 复用b字段存储bx */
      break;
    }
    case iAsBx: {
      a = GETARG_A(inst);
      sbx = GETARG_sBx(inst);
      a = reg_perm[a & 0xFF];
      /* sBx 是带符号偏移，转为无符号存储 */
      b = (unsigned int)(sbx + OFFSET_sBx);
      break;
    }
    case iAx: {
      ax = GETARG_Ax(inst);
      /* iAx模式的A字段不是寄存器，是扩展值，不置换 */
      a = ax;
      /* 标记为iAx模式使解码端知道不要对a做逆置换 */
      flags = (mode_idx << 1);  /* iAx = 4 */
      break;
    }
    case isJ: {
      sj = GETARG_sJ(inst);
      a = (unsigned int)(sj + OFFSET_sJ);
      b = 0;
      c = 0;
      flags = (mode_idx << 1);  /* isJ = 5 */
      break;
    }
  }

  /* 写入5个变长整数：opcode, a, b, c, flags */
  buf += encodeVarInt64(buf, (uint64_t)op);
  buf += encodeVarInt64(buf, (uint64_t)a);
  buf += encodeVarInt64(buf, (uint64_t)b);
  buf += encodeVarInt64(buf, (uint64_t)c);
  buf += encodeVarInt64(buf, (uint64_t)flags);

  return (int)(buf - start);
}

/*
** 释放常量间接引用表内存
*/
static void freeConstIndir(DumpState *D) {
  if (D->const_indir) {
    luaM_free_(D->L, D->const_indir, D->const_count * sizeof(int));
    D->const_indir = NULL;
  }
  if (D->const_indir_inv) {
    luaM_free_(D->L, D->const_indir_inv, D->const_count * sizeof(int));
    D->const_indir_inv = NULL;
  }
}
"""

# ============================================================
# 新的 dumpCode 函数
# ============================================================
NEW_DUMPCODE = """static void dumpCode (DumpState *D, const Proto *f) {
  int orig_size = f->sizecode;
  int i;

  /* 生成随机OPcode映射表 */
  generateOpcodeMap(D);

  /* 生成第三个OPcode映射表 */
  generateThirdOpcodeMap(D);

  /* ===== 结构重构：对每条指令进行编码转换 ===== */
  /* 估算最大编码大小：每条指令最多约 5 * 10 = 50 字节（极端情况），实际平均约 6-10 字节 */
  size_t max_encoded = (size_t)orig_size * 50;
  uint8_t *encoded_buf = (uint8_t *)luaM_malloc_(D->L, max_encoded, 0);
  if (encoded_buf == NULL) {
    D->status = LUA_ERRMEM;
    return;
  }

  size_t encoded_len = 0;
  for (i = 0; i < orig_size; i++) {
    Instruction inst = f->code[i];
    OpCode op = GET_OPCODE(inst);

    /* 应用OPcode双映射表 */
    SET_OPCODE(inst, D->opcode_map[op]);
    OpCode mapped_op = GET_OPCODE(inst);
    SET_OPCODE(inst, D->third_opcode_map[mapped_op]);

    /* 结构重构：变长编码 + 寄存器置换 + 常量间接引用 */
    int n = encodeInstruction(encoded_buf + encoded_len, inst,
                               D->reg_perm, D->const_indir_inv, f->sizek);
    encoded_len += (size_t)n;
  }

  /* XOR加密编码后的数据 */
  for (i = 0; i < (int)encoded_len; i++) {
    encoded_buf[i] ^= ((uint8_t *)&D->timestamp)[i % sizeof(D->timestamp)];
  }

  /* ===== 写入数据 ===== */
  /* 写入原始指令数量 */
  dumpInt(D, orig_size);

  /* 时间戳已在dumpFunction开头写入，此处不再重复写入 */

  /* 写入反向OPcode映射表 */
  for (i = 0; i < NUM_OPCODES; i++) {
    dumpByte(D, D->reverse_opcode_map[i]);
  }

  /* 写入第三个OPcode映射表 */
  for (i = 0; i < NUM_OPCODES; i++) {
    dumpByte(D, D->third_opcode_map[i]);
  }

  /* 写入寄存器逆向置换表（256字节） */
  for (i = 0; i < 256; i++) {
    dumpByte(D, D->reg_perm_inv[i]);
  }

  /* 计算并写入合并映射表的SHA-256哈希（OPcode表+寄存器置换表） */
  uint8_t allmaps_hash[SHA256_DIGEST_SIZE];
  {
    int combined_size = NUM_OPCODES * 2 + 256;
    int *combined = (int *)luaM_malloc_(D->L, combined_size * sizeof(int), 0);
    if (combined == NULL) {
      luaM_free_(D->L, encoded_buf, max_encoded);
      D->status = LUA_ERRMEM;
      return;
    }
    memcpy(combined, D->reverse_opcode_map, NUM_OPCODES * sizeof(int));
    memcpy(combined + NUM_OPCODES, D->third_opcode_map, NUM_OPCODES * sizeof(int));
    memcpy(combined + NUM_OPCODES * 2, D->reg_perm_inv, 256 * sizeof(int));
    SHA256((uint8_t *)combined, combined_size * sizeof(int), allmaps_hash);
    luaM_free_(D->L, combined, combined_size * sizeof(int));
  }
  dumpVector(D, allmaps_hash, SHA256_DIGEST_SIZE);

  /* 写入加密后的变长编码数据 */
  dumpSize(D, encoded_len);
  dumpBlock(D, encoded_buf, encoded_len);

  /* 释放内存 */
  luaM_free_(D->L, encoded_buf, max_encoded);
}"""

# ============================================================
# 新的 dumpConstants 函数（常量打乱）
# ============================================================
NEW_DUMPCONSTANTS = """static void dumpConstants (DumpState *D, const Proto *f) {
  int i;
  int n = f->sizek;
  dumpInt(D, n);

  /* 写入常量间接引用逆表（加载时用于恢复顺序） */
  if (n > 0 && D->const_indir_inv) {
    for (i = 0; i < n; i++) {
      dumpInt(D, D->const_indir_inv[i]);
    }
  }

  /* 按打乱后的顺序写入常量 */
  for (i = 0; i < n; i++) {
    /* const_indir[i] 指向原始常量下标 */
    const TValue *o = &f->k[D->const_indir ? D->const_indir[i] : i];
    int tt = ttypetag(o);
    dumpByte(D, tt);
    switch (tt) {
      case LUA_VNUMFLT:
        dumpNumber(D, fltvalue(o));
        break;
      case LUA_VNUMINT:
        dumpInteger(D, ivalue(o));
        break;
      case LUA_VSHRSTR:
      case LUA_VLNGSTR:
        dumpString(D, tsvalue(o));
        break;
      default:
        lua_assert(tt == LUA_VNIL || tt == LUA_VFALSE || tt == LUA_VTRUE);
    }
  }
}"""

# ============================================================
# 变长解码辅助函数 (添加到 loadCode 之前)
# ============================================================
VARINT_HELPERS_LOAD = """
/*
** 从变长格式解码uint64_t
** pbuf: 指向数据指针的指针（解码后自动推进）
** end:  缓冲区末尾
*/
static uint64_t decodeVarInt64(const uint8_t **pbuf, const uint8_t *end) {
  uint64_t value = 0;
  int shift = 0;
  while (*pbuf < end && shift < 64) {
    uint8_t b = *((*pbuf)++);
    value |= ((uint64_t)(b & 0x7F)) << shift;
    if (!(b & 0x80)) break;  /* 最高位为0表示结束 */
    shift += 7;
  }
  return value;
}

/*
** 从变长格式解码单条指令
** 编码格式：opcode(varint) a(varint) b(varint) c(varint) flags(varint)
*/
static Instruction decodeInstruction(const uint8_t **pbuf, const uint8_t *end,
                                      int *reg_perm_inv, int *const_indir,
                                      int sizek) {
  (void)sizek; /* 保留参数用于未来扩展 */

  uint64_t op64 = decodeVarInt64(pbuf, end);
  uint64_t a64  = decodeVarInt64(pbuf, end);
  uint64_t b64  = decodeVarInt64(pbuf, end);
  uint64_t c64  = decodeVarInt64(pbuf, end);
  uint64_t fl64 = decodeVarInt64(pbuf, end);

  int op      = (int)op64;
  int k       = (int)(fl64 & 1);
  int mode_idx= (int)((fl64 >> 1) & 7);

  int a = (int)a64;
  int b = (int)b64;
  int c = (int)c64;

  Instruction inst = 0;

  switch (mode_idx) {
    case 0: { /* iABC */
      /* 寄存器逆置换：恢复原始寄存器号 */
      a = reg_perm_inv[a & 0xFF];
      if (!k) b = reg_perm_inv[b & 0xFF];
      if (!k) c = reg_perm_inv[c & 0xFF];
      SET_OPCODE(inst, op);
      SETARG_A(inst, a);
      SETARG_B(inst, b);
      SETARG_C(inst, c);
      SETARG_k(inst, k);
      break;
    }
    case 1: { /* ivABC */
      a = reg_perm_inv[a & 0xFF];
      SET_OPCODE(inst, op);
      SETARG_A(inst, a);
      SETARG_vB(inst, b);
      SETARG_vC(inst, c);
      SETARG_k(inst, k);
      break;
    }
    case 2: { /* iABx */
      a = reg_perm_inv[a & 0xFF];
      /* 常量间接引用逆向：LOADK/LOADKX/NEWTABLE 恢复原始常量下标 */
      if ((op == OP_LOADK || op == OP_LOADKX || op == OP_NEWTABLE)
          && const_indir && b < sizek) {
        b = const_indir[b];
      }
      SET_OPCODE(inst, op);
      SETARG_A(inst, a);
      SETARG_Bx(inst, b);
      break;
    }
    case 3: { /* iAsBx */
      a = reg_perm_inv[a & 0xFF];
      SET_OPCODE(inst, op);
      SETARG_A(inst, a);
      SETARG_sBx(inst, (int)b - OFFSET_sBx);
      break;
    }
    case 4: { /* iAx */
      /* A字段不是寄存器，不置换 */
      SET_OPCODE(inst, op);
      SETARG_Ax(inst, a);
      break;
    }
    case 5: { /* isJ */
      SET_OPCODE(inst, op);
      SETARG_sJ(inst, (int)a - OFFSET_sJ);
      break;
    }
    default: {
      /* 未知模式，尽力解码为iABC */
      SET_OPCODE(inst, op);
      SETARG_A(inst, a);
      SETARG_B(inst, b);
      SETARG_C(inst, c);
      break;
    }
  }
  return inst;
}
"""

# ============================================================
# 新的 loadCode 函数
# ============================================================
NEW_LOADCODE = """static void loadCode (LoadState *S, Proto *f) {
  int orig_size = loadInt(S);
  int i;

  /* 时间戳已在loadFunction开头读取，此处不再重复读取 */

  // Read OPcode映射表
  for (i = 0; i < NUM_OPCODES; i++) {
    S->opcode_map[i] = loadByte(S);
  }

  // Read third OPcode映射表
  for (i = 0; i < NUM_OPCODES; i++) {
    S->third_opcode_map[i] = loadByte(S);
  }

  // Read 寄存器逆向置换表
  for (i = 0; i < 256; i++) {
    S->reg_perm_inv[i] = loadByte(S);
    /* 同时构建正向置换表 */
    S->reg_perm[S->reg_perm_inv[i]] = i;
  }

  // 读取并验证合并映射表的SHA-256哈希值
  uint8_t expected_hash[SHA256_DIGEST_SIZE];
  loadVector(S, expected_hash, SHA256_DIGEST_SIZE);
  {
    int combined_size = NUM_OPCODES * 2 + 256;
    int *combined = (int *)luaM_malloc_(S->L, combined_size * sizeof(int), 0);
    if (combined == NULL) {
      error(S, "memory allocation failed for combined map");
      return;
    }
    memcpy(combined, S->opcode_map, NUM_OPCODES * sizeof(int));
    memcpy(combined + NUM_OPCODES, S->third_opcode_map, NUM_OPCODES * sizeof(int));
    memcpy(combined + NUM_OPCODES * 2, S->reg_perm_inv, 256 * sizeof(int));
    uint8_t actual_hash[SHA256_DIGEST_SIZE];
    SHA256((uint8_t *)combined, combined_size * sizeof(int), actual_hash);
    luaM_free_(S->L, combined, combined_size * sizeof(int));
    if (memcmp(actual_hash, expected_hash, SHA256_DIGEST_SIZE) != 0) {
      error(S, "combined map integrity verification failed");
      return;
    }
  }

  // 读取加密数据长度
  size_t encrypted_len = loadSize(S);

  // 分配内存
  unsigned char *encrypted_data = (unsigned char *)luaM_malloc_(S->L, encrypted_len, 0);
  if (encrypted_data == NULL) {
    error(S, "memory allocation failed for encrypted data");
    return;
  }

  // 读取加密数据
  loadBlock(S, encrypted_data, encrypted_len);

  // XOR解密
  for (i = 0; i < (int)encrypted_len; i++) {
    encrypted_data[i] ^= ((uint8_t *)&S->timestamp)[i % sizeof(S->timestamp)];
  }

  // 分配内存给最终指令数组
  f->code = luaM_newvectorchecked(S->L, orig_size, Instruction);
  f->sizecode = orig_size;

  // 从变长编码解码每条指令
  const uint8_t *pbuf = encrypted_data;
  const uint8_t *pend = encrypted_data + encrypted_len;
  for (i = 0; i < orig_size; i++) {
    f->code[i] = decodeInstruction(&pbuf, pend,
                                    S->reg_perm_inv, S->const_indir, f->sizek);
  }

  // 释放加密数据
  luaM_free_(S->L, encrypted_data, encrypted_len);

  // 应用反向OPcode映射，恢复原始OPcode
  int reverse_third_opcode_map[NUM_OPCODES];
  for (i = 0; i < NUM_OPCODES; i++) {
    reverse_third_opcode_map[S->third_opcode_map[i]] = i;
  }

  for (i = 0; i < orig_size; i++) {
    Instruction inst = f->code[i];
    OpCode op = GET_OPCODE(inst);
    /* 首先使用第三个OPcode映射表的反向映射恢复 */
    SET_OPCODE(inst, reverse_third_opcode_map[op]);
    /* 然后使用原始映射表恢复 */
    op = GET_OPCODE(inst);
    SET_OPCODE(inst, S->opcode_map[op]);
    f->code[i] = inst;
  }
}"""

# ============================================================
# 新的 loadConstants 函数（常量逆打乱）
# ============================================================
NEW_LOADCONSTANTS = """static void loadConstants (LoadState *S, Proto *f) {
  int i;
  int n = loadInt(S);
  f->k = luaM_newvectorchecked(S->L, n, TValue);
  f->sizek = n;
  for (i = 0; i < n; i++)
    setnilvalue(&f->k[i]);

  /* 读取常量间接引用逆表 */
  S->const_count = n;
  if (n > 0) {
    S->const_indir_inv = (int *)luaM_malloc_(S->L, n * sizeof(int), 0);
    S->const_indir = (int *)luaM_malloc_(S->L, n * sizeof(int), 0);
    if (S->const_indir_inv == NULL || S->const_indir == NULL) {
      error(S, "memory allocation failed for const indir");
      return;
    }
    for (i = 0; i < n; i++) {
      S->const_indir_inv[i] = loadInt(S);
      /* 构建正向表：const_indir[文件位置] = 原始下标 */
      S->const_indir[S->const_indir_inv[i]] = i;
    }
  }

  /* 按打乱顺序读取常量，使用间接引用表放回正确位置 */
  for (i = 0; i < n; i++) {
    /* const_indir[i] = 该位置的常量对应的原始下标 */
    TValue *o = &f->k[S->const_indir[i]];
    int t = loadByte(S);
    switch (t) {
      case LUA_VNIL:
        setnilvalue(o);
        break;
      case LUA_VFALSE:
        setbfvalue(o);
        break;
      case LUA_VTRUE:
        setbtvalue(o);
        break;
      case LUA_VNUMFLT:
        setfltvalue(o, loadNumber(S));
        break;
      case LUA_VNUMINT:
        setivalue(o, loadInteger(S));
        break;
      case LUA_VSHRSTR:
      case LUA_VLNGSTR:
        setsvalue2n(S->L, o, loadString(S, f));
        break;
      default: lua_assert(0);
    }
  }

  /* 释放常量间接引用表（后续不再需要） */
  if (n > 0) {
    luaM_free_(S->L, S->const_indir_inv, n * sizeof(int));
    luaM_free_(S->L, S->const_indir, n * sizeof(int));
    S->const_indir_inv = NULL;
    S->const_indir = NULL;
  }
}"""


def modify_ldump(content):
    """修改 ldump.c"""

    # 1. 添加 DumpState 新字段
    content = content.replace(
        "  CSPRNG_State rng;  /* 密码学安全伪随机数生成器状态（替代srand/rand） */\n} DumpState;",
        DUMPSTATE_ADD_FIELDS
    )

    # 2. 替换 dumpCode 函数
    old_dumpcode = re.search(
        r'static void dumpCode \(DumpState \*D, const Proto \*f\) \{.*?\n\}',
        content, re.DOTALL
    )
    if old_dumpcode:
        content = content.replace(old_dumpcode.group(0), NEW_DUMPCODE)

    # 3. 替换 dumpConstants 函数
    old_dumpconst = re.search(
        r'static void dumpConstants \(DumpState \*D, const Proto \*f\) \{.*?\n\}',
        content, re.DOTALL
    )
    if old_dumpconst:
        content = content.replace(old_dumpconst.group(0), NEW_DUMPCONSTANTS)

    # 4. 在 generateThirdOpcodeMap 函数之后添加变长编码辅助函数
    marker = "    D->obfuscate_seed = D->obfuscate_seed * 1664525 + 1013904223;"
    # 找到 dumpString 函数中的这个标记行（只在 generateThirdOpcodeMap 之后的第一处）
    # 实际上我们需要在 generateThirdOpcodeMap 函数结束}之后插入
    old_gen_third = re.search(
        r'(static void generateThirdOpcodeMap.*?\n\})',
        content, re.DOTALL
    )
    if old_gen_third:
        insertion_point = old_gen_third.end()
        content = content[:insertion_point] + "\n\n" + VARINT_HELPERS_DUMP + content[insertion_point:]

    # 5. 在 dumpSegmented 的 proto 循环中，dumpCode 调用前添加置换表生成
    # 找到 "    /* Dump Code */" 行并在其之前插入
    old_dumpcode_call = "    /* Dump Code */\n    D->cur_buf = &buf_code;\n    dumpCode(D, work_proto);"
    new_dumpcode_call = """    /* ===== 结构重构：生成此Proto的寄存器置换表和常量间接引用表 ===== */
    generateRegisterPerm(D);
    generateConstIndir(D, work_proto->sizek);

    /* Dump Code */
    D->cur_buf = &buf_code;
    dumpCode(D, work_proto);"""
    content = content.replace(old_dumpcode_call, new_dumpcode_call)

    # 6. 在 dumpConstants 调用后添加内存释放
    old_dumpconst_call = "    D->cur_buf = &buf_const;\n    dumpConstants(D, work_proto);"
    new_dumpconst_call = """    D->cur_buf = &buf_const;
    dumpConstants(D, work_proto);

    /* 释放常量间接引用表（每个Proto独立） */
    freeConstIndir(D);"""
    content = content.replace(old_dumpconst_call, new_dumpconst_call)

    return content


def modify_lundump(content):
    """修改 lundump.c"""

    # 1. 添加 LoadState 新字段
    content = content.replace(
        "  int string_map[256];  /* 字符串映射表（用于动态加密解密） */\n\n  /* Standard Lua compatibility fields */",
        LOADSTATE_ADD_FIELDS
    )

    # 2. 在 loadCode 之前添加变长解码辅助函数
    # 找到 "static void loadCode" 并在其之前插入
    old_loadcode_start = "\nstatic void loadCode (LoadState *S, Proto *f) {"
    new_insertion = VARINT_HELPERS_LOAD + "\n" + "static void loadCode (LoadState *S, Proto *f) {"
    content = content.replace(old_loadcode_start, new_insertion)

    # 3. 替换 loadCode 函数
    # 由于我们已经在上面修改了函数开头，需要重新定位
    old_loadcode = re.search(
        r'static void loadCode \(LoadState \*S, Proto \*f\) \{.*?\n\}',
        content, re.DOTALL
    )
    if old_loadcode:
        content = content.replace(old_loadcode.group(0), NEW_LOADCODE)

    # 4. 替换 loadConstants 函数
    old_loadconst = re.search(
        r'static void loadConstants \(LoadState \*S, Proto \*f\) \{.*?\n\}',
        content, re.DOTALL
    )
    if old_loadconst:
        content = content.replace(old_loadconst.group(0), NEW_LOADCONSTANTS)

    return content


def main():
    ldump_path = r'e:\Soft\Proje\LXCLUA-NCore\lua\src\core\ldump.c'
    lundump_path = r'e:\Soft\Proje\LXCLUA-NCore\lua\src\core\lundump.c'

    print("Reading ldump.c...")
    ldump = read_file(ldump_path)
    print("Modifying ldump.c...")
    ldump = modify_ldump(ldump)
    print("Writing ldump.c...")
    write_file(ldump_path, ldump)
    print("ldump.c done.")

    print("Reading lundump.c...")
    lundump = read_file(lundump_path)
    print("Modifying lundump.c...")
    lundump = modify_lundump(lundump)
    print("Writing lundump.c...")
    write_file(lundump_path, lundump)
    print("lundump.c done.")

    print("\nAll structural changes applied successfully!")

if __name__ == '__main__':
    main()