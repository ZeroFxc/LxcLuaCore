-- Generate a Nirithy enveloped shell via string.dump (Lua-side producer)
local f = function(a, b)
  local x = a * 2
  return x + b, "hello"
end
local s = string.dump(f, { strip = true })
local fh = assert(io.open("build/strdump.shell", "wb"))
fh:write(s)
fh:close()
print("shell bytes:", #s)
