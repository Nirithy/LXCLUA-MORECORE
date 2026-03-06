-- lexer_cff_final_boss.lua
-- THE FINAL BOSS OF CONTROL-FLOW FLATTENING
-- 1. Register Renaming & Virtualization (Local variables map to a `__V` table).
-- 2. State transition via XOR and Math (`__state = (__state ~ A) + B`).
-- 3. Multi-layer Opaque Predicates (Nested math logic).
-- 4. String Splitting (Constant string destruction).
-- 5. In-place Shuffle (No table allocation).
-- 6. Deeply Nested Dispatchers (`__zone` -> `__group` -> `__stage`).
-- 7. Recursive Dispatcher Execution.

local lexer = require("lexer")

math.randomseed(os.time())

-- In-place shuffle to minimize GC overhead
local function shuffle_inplace(tbl)
    local n = #tbl
    for i = n, 2, -1 do
        local j = math.random(i)
        tbl[i], tbl[j] = tbl[j], tbl[i]
    end
end

local function generate_random_id()
    return math.random(100, 999)
end

local function generate_opaque_predicate()
    local a = math.random(2, 5)
    local b = math.random(2, 5)
    local c = a * b
    local x = math.random(10, 20)
    local y = math.random(10, 20)
    local z = x ~ y
    -- Multi-layer opaque predicate: (a * b == c) and (x ~ y == z)
    return "(" .. a .. " * " .. b .. " == " .. c .. ") and (" .. x .. " ~ " .. y .. " == " .. z .. ")"
end

local function generate_junk_code()
    local junk = {
        "__V[0] = " .. math.random(10, 99) .. " * " .. math.random(10, 99) .. "\n",
        "__V[0] = __V[0] ~ " .. math.random(100, 999) .. "\n",
        "if " .. math.random(10, 99) .. " > " .. math.random(100, 999) .. " then return end\n"
    }
    shuffle_inplace(junk)
    return "                  " .. junk[1] .. "                  " .. junk[2]
end

-- Splits a string into concatenated characters as a stream of new lexer tokens.
local function split_string_to_tokens(str_val, line_num)
    -- The token value from lexer actually doesn't have the outer quotes.
    -- Wait, let me check again. Earlier I saw the value was `HP: ` without quotes!
    -- Ah! In the `test_lex.lua` output, the value was exactly `HP: ` without quotes!
    -- So `sub(2, -2)` is eating the first and last chars!
    local inner = str_val

    if #inner == 0 then
        return {{token=lexer.TK_STRING, type="<string>", value="", line=line_num}}
    end

    local tokens = {}
    table.insert(tokens, {token=string.byte('('), type="'('", line=line_num})

    local idx = 1
    local is_first = true
    while idx <= #inner do
        local c = inner:sub(idx,idx)
        local val = c
        if c == "\\" then
            val = c .. inner:sub(idx+1, idx+1)
            idx = idx + 2
        elseif c == '"' then
            val = "\\\""
            idx = idx + 1
        else
            idx = idx + 1
        end

        if not is_first then
            table.insert(tokens, {token=lexer.TK_CONCAT, type="'..'", line=line_num})
        end
        is_first = false

        -- Wait, lexer.reconstruct DOES wrap <string> tokens in quotes if we're not careful?
        -- Actually, looking at the previous output: print(("\"P\"".."\":\""))
        -- It added quotes around our quotes! So we should NOT include quotes in the value if type is <string>.
        -- Wait, let's just make it a <name> token or just raw symbols so reconstruct doesn't quote it again.
        table.insert(tokens, {token=lexer.TK_NAME, type="<name>", value='"' .. val .. '"', line=line_num})
    end

    table.insert(tokens, {token=string.byte(')'), type="')'", line=line_num})
    return tokens
end

local function flatten_final_boss(func_node)
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

    -- Virtualization: Map local variable names to __V array indices
    local var_map = {}
    local next_v_idx = 1

    local transformed_statements = {}

    for _, stmt in ipairs(statements) do
        local names, rest = lexer.parse_local(stmt)
        if names then
            for _, n in ipairs(names) do
                if not var_map[n] then
                    var_map[n] = next_v_idx
                    next_v_idx = next_v_idx + 1
                end
            end
            if #rest > 0 then
                local new_stmt = {}
                for idx, n in ipairs(names) do
                    -- Replace local var declaration with __V array access
                    -- Because it's an assignment, we use it as a <name> (even though it's table index, reconstruct handles it mostly ok or we just emit the raw string as <name>)
                    table.insert(new_stmt, {token=lexer.TK_NAME, type="<name>", value="__V["..var_map[n].."]", line=stmt[1].line})
                    if idx < #names then
                        table.insert(new_stmt, {token=string.byte(','), type="','", line=stmt[1].line})
                    end
                end
                for _, r in ipairs(rest) do
                    if r.type == "<string>" or r.token == lexer.TK_STRING then
                        local split_tokens = split_string_to_tokens(r.value, r.line)
                        for _, st in ipairs(split_tokens) do
                            table.insert(new_stmt, st)
                        end
                    elseif r.type == "<name>" and var_map[r.value] then
                        r.value = "__V[" .. var_map[r.value] .. "]"
                        table.insert(new_stmt, r)
                    else
                        table.insert(new_stmt, r)
                    end
                end
                table.insert(transformed_statements, new_stmt)
            end
        else
            local new_stmt = {}
            for _, r in ipairs(stmt) do
                if r.type == "<string>" or r.token == lexer.TK_STRING then
                    local split_tokens = split_string_to_tokens(r.value, r.line)
                    for _, st in ipairs(split_tokens) do
                        table.insert(new_stmt, st)
                    end
                elseif r.type == "<name>" and var_map[r.value] then
                    r.value = "__V[" .. var_map[r.value] .. "]"
                    table.insert(new_stmt, r)
                else
                    table.insert(new_stmt, r)
                end
            end
            table.insert(transformed_statements, new_stmt)
        end
    end

    local code_parts = {}
    table.insert(code_parts, lexer.reconstruct(func_start))

    -- Initialize Virtual Machine State
    table.insert(code_parts, "\n  local __V = {[0]=0}\n")
    table.insert(code_parts, "  local __zone, __group, __stage\n")

    local states = {}
    local final_exit_id = generate_random_id()
    local final_exit_gr = generate_random_id()
    local final_exit_zn = generate_random_id()

    for i = 1, #transformed_statements do
        states[i] = {
            zone_id = generate_random_id(),
            group_id = generate_random_id(),
            stage_id = generate_random_id(),
            stmt = transformed_statements[i]
        }
    end

    for i = 1, #states do
        if i < #states then
            states[i].next_zone = states[i+1].zone_id
            states[i].next_group = states[i+1].group_id
            states[i].next_stage = states[i+1].stage_id
        else
            states[i].next_zone = final_exit_zn
            states[i].next_group = final_exit_gr
            states[i].next_stage = final_exit_id
        end
    end

    local start_z, start_g, start_s
    if #transformed_statements > 0 then
        start_z = states[1].zone_id
        start_g = states[1].group_id
        start_s = states[1].stage_id
    else
        start_z = final_exit_zn
        start_g = final_exit_gr
        start_s = final_exit_id
    end

    local map = {}
    for _, state in ipairs(states) do
        local z = state.zone_id
        local g = state.group_id
        map[z] = map[z] or {}
        map[z][g] = map[z][g] or {}
        table.insert(map[z][g], state)
    end

    local z_ids = {}
    for z, _ in pairs(map) do table.insert(z_ids, z) end
    shuffle_inplace(z_ids)

    table.insert(code_parts, "  local function __dispatcher()\n")

    local first_z = true
    for _, z in ipairs(z_ids) do
        table.insert(code_parts, first_z and "    if __zone == " .. z .. " then\n" or "    elseif __zone == " .. z .. " then\n")
        first_z = false
        table.insert(code_parts, "      switch (__group) do\n")

        local g_ids = {}
        for g, _ in pairs(map[z]) do table.insert(g_ids, g) end
        shuffle_inplace(g_ids)

        for _, g in ipairs(g_ids) do
            table.insert(code_parts, "        case " .. g .. ":\n")
            table.insert(code_parts, "          switch (__stage) do\n")

            local stage_list = map[z][g]
            shuffle_inplace(stage_list)

            for _, state in ipairs(stage_list) do
                table.insert(code_parts, "            case " .. state.stage_id .. ":\n")
                table.insert(code_parts, "              if " .. generate_opaque_predicate() .. " then\n")

                -- Reconstruct safely using lexer.reconstruct so it handles quotes and spaces correctly
                table.insert(code_parts, "                " .. lexer.reconstruct(state.stmt) .. "\n")

                local is_unconditional_return = false
                if state.stmt[1] and state.stmt[1].type == "'return'" then
                    is_unconditional_return = true
                end

                if not is_unconditional_return then
                    local xor_z = math.random(100, 999)
                    local add_z = state.next_zone - (z ~ xor_z)

                    local xor_g = math.random(100, 999)
                    local add_g = state.next_group - (g ~ xor_g)

                    local xor_s = math.random(100, 999)
                    local add_s = state.next_stage - (state.stage_id ~ xor_s)

                    table.insert(code_parts, "                __zone = (__zone ~ " .. xor_z .. ") + (" .. add_z .. ")\n")
                    table.insert(code_parts, "                __group = (__group ~ " .. xor_g .. ") + (" .. add_g .. ")\n")
                    table.insert(code_parts, "                __stage = (__stage ~ " .. xor_s .. ") + (" .. add_s .. ")\n")
                    table.insert(code_parts, "                return __dispatcher()\n")
                end

                table.insert(code_parts, "              else\n")
                table.insert(code_parts, generate_junk_code())
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

    table.insert(code_parts, "  __zone = " .. start_z .. "\n")
    table.insert(code_parts, "  __group = " .. start_g .. "\n")
    table.insert(code_parts, "  __stage = " .. start_s .. "\n")
    table.insert(code_parts, "  return __dispatcher()\n")

    table.insert(code_parts, "end\n")

    return table.concat(code_parts)
end

local function walk(node)
    if node.type == "function" then
        return flatten_final_boss(node)
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
                out_code = out_code .. "local " .. flatten_final_boss(node.elements[i+1]) .. " "
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
local function final_boss_example()
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
final_boss_example()
]]

print("--- Original Code ---")
print(code)

print("--- Final Boss CFF Obfuscated Code ---")
local refactored = obfuscate(code)
print(refactored)

print("--- Execution Output ---")
local f, err = load(refactored)
if f then
    f()
else
    print("Failed to load refactored code:", err)
end
