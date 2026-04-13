#!/usr/bin/env lua
--[[
  Nirithy Shell Unpacker - LXCLUA-NCore Edition
  用于解密Nirithy加密的Lua源码文件

  文件格式:
    [Nirithy==][Base64 Payload]
    Base64解码后: [timestamp(8B)][IV(16B)][Encrypted Data]
]]

local nirithy_b64 = "9876543210zyxwvutsrqponmlkjihgfedcbaZYXWVUTSRQPONMLKJIHGFEDCBA-_"

local function nirithy_b64_val(c)
  local p = string.find(nirithy_b64, c, 1, true)
  if p then return p - 1 end
  return -1
end

local function nirithy_decode(input)
  local input_len = #input
  if input_len % 4 ~= 0 then
    return nil, "Invalid base64 length"
  end

  local len = math.floor(input_len / 4 * 3)
  if input_len > 0 and string.sub(input, -1) == '=' then
    len = len - 1
  end
  if input_len > 1 and string.sub(input, -2, -1):sub(1,1) == '=' then
    len = len - 1
  end

  local out = {}
  local j = 1

  for i = 1, input_len, 4 do
    local a = string.sub(input, i, i)
    local b = string.sub(input, i+1, i+1)
    local c = string.sub(input, i+2, i+2)
    local d = string.sub(input, i+3, i+3)

    local va = a == '=' and 0 or nirithy_b64_val(a)
    local vb = b == '=' and 0 or nirithy_b64_val(b)
    local vc = c == '=' and 0 or nirithy_b64_val(c)
    local vd = d == '=' and 0 or nirithy_b64_val(d)

    if va < 0 or vb < 0 or vc < 0 or vd < 0 then
      return nil, "Invalid base64 character"
    end

    local triple = bit.lshift(va, 18) + bit.lshift(vb, 12) + bit.lshift(vc, 6) + vd

    if j <= len then
      out[j] = string.char(bit.rshift(triple, 16) and 0xFF)
      j = j + 1
    end
    if j <= len then
      out[j] = string.char(bit.rshift(triple, 8) and 0xFF)
      j = j + 1
    end
    if j <= len then
      out[j] = string.char(triple and 0xFF)
      j = j + 1
    end
  end

  return table.concat(out)
end

local function bytes_to_uint64_le(bytes)
  local result = 0
  for i = 8, 1, -1 do
    result = result * 256 + string.byte(bytes, i)
  end
  return result
end

local function unpack_nirithy_file(filepath)
  local file = io.open(filepath, 'rb')
  if not file then
    return nil, "Cannot open file: " .. filepath
  end

  local content = file:read('*a')
  file:close()

  if #content < 9 then
    return nil, "File too small"
  end

  local sig = string.sub(content, 1, 9)
  if sig ~= "Nirithy==" then
    return nil, "Not a Nirithy encrypted file"
  end

  local payload = string.sub(content, 10)
  print(string.format("[*] File size: %d bytes", #content))
  print(string.format("[*] Payload size: %d bytes", #payload))

  local bin, err = nirithy_decode(payload)
  if not bin then
    return nil, "Base64 decode failed: " .. (err or "unknown error")
  end

  print(string.format("[*] Decoded binary size: %d bytes", #bin))

  if #bin <= 24 then
    return nil, "Decoded data too small"
  end

  local timestamp_bytes = string.sub(bin, 1, 8)
  local iv = string.sub(bin, 9, 24)
  local encrypted_data = string.sub(bin, 25)

  local timestamp = bytes_to_uint64_le(timestamp_bytes)

  print(string.format("[+] Timestamp: %d", timestamp))
  print(string.format("[+] IV: %s", iv:gsub('.', function(c) return string.format('%02x', string.byte(c)) end)))
  print(string.format("[+] Encrypted data size: %d bytes", #encrypted_data))

  return {
    timestamp = timestamp,
    iv = iv,
    encrypted_data = encrypted_data,
    raw_binary = bin
  }
end

local function main()
  if #arg < 1 then
    print("Usage: lua nirithy_unpacker.lua <encrypted_file>")
    print("")
    print("Nirithy Shell Unpacker for LXCLUA-NCore")
    print("This tool extracts metadata from Nirithy encrypted Lua files.")
    print("")
    print("Note: Full decryption requires AES-128-CTR implementation")
    print("      which needs to be integrated with the LXCLUA runtime.")
    return 1
  end

  local filepath = arg[1]
  print(string.format("[*] Processing: %s", filepath))

  local result, err = unpack_nirithy_file(filepath)
  if not result then
    print(string.format("[-] Error: %s", err))
    return 1
  end

  print("")
  print("[+] Successfully parsed Nirithy shell!")
  print("")
  print("Metadata:")
  print(string.format("  Timestamp: %d (0x%016X)", result.timestamp, result.timestamp))
  print(string.format("  IV (hex): %s", result.iv:gsub('.', function(c) return string.format('%02x', string.byte(c)) end)))
  print(string.format("  Data length: %d bytes", #result.encrypted_data))

  local output_path = filepath .. ".unpacked.bin"
  local f = io.open(output_path, 'wb')
  if f then
    f:write(result.raw_binary)
    f:close()
    print(string.format("[*] Raw binary saved to: %s", output_path))
  end

  return 0
end

os.exit(main() or 0)
