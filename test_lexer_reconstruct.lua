local lexer = require("lexer")
local code = [[print("Hello", _raw"foo", "world")]]
local tokens = lexer(code)
print(lexer.reconstruct(tokens))
