#!/usr/bin/env lua
--[[
  Nirithy壳加解密测试脚本
  用于验证nirithy_unpacker_full.lua的正确性
  使用LXCLUA内置的bit库
]]

-- ============================================================
-- 使用LXCLUA内置的bit库（不要重新定义！）
-- ============================================================

local nirithy_b64_charset = "9876543210zyxwvutsrqponmlkjihgfedcbaZYXWVUTSRQPONMLKJIHGFEDCBA-_"

local function nirithy_b64_val(c)
  local p = string.find(nirithy_b64_charset, c, 1, true)
  if p then return p - 1 end
  return -1
end

local function nirithy_decode(input)
  if type(input) ~= "string" then
    return nil, "Input must be a string"
  end

  local input_len = #input
  if input_len == 0 then return "" end
  if input_len % 4 ~= 0 then
    return nil, string.format("Invalid base64 length: %d", input_len)
  end

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

    local va = (ch_a == '=') and 0 or nirithy_b64_val(ch_a)
    local vb = (ch_b == '=') and 0 or nirithy_b64_val(ch_b)
    local vc = (ch_c == '=') and 0 or nirithy_b64_val(ch_c)
    local vd = (ch_d == '=') and 0 or nirithy_b64_val(ch_d)

    if va < 0 or vb < 0 or vc < 0 or vd < 0 then
      return nil, string.format("Invalid base64 character at position %d", i)
    end

    local triple = bit.lshift(va, 18) + bit.lshift(vb, 12) + bit.lshift(vc, 6) + vd

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

local function nirithy_encode(input)
  local input_len = #input
  if input_len == 0 then return "" end

  local out_len = 4 * math.floor((input_len + 2) / 3)
  local out = {}

  local j = 1
  local i = 1
  while i <= input_len do
    local a = string.byte(input, i) or 0; i = i + 1
    local b = string.byte(input, i) or 0; i = i + 1
    local c = string.byte(input, i) or 0; i = i + 1

    local triple = bit.lshift(a, 16) + bit.lshift(b, 8) + c

    out[j] = string.sub(nirithy_b64_charset, bit.band(bit.rshift(triple, 18), 0x3F) + 1,
                        bit.band(bit.rshift(triple, 18), 0x3F) + 1); j = j + 1
    out[j] = string.sub(nirithy_b64_charset, bit.band(bit.rshift(triple, 12), 0x3F) + 1,
                        bit.band(bit.rshift(triple, 12), 0x3F) + 1); j = j + 1
    out[j] = string.sub(nirithy_b64_charset, bit.band(bit.rshift(triple, 6), 0x3F) + 1,
                        bit.band(bit.rshift(triple, 6), 0x3F) + 1); j = j + 1
    out[j] = string.sub(nirithy_b64_charset, bit.band(triple, 0x3F) + 1,
                        bit.band(triple, 0x3F) + 1); j = j + 1
  end

  if input_len % 3 == 1 then
    out[out_len] = '='
    out[out_len - 1] = '='
  elseif input_len % 3 == 2 then
    out[out_len] = '='
  end

  return table.concat(out)
end

-- SHA256实现 (使用LXCLUA的bit库)
local SHA256_K = {
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
  return bit.bor(bit.rshift(n, r), bit.lshift(n, 32 - r))
end

function sha256_hash(msg)
  local h0 = 0x6a09e667; local h1 = 0xbb67ae85
  local h2 = 0x3c6ef372; local h3 = 0xa54ff53a
  local h4 = 0x510e527f; local h5 = 0x9b05688c
  local h6 = 0x1f83d9ab; local h7 = 0x5be0cd19

  -- 预处理
  local len = #msg
  local bit_len = len * 8
  msg = msg .. string.char(0x80)
  while (#msg % 64) ~= 56 do msg = msg .. string.char(0) end
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

  for i = 1, #msg, 64 do
    local w = {}
    for j = 0, 15 do
      w[j] = bit.bor(
        bit.lshift(string.byte(msg, i+j) or 0, 24),
        bit.lshift(string.byte(msg, i+j+1) or 0, 16),
        bit.lshift(string.byte(msg, i+j+2) or 0, 8),
        string.byte(msg, i+j+3) or 0
      )
    end

    for j = 16, 63 do
      local s0 = bit.bxor(sha256_rr(w[j-15], 7), sha256_rr(w[j-15], 18), bit.rshift(w[j-15], 3))
      local s1 = bit.bxor(sha256_rr(w[j-2], 17), sha256_rr(w[j-2], 19), bit.rshift(w[j-2], 10))
      w[j] = bit.band(w[j-16] + s0 + w[j-7] + s1, 0xFFFFFFFF)
    end

    local a, b, c, d, e, f, g, hh = h0, h1, h2, h3, h4, h5, h6, h7

    for j = 0, 63 do
      local S1 = bit.bxor(sha256_rr(e, 6), sha256_rr(e, 11), sha256_rr(e, 25))
      local ch = bit.bor(bit.band(e, f), bit.band(bit.bnot(e), g))
      local temp1 = bit.band(hh + S1 + ch + SHA256_K[j+1] + w[j], 0xFFFFFFFF)
      local S0 = bit.bxor(sha256_rr(a, 2), sha256_rr(a, 13), sha256_rr(a, 22))
      local maj = bit.bor(bit.band(a, b), bit.band(a, c), bit.band(b, c))
      local temp2 = bit.band(S0 + maj, 0xFFFFFFFF)

      hh = g; g = f; f = e; e = bit.band(d + temp1, 0xFFFFFFFF)
      d = c; c = b; b = a; a = bit.band(temp1 + temp2, 0xFFFFFFFF)
    end

    h0 = bit.band(h0 + a, 0xFFFFFFFF); h1 = bit.band(h1 + b, 0xFFFFFFFF)
    h2 = bit.band(h2 + c, 0xFFFFFFFF); h3 = bit.band(h3 + d, 0xFFFFFFFF)
    h4 = bit.band(h4 + e, 0xFFFFFFFF); h5 = bit.band(h5 + f, 0xFFFFFFFF)
    h6 = bit.band(h6 + g, 0xFFFFFFFF); h7 = bit.band(h7 + hh, 0xFFFFFFFF)
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

-- AES-128实现 (使用LXCLUA的bit库)
local AES_SBOX = {
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

local AES_RCON = {0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80, 0x1b, 0x36}

local function aes_xtime(a)
  local result = bit.lshift(a, 1)
  if result >= 256 then result = bit.bxor(result, 0x1b) end
  return result
end

local function aes_encrypt_block(block, key)
  local state = {{}, {}, {}, {}}
  for i = 0, 15 do
    state[math.floor(i/4)+1][i%4+1] = string.byte(block, i+1)
  end

  local key_bytes = {}
  for i = 1, 16 do key_bytes[i] = string.byte(key, i) end

  local round_keys = {}
  local rk = {{}, {}, {}, {}}
  for i = 1, 4 do
    for j = 1, 4 do rk[j][i] = key_bytes[(i-1)*4+j] end
  end
  round_keys[1] = rk

  for round = 1, 10 do
    local prev = round_keys[round]
    local new_rk = {{}, {}, {}, {}}
    local temp = {prev[2][4], prev[3][4], prev[4][4], prev[1][4]}
    table.insert(temp, 1, table.remove(temp, 1))
    for i = 1, 4 do temp[i] = AES_SBOX[temp[i]+1] end
    temp[1] = bit.bxor(temp[1], AES_RCON[round])

    new_rk[1][1] = bit.bxor(prev[1][1], temp[1])
    new_rk[2][1] = bit.bxor(prev[2][1], temp[2])
    new_rk[3][1] = bit.bxor(prev[3][1], temp[3])
    new_rk[4][1] = bit.bxor(prev[4][1], temp[4])

    for i = 2, 4 do
      new_rk[1][i] = bit.bxor(new_rk[1][i-1], prev[1][i])
      new_rk[2][i] = bit.bxor(new_rk[2][i-1], prev[2][i])
      new_rk[3][i] = bit.bxor(new_rk[3][i-1], prev[3][i])
      new_rk[4][i] = bit.bxor(new_rk[4][i-1], prev[4][i])
    end
    round_keys[round+1] = new_rk
  end

  for i = 1, 4 do
    for j = 1, 4 do state[i][j] = bit.bxor(state[i][j], round_keys[1][i][j]) end
  end

  for round = 2, 10 do
    for i = 1, 4 do for j = 1, 4 do state[i][j] = AES_SBOX[state[i][j]+1] end end
    for i = 2, 4 do
      local tmp = {}
      for j = 1, 4 do tmp[j] = state[i][((j-1+i-1)%4)+1] end
      state[i] = tmp
    end
    for j = 1, 4 do
      local a = {state[1][j], state[2][j], state[3][j], state[4][j]}
      state[1][j] = bit.bxor(aes_xtime(a[1]), aes_xtime(a[2]), a[2], a[3], a[4]) % 256
      state[2][j] = bit.bxor(a[1], aes_xtime(a[2]), aes_xtime(a[3]), a[3], a[4]) % 256
      state[3][j] = bit.bxor(a[1], a[2], aes_xtime(a[3]), aes_xtime(a[4]), a[4]) % 256
      state[4][j] = bit.bxor(aes_xtime(a[1]), a[1], a[2], a[3], aes_xtime(a[4])) % 256
    end
    for i = 1, 4 do for j = 1, 4 do state[i][j] = bit.bxor(state[i][j], round_keys[round][i][j]) end end
  end

  for i = 1, 4 do for j = 1, 4 do state[i][j] = AES_SBOX[state[i][j]+1] end end
  for i = 2, 4 do
    local tmp = {}
    for j = 1, 4 do tmp[j] = state[i][((j-1+i-1)%4)+1] end
    state[i] = tmp
  end
  for i = 1, 4 do for j = 1, 4 do state[i][j] = bit.bxor(state[i][j], round_keys[11][i][j]) end end

  local out = {}
  for col = 1, 4 do for row = 1, 4 do table.insert(out, state[row][col]) end end
  return string.char(table.unpack(out))
end

local function aes_ctr_crypt(data, key, iv)
  local result = {}
  local data_len = #data
  local blocks = math.ceil(data_len / 16)

  for block_idx = 0, blocks - 1 do
    local counter = iv
    if block_idx > 0 then
      local counter_bytes = {}
      for j = 1, 16 do counter_bytes[j] = string.byte(iv, j) end
      local carry = block_idx
      for j = 16, 9, -1 do
        local val = counter_bytes[j] + carry
        counter_bytes[j] = val % 256
        carry = math.floor(val / 256)
        if carry == 0 then break end
      end
      counter = string.char(table.unpack(counter_bytes))
    end

    local encrypted_counter = aes_encrypt_block(counter, key)
    local offset = block_idx * 16 + 1
    for j = 1, 16 do
      if offset + j - 1 <= data_len then
        result[offset+j-1] = string.char(
          bit.bxor(string.byte(data, offset+j-1), string.byte(encrypted_counter, j))
        )
      end
    end
  end

  return table.concat(result)
end

local function derive_key(timestamp)
  local timestamp_bytes = {}
  local ts = timestamp
  for i = 1, 8 do
    timestamp_bytes[i] = string.char(ts % 256)
    ts = math.floor(ts / 256)
  end
  local input = table.concat(timestamp_bytes) .. "NirithySalt"
  local digest = sha256_hash(input)
  return string.sub(digest, 1, 16)
end

-- ============================================================
-- 主测试流程
-- ============================================================

local function create_test_encrypted_file()
  local test_content = [[
-- 这是一个测试文件
-- Nirithy Shell Test
print("Hello from Nirithy encrypted file!")
local function test()
    local x = 10
    local y = 20
    return x + y
end

local result = test()
print(string.format("Result: %d", result))

-- 测试中文支持
local msg = "解密成功！"
print(msg)

for i = 1, 5 do
    print(string.format("Count: %d", i))
end

return true
]]

  print("=== 创建测试加密文件 ===")
  print("原始内容:")
  print(test_content)
  print("")

  local timestamp = os.time()
  math.randomseed(timestamp)
  local iv = ""
  for i = 1, 16 do
    iv = iv .. string.char(math.random(0, 255))
  end

  print(string.format("时间戳: %d (0x%016X)", timestamp, timestamp))
  print(string.format("IV (hex): %s",
    iv:gsub('.', function(c) return string.format('%02x', string.byte(c)) end)))
  print("")

  local key = derive_key(timestamp)
  print(string.format("派生密钥 (hex): %s",
    key:gsub('.', function(c) return string.format('%02x', string.byte(c)) end)))
  print("")

  local encrypted_data = aes_ctr_crypt(test_content, key, iv)

  local payload_bin = {}
  local ts = timestamp
  for i = 1, 8 do
    payload_bin[i] = string.char(ts % 256)
    ts = math.floor(ts / 256)
  end
  for i = 1, 16 do payload_bin[8+i] = string.sub(iv, i, i) end
  for i = 1, #encrypted_data do payload_bin[24+i] = string.sub(encrypted_data, i, i) end
  local binary_payload = table.concat(payload_bin)

  local b64_encoded = nirithy_encode(binary_payload)

  local encrypted_file = "test_nirithy_encrypted.lua"
  local f = io.open(encrypted_file, 'wb')
  f:write("Nirithy==" .. b64_encoded)
  f:close()

  print(string.format("[+] 已创建测试加密文件: %s", encrypted_file))
  local total_size = #("Nirithy==" .. b64_encoded)
  print(string.format("[+] 文件大小: %d 字节", total_size))

  return {
    original = test_content,
    encrypted_file = encrypted_file,
    timestamp = timestamp,
    iv = iv
  }
end

local function decrypt_nirithy_file(filepath)
  local file = io.open(filepath, 'rb')
  if not file then return nil, "无法打开文件: " .. filepath end
  local content = file:read('*a')
  file:close()

  if #content < 9 then return nil, "文件太小" end
  if string.sub(content, 1, 9) ~= "Nirithy==" then return nil, "无效签名" end

  print(string.format("[+] 文件大小: %d 字节", #content))

  local payload = string.sub(content, 10)
  local bin, err = nirithy_decode(payload)
  if not bin then return nil, "Base64解码失败: " .. (err or "") end

  print(string.format("[+] 解码后大小: %d 字节", #bin))
  if #bin <= 24 then return nil, "数据太小" end

  local timestamp_bytes = string.sub(bin, 1, 8)
  local iv = string.sub(bin, 9, 24)
  local encrypted_data = string.sub(bin, 25)

  local timestamp = 0
  for i = 8, 1, -1 do
    timestamp = timestamp * 256 + string.byte(timestamp_bytes, i)
  end

  print(string.format("[+] 时间戳: %d", timestamp))
  print(string.format("[+] IV: %s",
    iv:gsub('.', function(c) return string.format('%02x', string.byte(c)) end)))
  print(string.format("[+] 加密数据长度: %d 字节", #encrypted_data))

  local key = derive_key(timestamp)
  print(string.format("[+] 派生密钥: %s",
    key:gsub('.', function(c) return string.format('%02x', string.byte(c)) end)))

  local decrypted_data = aes_ctr_crypt(encrypted_data, key, iv)
  print(string.format("[+] 解密完成! 大小: %d 字节", #decrypted_data))

  return decrypted_data
end

local function run_test()
  print("")
  print("========================================")
  print("  Nirithy Shell Unpacker - 自动化测试")
  print("========================================")
  print("")

  local test_data = create_test_encrypted_file()

  print("")
  print("========================================")
  print("  开始解密测试")
  print("========================================")
  print("")

  local decrypted, err = decrypt_nirithy_file(test_data.encrypted_file)
  if not decrypted then
    print(string.format("\n[-] 解密失败: %s", err))
    return false
  end

  print("")
  print("========================================")
  print("  验证解密结果")
  print("========================================")
  print("")

  if decrypted == test_data.original then
    print("[✓] ✓✓✓ 解密成功! 内容完全匹配! ✓✓✓")
    print("")
    print("--- 原始内容 ---")
    print(test_data.original)
    print("")
    print("--- 解密后内容 ---")
    print(decrypted)
    return true
  else
    print("[×] ✗✗✗ 内容不匹配! ✗✗✗")
    print("")
    print("期望长度:", #test_data.original)
    print("实际长度:", #decrypted)
    print("")
    print("--- 前200字符对比 ---")
    print("期望:", string.sub(test_data.original, 1, 200))
    print("实际:", string.sub(decrypted, 1, 200))

    local max_len = math.max(#test_data.original, #decrypted)
    local diff_count = 0
    for i = 1, max_len do
      if string.sub(test_data.original, i, i) ~= string.sub(decrypted, i, i) then
        diff_count = diff_count + 1
        if diff_count <= 10 then
          print(string.format("位置 %d: 期望='%s'(%02X) 实际='%s'(%02X)",
            i,
            string.sub(test_data.original, i, i),
            string.byte(test_data.original, i) or 0,
            string.sub(decrypted, i, i),
            string.byte(decrypted, i) or 0
          ))
        end
      end
    end
    print(string.format("总差异字节数: %d", diff_count))

    return false
  end
end

local success = run_test()

print("")
if success then
  print("========================================")
  print("  [✓] 所有测试通过!")
  print("  Nirithy脱壳工具工作正常")
  print("========================================")
  os.exit(0)
else
  print("========================================")
  print("  [×] 测试失败!")
  print("========================================")
  os.exit(1)
end
