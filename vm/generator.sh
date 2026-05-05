#!/bin/bash

if [ "$#" -ne 2 ]; then
    echo "Usage: $0 <input.lua> <output_standalone.lua>"
    exit 1
fi

INPUT=$1
OUTPUT=$2
TEMP_IR=$(mktemp)

# Compile translator if needed
cd translator && make > /dev/null 2>&1
cd ..

# Translate source code to IR
./translator/translator "$INPUT" "$TEMP_IR"
if [ $? -ne 0 ]; then
    echo "Translation failed"
    rm "$TEMP_IR"
    exit 1
fi

# Bundle the pure Lua VM and the generated IR
cat << 'EOF' > "$OUTPUT"
-- --- VIRTUAL MACHINE BUNDLE ---

local function load_module(name, func)
    package.preload[name] = func
end

load_module("state", function()
EOF

cat vm/state.lua >> "$OUTPUT"

cat << 'EOF' >> "$OUTPUT"
end)

load_module("opcodes", function()
EOF

cat vm/opcodes.lua >> "$OUTPUT"

cat << 'EOF' >> "$OUTPUT"
end)

load_module("core", function()
EOF

cat vm/core.lua >> "$OUTPUT"

cat << 'EOF' >> "$OUTPUT"
end)

load_module("loader", function()
EOF

cat vm/loader.lua >> "$OUTPUT"

cat << 'EOF' >> "$OUTPUT"
end)

-- --- END VIRTUAL MACHINE BUNDLE ---

local ir_table =
EOF

cat "$TEMP_IR" >> "$OUTPUT"

cat << 'EOF' >> "$OUTPUT"

local loader = require("loader")
return loader.load_and_run(ir_table)
EOF

rm "$TEMP_IR"

echo "Standalone bundled VM created at $OUTPUT"
chmod +x "$OUTPUT"
