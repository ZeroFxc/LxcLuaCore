-- 18-12-2025 @author Swarg
-- Lua-Object-Notation
-- interface:
-- - encode (or serialize)   - lua-table to string
-- - decode (or deserialize) - string to lua-table

local M = {}

M.VERSION = 'lon v0.2.0'
M._URL = 'https://gitlab.com/lua_rocks/lon'

--------------------------------------------------------------------------------

local E = {}

---@param s string
local function str2code(s)
  local has_new_lines = s:find("\n", 1, true) ~= nil
  local has_squote = s:find('\'', 1, true)
  local has_dquote = s:find('"', 1, true)

  if not has_new_lines then
    if has_squote and not has_dquote then
      s = '"' .. s .. '"'
    elseif not has_squote and has_dquote then
      s = "'" .. s .. "'"
    else -- both
      local f = s:sub(1, 1)
      if f == "'" then
        s = '"' .. s:gsub('"', '\\"') .. '"'
      else
        s = "'" .. s:gsub("'", "\\'") .. "'"
      end
    end
    --
  elseif not has_dquote then
    s = '"' .. s:gsub("\n", '\\n') .. '"'
  else
    s = '"' .. s:gsub('"', '\\"'):gsub("\n", '\\n') .. '"'
  end
  return s
end

---@param t table
---@return table
---@param entry_value any?
local function list_to_map(t, entry_value)
  local m = {}
  assert(type(t) == 'table', 't')
  for i, v in ipairs(t) do
    m[v] = entry_value ~= nil and entry_value or i
  end
  return m
end

--
-- Apply desired order to sorted string keys
--
-- skeys: {'a', 'b', 'c'}  desired order: {'b', 'a'} --> { 'b', 'a', 'c' },
--
---@param skeys table
---@param desired_order table
local function apply_order(skeys, desired_order)
  local new_order = {}
  local passed = {}
  local map = list_to_map(skeys, true)

  for _, k in ipairs(desired_order) do
    if map[k] then
      new_order[#new_order + 1] = k
      passed[k] = true
    end
  end

  for _, k in ipairs(skeys) do
    if not passed[k] then
      new_order[#new_order + 1] = k
    end
  end

  return new_order
end


---@param t table
local function split_to_keys_and_indexes(t, nosort, opts)
  local nums, strs1, strs2, another = {}, {}, {}, {}
  local ignore_key_map = (opts or E).ignore_keys or E

  for k, v in pairs(t) do
    if not ignore_key_map[k] then
      local kt = type(k)
      if kt == 'number' then
        nums[#nums + 1] = k
      elseif kt == 'string' then
        if type(v) == 'table' then
          strs2[#strs2 + 1] = k
        else
          strs1[#strs1 + 1] = k
        end
      else
        another[#another + 1] = k
      end
    end
  end
  local strs = {}
  if not nosort then
    -- table.sort(nums)
    table.sort(strs1)
    table.sort(strs2)
  end
  for _, k in ipairs(strs1) do strs[#strs + 1] = k end
  for _, k in ipairs(strs2) do strs[#strs + 1] = k end

  return nums, strs, another
end


-- serialize lua table to string
---@param obj table
---@param tab string
---@param eq string
---@param nl string
---@param opts table?{order, ignore_keys}
---@return string
local function serialize0(obj, tab, eq, nl, opts, passed)
  if type(obj) ~= 'table' then error('expected table t got:' .. type(obj)) end
  local order = (opts or E).order

  passed = passed or {}
  if passed[obj] then return 'nil' end -- key = nil -- e.g. for parent links
  passed[obj] = true

  local function v2s(v, tab0)
    if type(v) == 'string' then
      return str2code(v) -- "'" .. line .. "'"
      --
    elseif type(v) == 'number' or type(v) == 'boolean' then
      return tostring(v)
      --
    elseif type(v) == 'table' then
      if tab0 ~= nil and tab0 ~= '' then tab0 = tab0 .. '  ' end
      return serialize0(v, tab0, eq, nl, opts, passed)
    else
      return str2code(type(v))
    end
  end

  local t = { '{' }

  local function add(...)
    for i = 1, select('#', ...) do t[#t + 1] = select(i, ...) end
  end

  local indexes, str_keys, another = split_to_keys_and_indexes(obj, false, opts)
  if order and next(order) then
    str_keys = apply_order(str_keys, order)
  end

  -- indexes of table as arraylist
  -- local is_map = #str_keys > 0
  local has_gaps, previ = false, 0
  for _, i in ipairs(indexes) do
    local v = obj[i]
    if has_gaps or i - 1 ~= previ then
      has_gaps = true -- if any gap switch to map mode -- all next keys with []
      add(nl, tab, '[' .. tostring(i) .. ']', eq, v2s(v, tab), ',')
    else
      add(nl, tab, v2s(v, tab), ',')
    end
    previ = i
  end

  -- keys of table as map
  for _, key in ipairs(str_keys) do
    local v = obj[key]
    if (key:match('[^%w_]+') or key:match('^%d')) then
      key = "['" .. key .. "']" -- todo key with '
    end
    add(nl, tab, key, eq, v2s(v, tab), ',')
  end

  -- not an number or string keys
  for _, key in ipairs(another) do
    local v = obj[key]
    key = "[" .. v2s(key) .. "]"
    add(nl, tab, key, eq, v2s(v, tab), ',')
  end

  if t[#t] == ',' then t[#t] = nil end --  remove ','

  add(nl)
  if #tab > 0 then add(tab:sub(1, -3)) end
  add('}')

  return table.concat(t)
end


---@param obj table
---@param opts table?{tab, new_line, eq, order}
function M.serialize(obj, opts)
  if type(obj) ~= 'table' then error('expected table t got:' .. type(obj)) end
  opts = opts or E
  local tab = opts.tab or ''
  assert(type(tab) == 'string', 'tab')
  local nl = opts.new_line or (tab == '' and ' ' or "\n")
  local eq = opts.eq or ' = '
  if opts.minify then
    tab, nl, eq = '', '', '='
  elseif opts.pretty then
    tab, nl, eq = '  ', "\n", ' = '
  else
    if eq == '=' and tab == '' then nl = '' end -- minify-mode
  end
  return serialize0(obj, tab, eq, nl, opts, opts.ignore or {})
end

-------------------------------------------------------------------------------
--                          Deserializing

-- Create a safe environment (sandbox)
local function create_sandbox()
  local b = {} -- sandbox

  -- embed safe functions from standard libraries
  b.math, b.string, b.table = math, string, table

  -- But WE LIMIT dangerous functions:
  -- sandbox.io = nil        -- No access to files
  -- sandbox.os = nil        -- No system commands
  -- sandbox.package = nil   -- No modules loading
  -- sandbox.debug = nil     -- No debugging

  -- Add secure functions if needed
  b.type = type
  b.pairs = pairs
  b.ipairs = ipairs
  b.tonumber = tonumber
  b.tostring = tostring
  -- If necessary, but it is better to replace it with a logger
  b.print = print

  -- Set up a metatable to deny access to global variables
  local meta = {
    ---@diagnostic disable-next-line: unused-local
    __index = function(t, k)
      -- Deny access to global variables
      error(string.format("access to global '%s' is not allowed", k), 2)
    end,
    ---@diagnostic disable-next-line: unused-local
    __newindex = function(t, k, v)
      error(string.format("creating global '%s' is not allowed", k), 2)
    end
  }
  setmetatable(b, meta)

  return b
end

-- aka load_lua_table
---@param s string
function M.deserialize(s)
  if type(s) ~= 'string' or s == '' then return nil end

  -- Add 'return' if it starts with a table
  if s:sub(1, 1) == '{' or string.match(s, '^%s+{') then
    s = 'return ' .. s
  end

  local sandbox = create_sandbox()

  -- Loading code with a limited environment
  -- In Lua 5.1 use loadstring with an explicit environment
  local chunk, err = loadstring(s)
  if not chunk then
    error("加载失败: " .. (err or "未知错误"))
  end

  -- Set the environment for the function
  setfenv(chunk, sandbox)

  local success, result = pcall(chunk)
  if not success then
    error("执行失败: " .. result)
  end

  return result
end

-- aliases:

M.encode = M.serialize
M.decode = M.deserialize

--------------------------------------------------------------------------------

if _G.TEST then
  M.test = {
    str2code = str2code,
    apply_order = apply_order,
    list_to_map = list_to_map,
    split_to_keys_and_indexes = split_to_keys_and_indexes,
  }
end

return M
