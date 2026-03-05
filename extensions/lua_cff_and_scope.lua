local lexer = require("lexer")

local code = [[
local function test()
    local a = 1
    do
        ::my_label::
        a = a + 1
        if a < 5 then
            goto my_label
        end
    end
end
]]

local tokens = lexer.lex(code)

print("--- Validating Gotos in Lua ---")
for i, t in ipairs(tokens) do
    if t.type == "'goto'" then
        -- Find the goto target label name
        local target_label = tokens[i+1].value
        print("Found goto at index " .. i .. " targeting '" .. target_label .. "'")

        -- Use our new primitive to find the label
        local label_idx = lexer.find_label(tokens, target_label)
        if not label_idx then
            error("Label '" .. target_label .. "' not found!")
        end
        print("Found label '" .. target_label .. "' at index " .. label_idx)

        -- Use our new primitive to check scopes
        local goto_start, goto_end = lexer.get_block_bounds(tokens, i)
        local label_start, label_end = lexer.get_block_bounds(tokens, label_idx)

        print(string.format("Goto scope: [%d, %d]", goto_start, goto_end))
        print(string.format("Label scope: [%d, %d]", label_start, label_end))

        -- A goto is valid if it is in the SAME block or an INNER block relative to the label.
        -- It is INVALID if it jumps into an INNER block from the outside.
        -- Thus, the label's block must completely encompass the goto's block.
        if goto_start >= label_start and goto_end <= label_end then
            print("=> GOTO IS VALID (jumping within the same or to an outer scope)")
        else
            error("=> INVALID GOTO: jumping into a local nested scope is not allowed in Lua!")
        end
        print("-------------------------------")
    end
end
print("Validation complete.")
