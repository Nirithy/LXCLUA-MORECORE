local lexer = require("lexer")
local code = [[
local a = 1
print("hello", a)
]]
local tokens = lexer.lex(code)
for _, t in ipairs(tokens) do
  print(string.format("Line: %d, Token: %d, Type: %s, Value: %s",
    t.line, t.token, tostring(t.type), tostring(t.value)))
end
