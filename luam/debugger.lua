local OrientationListener = luajava.bindClass "android.view.OrientationListener"
local currentOrientation = activity.resources.configuration.orientation
local oldOrientation = currentOrientation
luajava.override(OrientationListener, {
  onOrientationChanged = function(_, orientation)
    currentOrientation = activity.resources.configuration.orientation

    if currentOrientation ~= oldOrientation then
      activity.recreate()
    end

    oldOrientation = currentOrientation
  end
}).enable()

local File = luajava.bindClass("java.io.File")
if File(tostring(activity.getLuaDir("manifest.json"))).isFile() then
  app = dofile(activity.getLuaDir("manifest.json"))
 else
  app = { application = { debugmode = false } }
end
return function(status)
  if app.application.debugmode == true or status then
    local bindClass = luajava.bindClass
    local newInstance = luajava.newInstance

    local _print = print
    local isOpenPanel = false

    --导入类
    local System = bindClass("java.lang.System")
    local SimpleDateFormat = bindClass("android.icu.text.SimpleDateFormat")
    local ListView = bindClass("android.widget.ListView")
    local LuaAdapter = bindClass("com.difierline.lua.LuaAdapter")
    local TypedValue = bindClass("android.util.TypedValue")
    local View = bindClass("android.view.View")
    local Build = bindClass("android.os.Build")
    local Context = bindClass("android.content.Context")
    local Gravity = bindClass("android.view.Gravity")
    local WindowManager = bindClass("android.view.WindowManager")
    local PixelFormat = bindClass("android.graphics.PixelFormat")
    local Toast = bindClass("android.widget.Toast")
    local LinearLayoutCompat = bindClass("androidx.appcompat.widget.LinearLayoutCompat")
    local ViewPager = bindClass("androidx.viewpager.widget.ViewPager")
    local MaterialCardView = bindClass("com.google.android.material.card.MaterialCardView")
    local MaterialTextView = bindClass("com.google.android.material.textview.MaterialTextView")
    local TextInputEditText = bindClass("com.google.android.material.textfield.TextInputEditText")
    local MaterialButton = bindClass("com.google.android.material.button.MaterialButton")
    local TabLayout = bindClass("com.google.android.material.tabs.TabLayout")
    local SpannableStringBuilder = bindClass("android.text.SpannableStringBuilder")
    local ForegroundColorSpan = bindClass("android.text.style.ForegroundColorSpan")
    local Spannable = bindClass("android.text.Spannable")
    local ColorStateList = bindClass("android.content.res.ColorStateList")
    local Glide = bindClass("com.bumptech.glide.Glide")
    local CodeEditor = bindClass("io.github.rosemoe.sora.widget.CodeEditor")
    local EditorColorScheme = bindClass("io.github.rosemoe.sora.widget.schemes.EditorColorScheme")
    local AlertDialog = bindClass("androidx.appcompat.app.AlertDialog")
    local MaterialAlertDialogBuilder = bindClass("com.google.android.material.dialog.MaterialAlertDialogBuilder")
    local RecyclerView = bindClass("androidx.recyclerview.widget.RecyclerView")
    local LinearLayoutManager = bindClass("androidx.recyclerview.widget.LinearLayoutManager")
    local Colors = require("Colors")

    --解析函数值
    local function parseFunctionValue(input, env)
      if type(input) ~= "string" then
        input = tostring(input)
      end
      input = input:gsub("^%s*(.-)%s*$", "%1")
      if input == "" then
        return nil, "输入为空"
      end
      -- 方式1: 函数名 "funcName" 或 "module.funcName"
      if input:match("^[a-zA-Z_][a-zA-Z0-9_.]*$") then
        local funcValue = env
        for part in input:gmatch("[^%.]+") do
          funcValue = funcValue and funcValue[part]
        end
        if type(funcValue) == "function" then
          return funcValue
        else
          return nil, "函数名 '" .. input .. "' 不存在或不是函数"
        end
      end
      -- 方式2: 代码块 "return ..." 或 "function() ... end" 或直接语句
      local chunk, err = load(input, "func", "t", env)
      if chunk then
        local success, result = pcall(chunk)
        if success then
          if type(result) == "function" then
            return result
          else
            return nil, "代码必须返回一个函数"
          end
        else
          return nil, "代码执行错误: " .. tostring(result)
        end
      else
        return nil, "代码语法错误: " .. tostring(err)
      end
    end

    --引入波纹
    local circleRippleRes = TypedValue()
    activity.getTheme().resolveAttribute(android.R.attr.selectableItemBackgroundBorderless, circleRippleRes, true)

    --引入颜色
    local colorPrimary = Colors.colorPrimary
    local colorBackground = Colors.colorBackground
    local colorPrimaryInverse = colorBackground
    local colorSurfaceVariant = Colors.colorSurfaceVariant
    local colorSurfaceContainer = Colors.colorSurfaceContainer
    local colorOnSurfaceVariant = Colors.colorOnSurfaceVariant

    local idss = {}

    --悬浮球
    local vmWindow = {
      MaterialCardView,
      CardElevation = "4dp",
      CardBackgroundColor = colorPrimary,
      id = "vmLinearLayout1",
      {
        MaterialTextView,
        text = "控制台",
        gravity = "center",
        textColor = colorBackground,
        layout_marginTop = "6dp",
        layout_marginBottom = "6dp",
        layout_margin = "13dp",
        id = "title",
      },
    }

    --悬浮窗口
    local variablesParams_x = activity.getWidth() / 1.4
    local variablesParams_y = activity.getHeight() / 5
    local variablesManager = activity.getSystemService(Context.WINDOW_SERVICE)
    local variablesParams = WindowManager.LayoutParams()
    local vmWindowALY = loadlayout(vmWindow, idss)
    variablesParams.format = PixelFormat.RGBA_8888
    variablesParams.flags = WindowManager.LayoutParams().FLAG_NOT_FOCUSABLE
    variablesParams.gravity = Gravity.LEFT | Gravity.TOP
    variablesParams.x = variablesParams_x
    variablesParams.y = variablesParams_y
    variablesParams.width = WindowManager.LayoutParams.WRAP_CONTENT
    variablesParams.height = WindowManager.LayoutParams.WRAP_CONTENT

    if ({
        pcall(function()
          variablesManager.addView(vmWindowALY, variablesParams)
        end)
      })[1] == false then
      Toast.makeText(activity, "调试面板启动失败", Toast.LENGTH_SHORT).show()
     else
      --读取日志
      local function readlog(s)
        local p = io.popen("logcat -d -v long " .. s)
        local s = p:read("*a")
        p:close()
        s = s:gsub("%-+ beginning of[^\n]*\n", "")
        if #s == 0 then
          s = "<运行应用程序查看日志输出>"
        end
        return s
      end

      --清除日志
      local function clearlog()
        local p = io.popen("logcat -c")
        local s = p:read("*a")
        p:close()
        return s
      end

      task(clearlog)

      --检查日志
      local function tasklog(s)
        _print((s:gsub('^%[.+%]\n', '')))
        safe_error((s:gsub('^%[.+%]\n', '')))
        idss.title.setText("出错")
        idss.vmLinearLayout1.setCardBackgroundColor(0xffAA3437)
      end

      thread(function(activity)
        local Thread = luajava.bindClass("java.lang.Thread")
        local function readlog(s)
          local p = io.popen("logcat -d -v long " .. s)
          local s = p:read("*a")
          p:close()
          s = s:gsub("%-+ beginning of[^\n]*\n", "")
          if #s == 0 then
            s = "<运行应用程序查看日志输出>"
          end
          return s
        end
        while (true) do
          local s = readlog('lua:* *:S')
          if s:find('Runtime%s-error') or s:find('错误') then
            call('tasklog', s)
            break
          end
          if activity.isFinishing() or activity.isDestroyed() then
            break
          end
          Thread.sleep(1000)
        end
      end, activity)

      local ids = {}

      --控制面板
      local vmWindow_2 = {
        LinearLayoutCompat,
        layout_width = "fill",
        layout_height = "fill",
        orientation = "vertical",
        id = "vmLinearLayout2",
        gravity = "bottom",
        backgroundColor = "0x80000000",
        {
          LinearLayoutCompat,
          orientation = "vertical",
          backgroundColor = colorBackground,
          layout_width = "fill",
          layout_height = "65%h",
          onClick = function()
            return true
          end,
          {
            TabLayout,
            layout_width = "fill",
            TabMode = 2,
            backgroundColor = colorSurfaceContainer,
            id = "mtab",
          },
          {
            View,
            layout_height = "1dp",
            backgroundColor = colorSurfaceVariant,
          },
          {
            ViewPager,
            id = "cvpgx",
            layout_weight = 1,
            layout_width = "fill",
            layout_height = "fill",
            pagesWithTitle = {
              { --View
                {
                  ListView,
                  layout_width = "fill",
                  id = "vmPrintListView",
                  fastScrollEnabled = true,
                  layout_height = "fill"
                },
                {
                  LinearLayoutCompat,
                  orientation = "vertical",
                  layout_width = "fill",
                  layout_height = "fill",
                  {
                    LinearLayoutCompat,
                    orientation = "vertical",
                    layout_width = "fill",
                    {
                      LinearLayoutCompat,
                      focusable = true,
                      layout_width = "fill",
                      focusableInTouchMode = true,
                      {
                        MaterialTextView,
                        padding = "5dp",
                        paddingRight = "0",
                        textColor = colorOnSurfaceVariant,
                        textSize = "13dp",
                        text = "当前节点: /",
                        id = "vmVariableListView_path",
                        paddingLeft = "6dp",
                      },
                      {
                        TextInputEditText,
                        padding = "5dp",
                        paddingLeft = "0",
                        textColor = colorOnSurfaceVariant,
                        textSize = "13dp",
                        backgroundColor = "0",
                        singleLine = "true",
                        paddingRight = "6dp",
                        id = "vmVariableListView_search",
                        layout_width = "fill",
                        imeOptions = "actionSearch",
                        hint = "搜索变量...",
                      },
                    },
                    {
                      View,
                      backgroundColor = colorSurfaceVariant,
                      layout_height = "1dp",
                    },
                  },
                  {
                    ListView,
                    fastScrollEnabled = true,
                    id = "vmVariableListView",
                    layout_width = "fill",
                    layout_height = "fill",
                  },
                },
                {
                  LinearLayoutCompat,
                  orientation = "vertical",
                  layout_width = "fill",
                  layout_height = "fill",
                  {
                    LinearLayoutCompat,
                    orientation = "vertical",
                    layout_width = "fill",
                    {
                      LinearLayoutCompat,
                      focusable = true,
                      layout_width = "fill",
                      focusableInTouchMode = true,
                      {
                        MaterialTextView,
                        padding = "5dp",
                        paddingRight = "0",
                        textColor = colorOnSurfaceVariant,
                        textSize = "13dp",
                        text = "搜索日志",
                        paddingLeft = "6dp",
                      },
                      {
                        TextInputEditText,
                        padding = "5dp",
                        paddingLeft = "0",
                        textColor = colorOnSurfaceVariant,
                        textSize = "13dp",
                        backgroundColor = "0",
                        singleLine = "true",
                        paddingRight = "6dp",
                        id = "vmLogcatSearch",
                        layout_width = "fill",
                        layout_marginStart = "10dp",
                        imeOptions = "actionSearch",
                        hint = "输入搜索内容...",
                      },
                    },
                    {
                      View,
                      backgroundColor = colorSurfaceVariant,
                      layout_height = "1dp",
                    },
                  },
                  {
                    MaterialCardView,
                    layout_width = "fill",
                    radius = "0dp",
                    StrokeWidth = "0dp",
                    {
                      LinearLayoutCompat,
                      layout_height = "40dp",
                      id = "logcat_bar",
                      gravity = "center",
                      layout_width = "fill",
                      {
                        MaterialTextView,
                        textSize = "13dp",
                        text = "A",
                        layout_width = "12.5%w",
                        gravity = "center",
                        layout_weight = "1",
                      },
                      {
                        View,
                        layout_width = "1dp",
                        layout_marginTop = "6dp",
                        layout_marginBottom = "6dp",
                        backgroundColor = colorSurfaceVariant,
                      },
                      {
                        MaterialTextView,
                        textSize = "13dp",
                        text = "L",
                        layout_width = "12.5%w",
                        gravity = "center",
                        layout_weight = "1",
                      },
                      {
                        View,
                        layout_width = "1dp",
                        layout_marginTop = "6dp",
                        layout_marginBottom = "6dp",
                        backgroundColor = colorSurfaceVariant,
                      },
                      {
                        MaterialTextView,
                        textSize = "13dp",
                        text = "T",
                        layout_width = "12.5%w",
                        gravity = "center",
                        layout_weight = "1",
                      },
                      {
                        View,
                        layout_width = "1dp",
                        layout_marginTop = "6dp",
                        layout_marginBottom = "6dp",
                        backgroundColor = colorSurfaceVariant,
                      },
                      {
                        MaterialTextView,
                        textSize = "13dp",
                        text = "E",
                        layout_width = "12.5%w",
                        gravity = "center",
                        layout_weight = "1",
                      },
                      {
                        View,
                        layout_width = "1dp",
                        layout_marginTop = "6dp",
                        layout_marginBottom = "6dp",
                        backgroundColor = colorSurfaceVariant,
                      },
                      {
                        MaterialTextView,
                        textSize = "13dp",
                        text = "W",
                        layout_width = "12.5%w",
                        gravity = "center",
                        layout_weight = "1",
                      },
                      {
                        View,
                        layout_width = "1dp",
                        layout_marginTop = "6dp",
                        layout_marginBottom = "6dp",
                        backgroundColor = colorSurfaceVariant,
                      },
                      {
                        MaterialTextView,
                        textSize = "13dp",
                        text = "I",
                        layout_width = "12.5%w",
                        gravity = "center",
                        layout_weight = "1",
                      },
                      {
                        View,
                        layout_width = "1dp",
                        layout_marginTop = "6dp",
                        layout_marginBottom = "6dp",
                        backgroundColor = colorSurfaceVariant,
                      },
                      {
                        MaterialTextView,
                        textSize = "13dp",
                        text = "D",
                        layout_width = "12.5%w",
                        gravity = "center",
                        layout_weight = "1",
                      },
                      {
                        View,
                        layout_width = "1dp",
                        layout_marginTop = "6dp",
                        layout_marginBottom = "6dp",
                        backgroundColor = colorSurfaceVariant,
                      },
                      {
                        MaterialTextView,
                        textSize = "13dp",
                        text = "V",
                        layout_width = "12.5%w",
                        gravity = "center",
                        layout_weight = "1",
                      },
                    },
                  },
                  {
                    View,
                    layout_height = "1dp",
                    backgroundColor = colorSurfaceVariant,
                  },
                  {
                    ListView,
                    overScrollMode = "2",
                    fastScrollEnabled = true,
                    layout_width = "fill",
                    id = "vmLogcatListView",
                    layout_height = "fill",
                  },
                },
                {
                  CodeEditor,
                  id = "vmText",
                  layout_width = "fill",
                },
                {
                  LinearLayoutCompat,
                  orientation = "vertical",
                  layout_width = "fill",
                  layout_height = "fill",
                  {
                    LinearLayoutCompat,
                    orientation = "horizontal",
                    layout_width = "fill",
                    layout_height = "wrap_content",
                    padding = "8dp",
                    {
                      TextInputEditText,
                      layout_weight = 1,
                      layout_height = "45dp",
                      hint = "输入要执行的Lua代码...",
                      textSize = "13dp",
                      id = "vmDebugInput",
                      imeOptions = "actionSend",
                      maxLines = 3,
                    },
                    {
                      MaterialButton,
                      text = "运行",
                      layout_height = "45dp",
                      layout_marginLeft = "8dp",
                      textSize = "13dp",
                      id = "vmDebugRunBtn",
                      onClick = function()
                        local code = ids.vmDebugInput.Text
                        if code and #code > 0 then
                          local func, err = loadstring(code)
                          if func then
                            local result = { pcall(func) }
                            if result[1] then
                              print("=> " .. tostring(result[2]))
                            else
                              safe_error(tostring(result[2]))
                            end
                          else
                            safe_error("语法错误: " .. tostring(err))
                          end
                          ids.vmDebugInput.setText("")
                          ids.cvpgx.setCurrentItem(0, false)
                        end
                      end,
                    },
                  },
                },
                {
                  LinearLayoutCompat,
                  orientation = "vertical",
                  layout_width = "fill",
                  {
                    MaterialTextView,
                    text = "内存占用: ",
                    layout_width = "fill",
                    textColor = colorPrimary,
                    textSize = "13sp",
                    layout_margin = "16dp",
                    layout_marginBottom = "0dp",
                    gravity = "left|center",
                    id = "vmMemory",
                  },
                  {
                    LinearLayoutCompat,
                    layout_width = "fill",
                    layout_margin = "16dp",
                    layout_marginBottom = "-6dp",
                    {
                      MaterialButton,
                      textSize = "13sp",
                      layout_weight = "1",
                      style = material.attr.materialButtonOutlinedStyle,
                      layout_marginRight = "8dp",
                      StrokeColor = ColorStateList.valueOf(colorSurfaceVariant),
                      text = "关闭当前界面",
                      onClick = function()
                        activity.finish()
                      end,
                    },
                    {
                      MaterialButton,
                      textSize = "13sp",
                      layout_weight = "1",
                      layout_marginLeft = "8dp",
                      style = material.attr.materialButtonOutlinedStyle,
                      StrokeColor = ColorStateList.valueOf(colorSurfaceVariant),
                      text = "重构当前界面",
                      onClick = function()
                        activity.recreate()
                      end,
                    },
                  },
                  {
                    LinearLayoutCompat,
                    layout_width = "fill",
                    layout_margin = "16dp",
                    layout_marginBottom = "-6dp",
                    {
                      MaterialButton,
                      textSize = "13sp",
                      layout_weight = "1",
                      layout_marginRight = "8dp",
                      style = material.attr.materialButtonOutlinedStyle,
                      StrokeColor = ColorStateList.valueOf(colorSurfaceVariant),
                      text = "重启当前界面",
                      onClick = function()
                        activity.finish()
                        this.startActivity(this.getIntent())
                      end,
                    },
                    {
                      MaterialButton,
                      textSize = "13sp",
                      layout_weight = "1",
                      layout_marginLeft = "8dp",
                      text = "结束当前进程",
                      StrokeColor = ColorStateList.valueOf(colorSurfaceVariant),
                      style = material.attr.materialButtonOutlinedStyle,
                      onClick = function()
                        os.exit()
                      end,
                    },
                  },
                  {
                    LinearLayoutCompat,
                    layout_width = "fill",
                    layout_margin = "16dp",
                    {
                      MaterialButton,
                      textSize = "13sp",
                      layout_weight = "1",
                      layout_marginRight = "8dp",
                      style = material.attr.materialButtonOutlinedStyle,
                      StrokeColor = ColorStateList.valueOf(colorSurfaceVariant),
                      text = "清理Glide缓存",
                      onClick = function()
                        Glide.get(activity).clearMemory()
                        thread(function(Glide, activity)
                          Glide.get(activity).clearDiskCache()
                        end, Glide, activity)
                      end,
                    },
                  },
                },
              },
              { --Title
                "打印",
                "变量",
                "日志",
                "文本",
                "调试",
                "其他"
              },
            },
          },
          {
            MaterialCardView,
            layout_width = "fill",
            radius = "0dp",
            StrokeWidth = "0dp",
            {
              View,
              layout_height = "1dp",
              backgroundColor = colorSurfaceVariant,
            },
            {
              LinearLayoutCompat,
              id = "bar",
              backgroundColor = colorSurfaceContainer,
              layout_height = "45dp",
              layout_width = "fill",
            },
          },
        },
      }
      local vmWindowALY_2 = loadlayout(vmWindow_2, ids)

      local scheme = EditorColorScheme()
      scheme.setColor(EditorColorScheme.IDENTIFIER_VAR, 0xFFFF9800)
      scheme.setColor(EditorColorScheme.LINE_NUMBER_BACKGROUND, Colors.colorBackground)
      scheme.setColor(EditorColorScheme.LINE_NUMBER, Colors.colorOnBackground)
      scheme.setColor(EditorColorScheme.WHOLE_BACKGROUND, Colors.colorBackground)
      scheme.setColor(EditorColorScheme.TEXT_NORMAL, Colors.colorOnBackground)
      scheme.setColor(EditorColorScheme.LINE_NUMBER_CURRENT, Colors.colorOnBackground)
      scheme.setColor(EditorColorScheme.CURRENT_LINE, 512264328)
      scheme.setColor(EditorColorScheme.BLOCK_LINE_CURRENT, Colors.colorOutline)
      scheme.setColor(EditorColorScheme.SELECTION_INSERT, Colors.colorPrimary)
      scheme.setColor(EditorColorScheme.SELECTION_HANDLE, 0xFFDCC2AE)
      scheme.setColor(EditorColorScheme.HIGHLIGHTED_DELIMITERS_FOREGROUND, -168430091)
      scheme.setColor(EditorColorScheme.LINE_NUMBER_PANEL, Colors.colorPrimary)
      scheme.setColor(EditorColorScheme.LINE_NUMBER_PANEL_TEXT, 0xFFFFFFFF)
      scheme.setColor(EditorColorScheme.TEXT_ACTION_WINDOW_BACKGROUND, Colors.colorBackground)
      scheme.setColor(EditorColorScheme.TEXT_ACTION_WINDOW_ICON_COLOR, Colors.colorOnBackground)
      scheme.setColor(EditorColorScheme.TEXT_ACTION_WINDOW_STROKE_COLOR, Colors.colorOutline)
      scheme.setColor(EditorColorScheme.COMPLETION_WND_TEXT_SECONDARY, Colors.colorOutline)
      scheme.setColor(EditorColorScheme.COMPLETION_WND_TEXT_PRIMARY, Colors.colorOnBackground)
      scheme.setColor(EditorColorScheme.HIGHLIGHTED_DELIMITERS_FOREGROUND, Colors.colorOnBackground)
      scheme.setColor(EditorColorScheme.MATCHED_TEXT_BACKGROUND, 0)
      scheme.setColor(EditorColorScheme.LINE_DIVIDER, colorSurfaceContainer)
      ids.vmText.setColorScheme(scheme)
      .setTabWidth(2)
      .setTextSizePx(40)

      ids.mtab.setupWithViewPager(ids.cvpgx)

      local variablesParams_firstX, variablesParams_firstY, variablesParams_wmX, variablesParams_wmY
      
      function idss.vmLinearLayout1.OnTouchListener(v, event)
        if event.getAction() == 0 then
          variablesParams_firstX = event.getRawX()
          variablesParams_firstY = event.getRawY()
          variablesParams_wmX = variablesParams.x
          variablesParams_wmY = variablesParams.y
         elseif event.getAction() == 2 then
          local newX = variablesParams_wmX + (event.getRawX() - variablesParams_firstX)
          local newY = variablesParams_wmY + (event.getRawY() - variablesParams_firstY)
          variablesParams.x = math.max(0, math.min(activity.getWidth() - 100, newX))
          variablesParams.y = math.max(0, math.min(activity.getHeight() - 100, newY))
          variablesParams_x = variablesParams.x
          variablesParams_y = variablesParams.y
          if variablesManager and vmWindowALY then
            pcall(function()
              variablesManager.updateViewLayout(vmWindowALY, variablesParams)
            end)
          end
          return false
        end
      end

      --关闭调试面板
      local function changeVMWindow()
        if not variablesManager or not vmWindowALY or not vmWindowALY_2 then
          return
        end
        local success, err = pcall(function()
          if isOpenPanel == false then
            isOpenPanel = true
            variablesManager.removeView(vmWindowALY)
            variablesParams.flags = WindowManager.LayoutParams().FLAG_LAYOUT_IN_SCREEN | WindowManager.LayoutParams().FLAG_NOT_TOUCH_MODAL
            variablesParams.x = activity.getWidth()
            variablesParams.y = activity.getHeight()
            variablesParams.width = WindowManager.LayoutParams.MATCH_PARENT
            variablesParams.height = WindowManager.LayoutParams.MATCH_PARENT
            variablesManager.addView(vmWindowALY_2, variablesParams)
           else
            isOpenPanel = false
            variablesManager.removeView(vmWindowALY_2)
            variablesParams.flags = WindowManager.LayoutParams().FLAG_NOT_FOCUSABLE
            variablesParams.x = variablesParams_x
            variablesParams.y = variablesParams_y
            variablesParams.width = WindowManager.LayoutParams.WRAP_CONTENT
            variablesParams.height = WindowManager.LayoutParams.WRAP_CONTENT
            variablesManager.addView(vmWindowALY, variablesParams)
          end
        end)
        if not success then
          Toast.makeText(activity, "切换面板失败: " .. tostring(err), Toast.LENGTH_SHORT).show()
        end
      end

      function idss.vmLinearLayout1.onClick()
        changeVMWindow()
        return true
      end

      function ids.vmLinearLayout2.onClick()
        changeVMWindow()
        return true
      end

      local vmItem = {
        LinearLayoutCompat,
        orientation = "vertical",
        layout_width = "fill",
        {
          MaterialTextView,
          id = "text",
          gravity = "center|left",
          textSize = "13sp",
          layout_width = "fill",
          MaxLines = 5,
          layout_margin = "8dp",
          ellipsize = "end"
        },
      }

      local vmPrintListView_data = {}
      vmPrintListView_adp = LuaAdapter(activity, vmPrintListView_data, vmItem)
      ids.vmPrintListView.setAdapter(vmPrintListView_adp)

      --添加打印日志
      local function log(color, iss, ...)
        local str = {}
        local arg = { ... }
        local date = SimpleDateFormat("HH:mm:ss.SSS:  ")

        for k, v in ipairs(arg) do
          str[#str + 1] = tostring(v)
          if k ~= #arg then
            str[#str + 1] = '   '
          end
        end

        local s = table.concat(str)

        if iss then
          s = date.format(System.currentTimeMillis()) .. s
        end

        table.insert(vmPrintListView_data, {
          text = {
            Text = s,
            textColor = color
          }
        })

        return ...
      end

      --打印默认日志
      local function vmPrintListView_default()
        local pkg = this.getPackageManager().getPackageInfo(this.getPackageName(), 0)
        log(colorPrimary, nil,
        string.format('系统: %s, Android %s (SDK%s), %s - %s',
        Build.MODEL, Build.VERSION.RELEASE, Build.VERSION.SDK, pkg.applicationInfo.loadLabel(this.getPackageManager())
        , pkg.versionName))
        log(colorPrimary, nil, '文件: ' .. this.getLuaPath())
      end

      function print(...)
        return log(colorOnSurfaceVariant, 1, ...)
      end

      function safe_error(...)
        return log(0xFFE90000, 1, ...)
      end

      function explain(...)
        return log(0xFF808080, 1, ...)
      end

      function info(...)
        return log(0xFF00A000, 1, ...)
      end

      function warning(...)
        return log(0xFFE97E00, 1, ...)
      end

      local _err = error
      function error(a, b)
        _print(a)
        _err(log(0xFFE90000, 1, a), b)
      end

      local _ass = assert
      function assert(a, ...)
        if not a then
          _print(...)
          log(0xFFE90000, 1, ...)
        end
        return _ass(a, ...)
      end

      local MyOnError = function(err, content)
        if content then
          safe_error(err .. ":\n" .. tostring(content))
         else
          safe_error(err)
        end
        idss.title.setText("Erroring")
        idss.vmLinearLayout1.setCardBackgroundColor(0xffAA3437)
      end

      local old_onError = onError
      onError = old_onError and function(...)
        MyOnError(...)
        old_onError()
      end or MyOnError

      function ids.vmPrintListView.onItemClick(parent, view, position, id)
        ids.vmText.setText(tostring(view.Tag.text.Text))
        ids.cvpgx.setCurrentItem(3)
        return true
      end

      --variable
      local variable_path = {}
      local flags = Spannable.SPAN_INCLUSIVE_INCLUSIVE
      local variable_kv = {}
      local variable_span = {}
      local variable_node = {}
      local variable_data = {}
      local variable_adp = LuaAdapter(activity, variable_data,
      {
        MaterialTextView,
        layout_width = "fill",
        textSize = "13dp",
        padding = "5dp",
        paddingLeft = "6dp",
        paddingRight = "16dp",
        singleLine = "true",
        ellipsize = "end",
        id = "kv",
      })

      ids.vmVariableListView.setAdapter(variable_adp)

      local function add(tab, str)
        variable_data[#variable_data + 1] = {
          kv = {
            text = str,
            textColor = 0xff808080
          }
        }
        variable_kv[#variable_data] = tab
      end

      local color_span1 = ForegroundColorSpan(0xFF00a000)
      local color_span2 = ForegroundColorSpan(colorPrimary)
      local color_span3 = ForegroundColorSpan(0xFF7F00FF)
      local color_span4 = ForegroundColorSpan(0xFF00A000)
      local color_span5 = ForegroundColorSpan(0xFFE97E00)

      --开始添加variable
      local function tree()
        local tab = _ENV

        table.clear(variable_path)
        table.clear(variable_data)

        for k, v in ipairs(variable_node) do
          if type(tab[v]) == 'table' then
            tab = tab[v]
           else
            variable_node[k] = nil
          end
        end

        local t = {}
        for k, v in ipairs(variable_node) do
          t[k] = tostring(v)
        end

        local s = table.concat(t, '/')

        if #s > 0 then
          s = s .. "/"
        end

        ids.vmVariableListView_path.setText("当前节点: /" .. s)

        table.clear(variable_data)
        if #variable_node ~= 0 then
          add(1, "返回父节点")
        end

        add(2, "序列化节点")

        for k, v in pairs(tab) do
          local _k, _v = k, v

          v = tostring(v)
          if v then
            if utf8.len(v) > 80 then
              v = utf8.sub(v, 1, 80) .. '...'
            end

            local keyStr
            if type(k) ~= 'string' then
              keyStr = string.format('[%s]', tostring(k))
            else
              keyStr = k
            end

            if type(_v) == 'string' then
              v = string.format('"%s"', v)
             elseif type(_v) == 'table' then
              v = string.format('%s => {...}', v)
            end

            local s = string.format('%s => %s', keyStr, tostring(v))

            local span
            if variable_span[#variable_data + 1] then
              span = variable_span[#variable_data + 1]
             else
              span = SpannableStringBuilder()
            end

            span.clearSpans()
            span.clear()
            span.append(s)

            local s1, e1 = utf8.find(s, '=>')

            span.setSpan(color_span2, 0, utf8.len(keyStr), flags)
            span.setSpan(color_span1, s1 - 1, e1, flags)

            if type(_v) == 'table' then
              if s:find('table') then
                local s0, e0 = utf8.find(s, 'table:%s-0x%x+%s')
                span.setSpan(color_span3, s0 - 1, e0, flags)
                local s1, e1 = utf8.find(s, '=>', e0)
                span.setSpan(color_span4, s1 - 1, e1, flags)
                local s2, e2 = utf8.find(s, '{%.%.%.}', e1)
                span.setSpan(color_span5, s2 - 1, e2, flags)
                variable_path[tostring(_k)] = _k
              end
            end
            add({ _k, _v }, span)
          end
        end

        variable_adp.notifyDataSetChanged()
        ids.vmVariableListView.setStackFromBottom(true)
        ids.vmVariableListView.setStackFromBottom(false)
      end

      --列表点击
      ids.vmVariableListView.onItemClick = function(a, b, c, d)
        local t = variable_kv[d]
        if t == 1 then
          variable_node[#variable_node] = nil
          tree()
         elseif t == 2 then
          local tab = _ENV
          for k, v in ipairs(variable_node) do
            tab = tab[v]
          end
          ids.vmText.setText(dump(tab))
          ids.cvpgx.setCurrentItem(3, false)
         else
          if type(t[2]) == "table" then
            variable_node[#variable_node + 1] = t[1]
            tree()
           else
            ids.vmText.setText(tostring(t[2]))
            ids.cvpgx.setCurrentItem(3, false)
          end
        end
      end

      --列表长按
      ids.vmVariableListView.onItemLongClick = function(a, b, c, d)
        local t = variable_kv[d]
        if t and type(t[1]) ~= 'number' and type(t[2]) ~= 'table' then
          local key = t[1]
          local currentValue = t[2]
          local valueType = type(currentValue)
          local displayValue = tostring(currentValue)
          
          local editView = TextInputEditText(activity)
          editView.setText(displayValue)
          
          local dialog = MaterialAlertDialogBuilder(activity)
            .setTitle("修改变量: " .. tostring(key) .. " (" .. valueType .. ")")
            .setView(editView)
            .setPositiveButton("确定", nil)
            .setNegativeButton("取消", nil)
            .create()
          
          dialog.setOnShowListener({
            onShow = function()
              dialog.getButton(AlertDialog.BUTTON_POSITIVE).setOnClickListener({
                onClick = function()
                  local newValueStr = editView.Text
                  local newValue = newValueStr
                  
                  if valueType == "number" then
                    newValue = tonumber(newValueStr) or newValueStr
                  elseif valueType == "boolean" then
                    newValue = newValueStr == "true" or newValueStr == "True" or newValueStr == "1"
                  elseif valueType == "function" then
                    local funcValue, err = parseFunctionValue(newValueStr, _ENV)
                    if funcValue then
                      newValue = funcValue
                    else
                      Toast.makeText(activity, "函数格式错误: " .. tostring(err), Toast.LENGTH_SHORT).show()
                      return
                    end
                  elseif valueType == "userdata" or valueType == "thread" then
                    Toast.makeText(activity, "不支持修改此类型: " .. valueType, Toast.LENGTH_SHORT).show()
                    return
                  end
                  
                  if #variable_node == 0 then
                    _ENV[key] = newValue
                  else
                    local parentTab = _ENV
                    for i, nodeName in ipairs(variable_node) do
                      parentTab = parentTab[nodeName]
                    end
                    parentTab[key] = newValue
                  end
                  tree()
                  dialog.dismiss()
                end
              })
            end
          })
          
          dialog.show()
         elseif t and type(t[2]) == 'table' then
          local targetTable = t[2]
          
          local listViewLayout = {
            LinearLayoutCompat,
            orientation = "vertical",
            layout_width = "match",
            layout_height = "300dp",
            {
              ListView,
              id = "tableListView",
              layout_width = "match",
              layout_height = "match",
              dividerHeight = "1dp",
            },
          }
          
          local listViewIds = {}
          local listViewWrapper = loadlayout(listViewLayout, listViewIds)
          local listView = listViewIds.tableListView
          
          local treeData = {}
          local visitedTables = {}
          
          local function isSameTable(a, b)
            if a == b then return true end
            return tostring(a) == tostring(b)
          end
          
          local function isCircular(tab, parentTables)
            if parentTables then
              for _, parent in ipairs(parentTables) do
                if isSameTable(tab, parent) then
                  return true
                end
              end
            end
            return false
          end
          
          local function buildTableTree(tab, prefix, level, parentTables)
            if level > 50 then return {} end
            local items = {}
            
            xpcall(function()
              for k, v in pairs(tab) do
                local keyStr = type(k) == 'string' and k or '[' .. tostring(k) .. ']'
                local fullKey = prefix .. (prefix ~= '' and '.' or '') .. keyStr
                
                local item = {
                  key = k,
                  value = v,
                  keyStr = keyStr,
                  fullKey = fullKey,
                  level = level,
                  isTable = type(v) == 'table',
                  opening = false,
                  children = nil,
                  isCircular = false
                }
                
                if type(v) == 'table' then
                  local newParentTables = parentTables or {}
                  table.insert(newParentTables, tab)
                  
                  if isCircular(v, newParentTables) then
                    item.isCircular = true
                    item.value = "*循环引用*"
                  else
                    item.children = buildTableTree(v, fullKey, level + 1, newParentTables)
                  end
                end
                table.insert(items, item)
              end
            end, function(e)
              return {}
            end)
            
            table.sort(items, function(a, b)
              if a.isTable ~= b.isTable then
                return a.isTable
              end
              return a.keyStr < b.keyStr
            end)
            return items
          end
          
          treeData = buildTableTree(targetTable, tostring(t[1]), 0, nil)
          
          local function flattenTree()
            local result = {}
            local function flatten(items, level)
              for _, item in ipairs(items) do
                table.insert(result, item)
                if item.opening and item.children then
                  flatten(item.children, level + 1)
                end
              end
            end
            flatten(treeData, 0)
            return result
          end
          
          local flatData = flattenTree()
          
          local TypedValue = luajava.bindClass("android.util.TypedValue")
          local function dp2px(dp)
            return TypedValue.applyDimension(TypedValue.COMPLEX_UNIT_DIP, dp, activity.getResources().getDisplayMetrics())
          end
          
          local itemLayout = {
            LinearLayoutCompat,
            orientation = "horizontal",
            layout_width = "match",
            layout_height = "wrap",
            gravity = "center|left",
            padding = "8dp",
            {
              MaterialTextView,
              id = "tvKey",
              textSize = "13dp",
              layout_width = "wrap",
              layout_height = "wrap",
              textColor = colorPrimary,
            },
          }
          
          local function getDisplayText(item)
            local display = string.rep("  ", item.level) .. item.keyStr
            if item.isCircular then
              display = display .. " (*循环引用*)"
            elseif item.isTable then
              display = display .. " {...}"
            end
            return display
          end
          
          local function buildAdapterData()
            local data = {}
            for i, item in ipairs(flatData) do
              data[i] = {
                tvKey = {
                  text = getDisplayText(item),
                  textColor = item.isTable and colorPrimary or colorOnSurfaceVariant
                }
              }
            end
            return data
          end
          
          local adapterData = buildAdapterData()
          local adp = LuaAdapter(activity, adapterData, itemLayout)
          listView.setAdapter(adp)
          
          listView.onItemClick = function(parent, view, position, id)
            local item = flatData[position + 1]
            if item and item.isTable and not item.isCircular then
              item.opening = not item.opening
              flatData = flattenTree()
              adapterData = buildAdapterData()
              adp.notifyDataSetChanged()
            end
          end
          
          listView.onItemLongClick = function(parent, view, position, id)
            local item = flatData[position + 1]
            if item and not item.isTable then
              local currentVal = item.value
              local valType = type(currentVal)
              local displayVal = tostring(currentVal)
              local editView = TextInputEditText(activity)
              editView.setText(displayVal)
              
              local dialog = MaterialAlertDialogBuilder(activity)
                .setTitle("修改变量: " .. item.keyStr .. " (" .. valType .. ")")
                .setView(editView)
                .setPositiveButton("确定", nil)
                .setNegativeButton("取消", nil)
                .create()
              
              dialog.setOnShowListener(luajava.createProxy("android.content.DialogInterface$OnShowListener", {
                onShow = function(d)
                  dialog.getButton(AlertDialog.BUTTON_POSITIVE).setOnClickListener({
                    onClick = function()
                      local newValStr = editView.Text
                      local newVal = newValStr
                      
                      if valType == "number" then
                        newVal = tonumber(newValStr) or newValStr
                      elseif valType == "boolean" then
                        newVal = newValStr == "true" or newValStr == "True" or newValStr == "1"
                      elseif valType == "function" then
                        local funcValue, err = parseFunctionValue(newValStr, targetTable)
                        if funcValue then
                          newVal = funcValue
                        else
                          Toast.makeText(activity, "函数格式错误: " .. tostring(err), Toast.LENGTH_SHORT).show()
                          return
                        end
                      elseif valType == "userdata" or valType == "thread" then
                        Toast.makeText(activity, "不支持修改此类型: " .. valType, Toast.LENGTH_SHORT).show()
                        return
                      end
                      
                      targetTable[item.key] = newVal
                      local newTree = buildTableTree(targetTable, tostring(t[1]), 0, nil)
                      treeData = newTree
                      flatData = flattenTree()
                      adapterData = buildAdapterData()
                      adp.notifyDataSetChanged()
                      dialog.dismiss()
                    end
                  })
                end
              }))
              
              dialog.show()
            end
            return true
          end
          
          MaterialAlertDialogBuilder(activity)
            .setTitle("查看表: " .. tostring(t[1]))
            .setView(listViewWrapper)
            .setPositiveButton("关闭", nil)
            .show()
        else
          ids.vmText.setText(string.format('%s/%s', table.concat(variable_node, '/'), b.text:match('(.+)%s=>')))
          ids.cvpgx.setCurrentItem(3, false)
        end
        return true
      end

      --回车搜索
      ids.vmVariableListView_search.onEditorAction = function(v, actionId, event)
        if actionId == 0 then
          local searchText = v.text
          if #searchText > 0 then
            local target_node = variable_path[searchText]
            if target_node then
              variable_node[#variable_node + 1] = target_node
              v.setText('')
              tree()
            end
          end
        end
        return false
      end

      --搜索变量
      local function searchVariables(keywor)
        keywor = tostring(keywor)
        keywor = string.lower(keywor)
        local tab = _ENV
        for k, v in ipairs(variable_node) do
          if type(tab[v]) == 'table' then
            tab = tab[v]
          end
        end
        
        table.clear(variable_data)
        if #variable_node ~= 0 then
          add(1, "返回父节点")
        end
        add(2, "序列化节点")
        
        for k, v in pairs(tab) do
          local _k, _v = k, v
          local keyStr = type(k) ~= 'string' and string.format('[%s]', tostring(k)) or k
          
          if keywor == '' or string.find(string.lower(keyStr), keywor, 1, true) then
            local v_str = tostring(v)
            if utf8.len(v_str) > 80 then
              v_str = utf8.sub(v_str, 1, 80) .. '...'
            end
            
            if type(_v) == 'string' then
              v_str = string.format('"%s"', v_str)
             elseif type(_v) == 'table' then
              v_str = string.format('%s => {...}', v_str)
            end
            
            local s = string.format('%s => %s', keyStr, v_str)
            
            local span = SpannableStringBuilder()
            span.append(s)
            
            local s1, e1 = utf8.find(s, '=>')
            span.setSpan(color_span2, 0, utf8.len(keyStr), flags)
            span.setSpan(color_span1, s1 - 1, e1, flags)
            
            if type(_v) == 'table' then
              if s:find('table') then
                local s0, e0 = utf8.find(s, 'table:%s-0x%x+%s')
                span.setSpan(color_span3, s0 - 1, e0, flags)
                local s1, e1 = utf8.find(s, '=>', e0)
                span.setSpan(color_span4, s1 - 1, e1, flags)
                local s2, e2 = utf8.find(s, '{%.%.%.}', e1)
                span.setSpan(color_span5, s2 - 1, e2, flags)
                variable_path[tostring(_k)] = _k
              end
            end
            add({ _k, _v }, span)
          end
        end
        
        variable_adp.notifyDataSetChanged()
      end

      --变量搜索框文本变化监听
      ids.vmVariableListView_search.addTextChangedListener({
        onTextChanged = function(s, start, before, count)
          searchVariables(tostring(s))
        end
      })

      --logcat
      local logcat_pos, show = 1

      local items = { "全部", "Lua", "Tcc", "错误", "警告", "信息", "调试", "详细" }
      local types = { '', "lua:* *:S", "tcc:* *:S", "*:E", "*:W", "*:I", "*:D", "*:V" }

      local logcat_text = {}
      local logcat_data = {}
      local logcat_adp = LuaAdapter(activity, logcat_data, {
        MaterialTextView,
        layout_width = "fill",
        textSize = "13dp",
        padding = "5dp",
        paddingLeft = "6dp",
        paddingRight = "16dp",
        textColor = colorPrimary,
        id = 'txt',
      })

      ids.vmLogcatListView.setAdapter(logcat_adp)

      ids.vmLogcatListView.onItemClick = function(a, b, c, d)
        ids.vmText.setText(logcat_text[d])
        ids.cvpgx.setCurrentItem(3, false)
      end

      ids.vmLogcatListView.onItemLongClick = function(a, b, c, d)
        local logContent = logcat_text[d]
        if logContent then
          activity.getSystemService(Context.CLIPBOARD_SERVICE).setText(logContent)
          Toast.makeText(activity, "已复制日志", Toast.LENGTH_SHORT).show()
        end
        return true
      end

      local function read()
        task(readlog, types[logcat_pos], function(str)
          table.clear(logcat_data)
          local t, n = {}, 0
          str:gsub('[^\n\n]+', function(w)
            if n % 2 == 0 then
              if not w:find('^%[') then
                t[#t] = string.format('%s\n%s', t[#t], w)
               else
                t[#t + 1] = w
                n = n + 1
              end
             else
              t[#t] = string.format('%s\n%s', t[#t], w)
              n = n + 1
            end
          end)
          for k, v in ipairs(t) do
            logcat_data[k] = {
              txt = {
                text = v,
              }
            }
            logcat_text[k] = v
          end
          logcat_adp.notifyDataSetChanged()
        end)
      end

      local function searchLogcat(keywor)
        keywor = tostring(keywor)
        if keywor == '' then
          read()
          return
        end
        
        keywor = string.lower(keywor)
        table.clear(logcat_data)
        
        for k, v in ipairs(logcat_text) do
          if string.find(string.lower(v), keywor, 1, true) then
            logcat_data[#logcat_data + 1] = {
              txt = {
                text = v,
              }
            }
          end
        end
        
        logcat_adp.notifyDataSetChanged()
      end

      --日志搜索框文本变化监听
      ids.vmLogcatSearch.addTextChangedListener({
        onTextChanged = function(s, start, before, count)
          searchLogcat(tostring(s))
        end
      })

      for i = 0, 7 do
        local v = ids.logcat_bar.getChildAt(i * 2)
        v.setTextColor(0xFF909090)
        v.onClick = function(v0)
          logcat_pos = i + 1
          for i = 0, 14, 2 do
            local _v = ids.logcat_bar.getChildAt(i)
            _v.setTextColor(0xFF909090)
            .setBackgroundResource(circleRippleRes.resourceId)
          end
          v0.setTextColor(colorPrimary)
          read()
        end
      end

      local v = ids.logcat_bar.getChildAt(0)
      v.setTextColor(colorPrimary)

      --bar
      local btn_list = {
        {
          '清空', function(v)
            vmPrintListView_adp.clear()
            vmPrintListView_default()
          end,
          '隐藏', function(v)
            changeVMWindow()
          end
        },
        {
          '刷新', function(v)
            tree()
          end,
          '隐藏', function(v)
            changeVMWindow()
          end
        },
        {
          '清空', function(v)
            task(clearlog, read)
          end,
          '隐藏', function(v)
            changeVMWindow()
          end
        },
        {
          '复制', function(v)
            activity.getSystemService(Context.CLIPBOARD_SERVICE).setText(ids.vmText.Text)
            Toast.makeText(activity, "复制成功", Toast.LENGTH_SHORT).show()
          end,
          '粘贴', function(v)
            ids.vmText.setText(activity.getSystemService(Context.CLIPBOARD_SERVICE).getText())
          end,
          '清空', function(v)
            ids.vmText.setText(nil)
          end,
          '隐藏', function(v)
            changeVMWindow()
          end
        },
        {
          '清空', function(v)
            ids.vmPrintListView_adp.clear()
          end,
          '隐藏', function(v)
            changeVMWindow()
          end
        },
        {
          '隐藏', function(v)
            changeVMWindow()
          end
        }
      }

      local btn_cache = {}

      local p = -1
      ids.cvpgx.addOnPageChangeListener {
        onPageScrolled = function(pos)
          if pos ~= p then
            p = pos
            if pos == 1 then
              tree()
             elseif pos == 2 then
              read()
             elseif pos == 5 then
              ids.vmMemory.setText("内存占用: " .. tostring(string.format("%0.2f", collectgarbage("count"))) .. " KB")
            end

            ids.bar.removeAllViews()

            task(100, function()
              ids.bar.post(function()
                local tab = btn_list[pos + 1]
                local n = 0
                local width = ids.bar.getWidth() / (#tab / 2)
                ids.bar.removeAllViews()
                for i = 1, #tab, 2 do
                  n = n + 1
                  local lay
                  if btn_cache[n] then
                    lay = btn_cache[n]
                   else
                    lay = loadlayout({
                      MaterialTextView,
                      layout_height = "fill",
                      textSize = "13dp",
                      text = tab[i],
                      gravity = "center",
                      backgroundResource = circleRippleRes.resourceId,
                      textColor = colorOnSurfaceVariant,
                    })
                    btn_cache[n] = lay
                  end
                  lay.setWidth(width)
                  lay.setText(tab[i])
                  lay.onClick = tab[i + 1]
                  ids.bar.addView(lay)
                  if n ~= #tab / 2 then
                    ids.bar.addView(loadlayout({
                      LinearLayoutCompat,
                      {
                        View,
                        layout_marginTop = "6dp",
                        layout_marginBottom = "6dp",
                        layout_width = "1dp",
                        backgroundColor = colorSurfaceVariant,
                      }
                    }))
                  end
                end
              end)
            end)
          end
        end
      }

      vmPrintListView_default()
    end
  end

end