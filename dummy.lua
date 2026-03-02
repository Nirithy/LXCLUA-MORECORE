local tcc = require("tcc")
local code = "print('hello regular test')"
local L = tcc.compile(code, {inline=false}, "regular_test")
print(L)
