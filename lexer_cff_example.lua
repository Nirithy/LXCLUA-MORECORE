-- lexer_cff_example.lua
-- This example demonstrates how to use the lexer library to perform a simple
-- Control-Flow Flattening (CFF) obfuscation. We take a function with sequential
-- statements, split them into blocks using lexer utilities, and rebuild the function
-- using a switch-based state machine.

local lexer = require("lexer")

local function build_cff(code)
    local tokens = lexer(code)
    local tree = lexer.build_tree(tokens)

    local in_body = false
    local statements = {}
    local current_stmt = {}

    -- Very naive sequential statement splitter:
    -- groups tokens by lines for the body of a single function.
    local i = 1
    while i <= #tokens do
        local t = tokens[i]

        if t.type == "'function'" then
            in_body = false
            -- Find the end of function signature
            local j = i + 1
            while j <= #tokens and tokens[j].type ~= "')'" do
                j = j + 1
            end
            if j <= #tokens then
                in_body = true
                i = j
            end
        elseif in_body then
            if t.type == "'end'" then
                in_body = false
                if #current_stmt > 0 then
                    table.insert(statements, current_stmt)
                    current_stmt = {}
                end
            else
                table.insert(current_stmt, t)
                if i < #tokens and tokens[i+1].line > t.line then
                    if #current_stmt > 0 then
                        table.insert(statements, current_stmt)
                        current_stmt = {}
                    end
                end
            end
        end
        i = i + 1
    end

    if #current_stmt > 0 then
        table.insert(statements, current_stmt)
    end

    -- Extract all 'local' declarations to lift them outside the switch state machine
    local declarations = {}
    for _, stmt in ipairs(statements) do
        if #stmt >= 3 and stmt[1].type == "'local'" and stmt[2].type == "<name>" then
            table.insert(declarations, stmt[2].value)
        end
    end

    local new_code = "local function obfuscated()\n"
    if #declarations > 0 then
        new_code = new_code .. "  local " .. table.concat(declarations, ", ") .. "\n"
    end
    new_code = new_code .. "  local state = 1\n"
    new_code = new_code .. "  while true do\n"
    new_code = new_code .. "    switch state do\n"

    for idx, stmt in ipairs(statements) do
        -- Strip 'local' from the reconstructed statements since we hoisted them
        local stripped_stmt = {}
        local j = 1
        if #stmt >= 3 and stmt[1].type == "'local'" then
            j = 2
        end
        for k = j, #stmt do
            table.insert(stripped_stmt, stmt[k])
        end

        local stmt_code = lexer.reconstruct(stripped_stmt)

        -- Check if statement is a return
        local is_return = false
        for _, t in ipairs(stripped_stmt) do
            if t.type == "'return'" then
                is_return = true
                break
            end
        end

        new_code = new_code .. "      case " .. idx .. ":\n"
        new_code = new_code .. "        " .. stmt_code .. "\n"
        if is_return then
            new_code = new_code .. "        return\n"
        else
            new_code = new_code .. "        state = " .. (idx + 1) .. "\n"
            new_code = new_code .. "        break\n"
        end
    end

    new_code = new_code .. "      default:\n"
    new_code = new_code .. "        return\n"
    new_code = new_code .. "    end\n"
    new_code = new_code .. "  end\n"
    new_code = new_code .. "end\n\n"
    new_code = new_code .. "obfuscated()\n"

    return new_code
end

local code = [[
function example()
  local a = 10
  local b = 20
  print("Result: ", a + b)
end
]]

print("--- Original Code ---")
print(code)

print("--- CFF Obfuscated Code ---")
local refactored = build_cff(code)
print(refactored)

print("--- Execution Output ---")
local f, err = load(refactored)
if f then
    f()
else
    print("Failed to load refactored code:", err)
end
