local test_str = 'print("hello world from png load")'
local str = require("string")
local fname = "test_out.png"
local f = io.open("test_in.lua", "w")
f:write(test_str)
f:close()
str.file2png("test_in.lua", fname)
local loaded, err = loadfile(fname)
if loaded then
    print("loaded successfully!")
    loaded()
else
    print("load failed:", err)
end
                                                                                                                                                  