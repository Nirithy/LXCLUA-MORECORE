local thread = require "thread"

local ch1 = thread.channel()
local ch2 = thread.channel()

print("Test 1: Pick with data")
local t1 = thread.create(function(c)
    local start = os.time()
    while os.time() - start < 1 do end
    c:send("hello from t1")
end, ch1)

local cases = {
    thread.on(ch1) => function(v)
        print("Callback ch1:", v)
        return "got it"
    end,
    thread.over(2.0) => function()
        print("Timeout 1")
        return "timeout"
    end
}

local res = thread.pick(cases)
assert(res == "got it")

t1:join()

print("Test 2: Timeout")
local res2 = thread.pick {
    thread.on(ch2) => function() print("Should not happen") end,
    thread.over(0.5) => function()
        print("Timeout 2 hit")
        return "timeout"
    end
}
assert(res2 == "timeout")

print("Done")
