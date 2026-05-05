local loader = require("loader")

if #arg < 1 then
    print("Usage: lua vm/main.lua <ir_file.lua>")
    os.exit(1)
end

local ir_func = loadfile(arg[1])
if not ir_func then
    print("Failed to load IR file")
    os.exit(1)
end

local ir_table = ir_func()
loader.load_and_run(ir_table)
