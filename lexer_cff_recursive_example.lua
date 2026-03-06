-- lexer_cff_recursive_example.lua
-- This example demonstrates how to perform a RECURSIVE NESTED CFF obfuscation.
-- Techniques combined:
-- 1. 4-Layered State Machine: `__zone` -> `__group` -> `__sub_group` -> `__stage`.
-- 2. Recursive Dispatcher: A local function handles execution and calls itself recursively for state transitions.
-- 3. State Encoding: Arithmetic-based state transitions.
-- 4. Opaque Predicates & Bogus Blocks.

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

local function generate_junk_code()
    local junk = {
        "local _j1 = " .. math.random(10, 99) .. " * " .. math.random(10, 99) .. "\n",
        "local _j2 = tostring(" .. math.random(1000, 9999) .. ")\n",
        "local _j3 = { a = " .. math.random(1, 9) .. " }\n",
        "if " .. math.random(10, 99) .. " > " .. math.random(100, 999) .. " then return end\n"
    }
    shuffle(junk)
    return "              " .. junk[1] .. "              " .. junk[2]
end

local function generate_opaque_predicate()
    local a = math.random(2, 9)
    local b = math.random(2, 9)
    local c = a * b
    return a .. " * " .. b .. " == " .. c
end

local function flatten_recursive(func_node)
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

    -- Assign random IDs
    local states = {}
    local final_exit_id = generate_random_id()
    local final_exit_sg = generate_random_id()
    local final_exit_gr = generate_random_id()
    local final_exit_zn = generate_random_id()

    for i = 1, #transformed_statements do
        states[i] = {
            zone_id = generate_random_id(),
            group_id = generate_random_id(),
            sub_id = generate_random_id(),
            stage_id = generate_random_id(),
            stmt = transformed_statements[i]
        }
    end

    for i = 1, #states do
        if i < #states then
            states[i].next_zone = states[i+1].zone_id
            states[i].next_group = states[i+1].group_id
            states[i].next_sub = states[i+1].sub_id
            states[i].next_stage = states[i+1].stage_id
        else
            states[i].next_zone = final_exit_zn
            states[i].next_group = final_exit_gr
            states[i].next_sub = final_exit_sg
            states[i].next_stage = final_exit_id
        end
    end

    -- Generate a few bogus blocks
    local num_bogus = math.floor(#transformed_statements / 2) + 1
    for i = 1, num_bogus do
        table.insert(states, {
            zone_id = generate_random_id(),
            group_id = generate_random_id(),
            sub_id = generate_random_id(),
            stage_id = generate_random_id(),
            stmt = {{token=lexer.TK_NAME, type="<name>", value="-- bogus block"}},
            is_bogus = true,
            next_zone = generate_random_id(),
            next_group = generate_random_id(),
            next_sub = generate_random_id(),
            next_stage = generate_random_id()
        })
    end

    local start_z, start_g, start_sg, start_s
    if #transformed_statements > 0 then
        start_z = states[1].zone_id
        start_g = states[1].group_id
        start_sg = states[1].sub_id
        start_s = states[1].stage_id
    else
        start_z = final_exit_zn
        start_g = final_exit_gr
        start_sg = final_exit_sg
        start_s = final_exit_id
    end

    -- Build map: Zone -> Group -> SubGroup -> Stage
    local map = {}
    for _, state in ipairs(states) do
        local z = state.zone_id
        local g = state.group_id
        local sg = state.sub_id

        map[z] = map[z] or {}
        map[z][g] = map[z][g] or {}
        map[z][g][sg] = map[z][g][sg] or {}

        table.insert(map[z][g][sg], state)
    end

    local z_ids = {}
    for z, _ in pairs(map) do table.insert(z_ids, z) end
    shuffle(z_ids)

    -- Define the recursive dispatcher inside the function body
    table.insert(code_parts, "\n  local __zone, __group, __sub_group, __stage\n")
    table.insert(code_parts, "  local function __dispatcher()\n")

    local first_z = true
    for _, z in ipairs(z_ids) do
        table.insert(code_parts, first_z and "    if __zone == " .. z .. " then\n" or "    elseif __zone == " .. z .. " then\n")
        first_z = false

        table.insert(code_parts, "      switch (__group) do\n")

        local g_ids = {}
        for g, _ in pairs(map[z]) do table.insert(g_ids, g) end
        shuffle(g_ids)

        for _, g in ipairs(g_ids) do
            table.insert(code_parts, "        case " .. g .. ":\n")
            table.insert(code_parts, "          switch (__sub_group) do\n")

            local sg_ids = {}
            for sg, _ in pairs(map[z][g]) do table.insert(sg_ids, sg) end
            shuffle(sg_ids)

            for _, sg in ipairs(sg_ids) do
                table.insert(code_parts, "            case " .. sg .. ":\n")
                table.insert(code_parts, "              switch (__stage) do\n")

                local stage_list = map[z][g][sg]
                shuffle(stage_list)

                for _, state in ipairs(stage_list) do
                    table.insert(code_parts, "                case " .. state.stage_id .. ":\n")
                    table.insert(code_parts, "                  if " .. generate_opaque_predicate() .. " then\n")

                    if state.is_bogus then
                        table.insert(code_parts, generate_junk_code())
                    else
                        table.insert(code_parts, "                    " .. lexer.reconstruct(state.stmt) .. "\n")
                    end

                    local is_return = false
                    local is_unconditional_return = false

                    if not state.is_bogus then
                        for _, t in ipairs(state.stmt) do
                            if t.type == "'return'" then is_return = true end
                        end
                        if state.stmt[1] and state.stmt[1].type == "'return'" then
                            is_unconditional_return = true
                        end
                    end

                    if not is_unconditional_return then
                        local diff_z = state.next_zone - z
                        local diff_g = state.next_group - g
                        local diff_sg = state.next_sub - sg
                        local diff_s = state.next_stage - state.stage_id

                        table.insert(code_parts, "                    __zone = __zone + (" .. diff_z .. ")\n")
                        table.insert(code_parts, "                    __group = __group + (" .. diff_g .. ")\n")
                        table.insert(code_parts, "                    __sub_group = __sub_group + (" .. diff_sg .. ")\n")
                        table.insert(code_parts, "                    __stage = __stage + (" .. diff_s .. ")\n")
                        -- RECURSIVE CALL instead of goto
                        table.insert(code_parts, "                    return __dispatcher()\n")
                    end

                    table.insert(code_parts, "                  else\n")
                    table.insert(code_parts, generate_junk_code())
                    table.insert(code_parts, "                  end\n")
                    table.insert(code_parts, "                  break\n")
                end
                table.insert(code_parts, "              end\n")
                table.insert(code_parts, "              break\n")
            end
            table.insert(code_parts, "          end\n")
            table.insert(code_parts, "          break\n")
        end
        table.insert(code_parts, "      end\n")
    end

    if first_z then
        table.insert(code_parts, "    if __zone == " .. final_exit_zn .. " then\n")
    else
        table.insert(code_parts, "    elseif __zone == " .. final_exit_zn .. " then\n")
    end
    table.insert(code_parts, "      return\n")
    table.insert(code_parts, "    end\n")
    table.insert(code_parts, "  end\n\n")

    -- Initial kick-off of the state machine
    table.insert(code_parts, "  __zone = " .. start_z .. "\n")
    table.insert(code_parts, "  __group = " .. start_g .. "\n")
    table.insert(code_parts, "  __sub_group = " .. start_sg .. "\n")
    table.insert(code_parts, "  __stage = " .. start_s .. "\n")
    table.insert(code_parts, "  return __dispatcher()\n")

    table.insert(code_parts, "end\n")

    return table.concat(code_parts)
end

local function walk(node)
    if node.type == "function" then
        return flatten_recursive(node)
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
                out_code = out_code .. "local " .. flatten_recursive(node.elements[i+1]) .. " "
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
local function recursive_example()
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
recursive_example()
]]

print("--- Original Code ---")
print(code)

print("--- Recursive CFF Obfuscated Code ---")
local refactored = obfuscate(code)
print(refactored)

print("--- Execution Output ---")
local f, err = load(refactored)
if f then
    f()
else
    print("Failed to load refactored code:", err)
end
