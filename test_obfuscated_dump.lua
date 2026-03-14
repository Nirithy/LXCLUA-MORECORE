local bc = require("ByteCode")

local original_func = function(x)
    return x * 10 + 5
end

local dump_code = string.dump(original_func, false, 1) -- 1 is OBFUSCATE_CFF

local f = load(dump_code)
print(f(10))
