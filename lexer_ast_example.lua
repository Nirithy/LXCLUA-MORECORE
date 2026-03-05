-- lexer_ast_example.lua
-- This is a simple example to show how to use the new `lexer` library
-- to analyze Lua code, enabling syntax manipulation and control-flow refactoring.

local lexer = require("lexer")

-- Function to print tokens cleanly
local function print_tokens(code)
    local tokens = lexer.lex(code)
    for i, t in ipairs(tokens) do
        local val_str = t.value and string.format(" [Value: %s]", tostring(t.value)) or ""
        print(string.format("%4d: Line %d, Token %d, Type %s%s",
            i, t.line, t.token, tostring(t.type), val_str))
    end
end

local sample_code = [[
local function factorial(n)
    if n <= 1 then
        return 1
    else
        return n * factorial(n - 1)
    end
end
print(factorial(5))
]]

print("--- Token Stream ---")
print_tokens(sample_code)

-- A basic AST builder using recursive descent parsing over tokens
-- would start here. For control-flow refactoring (like Control-Flow Flattening),
-- you could analyze `if`, `for`, `while` tokens to construct a CFG (Control Flow Graph),
-- then manipulate the AST and finally generate back the Lua code.

-- Example: Finding all function definitions in the token stream
local function find_functions(code)
    local tokens = lexer.lex(code)
    local funcs = {}
    for i = 1, #tokens do
        if tokens[i].type == "'function'" then
            local name = "anonymous"
            if i + 1 <= #tokens and tokens[i+1].type == "<name>" then
                name = tokens[i+1].value
            end
            table.insert(funcs, {name = name, line = tokens[i].line})
        end
    end
    return funcs
end

print("\n--- Found Functions ---")
local funcs = find_functions(sample_code)
for _, f in ipairs(funcs) do
    print(string.format("Function '%s' at line %d", f.name, f.line))
end
