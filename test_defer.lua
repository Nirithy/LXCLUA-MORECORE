print("Testing defer...")

local log = {}

local function append(msg)
    table.insert(log, msg)
end

local function test_basic()
    log = {}
    append("start")
    do
        defer append("deferred")
        append("body")
    end
    append("end")
    assert(log[1] == "start")
    assert(log[2] == "body")
    assert(log[3] == "deferred")
    assert(log[4] == "end")
    print("Basic test passed")
end

local function test_lifo()
    log = {}
    do
        defer append("first")
        defer append("second")
        append("body")
    end
    assert(log[1] == "body")
    assert(log[2] == "second")
    assert(log[3] == "first")
    print("LIFO test passed")
end

local function test_error()
    log = {}
    pcall(function()
        defer append("cleanup")
        append("body")
        error("oops")
    end)
    assert(log[1] == "body")
    assert(log[2] == "cleanup")
    print("Error handling test passed")
end

local function test_block()
    log = {}
    do
        defer
            append("block_defer")

        -- This code is inside the do block
    end
    -- Defer runs at end of do block.

    append("body")

    assert(log[1] == "block_defer")
    assert(log[2] == "body")
    print("Block defer passed")
end

local function test_explicit_block()
    log = {}
    do
        defer do
            append("explicit_block")
        end
        append("body")
    end
    assert(log[1] == "body")
    assert(log[2] == "explicit_block")
    print("Explicit block defer passed")
end

local function test_closure()
    log = {}
    local x = 10
    do
        defer append(x)
        x = 20
    end
    assert(log[1] == 20)
    print("Closure capture passed")
end

test_basic()
test_lifo()
test_error()
test_block()
test_explicit_block()
test_closure()

print("All tests passed!")
