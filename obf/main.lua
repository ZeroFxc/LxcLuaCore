local config = {
    CFF = true,           -- 控制流扁平化
    BLOCK_SHUFFLE = true, -- 基本块随机打乱顺序
    BOGUS_BLOCKS = true,  -- 插入虚假基本块
    STATE_ENCODE = false,  -- 状态值编码混淆
    NESTED_DISPATCHER = true, -- 嵌套分发器（多层状态机）
    OPAQUE_PREDICATES = false, -- 不透明谓词（恒真/恒假条件）
    FUNC_INTERLEAVE = true, -- 函数交织（虚假函数路径）
    VM_PROTECT = false     -- VM保护（自定义虚拟机指令集）
}

config=require"obfuscate_config".calculateFlag(config)
print(config)
local input_file = "/storage/emulated/0/XCLUA/project/OfxfxcEnc/x"
local output_file = input_file..".lua"
require"compile_cff"(input_file,output_file,config)