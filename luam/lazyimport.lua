--[[
懒加载导入模块

功能说明：
  通过 Lua 元表（metatable）机制实现全局变量的懒加载导入。
  当程序访问一个未定义的全局变量时，该模块会自动尝试导入对应的模块，
  从而实现按需加载、延迟初始化的效果。

主要特性：
  1. 自动懒加载：首次访问时自动导入所需模块
  2. 别名支持：支持自定义导入后的调用名称
  3. 错误处理：完善的错误处理机制，避免重复失败尝试
  4. 模块重载：支持重新加载已缓存的模块
  5. 性能优化：缓存已加载模块，减少重复查找开销

使用示例：
  lazyimport("my.module")                    -- 自动使用模块路径的最后一部分作为调用名
  lazyimport("my.module", nil, "别名")        -- 自定义调用名为"别名"
  lazyimport("my.module", myEnv)             -- 指定导入时的环境表
  lazyimport.reload("my.module")             -- 重新加载指定模块
  lazyimport.clearCache()                    -- 清空所有缓存（谨慎使用）

注意事项：
  - 与其他全局变量元表设置可能存在冲突
  - 循环依赖可能导致导入失败，需要手动处理
  - 性能敏感场景建议显式导入而非完全依赖懒加载
]]

-- 模块配置存储表，用于存储所有懒加载模块的配置信息
local lazyimportCfg = {}

-- 将配置表安全地设置为全局变量，确保在其他地方可以访问
rawset(_G, "lazyimportCfg", lazyimportCfg)

-- 导入基础导入函数模块，为后续懒加载机制提供支持
require "import"

-- 获取全局导入函数，用于执行实际的模块导入操作
local _import = _G["import"]

-- 懒加载映射表，存储模块路径与调用名称的对应关系
local lazyImportMap = {}

-- 导入失败记录表，用于追踪导入失败的模块，防止重复尝试
local failedImportCache = {}

-- 已成功导入的模块缓存表，用于快速查找和避免重复导入
local loadedModuleCache = {}

-- 默认分隔符列表，用于从模块路径中提取默认调用名
local DEFAULT_SEPARATORS = {".", "$"}

-- 获取原始全局变量元表，用于在懒加载机制中保留原有的元表行为
local oldGlobalMetatable = getmetatable(_G)

--[[
创建懒加载元表

功能说明：
  创建并返回一个元表，用于拦截全局变量的访问。
  当访问的全局变量不存在时，会自动尝试从 lazyImportMap 中查找对应的模块配置，
  并执行导入操作。

返回值：
  table: 配置好的元表，可用于 setmetatable(_G, metatable)

局部变量优化：
  - 缓存常用函数引用，减少全局查找开销
  - 使用 rawget 提高访问性能
]]
local function createLazyImportMetatable()
  return {
    --[[
    __index 元方法

    功能说明：
      当访问全局变量且变量不存在时调用此方法。
      依次检查：原始元表查找 -> 懒加载配置 -> 返回原始值

    参数：
      self: 全局表自身（_G）
      key: 被访问的变量名

    返回值：
      any: 导入后的模块值，或原始查找结果，或 nil
    ]]
    __index = function(self, key)
      -- 第一步：尝试从原始全局元表中查找值
      if oldGlobalMetatable and oldGlobalMetatable.__index then
        local value = oldGlobalMetatable.__index(self, key)
        if value then
          return value
        end
      end

      -- 第二步：检查是否是已失败过的导入，避免重复尝试
      if failedImportCache[key] then
        return nil
      end

      -- 第三步：检查懒加载配置，尝试导入模块
      local cfg = lazyImportMap[key]
      if cfg then
        local package = cfg[1]
        local env = cfg[2]

        -- 捕获导入过程中的错误，防止程序崩溃
        local success, result = pcall(function()
          return _import(package, env)
        end)

        if success then
          -- 导入成功，添加到已加载缓存
          loadedModuleCache[key] = true
          return rawget(_G, key)
        else
          -- 导入失败，记录到失败缓存，避免重复尝试
          failedImportCache[key] = package
          return nil
        end
      end

      -- 最后：返回原始全局变量值（若存在）
      return rawget(_G, key)
    end
  }
end

-- 创建并应用懒加载元表
local metatable = createLazyImportMetatable()
setmetatable(_G, metatable)

--[[
注册懒加载模块

功能说明：
  将指定的模块注册为懒加载模式。当程序首次访问与该模块关联的全局变量时，
  系统会自动执行导入操作。

参数说明：
  package (string): 要导入的模块完整路径，如 "my.module.path"
  env (table|nil): 可选，导入时使用的环境表。如果为 nil，则使用默认环境
  callName (string|nil): 可选，导入后的调用名称。
                         如果未指定，则自动从 package 路径末尾提取
                         支持的分隔符： "." 和 "$"

返回值：
  无

使用示例：
  lazyimport("my.module")                     -- 访问 my 或 module 触发导入
  lazyimport("my.module", nil, "别名")        -- 访问 别名 触发导入
  lazyimport("my.module", myEnv)              -- 使用指定环境导入
]]
local function lazyimport(package, env, callName)
  -- 如果未指定调用名，则从模块路径自动提取
  -- 提取规则：从路径末尾开始查找分隔符，取其后的部分作为默认调用名
  callName = callName or package:match('([^%.$]+)$')

  -- 只有当调用名未被注册时才添加到映射表，避免覆盖已有配置
  if callName and not lazyImportMap[callName] then
    lazyImportMap[callName] = {package, env}
  end
end

--[[
重新加载指定模块

功能说明：
  强制重新加载已注册为懒加载的模块。先清除相关缓存，
  然后重新执行导入操作。此功能在模块代码更新后非常有用。

参数说明：
  moduleName (string): 要重新加载的模块调用名（即访问时使用的名称）

返回值：
  boolean: 重新加载是否成功
          - true: 重新加载成功
          - false: 模块未注册，无法重新加载

使用示例：
  lazyimport.reload("myModule")   -- 重新加载 myModule
]]
function lazyimport.reload(moduleName)
  -- 检查模块是否已注册
  if not lazyImportMap[moduleName] then
    return false
  end

  -- 清除相关缓存，允许重新尝试导入
  failedImportCache[moduleName] = nil
  loadedModuleCache[moduleName] = nil

  -- 获取模块配置
  local cfg = lazyImportMap[moduleName]
  local package = cfg[1]
  local env = cfg[2]

  -- 尝试重新导入
  local success, result = pcall(function()
    return _import(package, env)
  end)

  if success then
    loadedModuleCache[moduleName] = true
    return true
  else
    failedImportCache[moduleName] = package
    return false
  end
end

--[[
检查模块是否已加载

功能说明：
  查询指定模块是否已经成功加载到全局环境中。

参数说明：
  moduleName (string): 要检查的模块调用名

返回值：
  boolean: 模块加载状态
          - true: 模块已成功加载
          - false: 模块未加载或加载失败

使用示例：
  if lazyimport.isLoaded("myModule") then
    print("模块已加载")
  end
]]
function lazyimport.isLoaded(moduleName)
  return loadedModuleCache[moduleName] or false
end

--[[
获取模块注册信息

功能说明：
  查询指定模块的完整注册信息，包括模块路径和环境配置。

参数说明：
  moduleName (string): 要查询的模块调用名

返回值：
  table|nil: 模块注册信息，包含以下字段：
             - package: 模块完整路径
             - env: 导入时使用的环境表
             - loaded: 是否已加载
             - failed: 是否加载失败
           如果模块未注册则返回 nil

使用示例：
  local info = lazyimport.getInfo("myModule")
  if info then
    print("模块路径:", info.package)
    print("加载状态:", info.loaded)
  end
]]
function lazyimport.getInfo(moduleName)
  local cfg = lazyImportMap[moduleName]
  if not cfg then
    return nil
  end

  return {
    package = cfg[1],
    env = cfg[2],
    loaded = loadedModuleCache[moduleName] or false,
    failed = failedImportCache[moduleName] and true or false
  }
end

--[[
清空所有缓存

功能说明：
  清空所有导入相关的缓存，包括：
  - 失败导入记录（允许重新尝试失败的导入）
  - 已加载模块记录（不会重新触发导入，但影响状态查询）

谨慎使用此功能，通常仅在需要完全重置导入状态时调用。

使用示例：
  lazyimport.clearCache()   -- 清空所有缓存
]]
function lazyimport.clearCache()
  -- 清空失败导入缓存，允许重新尝试
  for k in pairs(failedImportCache) do
    failedImportCache[k] = nil
  end

  -- 清空已加载模块缓存
  for k in pairs(loadedModuleCache) do
    loadedModuleCache[k] = nil
  end
end

-- 返回主函数，供外部使用
return lazyimport