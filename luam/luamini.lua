-- luamini.lua: Parse ini files

local luamini = {}

function luamini.parse(file)
   local ret = {}

   local section = nil
   local pattern = {
      comment = "^%s*[#;]",
      section = "^%[([^%]]+)%]$",
      key_val = "^%s*([^#;%s][%w_%-]*)%s*=%s*(.*)"
   }

   local ln = 0
   for line in file:lines() do
      ln = ln + 1

      if line == "" then
          continue
      end

      -- lines starting with comments
      if line:match(pattern.comment) then
          continue
      end

      local key,val = line:match(pattern.key_val)
      if key and val then
         if not section then
            ret[key] = val
             continue
         end

         ret[section][key] = val
          continue
      end

      local s = line:match(pattern.section)
      if s then
         section = s
         ret[s] = ret[s] or {}
          continue
      end

      -- Lua expects a return to be EOF but we have goto continue.
      if true then return nil,"Syntax error at line " .. ln end


   end

   return ret
end

return luamini
