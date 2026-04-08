        function hello()
            print("Hello from HMAC-protected bytecode!")
            return 42
        end
        local result = hello()
        assert(result == 42, "函数返回值不正确")
        print("[PASS] 正常执行完成")
    