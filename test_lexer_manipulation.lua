local lexer = require("lexer")

local code = [[
local a = 1
local b = 2
]]
local tokens = lexer(code)

print("Original length:", #tokens)
print(lexer.reconstruct(tokens))

-- find
local indices = lexer.find_tokens(tokens, "'local'")
print("Found 'local' at indices:", table.concat(indices, ", "))

-- insert single
lexer.insert_tokens(tokens, indices[2], { type = "<name>", token = lexer.TK_NAME, value = "FOO", line = 2 })
print("After insert single length:", #tokens)
print(lexer.reconstruct(tokens))

-- insert array
lexer.insert_tokens(tokens, indices[2], {
    { type = "<name>", token = lexer.TK_NAME, value = "BAR", line = 2 },
    { type = "=", token = 61, value = nil, line = 2 }
})
print("After insert array length:", #tokens)
print(lexer.reconstruct(tokens))

-- remove
lexer.remove_tokens(tokens, indices[2], 3)
print("After remove length:", #tokens)
print(lexer.reconstruct(tokens))
