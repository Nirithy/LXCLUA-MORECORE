local thread = require "thread"

print("Testing Channel...")
local ch = thread.channel()
ch:send("hello")
local val = ch:receive()
print("Received from channel:", val)
assert(val == "hello")

print("Testing Channel between threads...")
local ch2 = thread.channel()
thread.create(function(c)
    -- Share global state
    c:send("from thread")
end, ch2)

local msg = ch2:receive()
print("Received from thread:", msg)
assert(msg == "from thread")

print("Testing async/await...")

async function test_async(val)
    print("Inside async function, val:", val)
    local result = await "yield_value"
    print("Await returned:", result)
    return result .. "_done"
end

-- async function returns the task (thread)
local task = test_async("start")
assert(type(task) == "thread")

-- Resume the task (simulating a scheduler)
-- The first resume was done by __async_wrap. The task is suspended at 'await'.
-- We resume it providing the result of the 'await'.
local status, res = coroutine.resume(task, "resolved")
print("Resume status:", status, "result:", res)

assert(status == true)
assert(res == "resolved_done")

print("All tests passed!")
