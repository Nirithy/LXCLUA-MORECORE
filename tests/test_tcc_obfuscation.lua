local tcc = require "tcc"
local code = [[
function hello(name)
    print("Hello, " .. name)
end
hello("World")
]]

local c_code = tcc.compile(code, {obfuscate = true, string_encryption = true}, "test_mod")
print(c_code)
