
function Json解析有道翻译(英文字母翻译成中文,回调)
  function Translate(str)
    local body=import "http".get("http://fanyi.youdao.com/translate?&doctype=json&type=AUTO&i="..tostring(str))
    return import "json".decode(body)
  end
  回调(tostring((Translate(英文字母翻译成中文)["translateResult"][1][1]["tgt"])))
end
function 色调(路径,度,值,亮)
  local 运算值 = 50
  饱和度 = ColorMatrix()--饱和度
  色相 = ColorMatrix()--色相
  亮度 = ColorMatrix()--亮度
  atrix = ColorMatrix()--合成效果
  --饱和度
  饱和度.reset()
  饱和度.setSaturation(度*1/运算值)
  --色相
  mHueValue = (值-运算值)*1/运算值*180
  色相.reset()
  相.setRotate(0,mHueValue)--控制让红色区在色轮上旋转hueColor葛角度
  色相.setRotate(1,mHueValue)--控制让绿红色区在色轮上旋转hueColor葛角度
  色相.setRotate(2,mHueValue)--控制让蓝色区在色轮上旋转hueColor葛角度
  --亮度
  mLigh = 亮*1/运算值
  亮度.reset()
  --红、绿、蓝三分量按相同的比例,最后一个参数1表示透明度不做变化
  亮度.setScale(mLigh,mLigh,mLigh,1)
  --叠加所有效果
  atrix.reset()
  atrix.postConcat(饱和度)
  atrix.postConcat(色相)
  atrix.postConcat(亮度)
  bmp = Bitmap.createBitmap(路径.getWidth(),路径.getHeight(),Bitmap.Config.ARGB_8888)
  c = Canvas(bmp)
  p = Paint()
  p.setAntiAlias(true)
  p.setColorFilter(ColorMatrixColorFilter(atrix))
  c.drawBitmap(img,0,0,p)
  return bmp
end
function 透明度(bitmap,alpha)
  import "android.graphics.Canvas"
  import "android.graphics.Rect"
  import "android.graphics.Paint"
  import "android.graphics.RectF"
  import "android.graphics.PorterDuff"
  import "android.graphics.PorterDuffXfermode"
  output = Bitmap.createBitmap(bitmap.getWidth(),bitmap.getHeight(), Bitmap.Config.ARGB_8888)
  canvas = Canvas(output)
  paint = Paint()
  paint.setAlpha(alpha)
  rect = Rect(0, 0, bitmap.getWidth(), bitmap.getHeight())
  rectF = RectF(rect)
  paint.setAntiAlias(true)
  canvas.drawARGB(0, 0, 0, 0)
  canvas.drawRect(rectF, paint)
  paint.setXfermode( PorterDuffXfermode(PorterDuff.Mode.SRC_IN))
  canvas.drawBitmap(bitmap, rect, rect, paint)
  return output
end

function 压缩图片(路径,yt)
  import "java.io.BufferedOutputStream"
  import "android.graphics.BitmapFactory"
  options = BitmapFactory.Options()
  options.inJustDecodeBounds = true --只获取图片的大小信息，而不是将整张图片载入在内存中，避免内存溢出
  --BitmapFactory.decodeFile(路径,options)
  height = options.outHeight
  width= options.outWidth
  ins = yt --默认像素压缩比例，压缩为原图的1/2
  minLen = Math.min(height, width) --原图的最小边长
  if minLen > 100 then --如果原始图像的最小边长大于100dp（此处单位我认为是dp，而非px）
    ratio = minLen / 100 --计算像素压缩比例
    ins = ratio
  end
  options.inJustDecodeBounds = false --计算好压缩比例后，这次可以去加载原图了
  options.inSampleSize = ins --设置为刚才计算的压缩比例
  return BitmapFactory.decodeFile(路径,options) --解码文件
end
function 镜像(srcBitmap)
  import "android.graphics.Canvas"
  import "android.graphics.Rect"
  width = srcBitmap.getWidth()
  height = srcBitmap.getHeight()
  newBitmap = Bitmap.createBitmap(width,height, Bitmap.Config.ARGB_8888)
  canvas = Canvas()
  matrix = Matrix()
  matrix.postScale(-1,1)
  newBitmap2 = Bitmap.createBitmap(srcBitmap,0,0,width,height,matrix,true)
  canvas.drawBitmap(newBitmap2,Rect(0,0,width,height),
  Rect(0,0,width,height),nil)
  return newBitmap2
end
function 灰度(bmSrc)
  import "android.graphics.ColorMatrixColorFilter"
  import "android.graphics.Paint"
  import "android.graphics.Canvas"
  --得到图片的长和宽
  width = bmSrc.getWidth()
  height = bmSrc.getHeight()
  --创建目标灰度图像
  bmpGray = Bitmap.createBitmap(width, height, Bitmap.Config.RGB_565)
  --创建画布
  c = Canvas(bmpGray)
  paint = Paint()
  cm = ColorMatrix()
  cm.setSaturation(0)
  f = ColorMatrixColorFilter(cm)
  paint.setColorFilter(f)
  c.drawBitmap(bmSrc, 0, 0, paint)
  return bmpGray
end
function 纵向拼接(first,second)
  width = Math.max(first.getWidth(),second.getWidth())
  height = first.getHeight() + second.getHeight()
  result = Bitmap.createBitmap(width, height,Bitmap.Config.ARGB_8888)
  canvas = Canvas(result)
  canvas.drawBitmap(first, 0, 0,nil)
  canvas.drawBitmap(second, 0, first.getHeight(),nil)
  return result
end
function 横向拼接(first,second)
  width = first.getWidth() + second.getWidth()
  height = Math.max(first.getHeight(), second.getHeight())
  result = Bitmap.createBitmap(width, height, Bitmap.Config.ARGB_8888)
  canvas = Canvas(result)
  canvas.drawBitmap(first, 0, 0, nil)
  canvas.drawBitmap(second, first.getWidth(), 0, nil)
  return result
end
function 竖向多拼接(bitmaps)
  paint = Paint()
  width = bitmaps.get(0).getWidth()
  height = bitmaps.get(0).getHeight()
  for i = 1,bitmaps.size()-1 do
    if width < bitmaps.get(i).getWidth() then
      width = bitmaps.get(i).getWidth()
    end
    height = height+bitmaps.get(i).getHeight()
  end
  result = Bitmap.createBitmap(width, height, Bitmap.Config.ARGB_8888)
  canvas = Canvas(result)
  canvas.drawBitmap(bitmaps.get(0), 0, 0, paint)
  h = 0
  for j = 1,bitmaps.size()-1 do
    h = bitmaps.get(j).getHeight()+h
    canvas.drawBitmap(bitmaps.get(j),0,h, paint)
  end
  return result
end
import "android.content.*"
import "android.os.*"
function Alp导入(路径,目标路径)
  local time=os.clock()
  function getZipfiletext(zipfile,file,code)
    local zipfile,file = ZipFile(File(tostring(zipfile))),tostring(file)
    local entries = zipfile.entries()
    local str = {}
    while entries.hasMoreElements() do
      entry = entries.nextElement();
      if tostring(entry) == file then
        local br = BufferedReader(InputStreamReader(zipfile.getInputStream(entry),code or "utf-8"));
        local line = br.readLine()
        while line ~= nil do
          str[#str+1] = line
          line = br.readLine();
        end
        break
      end
    end
    zipfile.close()
    return table.concat(str,"\n")
  end
  text = getZipfiletext(路径,"init.lua")
  a=text:match([[appname="(.-)"]])
  if a==nil then
    print("错误")
   else
    print("工程将放在:"..目标路径..""..a.."/")
    LuaUtil.unZip(路径,目标路径..""..a.."/")
    print("最终耗时:"..os.clock()-time)
  end
end
function 弹窗去背景(dialog)
  dialog.setBackgroundDrawableResource(R.color.transparent);
  dialog.setGravity(Gravity.CENTER);
  dialog.setBackgroundDrawableResource(android.R.color.transparent);
  attributes =dialog.getAttributes();
  dialog.setAttributes(attributes);
end
function 下载文件(url,filepath,filename)
  import "android.content.Context"
  import "android.net.Uri"
  import "android.app.DownloadManager"
  import "android.os.Build"
  import "android.app.ProgressDialog" downloadManager=activity.getSystemService(Context.DOWNLOAD_SERVICE);
  url=Uri.parse(url);
  request=DownloadManager.Request(url);
  request.setAllowedNetworkTypes(DownloadManager.Request.NETWORK_MOBILE|DownloadManager.Request.NETWORK_WIFI);
  request.setDestinationInExternalPublicDir(filepath,filename);
  request.setNotificationVisibility(DownloadManager.Request.VISIBILITY_VISIBLE_NOTIFY_COMPLETED);
  request.setVisibleInDownloadsUi(true)
  downloadId=downloadManager.enqueue(request);
  downloadManager=activity.getSystemService(Context.DOWNLOAD_SERVICE);
  query=DownloadManager.Query();
  cursor = downloadManager.query(query);
  if not cursor.moveToFirst() then
    cursor.close();
    return;
  end
  id = cursor.getLong(cursor.getColumnIndex(DownloadManager.COLUMN_ID));
  status = cursor.getInt(cursor.getColumnIndex(DownloadManager.COLUMN_STATUS));
  totalSize = cursor.getLong(cursor.getColumnIndex(DownloadManager.COLUMN_TOTAL_SIZE_BYTES));
  downloadedSoFar = cursor.getLong(cursor.getColumnIndex(DownloadManager.COLUMN_BYTES_DOWNLOADED_SO_FAR));
  if Build.VERSION.SDK_INT >= 24 then
    localFilename = cursor.getString(cursor.getColumnIndex(DownloadManager.COLUMN_LOCAL_URI));
   else
    localFilename = cursor.getString(cursor.getColumnIndex(DownloadManager.COLUMN_LOCAL_FILENAME));
  end
  cursor.close();
  dlDialog=ProgressDialog(this)
  dlDialog.setProgressStyle(ProgressDialog.STYLE_HORIZONTAL);
  dlDialog.setTitle("即将开始下载...")
  dlDialog.setCancelable(false) dlDialog.setCanceledOnTouchOutside(true) dlDialog.setOnCancelListener{
    onCancel=function(l)
    end}
  dlDialog.setMax(1)
  dlDialog.setProgress(0)
  dlDialog.show()
  ti=Ticker()
  ti.Period=500
  ti.onTick=function()
    cursor = downloadManager.query(query);
    if not cursor.moveToFirst() then
      cursor.close();
      return;
    end
    id = cursor.getLong(cursor.getColumnIndex(DownloadManager.COLUMN_ID));
    status = cursor.getInt(cursor.getColumnIndex(DownloadManager.COLUMN_STATUS));
    totalSize = cursor.getLong(cursor.getColumnIndex(DownloadManager.COLUMN_TOTAL_SIZE_BYTES));
    downloadedSoFar = cursor.getLong(cursor.getColumnIndex(DownloadManager.COLUMN_BYTES_DOWNLOADED_SO_FAR));
    if Build.VERSION.SDK_INT >= 24 then
      localFilename = cursor.getString(cursor.getColumnIndex(DownloadManager.COLUMN_LOCAL_URI));
     else
      localFilename = cursor.getString(cursor.getColumnIndex(DownloadManager.COLUMN_LOCAL_FILENAME));
    end
    cursor.close();
    if downloadId and downloadId==id then
      if totalSize and totalSize>0 then
        if status==1 then
          dlDialog.setTitle("等待下载...")
         elseif status==2 then
          dlDialog.setTitle("正在下载...")
          dlDialog.setMax(totalSize)
          dlDialog.setProgress(downloadedSoFar)
         elseif status==4 then
          dlDialog.setTitle("下载暂停...")
         elseif status==8 then
          dlDialog.setTitle("下载成功")
          dlDialog.setMax(totalSize)
          dlDialog.setProgress(totalSize)
          downloadId=nil
          require "import"
          import "android.view.View"
          import "com.google.android.material.snackbar.Snackbar"
          local anchor=activity.findViewById(android.R.id.content)
          Thread.sleep(1000)
          dlDialog.dismiss()
          ti.stop()
         elseif status==16 then
          dlDialog.dismiss()
          ti.stop()
        end
      end
     else
      dlDialog.dismiss()
    end
  end
  ti.start()
end
function 视频取音频()
  import "android.content.Intent"
  function 获取(filepath)
    LuaUtil.copyDir(filepath,filepath..".mp3")
    print("提取成功!\n保存至"..filepath..".mp3")
  end
  local intent= Intent(Intent.ACTION_PICK)
  intent.setType("video/*")
  this.startActivityForResult(intent,1)
  function onActivityResult(requestCode,resultCode,intent)
    xpcall(function()
      if intent then
        local cursor =this.getContentResolver ().query(intent.getData(), nil, nil, nil, nil)
        cursor.moveToFirst()
        import "android.provider.MediaStore"
        local idx = cursor.getColumnIndex(MediaStore.Images.ImageColumns.DATA)
        local fileSrc = cursor.getString(idx)
        获取(fileSrc)
      end
    end,function(error)
      print("错误:"..error)
    end)
  end
end
function 下载文件与进度(文件的绝对下载链接,文件要存放的路径)
  文件的绝对下载链接=url
  文件要存放的路径=path
  function xdc(url,path)
    require "import"
    import "java.net.URL"
    local ur =URL(url)
    import "java.io.File"
    file =File(path);
    con = ur.openConnection();
    co = con.getContentLength();
    is = con.getInputStream();
    bs = byte[1024]
    local len,read=0,0
    import "java.io.FileOutputStream"
    wj= FileOutputStream(path);
    len = is.read(bs)
    while len~=-1 do
      wj.write(bs, 0, len);
      read=read+len
      pcall(call,"正在下载事件",read,co)
      len = is.read(bs)
    end
    wj.close();
    is.close();
    pcall(call,"下载完后的事件",co)
  end
  function 下载(url,path)
    thread(xdc,url,path)
  end
  下载(url,path)
  function 正在下载事件(a,b)
    print((a/1024).."kb / "..(b/1024).."kb")--当前下载数值/总数值
    print("正在下载……"..(a/b*100).."%")--总进度
  end
  function 下载完后的事件(c)
    print("下载完成，总长度"..(c/1024).."kb")
  end
end
function 判断进入软件第一次(第一次打开应用事件,又打开应用事件)
  local setting = activity.getSharedPreferences("com.androlua.demo", 0);
  user_first = setting.getBoolean("FIRST", true);
  if (user_first) then
    setting.edit().putBoolean("FIRST", false).commit();
    if 第一次打开应用事件==nil then
     else
      pcall(load(第一次打开应用事件))
    end
   else
    if 又打开应用事件==nil then
     else
      pcall(load(又打开应用事件))
    end
  end
end
function http下载(链接,路径与名称,下载完后的事件)
  Http.download(链接,(路径与名称),nil,nil,function(code,content)
    if 下载完后的事件==nil then
     else
      function 主代码()
        执行代码(下载完后的事件)
      end
      function a() print("错误")end
      xpcall(主代码,a)
    end
  end)
end
function 必应图(i,j,callback)
  dl=ProgressDialog.show(activity,nil,'正在拼命加载…').show()
  local url="http://cn.bing.com/HPImageArchive.aspx?format=js&idx="..i.."&n=8"
  Http.get(url,nil,"utf8",nil,function(code,content,cookie,header)
    if(code==200 and content)then
      local json=cjson.decode(content)
      te1.Text="\t\t"..(json.images[j].copyright)
      te2.Text=(json.images[j].enddate)
      api="http://s.cn.bing.net"..(json.images[j].url)
      tupian=loadbitmap(api)--加载图片
      callback(tostring(tupian))
     else
      task(250, function() print("连接失败,请检查网络"..code) end)
    end
  end)
end
function 圆形按钮(view,InsideColor,radiu)
  import "android.graphics.drawable.GradientDrawable"
  drawable = GradientDrawable()
  drawable.setShape(GradientDrawable.RECTANGLE)
  drawable.setColor(InsideColor)
  drawable.setCornerRadii({radiu,radiu,radiu,radiu,radiu,radiu,radiu,radiu});
  view.setBackgroundDrawable(drawable)
end
function 纽扣架(view,Thickness,yji,FrameColor,InsideColor)
  import "android.graphics.drawable.GradientDrawable"
  drawable = GradientDrawable()
  drawable.setShape(GradientDrawable.RECTANGLE)
  drawable.setStroke(Thickness, FrameColor)
  drawable.setColor(InsideColor)
  drawable.setCornerRadius(yji)
  view.setBackgroundDrawable(drawable)
end
function 通知(链接,标题,内容)
  import "android.net.Uri"
  import "android.graphics.BitmapFactory"
  mBuilder = Notification.Builder(this);
  mIntent = Intent(Intent.ACTION_VIEW,Uri.parse(链接));
  pendingIntent = PendingIntent.getActivity(activity.getApplicationContext(),0,mIntent,0);
  mBuilder.setContentIntent(pendingIntent);
  mBuilder.setSmallIcon(R.drawable.icon);
  mBuilder.setLargeIcon(BitmapFactory.decodeResource(activity.getApplicationContext().getResources(),R.drawable.icon));
  mBuilder.setAutoCancel(true);
  mBuilder.setContentTitle(标题);
  mBuilder.setContentText(内容)
  hangIntent = Intent();
  hangIntent.setFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
  hangPendingIntent = PendingIntent.getActivity(activity.getApplicationContext(), 0, hangIntent, PendingIntent.FLAG_CANCEL_CURRENT);
  mBuilder.setFullScreenIntent(hangPendingIntent, true);
  NotificationManager = activity.getSystemService(Context.NOTIFICATION_SERVICE);
  NotificationManager.notify(3, mBuilder.build());
end
function 网络环境()
  onReceive=function(c,intent)
    if intent.getAction()==ConnectivityManager.CONNECTIVITY_ACTION then
      cm =this.getSystemService(Context.CONNECTIVITY_SERVICE)
      info=cm.getActiveNetworkInfo()
      if info~=nil then
        types=info.getType()
        switch types
         case 0
          print("移动网络")
         case 1
          return ("wifi环境")
        end
       else
        return ("无网络")
      end
    end
  end
end
function 获取歌曲信息(data)
  import "android.media.MediaMetadataRetriever"
  import "java.io.File"
  data = File(data)
  mmr=MediaMetadataRetriever()
  mmr.setDataSource(data.getAbsolutePath())
  ablumString=mmr.extractMetadata(MediaMetadataRetriever.METADATA_KEY_ALBUM);--获得音乐专辑的标题
  artistString=mmr.extractMetadata(MediaMetadataRetriever.METADATA_KEY_ARTIST);--获取音乐的艺术家信息
  titleString=mmr.extractMetadata(MediaMetadataRetriever.METADATA_KEY_TITLE);--获取音乐标题信息
  mimetypeString=mmr.extractMetadata(MediaMetadataRetriever.METADATA_KEY_MIMETYPE);--获取音乐mime类型
  durationString=mmr.extractMetadata(MediaMetadataRetriever.METADATA_KEY_DURATION);--获取音乐持续时间
  bitrateString=mmr.extractMetadata(MediaMetadataRetriever.METADATA_KEY_BITRATE);--获取音乐比特率，位率
  dateString=mmr.extractMetadata(MediaMetadataRetriever.METADATA_KEY_DATE);--获取音乐的日期
end
function 转分辨率(sdp)
  import "android.util.TypedValue"
  dm=this.getResources().getDisplayMetrics()
  types={px=0,dp=1,sp=2,pt=3,["in"]=4,mm=5}
  n,ty=sdp:match("^(%-?[%.%d]+)(%a%a)$")
  return TypedValue.applyDimension(types[ty],tonumber(n),dm)
end
function 打开光照()
  local CameraManager=this.getSystemService(Context.CAMERA_SERVICE)
  local isOn=false
  function toggleLight(bool)
    pcall(function()CameraManager.setTorchMode("0",bool)end)
    if bool then
     else
    end
  end
  isOn=true
  toggleLight(isOn)
end
function 关闭光照()
  local CameraManager=this.getSystemService(Context.CAMERA_SERVICE)
  local isOn=false
  function toggleLight(bool)
    pcall(function()CameraManager.setTorchMode("0",bool)end)
    if bool then
     else
    end
  end
  isOn=false
  toggleLight(isOn)
end
function 判断字符串类型(字符串)
  return type(字符串)
end
function 安装判断(a)--a是包名
  if pcall(function() activity.getPackageManager().getPackageInfo(a,0) end) then
    return "安装完毕"
   else
    return "未安装"
  end
end
function 透明动画(id,时间,开始透明度,结束透明度)
  if 开始透明度==1 then
    id.setVisibility(0)
  end
  id.startAnimation(AlphaAnimation(开始透明度,结束透明度).setDuration(时间))
  task(时间, function ()
    if 结束透明度==0 then
      id.setVisibility(8)
    end
  end)
end
function 获取webview位图()
  scale = webView.getScale();
  width = webView.getWidth();
  height = webView.getContentHeight() * scale;
  bitmap = Bitmap.createBitmap(width, height, Bitmap.Config.ARGB_4444);
  canvas = Canvas(bitmap);
  webView.draw(canvas);
  return webbm
end
function 设置透明度(id, alpha)
  if pcall(function()
      id.getBackground().setAlpha(alpha)
    end) then
   else
    error({code="id or alpha error"})
  end
end
function 随机色()
  math.randomseed(os.clock()*1000000)
  local r=string.format("%02X",math.random(0,255))
  math.randomseed(os.clock()*1000000)
  local g=string.format("%02X",math.random(0,255))
  math.randomseed(os.clock()*1000000)
  local b=string.format("%02X",math.random(0,255))
  return "#FF"..r..g..b
end
function 缩放动画(id,time,FromX,ToX,FromY,ToY,FXtype,TXtype,FYtype,TYtype)
  id.startAnimation(ScaleAnimation(FromX, ToX, FromY, ToY, FXtype, TXtype, FYtype, TYtype).setDuration(time))
end
function 旋转动画(id,时间,开始角度,结束角度,x缩放模式,x缩放值,y缩放模式,y缩放值)
  id.startAnimation(RotateAnimation(开始角度, 结束角度,
  x缩放模式, x缩放值,
  y缩放模式, y缩放值).setDuration(时间))
end
function 设置Cookie(context,url,content)
  CookieSyncManager.createInstance(context)
  local cookieManager = CookieManager.getInstance()
  cookieManager.setAcceptCookie(true)
  cookieManager.removeSessionCookie()
  cookieManager.removeAllCookie()
  cookieManager.setCookie(url, content)
  CookieSyncManager.getInstance().sync()
end
function 获取Cookie(url)
  local cookieManager = CookieManager.getInstance();
  return cookieManager.getCookie(url);
end
function 屏蔽网页元素(id,table)
  for i,V in pairs(table) do
    加载Js(id,[[var remove=n=>{n.split(",").forEach(v=>{if(v.indexOf("@ID(")==0){document.getElementById(v.substring(4,v.length-1)).style.display="none"}else{for(let e of document.getElementsByClassName(v))e.style.display="none"}})}
remove("]]..V..[[")]])
  end
end
function MD5字符串(str)
  local md5 = MessageDigest.getInstance("MD5")
  local bytes = md5.digest(String(str).getBytes())
  local result = ""
  for i=0,#bytes-1 do
    local temp = string.format("%02x",(bytes[i] & 0xff))
    return result..temp
  end
end
function Http_upload(ur,name,f,zhacai)
  client = OkTest.newok()
  f=File(f)
  requestBody = MultipartBody.Builder()
  .setType(MultipartBody.FORM)
  .addFormDataPart(name,tostring(f.Name),RequestBody.create(MediaType.parse("multipart/form-data"), f))
  .build()

  request = Request.Builder()
  .header("User-Agent","Dalvik/2.1.0 (Linux; U; Android 9.0; MI MIX Alpha)")
  .url(ur)
  .post(requestBody)
  .build();

  client.newCall(request).enqueue(Callback{
    onFailure=function(call, e)--请求失败
      zhacai("","","")
    end,
    onResponse=function(call, response)--请求成功
      code=response.code()--.toString()
      header=response.headers()
      data=String(response.body().bytes()).toString()
      zhacai(code,data,header)
    end
  });
end
function 获得屏幕物理尺寸(ctx)
  import "android.util.DisplayMetrics"
  dm = DisplayMetrics();
  ctx.getWindowManager().getDefaultDisplay().getMetrics(dm);
  diagonalPixels = Math.sqrt(Math.pow(dm.widthPixels, 2) + Math.pow(dm.heightPixels, 2));
  return diagonalPixels / (160 * dm.density);
end
function 复制文件(from,to)
  xpcall(function()
    LuaUtil.copyDir(from,to)
  end,function()
    print("复制文件 从 "..from.." 到 "..to.." 失败")
  end)
end
function 删除文件(file)
  xpcall(function()
    LuaUtil.rmDir(File(file))
  end,function()
    print("删除文件(夹) "..file.." 失败")
  end)
end
function 获取系统夜间模式()
  _,Re=xpcall(function()
    import "android.content.res.Configuration"
    currentNightMode = activity.getResources().getConfiguration().uiMode & Configuration.UI_MODE_NIGHT_MASK
    return currentNightMode == Configuration.UI_MODE_NIGHT_YES--夜间模式启用
  end,function()
    return false
  end)
  return Re
end
function 关闭对话框(en)
  if en then
    en.dismiss()
  end
end
function 弹窗圆角(控件,背景色,上角度,下角度)
  if not 上角度 then
    上角度=25
  end
  if not 下角度 then
    下角度=上角度
  end
  控件.setBackgroundDrawable(GradientDrawable()
  .setShape(GradientDrawable.RECTANGLE)
  .setColor(背景色)
  .setCornerRadii({上角度,上角度,上角度,上角度,下角度,下角度,下角度,下角度}))
end
function 高级解压(zippath,outfilepath,filename)
  local time=os.clock()
  task(function(zippath,outfilepath,filename)
    require "import"
    import "java.util.zip.*"
    import "java.io.*"
    local file = File(zippath)
    local outFile = File(outfilepath)
    local zipFile = ZipFile(file)
    local entry = zipFile.getEntry(filename)
    local input = zipFile.getInputStream(entry)
    local output = FileOutputStream(outFile)
    local byte=byte[entry.getSize()]
    local temp=input.read(byte)
    while temp ~= -1 do
      output.write(byte)
      temp=input.read(byte)
    end
    input.close()
    output.close()
  end,zippath,outfilepath,filename,
  function()
    print("解压完成，耗时 "..os.clock()-time.." s")
  end)
end
function 对字符串进行URl编码(字符)
  local str=URLEncoder.encode(字符,'utf-8')
  return str
end
function 动态申请全部权限()
  import "android.content.pm.PackageManager"
  local mAppPermissions = ArrayList()
  local mAppPermissionsTable = luajava.astable(activity.getPackageManager().getPackageInfo(activity.getPackageName(),PackageManager.GET_PERMISSIONS).requestedPermissions)
  for k,v in pairs(mAppPermissionsTable) do
    mAppPermissions.add(v)
  end
  local size = mAppPermissions.size()
  local mArray = mAppPermissions.toArray(String[size])
  activity.requestPermissions(mArray,0)
end
function 加载Js(view,JavaScript代码)
  view.evaluateJavascript(JavaScript代码,nil)
end
local bindClass=luajava.bindClass
local AlertDialog=bindClass("android.app.AlertDialog")
local Builder=AlertDialog.Builder
local indexBuilderPool={}
function 对话框2(ctx)
  local index=#indexBuilderPool+1
  local dialog=AlertDialog.Builder(ctx or activity or this)
  indexBuilderPool[index]=dialog
  local _M
  _M= {
    ["设置标题"]=function(...) dialog.setTitle(...) return _M end;
    ["设置消息"]=function(...) dialog.setMessage(...) return _M end;
    ["设置积极按钮"]=function(...)
      local args={...}
      if (#args==1) then
        dialog.setPositiveButton(args[1],nil)
       else
        dialog.setPositiveButton(...)
      end
      return _M end;
    ["设置消极按钮"]=function(...)
      local args={...}
      if (#args==1) then
        dialog.setNegativeButton(args[1],nil)
       else
        dialog.setNegativeButton(...)
      end
      return _M end;
    ["设置中立按钮"]=function(...)
      local args={...}
      if (#args==1) then
        dialog.setNeutralButton(args[1],nil)
       else
        dialog.setNeutralButton(...)
      end
      return _M end;
    ["显示"]=function(...)
      dialog=dialog.show(...)
      indexBuilderPool[index]=dialog
      return _M end;
    ["创建"]=function(...)
      dialog=dialog.create(...)
      indexBuilderPool[index]=dialog
      return _M end;
    ["取消"]=function(...) dialog.cancel(...) return _M end;
    ["关闭"]=function(...)
      dialog.dismiss(...)
      luajava.clear(indexBuilderPool[index])
      indexBuilderPool[index]=true
      return _M end;
    ["dismiss"]=function(...)
      dialog.dismiss(...)
      luajava.clear(indexBuilderPool[index])
      indexBuilderPool[index]=true
      return _M end;
    ["隐藏"]=function(...) dialog.hide(...) return _M end;
    ["设置按钮"]=function(...) dialog.setButton(...) return _M end;
    ["设置按钮1"]=function(...) dialog.setButton(...) return _M end;
    ["设置按钮2"]=function(...) dialog.setButton(...) return _M end;
    ["设置按钮3"]=function(...) dialog.setButton(...) return _M end;
    ["设置视图"]=function(...) dialog.setView(...) return _M end;
    ["设置图标"]=function(...) dialog.setIcon(...) return _M end;
    ["设置是否可以取消"]=function(...) dialog.setCancelable(...) return _M end;
    ["设置项目"]=function(...) dialog.setItems(...) return _M end;
    ["设置多选项目"]=function(...) dialog.setMultiChoiceItems(...) return _M end;
    ["设置取消监听器"]=function(...) dialog.setOnCancelListener(...) return _M end;
    ["设置关闭监听器"]=function(...) dialog.setOnDismissListener(...) return _M end;
    ["设置按键监听器"]=function(...) dialog.setOnKeyListener(...) return _M end;
    ["设置项目选择监听器"]=function(...) dialog.setOnItemSelectedListener(...) return _M end;
    ["启用测量时设置回收"]=function(...) dialog.setRecycleOnMeasureEnabled(...) return _M end;
    ["设置简单选择项目"]=function(...) dialog.setSingleChoiceItems(...) return _M end;
    ["设置自定义标题"]=function(...) dialog.setCustomTitle(...) return _M end;
    ["设置适配器"]=function(...) dialog.setAdapter(...) return _M end;
    ["设置光标"]=function(...) dialog.setCursor(...) return _M end;
    ["设置图标属性"]=function(...) dialog.setIconAttribute(...) return _M end;
    ["设置背景强制反向"]=function(...) dialog.setInverseBackgroundForced(...) return _M end;
    ["获得按钮"]=function(...) dialog.getButton(...) return _M end;
    ["获得列表视图"]=function(...) dialog.getListView(...) return _M end;
    ["当键按下时"]=function(...) dialog.onKeyDown(...) return _M end;
    ["当键抬起时"]=function(...) dialog.onKeyUp(...) return _M end;
    ["添加内容视图"]=function(...) dialog.addContentView(...) return _M end;
    ["设置内容视图"]=function(...) dialog.setContentView(...) return _M end;
    ["关闭选项菜单"]=function(...) dialog.closeOptionsMenu(...) return _M end;
    ["是否正在显示"]=function(...) dialog.isShowing(...) return _M end;
    ["获得窗口"]=function(...) dialog.getWindow(...) return _M end;
    ["设置能否在点击外部后取消"]=function(...) dialog.setCanceledOnTouchOutside(...) return _M end;
    ["设置取消消息"]=function(...) dialog.setCancelMessage(...) return _M end;
  }
  setmetatable(_M,{
    ["__index"]=function(_M,method,...)
      _M[method]=function(...)
        local ok,arg=pcall(function()return _System.getField(method)end)
        if ok then
          _M[method]= arg.get(dialog)
         else
          dialog[method](...)
        end
        return _M
      end
      return _M[method]
    end
  })
  return _M
end
function 泡沫对话框2(ctxOrnum,num)
  if num==nil then
    num=ctxOrnum
    ctxOrnum=nil
  end
  local token="|"..tostring(tointeger(num))
  local OneTimeDialogMark=activity.getSharedData("ONE-TIME-DIALOG-MARK")
  if OneTimeDialogMark==nil then
    OneTimeDialogMark="|"
    activity.setSharedData("ONE-TIME-DIALOG-MARK",OneTimeDialogMark)
  end
  if OneTimeDialogMark:find(token,0,true) then
    local _M={}
    setmetatable(_M,{
      ["__index"]=function(_M,method,...)
        _M[method]=function(...) return _M end
        return _M[method]
      end
    })
    return _M
  end
  OneTimeDialogMark=OneTimeDialogMark..token
  local basedialog=对话框2(ctxOrnum)
  local func1=basedialog["显示"]
  local func2=basedialog["show"]
  basedialog["显示"]=function(...)
    func1(...)
    activity.setSharedData("ONE-TIME-DIALOG-MARK",OneTimeDialogMark)
  end
  basedialog["show"]=function(...)
    func2(...)
    activity.setSharedData("ONE-TIME-DIALOG-MARK",OneTimeDialogMark)
  end
  return basedialog
end
function 拨号(电话号码)
  uri = Uri.parse("tel:"..电话号码);
  intent = Intent(Intent.ACTION_CALL,uri);
  intent.setAction("android.intent.action.VIEW");
  activity.startActivity(intent);
end
function 字体(t)
  return Typeface.createFromFile(File(activity.getLuaDir().."/res/"..t..".ttf"))
end
function 揭露动画(v,centerX,centerY,startRadius,endRadius,time)
  animator = ViewAnimationUtils.createCircularReveal(v,centerX,centerY,startRadius,endRadius);
  animator.setInterpolator(AccelerateInterpolator());
  animator.setDuration(time);
  animator.start();
end
function 申请权限(权限)
  ActivityCompat.requestPermissions(this,权限,1)
end
function 颜色字体(t,c)
  local sp = SpannableString(t)
  sp.setSpan(ForegroundColorSpan(转0x(c)),0,#sp,Spannable.SPAN_EXCLUSIVE_INCLUSIVE)
  return sp
end
function 设置Cookie(context,url,content)
  CookieSyncManager.createInstance(context)
  local cookieManager = CookieManager.getInstance()
  cookieManager.setAcceptCookie(true)
  cookieManager.removeSessionCookie()
  cookieManager.removeAllCookie()
  cookieManager.setCookie(url, content)
  CookieSyncManager.getInstance().sync()
end
function 点击元素(Class名, 索引)
  if 索引 then
    加载Js("var element=document.getElementsByClassName(" .. Class名 .. ")[" .. 索引 .. "] if(typeof(element.onclick)=='undefined'){element.click()}else{element.onclick()}")
   else
    加载Js("var element=document.getElementsByClassName(" .. Class名 .. ")[0] if(typeof(element.onclick)=='undefined'){element.click()}else{element.onclick()}")
  end
end
function 返回网页顶部()
  加载Js("scrollTo(0,0)")
end
function 两角圆弧(id,颜色,角度)
  drawable = GradientDrawable()
  drawable.setColor(颜色)
  drawable.setCornerRadii({角度,角度,角度,角度,0,0,0,0});
  id.setBackgroundDrawable(drawable)
end
function 搜索应用(包名)
  intent = Intent("android.intent.action.VIEW")
  intent .setData(Uri.parse( "market://details?id="..包名))
  activity.startActivity(intent)
end
function 调用系统浏览器搜索(搜索内容)
  intent = Intent()
  intent.setAction(Intent.ACTION_WEB_SEARCH)
  intent.putExtra(SearchManager.QUERY,搜索内容)
  activity.startActivity(intent)
end
function 提示6(文本内容)
  if 文本内容~=nil then
    xxxx={
      LinearLayout;
      {
        LinearLayout;
        {
          CardView;
          radius='5dp';
          elevation='5dp';
          layout_height='wrap_content';
          layout_margin='18dp';
          layout_width=activity.getWidth()/1.1;
          CardBackgroundColor="#FF202124",
          {
            TextView;
            textColor='#ffffff';
            layout_marginTop='15dp';
            layout_marginBottom='15dp';
            layout_margin='15dp';
            text=文本内容;
          };
        };
      };
    };
    布局=loadlayout(xxxx)
    local toast=Toast.makeText(activity,"提示",Toast.LENGTH_SHORT).setGravity(Gravity.BOTTOM, 0, 0).setView(布局).show()
  end
end
function 调用系统浏览器打开链接(网页链接)
  viewIntent = Intent("android.intent.action.VIEW",Uri.parse(网页链接))
  activity.startActivity(viewIntent)
end
function 有道翻译(content)
  import "http"
  url="http://m.youdao.com/translate"
  data="inputtext="..content.."&type=EN2ZH_CN"
  body,cookie,code,headers=http.post(url,data)
  for v in body:gmatch('<ul id="translateResult">(.-)</ul>') do
    x=v:match('<li>(.-)</li>')
    x=x:match"^%s*(.-)%s*$"
    return v
  end
end
function activity背景颜色(color)
  import "android.graphics.drawable.ColorDrawable"
  _window = activity.getWindow();
  _window.setBackgroundDrawable(ColorDrawable(color));
  _wlp = _window.getAttributes();
  _wlp.gravity = Gravity.BOTTOM;
  _wlp.width = WindowManager.LayoutParams.MATCH_PARENT;
  _wlp.height = WindowManager.LayoutParams.MATCH_PARENT;--WRAP_CONTENT
  _window.setAttributes(_wlp);
end
function 控件边框(id,r,t,y)--控件的边框
  import "android.graphics.Color"
  InsideColor = Color.parseColor(t)
  import "android.graphics.drawable.GradientDrawable"
  drawable = GradientDrawable()
  drawable.setShape(GradientDrawable.RECTANGLE)
  --设置填充色
  drawable.setColor(InsideColor)
  --设置圆角 : 左上 右上 右下 左下
  drawable.setCornerRadii({r, r, r, r, r, r, r, r});
  --设置边框 : 宽度 颜色
  drawable.setStroke(2, Color.parseColor(y))
  view.setBackgroundDrawable(drawable)
  --例:控件边框(bt,5,"#ffffffff","#42A5F5")--id，度数，内框透明，边框颜色
end
function 清除应用数据()
  os.execute("pm clear "..activity.getPackageName())
end
function 悬浮按钮(ID,父布局,图片,颜色,边距,事件)
  布局= {
    RelativeLayout;
    layout_height="match_parent";
    layout_width="match_parent";
    {
      CardView;
      layout_alignParentBottom="true";
      layout_width="56dp";
      layout_height="56dp";
      backgroundColor=颜色;
      layout_alignParentRight="true";
      layout_margin=边距;
      CardElevation="4dp";
      radius="28dp";
      id=ID;
      {
        LinearLayout;
        layout_width="74dp";
        layout_height="74dp";
        style="?android:attr/buttonBarButtonStyle";
        id="button";
        layout_gravity="center";
        onClick=事件;
        {
          ImageView;
          layout_width="25dp";
          layout_height="25dp";
          src=图片;
          layout_gravity="center";
          colorFilter="#ffffffff";
        };
      };
    };
  };
  父布局.addView(loadlayout(布局))
end
function 打印网速()
  function update(s)
    print(s.."k/s")
  end
  function f()
    require "import"
    import "android.net.TrafficStats"
    s=TrafficStats.getTotalRxBytes()
    Thread.sleep(500)
    call("update",(TrafficStats.getTotalRxBytes()-s)*2/1000)
  end
  timer(f,0,1000)
end
function 获取控件颜色(id)
  import "android.graphics.drawable.ColorDrawable"
  a = id.getBackground().mutate().getColor()
  return (4294967296+a)
end
function 设置主题黑色()
  activity.setTheme(android.R.style.Theme_DeviceDefault_Wallpaper_NoTitleBar)
end
function 设置主题白色()
  activity.setTheme(android.R.style.Theme_DeviceDefault_Light_NoActionBar)
end

function 浏览器(id,链接即将跳转事件,网页加载前事件,网页加载完成)
  id.setWebViewClient{
    shouldOverrideUrlLoading=function(view,url)
      pcall(loadstring(链接即将跳转事件))
    end,
    onPageStarted=function(view,url,favicon)
      pcall(loadstring(网页加载前事件))
    end,
    onPageFinished=function(view,url)
      pcall(loadstring(网页加载完成))
    end}
end
function 取小数点后面的n位(d,n)
  import "java.math.BigDecimal"
  b = BigDecimal(d)
  return b.setScale(n,BigDecimal.ROUND_HALF_UP).doubleValue()
end
function dp2px(dpValue)
  local scale = activity.getResources().getDisplayMetrics().scaledDensity
  return dpValue * scale + 0.5
end
function px2dp(pxValue)
  local scale = activity.getResources().getDisplayMetrics().scaledDensity
  return pxValue / scale + 0.5
end
function 高斯模糊(id,tp,radius1,radius2)
  function blur( context, bitmap, blurRadius)
    renderScript = RenderScript.create(context);
    blurScript = ScriptIntrinsicBlur.create(renderScript, Element.U8_4(renderScript));
    inAllocation = Allocation.createFromBitmap(renderScript, bitmap);
    outputBitmap = bitmap;
    outAllocation = Allocation.createTyped(renderScript, inAllocation.getType());
    blurScript.setRadius(blurRadius);
    blurScript.setInput(inAllocation);
    blurScript.forEach(outAllocation);
    outAllocation.copyTo(outputBitmap);
    inAllocation.destroy();
    outAllocation.destroy();
    renderScript.destroy();
    blurScript.destroy();
    return outputBitmap;
  end
  function zoomBitmap(bitmap,scale)
    w = bitmap.getWidth();
    h = bitmap.getHeight();
    matrix = Matrix();
    matrix.postScale(scale, scale);
    bitmap = Bitmap.createBitmap(bitmap, 0, 0, w, h, matrix, true);
    return bitmap;
  end
  function blurAndZoom(context,bitmap,blurRadius,scale)
    return zoomBitmap(blur(context,zoomBitmap(bitmap, 1 / scale), blurRadius), scale);
  end
  id.setImageBitmap(blurAndZoom(activity,tp,radius1,radius2))
end
function 颜色字体(t,c)
  local sp = SpannableString(t)
  sp.setSpan(ForegroundColorSpan(c),0,#sp,Spannable.SPAN_EXCLUSIVE_INCLUSIVE)
  return sp
end
function 圆形图片(bitmap)
  import "android.graphics.PorterDuffXfermode"
  import "android.graphics.Paint"
  import "android.graphics.RectF"
  import "android.graphics.Bitmap"
  import "android.graphics.PorterDuff$Mode"
  import "android.graphics.Rect"
  import "android.graphics.Canvas"
  import "android.util.Config"
  width = bitmap.getWidth()
  output = Bitmap.createBitmap(width, bitmap.getHeight(),Bitmap.Config.ARGB_8888)
  canvas = Canvas(output);
  color = 0xff424242;
  paint = Paint()
  rect = Rect(0, 0, bitmap.getWidth(), bitmap.getHeight());
  rectF = RectF(rect);
  paint.setAntiAlias(true);
  canvas.drawARGB(0, 0, 0, 0);
  paint.setColor(color);
  canvas.drawRoundRect(rectF, width/2, bitmap.getHeight()/2, paint);
  paint.setXfermode(PorterDuffXfermode(Mode.SRC_IN));
  canvas.drawBitmap(bitmap, rect, rect, paint);
  return output;
end
function 全屏()
  window = activity.getWindow();
  window.getDecorView().setSystemUiVisibility(View.SYSTEM_UI_FLAG_FULLSCREEN|View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN);
  window.addFlags(WindowManager.LayoutParams.FLAG_FULLSCREEN)
  xpcall(function()
    lp = window.getAttributes();
    lp.layoutInDisplayCutoutMode = WindowManager.LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_SHORT_EDGES;
    window.setAttributes(lp);
  end,
  function(e)
  end)
end
function 调用图库(callback)
  import "android.content.Intent"
  local intent= Intent(Intent.ACTION_PICK)
  intent.setType("image/*")
  this.startActivityForResult(intent, 1)
  function onActivityResult(requestCode,resultCode,intent)
    if intent then
      local cursor =this.getContentResolver ().query(intent.getData(), nil, nil, nil, nil)
      cursor.moveToFirst()
      import "android.provider.MediaStore"
      local idx = cursor.getColumnIndex(MediaStore.Images.ImageColumns.DATA)
      fileSrc = cursor.getString(idx)
      bit=nil
      import "android.graphics.BitmapFactory"
      图片路径=fileSrc
      图片=BitmapFactory.decodeFile(fileSrc)
      callback(tostring(图片路径))
    end
  end
end
function 调用图库选择视频(callback)
  import "android.content.Intent"
  local intent= Intent(Intent.ACTION_PICK)
  intent.setType("video/*")
  this.startActivityForResult(intent, 1)
  function onActivityResult(requestCode,resultCode,intent)
    if intent then
      local cursor =this.getContentResolver ().query(intent.getData(), nil, nil, nil, nil)
      cursor.moveToFirst()
      import "android.provider.MediaStore"
      local idx = cursor.getColumnIndex(MediaStore.Images.ImageColumns.DATA)
      fileSrc = cursor.getString(idx)
      bit=nil
      import "android.graphics.BitmapFactory"
      图片路径=fileSrc
      图片=BitmapFactory.decodeFile(fileSrc)
      callback(tostring(图片路径))
    end
  end
end
function shell脚本(cmd)
  local p=io.popen(string.format('%s',cmd))
  local s=p:read("*a")
  p:close()
  return s
end
function 高级波纹(id,lx,color)
  xpcall(function()
    ripple = activity.obtainStyledAttributes({android.R.attr.selectableItemBackgroundBorderless}).getResourceId(0,0)
    ripples = activity.obtainStyledAttributes({android.R.attr.selectableItemBackground}).getResourceId(0,0)
    for index,content in ipairs(id) do
      if lx=="圆" then
        content.setBackgroundDrawable(activity.Resources.getDrawable(ripple).setColor(ColorStateList(int[0].class{int{}},int{color})))
      end
      if lx=="方" then
        content.setBackgroundDrawable(activity.Resources.getDrawable(ripples).setColor(ColorStateList(int[0].class{int{}},int{color})))
      end
    end
  end,function(e)end)
end
function px2sp(pxValue)
  local scale = activity.getResources().getDisplayMetrics().scaledDensity;
  return pxValue / scale + 0.5
end
function 退出页面()
  activity.finish()
end
function sp2px(spValue)
  local scale = activity.getResources().getDisplayMetrics().scaledDensity
  return spValue * scale + 0.5
end
function 宇宙超级噼里啪啦的卡的退出()
  os.exit()
end
function 执行代码(代码)
  pcall(loadstring(代码))
end
function 去除注释(str)
  local str_arr={}
  local len=utf8.len(str)
  local pos=0
  local delta
  local function eat()
    pos=pos+1
    if pos>len then
      return false
    end
    return true
  end
  local function look(delta)
    delta = pos + (delta or 0)
    return utf8.sub(str,delta, delta)
  end
  local function get()
    eat()
    return look()
  end

  local function skip()
    eat()
    if look()=="[" and get()=="[" then
      while(true)do
        if (look()=="]" and get()=="]")or( not eat() )then

          break
        end
      end
     else
      while(true)do
        if look()=="\n" or (not eat()) then
          break
        end
      end
    end
  end

  while (pos<len) do
    local t=look()
    if t=="-" and get()=="-" then
      skip()
     else
      str_arr[#str_arr+1]=t
    end
    eat()
  end
  return table.concat(str_arr)
end
function 代码高亮(edit,要高亮的代码)
  for k,v in pairs(_G) do
    要高亮的代码[k]=0xff0000ff
  end
  function highlight(ss)
    local str = tostring(ss);
    for s,e,c in str:gfind("(%w+)") do
      local clr=要高亮的代码[c]
      if clr then
        ss.setSpan(ForegroundColorSpan(clr), utf8.len(str,1,s)-1, utf8.len(str,1,e), Spanned.SPAN_EXCLUSIVE_EXCLUSIVE);
      end
    end
    return ss
  end
  edit.addTextChangedListener{
    onTextChanged=function()
      highlight(edit.getText())
    end}
  highlight(edit.getText())
end
function 播放视频(直链)--看到好多人用file://文件路径试了一下会报错所以可能这个只支持绝对链接
  import "android.content.Intent"
  import "android.net.Uri"
  intent = Intent(Intent.ACTION_VIEW)
  uri = Uri.parse(直链)
  intent.setDataAndType(uri,"video/mp4")
  activity.startActivity(intent)
end
function 获取签名(包名)
  return tostring(this.getPackageManager().getPackageInfo(this.getPackageName(),((32552732/2/2-8183)/10000-6-231)/9).signatures[(32552732+0+32552732-(32552732*2))].toCharsString())
end
function 打开系统设置()
  return Intent(Settings.ACTION_SETTINGS)
end
import "android.animation.Animator"
SnackerBar={shouldDismiss=true}
import "android.animation.ValueAnimator"
local w=activity.width
import "android.view.animation.AccelerateDecelerateInterpolator"
local layout={
  LinearLayout,
  Gravity="bottom",
  {
    LinearLayout,
    layout_height=-2,
    layout_width=-1,
    Gravity="center",
    BackgroundColor=0xff333333,
    {
      TextView,
      textColor=0xffffffff,
      layout_weight=.8,
      paddingLeft="10dp",
      paddingTop="5dp",
      paddingBottom="5dp",
      layout_width=0,
    },
    {
      Button,
      layout_height=-2,
      style="?android:attr/buttonBarButtonStyle",
      text="DONE",
    }
  }
}
local function addView(view)
  local mLayoutParams=ViewGroup.LayoutParams
  (-1,-1)
  activity.Window.DecorView.addView(view,mLayoutParams)
end
local function removeView(view)
  activity.Window.DecorView.removeView(view)
end
--设置提示文字
function SnackerBar:msg(textMsg)
  self.textView.text=textMsg
  return self
end
--设置按钮文字
function SnackerBar:actionText(textAction)
  self.actionView.text=textAction
  return self
end
--设置按钮动作
function SnackerBar:action(func)
  self.actionView.onClick=
  function()
    func()
    self:dismiss()
  end
  return self
end
--显示
function SnackerBar:show()
  local view=self.view
  addView(view)
  view.translationY=300
  view.animate().translationY(0)
  .setInterpolator(AccelerateDecelerateInterpolator())
  .setDuration(400).start()
  indefiniteDismiss(self)
end
function indefiniteDismiss(snackerBar)
  task(2000,function()
    if snackerBar.shouldDismiss==true then
      snackerBar:dismiss()
     else
      indefiniteDismiss(snackerBar)
    end
  end)
end
--关闭
function SnackerBar:dismiss()
  local view=self.view
  view.animate().translationY(300)
  .setDuration(400)
  .setListener(Animator.AnimatorListener{
    onAnimationEnd=function()
      removeView(view)
    end
  }).start()
end
SnackerBar.__index=SnackerBar
--构建者模式
function SnackerBar.build()
  local mSnackerBar={}
  setmetatable(mSnackerBar,SnackerBar)
  mSnackerBar.view=loadlayout(layout)
  mSnackerBar.bckView=mSnackerBar.view
  .getChildAt(0)
  mSnackerBar.textView=mSnackerBar.bckView
  .getChildAt(0)
  mSnackerBar.actionView=mSnackerBar.bckView
  .getChildAt(1)
  local function animate(v,tx,dura)
    ValueAnimator().ofFloat({v.translationX,tx}).setDuration(dura)
    .addUpdateListener( ValueAnimator.AnimatorUpdateListener
    {
      onAnimationUpdate=function( p1)
        local f=p1.animatedValue
        v.translationX=f
        v.alpha=1-math.abs(v.translationX)/w
      end
    }).addListener(ValueAnimator.AnimatorListener{
      onAnimationEnd=function()
        if math.abs(tx)>=w then
          removeView(mSnackerBar.view)
        end
      end
    }).start()
  end
  local frx,p,v,fx=0,0,0,0
  mSnackerBar.bckView.setOnTouchListener(View.OnTouchListener{
    onTouch=function(view,event)
      if event.Action==event.ACTION_DOWN then
        mSnackerBar.shouldDismiss=false
        frx=event.x
        fx=event.x
       elseif event.Action==event.ACTION_MOVE then
        if math.abs(event.rawX-frx)>=2 then
          v=math.abs((frx-event.rawX)/(os.clock()-p)/1000)
        end
        p=os.clock()
        frx=event.rawX
        view.translationX=frx-fx
        view.alpha=1-math.abs(view.translationX)/w
       elseif event.Action==event.ACTION_UP then
        mSnackerBar.shouldDismiss=true
        local tx=view.translationX
        if tx>=w/5 then
          animate(view,w,(w-tx)/v)
         elseif tx>0 and tx<w/5 then
          animate(view,0,tx/v)
         elseif tx<=-w/5 then
          animate(view,-w,(w+tx)/v)
         else
          animate(view,0,-tx/v)
        end
        fx=0
      end
      return true
    end
  })
  return mSnackerBar
end
function 加载gif图片(id,file)
  id.background=LuaBitmapDrawable(file)
end
function 编译文件(路径)
  a=string.dump(loadstring(io.open(路径):read("*a")))
  io.open(路径..".Lua","w"):write(a):close()
end
function 设置权限()
  local intent=Intent("android.settings.APPLICATION_DETAILS_SETTINGS");
  intent.setData(Uri.fromParts("package",activity.getPackageName(),null));
  intent.setFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
  activity.startActivity(intent);
end
function 获取ImageView的图片(id)
  id.setDrawingCacheEnabled(true);
  return id.getDrawingCache();
end
function 文件监听变化(路径)
  lfo=LuaFileObserver(路径).startWatching()
  lfo.onEvent=function(监听事件的类型,被改动的文件名称)
    if 监听事件的类型==LuaFileObserver.ACCESS then
      print(被改动的文件名称.."文件被访问")
     elseif 监听事件的类型==LuaFileObserver.MODIFY then
      print(被改动的文件名称.."文件被改动")
     elseif 监听事件的类型==LuaFileObserver.ATTRIB then
      print(被改动的文件名称.."属性改动")
     else
    end
  end
end
function 保存图片(name,bm)
  if bm then
    name=tostring(name)
    f = File(name)
    out = FileOutputStream(f)
    bm.compress(Bitmap.CompressFormat.PNG,90, out)
    out.flush()
    out.close()
    return true
   else
    return false
  end
end
function 获取壁纸()
  wallpaperManager = WallpaperManager.getInstance(this);
  return wallpaperManager.getDrawable();
end
function 旋转动画(id)
  rotate=RotateAnimation(0,359*1000,Animation.RELATIVE_TO_SELF,0.5,Animation.RELATIVE_TO_SELF,0.5)
  rotate.setDuration(100000)
  rotate.setRepeatCount(0.5)
  rotate.setInterpolator(AccelerateInterpolator())
  id.startAnimation(rotate)
end
function 复制文件(原文件,路径)
  LuaUtil.copyDir(原文件,路径)
end
function 获取文件名称(路径)
  return File(路径).getName()
end
function 获取文件字节(路径)
  return File(路径).length()
end
function 是否文件夹(路径)
  return File(路径).isDirectory()
end
function 获取文件的父文件夹(路径)
  return File(路径).getParentFile()
end
function 是否文件(路径)
  return File(路径).isFile()
end
function 获取文件是否隐藏文件(路径)
  return File(路径).isHidden()
end
function 打开应用详情()
  intent = Intent(Settings.ACTION_APPLICATION_DETAILS_SETTINGS);
  intent.setData(Uri.parse("package:" .. activity.getPackageName()));
  activity.startActivityForResult(intent, 100);
end
function 微信扫一扫()
  intent = Intent()
  intent.setComponent(ComponentName("com.tencent.mm","com.tencent.mm.ui.LauncherUI"))
  intent.putExtra("LauncherUI.From.Scaner.Shortcut",true)
  intent.setFlags(335544320)
  intent.setAction("android.intent.action.VIEW")
  activity.startActivity(intent)
end
function 颜色渐变(控件,左色,右色)
  drawable = GradientDrawable(GradientDrawable.Orientation.TR_BL,{左色,右色,});
  控件.setBackgroundDrawable(drawable)
end
function 三色渐变(控件,左色,中色,右色,圆角)
  drawable = GradientDrawable(GradientDrawable.Orientation.TR_BL,{左色,中色,右色});
  drawable.setCornerRadii({圆角,圆角,圆角,圆角,圆角,圆角,圆角,圆角});
  控件.setBackgroundDrawable(drawable)
end
function 四色渐变(控件,颜色1,颜色2,颜色3,颜色4)
  控件.setBackgroundDrawable(GradientDrawable(GradientDrawable.Orientation.TOP_BOTTOM,{颜色1,颜色2,颜色3,颜色4,}))
end
function 支付宝扫一扫()
  intent = Intent(Intent.ACTION_VIEW,Uri.parse("alipayqr://platformapi/startapp?saId=10000007"))
  activity.startActivity(intent)
end
function 播放音乐(链接)
  mediaPlayer = MediaPlayer()
  mediaPlayer.reset()
  mediaPlayer.setDataSource(链接)
  mediaPlayer.prepare()
  mediaPlayer.setLooping(true)
  mediaPlayer.isPlaying()
  task(1000,function()
    mediaPlayer.start()
  end)
end
function 淡入动画()
  activity.overridePendingTransition(android.R.anim.fade_in,android.R.anim.fade_out)
end
function 禁止截图()
  activity.getWindow().addFlags(WindowManager.LayoutParams.FLAG_SECURE)
end
function 转0x(a)
  if #a==7 then
    aa=a:match("#(.+)")
    aaa=tonumber("0xff"..aa)
   else
    aa=a:match("#(.+)")
    aaa=tonumber("0x"..aa)
  end
  return aaa
end
function 自动旋转()
  activity.setRequestedOrientation(ActivityInfo.SCREEN_ORIENTATION_SENSOR);
end
function 横屏模式()
  activity.setRequestedOrientation(0);
end
function 竖屏模式()
  activity.setRequestedOrientation(1);
end
function 后台运行()
  activity.moveTaskToBack(true)
end
function 截图(截图目录,截图文件名)
  local 截图控件=activity.getDecorView()
  截图控件.setDrawingCacheEnabled(false)
  截图控件.setDrawingCacheEnabled(true)
  截图控件.destroyDrawingCache()
  截图控件.buildDrawingCache()
  local drawingCache=截图控件.getDrawingCache()
  if drawingCache==nil then
    print("截图失败")
   else
    local bitmap=Bitmap.createBitmap(drawingCache)
    local directory=File(截图目录)
    if not directory.exists() then
      directory.mkdirs()
    end
    local file=File(截图目录,截图文件名)
    local fileOutputStream=FileOutputStream(file)
    bitmap.compress(Bitmap.CompressFormat.JPEG,100,fileOutputStream)
    local intent=Intent(Intent.ACTION_MEDIA_SCANNER_SCAN_FILE)
    intent.setData(Uri.fromFile(file))
    activity.sendBroadcast(intent)
    print("已保存到 "..截图目录.."/"..截图文件名)
  end
end
function 控件载入动画(id)
  animationSet = AnimationSet(true)
  layoutAnimationController=LayoutAnimationController(animationSet,0.3);
  layoutAnimationController.setOrder(LayoutAnimationController.ORDER_NORMAL);
  id.setLayoutAnimation(layoutAnimationController);
  animation = AlphaAnimation(0,1);
  animation.setDuration(600);
  animationSet.addAnimation(animation);
  animation=TranslateAnimation(Animation.RELATIVE_TO_PARENT, 1, Animation.RELATIVE_TO_SELF, 0, Animation.RELATIVE_TO_SELF, 0, Animation.RELATIVE_TO_SELF, 0);
  animation.setDuration(1000);
  animationSet.addAnimation(animation);
end
function 选择路径(StartPath,callback)
  lv=ListView(activity).setFastScrollEnabled(true)
  cp=TextView(activity)
  lay=LinearLayout(activity).setOrientation(1).addView(cp).addView(lv)
  ChoiceFile_dialog=AlertDialog.Builder(activity)
  .setTitle("选择路径")
  .setPositiveButton("确定",{
    onClick=function()
      callback(tostring(cp.Text))
    end})
  .setNegativeButton("取消",nil)
  .setView(lay)
  .show()
  adp=ArrayAdapter(activity,android.R.layout.simple_list_item_1)
  lv.setAdapter(adp)
  function SetItem(path)
    path=tostring(path)
    adp.clear()
    cp.Text=tostring(path)
    if path~="/" then
      adp.add("../")
    end
    ls=File(path).listFiles()
    if ls~=nil then
      ls=luajava.astable(File(path).listFiles())
      table.sort(ls,function(a,b)
        return (a.isDirectory()~=b.isDirectory() and a.isDirectory()) or ((a.isDirectory()==b.isDirectory()) and a.Name<b.Name)
      end)
     else
      ls={}
    end
    for index,c in ipairs(ls) do
      if c.isDirectory() then
        adp.add(c.Name.."/")
      end
    end
  end
  lv.onItemClick=function(l,v,p,s)
    项目=tostring(v.Text)
    if tostring(cp.Text)=="/" then
      路径=ls[p+1]
     else
      路径=ls[p]
    end

    if 项目=="../" then
      SetItem(File(cp.Text).getParentFile())
     elseif 路径.isDirectory() then
      SetItem(路径)
     elseif 路径.isFile() then
      callback(tostring(路径))
      ChoiceFile_dialog.hide()
    end
  end
  SetItem(StartPath)
end
function 替换文件字符串(路径,要替换的字符串,替换成的字符串)
  if 路径 then
    路径=tostring(路径)
    内容=io.open(路径):read("*a")
    io.open(路径,"w+"):write(tostring(内容:gsub(要替换的字符串,替换成的字符串))):close()
   else
    return false
  end
end
function 文件是否存在(路径)
  file,err=io.open(路径)
  if err==nil then
    return "存在"
   else
    return "不存在"
  end
end
function 获取Lua文件的执行路径()
  return activity.getLuaDir()
end
function 文件修改时间(path)
  f = File(path);
  time = f.lastModified()
  return time
end
function 删除文件(路径)
  os.remove(路径)
end
function 解压zip(ZIP路径,解压到的路径)
  ZipUtil.unzip(ZIP路径,解压到的路径)
end
function 读取文件(路径)
  return io.open(路径):read("*a")
end
function 分享文件(path)
  FileName=tostring(File(path).Name)
  ExtensionName=FileName:match("%.(.+)")
  Mime=MimeTypeMap.getSingleton().getMimeTypeFromExtension(ExtensionName)
  intent = Intent()
  intent.setAction(Intent.ACTION_SEND)
  intent.setType(Mime)
  file = File(path)
  uri = Uri.fromFile(file)
  intent.putExtra(Intent.EXTRA_STREAM,uri)
  intent.setFlags(Intent.FLAG_ACTIVITY_NEW_TASK)
  activity.startActivity(Intent.createChooser(intent, "分享到:"))
end
function 选择文件(StartPath,callback)
  lv=ListView(activity).setFastScrollEnabled(true)
  cp=TextView(activity)
  lay=LinearLayout(activity).setOrientation(1).addView(cp).addView(lv)
  ChoiceFile_dialog=AlertDialog.Builder(activity)--创建对话框
  .setTitle("选择文件")
  .setView(lay)
  .show()
  adp=ArrayAdapter(activity,android.R.layout.simple_list_item_1)
  lv.setAdapter(adp)
  function SetItem(path)
    path=tostring(path)
    adp.clear()
    cp.Text=tostring(path)
    if path~="/" then
      adp.add("../")
    end
    ls=File(path).listFiles()
    if ls~=nil then
      ls=luajava.astable(File(path).listFiles())
      table.sort(ls,function(a,b)
        return (a.isDirectory()~=b.isDirectory() and a.isDirectory()) or ((a.isDirectory()==b.isDirectory()) and a.Name<b.Name)
      end)
     else
      ls={}
    end
    for index,c in ipairs(ls) do
      if c.isDirectory() then
        adp.add(c.Name.."/")
       else--如果是文件则
        adp.add(c.Name)
      end
    end
  end
  lv.onItemClick=function(l,v,p,s)
    项目=tostring(v.Text)
    if tostring(cp.Text)=="/" then
      路径=ls[p+1]
     else
      路径=ls[p]
    end

    if 项目=="../" then
      SetItem(File(cp.Text).getParentFile())
     elseif 路径.isDirectory() then
      SetItem(路径)
     elseif 路径.isFile() then
      callback(tostring(路径))
      ChoiceFile_dialog.hide()
    end
  end
  SetItem(StartPath)
end
function MD提示(str,color,color2,ele,rad)
  if time then toasttime=Toast.LENGTH_SHORT else toasttime= Toast.LENGTH_SHORT end
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
  local toast=Toast.makeText(activity,nil,toasttime);
  toast.setView(loadlayout(toasts))
  toast.show()
end
function 窗口标题(text)
  activity.setTitle(text)
end
function 载入界面(id)
  activity.setContentView(loadlayout(id))
end
function 隐藏标题栏()
  activity.ActionBar.hide()
end
function 设置主题(id)
  activity.setTheme(id)
end
function 读取zip包内文件的内容(zipfile,file,code)
  local zipfile,file = ZipFile(File(tostring(zipfile))),tostring(file)
  local entries = zipfile.entries()
  local str = {}
  while entries.hasMoreElements() do
    entry = entries.nextElement();
    if tostring(entry) == file then
      local br = BufferedReader(InputStreamReader(zipfile.getInputStream(entry),code or "utf-8"));
      local line = br.readLine()
      while line ~= nil do
        str[#str+1] = line
        line = br.readLine();
      end
      break
    end
  end
  zipfile.close()
  return table.concat(str,"\n")
end
function 启动应用(名称)
  for jdpuk in each(this.packageManager.getInstalledApplications(0))do
    if 名称==this.packageManager.getApplicationLabel(jdpuk)then
      this.startActivity(this.packageManager.getLaunchIntentForPackage(jdpuk.packageName))
      return
    end
  end
end
function 取文件名(path)
  return path:match(".+/(.+)$")
end
function 防止二改(真包名,真版本)
  当前版本=activity.getPackageManager().getPackageInfo(this.packageName, 0).versionName
  包名=activity.getPackageName()
  if 当前版本~=真版本 then
    宇宙超级噼里啪啦的卡的退出()
  end
  if 包名~=真包名 then
    宇宙超级噼里啪啦的卡的退出()
  end
end
function 取文件名无后缀(path)
  return path:match(".+/(.+)%..+$")
end
function 隐藏状态栏()
  activity.getWindow().setFlags(WindowManager.LayoutParams.FLAG_FULLSCREEN,WindowManager.LayoutParams.FLAG_FULLSCREEN);
end
function 获取zip包大小(zipfile,file)
  local zipfile = ZipFile(File(tostring(zipfile)))
  local entries = zipfile.entries()
  while entries.hasMoreElements() do
    entry = entries.nextElement();
    if tostring(entry) == file then
      return entry.getSize()
    end
  end
  zipfile.close()
  return entry.getSize()
end
function 获取zip包列表(zipfile)
  local zipfile = ZipFile(File(tostring(zipfile)))
  local entries = zipfile.entries()
  local files ={}
  while entries.hasMoreElements() do
    entry=entries.nextElement();
    files[#files+1] = entry--通过getName()可以得到文件名称
    --a=("文件大小:"..entry.getSize())
  end
  zipfile.close()
  return files
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
function 开关保存(kg,wj,k,j)
  pref = this.getSharedPreferences("kg",0)
  kg.setChecked(pref.getBoolean(wj,false))
  if pref.getBoolean(wj,false) then
    loadstring(k)()
   else
    loadstring(j)()
  end
  editor=activity.getSharedPreferences("kg",0).edit()
  kg.setOnCheckedChangeListener{
    onCheckedChanged=function(g,c)
      if c then
        loadstring(k)()
       else
        loadstring(j)()
      end
    end}
  kg.onClick=function(v)
    editor.putBoolean(wj,v.isChecked())
    editor.commit()
  end
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
function 关闭页面()
  activity.finish()
end
function 关闭界面()
  activity.finish()
end
function 获取文本(id)
  return id.Text
end
function 圆白(id)
  id.setBackgroundDrawable(activity.Resources.getDrawable(ripple).setColor(ColorStateList(int[0].class{int{}},int{0x3fffffff})))
end
function 检测键盘()
  imm = activity.getSystemService(Context.INPUT_METHOD_SERVICE)
  isOpen=imm.isActive()
  return isOpen==true or false
end
function 隐藏键盘()
  activity.getSystemService(INPUT_METHOD_SERVICE).hideSoftInputFromWindow(WidgetSearchActivity.this.getCurrentFocus().getWindowToken(), InputMethodManager.HIDE_NOT_ALWAYS)
end
function 显示键盘(id)
  activity.getSystemService(INPUT_METHOD_SERVICE).showSoftInput(id, 0)
end
function 方白(id)
  id.setBackgroundDrawable(activity.Resources.getDrawable(ripples).setColor(ColorStateList(int[0].class{int{}},int{0x3fffffff})))
end
function 圆黑(id)
  id.setBackgroundDrawable(activity.Resources.getDrawable(ripple).setColor(ColorStateList(int[0].class{int{}},int{0x3f000000})))
end
function 方黑(id)
  id.setBackgroundDrawable(activity.Resources.getDrawable(ripples).setColor(ColorStateList(int[0].class{int{}},int{0x3f000000})))
end
function 控件圆角(view,InsideColor,radiu)
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
function 字符md5值(str)
  import "java.security.MessageDigest"
  import "android.text.TextUtils"
  import "java.lang.StringBuffer"
  sb=StringBuffer();
  import "java.lang.Byte"
  md5=MessageDigest.getInstance("md5")
  import "java.lang.Byte"
  bytes =md5.digest(String(str).getBytes())
  result=""
  by=luajava.astable(bytes)
  for k,n in ipairs(by) do
    import "java.lang.Integer"
    temp = Integer.toHexString(n & 255);

    if #temp == 1 then
      sb.append("0")
    end
    sb.append(temp);
  end
  return sb
end
function 控件背景渐变动画(view,color1,color2,color3,color4)
  colorAnim = ObjectAnimator.ofInt(view,"backgroundColor",{color1, color2, color3,color4})
  colorAnim.setDuration(3000)
  colorAnim.setEvaluator(ArgbEvaluator())
  colorAnim.setRepeatCount(ValueAnimator.INFINITE)
  colorAnim.setRepeatMode(ValueAnimator.REVERSE)
  colorAnim.start()
end
function 打印出设备信息()
  device_model = Build.MODEL
  version_xtbb = Build.VERSION.RELEASE
  version_cpxh = Build.PRODUCT
  version_bbxx = Build.DISPLAY
  version_cpmc = Build.PRODUCT
  version_xtdz = Build.BRAND
  version_sbcs = Build.DEVICE
  version_kfdh = Build.VERSION.CODENAME
  version_sdk = Build.VERSION.SDK
  version_cpu = Build.CPU_ABI
  version_yjlx = Build.HARDWARE
  version_zjdz = Build.HOST
  version_id = Build.ID
  version_rom = Build.MANUFACTURER
  android_id = Secure.getString(activity.getContentResolver(),Secure.ANDROID_ID)
  packinfo=this.getPackageManager().getPackageInfo(this.getPackageName(),((32552732/2/2-8183)/10000-6-231)/9)
  version=tostring(packinfo.versionName)
  versioncode=tostring(packinfo.versionCode)
  print("手机型号："..device_model.."\n安卓版本："..version_xtbb.."\n产品型号："..version_cpxh.."\n版本显示：" ..version_bbxx.."\n系统定制商："..version_xtdz.."\n设备参数：" ..version_sbcs.."\n开发代号："..version_kfdh.."\nSDK版本号:"..version_sdk.."\nCPU类型："..version_cpu.."\n硬件类型：" ..version_yjlx.."\n主机地址:"..version_zjdz.."\n生产ID："..version_id.."\nROM制造商："..version_rom.."\n安卓ID："..android_id.."\n当前app版本名："..version.."\n当前app版本号："..versioncode)
end
function 判断悬浮窗权限()
  if (Build.VERSION.SDK_INT >= 23 and not Settings.canDrawOverlays(this)) then
    return false
   elseif Build.VERSION.SDK_INT < 23 then
    return nil
   else
    return true
  end
end
function 判断root权限()
  local root = RootUtil()
  if root.haveRoot()==true then
    return ("已Root")
   else
    return ("未Root")
  end
end
function 判断WiFi是否连接()
  local wl=activity.getApplicationContext().getSystemService(Context.CONNECTIVITY_SERVICE).getActiveNetworkInfo();
  if wl== nil then
    return nil
   else
    return "连接了"
  end
end
function 获取悬浮窗权限()
  intent = Intent(Settings.ACTION_MANAGE_OVERLAY_PERMISSION);
  intent.setData(Uri.parse("package:" .. activity.getPackageName()));
  activity.startActivityForResult(intent, 100);
end
function 是否安装APP(packageName)
  if pcall(function() activity.getPackageManager().getPackageInfo(packageName,0) end) then
    return "安装了"
   else
    return "没安装"
  end
end
function 设置中划线(id)
  id.getPaint().setFlags(Paint. STRIKE_THRU_TEXT_FLAG)
end
function 设置下划线(id)
  import "android.graphics.Paint"
  id.getPaint().setFlags(Paint. UNDERLINE_TEXT_FLAG)
end
function 设置字体加粗(id)
  id.getPaint().setFakeBoldText(true)
end
function 设置斜体(id)
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
  activity.startActivity(Intent(Intent.ACTION_VIEW, Uri.parse("mqqapi://card/show_pslcard?src_type=internal&version=1&uin="..qq.."&card_type=group&source=qrcode")))
end
function 跳转QQ群(qq)
  activity.startActivity(Intent(Intent.ACTION_VIEW, Uri.parse("mqqapi://card/show_pslcard?src_type=internal&version=1&uin="..qq.."&card_type=group&source=qrcode")))
end
function QQ聊天(qq)
  activity.startActivity(Intent(Intent.ACTION_VIEW, Uri.parse("mqqwpa://im/chat?chat_type=wpa&uin="..qq)))
end
function 跳转QQ聊天(qq)
  activity.startActivity(Intent(Intent.ACTION_VIEW, Uri.parse("mqqwpa://im/chat?chat_type=wpa&uin="..qq)))
end
function 获取剪切板()
  return activity.getSystemService(Context.CLIPBOARD_SERVICE).getText()
end
function 写入剪切板(text)
  activity.getSystemService(Context.CLIPBOARD_SERVICE).setText(text)
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
function 写入某一行(路径,内容,行,模式)
  import "java.io.*"
  file,err=io.open(路径)
  if err==nil then
    a={}
    n=0
    for v,s in io.lines(路径) do
      n=n+1
      table.insert(a,v)
    end
    if 模式==0 then
      table.remove(a,行)
      table.insert(a,行,内容)
     elseif 模式==1 then
      内容=a[行]..内容
      table.remove(a,行)
      table.insert(a,行,内容)
     else
      print("无效操作")
      return true
    end
    内容=""
    for v,c in pairs(a) do
      if n==v then
        内容=内容..c
       else
        内容=内容..c.."\n"
      end
    end
    import "java.io.File"
    f=File(tostring(File(tostring(路径)).getParentFile())).mkdirs()
    io.open(tostring(路径),"w"):write(tostring(内容)):close()
   else
    print("文件不存在")
  end
end
function 设置进度条颜色(id,color)
  id.IndeterminateDrawable.setColorFilter(PorterDuffColorFilter(color,PorterDuff.Mode.SRC_ATOP))
end
function 设置控件颜色(id,color)
  id.setBackgroundColor(color)
end
function 设置文本颜色(id,颜色)
  id.setTextColor(颜色)
end
function 控件振幅(id,mo,v,e,b,xx,yy)
  local e= e/100
  local b=b/500
  local sj = math.random(-v,v)
  local eee = v
  ti=Ticker()
  ti.Period=5
  ti.onTick=function()
    local v = math.sin(sj)
    local bod = (-eee - eee)*v
    eee = eee - b
    sj =sj + e
    if eee <0 then
      ti.stop()
    end
    if mo==1 then
      id.setRotation(bod)
     elseif mo==2 then
      id.setRotationX(bod)
     elseif mo==3 then
      id.setRotationY(bod)
    end
  end
  if xx~=nil then
    id.setPivotX(xx)
    print"xx"
  end
  if yy~=nil then
    id.setPivotY(yy)
    print"yy"
  end
  ti.start()
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
function 显示控件(id)
  id.setVisibility(0)
end
function 隐藏控件(id)
  id.setVisibility(8)
end
function 打开APP(packageName)
  manager = activity.getPackageManager()
  open = manager.getLaunchIntentForPackage(packageName)
  this.startActivity(open)
end
function 卸载APP(file)
  uri = Uri.parse("package:"..file)
  intent = Intent(Intent.ACTION_DELETE,uri)
  activity.startActivity(intent)
end
function 安装APP(file)
  intent = Intent(Intent.ACTION_VIEW)
  intent.setDataAndType(Uri.parse("file://"..file), "application/vnd.android.package-archive")
  intent.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)
  activity.startActivity(intent)
end
function 调用系统下载文件(url,directory,name)
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
function 水珠动画(控件,time)
  ObjectAnimator().ofFloat(控件,"scaleX",{1,.8,1.3,.9,1}).setDuration(time).start()
  ObjectAnimator().ofFloat(控件,"scaleY",{1,.8,1.3,.9,1}).setDuration(time).start()
end
function 波纹(id,颜色)
  local attrsArray = {android.R.attr.selectableItemBackgroundBorderless}
  local typedArray =activity.obtainStyledAttributes(attrsArray)
  ripple=typedArray.getResourceId(0,0)
  Pretend=activity.Resources.getDrawable(ripple)
  Pretend.setColor(ColorStateList(int[0].class{int{}},int{颜色}))
  id.setBackground(Pretend.setColor(ColorStateList(int[0].class{int{}},int{颜色})))
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
function 获取软件自身包名()
  return activity.getPackageName()
end
function 获取软件自身版本()
  当前版本=activity.getPackageManager().getPackageInfo(this.packageName, 0).versionName
  return 当前版本
end
function MD5(str)
  local HexTable = {"0","1","2","3","4","5","6","7","8","9","A","B","C","D","E","F"}
  local A = 0x67452301
  local B = 0xefcdab89
  local C = 0x98badcfe
  local D = 0x10325476

  local S11 = 7
  local S12 = 12
  local S13 = 17
  local S14 = 22
  local S21 = 5
  local S22 = 9
  local S23 = 14
  local S24 = 20
  local S31 = 4
  local S32 = 11
  local S33 = 16
  local S34 = 23
  local S41 = 6
  local S42 = 10
  local S43 = 15
  local S44 = 21
  local function F(x,y,z)
    return (x & y) | ((~x) & z)
  end
  local function G(x,y,z)
    return (x & z) | (y & (~z))
  end
  local function H(x,y,z)
    return x ~ y ~ z
  end
  local function I(x,y,z)
    return y ~ (x | (~z))
  end
  local function FF(a,b,c,d,x,s,ac)
    a = a + F(b,c,d) + x + ac
    a = (((a & 0xffffffff) << s) | ((a & 0xffffffff) >> 32 - s)) + b
    return a & 0xffffffff
  end
  local function GG(a,b,c,d,x,s,ac)
    a = a + G(b,c,d) + x + ac
    a = (((a & 0xffffffff) << s) | ((a & 0xffffffff) >> 32 - s)) + b
    return a & 0xffffffff
  end
  local function HH(a,b,c,d,x,s,ac)
    a = a + H(b,c,d) + x + ac
    a = (((a & 0xffffffff) << s) | ((a & 0xffffffff) >> 32 - s)) + b
    return a & 0xffffffff
  end
  local function II(a,b,c,d,x,s,ac)
    a = a + I(b,c,d) + x + ac
    a = (((a & 0xffffffff) << s) | ((a & 0xffffffff) >> 32 - s)) + b
    return a & 0xffffffff
  end
  local function MD5StringFill(s)
    local len = s:len()
    local mod512 = len * 8 % 512
    local fillSize = (448 - mod512) // 8
    if mod512 > 448 then
      fillSize = (960 - mod512) // 8
    end
    local rTab = {}
    local byteIndex = 1
    for i = 1,len do
      local index = (i - 1) // 4 + 1
      rTab[index] = rTab[index] or 0
      rTab[index] = rTab[index] | (s:byte(i) << (byteIndex - 1) * 8)
      byteIndex = byteIndex + 1
      if byteIndex == 5 then
        byteIndex = 1
      end
    end
    --先将最后一个字节组成4字节一组
    --表示0x80是否已插入
    local b0x80 = false
    local tLen = #rTab
    if byteIndex ~= 1 then
      rTab[tLen] = rTab[tLen] | 0x80 << (byteIndex - 1) * 8
      b0x80 = true
    end

    --将余下的字节补齐
    for i = 1,fillSize // 4 do
      if not b0x80 and i == 1 then
        rTab[tLen + i] = 0x80
       else
        rTab[tLen + i] = 0x0
      end
    end

    --后面加原始数据bit长度
    local bitLen = math.floor(len * 8)
    tLen = #rTab
    rTab[tLen + 1] = bitLen & 0xffffffff
    rTab[tLen + 2] = bitLen >> 32

    return rTab
  end

  --	Func:	计算MD5
  --	Param:	string
  --	Return:	string
  ---------------------------------------------

  function string.md5(s)
    --填充
    local fillTab = MD5StringFill(s)
    local result = {A,B,C,D}

    for i = 1,#fillTab // 16 do
      local a = result[1]
      local b = result[2]
      local c = result[3]
      local d = result[4]
      local offset = (i - 1) * 16 + 1
      --第一轮
      a = FF(a, b, c, d, fillTab[offset + 0], S11, 0xd76aa478)
      d = FF(d, a, b, c, fillTab[offset + 1], S12, 0xe8c7b756)
      c = FF(c, d, a, b, fillTab[offset + 2], S13, 0x242070db)
      b = FF(b, c, d, a, fillTab[offset + 3], S14, 0xc1bdceee)
      a = FF(a, b, c, d, fillTab[offset + 4], S11, 0xf57c0faf)
      d = FF(d, a, b, c, fillTab[offset + 5], S12, 0x4787c62a)
      c = FF(c, d, a, b, fillTab[offset + 6], S13, 0xa8304613)
      b = FF(b, c, d, a, fillTab[offset + 7], S14, 0xfd469501)
      a = FF(a, b, c, d, fillTab[offset + 8], S11, 0x698098d8)
      d = FF(d, a, b, c, fillTab[offset + 9], S12, 0x8b44f7af)
      c = FF(c, d, a, b, fillTab[offset + 10], S13, 0xffff5bb1)
      b = FF(b, c, d, a, fillTab[offset + 11], S14, 0x895cd7be)
      a = FF(a, b, c, d, fillTab[offset + 12], S11, 0x6b901122)
      d = FF(d, a, b, c, fillTab[offset + 13], S12, 0xfd987193)
      c = FF(c, d, a, b, fillTab[offset + 14], S13, 0xa679438e)
      b = FF(b, c, d, a, fillTab[offset + 15], S14, 0x49b40821)

      --第二轮
      a = GG(a, b, c, d, fillTab[offset + 1], S21, 0xf61e2562)
      d = GG(d, a, b, c, fillTab[offset + 6], S22, 0xc040b340)
      c = GG(c, d, a, b, fillTab[offset + 11], S23, 0x265e5a51)
      b = GG(b, c, d, a, fillTab[offset + 0], S24, 0xe9b6c7aa)
      a = GG(a, b, c, d, fillTab[offset + 5], S21, 0xd62f105d)
      d = GG(d, a, b, c, fillTab[offset + 10], S22, 0x2441453)
      c = GG(c, d, a, b, fillTab[offset + 15], S23, 0xd8a1e681)
      b = GG(b, c, d, a, fillTab[offset + 4], S24, 0xe7d3fbc8)
      a = GG(a, b, c, d, fillTab[offset + 9], S21, 0x21e1cde6)
      d = GG(d, a, b, c, fillTab[offset + 14], S22, 0xc33707d6)
      c = GG(c, d, a, b, fillTab[offset + 3], S23, 0xf4d50d87)
      b = GG(b, c, d, a, fillTab[offset + 8], S24, 0x455a14ed)
      a = GG(a, b, c, d, fillTab[offset + 13], S21, 0xa9e3e905)
      d = GG(d, a, b, c, fillTab[offset + 2], S22, 0xfcefa3f8)
      c = GG(c, d, a, b, fillTab[offset + 7], S23, 0x676f02d9)
      b = GG(b, c, d, a, fillTab[offset + 12], S24, 0x8d2a4c8a)

      --第三轮
      a = HH(a, b, c, d, fillTab[offset + 5], S31, 0xfffa3942)
      d = HH(d, a, b, c, fillTab[offset + 8], S32, 0x8771f681)
      c = HH(c, d, a, b, fillTab[offset + 11], S33, 0x6d9d6122)
      b = HH(b, c, d, a, fillTab[offset + 14], S34, 0xfde5380c)
      a = HH(a, b, c, d, fillTab[offset + 1], S31, 0xa4beea44)
      d = HH(d, a, b, c, fillTab[offset + 4], S32, 0x4bdecfa9)
      c = HH(c, d, a, b, fillTab[offset + 7], S33, 0xf6bb4b60)
      b = HH(b, c, d, a, fillTab[offset + 10], S34, 0xbebfbc70)
      a = HH(a, b, c, d, fillTab[offset + 13], S31, 0x289b7ec6)
      d = HH(d, a, b, c, fillTab[offset + 0], S32, 0xeaa127fa)
      c = HH(c, d, a, b, fillTab[offset + 3], S33, 0xd4ef3085)
      b = HH(b, c, d, a, fillTab[offset + 6], S34, 0x4881d05)
      a = HH(a, b, c, d, fillTab[offset + 9], S31, 0xd9d4d039)
      d = HH(d, a, b, c, fillTab[offset + 12], S32, 0xe6db99e5)
      c = HH(c, d, a, b, fillTab[offset + 15], S33, 0x1fa27cf8)
      b = HH(b, c, d, a, fillTab[offset + 2], S34, 0xc4ac5665)

      --第四轮
      a = II(a, b, c, d, fillTab[offset + 0], S41, 0xf4292244)
      d = II(d, a, b, c, fillTab[offset + 7], S42, 0x432aff97)
      c = II(c, d, a, b, fillTab[offset + 14], S43, 0xab9423a7)
      b = II(b, c, d, a, fillTab[offset + 5], S44, 0xfc93a039)
      a = II(a, b, c, d, fillTab[offset + 12], S41, 0x655b59c3)
      d = II(d, a, b, c, fillTab[offset + 3], S42, 0x8f0ccc92)
      c = II(c, d, a, b, fillTab[offset + 10], S43, 0xffeff47d)
      b = II(b, c, d, a, fillTab[offset + 1], S44, 0x85845dd1)
      a = II(a, b, c, d, fillTab[offset + 8], S41, 0x6fa87e4f)
      d = II(d, a, b, c, fillTab[offset + 15], S42, 0xfe2ce6e0)
      c = II(c, d, a, b, fillTab[offset + 6], S43, 0xa3014314)
      b = II(b, c, d, a, fillTab[offset + 13], S44, 0x4e0811a1)
      a = II(a, b, c, d, fillTab[offset + 4], S41, 0xf7537e82)
      d = II(d, a, b, c, fillTab[offset + 11], S42, 0xbd3af235)
      c = II(c, d, a, b, fillTab[offset + 2], S43, 0x2ad7d2bb)
      b = II(b, c, d, a, fillTab[offset + 9], S44, 0xeb86d391)

      --加入到之前计算的结果当中
      result[1] = result[1] + a
      result[2] = result[2] + b
      result[3] = result[3] + c
      result[4] = result[4] + d
      result[1] = result[1] & 0xffffffff
      result[2] = result[2] & 0xffffffff
      result[3] = result[3] & 0xffffffff
      result[4] = result[4] & 0xffffffff
    end
    local retStr = ""
    for i = 1,4 do
      for _ = 1,4 do
        local temp = result[i] & 0x0F
        local str = HexTable[temp + 1]
        result[i] = result[i] >> 4
        temp = result[i] & 0x0F
        retStr = retStr .. HexTable[temp + 1] .. str
        result[i] = result[i] >> 4
      end
    end
    return retStr
  end
  return string.md5(str)
end
function 圆角黑色背景提示(内容)
  tsbj={
    LinearLayout;
    layout_width="fill";
    layout_height="fill";
    gravity="center";
    orientation="vertical";
    {
      CardView;
      radius=15,
      CardElevation="0dp";
      backgroundColor="0xff6f6f6f";
      {
        TextView;
        layout_marginRight="35dp";
        text="提示";
        id="提示内容",
        textColor="0xffffffff";
        layout_marginLeft="35dp";
        layout_marginTop="10dp";
        layout_gravity="center";
        layout_marginBottom="10dp";
      };
    };
  };
  local toast=Toast.makeText(activity,"内容",Toast.LENGTH_SHORT).setView(loadlayout(tsbj))
  toast.setGravity(Gravity.BOTTOM,0,100)
  提示内容.Text=tostring(内容)
  toast.show()
end
function 上移动画(id)
  translate=TranslateAnimation(0,0, 500, 0)
  translate.setDuration(300);
  translate.setRepeatCount(0);
  translate.setFillAfter(true)
  id.startAnimation(translate)
  水珠动画(id,1000)
end
function 下移动画(id)
  translate=TranslateAnimation(0,0, 0, 500)
  translate.setDuration(300);
  translate.setRepeatCount(0);
  translate.setFillAfter(true)
  id.startAnimation(translate)
end
function 图片转链接(callback)
  import "http"
  import "android.provider.MediaStore"
  local intent= Intent(Intent.ACTION_PICK)
  intent.setType("image/*")
  this.startActivityForResult(intent,1)
  function onActivityResult(requestCode,resultCode,intent)
    xpcall(function()
      if intent then
        local cursor =this.getContentResolver ().query(intent.getData(), nil, nil, nil, nil)
        cursor.moveToFirst()
        local idx = cursor.getColumnIndex(MediaStore.Images.ImageColumns.DATA)
        local filepath = cursor.getString(idx)
        local a,b,c = http.upload("http://pic.sogou.com/pic/upload_pic.jsp",{name="pic_path",filename="icon.png"},{file=filepath})
        if c == 200 then
          callback(tosing(a))
         else
          print("error")
        end
      end
    end,function(error)
      print("错误:"..error)
    end)
  end
end
function 进入首页()
  activity.newActivity("main")
end
function 转换字符串(zfc)
  tostring(zfc)
end
function 保存网页图片(浏览器id)
  浏览器id.setOnLongClickListener(View.OnLongClickListener{
    onLongClick=function(v)
      result = v.getHitTestResult()
      mtype = result.getType()
      if mtype==WebView.HitTestResult.IMAGE_TYPE then
        url=result.getExtra()
        AlertDialog.Builder(activity)
        .setTitle("提示")
        .setMessage("是否保存该图片？")
        .setPositiveButton("确定",{onClick=function()
            downloadManager=activity.getSystemService(Context.DOWNLOAD_SERVICE)
            url=Uri.parse(url)
            file_name="picture_"..os.time()
            request=DownloadManager.Request(url);
            request.setAllowedNetworkTypes(DownloadManager.Request.NETWORK_MOBILE|DownloadManager.Request.NETWORK_WIFI)
            request.setDestinationInExternalPublicDir("Download",file_name)
            request.setTitle(file_name)
            request.setNotificationVisibility(DownloadManager.Request.VISIBILITY_VISIBLE_NOTIFY_COMPLETED);
            downloadManager.enqueue(request)
          end})
        .show()
      end
    end})
end
function 打开已下载软件列表()
  local intent = Intent(Settings.ACTION_MANAGE_APPLICATIONS_SETTINGS);
  activity.startActivity(intent);
end
function 控件旋转(view,速度,次数)
  import 'android.view.animation.Animation'
  import 'android.view.animation.RotateAnimation'
  rotate=RotateAnimation(0, 360,
  Animation.RELATIVE_TO_SELF, 0.5,
  Animation.RELATIVE_TO_SELF, 0.5)
  rotate.setDuration(速度)--设置毫秒1000=1秒
  rotate.setRepeatCount(次数)
  view.startAnimation(rotate)
end
function 循环(cs,cr,事件)
  for i=cs,cr do
    执行代码(事件)
  end
end
function 复制文本(文本)
  import "android.content.*"
  CLIPBOARD_SERVICE=this.getSystemService(Context.CLIPBOARD_SERVICE)
  CLIPBOARD_SERVICE.setText(tostring(文本))
end
