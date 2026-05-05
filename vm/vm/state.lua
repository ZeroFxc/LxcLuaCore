local State = {}
State.__index = State

function State.new(global_env)
    local self = setmetatable({}, State)
    self.stack = {}
    self.top = 0
    self.call_frames = {}
    self.globals = global_env or _G
    self.open_upvalues = {}
    return self
end

function State:push_frame(func, base, nresults)
    local frame = {
        state = self,
        func = func,
        pc = 1,
        base = base,
        nresults = nresults
    }
    table.insert(self.call_frames, frame)
    return frame
end

function State:pop_frame()
    return table.remove(self.call_frames)
end

function State:current_frame()
    return self.call_frames[#self.call_frames]
end

return State
