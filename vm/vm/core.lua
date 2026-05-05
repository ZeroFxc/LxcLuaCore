local opcodes = require("opcodes")

local function run(state)
    while #state.call_frames > 0 do
        local frame = state:current_frame()
        state.executing_call = false
        state.executing_return = false

        while frame.pc <= #frame.func.code do
            local inst = frame.func.code[frame.pc]
            local pc = frame.pc
            frame.pc = frame.pc + 1

            local handler = opcodes[inst.op]
            if not handler then
                error("Unknown opcode: " .. tostring(inst.op))
            end

            local ok, err = pcall(handler, frame, inst)
            if not ok then
                error(string.format("pc=%d op=%s\n%s", pc, inst.op, tostring(err)))
            end

            if state.executing_call or state.executing_return then
                break
            end
        end

        if state.executing_return then
            state:pop_frame()
            if #state.call_frames == 0 then
                return unpack(frame.return_values or {})
            end

            local caller = state:current_frame()

            if caller.pending_meta_cmp then
                local result = (frame.return_values or {})[1]
                local cmp_a = caller.pending_cmp_a
                caller.pending_meta_cmp = nil
                caller.pending_cmp_a = nil
                if (not not result) ~= (cmp_a ~= 0) then
                    caller.pc = caller.pc + 1
                end
            else
                local a = caller.base + caller.last_call_a
                local expected_results = caller.last_call_nresults

                if expected_results == -1 then
                    expected_results = frame.nresults or 0
                    state.top = a + expected_results - 1
                end

                for i = 1, expected_results do
                    state.stack[a + i - 1] = (frame.return_values or {})[i]
                end

                if caller.meta_result_a then
                    local val = (frame.return_values or {})[1]
                    state.stack[caller.base + caller.meta_result_a] = val
                    caller.meta_result_a = nil
                end
            end
        elseif not state.executing_call then
            state:pop_frame()
        end
    end
end

return { run = run }
