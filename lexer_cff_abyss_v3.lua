-- lexer_cff_abyss_v3.lua
-- ABYSS MODE V3: THE ABSOLUTE PINNACLE
-- Features:
-- 1. Bytecode Dynamic Loading: Blocks are compiled to Lua bytecode, stripped, XOR encrypted, and loaded dynamically. Hooking `load` only yields bytecode, not source.
-- 2. Char Code String Encryption: Strings are converted to `(string.char(A) .. string.char(B))` to evade string dumping.
-- 3. Pure Randomized Bogus Code: Uses non-suspicious randomized variable names.
-- 4. Unified Memory Mapping (`__M`).
-- 5. Exception-Based Recursive Dispatching.

local lexer = require("lexer")
math.randomseed(os.time())

local function shuffle_inplace(tbl)
    local n = #tbl
    for i = n, 2, -1 do
        local j = math.random(i)
        tbl[i], tbl[j] = tbl[j], tbl[i]
    end
end

local function generate_random_id() return math.random(1000, 9999) end

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
    local a = math.random(2, 5)
    local b = math.random(2, 5)
    local c = a * b
    if is_true then
        return "(" .. a .. " * " .. b .. " == " .. c .. ") and (#tostring(math.pi) > 2)"
    else
        return "(" .. a .. " * " .. b .. " ~= " .. c .. ") or (#tostring(math.pi) < 2)"
    end
end

local function generate_realistic_bogus_code(var_map, next_v_idx)
    local v_idx = math.random(1, next_v_idx > 1 and next_v_idx - 1 or 1)
    local fake_var = generate_random_var_name()
    local fake_loop = generate_random_var_name()
    local fake_code = {
        "__M["..v_idx.."] = __M["..v_idx.."] + " .. math.random(1, 100),
        "if __M["..v_idx.."] > " .. math.random(100, 999) .. " then __M["..v_idx.."] = 0 end",
        "local " .. fake_var .. " = " .. math.random(100, 999),
        "_G['type'](__M["..v_idx.."])",
        "for " .. fake_loop .. " = 1, " .. math.random(2, 5) .. " do __M[200] = __M[200] + 1 end"
    }
    shuffle_inplace(fake_code)
    return "                  " .. fake_code[1] .. "\n                  " .. fake_code[2] .. "\n"
end

-- String to string.char() concatenation
local function split_string_to_tokens(str_val, line_num)
    local inner = str_val
    if inner:sub(1,1) == '"' or inner:sub(1,1) == "'" then
        inner = str_val:sub(2, -2)
    end
    if #inner == 0 then return {{token=0, type="<raw>", value='""', line=line_num}} end

    local tokens = {{token=0, type="<raw>", value="(", line=line_num}}
    local idx = 1
    local is_first = true
    while idx <= #inner do
        local c = inner:sub(idx,idx)
        local byte_val = string.byte(c)
        if c == "\\" then
            -- simplistic escape evaluation for this example
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
        if not is_first then table.insert(tokens, {token=0, type="<raw>", value="..", line=line_num}) end
        is_first = false
        table.insert(tokens, {token=0, type="<raw>", value='string.char(' .. byte_val .. ')', line=line_num})
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

local function flatten_abyss(func_node)
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
                        table.insert(out_stmt, {token=0, type="<raw>", value="__M[" .. var_map[r.value] .. "]", line=r.line})
                    end
                elseif is_keyword(r.value) then
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
                    table.insert(new_stmt, {token=0, type="<raw>", value="__M["..var_map[n].."]", line=stmt[1].line})
                    if idx < #names then table.insert(new_stmt, {token=0, type="<raw>", value=",", line=stmt[1].line}) end
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
            fs_reconstruct = fs_reconstruct .. "__M" .. var_map[v] .. " "
        else
            fs_reconstruct = fs_reconstruct .. v .. " "
        end
    end
    table.insert(code_parts, fs_reconstruct)

    local MEM_Z = 101
    local MEM_G = 102
    local MEM_S = 103
    local MEM_D = 200

    table.insert(code_parts, "\n  local __M = {[0]=0}\n  for _i=1, 200 do __M[_i] = 0 end\n")

    local map_args = ""
    for arg_name, arg_idx in pairs(var_map) do
        if type(arg_idx) == "number" and arg_idx < next_v_idx then
            map_args = map_args .. "  if __M"..arg_idx.." ~= nil then __M["..arg_idx.."] = __M"..arg_idx.." end\n"
        end
    end
    table.insert(code_parts, map_args)

    local states = {}
    local final_exit_s = generate_random_id()
    local final_exit_g = generate_random_id()
    local final_exit_z = generate_random_id()

    for i = 1, #transformed_statements do
        states[i] = {
            z = generate_random_id(),
            g = generate_random_id(),
            s = generate_random_id(),
            stmt = transformed_statements[i]
        }
    end

    local num_bogus = math.floor(#transformed_statements / 2) + 2
    for i = 1, num_bogus do
        table.insert(states, {
            z = generate_random_id(),
            g = generate_random_id(),
            s = generate_random_id(),
            stmt = {},
            is_bogus = true
        })
    end

    local real_states = {}
    for _, state in ipairs(states) do if not state.is_bogus then table.insert(real_states, state) end end

    for i = 1, #real_states do
        if i < #real_states then
            real_states[i].next_z = real_states[i+1].z
            real_states[i].next_g = real_states[i+1].g
            real_states[i].next_s = real_states[i+1].s
        else
            real_states[i].next_z = final_exit_z
            real_states[i].next_g = final_exit_g
            real_states[i].next_s = final_exit_s
        end
    end

    for _, state in ipairs(states) do
        if state.is_bogus then
            state.next_z = generate_random_id()
            state.next_g = generate_random_id()
            state.next_s = generate_random_id()
        end
    end

    local start_z, start_g, start_s
    if #real_states > 0 then
        start_z = real_states[1].z
        start_g = real_states[1].g
        start_s = real_states[1].s
    else
        start_z = final_exit_z
        start_g = final_exit_g
        start_s = final_exit_s
    end

    local map = {}
    for _, state in ipairs(states) do
        local z = state.z
        local g = state.g
        map[z] = map[z] or {}
        map[z][g] = map[z][g] or {}
        table.insert(map[z][g], state)
    end

    local z_ids = {}
    for z, _ in pairs(map) do table.insert(z_ids, z) end
    shuffle_inplace(z_ids)

    table.insert(code_parts, "  local function __dispatcher()\n")
    table.insert(code_parts, "    try\n")

    local first_z = true
    for _, z in ipairs(z_ids) do
        table.insert(code_parts, first_z and "      if __M["..MEM_Z.."] == " .. z .. " then\n" or "      elseif __M["..MEM_Z.."] == " .. z .. " then\n")
        first_z = false
        table.insert(code_parts, "        switch (__M["..MEM_G.."]) do\n")

        local g_ids = {}
        for g, _ in pairs(map[z]) do table.insert(g_ids, g) end
        shuffle_inplace(g_ids)

        for _, g in ipairs(g_ids) do
            table.insert(code_parts, "          case " .. g .. ":\n")
            table.insert(code_parts, "            switch (__M["..MEM_S.."]) do\n")

            local stage_list = map[z][g]
            shuffle_inplace(stage_list)

            for _, state in ipairs(stage_list) do
                table.insert(code_parts, "              case " .. state.s .. ":\n")

                if state.is_bogus then
                    table.insert(code_parts, "                if " .. generate_opaque_predicate(false) .. " then\n")
                    table.insert(code_parts, generate_realistic_bogus_code(var_map, next_v_idx))
                    table.insert(code_parts, "                else\n")
                    table.insert(code_parts, "                  __M["..MEM_Z.."] = " .. state.next_z .. "\n")
                    table.insert(code_parts, "                  __M["..MEM_G.."] = " .. state.next_g .. "\n")
                    table.insert(code_parts, "                  __M["..MEM_S.."] = " .. state.next_s .. "\n")
                    table.insert(code_parts, "                  error('VOID_JUMP')\n")
                    table.insert(code_parts, "                end\n")
                else
                    table.insert(code_parts, "                if " .. generate_opaque_predicate(true) .. " then\n")

                    local stmt_text = ""
                    for _, t in ipairs(state.stmt) do
                        local val = t.value
                        if not val then
                            if t.type and t.type:sub(1,1) == "'" then val = t.type:sub(2, -2) else val = t.type end
                        end
                        stmt_text = stmt_text .. val .. " "
                    end

                    local is_unconditional_return = false
                    if state.stmt[1] and (state.stmt[1].type == "'return'" or state.stmt[1].value == "return") then
                        is_unconditional_return = true
                    end

                    local has_conditional_return = false
                    for _, t in ipairs(state.stmt) do
                        if t.type == "'return'" or t.value == "return" then has_conditional_return = true end
                        if t.type == "'break'" or t.value == "break" then has_conditional_return = true end
                    end

                    -- Encrypt the bytecode
                    -- To avoid compiling unclosed blocks (like "for i=1,3 do"), we check if it is structural.
                    local is_structural = false
                    for _, t in ipairs(state.stmt) do
                        if t.value == "if" or t.value == "for" or t.value == "while" then is_structural = true break end
                    end

                    if is_structural or has_conditional_return then
                        -- For structural/control flow, execute normally to maintain safe AST logic.
                        table.insert(code_parts, "                  " .. stmt_text .. "\n")
                    else
                        -- Compile into stripped bytecode and XOR encrypt the bytes
                        local dyn_code = "return function(__M) " .. stmt_text .. " end"
                        local fn = load(dyn_code)
                        if fn then
                            local bc = string.dump(fn, true) -- strip debug
                            local xor_key = math.random(10, 250)
                            local encrypted_bytes = {}
                            for b = 1, #bc do
                                table.insert(encrypted_bytes, string.byte(bc, b) ~ xor_key)
                            end
                            table.insert(code_parts, "                  local _enc = {" .. table.concat(encrypted_bytes, ",") .. "}\n")
                            table.insert(code_parts, "                  local _dec = {}\n")
                            table.insert(code_parts, "                  for _i=1, #_enc do _dec[_i] = string.char(_enc[_i] ~ " .. xor_key .. ") end\n")
                            table.insert(code_parts, "                  local _f = load(table.concat(_dec))\n")
                            table.insert(code_parts, "                  if _f then local _r = _f(); _r(__M) end\n")
                        else
                            -- Fallback if snippet cannot be compiled standalone
                            table.insert(code_parts, "                  " .. stmt_text .. "\n")
                        end
                    end

                    if not is_unconditional_return then
                        local xor_z = math.random(100, 999)
                        local add_z = state.next_z - (z ~ xor_z)
                        local xor_g = math.random(100, 999)
                        local add_g = state.next_g - (g ~ xor_g)
                        local xor_s = math.random(100, 999)
                        local add_s = state.next_s - (state.s ~ xor_s)

                        table.insert(code_parts, "                  __M["..MEM_Z.."] = (__M["..MEM_Z.."] ~ " .. xor_z .. ") + (" .. add_z .. ")\n")
                        table.insert(code_parts, "                  __M["..MEM_G.."] = (__M["..MEM_G.."] ~ " .. xor_g .. ") + (" .. add_g .. ")\n")
                        table.insert(code_parts, "                  __M["..MEM_S.."] = (__M["..MEM_S.."] ~ " .. xor_s .. ") + (" .. add_s .. ")\n")
                        table.insert(code_parts, "                  __M["..MEM_D.."] = (__M["..MEM_D.."] ~ " .. math.random(1,99) .. ") * 2\n")
                        table.insert(code_parts, "                  error('VOID_JUMP')\n")
                    end
                    table.insert(code_parts, "                else\n")
                    table.insert(code_parts, generate_realistic_bogus_code(var_map, next_v_idx))
                    table.insert(code_parts, "                end\n")
                end
                table.insert(code_parts, "                break\n")
            end
            table.insert(code_parts, "            end\n")
            table.insert(code_parts, "            break\n")
        end
        table.insert(code_parts, "        end\n")
    end

    if first_z then
        table.insert(code_parts, "      if __M["..MEM_Z.."] == " .. final_exit_z .. " then\n")
    else
        table.insert(code_parts, "      elseif __M["..MEM_Z.."] == " .. final_exit_z .. " then\n")
    end
    table.insert(code_parts, "        return\n")
    table.insert(code_parts, "      end\n")

    table.insert(code_parts, "    catch(e)\n")
    table.insert(code_parts, "      if type(e) == 'string' and e:match('VOID_JUMP') then return __dispatcher() else error(e) end\n")
    table.insert(code_parts, "    end\n")
    table.insert(code_parts, "  end\n\n")

    table.insert(code_parts, "  __M["..MEM_Z.."] = " .. start_z .. "\n")
    table.insert(code_parts, "  __M["..MEM_G.."] = " .. start_g .. "\n")
    table.insert(code_parts, "  __M["..MEM_S.."] = " .. start_s .. "\n")
    table.insert(code_parts, "  return __dispatcher()\n")

    table.insert(code_parts, "end\n")

    return table.concat(code_parts)
end

local function walk(node)
    if node.type == "function" then
        return flatten_abyss(node)
    end
    local out_code = ""
    if not node.elements then return lexer.reconstruct({node}) end

    local skip_next = false
    for i=1, #node.elements do
        if skip_next then
            skip_next = false
        else
            local el = node.elements[i]
            if el.type == "'local'" and node.elements[i+1] and node.elements[i+1].type == "function" then
                out_code = out_code .. "local " .. flatten_abyss(node.elements[i+1]) .. " "
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
local function abyss_v3_example(start_val)
  local obj = { hp = start_val, name = "Boss" }
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
  print("Abyss Result: ", obj.hp, " MP: ", mp)
end
abyss_v3_example(100)
]]

print("--- Original Code ---")
print(code)

print("--- Abyss V3 Mode CFF Obfuscated Code ---")
local refactored = obfuscate(code)
print(refactored)

print("--- Execution Output ---")
local f, err = load(refactored)
if f then
    f()
else
    print("Failed to load refactored code:", err)
end
