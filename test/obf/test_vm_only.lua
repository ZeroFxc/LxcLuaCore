package.path = "./obf/?.lua;" .. package.path
local config = {
    CFF = false,
    BLOCK_SHUFFLE = false,
    BOGUS_BLOCKS = false,
    STATE_ENCODE = false,
    NESTED_DISPATCHER = false,
    OPAQUE_PREDICATES = false,
    FUNC_INTERLEAVE = false,
    VM_PROTECT = true
}

config=require"obfuscate_config".calculateFlag(config)
print("混淆标志: " .. config)
local input_file = "test_cff.lua"
local output_file = "test_cff_vm_only.lua"
require"compile_cff"(input_file, output_file, config)
