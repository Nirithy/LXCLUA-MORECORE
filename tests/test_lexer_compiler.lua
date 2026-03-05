-- tests/test_lexer_compiler.lua

local lexer = require("lexer")

local code = [[
local a = 10
if a > 5 then
    print("Greater")
else
    print("Smaller")
end
local function myfunc(x)
    return x * 2
end
print(myfunc(a))
]]

print("=== ORIGINAL CODE ===")
print(code)

local options = {
    cff = true,
    bogus_branches = true,
    string_encryption = true
}

local success, obfuscated = pcall(lexer.obfuscate, code, options)

if success then
    print("\n=== OBFUSCATED CODE ===")
    print(obfuscated)

    print("\n=== EXECUTING OBFUSCATED CODE ===")
    local f, err = load(obfuscated)
    if f then
        f()
    else
        print("Error compiling obfuscated code:", err)
    end
else
    print("Lexer obfuscator failed:", obfuscated)
end
