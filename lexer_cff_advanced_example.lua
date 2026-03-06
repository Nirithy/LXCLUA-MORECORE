-- lexer_cff_advanced_example.lua
-- This example demonstrates how to use the lexer library to perform an
-- ADVANCED Control-Flow Flattening (CFF) obfuscation.
-- Techniques combined:
-- 1. Outer Dispatcher: `goto`-based routing based on a `__group` state.
-- 2. Inner Dispatcher: `if/elseif`-based Nested State Machine switching on an `__inner_state`.
-- 3. Basic Block Scrambling: Statements are randomly assigned to groups, and groups/cases are fully shuffled.

local lexer = require("lexer")

math.randomseed(os.time())

local function shuffle(tbl)
    for i = #tbl, 2, -1 do
        local j = math.random(i)
        tbl[i], tbl[j] = tbl[j], tbl[i]
    end
end

local function generate_random_id()
    return math.random(100000, 999999)
end

local function generate_label(index)
    return "GOTO_" .. index .. "_" .. generate_random_id()
end

local function flatten_advanced(func_node)
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

    -- Hoist locals and declare state variables
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
    table.insert(code_parts, "\n  local __group, __inner_state\n")

    -- Assign random IDs for each statement
    local states = {}
    local final_exit_id = generate_random_id()
    local final_exit_group = generate_random_id()

    for i = 1, #transformed_statements do
        states[i] = {
            group_id = generate_random_id(),
            inner_id = generate_random_id(),
            stmt = transformed_statements[i]
        }
    end

    -- The next state to transition to for statement i
    for i = 1, #states do
        if i < #states then
            states[i].next_group = states[i+1].group_id
            states[i].next_inner = states[i+1].inner_id
        else
            states[i].next_group = final_exit_group
            states[i].next_inner = final_exit_id
        end
    end

    -- Initial state assignments
    if #states > 0 then
        table.insert(code_parts, "  __group = " .. states[1].group_id .. "\n")
        table.insert(code_parts, "  __inner_state = " .. states[1].inner_id .. "\n")
    else
        table.insert(code_parts, "  __group = " .. final_exit_group .. "\n")
        table.insert(code_parts, "  __inner_state = " .. final_exit_id .. "\n")
    end

    -- Group states by their Group ID
    local groups_map = {}
    for _, state in ipairs(states) do
        if not groups_map[state.group_id] then
            groups_map[state.group_id] = {}
        end
        table.insert(groups_map[state.group_id], state)
    end

    -- Shuffle the order of groups for the Outer Dispatcher
    local group_ids = {}
    for g_id, _ in pairs(groups_map) do
        table.insert(group_ids, g_id)
    end
    shuffle(group_ids)

    local dispatcher_label = generate_label("DISPATCHER")

    table.insert(code_parts, "\n  ::" .. dispatcher_label .. "::\n")

    local first_group = true
    for _, g_id in ipairs(group_ids) do
        if first_group then
            table.insert(code_parts, "  if __group == " .. g_id .. " then\n")
            first_group = false
        else
            table.insert(code_parts, "  elseif __group == " .. g_id .. " then\n")
        end

        -- Shuffle cases within the Inner Dispatcher
        local inner_states = groups_map[g_id]
        shuffle(inner_states)

        local first_inner = true
        for _, state in ipairs(inner_states) do
            if first_inner then
                table.insert(code_parts, "    if __inner_state == " .. state.inner_id .. " then\n      ")
                first_inner = false
            else
                table.insert(code_parts, "    elseif __inner_state == " .. state.inner_id .. " then\n      ")
            end

            table.insert(code_parts, lexer.reconstruct(state.stmt) .. "\n")

            local is_return = false
            local is_unconditional_return = false

            for _, t in ipairs(state.stmt) do
                if t.type == "'return'" then is_return = true end
            end

            if state.stmt[1] and state.stmt[1].type == "'return'" then
                is_unconditional_return = true
            end

            if not is_unconditional_return then
                table.insert(code_parts, "      __group = " .. state.next_group .. "\n")
                table.insert(code_parts, "      __inner_state = " .. state.next_inner .. "\n")
                table.insert(code_parts, "      goto " .. dispatcher_label .. "\n")
            end
        end
        if not first_inner then
            table.insert(code_parts, "    end\n")
        end
    end

    -- Final exit logic in the outer dispatcher
    if first_group then
        table.insert(code_parts, "  if __group == " .. final_exit_group .. " then\n")
    else
        table.insert(code_parts, "  elseif __group == " .. final_exit_group .. " then\n")
    end
    table.insert(code_parts, "    return\n")
    table.insert(code_parts, "  end\n")

    table.insert(code_parts, "end\n")

    return table.concat(code_parts)
end

-- Walk the AST and obfuscate function nodes
local function walk(node)
    if node.type == "function" then
        return flatten_advanced(node)
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
                out_code = out_code .. "local " .. flatten_advanced(node.elements[i+1]) .. " "
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
local function advanced_example()
  local x = 100
  local y = 200
  local z = 300
  x = x + 50
  y = y - 20
  z = z * 2
  print("Advanced Result: ", x + y + z)
end
advanced_example()
]]

print("--- Original Code ---")
print(code)

print("--- Advanced CFF Obfuscated Code ---")
local refactored = obfuscate(code)
print(refactored)

print("--- Execution Output ---")
local f, err = load(refactored)
if f then
    f()
else
    print("Failed to load refactored code:", err)
end
