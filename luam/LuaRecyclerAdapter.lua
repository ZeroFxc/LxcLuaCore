local bindClass = luajava.bindClass
local LuaCustRecyclerHolder = bindClass "com.lua.custrecycleradapter.LuaCustRecyclerHolder"
local LuaCustRecyclerAdapter = bindClass "com.lua.custrecycleradapter.LuaCustRecyclerAdapter"
local AdapterCreator = bindClass "com.lua.custrecycleradapter.AdapterCreator"
local loadlayout = require "loadlayout"

function LuaRecyclerAdapter(data, list_item, method)
  return LuaCustRecyclerAdapter(AdapterCreator({
    getItemCount = function()
      return #data
    end
    ,
    getItemViewType = function(position)
      return 0
    end
    ,
    onCreateViewHolder = function(parent, viewType)
      local views = {}
      holder = LuaCustRecyclerHolder(loadlayout(list_item, views))
      holder.view.setTag(views)
      if method.setViews then
        method.setViews(views, viewType)
      end
      return holder
    end
    ,
    onBindViewHolder = function(holder, position)
      if method.onBindViewHolder then
        method.onBindViewHolder(holder, position, holder.view.getTag(), data[position+1])
      end
    end
  }))
end

return LuaRecyclerAdapter