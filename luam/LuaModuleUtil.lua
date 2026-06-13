local _M = {}
local bindClass = luajava.bindClass
local LuaErrorHandler = luajava.bindClass("com.difierline.lua.lxclua.utils.exceptions.LuaErrorHandler")
local LuaModuleManager = luajava.bindClass("com.difierline.lua.lxclua.utils.LuaModuleManager")

local function getErrorHandler()
  return LuaErrorHandler.getInstance()
end

local function getModule(name)
  return LuaModuleManager.get(name)
end

--- 获取异步任务管理器
-- @return table: AsyncTaskManager 实例
function _M.AsyncTaskManager()
  return getModule("AsyncTaskManager") or LuaModuleManager.getOrCreate("AsyncTaskManager", function()
    local asm = luajava.bindClass("com.difierline.lua.lxclua.utils.AsyncTaskManager")
    return asm.getInstance()
  end)
end

--- 获取计时器
-- @return table: Timer 实例
function _M.Timer()
  return getModule("Timer") or LuaModuleManager.getOrCreate("Timer", function()
    local timer = luajava.bindClass("com.difierline.lua.lxclua.utils.Timer")
    return timer.getInstance()
  end)
end

--- 获取文件工具
-- @return table: FileUtil 实例
function _M.FileUtil()
  return getModule("FileUtil") or LuaModuleManager.getOrCreate("FileUtil", function()
    local fu = luajava.bindClass("com.difierline.lua.lxclua.utils.FileUtil")
    return fu.getInstance()
  end)
end

--- 获取图片工具
-- @return table: ImageUtil 实例
function _M.ImageUtil()
  return getModule("ImageUtil") or LuaModuleManager.getOrCreate("ImageUtil", function()
    local iu = luajava.bindClass("com.difierline.lua.lxclua.utils.ImageUtil")
    return iu.getInstance()
  end)
end

--- 获取日期工具
-- @return table: DateUtil 实例
function _M.DateUtil()
  return getModule("DateUtil") or LuaModuleManager.getOrCreate("DateUtil", function()
    local du = luajava.bindClass("com.difierline.lua.lxclua.utils.DateUtil")
    return du.getInstance()
  end)
end

--- 获取字符串工具
-- @return table: StringUtil 实例
function _M.StringUtil()
  return getModule("StringUtil") or LuaModuleManager.getOrCreate("StringUtil", function()
    local su = luajava.bindClass("com.difierline.lua.lxclua.utils.StringUtil")
    return su.getInstance()
  end)
end

--- 获取颜色工具
-- @return table: ColorUtil 实例
function _M.ColorUtil()
  return getModule("ColorUtil") or LuaModuleManager.getOrCreate("ColorUtil", function()
    local cu = luajava.bindClass("com.difierline.lua.lxclua.utils.ColorUtil")
    return cu.getInstance()
  end)
end

--- 获取事件总线
-- @return table: EventBus 实例
function _M.EventBus()
  return getModule("EventBus") or LuaModuleManager.getOrCreate("EventBus", function()
    local eb = luajava.bindClass("com.difierline.lua.lxclua.utils.EventBus")
    return eb.getInstance()
  end)
end

--- 获取缓存管理器
-- @return table: CacheManager 实例
function _M.CacheManager()
  return getModule("CacheManager") or LuaModuleManager.getOrCreate("CacheManager", function()
    local cm = luajava.bindClass("com.difierline.lua.lxclua.utils.CacheManager")
    return cm.getInstance()
  end)
end

--- 获取 SharedPreferences 封装
-- @param name string: 偏好设置名称
-- @return table: Prefs 实例
function _M.Prefs(name)
  local prefsClass = luajava.bindClass("com.difierline.lua.lxclua.utils.Prefs")
  return prefsClass.getInstance(activity)
end

--- 视图工具
-- @return table: ViewUtils 工具对象
function _M.ViewUtils()
  local vu = luajava.bindClass("com.difierline.lua.lxclua.utils.ViewUtils")
  return vu
end

--- 日志工具
-- @return table: Logger 工具对象
function _M.Logger()
  local logger = luajava.bindClass("com.difierline.lua.lxclua.utils.Logger")
  return logger
end

--- 错误处理
-- @return table: LuaErrorHandler 实例
function _M.ErrorHandler()
  return getErrorHandler()
end

--- 处理 Lua 错误
-- @param errorMessage string: 错误信息
-- @param stackTrace string: 堆栈跟踪
-- @param context string: 上下文信息
-- @return table: ErrorDetail 对象
function _M.handleLuaError(errorMessage, stackTrace, context)
  return getErrorHandler():handleLuaError(errorMessage, stackTrace, context)
end

--- 处理布局加载错误
-- @param layoutPath string: 布局路径
-- @param error any: 错误对象
-- @param details string: 详细信息
-- @return table: ErrorDetail 对象
function _M.handleLayoutError(layoutPath, error, details)
  return getErrorHandler():handleLayoutLoadError(layoutPath, error, details)
end

--- 处理视图创建错误
-- @param viewClass string: 视图类名
-- @param error any: 错误对象
-- @param layoutInfo string: 布局信息
-- @return table: ErrorDetail 对象
function _M.handleViewCreateError(viewClass, error, layoutInfo)
  return getErrorHandler():handleViewCreateError(viewClass, error, layoutInfo)
end

--- 处理属性错误
-- @param viewClass string: 视图类名
-- @param attribute string: 属性名
-- @param value string: 属性值
-- @param error any: 错误对象
-- @return table: ErrorDetail 对象
function _M.handleAttributeError(viewClass, attribute, value, error)
  return getErrorHandler():handleAttributeError(viewClass, attribute, value, error)
end

--- 格式化错误显示
-- @param errorDetail table: ErrorDetail 对象
-- @return string: 格式化的错误字符串
function _M.formatError(errorDetail)
  return getErrorHandler():formatErrorForDisplay(errorDetail)
end

--- 获取错误统计
-- @return table: 错误统计信息
function _M.getErrorStats()
  local stats = getErrorHandler():getErrorStatistics()
  local result = {}
  for k, v in pairs(stats) do
    result[k] = v
  end
  return result
end

--- 获取所有错误历史
-- @return table: 错误历史列表
function _M.getErrorHistory()
  local history = getErrorHandler():getErrorHistory()
  local result = {}
  for i, e in ipairs(history) do
    result[i] = {
      type = tostring(e.type),
      message = e.message,
      fileName = e.fileName,
      lineNumber = e.lineNumber,
      methodName = e.methodName,
      timestamp = e.timestamp
    }
  end
  return result
end

--- 清除错误历史
function _M.clearErrorHistory()
  getErrorHandler():clearHistory()
end

--- 注册错误监听器
-- @param callback function: 回调函数
function _M.onError(callback)
  local listener = luajava.newInstance("com.difierline.lua.lxclua.utils.exceptions.LuaErrorHandler$ErrorListener", {
    onError = function(error)
      callback({
        type = tostring(error.type),
        message = error.message,
        fileName = error.fileName,
        lineNumber = error.lineNumber,
        methodName = error.methodName,
        contextInfo = error.contextInfo
      })
    end
  })
  getErrorHandler():addErrorListener(listener)
end

return _M
