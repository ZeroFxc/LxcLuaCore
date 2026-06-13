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
local input_file = "test_simple.lua"
local output_file = "test_simple_vm.lua"
require"compile_cff"(input_file, output_file, config)
