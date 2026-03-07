-- lexer_cff_god_mode.lua
-- GOD MODE CONTROL-FLOW FLATTENING
-- 1. All features of Final Boss (Virtualization, Recursion, Deep Nesting, Math State, String Splits).
-- 2. Number Literal Obfuscation: Turns `100` into `((100+A)~B~B)-A`.
-- 3. Global Hiding: Turns `print` into `_G[("p".."r".."i".."n".."t")]`.
-- 4. Decoy State Variables: Dead variables that mutate alongside real state.
-- 5. Dynamic Opaque Predicates: Mixes math logic with environment constants.

local lexer = require("lexer")

math.randomseed(os.time())

local function shuffle_inplace(tbl)
    local n = #tbl
    for i = n, 2, -1 do
        local j = math.random(i)
        tbl[i], tbl[j] = tbl[j], tbl[i]
    end
end

local function generate_random_id()
    return math.random(1000, 9999)
end

local function generate_opaque_predicate()
    local env_checks = {
        "#tostring(math.pi) > 2",
        "type(_G) == 'table'",
        "1 == 1"
    }
    local a = math.random(2, 5)
    local b = math.random(2, 5)
    local c = a * b
    local x = math.random(10, 20)
    local y = math.random(10, 20)
    local z = x ~ y

    local base = "(" .. a .. " * " .. b .. " == " .. c .. ") and (" .. x .. " ~ " .. y .. " == " .. z .. ")"
    return base .. " and (" .. env_checks[math.random(1, #env_checks)] .. ")"
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

local function split_string_to_tokens(str_val, line_num)
    local inner = str_val
    if inner:sub(1,1) == '"' or inner:sub(1,1) == "'" then
        inner = str_val:sub(2, -2)
    end
    if #inner == 0 then
        return {{token=0, type="<string>", value="", line=line_num}}
    end

    local tokens = {}
    table.insert(tokens, {token=0, type="'('", value="(", line=line_num})

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
            table.insert(tokens, {token=0, type="'..'", value="..", line=line_num})
        end
        is_first = false

        table.insert(tokens, {token=0, type="<raw>", value='"' .. val .. '"', line=line_num})
    end

    table.insert(tokens, {token=0, type="')'", value=")", line=line_num})
    return tokens
end

local function obfuscate_number(num_str)
    local num = tonumber(num_str)
    if not num or math.floor(num) ~= num then return num_str end
    local A = math.random(10, 99)
    local B = math.random(100, 999)
    return "((((" .. num .. " + " .. A .. ") ~ " .. B .. ") ~ " .. B .. ") - " .. A .. ")"
end

local function flatten_god_mode(func_node)
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
            else
                table.insert(body_tokens, t)
            end
        end
    end

    local var_map = {}
    local next_v_idx = 1

    local function extract_params(f_node)
        local in_params = false
        for _, el in ipairs(f_node.elements) do
            if el.type == "body" or el.type == "'end'" then break end
            local flat = lexer.flatten_tree({elements={el}})
            for _, t in ipairs(flat) do
                if t.value == "(" or t.type == "'('" then in_params = true
                elseif t.value == ")" or t.type == "')'" then in_params = false
                elseif in_params and t.type == "<name>" then
                    if not var_map[t.value] then
                        var_map[t.value] = next_v_idx
                        next_v_idx = next_v_idx + 1
                    end
                end
            end
        end
    end
    extract_params(func_node)

    local statements = lexer.split_statements(body_tokens)
    local transformed_statements = {}

    local function is_keyword(r)
        if r.token and r.token >= 257 and r.token <= 278 then return true end
        local kw = {["return"]=true, ["if"]=true, ["then"]=true, ["end"]=true, ["local"]=true, ["for"]=true, ["while"]=true, ["do"]=true, ["break"]=true, ["continue"]=true, ["function"]=true, ["in"]=true, ["elseif"]=true, ["else"]=true, ["not"]=true, ["and"]=true, ["or"]=true}
        if kw[r.value] then return true end
        return false
    end

    local function transform_tokens(rest, out_stmt)
        local prev_val = ""
        for i, r in ipairs(rest) do
            local val = r.value or r.type
            if val and type(val) == "string" and val:sub(1,1) == "'" then val = val:sub(2, -2) end

            local next_token = rest[i+1]
            local next_val = next_token and (next_token.value or next_token.type) or ""
            if next_val and type(next_val) == "string" and next_val:sub(1,1) == "'" then next_val = next_val:sub(2, -2) end

            if r.type == "<string>" or r.token == lexer.TK_STRING then
                local split_tokens = split_string_to_tokens(r.value, r.line)
                for _, st in ipairs(split_tokens) do table.insert(out_stmt, st) end
            elseif r.type == "<number>" or r.token == lexer.TK_FLT or r.token == lexer.TK_INT then
                table.insert(out_stmt, {token=0, type="<raw>", value=obfuscate_number(r.value), line=r.line})
            elseif r.type == "<name>" then
                if next_val == "=" and prev_val == "{" or next_val == "=" and prev_val == "," then
                    table.insert(out_stmt, r)
                elseif var_map[r.value] then
                    if prev_val == "." or prev_val == ":" then
                        table.insert(out_stmt, r)
                    elseif type(var_map[r.value]) == "string" then
                        table.insert(out_stmt, r)
                    else
                        table.insert(out_stmt, {token=0, type="<raw>", value="__V[" .. var_map[r.value] .. "]", line=r.line})
                    end
                elseif is_keyword(r) then
                    table.insert(out_stmt, r)
                else
                    if prev_val == "." or prev_val == ":" then
                        table.insert(out_stmt, r)
                    elseif prev_val == "for" or prev_val == "local" then
                        var_map[r.value] = r.value
                        table.insert(out_stmt, r)
                    else
                        if var_map[r.value] == r.value then
                            table.insert(out_stmt, r)
                        else
                            table.insert(out_stmt, {token=0, type="<raw>", value="_G", line=r.line})
                            table.insert(out_stmt, {token=0, type="'['", value="[", line=r.line})
                            local g_toks = split_string_to_tokens('"'..r.value..'"', r.line)
                            for _, st in ipairs(g_toks) do table.insert(out_stmt, st) end
                            table.insert(out_stmt, {token=0, type="']'", value="]", line=r.line})
                        end
                    end
                end
            else
                table.insert(out_stmt, r)
            end
            prev_val = val
        end
    end

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
                    table.insert(new_stmt, {token=0, type="<raw>", value="__V["..var_map[n].."]", line=stmt[1].line})
                    if idx < #names then
                        table.insert(new_stmt, {token=string.byte(','), type="','", value=",", line=stmt[1].line})
                    end
                end

                transform_tokens(rest, new_stmt)
                table.insert(transformed_statements, new_stmt)
            end
        else
            local new_stmt = {}
            transform_tokens(stmt, new_stmt)
            table.insert(transformed_statements, new_stmt)
        end
    end

    local code_parts = {}

    local fs_reconstruct = ""
    for _, t in ipairs(func_start) do
        local v = t.value or t.type
        if v and v:sub(1,1)=="'" then v = v:sub(2, -2) end
        if t.type == "<name>" and var_map[v] then
            fs_reconstruct = fs_reconstruct .. "__V" .. var_map[v] .. " "
        else
            fs_reconstruct = fs_reconstruct .. v .. " "
        end
    end
    table.insert(code_parts, fs_reconstruct)

    table.insert(code_parts, "\n  local __V = {[0]=0}\n")

    local map_args = ""
    for arg_name, arg_idx in pairs(var_map) do
        if type(arg_idx) == "number" and arg_idx < next_v_idx then
            map_args = map_args .. "  if __V"..arg_idx.." ~= nil then __V["..arg_idx.."] = __V"..arg_idx.." end\n"
        end
    end
    table.insert(code_parts, map_args)

    table.insert(code_parts, "  local __zone, __group, __stage\n")
    table.insert(code_parts, "  local __decoy1, __decoy2 = " .. math.random(100, 999) .. ", " .. math.random(100, 999) .. "\n")

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

                -- We use manual concat since we injected raw value tokens
                local stmt_text = ""
                for _, t in ipairs(state.stmt) do
                    local val = t.value
                    if not val then
                        if t.type and t.type:sub(1,1) == "'" then
                            val = t.type:sub(2, -2)
                        else
                            val = t.type
                        end
                    end
                    stmt_text = stmt_text .. val .. " "
                end
                table.insert(code_parts, "                " .. stmt_text .. "\n")

                local is_unconditional_return = false
                if state.stmt[1] and (state.stmt[1].type == "'return'" or state.stmt[1].value == "return") then
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

                    -- Decoy mutations
                    table.insert(code_parts, "                __decoy1 = (__decoy1 ~ " .. math.random(1, 99) .. ") * " .. math.random(2, 5) .. "\n")
                    table.insert(code_parts, "                __decoy2 = __decoy2 + __decoy1 - " .. math.random(1, 99) .. "\n")

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
        return flatten_god_mode(node)
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
                out_code = out_code .. "local " .. flatten_god_mode(node.elements[i+1]) .. " "
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
local function god_mode_example()
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
god_mode_example()
]]

print("--- Original Code ---")
print(code)

print("--- God Mode CFF Obfuscated Code ---")
local refactored = obfuscate(code)
print(refactored)

print("--- Execution Output ---")
local f, err = load(refactored)
if f then
    f()
else
    print("Failed to load refactored code:", err)
end
