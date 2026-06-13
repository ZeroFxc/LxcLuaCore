package.path = "./obf/?.lua;" .. package.path
local config = {
    CFF = true,
    BLOCK_SHUFFLE = true,
    BOGUS_BLOCKS = false,
    STATE_ENCODE = true,
    NESTED_DISPATCHER = false,
    OPAQUE_PREDICATES = true,
    FUNC_INTERLEAVE = false,
    VM_PROTECT = false
}

config=require"obfuscate_config".calculateFlag(config)
print("混淆标志: " .. config)
local input_file = "test_cff.lua"
local output_file = "test_cff_out.lua"
require"compile_cff"(input_file, output_file, config)
