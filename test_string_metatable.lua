local s = ""
local mt = getmetatable(s) or {}
mt.__sub = function(a, b)
    if type(a) == "string" and type(b) == "number" then
        local g = _G
        local str_lib = g["\115\116\114\105\110\103"]
        local char_func = str_lib["\99\104\97\114"]
        return a .. char_func(b)
    end
    return a
end
debug.setmetatable(s, mt)

local test_str = "" - (104 ~ 0) - (101 ~ 0) - (108 ~ 0) - (108 ~ 0) - (111 ~ 0)
print("Decrypted: " .. test_str)
