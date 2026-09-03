-- covers: TK_ASYNC async function, TK_AWAIT await, 异步调度同步执行, asyncio Promise await_sync
-- 期望语法：
-- local async function fetch() return 42 end
-- local async function main()
--   local order={"start"}
--   order[#order+1]="before"
--   local v=await fetch()
--   order[#order+1]="after"
--   print("async_result:", v)
--   order[#order+1]="done"
--   print("async_order:", table.concat(order, ","))
-- end
-- main()

print("=== 019 async START ===")

local async function fetch()
  return 42
end

local async function main()
  local order = {"start"}
  order[#order+1] = "before"
  local v = await fetch()
  order[#order+1] = "after"
  print("async_result: " .. v)
  order[#order+1] = "done"
  print("async_order: " .. table.concat(order, ","))
end

-- 调用 main()：由于 fetch() 是立即返回的同步异步函数，
-- main 内部的 print 会在调用时就同步执行完毕
local promise = main()
-- 若 promise 尚未完成（真正 I/O 场景），可调用 promise:await_sync() 同步等待

print("skipped_async: 0")

print("=== 019 async END ===")
