local tcc = require("tcc")

function test_obfuscation()
    print("Testing TCC obfuscation...")
    local code = "return 1"

    -- Test 1: Enable obfuscation
    local c_code = tcc.compile(code, {obfuscate=true, seed=12345})

    if not string.find(c_code, "typedef struct TCC_Interface") then
        error("Obfuscation failed: struct definition missing")
    end

    if not string.find(c_code, "#define lua_pushvalue") then
        error("Obfuscation failed: macros missing")
    end

    if not string.find(c_code, "lua_tcc_get_interface") then
        error("Obfuscation failed: interface getter missing")
    end

    if not string.find(c_code, "luaL_ref%(L, LUA_REGISTRYINDEX%)") then
        error("Obfuscation failed: interface anchoring (luaL_ref) missing")
    end

    -- Check seed usage (determinism)
    local idx1 = string.match(c_code, "#define lua_pushvalue.-api%->f%[(%d+)%]")
    print("Index for lua_pushvalue (seed 12345): " .. (idx1 or "nil"))

    if not idx1 then
        error("Failed to extract index from macro definition")
    end

    local c_code2 = tcc.compile(code, {obfuscate=true, seed=12345})
    local idx2 = string.match(c_code2, "#define lua_pushvalue.-api%->f%[(%d+)%]")

    if idx1 ~= idx2 then
        error("Determinism failed: same seed produced different indices")
    end

    -- Check seed variation
    local c_code3 = tcc.compile(code, {obfuscate=true, seed=67890})
    local idx3 = string.match(c_code3, "#define lua_pushvalue.-api%->f%[(%d+)%]")
    print("Index for lua_pushvalue (seed 67890): " .. (idx3 or "nil"))

    if idx1 == idx3 then
        print("Warning: Different seeds produced same index (possible but unlikely if count is large)")
    else
        print("Different seeds produced different indices. Good.")
    end

    -- Test 2: Disable obfuscation
    local c_code_plain = tcc.compile(code, {obfuscate=false})
    if string.find(c_code_plain, "typedef struct TCC_Interface") then
        error("Obfuscation should be disabled")
    end

    print("test_obfuscation passed")
end

test_obfuscation()
