local lexer = require("lexer")
local code = [[
local a = 1
print("hello", a)
]]
local tokens = lexer(code)
for _, t in ipairs(tokens) do
  print(string.format("Line: %d, Token: %d, Type: %s, Value: %s",
    t.line, t.token, tostring(t.type), tostring(t.value)))
end

print("\n--- Reconstruct Test ---")
local rec = lexer.reconstruct(tokens)
print(rec)

print("\n--- Advanced String Reconstruct Test ---")
local complex_code = [[
local a = "string with\nnewline and \"quotes\""
]]
local complex_tokens = lexer.lex(complex_code)
print(lexer.reconstruct(complex_tokens))
