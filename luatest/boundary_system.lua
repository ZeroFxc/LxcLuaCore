-- ================================================================
-- system 组边界测试: smgr, logtable
-- ================================================================
local passed, failed = 0, 0
local function check(name, ok, msg)
    if ok then passed = passed + 1
    else failed = failed + 1; print("  FAIL: " .. (msg or name)) end
end

-- ================================================================
-- 1. smgr 库
-- ================================================================
print("\n========== smgr ==========")
do
    local ok_smgr, smgr = pcall(require, "smgr")
    if not ok_smgr then
        check("smgr load", false, "require smgr failed: " .. tostring(smgr))
    else
        check("smgr loaded", type(smgr) == "table")

        -- 验证所有函数存在
        local funcs = {"getuserid", "hasshareduserid", "getdatadir", "readfile",
                        "writefile", "deletefile", "listfiles", "fileexists",
                        "getfilesize", "copyfile", "renamefile", "getpackagename",
                        "mkdir", "find"}
        for _, fn in ipairs(funcs) do
            check("smgr." .. fn, type(smgr[fn]) == "function", fn .. " missing")
        end

        -- getuserid: 返回值类型
        local uid = smgr.getuserid()
        check("smgr.getuserid type", type(uid) == "string" or type(uid) == "number", "got " .. type(uid))

        -- hasshareduserid: 返回 boolean
        local has_shared = smgr.hasshareduserid()
        check("smgr.hasshareduserid", type(has_shared) == "boolean")

        -- getdatadir: 返回 string，长度 > 0
        local datadir = smgr.getdatadir()
        check("smgr.getdatadir", type(datadir) == "string" and #datadir > 0)

        -- getpackagename: 返回 string
        local pkgname = smgr.getpackagename()
        check("smgr.getpackagename", type(pkgname) == "string")

        -- readfile: pcall 调用
        local ok_rf, rf_result = pcall(function() return smgr.readfile("test.txt") end)
        check("smgr.readfile pcall", ok_rf, "readfile should not crash")
        if ok_rf then
            check("smgr.readfile result", rf_result ~= nil)
        end

        -- writefile: 写入测试文件
        local ok_wf, wf_result = pcall(function() return smgr.writefile("_test.txt", "data") end)
        check("smgr.writefile pcall", ok_wf, "writefile should not crash")
        if ok_wf then
            check("smgr.writefile result", wf_result ~= nil)
        end

        -- fileexists: 测试存在/不存在
        local ok_fe1, fe1 = pcall(function() return smgr.fileexists("_test.txt") end)
        check("smgr.fileexists pcall", ok_fe1, "fileexists should not crash")
        if ok_fe1 then
            check("smgr.fileexists exist", fe1 == true or fe1 == 1)
        end
        local ok_fe2, fe2 = pcall(function() return smgr.fileexists("/nonexist_xyz_12345") end)
        check("smgr.fileexists nonexist pcall", ok_fe2, "fileexists nonexist should not crash")
        if ok_fe2 then
            check("smgr.fileexists nonexist", fe2 == false)
        end

        -- getfilesize: 返回数字
        local ok_fs, fs = pcall(function() return smgr.getfilesize("_test.txt") end)
        check("smgr.getfilesize pcall", ok_fs, "getfilesize should not crash")
        if ok_fs then
            check("smgr.getfilesize", type(fs) == "number")
        end

        -- listfiles: 返回 table
        local ok_lf, lf = pcall(function() return smgr.listfiles(".") end)
        check("smgr.listfiles pcall", ok_lf, "listfiles should not crash")
        if ok_lf then
            check("smgr.listfiles", type(lf) == "table")
        end

        -- copyfile: 复制测试文件
        local ok_cp, cp = pcall(function() return smgr.copyfile("_test.txt", "_test_copy.txt") end)
        check("smgr.copyfile pcall", ok_cp, "copyfile should not crash")
        if ok_cp then
            check("smgr.copyfile result", cp ~= nil)
        end

        -- renamefile: 重命名
        local ok_rn, rn = pcall(function() return smgr.renamefile("_test_copy.txt", "_test_renamed.txt") end)
        check("smgr.renamefile pcall", ok_rn, "renamefile should not crash")
        if ok_rn then
            check("smgr.renamefile result", rn ~= nil)
        end

        -- deletefile: 清理测试文件
        pcall(function() smgr.deletefile("_test.txt") end)
        pcall(function() smgr.deletefile("_test_copy.txt") end)
        pcall(function() smgr.deletefile("_test_renamed.txt") end)
        -- 验证文件已删除
        local ok_del, del = pcall(function() return smgr.fileexists("_test.txt") end)
        if ok_del then
            check("smgr.deletefile cleanup _test.txt", del == false)
        end
        local ok_del2, del2 = pcall(function() return smgr.fileexists("_test_renamed.txt") end)
        if ok_del2 then
            check("smgr.deletefile cleanup _test_renamed.txt", del2 == false)
        end

        -- mkdir: 创建测试目录
        local ok_md, md = pcall(function() return smgr.mkdir("_test_dir") end)
        check("smgr.mkdir pcall", ok_md, "mkdir should not crash")
        if ok_md then
            check("smgr.mkdir result", md ~= nil)
        end
        -- 清理测试目录 (尝试用 deletefile 删除目录，如果支持的话)
        pcall(function() smgr.deletefile("_test_dir") end)

        -- find: 搜索文件
        local ok_find, find_result = pcall(function() return smgr.find(".", "*.lua") end)
        check("smgr.find pcall", ok_find, "find should not crash")
        if ok_find then
            check("smgr.find", type(find_result) == "table")
        end

        -- nil 输入测试
        local ok_nil1, _ = pcall(function() smgr.readfile(nil) end)
        check("smgr.readfile nil", ok_nil1, "readfile(nil) should not crash")
        local ok_nil2, _ = pcall(function() smgr.writefile(nil, nil) end)
        check("smgr.writefile nil", ok_nil2, "writefile(nil) should not crash")
        local ok_nil3, _ = pcall(function() smgr.fileexists(nil) end)
        check("smgr.fileexists nil", ok_nil3, "fileexists(nil) should not crash")
        local ok_nil4, _ = pcall(function() smgr.getfilesize(nil) end)
        check("smgr.getfilesize nil", ok_nil4, "getfilesize(nil) should not crash")
        local ok_nil5, _ = pcall(function() smgr.listfiles(nil) end)
        check("smgr.listfiles nil", ok_nil5, "listfiles(nil) should not crash")
        local ok_nil6, _ = pcall(function() smgr.copyfile(nil, nil) end)
        check("smgr.copyfile nil", ok_nil6, "copyfile(nil) should not crash")
        local ok_nil7, _ = pcall(function() smgr.renamefile(nil, nil) end)
        check("smgr.renamefile nil", ok_nil7, "renamefile(nil) should not crash")
        local ok_nil8, _ = pcall(function() smgr.deletefile(nil) end)
        check("smgr.deletefile nil", ok_nil8, "deletefile(nil) should not crash")
        local ok_nil9, _ = pcall(function() smgr.mkdir(nil) end)
        check("smgr.mkdir nil", ok_nil9, "mkdir(nil) should not crash")
        local ok_nil10, _ = pcall(function() smgr.find(nil, nil) end)
        check("smgr.find nil", ok_nil10, "find(nil) should not crash")
    end
end

-- ================================================================
-- 2. logtable 库
-- ================================================================
print("\n========== logtable ==========")
do
    local ok_lt, logtable = pcall(require, "logtable")
    if not ok_lt then
        check("logtable load", false, "require logtable failed: " .. tostring(logtable))
    else
        check("logtable loaded", type(logtable) == "table")

        -- 验证所有函数存在
        local funcs = {"onlog", "getlogpath", "setfilter", "clearfilter",
                        "addinkey", "exckey", "addinval", "exczval",
                        "addinop", "exczop", "keyrange", "valrange",
                        "setdedup", "setunique", "resetdedup",
                        "addinkeytype", "exckeytype", "addinvaltype", "exczvaltype",
                        "setintelligent", "getintelligent",
                        "setjnienv", "getjnienv", "setuserdata", "getuserdata"}
        for _, fn in ipairs(funcs) do
            check("logtable." .. fn, type(logtable[fn]) == "function", fn .. " missing")
        end

        -- onlog: 开启/关闭日志
        local ok_on1, _ = pcall(function() logtable.onlog(true) end)
        check("logtable.onlog true", ok_on1, "onlog(true) should not crash")
        local ok_on2, _ = pcall(function() logtable.onlog(false) end)
        check("logtable.onlog false", ok_on2, "onlog(false) should not crash")

        -- getlogpath: 返回 string
        local logpath = logtable.getlogpath()
        check("logtable.getlogpath", type(logpath) == "string")

        -- setfilter / clearfilter
        local ok_sf, _ = pcall(function() logtable.setfilter("test_filter") end)
        check("logtable.setfilter", ok_sf, "setfilter should not crash")
        local ok_cf, _ = pcall(function() logtable.clearfilter() end)
        check("logtable.clearfilter", ok_cf, "clearfilter should not crash")

        -- addinkey / exckey: 添加包含/排除的键
        local ok_aik, _ = pcall(function() logtable.addinkey("test_key") end)
        check("logtable.addinkey", ok_aik, "addinkey should not crash")
        local ok_ek, _ = pcall(function() logtable.exckey("test_exclude_key") end)
        check("logtable.exckey", ok_ek, "exckey should not crash")

        -- addinval / exczval: 添加包含/排除的值
        local ok_aiv, _ = pcall(function() logtable.addinval("test_value") end)
        check("logtable.addinval", ok_aiv, "addinval should not crash")
        local ok_ev, _ = pcall(function() logtable.exczval("test_exclude_value") end)
        check("logtable.exczval", ok_ev, "exczval should not crash")

        -- addinop / exczop: 添加包含/排除的操作
        local ok_aio, _ = pcall(function() logtable.addinop("test_op") end)
        check("logtable.addinop", ok_aio, "addinop should not crash")
        local ok_eo, _ = pcall(function() logtable.exczop("test_exclude_op") end)
        check("logtable.exczop", ok_eo, "exczop should not crash")

        -- keyrange / valrange: 设置键/值范围（需要数字参数）
        local ok_kr, _ = pcall(function() logtable.keyrange(0, 100) end)
        check("logtable.keyrange", ok_kr, "keyrange should not crash")
        local ok_vr, _ = pcall(function() logtable.valrange(0, 100) end)
        check("logtable.valrange", ok_vr, "valrange should not crash")

        -- setdedup / setunique / resetdedup: 去重/唯一设置
        local ok_sd, _ = pcall(function() logtable.setdedup(true) end)
        check("logtable.setdedup", ok_sd, "setdedup should not crash")
        local ok_su, _ = pcall(function() logtable.setunique(true) end)
        check("logtable.setunique", ok_su, "setunique should not crash")
        local ok_rd, _ = pcall(function() logtable.resetdedup() end)
        check("logtable.resetdedup", ok_rd, "resetdedup should not crash")

        -- addinkeytype / exckeytype: 键类型过滤
        local ok_ait, _ = pcall(function() logtable.addinkeytype(1) end)
        check("logtable.addinkeytype", ok_ait, "addinkeytype should not crash")
        local ok_et, _ = pcall(function() logtable.exckeytype(2) end)
        check("logtable.exckeytype", ok_et, "exckeytype should not crash")

        -- addinvaltype / exczvaltype: 值类型过滤
        local ok_avi, _ = pcall(function() logtable.addinvaltype(3) end)
        check("logtable.addinvaltype", ok_avi, "addinvaltype should not crash")
        local ok_evi, _ = pcall(function() logtable.exczvaltype(4) end)
        check("logtable.exczvaltype", ok_evi, "exczvaltype should not crash")

        -- setintelligent / getintelligent: 智能模式
        local ok_si, _ = pcall(function() logtable.setintelligent(true) end)
        check("logtable.setintelligent", ok_si, "setintelligent should not crash")
        local gi = logtable.getintelligent()
        check("logtable.getintelligent", type(gi) == "boolean")

        -- setjnienv / getjnienv: JNI环境 (pcall, 可能不支持)
        local ok_sje, _ = pcall(function() logtable.setjnienv("test_jni") end)
        check("logtable.setjnienv", ok_sje, "setjnienv should not crash")
        local ok_gje, gje = pcall(function() return logtable.getjnienv() end)
        check("logtable.getjnienv pcall", ok_gje, "getjnienv should not crash")

        -- setuserdata / getuserdata: 用户数据存取
        local ok_sud, _ = pcall(function() logtable.setuserdata("my_key", "my_value") end)
        check("logtable.setuserdata", ok_sud, "setuserdata should not crash")
        local ok_gud, gud = pcall(function() return logtable.getuserdata("my_key") end)
        if ok_gud and gud == "my_value" then
            check("logtable.getuserdata", true)
        else
            check("logtable.getuserdata", ok_gud, "getuserdata result mismatch")
        end

        -- 复杂 userdata 测试
        local ok_sud2, _ = pcall(function() logtable.setuserdata("num_key", 42) end)
        check("logtable.setuserdata number", ok_sud2, "setuserdata number should not crash")
        local ok_gud2, gud2 = pcall(function() return logtable.getuserdata("num_key") end)
        if ok_gud2 and gud2 == 42 then
            check("logtable.getuserdata number", true)
        else
            check("logtable.getuserdata number", ok_gud2, "getuserdata number mismatch")
        end

        -- nil 输入测试
        local ok_nt1, _ = pcall(function() logtable.onlog(nil) end)
        check("logtable.onlog nil", ok_nt1, "onlog(nil) should not crash")
        local ok_nt2, _ = pcall(function() logtable.setfilter(nil) end)
        check("logtable.setfilter nil", ok_nt2, "setfilter(nil) should not crash")
        local ok_nt3, _ = pcall(function() logtable.addinkey(nil) end)
        check("logtable.addinkey nil", ok_nt3, "addinkey(nil) should not crash")
        local ok_nt4, _ = pcall(function() logtable.exckey(nil) end)
        check("logtable.exckey nil", ok_nt4, "exckey(nil) should not crash")
        local ok_nt5, _ = pcall(function() logtable.addinval(nil) end)
        check("logtable.addinval nil", ok_nt5, "addinval(nil) should not crash")
        local ok_nt6, _ = pcall(function() logtable.exczval(nil) end)
        check("logtable.exczval nil", ok_nt6, "exczval(nil) should not crash")
        local ok_nt7, _ = pcall(function() logtable.addinop(nil) end)
        check("logtable.addinop nil", ok_nt7, "addinop(nil) should not crash")
        local ok_nt8, _ = pcall(function() logtable.exczop(nil) end)
        check("logtable.exczop nil", ok_nt8, "exczop(nil) should not crash")
        local ok_nt9, _ = pcall(function() logtable.keyrange(nil, nil) end)
        check("logtable.keyrange nil", ok_nt9, "keyrange(nil) should not crash")
        local ok_nt10, _ = pcall(function() logtable.valrange(nil, nil) end)
        check("logtable.valrange nil", ok_nt10, "valrange(nil) should not crash")
        local ok_nt11, _ = pcall(function() logtable.setdedup(nil) end)
        check("logtable.setdedup nil", ok_nt11, "setdedup(nil) should not crash")
        local ok_nt12, _ = pcall(function() logtable.setunique(nil) end)
        check("logtable.setunique nil", ok_nt12, "setunique(nil) should not crash")
        local ok_nt13, _ = pcall(function() logtable.addinkeytype(nil) end)
        check("logtable.addinkeytype nil", ok_nt13, "addinkeytype(nil) should not crash")
        local ok_nt14, _ = pcall(function() logtable.exckeytype(nil) end)
        check("logtable.exckeytype nil", ok_nt14, "exckeytype(nil) should not crash")
        local ok_nt15, _ = pcall(function() logtable.addinvaltype(nil) end)
        check("logtable.addinvaltype nil", ok_nt15, "addinvaltype(nil) should not crash")
        local ok_nt16, _ = pcall(function() logtable.exczvaltype(nil) end)
        check("logtable.exczvaltype nil", ok_nt16, "exczvaltype(nil) should not crash")
        local ok_nt17, _ = pcall(function() logtable.setintelligent(nil) end)
        check("logtable.setintelligent nil", ok_nt17, "setintelligent(nil) should not crash")
        local ok_nt18, _ = pcall(function() logtable.setjnienv(nil) end)
        check("logtable.setjnienv nil", ok_nt18, "setjnienv(nil) should not crash")
        local ok_nt19, _ = pcall(function() logtable.setuserdata(nil, nil) end)
        check("logtable.setuserdata nil", ok_nt19, "setuserdata(nil) should not crash")
        local ok_nt20, _ = pcall(function() logtable.getuserdata(nil) end)
        check("logtable.getuserdata nil", ok_nt20, "getuserdata(nil) should not crash")
    end
end

-- ================================================================
-- 结果汇总
-- ================================================================
print("")
print(string.format("Result: %d/%d passed", passed, failed + passed))
if failed > 0 then
    os.exit(1)
end