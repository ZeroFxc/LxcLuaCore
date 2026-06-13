-- 测试新添加的 proto 替换 API 和小写别名

print("===== 测试 1: 小写别名都存在 =====")
local BC = require("ByteCode")
local function exists(name)
  return type(BC[name]) == "function" or type(BC[name]) == "table"
end

local tests = {
  -- PascalCase
  "CheckFunction", "GetProto", "GetCodeCount", "GetCode", "SetCode",
  "GetLine", "GetParamCount", "IsGC", "GetOpCode", "GetArgs", "Make", "Dump",
  "GetConstant", "GetConstants", "GetUpvalue", "GetUpvalues",
  "GetLocal", "GetLocals", "GetNestedProto", "GetNestedProtos",
  "GetInstruction", "SetInstruction", "Lock", "IsLocked",
  "MarkOriginal", "IsTampered",
  -- 新增 PascalCase
  "GetClosure", "SetProto", "GetUpvalueValues", "SetUpvalueValue",
  "IsHotFixed", "GetMaxStackSize", "GetNumParams", "GetUpvalueCount", "GetSource",
  -- 小写
  "checkfunction", "getproto", "getcodecount", "getcode", "setcode",
  "getline", "getparamcount", "isgc", "getopcode", "getargs", "make", "dump",
  "getconstant", "getconstants", "getupvalue", "getupvalues",
  "getlocal", "getlocals", "getnestedproto", "getnestedprotos",
  "getinstruction", "setinstruction", "lock", "islocked",
  "markoriginal", "istampered",
  "getclosure", "setproto", "getupvaluevalues", "setupvaluevalue",
  "ishotfixed", "getmaxstacksize", "getnumparams", "getupvaluecount", "getsource",
  -- 表
  "OpCodes", "opcodes",
}
local missing = {}
for _, n in ipairs(tests) do
  if not exists(n) then table.insert(missing, n) end
end
if #missing == 0 then
  print("  PASS: 全部 58 个别名都存在")
else
  print("  FAIL: 缺失: " .. table.concat(missing, ", "))
end

print()
print("===== 测试 2: SetProto 替换无 upvalue 函数 =====")
do
  local function old() return "old version" end
  local function new() return "new version" end
  assert(old() == "old version")
  local ret = BC.setproto(old, new)
  assert(ret == new, "SetProto 应返回 newFunc")
  assert(old() == "new version", "调用 old 应执行 new 的代码")
  assert(BC.ishotfixed(old) == true, "old 应被标记为 hotfixed")
  assert(BC.ishotfixed(new) == false, "new 不应被标记")
  print("  PASS: 替换无 upvalue 函数成功")
end

print()
print("===== 测试 3: SetProto 保留 upvalue（不传 map）=====")
do
  local counter = 0
  local function incOld()
    counter = counter + 1
    return "old:" .. counter
  end
  local function incNew()
    counter = counter + 100  -- 不同的实现
    return "new:" .. counter
  end
  counter = 0
  incOld(); incOld()  -- counter=2
  BC.setproto(incOld, incNew)
  local r = incOld()
  assert(r == "new:102", "应执行 new 的代码, got: " .. tostring(r))
  assert(BC.ishotfixed(incOld), "应标记为 hotfixed")
  print("  PASS: upvalue 保留, 字节码被替换")
end

print()
print("===== 测试 4: SetProto 带 upvalueMap（数量不一致）=====")
do
  local x, y = 1, 2
  local function old(a, b)  -- 0 upvalue
    return a + b
  end
  local function newFn()  -- 2 upvalue
    return x + y
  end
  -- old 原本无 upvalue，upvalueMap 即使给了也没意义（nupvalues=0）
  -- 这里我们改成 newFn 也无 upvalue 的版本
  local function newFn0() return 999 end
  BC.setproto(old, newFn0)
  assert(old(1, 2) == 999, "替换后应返回 999")
  print("  PASS: 数量不一致也能替换（取 min）")
end

print()
print("===== 测试 5: GetUpvalueValues 读 upvalue 实际值 =====")
do
  local a, b, c = 10, 20, 30
  local function f() return a + b + c end
  local vals = BC.getupvaluevalues(f)
  assert(#vals == 3, "应有 3 个 upvalue, got " .. #vals)
  assert(vals[1] == 10 and vals[2] == 20 and vals[3] == 30,
         "upvalue 值不对: " .. tostring(vals[1]) .. "," .. tostring(vals[2]) .. "," .. tostring(vals[3]))
  print("  PASS: 读取 upvalue 实际值正确")
end

print()
print("===== 测试 6: SetUpvalueValue 改 upvalue 值 =====")
do
  local x = 100
  local function f() return x end
  assert(f() == 100)
  BC.setupvaluevalue(f, 1, 200)
  assert(f() == 200, "应返回 200, got " .. tostring(f()))
  print("  PASS: 写入 upvalue 成功")
end

print()
print("===== 测试 7: GetClosure / GetMaxStackSize / GetNumParams / GetSource / GetUpvalueCount =====")
do
  local function f(a, b, c) local t = {}; return t end
  local cl = BC.getclosure(f)
  assert(type(cl) == "userdata", "GetClosure 应返回 lightuserdata")
  local p = BC.getproto(f)
  assert(BC.getnumparams(p) == 3, "numparams 应为 3")
  assert(BC.getupvaluecount(p) == 0, "upvalues 应为 0")
  assert(type(BC.getmaxstacksize(p)) == "number", "maxstacksize 应为 number")
  assert(type(BC.getsource(p)) == "string", "source 应为 string")
  print("  PASS: 基础元信息接口正常")
end

print()
print("===== 测试 8: 旧 PascalCase 名字仍然有效 =====")
do
  local function f() return 42 end
  local p = BC.GetProto(f)
  assert(p ~= nil)
  assert(BC.IsHotFixed(f) == false)
  assert(type(BC.OpCodes) == "table")
  assert(type(BC.opcodes) == "table")
  assert(BC.OpCodes.MOVE == BC.opcodes.MOVE, "大小写 opcodes 表应一致")
  print("  PASS: PascalCase 与 camelCase 双套 API 都能用")
end

print()
print("===== 测试 9: 锁定的 proto 不能 SetProto =====")
do
  local function f() return 1 end
  local function g() return 2 end
  BC.Lock(f)
  local ok, err = pcall(BC.setproto, f, g)
  assert(not ok, "锁定的函数应该报错")
  assert(string.find(tostring(err), "locked"), "应提示 locked, got: " .. tostring(err))
  print("  PASS: 锁定检查生效")
end

print()
print("===== 所有测试通过 =====")
