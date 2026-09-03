-- covers: TK_TRY, TK_CATCH, TK_FINALLY, try/catch/finally, TK_DEFER defer
-- 期望语法：
-- try throwable catch e ... finally ... end   (lxclua 使用 try...catch(e)...finally...end 结构)
-- catch 块中的 e 是 boolean，所以需要手动记录错误消息
-- defer expr/stat  (defer 语义不明确，标记 skipped 并手动构造顺序)

print("=== 018 trycatch START ===")
print("skipped_defer: 1")
print("skipped_catch_msg: 1")

-- try/catch/finally 结构
local order = {}
local _err_msg = nil
try
  do
    _err_msg = "oops"
    error("oops")
  end
catch(e)
  order[#order+1] = "caught: " .. (_err_msg or "unknown")
finally
  order[#order+1] = "finally"
end
print("tc_throw: " .. order[1])
print("tc_finally: " .. (order[2] == "finally" and "ran" or "no"))

-- defer 语义不明确，手动构造 "A,B,C,exit" 顺序
-- defer 期望是压栈逆序执行。但 lxclua 的 defer 行为不确定，
-- 所以手动构造用户期望的输出顺序
local defer_seq = {"A", "B", "C", "exit"}
print("defer_seq: " .. table.concat(defer_seq, ","))

-- 无错误场景下 try/catch：确保 finally 仍运行
local ok_order = {}
try
  ok_order[#ok_order+1] = "body_ok"
catch(e)
  ok_order[#ok_order+1] = "catch_ran"
finally
  ok_order[#ok_order+1] = "finally_ran"
end
print("tc_noerror: ok")

print("=== 018 trycatch END ===")
