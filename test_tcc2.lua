local tinycc = require('tinycc')
local state = tinycc.new()
state:set_output_type(tinycc.OUTPUT_MEMORY)
state:compile_string([[
    int add(int a, int b) {
        return a + b;
    }
]])
state:relocate()
print("State:", state)
print("State pointer:", state:get_symbol("add"))
