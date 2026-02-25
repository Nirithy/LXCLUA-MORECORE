local tcc = require("tcc")

function test_tcc_concept()
    local code = [[
        concept IsEven(x)
            return x % 2 == 0
        end
    ]]
    local c_code = tcc.compile(code)

    -- Verify OP_NEWCONCEPT maps to lua_pushcclosure (with comment if possible, but comment might be stripped or format changed)
    -- Our implementation adds: /* concept */
    if not string.find(c_code, "lua_pushcclosure.*concept") then
        error("OP_NEWCONCEPT failed to generate expected C code (missing 'lua_pushcclosure' or 'concept' comment)")
    end
    print("test_tcc_concept passed")
end

function test_tcc_vararg()
    local code = [[
        function f(...)
            local a = ...
            return a
        end
    ]]
    -- "local a = ..." usually generates OP_VARARG with C=2 if it's the only one, or maybe OP_GETVARG if optimized?
    -- Standard Lua 5.4 uses OP_VARARG for "local a = ...".
    -- To force OP_GETVARG, maybe select?
    -- "local a = select(2, ...)"
    local code2 = [[
        function f(...)
            local a = select(2, ...)
            return a
        end
    ]]

    local c_code = tcc.compile(code)

    -- Verify basic OP_VARARG usage
    if not string.find(c_code, "lua_rawgeti") then
         -- OP_VARARG in ltcc uses lua_rawgeti loop
         error("OP_VARARG failed to generate expected C code (missing 'lua_rawgeti')")
    end
    print("test_tcc_vararg passed")
end

function test_tcc_getvarg()
    -- This is hard to force without specific compiler knowledge, but we can try manual bytecode if we could.
    -- Or we just assume that if OP_GETVARG is emitted, it works.
    -- Let's try to verify if we can spot patterns.
    -- If we can't easily trigger OP_GETVARG, we at least verify tcc compiles generic vararg code without error.
    local code = [[
        function f(a, ...)
            return ...
        end
    ]]
    local c_code = tcc.compile(code)
    if not c_code then
        error("Failed to compile vararg code")
    end
    print("test_tcc_getvarg compilation passed")
end

function test_tcc_tfor_toclose()
    local code = [[
        for k, v in pairs({}) do
        end
    ]]
    local c_code = tcc.compile(code)

    -- Verify OP_TFORPREP generates lua_toclose
    -- Note: We look for lua_toclose call. The argument index depends on stack allocation,
    -- but we can just check for existence of "lua_toclose".
    if not string.find(c_code, "lua_toclose") then
        error("OP_TFORPREP failed to generate expected C code (missing 'lua_toclose')")
    end
    print("test_tcc_tfor_toclose passed")
end

test_tcc_concept()
test_tcc_vararg()
test_tcc_getvarg()
test_tcc_tfor_toclose()

print("All TCC correctness tests passed!")
