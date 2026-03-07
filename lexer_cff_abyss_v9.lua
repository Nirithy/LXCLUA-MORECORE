-- lexer_cff_abyss_v9.lua
-- ABYSS MODE V9: INFINITE FRACTAL
-- 1. Restores 7D nesting fully.
-- 2. Uses a Fractal coordinate generator so EVERY layer has multiple branches (density).
-- 3. In-line String Decryption: No central `__D` decoder function. Strings are decrypted inside the AST natively.
-- 4. Unified Memory Mapping (`__M`) and non-linear Exception-Based Recursion.

local lexer = require("lexer")
math.randomseed(os.time())

local function shuffle_inplace(tbl)
    local n = #tbl
    for i = n, 2, -1 do
        local j = math.random(i)
        tbl[i], tbl[j] = tbl[j], tbl[i]
    end
end

local function generate_random_var_name()
    local chars = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ"
    local name = "_"
    for i = 1, math.random(4, 7) do
        local r = math.random(1, #chars)
        name = name .. chars:sub(r, r)
    end
    return name
end

local function generate_opaque_predicate(is_true)
    local x1 = math.random(10, 99)
    local y1 = math.random(10, 99)
    local z1 = x1 ~ y1
    local r1 = math.random(10, 99)
    local tautology = "((("..r1.." * "..r1..") + "..r1..") % 2 == 0)"
    local fallacy = "((("..r1.." * "..r1..") + "..r1..") % 2 ~= 0)"
    local dyn_check = "(#tostring("..math.random(100,999)..") > 0)"

    if is_true then
        return "(" .. x1 .. " ~ " .. y1 .. " == " .. z1 .. ") and " .. tautology .. " and " .. dyn_check
    else
        return "(" .. x1 .. " ~ " .. y1 .. " ~= " .. z1 .. ") or " .. fallacy
    end
end

local function encrypt_index(idx)
    local a = math.random(10, 255)
    local b = math.random(10, 255)
    local xor_val = a ~ b
    local c = idx - xor_val
    return "((("..a.." ~ "..b..") + ("..c..")))"
end

local function split_string_inline_decrypt(str_val, line_num)
    local inner = str_val
    if inner:sub(1,1) == '"' or inner:sub(1,1) == "'" then inner = str_val:sub(2, -2) end
    if #inner == 0 then return {{token=0, type="<raw>", value='""', line=line_num}} end

    local tokens = {{token=0, type="<raw>", value="(", line=line_num}}
    local is_first = true
    local idx = 1
    local key = math.random(10, 250)

    while idx <= #inner do
        local c = inner:sub(idx,idx)
        local byte_val = string.byte(c)
        if c == "\\" then
            local next_c = inner:sub(idx+1, idx+1)
            if next_c == "n" then byte_val = 10
            elseif next_c == "t" then byte_val = 9
            elseif next_c == "r" then byte_val = 13
            elseif next_c == '"' then byte_val = 34
            elseif next_c == "'" then byte_val = 39
            elseif next_c == "\\" then byte_val = 92
            end
            idx = idx + 2
        else
            idx = idx + 1
        end

        local encrypted_byte = byte_val ~ key
        if not is_first then table.insert(tokens, {token=0, type="<raw>", value="..", line=line_num}) end
        is_first = false

        -- Inline decryption: string.char( encrypted_byte ~ key )
        table.insert(tokens, {token=0, type="<raw>", value='string.char(' .. encrypted_byte .. ' ~ ' .. key .. ')', line=line_num})
    end
    table.insert(tokens, {token=0, type="<raw>", value=")", line=line_num})
    return tokens
end

local function obfuscate_number(num_str)
    local num = tonumber(num_str)
    if not num or math.floor(num) ~= num then return num_str end
    local A = math.random(10, 99)
    local B = math.random(100, 999)
    return "((((" .. num .. " + " .. A .. ") ~ " .. B .. ") ~ " .. B .. ") - " .. A .. ")"
end

local function is_keyword(v)
    local kw = {["return"]=true, ["if"]=true, ["then"]=true, ["end"]=true, ["local"]=true, ["for"]=true, ["while"]=true, ["do"]=true, ["break"]=true, ["continue"]=true, ["function"]=true, ["in"]=true, ["elseif"]=true, ["else"]=true, ["not"]=true, ["and"]=true, ["or"]=true, ["try"]=true, ["catch"]=true, ["finally"]=true, ["switch"]=true, ["case"]=true, ["default"]=true}
    return kw[v] == true
end

local function extract_params(func_node, var_map, next_v_idx)
    local in_params = false
    for _, el in ipairs(func_node.elements) do
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
    return next_v_idx
end

-- Generates a 7D bushy tree. We enforce breadth at higher levels.
local function generate_fractal_coordinates(num_states)
    local coords = {}
    -- To ensure many branches per level, we cycle through small domains
    local bounds = {3, 3, 3, 3, 3, 3, 3}

    local curr = {1,1,1,1,1,1,1}
    for i = 1, num_states do
        table.insert(coords, {table.unpack(curr)})
        -- Increment odometer style
        for d = 7, 1, -1 do
            curr[d] = curr[d] + 1
            if curr[d] <= bounds[d] then break end
            curr[d] = 1
        end
    end
    shuffle_inplace(coords)
    return coords
end

local function flatten_v9(func_node)
    local body_tokens = {}
    local params_ended = false
    local func_start = {}

    for i, el in ipairs(func_node.elements) do
        local flat_el = lexer.flatten_tree({elements={el}})
        for _, t in ipairs(flat_el) do
            if not params_ended then
                table.insert(func_start, t)
                if t.type == "')'" or t.value == ")" then params_ended = true end
            elseif i == #func_node.elements and (t.type == "'end'" or t.value == "end") then
            else
                table.insert(body_tokens, t)
            end
        end
    end

    local var_map = {}
    local next_v_idx = 1
    next_v_idx = extract_params(func_node, var_map, next_v_idx)

    local statements = lexer.split_statements(body_tokens)
    local transformed_statements = {}
    local snippet_pool = {}

    local function transform_tokens(rest, out_stmt)
        local prev_val = ""
        for i, r in ipairs(rest) do
            local val = r.value or r.type
            if val and type(val) == "string" and val:sub(1,1) == "'" then val = val:sub(2, -2) end

            local next_token = rest[i+1]
            local next_val = next_token and (next_token.value or next_token.type) or ""
            if next_val and type(next_val) == "string" and next_val:sub(1,1) == "'" then next_val = next_val:sub(2, -2) end

            if r.type == "<string>" or r.token == lexer.TK_STRING then
                local split_tokens = split_string_inline_decrypt(r.value, r.line)
                for _, st in ipairs(split_tokens) do table.insert(out_stmt, st) end
            elseif r.type == "<number>" or r.token == lexer.TK_FLT or r.token == lexer.TK_INT then
                table.insert(out_stmt, {token=0, type="<raw>", value=obfuscate_number(r.value), line=r.line})
            elseif r.type == "<name>" then
                if next_val == "=" and prev_val == "{" or next_val == "=" and prev_val == "," then
                    table.insert(out_stmt, r)
                elseif var_map[r.value] then
                    if prev_val == "." or prev_val == ":" then table.insert(out_stmt, r)
                    elseif type(var_map[r.value]) == "string" then table.insert(out_stmt, r)
                    else table.insert(out_stmt, {token=0, type="<raw>", value="__M[" .. encrypt_index(var_map[r.value]) .. "]", line=r.line}) end
                elseif is_keyword(r.value) then table.insert(out_stmt, r)
                else
                    if prev_val == "." or prev_val == ":" then table.insert(out_stmt, r)
                    elseif prev_val == "for" or prev_val == "local" or prev_val == "function" then
                        var_map[r.value] = r.value
                        table.insert(out_stmt, r)
                    else
                        if var_map[r.value] == r.value then table.insert(out_stmt, r)
                        else
                            table.insert(out_stmt, {token=0, type="<raw>", value="_G", line=r.line})
                            table.insert(out_stmt, {token=0, type="'['", value="[", line=r.line})
                            local g_toks = split_string_inline_decrypt('"'..r.value..'"', r.line)
                            for _, st in ipairs(g_toks) do table.insert(out_stmt, st) end
                            table.insert(out_stmt, {token=0, type="']'", value="]", line=r.line})
                        end
                    end
                end
            else table.insert(out_stmt, r) end
            prev_val = val
        end
    end

    for _, stmt in ipairs(statements) do
        local names, rest = lexer.parse_local(stmt)
        if names then
            for _, n in ipairs(names) do if not var_map[n] then var_map[n] = next_v_idx; next_v_idx = next_v_idx + 1 end end
            if #rest > 0 then
                local new_stmt = {}
                for idx, n in ipairs(names) do
                    table.insert(new_stmt, {token=0, type="<raw>", value="__M["..encrypt_index(var_map[n]).."]", line=stmt[1].line})
                    if idx < #names then table.insert(new_stmt, {token=0, type="<raw>", value=",", line=stmt[1].line}) end
                end
                transform_tokens(rest, new_stmt)
                table.insert(transformed_statements, new_stmt)
                table.insert(snippet_pool, new_stmt)
            end
        else
            local new_stmt = {}
            transform_tokens(stmt, new_stmt)
            table.insert(transformed_statements, new_stmt)
            if stmt[1] and stmt[1].value ~= "return" and stmt[1].value ~= "break" then
                table.insert(snippet_pool, new_stmt)
            end
        end
    end

    if #snippet_pool == 0 then table.insert(snippet_pool, {{token=0, type="<raw>", value="__M[200] = __M[200] + 1"}}) end

    local code_parts = {}
    local fs_reconstruct = ""
    for _, t in ipairs(func_start) do
        local v = t.value or t.type
        if v and v:sub(1,1)=="'" then v = v:sub(2, -2) end
        if t.type == "<name>" and var_map[v] then fs_reconstruct = fs_reconstruct .. "__M" .. var_map[v] .. " "
        else fs_reconstruct = fs_reconstruct .. v .. " " end
    end
    table.insert(code_parts, fs_reconstruct)

    local DIMENSIONS = 7
    local TOTAL_STATES = 350 -- About 350 nodes is very dense across 7D

    local M_Z = {}
    for i=1, DIMENSIONS do M_Z[i] = 100 + i end

    table.insert(code_parts, "\n  local __M = {[0]=0}\n  for _i=1, 300 do __M[_i] = 0 end\n")
    local map_args = ""
    for arg_name, arg_idx in pairs(var_map) do
        if type(arg_idx) == "number" and arg_idx < next_v_idx then
            map_args = map_args .. "  if __M"..arg_idx.." ~= nil then __M["..encrypt_index(arg_idx).."] = __M"..arg_idx.." end\n"
        end
    end
    table.insert(code_parts, map_args)

    local all_coords = generate_fractal_coordinates(TOTAL_STATES)

    local states = {}
    for i = 1, #transformed_statements do
        local c = table.remove(all_coords)
        states[i] = { c = c, stmt = transformed_statements[i], is_bogus = false }
    end

    -- Fill the rest with bogus clones
    while #all_coords > 0 do
        local c = table.remove(all_coords)
        local template = snippet_pool[math.random(1, #snippet_pool)]
        table.insert(states, { c = c, stmt = template, is_bogus = true })
    end

    local final_exit = {99, 99, 99, 99, 99, 99, 99}

    local real_states = {}
    local fake_states = {}
    for _, state in ipairs(states) do
        if not state.is_bogus then table.insert(real_states, state) else table.insert(fake_states, state) end
    end

    -- Network non-linear hops
    for i = 1, #real_states do
        if i < #real_states then
            local intermediate_hops = math.random(2, 5)
            local current = real_states[i]
            for h = 1, intermediate_hops do
                local r_fake = fake_states[math.random(1, #fake_states)]
                current.next_c = r_fake.c
                current = r_fake
            end
            current.next_c = real_states[i+1].c
        else
            real_states[i].next_c = final_exit
        end
    end

    for _, state in ipairs(fake_states) do
        if not state.next_c then state.next_c = fake_states[math.random(1, #fake_states)].c end
    end

    local start_c = #real_states > 0 and real_states[1].c or final_exit

    -- Map [Z1][Z2][Z3][Z4][Z5][Z6][Z7]
    local map = {}
    for _, state in ipairs(states) do
        local c = state.c
        map[c[1]] = map[c[1]] or {}
        map[c[1]][c[2]] = map[c[1]][c[2]] or {}
        map[c[1]][c[2]][c[3]] = map[c[1]][c[2]][c[3]] or {}
        map[c[1]][c[2]][c[3]][c[4]] = map[c[1]][c[2]][c[3]][c[4]] or {}
        map[c[1]][c[2]][c[3]][c[4]][c[5]] = map[c[1]][c[2]][c[3]][c[4]][c[5]] or {}
        map[c[1]][c[2]][c[3]][c[4]][c[5]][c[6]] = map[c[1]][c[2]][c[3]][c[4]][c[5]][c[6]] or {}
        table.insert(map[c[1]][c[2]][c[3]][c[4]][c[5]][c[6]], state)
    end

    local function get_keys(t) local k={} for x,_ in pairs(t) do table.insert(k,x) end return k end

    table.insert(code_parts, "  local function __dispatcher()\n")
    table.insert(code_parts, "    try\n")

    local first_z1 = true
    for _, z1 in ipairs(get_keys(map)) do
        table.insert(code_parts, first_z1 and "      if __M["..encrypt_index(M_Z[1]).."] == " .. z1 .. " then\n" or "      elseif __M["..encrypt_index(M_Z[1]).."] == " .. z1 .. " then\n")
        first_z1 = false
        table.insert(code_parts, "        switch (__M["..encrypt_index(M_Z[2]).."]) do\n")
        for _, z2 in ipairs(get_keys(map[z1])) do
            table.insert(code_parts, "          case " .. z2 .. ":\n")
            table.insert(code_parts, "            switch (__M["..encrypt_index(M_Z[3]).."]) do\n")
            for _, z3 in ipairs(get_keys(map[z1][z2])) do
                table.insert(code_parts, "              case " .. z3 .. ":\n")
                table.insert(code_parts, "                switch (__M["..encrypt_index(M_Z[4]).."]) do\n")
                for _, z4 in ipairs(get_keys(map[z1][z2][z3])) do
                    table.insert(code_parts, "                  case " .. z4 .. ":\n")
                    table.insert(code_parts, "                    switch (__M["..encrypt_index(M_Z[5]).."]) do\n")
                    for _, z5 in ipairs(get_keys(map[z1][z2][z3][z4])) do
                        table.insert(code_parts, "                      case " .. z5 .. ":\n")
                        table.insert(code_parts, "                        switch (__M["..encrypt_index(M_Z[6]).."]) do\n")
                        for _, z6 in ipairs(get_keys(map[z1][z2][z3][z4][z5])) do
                            table.insert(code_parts, "                          case " .. z6 .. ":\n")
                            local stage_list = map[z1][z2][z3][z4][z5][z6]
                            shuffle_inplace(stage_list)
                            for _, state in ipairs(stage_list) do
                                table.insert(code_parts, "                            if __M["..encrypt_index(M_Z[7]).."] == " .. state.c[7] .. " then\n")

                                local is_true_pred = not state.is_bogus
                                table.insert(code_parts, "                              if " .. generate_opaque_predicate(is_true_pred) .. " then\n")

                                local stmt_text = ""
                                for _, t in ipairs(state.stmt) do
                                    local val = t.value
                                    if not val then if t.type and t.type:sub(1,1) == "'" then val = t.type:sub(2, -2) else val = t.type end end
                                    stmt_text = stmt_text .. val .. " "
                                end

                                local is_unconditional_return = false
                                if state.stmt[1] and (state.stmt[1].type == "'return'" or state.stmt[1].value == "return") then is_unconditional_return = true end

                                table.insert(code_parts, "                                " .. stmt_text .. "\n")

                                if not is_unconditional_return then
                                    local z_list = {z1, z2, z3, z4, z5, z6, state.c[7]}
                                    for l=1, DIMENSIONS do
                                        local xor_v = math.random(100, 999)
                                        local add_v = state.next_c[l] - (z_list[l] ~ xor_v)
                                        table.insert(code_parts, "                                __M["..encrypt_index(M_Z[l]).."] = (__M["..encrypt_index(M_Z[l]).."] ~ " .. xor_v .. ") + (" .. add_v .. ")\n")
                                    end
                                    if math.random() > 0.5 then table.insert(code_parts, "                                return __dispatcher()\n")
                                    else table.insert(code_parts, "                                error('LABYRINTH')\n") end
                                end

                                table.insert(code_parts, "                              else\n")
                                if state.is_bogus then
                                    local z_list = {z1, z2, z3, z4, z5, z6, state.c[7]}
                                    for l=1, DIMENSIONS do
                                        local xor_v = math.random(100, 999)
                                        local add_v = state.next_c[l] - (z_list[l] ~ xor_v)
                                        table.insert(code_parts, "                                __M["..encrypt_index(M_Z[l]).."] = (__M["..encrypt_index(M_Z[l]).."] ~ " .. xor_v .. ") + (" .. add_v .. ")\n")
                                    end
                                    if math.random() > 0.5 then table.insert(code_parts, "                                return __dispatcher()\n")
                                    else table.insert(code_parts, "                                error('LABYRINTH')\n") end
                                else
                                    table.insert(code_parts, "                                error('DEAD_END_REAL')\n")
                                end
                                table.insert(code_parts, "                              end\n")

                                table.insert(code_parts, "                            end\n")
                            end
                            table.insert(code_parts, "                            break\n")
                        end
                        table.insert(code_parts, "                        end\n")
                        table.insert(code_parts, "                        break\n")
                    end
                    table.insert(code_parts, "                    end\n")
                    table.insert(code_parts, "                    break\n")
                end
                table.insert(code_parts, "                end\n")
                table.insert(code_parts, "                break\n")
            end
            table.insert(code_parts, "            end\n")
            table.insert(code_parts, "            break\n")
        end
        table.insert(code_parts, "        end\n")
    end

    table.insert(code_parts, "      elseif __M["..encrypt_index(M_Z[1]).."] == " .. final_exit[1] .. " then\n")
    table.insert(code_parts, "        return\n")
    table.insert(code_parts, "      end\n")

    table.insert(code_parts, "    catch(e)\n")
    table.insert(code_parts, "      if type(e) == 'string' and e:match('LABYRINTH') then return __dispatcher() else error(e) end\n")
    table.insert(code_parts, "    end\n")
    table.insert(code_parts, "  end\n\n")

    for i=1, DIMENSIONS do
        table.insert(code_parts, "  __M["..encrypt_index(M_Z[i]).."] = " .. start_c[i] .. "\n")
    end
    table.insert(code_parts, "  return __dispatcher()\n")

    table.insert(code_parts, "end\n")

    return table.concat(code_parts)
end

local function walk(node)
    if node.type == "function" then return flatten_v9(node) end
    local out_code = ""
    if not node.elements then return lexer.reconstruct({node}) end

    local skip_next = false
    for i=1, #node.elements do
        if skip_next then skip_next = false
        else
            local el = node.elements[i]
            if el.type == "'local'" and node.elements[i+1] and node.elements[i+1].type == "function" then
                out_code = out_code .. "local " .. flatten_v9(node.elements[i+1]) .. " "
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

local code = [[
local function abyss_v9_example(start_val)
  local obj = { hp = start_val, name = "Terminator" }
  local mp = 50
  for i = 1, 3 do
    local step = i * 10
    if step > 15 then
      obj.hp = obj.hp + step
      if obj.hp > 140 then break end
    else
      mp = mp + 1
    end
  end
  print("Abyss V9 Result: ", obj.hp, " MP: ", mp)
end
abyss_v9_example(100)
]]

print("--- Original Code ---")
print(code)

print("--- Abyss V9 Infinite Fractal Obfuscated Code ---")
local refactored = obfuscate(code)

-- Due to extreme length (tens of thousands of lines), we print the first 50 lines
local lines = {}
for line in string.gmatch(refactored, "[^\r\n]+") do table.insert(lines, line) end
print(table.concat(lines, "\n", 1, 50))
print("-- [ ... " .. (#lines - 50) .. " lines hidden ... ] --")

print("--- Execution Output ---")
local f, err = load(refactored)
if f then
    f()
else
    print("Failed to load refactored code:", err)
end
