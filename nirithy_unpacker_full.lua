#!/usr/bin/env lua
--[[
  Nirithy Shell Unpacker - 完整版 (LXCLUA-NCore)
  ================================================
  功能：完全解密Nirithy加密的Lua源码文件

  加密流程分析：
  1. 文件签名: "Nirithy==" (9字节)
  2. Base64编码数据（自定义字符集）
  3. 解码后结构: [timestamp(8B, LE)][IV(16B)][AES-128-CTR加密数据]
  4. 密钥派生: SHA256(timestamp + "NirithySalt")[0:16]

  作者: DiferLine
  日期: 2026-04-13
]]

-- ============================================================
-- 位操作兼容层 (支持 Lua 5.1/5.2/5.3/5.4/5.5+)
-- ============================================================
local bit = bit or bit32 or {}

if not bit.band then
  function bit.band(a, b)
    a = a % 0x100000000
    b = b % 0x100000000
    local result = 0
    local mask = 1
    for i = 1, 32 do
      if (a % 2 == 1) and (b % 2 == 1) then
        result = result + mask
      end
      a = math.floor(a / 2)
      b = math.floor(b / 2)
      mask = mask * 2
    end
    return result
  end
end

if not bit.bxor then
  function bit.bxor(a, b)
    a = a % 0x100000000
    b = b % 0x100000000
    local result = 0
    local mask = 1
    for i = 1, 32 do
      local abit = a % 2
      local bbit = b % 2
      if (abit ~= bbit) then
        result = result + mask
      end
      a = math.floor(a / 2)
      b = math.floor(b / 2)
      mask = mask * 2
    end
    return result
  end
end

if not bit.bnot then
  function bit.bnot(a)
    return (0xFFFFFFFF - (a % 0x100000000)) % 0x100000000
  end
end

if not bit.lshift then
  function bit.lshift(a, n)
    a = a % 0x100000000
    n = n % 32
    return (a * (2 ^ n)) % 0x100000000
  end
end

if not bit.rshift then
  function bit.rshift(a, n)
    a = a % 0x100000000
    n = n % 32
    return math.floor(a / (2 ^ n))
  end
end

-- ============================================================
-- Nirithy自定义Base64编解码模块
-- 字符集: "9876543210zyxwvutsrqponmlkjihgfedcbaZYXWVUTSRQPONMLKJIHGFEDCBA-_"
-- ============================================================
local NirithyBase64 = {}
NirithyBase64.__index = NirithyBase64

NirithyB64_CHARSET = "9876543210zyxwvutsrqponmlkjihgfedcbaZYXWVUTSRQPONMLKJIHGFEDCBA-_"

function NirithyBase64.val(c)
  local p = string.find(NirithyB64_CHARSET, c, 1, true)
  if p then return p - 1 end
  return -1
end

--[[
  解码Nirithy自定义Base64字符串
  @param input: Base64编码的字符串
  @return: 解码后的二进制字符串, 或nil+错误信息
]]
function NirithyBase64.decode(input)
  if type(input) ~= "string" then
    return nil, "Input must be a string"
  end

  local input_len = #input
  if input_len == 0 then return "" end
  if input_len % 4 ~= 0 then
    return nil, string.format("Invalid base64 length: %d (must be multiple of 4)", input_len)
  end

  -- 计算输出长度
  local len = math.floor(input_len / 4 * 3)
  if input_len > 0 and string.sub(input, -1) == '=' then
    len = len - 1
  end
  if input_len > 1 and string.sub(input, -2, -1):sub(1, 1) == '=' then
    len = len - 1
  end

  local out = {}
  local j = 1

  for i = 1, input_len, 4 do
    local ch_a = string.sub(input, i, i)
    local ch_b = string.sub(input, i+1, i+1)
    local ch_c = string.sub(input, i+2, i+2)
    local ch_d = string.sub(input, i+3, i+3)

    local va = (ch_a == '=') and 0 or NirithyBase64.val(ch_a)
    local vb = (ch_b == '=') and 0 or NirithyBase64.val(ch_b)
    local vc = (ch_c == '=') and 0 or NirithyBase64.val(ch_c)
    local vd = (ch_d == '=') and 0 or NirithyBase64.val(ch_d)

    if va < 0 or vb < 0 or vc < 0 or vd < 0 then
      return nil, string.format("Invalid base64 character at position %d", i)
    end

    -- 组合成24位整数
    local triple = bit.lshift(va, 18) + bit.lshift(vb, 12) + bit.lshift(vc, 6) + vd

    -- 提取3个字节
    if j <= len then
      out[j] = string.char(bit.band(bit.rshift(triple, 16), 0xFF))
      j = j + 1
    end
    if j <= len then
      out[j] = string.char(bit.band(bit.rshift(triple, 8), 0xFF))
      j = j + 1
    end
    if j <= len then
      out[j] = string.char(bit.band(triple, 0xFF))
      j = j + 1
    end
  end

  return table.concat(out)
end

-- ============================================================
-- SHA-256 纯Lua实现（用于密钥派生）
-- ============================================================
local SHA256 = {}

SHA256.K = {
  0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5,
  0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
  0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3,
  0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
  0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc,
  0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
  0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7,
  0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
  0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
  0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
  0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3,
  0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
  0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5,
  0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
  0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
  0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2
}

local function sha256_rr(n, r)
  return bit.rshift(n, r) + bit.lshift(n, 32 - r)
end

local function sha256_preprocess(msg)
  local len = #msg
  local bit_len = len * 8

  msg = msg .. string.char(0x80)

  while (#msg % 64) ~= 56 do
    msg = msg .. string.char(0)
  end

  msg = msg .. string.char(0, 0, 0, 0, 0, 0, 0, 0)
  msg = msg .. string.char(
    bit.band(bit.rshift(bit_len, 56), 0xFF),
    bit.band(bit.rshift(bit_len, 48), 0xFF),
    bit.band(bit.rshift(bit_len, 40), 0xFF),
    bit.band(bit.rshift(bit_len, 32), 0xFF),
    bit.band(bit.rshift(bit_len, 24), 0xFF),
    bit.band(bit.rshift(bit_len, 16), 0xFF),
    bit.band(bit.rshift(bit_len, 8), 0xFF),
    bit.band(bit_len, 0xFF)
  )

  return msg
end

function SHA256.hash(msg)
  local h0 = 0x6a09e667
  local h1 = 0xbb67ae85
  local h2 = 0x3c6ef372
  local h3 = 0xa54ff53a
  local h4 = 0x510e527f
  local h5 = 0x9b05688c
  local h6 = 0x1f83d9ab
  local h7 = 0x5be0cd19

  msg = sha256_preprocess(msg)

  for i = 1, #msg, 64 do
    local w = {}

    for j = 0, 15 do
      w[j] = bit.lshift(string.byte(msg, i+j), 24) +
             bit.lshift(string.byte(msg, i+j+1), 16) +
             bit.lshift(string.byte(msg, i+j+2), 8) +
             string.byte(msg, i+j+3)
    end

    for j = 16, 63 do
      local s0 = bit.bxor(sha256_rr(w[j-15], 7), sha256_rr(w[j-15], 18), bit.rshift(w[j-15], 3))
      local s1 = bit.bxor(sha256_rr(w[j-2], 17), sha256_rr(w[j-2], 19), bit.rshift(w[j-2], 10))
      w[j] = (w[j-16] + s0 + w[j-7] + s1) % 0x100000000
    end

    local a, b, c, d, e, f, g, hh = h0, h1, h2, h3, h4, h5, h6, h7

    for j = 0, 63 do
      local S1 = bit.bxor(sha256_rr(e, 6), sha256_rr(e, 11), sha256_rr(e, 25))
      local ch = bit.band(e, f) + bit.band(bit.bnot(e), g)
      local temp1 = (hh + S1 + ch + SHA256.K[j+1] + w[j]) % 0x100000000
      local S0 = bit.bxor(sha256_rr(a, 2), sha256_rr(a, 13), sha256_rr(a, 22))
      local maj = bit.band(a, b) + bit.band(a, c) + bit.band(b, c)
      local temp2 = (S0 + maj) % 0x100000000

      hh = g
      g = f
      f = e
      e = (d + temp1) % 0x100000000
      d = c
      c = b
      b = a
      a = (temp1 + temp2) % 0x100000000
    end

    h0 = (h0 + a) % 0x100000000
    h1 = (h1 + b) % 0x100000000
    h2 = (h2 + c) % 0x100000000
    h3 = (h3 + d) % 0x100000000
    h4 = (h4 + e) % 0x100000000
    h5 = (h5 + f) % 0x100000000
    h6 = (h6 + g) % 0x100000000
    h7 = (h7 + hh) % 0x100000000
  end

  return string.char(
    bit.band(bit.rshift(h0, 24), 0xFF), bit.band(bit.rshift(h0, 16), 0xFF),
    bit.band(bit.rshift(h0, 8), 0xFF), bit.band(h0, 0xFF),
    bit.band(bit.rshift(h1, 24), 0xFF), bit.band(bit.rshift(h1, 16), 0xFF),
    bit.band(bit.rshift(h1, 8), 0xFF), bit.band(h1, 0xFF),
    bit.band(bit.rshift(h2, 24), 0xFF), bit.band(bit.rshift(h2, 16), 0xFF),
    bit.band(bit.rshift(h2, 8), 0xFF), bit.band(h2, 0xFF),
    bit.band(bit.rshift(h3, 24), 0xFF), bit.band(bit.rshift(h3, 16), 0xFF),
    bit.band(bit.rshift(h3, 8), 0xFF), bit.band(h3, 0xFF),
    bit.band(bit.rshift(h4, 24), 0xFF), bit.band(bit.rshift(h4, 16), 0xFF),
    bit.band(bit.rshift(h4, 8), 0xFF), bit.band(h4, 0xFF),
    bit.band(bit.rshift(h5, 24), 0xFF), bit.band(bit.rshift(h5, 16), 0xFF),
    bit.band(bit.rshift(h5, 8), 0xFF), bit.band(h5, 0xFF),
    bit.band(bit.rshift(h6, 24), 0xFF), bit.band(bit.rshift(h6, 16), 0xFF),
    bit.band(bit.rshift(h6, 8), 0xFF), bit.band(h6, 0xFF),
    bit.band(bit.rshift(h7, 24), 0xFF), bit.band(bit.rshift(h7, 16), 0xFF),
    bit.band(bit.rshift(h7, 8), 0xFF), bit.band(h7, 0xFF)
  )
end

-- ============================================================
-- AES-128-CTR 模式解密实现
-- ============================================================
local AES128 = {}

-- AES S-Box
AES128.SBOX = {
  0x63, 0x7c, 0x77, 0x7b, 0xf2, 0x6b, 0x6f, 0xc5, 0x30, 0x01, 0x67, 0x2b, 0xfe, 0xd7, 0xab, 0x76,
  0xca, 0x82, 0xc9, 0x7d, 0xfa, 0x59, 0x47, 0xf0, 0xad, 0xd4, 0xa2, 0xaf, 0x9c, 0xa4, 0x72, 0xc0,
  0xb7, 0xfd, 0x93, 0x26, 0x36, 0x3f, 0xf7, 0xcc, 0x34, 0xa5, 0xe5, 0xf1, 0x71, 0xd8, 0x31, 0x15,
  0x04, 0xc7, 0x23, 0xc3, 0x18, 0x96, 0x05, 0x9a, 0x07, 0x12, 0x80, 0xe2, 0xeb, 0x27, 0xb2, 0x75,
  0x09, 0x83, 0x2c, 0x1a, 0x1b, 0x6e, 0x5a, 0xa0, 0x52, 0x3b, 0xd6, 0xb3, 0x29, 0xe3, 0x2f, 0x84,
  0x53, 0xd1, 0x00, 0xed, 0x20, 0xfc, 0xb1, 0x5b, 0x6a, 0xcb, 0xbe, 0x39, 0x4a, 0x4c, 0x58, 0xcf,
  0xd0, 0xef, 0xaa, 0xfb, 0x43, 0x4d, 0x33, 0x85, 0x45, 0xf9, 0x02, 0x7f, 0x50, 0x3c, 0x9f, 0xa8,
  0x51, 0xa3, 0x40, 0x8f, 0x92, 0x9d, 0x38, 0xf5, 0xbc, 0xb6, 0xda, 0x21, 0x10, 0xff, 0xf3, 0xd2,
  0xcd, 0x0c, 0x13, 0xec, 0x5f, 0x97, 0x44, 0x17, 0xc4, 0xa7, 0x7e, 0x3d, 0x64, 0x5d, 0x19, 0x73,
  0x60, 0x81, 0x4f, 0xdc, 0x22, 0x2a, 0x90, 0x88, 0x46, 0xee, 0xb8, 0x14, 0xde, 0x5e, 0x0b, 0xdb,
  0xe0, 0x32, 0x3a, 0x0a, 0x49, 0x06, 0x24, 0x5c, 0xc2, 0xd3, 0xac, 0x62, 0x91, 0x95, 0xe4, 0x79,
  0xe7, 0xc8, 0x37, 0x6d, 0x8d, 0xd5, 0x4e, 0xa9, 0x6c, 0x56, 0xf4, 0xea, 0x65, 0x7a, 0xae, 0x08,
  0xba, 0x78, 0x25, 0x2e, 0x1c, 0xa6, 0xb4, 0xc6, 0xe8, 0xdd, 0x74, 0x1f, 0x4b, 0xbd, 0x8b, 0x8a,
  0x70, 0x3e, 0xb5, 0x66, 0x48, 0x03, 0xf6, 0x0e, 0x61, 0x35, 0x57, 0xb9, 0x86, 0xc1, 0x1d, 0x9e,
  0xe1, 0xf8, 0x98, 0x11, 0x69, 0xd9, 0x8e, 0x94, 0x9b, 0x1e, 0x87, 0xe9, 0xce, 0x55, 0x28, 0xdf,
  0x8c, 0xa1, 0x89, 0x0d, 0xbf, 0xe6, 0x42, 0x68, 0x41, 0x99, 0x2d, 0x0f, 0xb0, 0x54, 0xbb, 0x16
}

-- AES逆S-Box
AES128.INV_SBOX = {
  0x52, 0x09, 0x6a, 0xd5, 0x30, 0x36, 0xa5, 0x38, 0xbf, 0x40, 0xa3, 0x9e, 0x81, 0xf3, 0xd7, 0xfb,
  0x7c, 0xe3, 0x39, 0x82, 0x9b, 0x2f, 0xff, 0x87, 0x34, 0x8e, 0x43, 0x44, 0xc4, 0xde, 0xe9, 0xcb,
  0x54, 0x7b, 0x94, 0x32, 0xa6, 0xc2, 0x23, 0x3d, 0xee, 0x4c, 0x95, 0x0b, 0x42, 0xfa, 0xc3, 0x4e,
  0x08, 0x2e, 0xa1, 0x66, 0x28, 0xd9, 0x24, 0xb2, 0x76, 0x5b, 0xa2, 0x49, 0x6d, 0x8b, 0xd1, 0x25,
  0x72, 0xf8, 0xf6, 0x64, 0x86, 0x68, 0x98, 0x16, 0xd4, 0xa4, 0x5c, 0xcc, 0x5d, 0x65, 0xb6, 0x92,
  0x6c, 0x70, 0x48, 0x50, 0xfd, 0xed, 0xb9, 0xda, 0x5e, 0x15, 0x46, 0x57, 0xa7, 0x8d, 0x9d, 0x84,
  0x90, 0xd8, 0xab, 0x00, 0x8c, 0xbc, 0xd3, 0x0a, 0xf7, 0xe4, 0x58, 0x05, 0xb8, 0xb3, 0x45, 0x06,
  0xd0, 0x2c, 0x1e, 0x8f, 0xca, 0x3f, 0x0f, 0x02, 0xc1, 0xaf, 0xbd, 0x03, 0x01, 0x13, 0x8a, 0x6b,
  0x3a, 0x91, 0x11, 0x41, 0x4f, 0x67, 0xdc, 0xea, 0x97, 0xf2, 0xcf, 0xce, 0xf0, 0xb4, 0xe6, 0x73,
  0x96, 0xac, 0x74, 0x22, 0xe7, 0xad, 0x35, 0x85, 0xe2, 0xf9, 0x37, 0xe8, 0x1c, 0x75, 0xdf, 0x6e,
  0x47, 0xf1, 0x1a, 0x71, 0x1d, 0x29, 0xc5, 0x89, 0x6f, 0xb7, 0x62, 0x0e, 0xaa, 0x18, 0xbe, 0x1b,
  0xfc, 0x56, 0x3e, 0x4b, 0xc6, 0xd2, 0x79, 0x20, 0x9a, 0xdb, 0xc0, 0xfe, 0x78, 0xcd, 0x5a, 0xf4,
  0x1f, 0xdd, 0xa8, 0x33, 0x88, 0x07, 0xc7, 0x31, 0xb1, 0x12, 0x10, 0x59, 0x27, 0x80, 0xec, 0x5f,
  0x60, 0x51, 0x7f, 0xa9, 0x19, 0xb5, 0x4a, 0x0d, 0x2d, 0xe5, 0x7a, 0x9f, 0x93, 0xc9, 0x9c, 0xef,
  0xa0, 0xe0, 0x3b, 0x4d, 0xae, 0x2a, 0xf5, 0xb0, 0xc8, 0xeb, 0xbb, 0x3c, 0x83, 0x53, 0x99, 0x61,
  0x17, 0x2b, 0x04, 0x7e, 0xba, 0x77, 0xd6, 0x26, 0xe1, 0x69, 0x14, 0x63, 0x55, 0x21, 0x0c, 0x7d
}

-- Rcon (轮常量)
AES128.RCON = {
  0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80, 0x1b, 0x36
}

--[[
  将字节字符串转换为4x4状态矩阵
  @param bytes: 16字节字符串
  @return: 4x4矩阵 {row1, row2, row3, row4}
]]
local function bytes_to_state(bytes)
  local state = {{}, {}, {}, {}}
  for i = 0, 15 do
    local row = math.floor(i / 4) + 1
    local col = i % 4 + 1
    state[row][col] = string.byte(bytes, i + 1)
  end
  return state
end

--[[
  将4x4状态矩阵转换为字节字符串
  @param state: 4x4矩阵
  @return: 16字节字符串
]]
local function state_to_bytes(state)
  local bytes = {}
  for col = 1, 4 do
    for row = 1, 4 do
      table.insert(bytes, state[row][col])
    end
  end
  return string.char(table.unpack(bytes))
end

--[[
  AES字替换 (SubBytes)
  使用S-Box对每个字节进行替换
]]
local function sub_bytes(state)
  for i = 1, 4 do
    for j = 1, 4 do
      state[i][j] = AES128.SBOX[state[i][j] + 1]
    end
  end
end

--[[
  AES行移位 (ShiftRows)
  对每一行进行循环左移
]]
local function shift_rows(state)
  for i = 2, 4 do
    local tmp = {}
    for j = 1, 4 do
      tmp[j] = state[i][((j - 1 + i - 1) % 4) + 1]
    end
    state[i] = tmp
  end
end

--[[
  AES列混合 (MixColumns)
  在GF(2^8)上进行矩阵乘法
]]
local function xtime(a)
  local result = bit.lshift(a, 1)
  if result >= 256 then
    result = bit.bxor(result, 0x1b)
  end
  return result
end

local function mix_columns(state)
  for j = 1, 4 do
    local a = {state[1][j], state[2][j], state[3][j], state[4][j]}
    state[1][j] = bit.bxor(xtime(a[1]), xtime(a[2]), a[2], a[3], a[4]) % 256
    state[2][j] = bit.bxor(a[1], xtime(a[2]), xtime(a[3]), a[3], a[4]) % 256
    state[3][j] = bit.bxor(a[1], a[2], xtime(a[3]), xtime(a[4]), a[4]) % 256
    state[4][j] = bit.bxor(xtime(a[1]), a[1], a[2], a[3], xtime(a[4])) % 256
  end
end

--[[
  AES轮密钥加 (AddRoundKey)
  将状态与轮密钥异或
]]
local function add_round_key(state, round_key)
  for i = 1, 4 do
    for j = 1, 4 do
      state[i][j] = bit.bxor(state[i][j], round_key[i][j])
    end
  end
end

--[[
  密钥扩展 (Key Expansion)
  从16字节密钥生成11个轮密钥
  @param key: 16字节密钥字符串
  @return: 轮密钥表 {round_key_1, ..., round_key_11}
]]
local function key_expansion(key)
  local key_bytes = {}
  for i = 1, 16 do
    key_bytes[i] = string.byte(key, i)
  end

  local round_keys = {}

  -- 初始轮密钥
  local rk = {{}, {}, {}, {}}
  for i = 1, 4 do
    for j = 1, 4 do
      rk[j][i] = key_bytes[(i-1)*4 + j]
    end
  end
  table.insert(round_keys, rk)

  -- 生成后续轮密钥
  for round = 1, 10 do
    local prev_rk = round_keys[round]
    local new_rk = {{}, {}, {}, {}}

    -- 第一个字
    local temp = {prev_rk[2][4], prev_rk[3][4], prev_rk[4][4], prev_rk[1][4]}
    -- RotWord
    table.insert(temp, 1, table.remove(temp, 1))
    -- SubWord
    for i = 1, 4 do
      temp[i] = AES128.SBOX[temp[i] + 1]
    end
    -- XOR with Rcon
    temp[1] = bit.bxor(temp[1], AES128.RCON[round])

    new_rk[1][1] = bit.bxor(prev_rk[1][1], temp[1])
    new_rk[2][1] = bit.bxor(prev_rk[2][1], temp[2])
    new_rk[3][1] = bit.bxor(prev_rk[3][1], temp[3])
    new_rk[4][1] = bit.bxor(prev_rk[4][1], temp[4])

    -- 其余3个字
    for i = 2, 4 do
      new_rk[1][i] = bit.bxor(new_rk[1][i-1], prev_rk[1][i])
      new_rk[2][i] = bit.bxor(new_rk[2][i-1], prev_rk[2][i])
      new_rk[3][i] = bit.bxor(new_rk[3][i-1], prev_rk[3][i])
      new_rk[4][i] = bit.bxor(new_rk[4][i-1], prev_rk[4][i])
    end

    table.insert(round_keys, new_rk)
  end

  return round_keys
end

--[[
  AES-128单块加密
  @param block: 16字节明文字符串
  @param key: 16字节密钥字符串
  @return: 16字节密文字符串
]]
function AES128.encrypt_block(block, key)
  local state = bytes_to_state(block)
  local round_keys = key_expansion(key)

  -- 初始轮密钥加
  add_round_key(state, round_keys[1])

  -- 9轮主循环
  for round = 2, 10 do
    sub_bytes(state)
    shift_rows(state)
    mix_columns(state)
    add_round_key(state, round_keys[round])
  end

  -- 最后一轮（无列混合）
  sub_bytes(state)
  shift_rows(state)
  add_round_key(state, round_keys[11])

  return state_to_bytes(state)
end

--[[
  AES-128-CTR模式加解密
  CTR模式是对称的，加密和解密使用相同的操作
  @param data: 待处理的数据字符串
  @param key: 16字节密钥字符串
  @param iv: 16字节初始向量字符串
  @return: 处理后的数据字符串
]]
function AES128.ctr_crypt(data, key, iv)
  local result = {}
  local data_len = #data
  local blocks = math.ceil(data_len / 16)

  for i = 0, blocks - 1 do
    -- 构造计数器块 (大端序)
    local counter = iv
    if i > 0 then
      -- 增量计数器（简单实现，实际可能需要更复杂的计数器管理）
      local counter_bytes = {}
      for j = 1, 16 do
        counter_bytes[j] = string.byte(iv, j)
      end

      -- 将最后8字节作为计数器部分并递增
      local carry = i
      for j = 16, 9, -1 do
        local val = counter_bytes[j] + carry
        counter_bytes[j] = val % 256
        carry = math.floor(val / 256)
        if carry == 0 then break end
      end

      counter = string.char(table.unpack(counter_bytes))
    end

    -- 加密计数器块
    local encrypted_counter = AES128.encrypt_block(counter, key)

    -- 与数据异或
    local offset = i * 16 + 1
    for j = 1, 16 do
      if offset + j - 1 <= data_len then
        result[offset + j - 1] = string.char(
          bit.bxor(
            string.byte(data, offset + j - 1),
            string.byte(encrypted_counter, j)
          )
        )
      end
    end
  end

  return table.concat(result)
end

-- ============================================================
-- Nirithy壳核心解密逻辑
-- ============================================================

--[[
  从二进制数据中提取小端序uint64_t值
  @param bytes: 8字节字符串
  @return: 整数值
]]
local function bytes_to_uint64_le(bytes)
  local result = 0
  for i = 8, 1, -1 do
    result = result * 256 + string.byte(bytes, i)
  end
  return result
end

--[[
  派生AES-128密钥
  使用SHA256(timestamp || "NirithySalt")的前16字节作为密钥
  @param timestamp: uint64_t时间戳
  @return: 16字节密钥字符串
]]
local function nirithy_derive_key(timestamp)
  -- 构造输入: timestamp (8字节小端序) + "NirithySalt" (11字节)
  local timestamp_bytes = {}
  local ts = timestamp
  for i = 1, 8 do
    timestamp_bytes[i] = string.char(ts % 256)
    ts = math.floor(ts / 256)
  end

  local input = table.concat(timestamp_bytes) .. "NirithySalt"

  print(string.format("[*] Key derivation input (%d bytes):", #input))

  -- 计算SHA256哈希
  local digest = SHA256.hash(input)

  print(string.format("[*] SHA256 digest (hex): %s",
    digest:gsub('.', function(c) return string.format('%02x', string.byte(c)) end)))

  -- 取前16字节作为AES-128密钥
  local key = string.sub(digest, 1, 16)

  print(string.format("[*] Derived AES key (hex): %s",
    key:gsub('.', function(c) return string.format('%02x', string.byte(c)) end)))

  return key
end

--[[
  完整的Nirithy文件解密函数
  @param filepath: 加密的文件路径
  @return: 解密后的源代码字符串, 或nil+错误信息
]]
local function decrypt_nirithy_file(filepath)
  -- 读取文件
  local file = io.open(filepath, 'rb')
  if not file then
    return nil, string.format("无法打开文件: %s", filepath)
  end

  local content = file:read('*a')
  file:close()

  -- 验证文件大小
  if #content < 9 then
    return nil, "文件太小，不是有效的Nirithy加密文件"
  end

  -- 检查签名
  local sig = string.sub(content, 1, 9)
  if sig ~= "Nirithy==" then
    return nil, "无效的Nirithy签名，该文件未使用Nirithy壳加密"
  end

  print(string.format("========================================"))
  print(string.format("  Nirithy Shell Unpacker"))
  print(string.format("  处理文件: %s", filepath))
  print(string.format("========================================"))
  print(string.format("[+] 文件大小: %d 字节", #content))
  print(string.format("[+] 签名验证: 通过"))

  -- 提取Base64 payload
  local payload = string.sub(content, 10)
  print(string.format("[+] Payload长度: %d 字节", #payload))

  -- Base64解码
  local bin, err = NirithyBase64.decode(payload)
  if not bin then
    return nil, "Base64解码失败: " .. (err or "未知错误")
  end

  print(string.format("[+] Base64解码后大小: %d 字节", #bin))

  -- 验证解码后数据大小
  if #bin <= 24 then
    return nil, "解码后的数据太小，格式错误"
  end

  -- 提取元数据
  local timestamp_bytes = string.sub(bin, 1, 8)
  local iv = string.sub(bin, 9, 24)
  local encrypted_data = string.sub(bin, 25)

  local timestamp = bytes_to_uint64_le(timestamp_bytes)

  print("")
  print("--- 元数据 ---")
  print(string.format("时间戳: %d (0x%016X)", timestamp, timestamp))
  print(string.format("IV (hex): %s",
    iv:gsub('.', function(c) return string.format('%02x', string.byte(c)) end)))
  print(string.format("加密数据长度: %d 字节", #encrypted_data))
  print("")

  -- 派生密钥
  print("--- 密钥派生 ---")
  local key = nirithy_derive_key(timestamp)
  print("")

  -- AES-128-CTR解密
  print("--- 解密过程 ---")
  local decrypted_data = AES128.ctr_crypt(encrypted_data, key, iv)

  print(string.format("[+] 解密完成! 输出大小: %d 字节", #decrypted_data))
  print("")

  return decrypted_data
end

-- ============================================================
-- 主程序入口
-- ============================================================

local function main()
  if #arg < 1 then
    print([[
用法: lua nirithy_unpacker.lua <加密文件> [输出文件]

Nirithy壳脱壳工具 - LXCLUA-NCore专用版
用于解密使用Nirithy壳加密的Lua源码文件

参数:
  加密文件   Nirithy加密的.lua文件路径
  输出文件   可选，指定输出文件路径（默认: 原文件名_decoded.lua）

示例:
  lua nirithy_unpacker.lua encrypted.lua
  lua nirithy_unpacker.lua encrypted.lua decrypted.lua

支持的加密算法:
  - 自定义Base64编码 (字符集: 9876543210...ZYXWVUTSRQPONMLKJIHGFEDCBA-_)
  - SHA-256 密钥派生
  - AES-128-CTR 对称加密

作者: DiferLine
日期: 2026-04-13
]])
    return 1
  end

  local input_file = arg[1]
  local output_file = arg[2]

  if not output_file then
    -- 自动生成输出文件名
    output_file = input_file:gsub('%.lua$', '') .. '_decoded.lua'
    if output_file == input_file then
      output_file = input_file .. '_decoded'
    end
  end

  -- 执行解密
  local result, err = decrypt_nirithy_file(input_file)
  if not result then
    print(string.format("\n[-] 错误: %s", err))
    return 1
  end

  -- 保存结果
  local f = io.open(output_file, 'wb')
  if not f then
    print(string.format("\n[-] 无法创建输出文件: %s", output_file))
    return 1
  end

  f:write(result)
  f:close()

  print(string.format("========================================"))
  print(string.format("[✓] 脱壳成功!"))
  print(string.format("[✓] 输出文件: %s", output_file))
  print(string.format("========================================"))

  -- 显示前几行预览
  print("")
  print("--- 解密内容预览 (前20行) ---")
  local lines = {}
  for line in result:gmatch('[^\r\n]+') do
    table.insert(lines, line)
    if #lines >= 20 then break end
  end
  for i, line in ipairs(lines) do
    print(string.format("%3d | %s", i, line))
  end
  if #result:gmatch('[^\r\n]+')() > 20 then
    print("... (更多内容已保存到文件)")
  end

  return 0
end

os.exit(main())
