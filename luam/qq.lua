-- 来源 一只小柒夏

local bindClass = luajava.bindClass
local Bundle = bindClass "android.os.Bundle"
local Intent = bindClass "android.content.Intent"
local Activity = bindClass "android.app.Activity"
local Array = bindClass "java.lang.reflect.Array"
local cjson = require "cjson"

-----------------------------------------------------------
-- 错误码
-----------------------------------------------------------
local ErrorMessages = {
  [-1] = "登录失败或用户取消",
  [-2] = "TIM/QQ 未安装",
  [-3] = "JSON解析失败",
  [-4] = "无效参数: appeid不能为空"
}

-----------------------------------------------------------
-- 全局回调表
-----------------------------------------------------------
local qqLoginCallbacks = {}

-----------------------------------------------------------
-- Activity 结果处理
-----------------------------------------------------------
function onActivityResult(requestCode, resultCode, data)
  if requestCode == 1726 then
    local callback = qqLoginCallbacks[1726]
    qqLoginCallbacks[1726] = nil

    if not callback or type(callback) ~= "function" then
      return
    end

    if resultCode == Activity.RESULT_OK and data then
      local extras = data.getExtras()
      local response = extras and extras.getString("key_response")

      if response then
        local ok, result = pcall(cjson.decode, response)
        if not ok or not result then
          callback(-3, ErrorMessages[-3])
          return
        end

        local appeid = activity.getSharedData("appeid")
        local openid = result.openid

        if appeid and openid then
          callback(200, openid)
         else
          callback(-1, ErrorMessages[-1])
        end
       else
        callback(-1, ErrorMessages[-1])
      end
     else
      callback(-1, ErrorMessages[-1])
    end
  end
end

-----------------------------------------------------------
-- 构造登录 Intent
-----------------------------------------------------------
local function buildLoginIntent(appeid)
  local b1 = Bundle()
  b1.putString("format", "json")
  b1.putString("client_id", appeid)

  local b2 = Bundle()
  b2.putBundle("key_params", b1)
  b2.putString("appeid", appeid)
  b2.putString("key_request_code", "11101")
  b2.putString("key_action", "action_login")

  local intent = Intent()
  intent.putExtras(b2)
  return intent
end

-----------------------------------------------------------
-- 主登录接口
-----------------------------------------------------------
function Login(appeid, callback)
  if not (callback and type(callback) == "function") then
    return
  end

  appeid = tostring(appeid)
  if appeid == "" then
    callback(-4, ErrorMessages[-4])
    return
  end

  activity.setSharedData("appeid", appeid)

  local pm = activity.getPackageManager()
  local pkgs = {}

  -- 收集已安装的包
  if pcall(pm.getPackageInfo, "com.tencent.tim", 0) then
    table.insert(pkgs, "com.tencent.tim")
  end
  if pcall(pm.getPackageInfo, "com.tencent.mobileqq", 0) then
    table.insert(pkgs, "com.tencent.mobileqq")
  end

  if #pkgs == 0 then
    callback(-2, ErrorMessages[-2])
    return
  end

  -- 只有一个 App：直接启动
  if #pkgs == 1 then
    local intent = buildLoginIntent(appeid)
    intent.setClassName(pkgs[1], "com.tencent.open.agent.AgentActivity")
    qqLoginCallbacks[1726] = callback
    activity.startActivityForResult(intent, 1726)
    return
  end

  -- 多个 App：系统选择器
  local IntentClass = bindClass "android.content.Intent"
  local LabeledIntent = bindClass "android.content.pm.LabeledIntent"

  local chooserIntent = Intent.createChooser(Intent(), "请选择 QQ 或 TIM 进行登录")
  local extraIntents = Array.newInstance(IntentClass, #pkgs)

  for i, pkg in ipairs(pkgs) do
    local target = buildLoginIntent(appeid)
    target.setClassName(pkg, "com.tencent.open.agent.AgentActivity")

    -- 获取应用信息
    local appInfo = pm.getApplicationInfo(pkg, 0)
    local label = pm.getApplicationLabel(appInfo)
    local iconRes = appInfo.icon -- 获取图标资源 ID

    -- 使用正确的构造函数创建 LabeledIntent
    local li = LabeledIntent(target, pkg, label, iconRes)
    Array.set(extraIntents, i - 1, li)
  end

  chooserIntent.putExtra(Intent.EXTRA_INITIAL_INTENTS, extraIntents)
  qqLoginCallbacks[1726] = callback
  activity.startActivityForResult(chooserIntent, 1726)
end

-----------------------------------------------------------
-- 导出
-----------------------------------------------------------
return { Login = Login }