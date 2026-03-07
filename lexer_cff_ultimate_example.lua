-- lexer_cff_ultimate_example.lua
-- This example demonstrates how to use the lexer library to perform an
-- ULTIMATE Control-Flow Flattening (CFF) obfuscation utilizing custom
-- VM syntax extensions (`switch`, `case`).
-- Techniques combined:
-- 1. 3-Layered Dispatcher: `if/elseif` outer router, `switch __group`, `switch __state`.
-- 2. State Encoding: `__state` is modified via arithmetic rather than direct assignment.
-- 3. Opaque Predicates: `if 7 * 8 == 56 then ... else (junk) end`.
-- 4. Bogus Blocks: Junk code injection inside false opaque predicate branches.
-- 5. Full Basic Block Scrambling.

local lexer = require("lexer")

math.randomseed(os.time())

local function shuffle(tbl)
    for i = #tbl, 2, -1 do
        local j = math.random(i)
        tbl[i], tbl[j] = tbl[j], tbl[i]
    end
end

local function generate_random_id()
    return math.random(1000, 9999)
end

local function generate_label(index)
    return "DISPATCH_" .. index .. "_" .. math.random(10000, 99999)
end

local function generate_junk_code()
    local junk = {
        "local _j1 = " .. math.random(10, 99) .. " * " .. math.random(10, 99) .. "\n",
        "local _j2 = tostring(" .. math.random(1000, 9999) .. ")\n",
        "local _j3 = { a = " .. math.random(1, 9) .. " }\n",
        "if " .. math.random(10, 99) .. " > " .. math.random(100, 999) .. " then return end\n",
        "for _i=1, " .. math.random(2, 5) .. " do local _ = _i end\n"
    }
    shuffle(junk)
    return "          " .. junk[1] .. "          " .. junk[2]
end

local function generate_opaque_predicate()
    local a = math.random(2, 9)
    local b = math.random(2, 9)
    local c = a * b
    return a .. " * " .. b .. " == " .. c
end

local function flatten_ultimate(func_node)
    local body_tokens = {}
    local params_ended = false
    local func_start = {}

    for i, el in ipairs(func_node.elements) do
        local flat_el = lexer.flatten_tree({elements={el}})
        for _, t in ipairs(flat_el) do
            if not params_ended then
                table.insert(func_start, t)
                if t.type == "')'" then
                    params_ended = true
                end
            elseif i == #func_node.elements and t.type == "'end'" then
                -- skip final end
            else
                table.insert(body_tokens, t)
            end
        end
    end

    local statements = lexer.split_statements(body_tokens)
    local hoisted_locals = {}
    local transformed_statements = {}

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
    table.insert(code_parts, "\n  local __zone, __group, __state\n")

    -- Assign random IDs
    local states = {}
    local final_exit_id = generate_random_id()
    local final_exit_group = generate_random_id()
    local final_exit_zone = generate_random_id()

    for i = 1, #transformed_statements do
        states[i] = {
            zone_id = generate_random_id(),
            group_id = generate_random_id(),
            state_id = generate_random_id(),
            stmt = transformed_statements[i]
        }
    end

    -- Link states
    for i = 1, #states do
        if i < #states then
            states[i].next_zone = states[i+1].zone_id
            states[i].next_group = states[i+1].group_id
            states[i].next_state = states[i+1].state_id
        else
            states[i].next_zone = final_exit_zone
            states[i].next_group = final_exit_group
            states[i].next_state = final_exit_id
        end
    end

    -- Add some purely bogus blocks that will never be reached naturally
    local num_bogus = math.floor(#transformed_statements / 2) + 1
    for i = 1, num_bogus do
        table.insert(states, {
            zone_id = generate_random_id(),
            group_id = generate_random_id(),
            state_id = generate_random_id(),
            stmt = {{token=lexer.TK_NAME, type="<name>", value="-- bogus block"}},
            is_bogus = true,
            next_zone = generate_random_id(),
            next_group = generate_random_id(),
            next_state = generate_random_id()
        })
    end

    -- Initial state assignments
    if #transformed_statements > 0 then
        table.insert(code_parts, "  __zone = " .. states[1].zone_id .. "\n")
        table.insert(code_parts, "  __group = " .. states[1].group_id .. "\n")
        table.insert(code_parts, "  __state = " .. states[1].state_id .. "\n")
    else
        table.insert(code_parts, "  __zone = " .. final_exit_zone .. "\n")
        table.insert(code_parts, "  __group = " .. final_exit_group .. "\n")
        table.insert(code_parts, "  __state = " .. final_exit_id .. "\n")
    end

    -- Map Zones -> Groups -> States
    local zones_map = {}
    for _, state in ipairs(states) do
        if not zones_map[state.zone_id] then
            zones_map[state.zone_id] = {}
        end
        if not zones_map[state.zone_id][state.group_id] then
            zones_map[state.zone_id][state.group_id] = {}
        end
        table.insert(zones_map[state.zone_id][state.group_id], state)
    end

    local zone_ids = {}
    for z_id, _ in pairs(zones_map) do
        table.insert(zone_ids, z_id)
    end
    shuffle(zone_ids)

    local dispatcher_label = generate_label("OUTER")
    table.insert(code_parts, "\n  ::" .. dispatcher_label .. "::\n")

    local first_zone = true
    for _, z_id in ipairs(zone_ids) do
        if first_zone then
            table.insert(code_parts, "  if __zone == " .. z_id .. " then\n")
            first_zone = false
        else
            table.insert(code_parts, "  elseif __zone == " .. z_id .. " then\n")
        end

        table.insert(code_parts, "    switch (__group) do\n")

        local groups_map = zones_map[z_id]
        local group_ids = {}
        for g_id, _ in pairs(groups_map) do
            table.insert(group_ids, g_id)
        end
        shuffle(group_ids)

        for _, g_id in ipairs(group_ids) do
            table.insert(code_parts, "      case " .. g_id .. ":\n")
            table.insert(code_parts, "        switch (__state) do\n")

            local inner_states = groups_map[g_id]
            shuffle(inner_states)

            for _, state in ipairs(inner_states) do
                table.insert(code_parts, "          case " .. state.state_id .. ":\n")

                -- Wrap in opaque predicate
                table.insert(code_parts, "            if " .. generate_opaque_predicate() .. " then\n")

                if state.is_bogus then
                    table.insert(code_parts, generate_junk_code())
                else
                    table.insert(code_parts, "              " .. lexer.reconstruct(state.stmt) .. "\n")
                end

                local is_return = false
                local is_unconditional_return = false

                if not state.is_bogus then
                    for _, t in ipairs(state.stmt) do
                        if t.type == "'return'" then is_return = true end
                    end

                    -- Heuristic: if return is the VERY first token in the statement, it's unconditional
                    if state.stmt[1] and state.stmt[1].type == "'return'" then
                        is_unconditional_return = true
                    end
                end

                if not is_unconditional_return then
                    -- State encoding math
                    local diff_z = state.next_zone - z_id
                    local diff_g = state.next_group - g_id
                    local diff_s = state.next_state - state.state_id

                    table.insert(code_parts, "              __zone = __zone + (" .. diff_z .. ")\n")
                    table.insert(code_parts, "              __group = __group + (" .. diff_g .. ")\n")
                    table.insert(code_parts, "              __state = __state + (" .. diff_s .. ")\n")
                    table.insert(code_parts, "              goto " .. dispatcher_label .. "\n")
                end

                table.insert(code_parts, "            else\n")
                table.insert(code_parts, generate_junk_code())
                table.insert(code_parts, "            end\n")
                table.insert(code_parts, "            break\n")
            end
            table.insert(code_parts, "        end\n")
            table.insert(code_parts, "        break\n")
        end
        table.insert(code_parts, "    end\n")
    end

    if first_zone then
        table.insert(code_parts, "  if __zone == " .. final_exit_zone .. " then\n")
    else
        table.insert(code_parts, "  elseif __zone == " .. final_exit_zone .. " then\n")
    end
    table.insert(code_parts, "    return\n")
    table.insert(code_parts, "  end\n")

    table.insert(code_parts, "end\n")

    return table.concat(code_parts)
end

local function walk(node)
    if node.type == "function" then
        return flatten_ultimate(node)
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
            if el.type == "'local'" and node.elements[i+1] and node.elements[i+1].type == "function" then
                out_code = out_code .. "local " .. flatten_ultimate(node.elements[i+1]) .. " "
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
local function ultimate_example()
  local hp = 100
  local mp = 50
  if hp > 50 then
    mp = mp - 10
    if mp < 0 then
      return
    end
  end
  hp = hp + 20
  print("HP: ", hp, " MP: ", mp)
end
ultimate_example()
]]

print("--- Original Code ---")
print(code)

print("--- Ultimate CFF Obfuscated Code ---")
local refactored = obfuscate(code)
print(refactored)

print("--- Execution Output ---")
local f, err = load(refactored)
if f then
    f()
else
    print("Failed to load refactored code:", err)
end
