-- Test: strings and tables
local s = "Hello World"
print(string.len(s))
print(string.upper(s))
print(string.lower(s))
print(string.sub(s, 1, 5))
print(string.sub(s, 7))
print(string.rep("ab", 3))
print(string.find(s, "World"))
print(string.gsub(s, "o", "0"))
print(string.format("%d + %d = %d", 3, 4, 7))
print(string.format("%.2f", 3.14159))

-- string methods via colon
print(s:upper())
print(s:len())

-- table operations
local t = {10, 20, 30, 40, 50}
print(#t)
print(table.concat(t, "-"))
table.insert(t, 60)
print(t[#t])
table.remove(t, 1)
print(t[1])
print(#t)

-- table sort
local arr = {5, 3, 8, 1, 9, 2}
table.sort(arr)
print(table.concat(arr, ","))

-- hash table
local h = {name = "test", value = 42, active = true}
print(h.name, h.value, h.active)
h.new_key = "added"
print(h.new_key)
h.name = nil
print(h.name)

-- nested tables
local nested = {a = {b = {c = 42}}}
print(nested.a.b.c)

-- ipairs
local list = {"x", "y", "z"}
for i, v in ipairs(list) do
    print(i, v)
end
