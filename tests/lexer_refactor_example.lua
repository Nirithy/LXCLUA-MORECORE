-- tests/lexer_refactor_example.lua
-- This example demonstrates how to use the lexer and lexer_utils library to perform AST manipulations
-- specifically, refactoring a function to dynamically rename all instances of a local variable.

local lexer = require("lexer")
local lexer_utils = require("extensions.lexer_utils")

local code = [[
local function calculate_sum(a, b)
    local result = a + b
    print("The sum is: ", result)
    return result
end

local function calculate_difference(a, b)
    local result = a - b
    print("The difference is: ", result)
    return result
end

calculate_sum(10, 5)
calculate_difference(10, 5)
]]

print("================ ORIGINAL CODE ================")
print(code)
print("===============================================\n")

-- 1. Parse the code into tokens
local tokens = lexer.lex(code)

-- 2. Find all occurrences of the variable named 'result'
-- The lexer outputs identifiers as token type "<name>" and their value is the string name.
-- To find them all, we iterate through tokens and look for type="<name>" and value="result"
local function rename_variable(tokens, old_name, new_name)
    -- We can just iterate through tokens and replace the value directly
    for i, t in ipairs(tokens) do
        if t.type == "<name>" and t.value == old_name then
            t.value = new_name
            -- we can optionally reconstruct the string literal for t.type but lexer.reconstruct handles the .value properly
        end
    end
end

-- Rename 'result' to 'computed_value'
rename_variable(tokens, "result", "computed_value")


-- 3. Replace a function call 'calculate_sum' to 'math_sum'
local sum_calls = lexer_utils.find_all_tokens(tokens, "<name>")
for _, idx in ipairs(sum_calls) do
    if tokens[idx].value == "calculate_sum" then
        tokens[idx].value = "math_sum"
    end
end

-- 4. Reconstruct the modified token array back to Lua source code string
local refactored_code = lexer.reconstruct(tokens)

print("=============== REFACTORED CODE ===============")
print(refactored_code)
print("===============================================")

-- Now let's try replacing an entire statement.
-- Let's replace 'return computed_value' with 'return { value = computed_value }' in the sum function.

-- Find the math_sum function declaration
local math_sum_indices = lexer_utils.find_all_tokens(tokens, "<name>")
local start_idx = nil
for _, idx in ipairs(math_sum_indices) do
    if tokens[idx].value == "math_sum" then
        -- The function keyword should be right before it or nearby, but we can also just find the function bounds directly
        -- Actually, 'local function math_sum' -> tokens[idx-1] == 'function'
        if tokens[idx-1].type == "'function'" then
            start_idx = idx - 1
            break
        end
    end
end

if start_idx then
    local func_start, func_end = lexer_utils.get_function_bounds(tokens, start_idx)

    -- Within these bounds, let's find the 'return' token
    local return_idx = nil
    for i = func_start, func_end do
        if tokens[i].type == "'return'" then
            return_idx = i
            break
        end
    end

    if return_idx then
        -- The return statement is 'return computed_value'
        -- We will replace 'computed_value' with '{ value = computed_value }'
        -- So we replace indices: return_idx + 1 to return_idx + 1

        -- Create new tokens
        -- '{' token type is "'{'"
        -- 'value' token type is "<name>"
        -- '=' token type is "'='"
        -- 'computed_value' token type is "<name>"
        -- '}' token type is "'}'"

        local new_tokens = {
            { token = string.byte('{'), type = "'{'", line = tokens[return_idx].line },
            { token = lexer.TK_NAME, type = "<name>", value = "value", line = tokens[return_idx].line },
            { token = string.byte('='), type = "'='", line = tokens[return_idx].line },
            { token = lexer.TK_NAME, type = "<name>", value = "computed_value", line = tokens[return_idx].line },
            { token = string.byte('}'), type = "'}'", line = tokens[return_idx].line }
        }

        -- We replace 'computed_value' which is at return_idx + 1
        lexer_utils.replace_tokens(tokens, return_idx + 1, return_idx + 1, new_tokens)
    end
end

local final_code = lexer.reconstruct(tokens)

print("=============== FINAL REFACTORED CODE ===============")
print(final_code)
print("=====================================================")
