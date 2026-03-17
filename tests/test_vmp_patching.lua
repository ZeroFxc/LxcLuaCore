local patch = require("patch")

-- Get the memory address and size of the LVM marker
local addr, size = patch.get_marker("lvm")
print(string.format("LVM Marker found at: %s, Size: %d bytes", tostring(addr), size))

-- Write NOP instructions over the marker dynamically
-- NOP in x86/x64 is 0x90 (\144)
patch.write(addr, string.rep("\144", size))

print("Successfully applied dynamic memory patch! Running VM loop...")

local s = 0
for i = 1, 100 do
    s = s + i
end

print("VM Execution successful. Result:", s)
