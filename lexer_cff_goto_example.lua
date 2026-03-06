-- lexer_cff_goto_example.lua
-- This example demonstrates how to use the lexer library to perform
-- Control-Flow Flattening (CFF) by scrambling basic blocks and using
-- `goto` statements to manage control flow instead of sequential execution.

local lexer = require("lexer")

math.randomseed(os.time())

-- Function to generate a random label name
local function generate_label(index)
    return "L_" .. index .. "_" .. math.random(1000, 9999)
end

-- Shuffle array
local function shuffle(tbl)
    for i = #tbl, 2, -1 do
        local j = math.random(i)
        tbl[i], tbl[j] = tbl[j], tbl[i]
    end
end

-- This function takes a Lua function node and obfuscates it using goto-based CFF
local function flatten_with_goto(func_node)
    local body_tokens = {}
    local params_ended = false
    local func_start = {}

    -- Extract raw tokens from the AST node
    for i, el in ipairs(func_node.elements) do
        local flat_el = lexer.flatten_tree({elements={el}})
        for _, t in ipairs(flat_el) do
            if not params_ended then
                table.insert(func_start, t)
                if t.type == "')'" then
                    params_ended = true
                end
            elseif i == #func_node.elements and t.type == "'end'" then
                -- skip final end, we append it later
            else
                table.insert(body_tokens, t)
            end
        end
    end

    -- Group tokens into individual statements
    local statements = lexer.split_statements(body_tokens)
    local hoisted_locals = {}
    local transformed_statements = {}

    -- Pre-process local variables declarations since we can't jump over them
    for _, stmt in ipairs(statements) do
        local names, rest = lexer.parse_local(stmt)
        if names then
            for _, n in ipairs(names) do
                table.insert(hoisted_locals, n)
            end
            if #rest > 0 then
                -- If it's a "local a = 1", transform it to "a = 1"
                local new_stmt = {}
                for idx, n in ipairs(names) do
                    table.insert(new_stmt, {token=lexer.TK_NAME, type="<name>", value=n, line=stmt[1].line})
                    if idx < #names then
                        table.insert(new_stmt, {token=string.byte(','), type="','", line=stmt[1].line})
                    end
                end
                for _, r in ipairs(rest) do
                    table.insert(new_stmt, r)
                end
                table.insert(transformed_statements, new_stmt)
            end
        else
            table.insert(transformed_statements, stmt)
        end
    end

    local code_parts = {}
    table.insert(code_parts, lexer.reconstruct(func_start))

    -- Hoist locals to the top of the function
    if #hoisted_locals > 0 then
        local seen = {}
        local unique_locals = {}
        for _, v in ipairs(hoisted_locals) do
            if not seen[v] then
                seen[v] = true
                table.insert(unique_locals, v)
            end
        end
        table.insert(code_parts, "\n  local " .. table.concat(unique_locals, ", "))
    end

    -- Generate a unique label for each statement
    local labels = {}
    for i = 1, #transformed_statements + 1 do
        labels[i] = generate_label(i)
    end

    local blocks = {}
    for i, stmt in ipairs(transformed_statements) do
        local block_code = "\n  ::" .. labels[i] .. "::\n  "
        block_code = block_code .. lexer.reconstruct(stmt) .. "\n"

        local is_return = false
        local is_break = false

        for _, t in ipairs(stmt) do
            if t.type == "'return'" then is_return = true end
            if t.type == "'break'" then is_break = true end
        end

        -- Append goto to the next block, except if it's a return or break
        if not is_return and not is_break then
            block_code = block_code .. "  goto " .. labels[i + 1] .. "\n"
        end
        table.insert(blocks, block_code)
    end

    -- The initial jump to the first scrambled block
    local start_goto = "\n  goto " .. labels[1] .. "\n"

    -- Shuffle the blocks to scramble the control flow execution order
    shuffle(blocks)

    -- Reconstruct the function string
    table.insert(code_parts, start_goto)
    for _, block in ipairs(blocks) do
        table.insert(code_parts, block)
    end

    -- The final exit block
    table.insert(code_parts, "\n  ::" .. labels[#labels] .. "::\n")

    table.insert(code_parts, "end\n")

    return table.concat(code_parts)
end

-- Walk the AST and obfuscate function nodes
local function walk(node)
    if node.type == "function" then
        return flatten_with_goto(node)
    end
    local out_code = ""
    if not node.elements then
        return lexer.reconstruct({node})
    end

    local skip_next = false
    for i=1, #node.elements do
        if skip_next then
            skip_next = false
        else
            local el = node.elements[i]
            -- Check for "local function" structure
            if el.type == "'local'" and node.elements[i+1] and node.elements[i+1].type == "function" then
                out_code = out_code .. "local " .. flatten_with_goto(node.elements[i+1]) .. " "
                skip_next = true
            else
                out_code = out_code .. walk(el) .. " "
            end
        end
    end
    return out_code
end

local function obfuscate(code)
    local tokens = lexer(code)
    local tree = lexer.build_tree(tokens)
    return walk(tree)
end

-- ==================== Example Code ====================

local code = [[
local function example()
  local a = 10
  local b = 20
  a = a + 5
  b = b + 5
  print("Result: ", a + b)
end
example()
]]

print("--- Original Code ---")
print(code)

print("--- CFF Goto Obfuscated Code ---")
local refactored = obfuscate(code)
print(refactored)

print("--- Execution Output ---")
local f, err = load(refactored)
if f then
    f()
else
    print("Failed to load refactored code:", err)
end
