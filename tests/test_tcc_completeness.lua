local tcc = require("tcc")

local function verify(name, code, patterns)
    print("Testing " .. name .. "...")
    local status, c_code = pcall(tcc.compile, code, name)

    if not status then
        print("  FAILED: Compilation error: " .. tostring(c_code))
        return false
    end

    local missing = {}
    for _, pattern in ipairs(patterns) do
        if not c_code:find(pattern, 1, true) then
            table.insert(missing, pattern)
        end
    end

    if #missing > 0 then
        print("  FAILED: Missing patterns in generated C code:")
        for _, m in ipairs(missing) do
            print("    - " .. m)
        end
        print("  Generated Code Snippet:")
        print(c_code:sub(1, 2000))
        return false
    else
        print("  PASSED")
        return true
    end
end

local tests = {
    {
        name = "Generic For Loop (OP_TFOR*)",
        code = [[
            local t = {1, 2, 3}
            for k, v in pairs(t) do
                print(k, v)
            end
        ]],
        patterns = {
            "lua_call(L, 2,", -- TFORCALL
            "goto Label_",   -- Jumps for loops
        }
    },
    {
        name = "Explicit Opcodes via ASM",
        code = [[
            asm(
                LFALSESKIP 0;
                CLOSE 0;
            )
        ]],
        patterns = {
            "if (!lua_toboolean(L,", -- LFALSESKIP
            "lua_toclose(L,",        -- CLOSE
        }
    }
}

local success = true
for _, t in ipairs(tests) do
    if not verify(t.name, t.code, t.patterns) then
        success = false
    end
end

if not success then
    print("\nSome tests failed!")
    os.exit(1)
else
    print("\nAll tests passed!")
end
