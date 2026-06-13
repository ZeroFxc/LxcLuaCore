local luajava = luajava
local Reflect = {}

function Reflect.construct(object)
  if not luajava.instanceof(object, Class)
    object = object.class
  end
  return setmetatable({cls = object},{
    __call = function(self,...)
      local argCount = select("#", ...)
      local classes = {}
      for i = 1, argCount do
        local arg = select(i, ...)
        classes[#classes+1] = arg.class
      end
      local ret = self.cls.getDeclaredConstructor(classes)
      ret.accessible = true
      return ret.newInstance{...}
    end
  })
end

function Reflect.of(obj)
  local ret = { 
    obj,
    
    __index = function(self, k)
      local obj = self[1]
      local clazz = obj.class
      local ret, method = pcall(clazz.getDeclaredMethod, k)
      if ret then
        return method.setAccessible(true)
       else
        local _, field = pcall(clazz.getDeclaredField, k)
        return field.setAccessible(true).get(obj)
      end 
      error("NoSuchMethodOrFieldException: "..k, 0)
    end,
    
    __newindex = function(self, k, v)
      local obj = self[1]
      local ret, field = pcall(obj.class.getDeclaredField, k)
      when ret return field.setAccessible(true).set(obj, v)
      error("NoSuchFieldException: "..k, 0)
    end
  }
  return setmetatable(ret, ret)
end

return Reflect