# 内置库参考

> 基于 `lbaselib.c` / `lstrlib.c` / `lmathlib.c` / `ltablib.c` 源码中注册表验证

---

## 1. 全局函数 (base)

基于 `lbaselib.c` 函数注册表：

| 函数 | 说明 |
|------|------|
| `assert(v, msg)` | 断言 |
| `collectgarbage(opt, ...)` | GC 控制 |
| `defer(f)` | 延迟执行 |
| `dofile(filename)` | 执行文件 |
| `getfile(filename)` | 获取文件内容 |
| `dump(func)` | 函数转字节码 |
| `error(msg, level)` | 抛出错误 |
| `grand(min, max)` | 随机整数 |
| `fsleep(ms)` | 休眠（毫秒） |
| `fwake(func)` | 唤醒函数 |
| `findtable(t, name)` | 查找表 |
| `getenv(name)` | 获取环境变量 |
| `getfenv(f)` | 获取函数环境 |
| `getmetatable(obj)` | 获取元表 |
| `ipairs(t)` | 数组迭代器 |
| `ismap(v)` | 是 map 类型 |
| `istable(v)` | 是表类型 |
| `loadfile(filename)` | 加载文件 |
| `loadsfile(filename)` | 加载文件（返回字符串） |
| `load(chunk, name, mode)` | 加载代码 |
| `loadstring(s)` | 加载字符串 |
| `match(t, pattern)` | 表模式匹配 |
| `mpairs(t)` | map 迭代器 |
| `next(t, key)` | 下一个键 |
| `pairs(t)` | 遍历迭代器 |
| `range(start, stop, step)` | 范围生成器 |
| `pcall(f, ...)` | 保护调用 |
| `print(...)` | 打印 |
| `warn(msg)` | 警告 |
| `rawequal(a, b)` | 原始相等 |
| `rawlen(v)` | 原始长度 |
| `rawget(t, k)` | 原始读 |
| `rawset(t, k, v)` | 原始写 |
| `select(n, ...)` | 选择参数 |
| `setfenv(f, env)` | 设置函数环境 |
| `setmetatable(obj, mt)` | 设置元表 |
| `tonumber(s)` | 转数字 |
| `tointeger(v)` | 转整数 |
| `tostring(v)` | 转字符串 |
| `toasc2i(s)` | 字符串转 ASCII 十进制 |
| `type(v)` | 获取类型 |
| `typeof(v)` | 获取详细类型 |
| `issubtype(a, b)` | 检查子类型 |
| `isgeneric(v)` | 是否泛型 |
| `wymd5(s)` | MD5 哈希 |
| `xpcall(f, msgh)` | 带错误处理的保护调用 |

---

## 2. 字符串 (string)

基于 `lstrlib.c` 函数注册表：

| 函数 | 说明 |
|------|------|
| `string.byte(s, i, j)` | 获取字节码 |
| `string.char(...)` | 字节码转字符 |
| `string.contains(s, sub)` | 包含检查 |
| `string.crc32(s)` | CRC32 校验 |
| `string.dump(func)` | 函数转字节码 |
| `string.endswith(s, suffix)` | 后缀检查 |
| `string.envelop(s, prefix, suffix)` | 包裹字符串 |
| `string.escape(s)` | 转义 |
| `string.file(path)` | 读取文件内容 |
| `string.find(s, pattern, init)` | 查找 |
| `string.format(fmt, ...)` | 格式化 |
| `string.fromhex(s)` | 十六进制解码 |
| `string.gmatch(s, pattern)` | 全局匹配迭代器 |
| `string.gfind(s, pattern)` | 全局查找（别名） |
| `string.gsub(s, pattern, repl)` | 全局替换 |
| `string.hex(s)` | 十六进制编码 |
| `string.len(s)` | 长度 |
| `string.lower(s)` | 转小写 |
| `string.ltrim(s)` | 去左空白 |
| `string.match(s, pattern)` | 匹配 |
| `string.pack(fmt, ...)` | 打包二进制 |
| `string.packsize(fmt)` | 打包格式大小 |
| `string.rep(s, n)` | 重复 |
| `string.reverse(s)` | 反转 |
| `string.rtrim(s)` | 去右空白 |
| `string.sha256(s)` | SHA-256 哈希 |
| `string.split(s, sep)` | 分割 |
| `string.startswith(s, prefix)` | 前缀检查 |
| `string.sub(s, i, j)` | 截取 |
| `string.trim(s)` | 去空白 |
| `string.unpack(fmt, s)` | 解包二进制 |
| `string.upper(s)` | 转大写 |
| `string.aes_encrypt(data, key)` | AES 加密 |
| `string.aes_decrypt(data, key)` | AES 解密 |

### 字符串算术元方法

```lua
-- 注意：以下元方法在源码中定义但运行时不可用
-- 需要 string 模块显式设置元表后才能使用
local mt = getmetatable("")
-- mt.__mul, mt.__add 等由 string 模块注册
```

---

## 3. 数学 (math)

基于 `lmathlib.c` 函数注册表：

| 函数 | 说明 |
|------|------|
| `math.abs(x)` | 绝对值 |
| `math.acos(x)` | 反余弦 |
| `math.asin(x)` | 反正弦 |
| `math.atan(y, x)` | 反正切 |
| `math.atan2(y, x)` | 反正切（别名） |
| `math.ceil(x)` | 向上取整 |
| `math.clamp(x, min, max)` | 限定范围 |
| `math.cos(x)` | 余弦 |
| `math.cosh(x)` | 双曲余弦 |
| `math.deg(x)` | 弧度转角度 |
| `math.exp(x)` | e^x |
| `math.floor(x)` | 向下取整 |
| `math.fmod(x, y)` | 浮点取模 |
| `math.frexp(x)` | 分解尾数指数 |
| `math.ldexp(m, e)` | 组合尾数指数 |
| `math.lerp(a, b, t)` | 线性插值 |
| `math.log(x, base)` | 对数 |
| `math.log10(x)` | 以10为底对数 |
| `math.maprange(v, a1, a2, b1, b2)` | 范围映射 |
| `math.max(x, ...)` | 最大值 |
| `math.min(x, ...)` | 最小值 |
| `math.modf(x)` | 整数小数分离 |
| `math.noise(x, y)` | 噪声函数 |
| `math.pow(x, y)` | 幂运算 |
| `math.rad(x)` | 角度转弧度 |
| `math.random(m, n)` | 随机数 |
| `math.randomseed(seed)` | 随机种子 |
| `math.round(x)` | 四舍五入 |
| `math.sign(x)` | 符号函数 |
| `math.sin(x)` | 正弦 |
| `math.sinh(x)` | 双曲正弦 |
| `math.sqrt(x)` | 平方根 |
| `math.tan(x)` | 正切 |
| `math.tanh(x)` | 双曲正切 |
| `math.tointeger(x)` | 转整数 |
| `math.type(x)` | 数字类型 |
| `math.ult(m, n)` | 无符号比较 |
| `math.ispow2(x)` | 是否2的幂 |
| `math.nextpow2(x)` | 下一个2的幂 |
| `math.toexpr(x)` | 转表达式字符串 |
| `math.array(...)` | 创建数组 |

### 常量

| 常量 | 值 |
|------|-----|
| `math.pi` | 3.1415926535898 |
| `math.huge` | inf |
| `math.maxinteger` | 9223372036854775807 |
| `math.mininteger` | -9223372036854775808 |

---

## 4. 表操作 (table)

基于 `ltablib.c` 函数注册表：

| 函数 | 说明 |
|------|------|
| `table.add(t, value)` | 添加元素 |
| `table.clear(t)` | 清空 |
| `table.clone(t)` | 浅拷贝 |
| `table.concat(t, sep, i, j)` | 连接 |
| `table.const(t)` | 常量表 |
| `table.create(arr, hash)` | 创建表 |
| `table.fill(t, v, i, j)` | 填充 |
| `table.filter(t, pred)` | 过滤 |
| `table.find(t, value)` | 查找 |
| `table.flatten(t)` | 展平 |
| `table.foreach(t, f)` | 遍历 |
| `table.foreachi(t, f)` | 数组遍历 |
| `table.gfind(t, value)` | 全局查找 |
| `table.insert(t, pos, value)` | 插入 |
| `table.keys(t)` | 获取键列表 |
| `table.map(t, f)` | 映射 |
| `table.maxn(t)` | 最大数字键 |
| `table.merge(t1, t2)` | 合并 |
| `table.move(a1, f, e, t, a2)` | 移动 |
| `table.pack(...)` | 打包 |
| `table.reduce(t, f, init)` | 折叠 |
| `table.remove(t, pos)` | 移除 |
| `table.reverse(t)` | 反转 |
| `table.share(t)` | 共享表 |
| `table.size(t)` | 元素数量 |
| `table.slice(t, start, stop, step)` | 切片 |
| `table.sort(t, comp)` | 排序 |
| `table.unpack(t, i, j)` | 解包 |
| `table.vals(t)` | 获取值列表 |

---

## 5. IO (io)

| 函数 | 说明 |
|------|------|
| `io.open(filename, mode)` | 打开文件 |
| `io.input(file)` | 设置输入 |
| `io.output(file)` | 设置输出 |
| `io.tmpfile()` | 临时文件 |
| `io.type(obj)` | 文件句柄类型 |
| `io.write(...)` | 写入 |
| `io.read(...)` | 读取 |
| `io.lines(filename)` | 行迭代器 |
| `io.close(file)` | 关闭 |
| `io.flush()` | 刷新 |

### 文件句柄方法

| 方法 | 说明 |
|------|------|
| `file:read(...)` | 读取 |
| `file:write(...)` | 写入 |
| `file:seek(whence, offset)` | 定位 |
| `file:close()` | 关闭 |
| `file:flush()` | 刷新 |
| `file:lines()` | 行迭代器 |
| `file:setvbuf(mode, size)` | 缓冲 |
| `file:lock(mode, start, len)` | 锁定 |
| `file:unlock(start, len)` | 解锁 |
| `file:stat()` | 文件信息 |
| `file:size()` | 文件大小 |

---

## 6. OS (os)

| 函数 | 说明 |
|------|------|
| `os.clock()` | CPU 时间 |
| `os.date(format, time)` | 日期格式化 |
| `os.difftime(t2, t1)` | 时间差 |
| `os.execute(cmd)` | 执行命令 |
| `os.exit(code, close)` | 退出 |
| `os.getenv(name)` | 环境变量 |
| `os.setenv(name, value)` | 设置环境变量 |
| `os.remove(filename)` | 删除文件 |
| `os.rename(old, new)` | 重命名 |
| `os.tmpname()` | 临时文件名 |
| `os.time(table)` | 时间戳 |
| `os.clock_gettime(id)` | 高精度时钟 |
| `os.nanosleep(sec, nsec)` | 纳秒休眠 |

---

## 7. 协程 (coroutine)

| 函数 | 说明 |
|------|------|
| `coroutine.create(f)` | 创建 |
| `coroutine.resume(co, ...)` | 恢复 |
| `coroutine.yield(...)` | 挂起 |
| `coroutine.status(co)` | 状态 |
| `coroutine.wrap(f)` | 包装 |
| `coroutine.running()` | 当前协程 |
| `coroutine.isyieldable()` | 可挂起 |
| `coroutine.close(co)` | 关闭 |

---

## 8. 位操作 (bit)

| 函数 | 说明 |
|------|------|
| `bit.arshift(x, n)` | 算术右移 |
| `bit.band(x, y)` | 按位与 |
| `bit.bnot(x)` | 按位取反 |
| `bit.bor(x, y)` | 按位或 |
| `bit.btest(x, y)` | 位测试 |
| `bit.bxor(x, y)` | 按位异或 |
| `bit.extract(x, f, w)` | 提取位域 |
| `bit.replace(x, v, f, w)` | 替换位域 |
| `bit.lrotate(x, n)` | 循环左移 |
| `bit.lshift(x, n)` | 逻辑左移 |
| `bit.rrotate(x, n)` | 循环右移 |
| `bit.rshift(x, n)` | 逻辑右移 |
| `bit.tohex(x, n)` | 转十六进制 |
| `bit.bswap(x)` | 字节序交换 |

---

## 9. 调试 (debug)

| 函数 | 说明 |
|------|------|
| `debug.debug()` | 进入调试器 |
| `debug.gethook(thread)` | 获取钩子 |
| `debug.getinfo(f, what)` | 获取信息 |
| `debug.getlocal(f, n)` | 获取局部变量 |
| `debug.getupvalue(f, n)` | 获取 upvalue |
| `debug.sethook(hook, mask, count)` | 设置钩子 |
| `debug.setlocal(level, n, value)` | 设置局部变量 |
| `debug.setupvalue(f, n, value)` | 设置 upvalue |
| `debug.traceback(msg, level)` | 堆栈跟踪 |
| `debug.getregistry()` | 获取注册表 |
| `debug.upvalueid(f, n)` | upvalue ID |
| `debug.upvaluejoin(f1, n1, f2, n2)` | 连接 upvalue |
| `debug.settrace(flag)` | 设置跟踪 |
| `debug.hotfix(old, new)` | 热修复 |
| `debug.lockproto(f)` | 锁定函数原型 |
| `debug.dumpheap()` | 导出堆信息 |

---

## 10. UTF-8 (utf8)

| 函数 | 说明 |
|------|------|
| `utf8.char(...)` | 码点转字符 |
| `utf8.codes(s)` | 码点迭代器 |
| `utf8.codepoint(s, i, j)` | 获取码点 |
| `utf8.len(s, i, j)` | 码点长度 |
| `utf8.offset(s, n, i)` | 码点偏移 |
| `utf8.escape(s)` | 转义 |
| `utf8.unescape(s)` | 反转义 |

---

## 11. 包管理 (package)

| 字段 | 说明 |
|------|------|
| `package.path` | Lua 搜索路径 |
| `package.cpath` | C 库搜索路径 |
| `package.loaded` | 已加载模块 |
| `package.preload` | 预加载模块 |
| `package.loadlib(lib, func)` | 加载 C 库 |
| `package.searchpath(name, path)` | 搜索路径 |
| `package.config` | 配置信息 |

---

## 12. 文件系统 (fs)

| 函数 | 说明 |
|------|------|
| `fs.read(path)` | 读取文件 |
| `fs.write(path, data)` | 写入文件 |
| `fs.append(path, data)` | 追加 |
| `fs.exists(path)` | 检查存在 |
| `fs.isdir(path)` | 是目录 |
| `fs.isfile(path)` | 是文件 |
| `fs.mkdir(path)` | 创建目录 |
| `fs.rmdir(path)` | 删除目录 |
| `fs.remove(path)` | 删除文件 |
| `fs.rename(old, new)` | 重命名 |
| `fs.copy(src, dst)` | 复制 |
| `fs.move(src, dst)` | 移动 |
| `fs.stat(path)` | 信息 |
| `fs.size(path)` | 大小 |
| `fs.listdir(path)` | 列出目录 |
| `fs.chdir(path)` | 切换目录 |
| `fs.cwd()` | 当前目录 |
| `fs.basename(path)` | 文件名 |
| `fs.dirname(path)` | 目录名 |
| `fs.join(...)` | 拼接路径 |

---

## 13. 加密 (crypto)

| 函数 | 说明 |
|------|------|
| `crypto.md5(s)` | MD5 |
| `crypto.sha1(s)` | SHA-1 |
| `crypto.sha256(s)` | SHA-256 |
| `crypto.aes_encrypt(data, key)` | AES 加密 |
| `crypto.aes_decrypt(data, key)` | AES 解密 |
| `crypto.rsa_encrypt(data, key)` | RSA 加密 |
| `crypto.rsa_decrypt(data, key)` | RSA 解密 |
| `crypto.rsa_sign(data, key)` | RSA 签名 |
| `crypto.rsa_verify(data, sig, key)` | RSA 验签 |
| `crypto.ecc_encrypt(data, key)` | ECC 加密 |
| `crypto.ecc_decrypt(data, key)` | ECC 解密 |
| `crypto.ecc_sign(data, key)` | ECC 签名 |
| `crypto.ecc_verify(data, sig, key)` | ECC 验签 |
| `crypto.crc32(data)` | CRC32 |
| `crypto.base64_encode(data)` | Base64 编码 |
| `crypto.base64_decode(data)` | Base64 解码 |
| `crypto.random_bytes(n)` | 随机字节 |
| `crypto.uuid()` | UUID |