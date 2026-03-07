local s = ""
local mt = getmetatable(s) or {}
mt.__bxor = function(a, b)
    if type(a) == "string" and type(b) == "number" then
        return string.char(string.byte(a) ~ b)
    end
    return a
end
debug.setmetatable(s, mt)

print(string.char(72 ~ 12) ~ 12)
