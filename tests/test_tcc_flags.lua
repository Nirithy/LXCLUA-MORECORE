local tcc = require "tcc"

local function test_compute_flags()
    print("Testing tcc.compute_flags...")

    -- Check basic flags based on lobfuscate.h
    -- #define OBFUSCATE_CFF               (1<<0)  -> 1
    -- #define OBFUSCATE_STR_ENCRYPT       (1<<11) -> 2048

    assert(tcc.compute_flags({flatten=true}) == 1, "flatten should be 1")
    assert(tcc.compute_flags({string_encryption=true}) == 2048, "string_encryption should be 2048")

    -- Check combination
    local combined = tcc.compute_flags({flatten=true, string_encryption=true})
    assert(combined == (1 | 2048), "flatten | string_encryption should be 2049")

    -- Check all flags
    local all_flags = {
        flatten = 1<<0,
        block_shuffle = 1<<1,
        bogus_blocks = 1<<2,
        state_encode = 1<<3,
        nested_dispatcher = 1<<4,
        opaque_predicates = 1<<5,
        func_interleave = 1<<6,
        vm_protect = 1<<7,
        binary_dispatcher = 1<<8,
        random_nop = 1<<9,
        string_encryption = 1<<11
    }

    for k, v in pairs(all_flags) do
        local f = tcc.compute_flags({[k]=true})
        assert(f == v, string.format("Flag %s should be %d, got %d", k, v, f))
    end

    print("tcc.compute_flags passed.")
end

local function test_compile_with_flags()
    print("Testing tcc.compile with flags...")

    local code = [[
        return function(a, b)
            return a + b
        end
    ]]

    -- Compile with manually computed flag
    local flags = tcc.compute_flags({flatten=true})
    local success, err = pcall(function()
        tcc.compile(code, {flags=flags})
    end)

    if not success then
        print("Error compiling with flags:", err)
    end
    assert(success, "tcc.compile should succeed with flags")

    -- Compile with boolean options (legacy path check)
    local success2, err2 = pcall(function()
        tcc.compile(code, {flatten=true})
    end)
    if not success2 then
        print("Error compiling with flatten=true:", err2)
    end
    assert(success2, "tcc.compile should succeed with flatten=true")

    print("tcc.compile with flags passed.")
end

test_compute_flags()
test_compile_with_flags()
