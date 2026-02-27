local thread = require("thread")

print("========== Thread 库完整测试 ==========")

local function test(name, func)
    local status, err = pcall(func)
    if not status then
        print("[FAIL] " .. name .. ": " .. tostring(err))
        return false
    else
        print("[PASS] " .. name)
        return true
    end
end

local passed = 0
local failed = 0

local function run_test(name, func)
    if test(name, func) then
        passed = passed + 1
    else
        failed = failed + 1
    end
end

print("\n---------- 1. 线程创建测试 ----------")

run_test("thread.create 基本创建", function()
    local t = thread.create(function()
        return true
    end)
    assert(t ~= nil, "线程创建失败")
    t:join()
end)

run_test("thread.create 带参数", function()
    local t = thread.create(function(a, b, c)
        return a + b + c
    end, 10, 20, 30)
    local result = t:join()
    assert(result == 60, "参数传递失败，期望60，得到" .. tostring(result))
end)

run_test("thread.create 多返回值", function()
    local t = thread.create(function()
        return 1, 2, 3
    end)
    local a, b, c = t:join()
    assert(a == 1 and b == 2 and c == 3, "多返回值失败")
end)

run_test("thread.createx 同步执行", function()
    local result = thread.createx(function(x)
        return x * x
    end, 5)
    assert(result == 25, "createx失败，期望25，得到" .. tostring(result))
end)

print("\n---------- 2. 线程对象方法测试 ----------")

run_test("thread.self 获取当前线程", function()
    local current = thread.self()
    assert(current ~= nil, "获取当前线程失败")
end)

run_test("thread.current 别名测试", function()
    local current = thread.current()
    assert(current ~= nil, "current别名失败")
end)

run_test("th:id 获取线程ID", function()
    local t = thread.create(function()
        local self = thread.self()
        local id = self:id()
        assert(type(id) == "number", "线程ID应为数字")
        return id
    end)
    local id = t:join()
    assert(type(id) == "number", "返回的ID应为数字")
end)

run_test("th:name 获取设置线程名", function()
    local t = thread.create(function()
        local self = thread.self()
        self:name("test-worker")
        return self:name()
    end)
    local name = t:join()
    assert(name == "test-worker", "线程名设置失败，得到" .. tostring(name))
end)

run_test("th:name 默认名称", function()
    local t = thread.create(function()
        return thread.self():name()
    end)
    local name = t:join()
    assert(name == "thread", "默认名称应为'thread'，得到" .. tostring(name))
end)

print("\n---------- 3. Channel 基本操作测试 ----------")

run_test("thread.channel 创建通道", function()
    local ch = thread.channel()
    assert(ch ~= nil, "通道创建失败")
end)

run_test("ch:send/ch:receive 基本收发", function()
    local ch = thread.channel()
    local t = thread.create(function()
        ch:send(42)
    end)
    local val = ch:receive()
    t:join()
    assert(val == 42, "收发失败，期望42，得到" .. tostring(val))
end)

run_test("ch:push/ch:pop 别名测试", function()
    local ch = thread.channel()
    local t = thread.create(function()
        ch:push("hello")
    end)
    local val = ch:pop()
    t:join()
    assert(val == "hello", "push/pop别名失败")
end)

run_test("ch:send 发送表", function()
    local ch = thread.channel()
    local t = thread.create(function()
        ch:send({ a = 1, b = 2 })
    end)
    local val = ch:receive()
    t:join()
    assert(val.a == 1 and val.b == 2, "发送表失败")
end)

run_test("ch:send 发送字符串", function()
    local ch = thread.channel()
    local t = thread.create(function()
        ch:send("test string")
    end)
    local val = ch:receive()
    t:join()
    assert(val == "test string", "发送字符串失败")
end)

run_test("ch:send 发送布尔值", function()
    local ch = thread.channel()
    local t = thread.create(function()
        ch:send(true)
    end)
    local val = ch:receive()
    t:join()
    assert(val == true, "发送布尔值失败")
end)

print("\n---------- 4. Channel 非阻塞操作测试 ----------")

run_test("ch:try_send 成功发送", function()
    local ch = thread.channel()
    local success = ch:try_send(100)
    assert(success == true, "try_send应成功")
    local val = ch:receive()
    assert(val == 100, "接收值应为100")
end)

run_test("ch:try_recv 成功接收", function()
    local ch = thread.channel()
    ch:send(200)
    local val = ch:try_recv()
    assert(val == 200, "try_recv应返回200")
end)

run_test("ch:try_recv 空通道", function()
    local ch = thread.channel()
    local val = ch:try_recv()
    assert(val == nil, "空通道try_recv应返回nil")
end)

run_test("ch:peek 查看头部", function()
    local ch = thread.channel()
    ch:send(300)
    local val = ch:peek()
    assert(val == 300, "peek应返回300")
    local val2 = ch:receive()
    assert(val2 == 300, "receive后仍应为300")
end)

run_test("ch:peek 空通道", function()
    local ch = thread.channel()
    local val = ch:peek()
    assert(val == nil, "空通道peek应返回nil")
end)

print("\n---------- 5. Channel 关闭测试 ----------")

run_test("ch:close 关闭通道", function()
    local ch = thread.channel()
    ch:send(1)
    ch:send(2)
    ch:close()
    local v1 = ch:receive()
    local v2 = ch:receive()
    local v3 = ch:receive()
    assert(v1 == 1 and v2 == 2 and v3 == nil, "关闭后应能读取剩余数据")
end)

run_test("ch:close 后发送报错", function()
    local ch = thread.channel()
    ch:close()
    local ok, err = pcall(function()
        ch:send(1)
    end)
    assert(not ok, "关闭后发送应报错")
end)

print("\n---------- 6. Channel 类型约束测试 ----------")

run_test("thread.channel 类型约束 - number", function()
    local ch = thread.channel("number")
    local t = thread.create(function()
        ch:send(123)
    end)
    local val = ch:receive()
    t:join()
    assert(val == 123, "number类型约束失败")
end)

run_test("thread.channel 类型约束 - string", function()
    local ch = thread.channel("string")
    local t = thread.create(function()
        ch:send("abc")
    end)
    local val = ch:receive()
    t:join()
    assert(val == "abc", "string类型约束失败")
end)

run_test("thread.channel 类型约束 - table", function()
    local ch = thread.channel("table")
    local t = thread.create(function()
        ch:send({ x = 1 })
    end)
    local val = ch:receive()
    t:join()
    assert(val.x == 1, "table类型约束失败")
end)

run_test("thread.channel 类型约束 - boolean", function()
    local ch = thread.channel("boolean")
    local t = thread.create(function()
        ch:send(true)
    end)
    local val = ch:receive()
    t:join()
    assert(val == true, "boolean类型约束失败")
end)

run_test("thread.channel 类型约束 - function", function()
    local ch = thread.channel("function")
    local t = thread.create(function()
        ch:send(function() return 42 end)
    end)
    local val = ch:receive()
    t:join()
    assert(val() == 42, "function类型约束失败")
end)

print("\n---------- 7. thread.pick 选择器测试 ----------")

run_test("thread.pick 单通道", function()
    local ch = thread.channel()
    ch:send("pick-test")
    local result = thread.pick {
        { thread.on(ch), function(val)
            return val
        end }
    }
    assert(result == "pick-test", "pick单通道失败")
end)

run_test("thread.pick 多通道", function()
    local ch1 = thread.channel()
    local ch2 = thread.channel()
    ch1:send("from-ch1")
    local result = thread.pick {
        { thread.on(ch1), function(val)
            return "ch1:" .. val
        end },
        { thread.on(ch2), function(val)
            return "ch2:" .. val
        end }
    }
    assert(result == "ch1:from-ch1", "pick多通道失败")
end)

run_test("thread.on 创建操作描述符", function()
    local ch = thread.channel()
    local op = thread.on(ch)
    assert(op.op == "recv", "op类型应为recv")
    assert(op.ch == ch, "ch应为原通道")
end)

run_test("ch:recv_op 创建操作描述符", function()
    local ch = thread.channel()
    local op = ch:recv_op()
    assert(op.op == "recv", "recv_op类型应为recv")
    assert(op.ch == ch, "ch应为原通道")
end)

print("\n---------- 8. thread.over 超时测试 ----------")

run_test("thread.over 创建超时描述符", function()
    local op = thread.over(1.0)
    assert(op.op == "timeout", "op类型应为timeout")
    assert(op.duration == 1.0, "duration应为1.0")
end)

run_test("thread.pick 超时触发", function()
    local ch = thread.channel()
    local start = os.clock()
    local result = thread.pick {
        { thread.on(ch), function(val)
            return "received"
        end },
        { thread.over(0.5), function()
            return "timeout"
        end }
    }
    local elapsed = os.clock() - start
    assert(result == "timeout", "应触发超时")
    assert(elapsed >= 0.4, "应等待约0.5秒")
end)

print("\n---------- 9. 生产者-消费者模式测试 ----------")

run_test("生产者-消费者", function()
    local ch = thread.channel()
    local produced = {}
    local consumed = {}

    local producer = thread.create(function()
        for i = 1, 5 do
            ch:send(i)
            table.insert(produced, i)
        end
        ch:close()
    end)

    local consumer = thread.create(function()
        while true do
            local val = ch:receive()
            if val == nil then break end
            table.insert(consumed, val)
        end
    end)

    producer:join()
    consumer:join()

    assert(#consumed == 5, "应消费5个元素")
    for i = 1, 5 do
        assert(consumed[i] == i, "消费顺序错误")
    end
end)

print("\n---------- 10. 多线程并行计算测试 ----------")

run_test("多线程并行计算", function()
    local threads = {}
    local results = {}

    for i = 1, 4 do
        threads[i] = thread.create(function(id)
            local sum = 0
            for j = 1, 10000 do
                sum = sum + j
            end
            return id, sum
        end, i)
    end

    for i, t in ipairs(threads) do
        local id, sum = t:join()
        results[id] = sum
    end

    local expected = 0
    for j = 1, 10000 do
        expected = expected + j
    end

    for i = 1, 4 do
        assert(results[i] == expected, "线程" .. i .. "计算结果错误")
    end
end)

print("\n---------- 11. 线程间通信测试 ----------")

run_test("双向通信", function()
    local ch1 = thread.channel()
    local ch2 = thread.channel()

    local t1 = thread.create(function()
        ch1:send("hello from t1")
        local msg = ch2:receive()
        return msg
    end)

    local t2 = thread.create(function()
        local msg = ch1:receive()
        ch2:send("reply to: " .. msg)
    end)

    t2:join()
    local reply = t1:join()
    assert(reply == "reply to: hello from t1", "双向通信失败")
end)

run_test("广播模式", function()
    local ch = thread.channel()
    local results = {}

    local producer = thread.create(function()
        for i = 1, 3 do
            ch:send(i)
        end
        ch:close()
    end)

    local consumers = {}
    for i = 1, 3 do
        consumers[i] = thread.create(function(id)
            local count = 0
            while true do
                local val = ch:receive()
                if val == nil then break end
                count = count + 1
            end
            return id, count
        end, i)
    end

    producer:join()
    for i, c in ipairs(consumers) do
        local id, count = c:join()
        results[id] = count
    end
end)

print("\n---------- 12. 边界条件测试 ----------")

run_test("空参数创建线程", function()
    local t = thread.create(function()
        return "no args"
    end)
    local result = t:join()
    assert(result == "no args", "无参数线程失败")
end)

run_test("大量参数传递", function()
    local t = thread.create(function(a, b, c, d, e)
        return a + b + c + d + e
    end, 1, 2, 3, 4, 5)
    local result = t:join()
    assert(result == 15, "多参数传递失败")
end)

run_test("线程返回nil", function()
    local t = thread.create(function()
        return nil
    end)
    local result = t:join()
    assert(result == nil, "返回nil失败")
end)

run_test("通道发送nil", function()
    local ch = thread.channel("nil_type")
    local t = thread.create(function()
        ch:send(nil)
    end)
    t:join()
    local val = ch:receive()
    assert(val == nil, "发送nil失败")
end)

print("\n========== 测试结果汇总 ==========")
print(string.format("通过: %d", passed))
print(string.format("失败: %d", failed))
print("==================================")

if failed > 0 then
    os.exit(1)
else
    print("所有测试通过!")
    os.exit(0)
end
