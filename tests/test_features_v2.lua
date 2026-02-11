local function assert_eq(a, b, msg)
    if a ~= b then
        error(msg .. ": " .. tostring(a) .. " ~= " .. tostring(b))
    end
end

print("Testing Struct Defaults...")
struct Point { x: int = 1, y = 2 }
local p = Point()
assert_eq(p.x, 1, "p.x default")
assert_eq(p.y, 2, "p.y default")
local p2 = Point{x=10}
assert_eq(p2.x, 10, "p2.x set")
assert_eq(p2.y, 2, "p2.y default")

print("Testing Struct Arrays...")
struct Vector3 { x=0.0, y=0.0, z=0.0 }
local N = 10
local arr = array(Vector3)[N]
assert_eq(#arr, N, "array len")

local v = Vector3{x=1, y=2, z=3}
arr[1] = v
local v2 = arr[1]
assert_eq(v2.x, 1.0, "arr[1].x")
assert_eq(v2.y, 2.0, "arr[1].y")
assert_eq(v2.z, 3.0, "arr[1].z")

-- Check copy semantics
v.x = 99
local v3 = arr[1]
assert_eq(v3.x, 1.0, "arr[1].x should not change when v changes")

arr[2] = arr[1]
local v4 = arr[2]
assert_eq(v4.x, 1.0, "arr[2].x copy from arr[1]")

print("Testing Channels...")
local thread = require "thread"
local ch = thread.channel()

-- try_recv empty
local val = ch:try_recv()
assert_eq(val, nil, "try_recv empty")

-- try_send
local ok = ch:try_send(123)
assert_eq(ok, true, "try_send success")

-- try_recv data
val = ch:try_recv()
assert_eq(val, 123, "try_recv data")

val = ch:try_recv()
assert_eq(val, nil, "try_recv empty again")

-- close
ch:close()
local ok_closed = ch:try_send(456)
assert_eq(ok_closed, false, "try_send closed")

print("Testing Thread Identity...")
local t_self = thread.self()
if t_self then
    print("thread.self() returned object")
    assert_eq(thread.current(), t_self, "thread.current() alias")

    local id = t_self:id()
    print("Current thread ID:", id)
    -- assert(id ~= 0, "Thread ID valid") -- Main thread ID might be anything

    local name = t_self:name()
    print("Current thread Name:", name)
    assert_eq(name, "thread", "Default name")

    t_self:name("main_worker")
    assert_eq(t_self:name(), "main_worker", "Set name")
else
    print("thread.self() returned nil (not created via thread.create?)")
    -- Main thread should have been auto-created by thread_self logic
    error("thread.self() failed")
end

print("All tests passed!")
