-- Test coroutine safety for breakpoints

local function worker(name, steps)
    for i = 1, steps do
        print(string.format("Worker %s at step %d", name, i))
    end
end

-- Set hook for main thread just to have hookf active
debug.sethook(function() end, "l")

local co1 = coroutine.create(function()
    -- Inherit hookf from main state (hookf is the C hook)
    debug.sethook(coroutine.running(), debug.gethook(), "l")
    debug.step(coroutine.running())
    worker("A", 2)
end)

local co2 = coroutine.create(function()
    debug.sethook(coroutine.running(), debug.gethook(), "l")
    worker("B", 2)
end)

debug.setoutputcallback(function(event, src, line)
    local thread = coroutine.running()
    print(string.format("[%s] Thread %s at %s:%d", event, tostring(thread), src, line))
    debug.step(thread)
end)

print("--- Scenario: Stepping in Coroutine A should not affect Coroutine B ---")

print("Resuming Coroutine A...")
coroutine.resume(co1)
-- Should see [step] for A

print("Resuming Coroutine B...")
coroutine.resume(co2)
-- Should NOT see [step] for B

print("Resuming Coroutine A again...")
coroutine.resume(co1)
