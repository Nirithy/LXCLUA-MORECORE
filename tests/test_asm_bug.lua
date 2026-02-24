print("Testing ASM bug reproduction...")

function assert_eq(a, b, msg)
    if a ~= b then
        error(string.format("Assertion failed: %s (expected %s, got %s)", msg or "", tostring(b), tostring(a)))
    end
end

-- 1. ASM Control Flow
do
    print("1. Testing ASM Control Flow...")
    local function run_asm()
        local val = 0
        asm(
            newreg r0
            LOADI r0 10
            _if 1
                LOADI r0 20
            _else
                LOADI r0 30
            _endif

            _if 0
               LOADI r0 40
            _endif

            -- Move result to val (assuming val is in a register or can be set)
            -- But wait, local variables inside asm block need careful handling.
            -- Let's try to return the value.
            RETURN1 r0
        )
    end
    local res = run_asm()
    print("Result:", res)
    assert_eq(res, 20, "ASM _if branch executed correctly")
end

-- 2. ASM Junk
do
    print("2. Testing ASM Junk (Skipping execution)...")
    local function run_junk()
        asm(
            _if 0
            junk "some_random_string_data"
            _endif
            junk 5
        )
    end
    run_junk()
    print("ASM Junk passed")
end

print("ASM bug reproduction complete.")
