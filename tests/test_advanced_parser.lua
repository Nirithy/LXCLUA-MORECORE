print("Testing Advanced Parser Features...")

function assert_eq(a, b, msg)
    if a ~= b then
        error(string.format("Assertion failed: %s (expected %s, got %s)", msg or "", tostring(b), tostring(a)))
    end
end

-- 1. Preprocessor $alias
do
    print("1. Testing $alias...")
    $alias MY_CONST = 123
    assert_eq(MY_CONST, 123, "$alias simple")

    $alias ADD_OP = +
    assert_eq(10 ADD_OP 20, 30, "$alias operator")
end

-- 2. Preprocessor $type and $declare
do
    print("2. Testing $type and $declare...")
    $type MyInt = int
    $declare globalVar: MyInt

    -- globalVar is nil initially
    -- assert_eq(globalVar, nil, "$declare initial value")
    globalVar = 10
    assert_eq(globalVar, 10, "$declare assignment")
end

-- 3. ASM Directives (rep, db, dw, dd, str, junk, _if)
do
    print("3. Testing ASM Directives...")

    local function run_asm_rep()
        local res = 0
        asm(
            newreg r0
            LOADI r0 0

            -- rep: repeat instructions
            rep 5 {
                ADDI r0 r0 1
            }

            RETURN1 r0
        )
    end
    assert_eq(run_asm_rep(), 5, "asm rep")

    local function run_asm_data()
        -- Note: db/dw/dd/str emit raw bytes into the instruction stream.
        -- This is dangerous to execute unless it forms valid instructions or is skipped.
        -- Here we just test parsing (compilation), not execution of data as code.
        -- We jump over the data.
        asm(
            newreg r0
            LOADI r0 100
            JMP 2  -- Skip data
            dd 0   -- data (NOP)
            RETURN1 r0
        )
    end
    -- Just verify it compiles and runs without crashing
    run_asm_data()
    print("   asm data directives parsed.")

    local function run_asm_str()
        asm(
            newreg r0
            LOADI r0 200
            JMP 2
            str "junk" -- emits 4-byte aligned data instructions (extraarg)
            RETURN1 r0
        )
    end
    run_asm_str()
    print("   asm str directive parsed.")

    local function run_asm_if()
        asm(
            newreg r0
            LOADI r0 0

            _if 1 == 1
               LOADI r0 1
            _else
               LOADI r0 2
            _endif

            RETURN1 r0
        )
    end
    assert_eq(run_asm_if(), 1, "asm _if true")

    local function run_asm_if_false()
        asm(
            newreg r0
            LOADI r0 0

            _if 1 == 0
               LOADI r0 1
            _else
               LOADI r0 2
            _endif

            RETURN1 r0
        )
    end
    assert_eq(run_asm_if_false(), 2, "asm _if false")
end

-- 4. Using Member & Namespace
do
    print("4. Testing Namespace & Using...")
    namespace Lib {
        val = 42
        function func() return 100 end
    }

    using Lib::val;
    assert_eq(val, 42, "using Lib::val")

    using Lib::func;
    assert_eq(func(), 100, "using Lib::func")
end

-- 5. Sealed Class (Syntax Check)
do
    print("5. Testing Sealed Class...")
    sealed class SealedClass
        function msg() return "sealed" end
    end
    local s = SealedClass()
    assert_eq(s:msg(), "sealed", "sealed class instantiation")

    -- Runtime enforcement of 'sealed' might not be implemented in this test,
    -- but we verify the parser accepts 'sealed class'.
end

-- 6. Concept
do
    print("6. Testing Concept...")
    concept IsEven(x)
        return x % 2 == 0
    end

    assert_eq(IsEven(4), true, "concept even")
    assert_eq(IsEven(3), false, "concept odd")

    -- Concept expression syntax
    concept IsOdd(x) = x % 2 != 0
    assert_eq(IsOdd(3), true, "concept expression true")
    assert_eq(IsOdd(4), false, "concept expression false")
end

print("Advanced Parser Features Tests Completed.")
