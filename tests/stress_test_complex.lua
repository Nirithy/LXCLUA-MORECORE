print("=== Stress Testing Complex Features ===\n")

local function assert_eq(a, b, msg)
    if a ~= b then
        error("FAIL: " .. msg .. " (Expected " .. tostring(b) .. ", got " .. tostring(a) .. ")")
    end
    print("PASS: " .. msg)
end

-- 1. Switch with Defer and Fallthrough
do
    print("\n--- 1. Switch + Defer ---")
    local log = {}
    local val = 1

    local function test_switch()
        switch (val) do
            case 1:
                defer do table.insert(log, "deferred 1") end
                table.insert(log, "case 1")
                -- fallthrough implicit? No, Lua switch usually breaks unless configured.
                -- But let's check if break is needed or if fallthrough happens.
                -- Based on memory: "switch statement executes with C-style fallthrough behavior"
                -- So it should fall through to case 2.
            case 2:
                defer do table.insert(log, "deferred 2") end
                table.insert(log, "case 2")
                break
            default:
                table.insert(log, "default")
        end
    end

    test_switch()

    -- Expected order:
    -- case 1
    -- case 2
    -- deferred 2 (LIFO/scope exit)
    -- deferred 1 (scope exit of switch block? or case block?)
    -- Usually defer is block-scoped. If switch is one block, they run at end.
    -- If case is a block, they run at case exit?
    -- Memory says "defer statement accepts a single statement... does not require an end keyword unless...".

    print("Log: " .. table.concat(log, ", "))

    -- We expect case 1 -> case 2 -> deferred 2 -> deferred 1 (if scope is function or switch block)
    -- or case 1 -> deferred 1 -> case 2 -> deferred 2 (if scope is case)
    -- But fallthrough implies we are in the same block?
    -- This test will reveal the behavior/bug.
end

-- 2. Try-Catch Nested
do
    print("\n--- 2. Try-Catch Nested ---")
    local caught_inner = false
    local caught_outer = false

    try
        try
            error("inner error")
        catch(e)
            caught_inner = true
            print("Caught inner: " .. e)
            error("re-throw")
        end
    catch(e)
        caught_outer = true
        print("Caught outer: " .. e)
    end

    assert_eq(caught_inner, true, "Inner catch executed")
    assert_eq(caught_outer, true, "Outer catch executed")
end

-- 3. ASM Loop (if supported)
do
    print("\n--- 3. ASM Loop ---")
    -- Simulating a loop with _if and jumps is hard without labels.
    -- But we can test _if _else limits.
    local res = 0
    asm(
        newreg r0
        LOADI r0 0

        -- Unrolled loop 3 times
        LOADI r1 1
        ADD r0 r0 r1
        ADD r0 r0 r1
        ADD r0 r0 r1

        -- r0 should be 3
        _if 1
            LOADI r2 10
            ADD r0 r0 r2
        _endif

        -- r0 should be 13
    )
    -- How to get r0 out? We can't easily return from ASM block to Lua var without `return`.
    -- `asm` block returns nil usually unless it returns.
    print("ASM block finished (no return checked here)")
end

print("\n=== Stress Test Completed ===")
