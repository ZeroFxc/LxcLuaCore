-----------------------------------------------------------------------------
-- Author: AndLua+ 陵阳 靐圝齉齾麤龖齉龘扩展
-----------------------------------------------------------------------------

import "android.graphics.PorterDuffColorFilter"
import "android.graphics.PorterDuff"
import "android.graphics.drawable.ColorDrawable"
import"android.view.WindowManager"
function MD提示(str,color,color2,ele,rad,gen)
  if lastToast and gen then
    lastToast.cancel()
    lastToast = nil
  end
  toasts={
    CardView;
    id="toastb",
    CardElevation=ele;
    radius=rad;
    backgroundColor=color;
    {
      TextView;
      layout_margin="7dp";
      textSize="13sp";
      TextColor=color2,
      text=str;
      layout_gravity="center";
      id="mess",
    };
  };
  local toast=Toast.makeText(activity,nil,Toast.LENGTH_SHORT);
  toast.setView(loadlayout(toasts))
  toast.show()
  lastToast = toast
end

function HJ提示(str,color,color2,ele,rad,gen,xyg,icon,xy)
  if lastToast and not gen then
    lastToast.cancel()
    lastToast = nil
  end
  toasts={
    CardView;
    id="toastb",
    CardElevation=ele;
    radius=rad;
    backgroundColor=color;
    {
      LinearLayout;
      gravity="center_vertical";
      {
        ImageView;
        src=icon;
        layout_margin="2dp";
        layout_width=xy and tonumber(xy[1]) or 60;
        layout_height=xy and tonumber(xy[2]) or 60;
        visibility=icon and 0 or 8;
      };
      {
        TextView;
        layout_margin="7dp";
        textSize="13sp";
        TextColor=color2,
        text=str;
        layout_gravity="center";
        id="mess",
      };
    };
  };
  local toast=Toast.makeText(activity,nil,Toast.LENGTH_SHORT);
  toast.setView(loadlayout(toasts))
  if xyg then
    toast.setGravity(xyg[3] or Gravity.BOTTOM,tonumber(xyg[1]) or 0,tonumber(xyg[2]) or 300)
  end
  toast.show()
  lastToast = toast
end

function 窗口标题(text)
  activity.setTitle(text)
end

function 载入界面(id)
  activity.setContentView(loadlayout(id))
end

function 隐藏标题栏()
  activity.getSupportActionBar().hide()
end
function 显示标题栏()
  activity.getSupportActionBar().show()
end
function 设置主题(id)
  activity.setTheme(id)
end

function 打印(text)
  print(text)
end

function 窗口全屏()
  activity.getWindow().setFlags(WindowManager.LayoutParams.FLAG_FULLSCREEN,WindowManager.LayoutParams.FLAG_FULLSCREEN)
end

function 取消全屏()
  activity.getWindow().clearFlags(WindowManager.LayoutParams.FLAG_FULLSCREEN)
end

function 返回桌面()
  activity.moveTaskToBack(true)
end

function 提示(text)
  Toast.makeText(activity,text,Toast.LENGTH_SHORT).show()
end

function 截取文本(str,str1,str2)
  str1=str1:gsub("%p",function(s) return("%"..s) end)
  return str:match(str1 .. "(.-)"..str2)
end

function 替换文本(str,str1,str2)
  str1=str1:gsub("%p",function(s) return("%"..s) end)
  str2=str2:gsub("%%","%%%%")
  return str:gsub(str1,str2)
end

function 字符串长度(str)
  return utf8.len(str)
end

function 状态栏颜色(color)
  if Build.VERSION.SDK_INT >= 21 then
    activity.getWindow().addFlags(WindowManager.LayoutParams.FLAG_DRAWS_SYSTEM_BAR_BACKGROUNDS).setStatusBarColor(color);
  end
end

function 沉浸状态栏()
  if Build.VERSION.SDK_INT >= 19 then
    activity.getWindow().addFlags(WindowManager.LayoutParams.FLAG_TRANSLUCENT_STATUS);
  end
end

function 设置文本(id,text)
  id.Text=text
end

function 跳转页面(name)
  activity.newActivity(name)
end

function 跳转界面(name)
  activity.newActivity(name)
end

function 关闭页面()
  activity.finish()
end

function 关闭界面()
  activity.finish()
end

function 获取文本(id)
  return id.Text
end

function 结束程序()
  activity.finish()
end

function 重构页面()
  activity.recreate()
end

function 重构界面()
  activity.recreate()
end

function 控件圆角(view,InsideColor,radiu)
  import "android.graphics.drawable.GradientDrawable"
  drawable = GradientDrawable()
  drawable.setShape(GradientDrawable.RECTANGLE)
  drawable.setColor(InsideColor)
  drawable.setCornerRadii({radiu,radiu,radiu,radiu,radiu,radiu,radiu,radiu});
  view.setBackgroundDrawable(drawable)
end

function 获取设备标识码()
  import "android.provider.Settings$Secure"
  return Secure.getString(activity.getContentResolver(), Secure.ANDROID_ID)
end

function 获取IMEI()
  import "android.content.Context"
  return activity.getSystemService(Context.TELEPHONY_SERVICE).getDeviceId()
end

function 控件背景渐变动画(view,color1,color2,color3,color4)
  import "android.animation.ObjectAnimator"
  import "android.animation.ArgbEvaluator"
  import "android.animation.ValueAnimator"
  import "android.graphics.Color"
  colorAnim = ObjectAnimator.ofInt(view,"backgroundColor",{color1, color2, color3,color4})
  colorAnim.setDuration(3000)
  colorAnim.setEvaluator(ArgbEvaluator())
  colorAnim.setRepeatCount(ValueAnimator.INFINITE)
  colorAnim.setRepeatMode(ValueAnimator.REVERSE)
  colorAnim.start()
end

function 获取屏幕尺寸(ctx)
  import "android.util.DisplayMetrics"
  dm = DisplayMetrics();
  ctx.getWindowManager().getDefaultDisplay().getMetrics(dm);
  diagonalPixels = Math.sqrt(Math.pow(dm.widthPixels, 2) + Math.pow(dm.heightPixels, 2));
  return diagonalPixels / (160 * dm.density);
end

function 是否安装APP(packageName)
  if pcall(function() activity.getPackageManager().getPackageInfo(packageName,0) end) then
    return true
   else
    return false
  end
end

function 设置中划线(id)
  import "android.graphics.Paint"
  id.getPaint().setFlags(Paint. STRIKE_THRU_TEXT_FLAG)
end

function 设置下划线(id)
  import "android.graphics.Paint"
  id.getPaint().setFlags(Paint. UNDERLINE_TEXT_FLAG)
end

function 设置字体加粗(id)
  import "android.graphics.Paint"
  id.getPaint().setFakeBoldText(true)
end

function 设置斜体(id)
  import "android.graphics.Paint"
  id.getPaint().setTextSkewX(0.2)
end

function 分享内容(text)
  intent=Intent(Intent.ACTION_SEND);
  intent.setType("text/plain");
  intent.putExtra(Intent.EXTRA_SUBJECT, "分享");
  intent.putExtra(Intent.EXTRA_TEXT, text);
  intent.setFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
  activity.startActivity(Intent.createChooser(intent,"分享到:"));
end

function 加QQ群(qq)
  import "android.net.Uri"
  import "android.content.Intent"
  activity.startActivity(Intent(Intent.ACTION_VIEW, Uri.parse("mqqapi://card/show_pslcard?src_type=internal&version=1&uin="..qq.."&card_type=group&source=qrcode")))
end

function 跳转QQ群(qq)
  import "android.net.Uri"
  import "android.content.Intent"
  activity.startActivity(Intent(Intent.ACTION_VIEW, Uri.parse("mqqapi://card/show_pslcard?src_type=internal&version=1&uin="..qq.."&card_type=group&source=qrcode")))
end

function QQ聊天(qq)
  import "android.net.Uri"
  import "android.content.Intent"
  activity.startActivity(Intent(Intent.ACTION_VIEW, Uri.parse("mqqwpa://im/chat?chat_type=wpa&uin="..qq)))
end

function 跳转QQ聊天(qq)
  import "android.net.Uri"
  import "android.content.Intent"
  activity.startActivity(Intent(Intent.ACTION_VIEW, Uri.parse("mqqwpa://im/chat?chat_type=wpa&uin="..qq)))
end

function 发送短信(phone,text)
  require "import"
  import "android.telephony.*"
  SmsManager.getDefault().sendTextMessage(tostring(phone), nil, tostring(text), nil, nil)
end

function 获取剪切板()
  import "android.content.Context"
  return activity.getSystemService(Context.CLIPBOARD_SERVICE).getText()
end

function 写入剪切板(text)
  import "android.content.Context"
  activity.getSystemService(Context.CLIPBOARD_SERVICE).setText(text)
end

function 开启WIFI()
  import "android.content.Context"
  wifi = activity.Context.getSystemService(Context.WIFI_SERVICE)
  wifi.setWifiEnabled(true)
end

function 关闭WIFI()
  import "android.content.Context"
  wifi = activity.Context.getSystemService(Context.WIFI_SERVICE)
  wifi.setWifiEnabled(false)
end

function 断开网络()
  import "android.content.Context"
  wifi = activity.Context.getSystemService(Context.WIFI_SERVICE)
  wifi.disconnect()
end

function 创建文件(file)
  import "java.io.File"
  return File(file).createNewFile()
end

function 创建文件夹(file)
  import "java.io.File"
  return File(file).mkdir()
end

function 创建多级文件夹(file)
  import "java.io.File"
  return File(file).mkdirs()
end

function 移动文件(file,file2)
  import "java.io.File"
  return File(file).renameTo(File(file2))
end

function 写入文件(file,text)
  return io.open(file,"w"):write(text):close()
end

function 设置按钮颜色(id,color)
  id.getBackground().setColorFilter(PorterDuffColorFilter(color,PorterDuff.Mode.SRC_ATOP))
end

function 设置编辑框颜色(id,color)
  id.getBackground().setColorFilter(PorterDuffColorFilter(color,PorterDuff.Mode.SRC_ATOP));
end

function 设置进度条颜色(id,color)
  id.IndeterminateDrawable.setColorFilter(PorterDuffColorFilter(color,PorterDuff.Mode.SRC_ATOP))
end

function 设置控件颜色(id,color)
  id.setBackgroundColor(color)
end

function 获取手机存储路径()
  return Environment.getExternalStorageDirectory().toString()
end

function 获取屏幕宽()
  return activity.getWidth()
end

function 获取屏幕高()
  return activity.getHeight()
end

function 文件是否存在(file)
  import "java.io.File"
  return File(file).exists()
end

function 关闭左侧滑(id)
  id.closeDrawer(3)
end

function 打开左侧滑()
  id.openDrawer(3)
end

function 显示控件(id)
  id.setVisibility(0)
end

function 隐藏控件(id)
  id.setVisibility(8)
end

function 播放本地音乐(url)
  import "android.content.Intent"
  import "android.net.Uri"
  intent = Intent(Intent.ACTION_VIEW)
  uri = Uri.parse("file://"..url)
  intent.setDataAndType(uri, "audio/mp3")
  this.startActivity(intent)
end

function 在线播放音乐(url)
  import "android.content.Intent"
  import "android.net.Uri"
  intent = Intent(Intent.ACTION_VIEW)
  uri = Uri.parse(url)
  intent.setDataAndType(uri, "audio/mp3")
  this.startActivity(intent)
end

function 播放本地视频(url)
  import "android.content.Intent"
  import "android.net.Uri"
  intent = Intent(Intent.ACTION_VIEW)
  uri = Uri.parse("file://"..url)
  intent.setDataAndType(uri, "video/mp4")
  activity.startActivity(intent)
end

function 在线播放视频(url)
  import "android.content.Intent"
  import "android.net.Uri"
  intent = Intent(Intent.ACTION_VIEW)
  uri = Uri.parse(url)
  intent.setDataAndType(uri, "video/mp4")
  activity.startActivity(intent)
end

function 打开APP(packageName)
  import "android.content.Intent"
  import "android.content.pm.PackageManager"
  manager = activity.getPackageManager()
  open = manager.getLaunchIntentForPackage(packageName)
  this.startActivity(open)
end

function 卸载APP(file)
  import "android.net.Uri"
  import "android.content.Intent"
  uri = Uri.parse("package:"..file)
  intent = Intent(Intent.ACTION_DELETE,uri)
  activity.startActivity(intent)
end

function 安装APP(file)
  import "android.content.Intent"
  import "android.net.Uri"
  intent = Intent(Intent.ACTION_VIEW)
  intent.setDataAndType(Uri.parse("file://"..file), "application/vnd.android.package-archive")
  intent.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)
  activity.startActivity(intent)
end

function 系统下载文件(url,directory,name)
  import "android.content.Context"
  import "android.net.Uri"
  downloadManager=activity.getSystemService(Context.DOWNLOAD_SERVICE);
  url=Uri.parse(url);
  request=DownloadManager.Request(url);
  request.setAllowedNetworkTypes(DownloadManager.Request.NETWORK_MOBILE|DownloadManager.Request.NETWORK_WIFI);
  request.setDestinationInExternalPublicDir(directory,name);
  request.setNotificationVisibility(DownloadManager.Request.VISIBILITY_VISIBLE_NOTIFY_COMPLETED);
  downloadManager.enqueue(request);
end

function 调用系统下载文件(url,directory,name)
  import "android.content.Context"
  import "android.net.Uri"
  downloadManager=activity.getSystemService(Context.DOWNLOAD_SERVICE);
  url=Uri.parse(url);
  request=DownloadManager.Request(url);
  request.setAllowedNetworkTypes(DownloadManager.Request.NETWORK_MOBILE|DownloadManager.Request.NETWORK_WIFI);
  request.setDestinationInExternalPublicDir(directory,name);
  request.setNotificationVisibility(DownloadManager.Request.VISIBILITY_VISIBLE_NOTIFY_COMPLETED);
  downloadManager.enqueue(request);
end

function 弹窗1(title,content,text,fun)
  dialog=AlertDialog.Builder(this)
  .setTitle(title)
  .setMessage(content)
  .setPositiveButton(text,{onClick=fun})
  .show()
  dialog.create()
end

function 确定弹窗(title,content,text,fun)
  dialog=AlertDialog.Builder(this)
  .setTitle(title)
  .setMessage(content)
  .setPositiveButton(text,{onClick=fun})
  .show()
  dialog.create()
end

function 波纹(id,color)
  import "android.content.res.ColorStateList"
  local attrsArray = {android.R.attr.selectableItemBackgroundBorderless}
  local typedArray =activity.obtainStyledAttributes(attrsArray)
  ripple=typedArray.getResourceId(0,0)
  aoos=activity.Resources.getDrawable(ripple)
  aoos.setColor(ColorStateList(int[0].class{int{}},int{color}))
  id.setBackground(aoos.setColor(ColorStateList(int[0].class{int{}},int{color})))
end

function 添加波纹效果(id,color)
  import "android.content.res.ColorStateList"
  local attrsArray = {android.R.attr.selectableItemBackgroundBorderless}
  local typedArray =activity.obtainStyledAttributes(attrsArray)
  ripple=typedArray.getResourceId(0,0)
  aoos=activity.Resources.getDrawable(ripple)
  aoos.setColor(ColorStateList(int[0].class{int{}},int{color}))
  id.setBackground(aoos.setColor(ColorStateList(int[0].class{int{}},int{color})))
end

function 随机数(min,max)
  return math.random(min,max)
end

function 删除控件(id,id2)
  return (id).removeView(id2)
end

function 状态栏亮色()
  if Build.VERSION.SDK_INT >= 23 then
    activity.getDecorView().setSystemUiVisibility(View.SYSTEM_UI_FLAG_LIGHT_STATUS_BAR);
  end
end


import "com.limo.tool.*"
function 初始化(l,v,dia)
  local a,t,w
  if not dia
    a,t,w=l.flags,l.title,this.getSystemService(Context.WINDOW_SERVICE)
   else
    w=this.getSystemService(Context.WINDOW_SERVICE)
  end
  return {
    隐藏=function()
      if v
        w.removeView(v)
      end
      if not dia
        l.flags=a|WindowManager.LayoutParams().FLAG_NOT_FOCUSABLE
       else
        l.addFlags(WindowManager.LayoutParams().FLAG_NOT_FOCUSABLE)
        l.setDimAmount(0)
      end
      if RomUtil.isMiui()
        l.title="com.miui.screenrecorder"
       elseif RomUtil.isEmui()
        l.title="ScreenRecoderTimer"
       elseif RomUtil.isVivo()
        l.title="screen_record_menu"
       elseif RomUtil.isOppo()
        l.title="com.coloros.screenrecorder.FloatView"
       elseif RomUtil.isSmartisan()
        l.title=""
       elseif RomUtil.isFlyme()
        pcall(function()
          l.title="SysScreenRecorder"
          local MeizuParamsClass = Class.forName("android.view.MeizuLayoutParams");
          local flagField = MeizuParamsClass.getDeclaredField("flags");
          flagField.setAccessible(true);
          local MeizuParams = MeizuParamsClass.newInstance();
          flagField.setInt(MeizuParams, WindowManager.LayoutParams().FLAG_DITHER | 1048576 | 262696 | 131072 | 4136);
          local mzParamsField = l.getClass().getField("meizuParams");
          mzParamsField.set(l, MeizuParams);
        end)
      end
      if v
        w.addView(v,l)
      end
    end,
    显示=function()
      if v
        w.removeView(v)
      end
      l.flags=a
      l.title=t
      if v
        w.addView(v,l)
      end
    end
  }
end

--靐圝齉齾麤龖齉龘新添加↓
function 设置边框(view, Thickness, FrameColor, InsideColor, radiu)
  import("android.graphics.drawable.GradientDrawable")
  drawable = GradientDrawable()
  drawable.setShape(GradientDrawable.RECTANGLE)
  drawable.setStroke(Thickness, FrameColor)
  drawable.setColor(InsideColor)
  drawable.setCornerRadii({radiu,radiu,radiu,radiu,radiu,radiu,radiu,radiu})
  view.setBackgroundDrawable(drawable)
end

function 水珠动画(控件,时间)
  import "android.animation.ObjectAnimator"
  ObjectAnimator().ofFloat(控件,"scaleX",{1,0.8,1}).setDuration(时间).start()
  ObjectAnimator().ofFloat(控件,"scaleY",{1,0.8,1}).setDuration(时间).start()
end

function 时间总和()
import"android.widget.Toast" local currentDateTime=os.date("*t") local year=currentDateTime.year local month=currentDateTime.month local day=currentDateTime.day local hour=currentDateTime.hour local minute=currentDateTime.min local second=currentDateTime.sec local ms=math.floor(os.clock()*1000) local result=year+month+day+hour+minute+second+ms print(tostring(result))
end

function 执行二进制(路径,提示)
  os.execute("su")
  ROOT = os.execute("su")
  调用路径=activity.getLuaDir(路径)
  if ROOT then
  os.execute("su -c chmod 777 "..调用路径)
  Runtime.getRuntime().exec("su -c "..调用路径)
  else
  os.execute("chmod 777 "..调用路径)
  Runtime.getRuntime().exec(""..调用路径)
  end
  if 提示 then
  MD提示(提示,0xFFFFFFFF,0x9C000000,4,10)
  end
end

function 列表载入(view,列表)
local Adapter = luajava.bindClass("android.widget.ArrayAdapter")
local ListView = luajava.bindClass("android.widget.ListView")
local adapter = Adapter(activity, android.R.layout.simple_list_item_1, 列表)
view.setAdapter(adapter)
end

function 修改音量(音量,显示)
import "android.content.Context"
import "android.media.AudioManager"
local mAudioManager = activity.getSystemService(Context.AUDIO_SERVICE);
local mVolume = mAudioManager.getStreamVolume(AudioManager.STREAM_MUSIC);
mAudioManager.setStreamVolume(AudioManager.STREAM_MUSIC, 音量, 显示);
end

function 私有目录(fin)
  local t = activity.getPackageManager().getInstalledPackages(0)
  for _FORV_4_ = 0, t.size() - 1 do
    local app = {}
    local count = t.get(_FORV_4_)
    app.filedir = count.applicationInfo.sourceDir
    if app.filedir:find(activity.getPackageName()) then
      local apk=app.filedir:gsub("%/base%.apk","")
      if fin then apk=apk.."/"..fin end
      return apk
    end
  end
end

function 私有目录1(fin)
  local fi=""
  if fin then
    fi="/"..fin
  end
  return "/data/user/0/"..activity.getPackageName()..fi
end

function 关闭弹窗(id)
  id.dismiss()
end

function 构建弹窗(id,hjoo,hjao,hjbo,hjco,title,text1,fun1,text2,fun2,text3,fun3)
  dds=AlertDialog.Builder(this)
  .setView(loadlayout(id))
  if title then dds.setTitle(title) end
  if text1 then dds.setPositiveButton(text1,{onClick=fun1 or function()end}) end
  if text2 then dds.setNegativeButton(text2,{onClick=fun2 or function()end}) end
  if text3 then dds.setNeutralButton(text3,{onClick=fun3 or function()end}) end
  dd=dds.show()
  dd.setCanceledOnTouchOutside(hjao or true)
  dd.setCancelable(hjbo or true)
  import "android.graphics.drawable.ColorDrawable"
  import "android.view.WindowManager$LayoutParams"
  dd.getWindow().setBackgroundDrawable(ColorDrawable(hjoo or 0x00000000));
  if hjco then
    local window = dd.getWindow()
    local params = window.getAttributes()
    params.width = WindowManager.LayoutParams.MATCH_PARENT
    params.height = WindowManager.LayoutParams.MATCH_PARENT
    window.setAttributes(params)
  end
  return dd
end

function 获取控件ID(a)
  return load("return "..a)()
end

function 查找列表(list,txt)
  for i, value in pairs(list) do
    if value == txt then
      return i
    end
  end
end

function 查找列表1(list,txt)
  for i, value in pairs(list) do
    if value:find(txt) then
      return i
    end
  end
end

function 递增压缩(avav,avab)
  import "java.io.File"
  import "zip4j"
  local directory = File(avav)
  local files = directory.listFiles()
  for i=0,#files-1 do
    zip4j.ZipDir(tostring(files[i]),avab,"1234")
  end
end

function 语音播报(内容,语速,语调)
  import "android.speech.tts.*"
  mTextSpeech = TextToSpeech(activity, TextToSpeech.OnInitListener{
    onInit=function(status)
      --如果装载TTS成功
      if (status == TextToSpeech.SUCCESS)
        result = mTextSpeech.setLanguage(Locale.CHINESE);
        --[[LANG_MISSING_DATA-->语言的数据丢失
          LANG_NOT_SUPPORTED-->语言不支持]]
        if (result == TextToSpeech.LANG_MISSING_DATA or result == TextToSpeech.LANG_NOT_SUPPORTED)
          --不支持中文
          print("您的手机不支持中文语音播报功能。");
          result = mTextSpeech.setLanguage(Locale.ENGLISH);
          if (result == TextToSpeech.LANG_MISSING_DATA or result == TextToSpeech.LANG_NOT_SUPPORTED)
            --不支持中文和英文
            print("您的手机不支持语音播报功能。");
           else
            --不支持中文但支持英文
            --语调,1.0默认
            mTextSpeech.setPitch(语调 or 1);
            --语速,1.0默认
            mTextSpeech.setSpeechRate(语速 or 1);
            mTextSpeech.speak("hello,MLua Manual.Hello,World!", TextToSpeech.QUEUE_FLUSH, nil);
          end
         else
          --支持中文
          --语调,1.0默认
          mTextSpeech.setPitch(语调 or 1);
          --语速,1.0默认
          mTextSpeech.setSpeechRate(语速 or 1);
          mTextSpeech.speak(内容, TextToSpeech.QUEUE_FLUSH, nil);
        end
      end
    end
  });
end

function 防截屏录屏(enable)
  import "android.view.WindowManager"
  local window = activity.getWindow()
  local params = window.getAttributes()
  if enable then
    -- 启用安全标志
    params.flags = params.flags | WindowManager.LayoutParams.FLAG_SECURE
   else
    -- 禁用安全标志
    params.flags = params.flags & ~WindowManager.LayoutParams.FLAG_SECURE
  end
  window.setAttributes(params)
end

function 防抓包()
  import "java.net.NetworkInterface"
  import "java.util.Collections"
  import "java.util.Enumeration"
  import "java.util.Iterator"
  local nlp= NetworkInterface.getNetworkInterfaces();
  if (nlp~=nil) then
    local it = Collections.list(nlp).iterator();
    while (it.hasNext()) do
      local nlo = it.next();
      if (nlo.isUp() && nlo.getInterfaceAddresses().size() ~= 0) then
        if ((tostring(nlo):find("tun0")) or (tostring(nlo):find("ppp0"))) then
          return true
        end
      end
    end
    return false
  end
end

function 获取文件当前目录(strg)
  import "java.io.File"
  strg=strg:sub(1,#strg-#File(strg).getName())
  return strg
end

function 移除文件名后缀(strg)
  strg=strg:match("^(.*)%.%w+$")
  return strg
end

function 获取文件名后缀(strg)
  local srf=移除文件名后缀(strg)
  local srf=strg:sub(#srf+2,-1)
  return srf
end

function 文件名扩展(strg,txf)
  local srf=移除文件名后缀(strg)
  local srff=strg:sub(#srf+1,-1)
  return srf..txf..srff
end

function ASII解密(text,tonu)
  if tonu then tonu=0 end
  local ah=text:gsub("..", function(a)
    return string.char((tonumber(a, 16)-tonumber(tonu))%256)
  end)
  return ah
end

function ASII加密(str,tonu)
  if tonu then tonu=0 end
  local aj=table.concat({string.byte(str,1,-1)},',')
  local ak=""
  for i in aj:gmatch("%d+") do
    ak=ak..string.format("%X",(i+tonumber(tonu))%256)
  end
  return ak
end

function utf8字节码位移(str,unk)
  if not unk then unk=0 end
  local text=""
  for i=1, utf8.len(str) do
    local c = utf8.sub(str, i, i)
    local unicode = utf8.codepoint(c)
    local hex = string.format('%X', unicode+unk)
    text=text.."\\u"..hex
  end
  return text
end

function string字节码位移(text, offset)
  local encryptedText = ""
  for i = 1, string.len(text) do
    local char = string.char((string.byte(text, i) + offset)%256)
    encryptedText = encryptedText .. char
  end
  return encryptedText
end

function 加载dex(路径,类名)
  import "java.io.File"
  import "dalvik.system.DexFile"
  类名=类名:gsub("/",".")
  local function 载dexs(路1径)
    local d导成=false
    if 类名:find("%*$") then
      local 类名HJ=类名:gsub("%.","%%."):sub(1,-2)
      local success, dexFile = pcall(function()
        return DexFile(路1径)
      end)
      if success and dexFile then
        local 枚举 = dexFile.entries()
        while 枚举.hasMoreElements() do
          local 类名 = 枚举.nextElement()
          local hjs=类名:match("^"..类名HJ.."([^%.%$]-)$")
          if hjs then
            if pcall(activity.loadDex(路1径).loadClass,类名) then
              d导成=true
              _ENV[hjs]=activity.loadDex(路1径).loadClass(类名)
            end
          end
        end
       else
        return false,"dex错误"
      end
     else
      local 类名b=类名:match("([^%.]+)$")
      if pcall(activity.loadDex(路1径).loadClass,类名) then
        d导成=true
        _ENV[类名b]=activity.loadDex(路1径).loadClass(类名)
       else
        return false
      end
    end
    return d导成
  end
  local file=io.open(路径, "r")
  if file then
    if File(路径).isDirectory() then
      local d导成=false
      local d导志="文件夹没有dex或没有这个类"
      local 文表=luajava.astable(File(路径).listFiles())
      if #文表==0 then return false,"空文件夹" end
      for i=1,#文表 do
        if tostring(文表[i]):find("%.dex$") then
          local ab,_=载dexs(tostring(文表[i]))
          if ab then
            d导成=true d导志=nil
          end
        end
      end
      return d导成,d导志
     else
      载dexs(路径)
    end
    return true
   else
    return false,"路径错误"
  end
end

function 导入alua内部dex(dex,类名)
  local 路径="/data/user/0/com.AndLua.LZ/files/libs/"..dex
  local 路径1=activity.getLuaDir("libs").."/"..dex
  if io.open(路径) then
    LuaUtil.copyDir(路径,路径1)
  end
  if 类名 then 加载dex(路径1,类名) end
end

function 悬浮窗权限检测(txt)
  if Build.VERSION.SDK_INT >= Build.VERSION_CODES.M&&!Settings.canDrawOverlays(this) then
    MD提示(txt or "没有悬浮窗权限悬，请打开权限",0xFFFFFFFF,0x9C000000,4,10)
    intent=Intent(Settings.ACTION_MANAGE_OVERLAY_PERMISSION)
    activity.startActivityForResult(intent, 100)
    os.exit()
  end
end

function HJ快捷悬浮窗(id, text, tSize, tColorg, tColork, x, y, dx, dy, radius, bColorg, bColork, functiona, functionb)
  悬浮窗权限检测()
  text=text or "滑稽"
  functiona=functiona or ""
  functionb=functionb or ""
  radius=radius or 40
  tSize=tSize or 18
  tColorg=tColorg or 0xFF000000
  tColork=tColork or 0xFF00B2FF
  bColorg=bColorg or 0xFFFFFFFF
  bColork=bColork or 0xFFFFFFFF
  x=x or 2 y=y or 2 dx=dx or 40 dy=dy or 40
  if 获取控件ID("悬浮窗状态"..id) then
    return false
  end
  _ENV["窗口"..id] = activity.getSystemService(Context.WINDOW_SERVICE)
  _ENV["窗口容器"..id] = WindowManager.LayoutParams()
  if Build.VERSION.SDK_INT >= 26 then
    获取控件ID("窗口容器"..id).type = WindowManager.LayoutParams.TYPE_APPLICATION_OVERLAY
   else
    获取控件ID("窗口容器"..id).type = WindowManager.LayoutParams.TYPE_SYSTEM_ALERT
  end
  获取控件ID("窗口容器"..id).flags = WindowManager.LayoutParams.FLAG_NOT_FOCUSABLE
  获取控件ID("窗口容器"..id).gravity = Gravity.LEFT | Gravity.TOP
  获取控件ID("窗口容器"..id).format = 1
  获取控件ID("窗口容器"..id).x = x
  获取控件ID("窗口容器"..id).y = y
  获取控件ID("窗口容器"..id).width = WindowManager.LayoutParams.WRAP_CONTENT
  获取控件ID("窗口容器"..id).height = WindowManager.LayoutParams.WRAP_CONTENT
  _ENV["悬浮窗布局"..id] = {
    LinearLayout;
    id = "快捷悬浮"..id;
    layout_height = "fill";
    layout_width = "fill";
    {
      CardView;
      radius = radius;
      layout_width = dx.."dp";
      layout_height = dy.."dp";
      layout_margin = "2dp";
      backgroundColor = bColorg;
      {
        LinearLayout;
        id = "悬浮窗框架"..id;
        layout_width = "match_parent";
        layout_height = "match_parent";
        {
          TextView;
          layout_margin = "2dp";
          textColor = tColorg;
          id = "悬浮窗字体"..id;
          textSize = tSize;
          layout_width = "match_parent";
          layout_height = "match_parent";
          gravity = "center";
          text = text;
        };
      };
    };
  }

  _ENV["悬浮球"..id] = loadlayout(_ENV["悬浮窗布局"..id])
  _G["悬浮窗状态"..id] = true
  _G["坐标"..id] = {x = x, y = y}
  获取控件ID("窗口"..id).addView(获取控件ID("悬浮球"..id), 获取控件ID("窗口容器"..id))

  local firstX, firstY, wmX, wmY
  _G["悬浮窗开关状态"..id] = false

  获取控件ID("悬浮球"..id).setOnClickListener(function()
    local 字体控件 = 获取控件ID("悬浮窗字体"..id)
    local 悬浮框架 = 获取控件ID("悬浮窗框架"..id)
    if _G["悬浮窗开关状态"..id] == false then
      _G["悬浮窗开关状态"..id] = true
      字体控件.setTextColor(tColork)
      悬浮框架.setBackgroundDrawable(ColorDrawable(bColork))
      if functiona ~= "" then functiona(获取控件ID("快捷悬浮"..id)) end
     else
      _G["悬浮窗开关状态"..id] = false
      字体控件.setTextColor(tColorg)
      悬浮框架.setBackgroundDrawable(ColorDrawable(bColorg))
      if functionb ~= "" then functionb(获取控件ID("快捷悬浮"..id)) end
    end
  end)

  获取控件ID("悬浮球"..id).setOnTouchListener(function(v, event)
    if event.getAction() == MotionEvent.ACTION_DOWN then
      firstX = event.getRawX()
      firstY = event.getRawY()
      wmX = 获取控件ID("窗口容器"..id).x
      wmY = 获取控件ID("窗口容器"..id).y
     elseif event.getAction() == MotionEvent.ACTION_MOVE then
      _G["坐标"..id].x = wmX + (event.getRawX() - firstX)
      _G["坐标"..id].y = wmY + (event.getRawY() - firstY)
      获取控件ID("窗口容器"..id).x = _G["坐标"..id].x
      获取控件ID("窗口容器"..id).y = _G["坐标"..id].y
      获取控件ID("窗口"..id).updateViewLayout(获取控件ID("悬浮球"..id), 获取控件ID("窗口容器"..id))
     elseif event.getAction() == MotionEvent.ACTION_UP then
      获取控件ID("窗口"..id).updateViewLayout(获取控件ID("悬浮球"..id), 获取控件ID("窗口容器"..id))
    end
    return false
  end)
  return true
end

function HJ快捷悬浮窗关闭(id)
  if _G["悬浮窗状态"..id] then
    获取控件ID("窗口"..id).removeView(获取控件ID("悬浮球"..id))
    _G["悬浮窗状态"..id] = false
    return true
   else
    return false
  end
end

function HJ弹窗(title,content,text1,fun1,text2,fun2,text3,fun3)
  dialog=AlertDialog.Builder(this)
  .setTitle(title or "标题")
  .setMessage(content or "内容")
  if text1 then dialog.setPositiveButton(text1,{onClick=fun1 or function()end}) end
  if text2 then dialog.setNegativeButton(text2,{onClick=fun2 or function()end}) end
  if text3 then dialog.setNeutralButton(text3,{onClick=fun3 or function()end}) end
  dialog.show().create()
end
