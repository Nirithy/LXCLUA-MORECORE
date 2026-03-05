local lexer = require("lexer")
local ok, err = pcall(function()
    print("Testing lexer...")
    local tokens = lexer("local a = 1")
    print("Tokens length: ", #tokens)
    for i, t in ipairs(tokens) do
        print(i, t.type, t.value)
    end
end)
if not ok then print("Error: ", err) end
