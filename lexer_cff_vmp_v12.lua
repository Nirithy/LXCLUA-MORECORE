-- lexer_cff_vmp.lua
-- lexer_cff_vmp_v12.lua
-- ABYSS MODE V12: TRUE VMP MODE + DYNAMIC BYTECODE + EXTENDED ISA
-- Introduces a randomly generated, proprietary Virtual Machine (CPU Emulator).
-- 1. Random ISA: Opcodes for ADD, SUB, MUL, MOVE, LOADK are scrambled per-build.
-- 2. Bytecode Compiler: Converts target mathematical assignments into VMP byte arrays.
-- 3. Fetch-Decode-Execute: A custom VM loop runs the byte arrays inside the labyrinth.
-- 4. 7D Nested CFF: The labyrinth guides the flow of VM chunks.

local lexer = require("lexer")
math.randomseed(os.time())

local function shuffle_inplace(tbl)
    local n = #tbl
    for i = n, 2, -1 do
        local j = math.random(i)
        tbl[i], tbl[j] = tbl[j], tbl[i]
    end
end

local function generate_random_id() return math.random(100, 999) end

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

-- ==============================================================
-- VMP ENGINE GENERATOR
-- ==============================================================

local OP_HALT = math.random(1, 15)
local OP_LOADK = math.random(16, 30)
local OP_MOVE = math.random(31, 45)
local OP_ADD = math.random(46, 60)
local OP_SUB = math.random(61, 75)
local OP_MUL = math.random(76, 90)
local OP_DIV = math.random(91, 105)
local OP_MOD = math.random(106, 120)
local OP_JMP = math.random(121, 135)
local OP_JMP_T = math.random(136, 150)
local OP_XOR = math.random(151, 165)
local OP_AND = math.random(166, 180)
local OP_OR = math.random(181, 195)
local OP_SHL = math.random(196, 210)
local OP_SHR = math.random(211, 225)
local OP_EQ = math.random(226, 240)
local OP_LT = math.random(241, 255)
local OP_NEWTABLE = math.random(256, 270)
local OP_GETTABLE = math.random(271, 285)
local OP_SETTABLE = math.random(286, 300)
local OP_CALL0 = math.random(301, 315)
local OP_CALL1 = math.random(316, 330)
local OP_CALL2 = math.random(331, 345)

-- Dynamic Number Obfuscation to calculate bytecode array elements
local function obfuscate_number(n)
    if type(n) ~= "number" then return tostring(n) end
    local x = math.random(10, 99)
    local y = math.random(10, 99)
    local k = math.random(1, 50)
    local base = x ~ y
    local diff = n - base - k
    return "((("..x.." ~ "..y..") + "..k..") + ("..diff.."))"
end

local function inject_vm_engine(code_parts)
    local vm_code = string.format([=[
  local function __VMP_EXEC(_bc, _k)
      local pc = 1
      while true do
          local op = _bc[pc]
          if not op then break end
          if op == %d then break
          elseif op == %d then __M[_bc[pc+1]] = _k[_bc[pc+2]]; pc = pc + 3
          elseif op == %d then __M[_bc[pc+1]] = __M[_bc[pc+2]]; pc = pc + 3
          elseif op == %d then __M[_bc[pc+1]] = __M[_bc[pc+2]] + __M[_bc[pc+3]]; pc = pc + 4
          elseif op == %d then __M[_bc[pc+1]] = __M[_bc[pc+2]] - __M[_bc[pc+3]]; pc = pc + 4
          elseif op == %d then __M[_bc[pc+1]] = __M[_bc[pc+2]] * __M[_bc[pc+3]]; pc = pc + 4
          elseif op == %d then __M[_bc[pc+1]] = __M[_bc[pc+2]] / __M[_bc[pc+3]]; pc = pc + 4
          elseif op == %d then __M[_bc[pc+1]] = __M[_bc[pc+2]] %% __M[_bc[pc+3]]; pc = pc + 4
          elseif op == %d then pc = pc + _bc[pc+1]
          elseif op == %d then if __M[_bc[pc+1]] then pc = pc + _bc[pc+2] else pc = pc + 3 end
          elseif op == %d then __M[_bc[pc+1]] = __M[_bc[pc+2]] ~ __M[_bc[pc+3]]; pc = pc + 4
          elseif op == %d then __M[_bc[pc+1]] = __M[_bc[pc+2]] & __M[_bc[pc+3]]; pc = pc + 4
          elseif op == %d then __M[_bc[pc+1]] = __M[_bc[pc+2]] | __M[_bc[pc+3]]; pc = pc + 4
          elseif op == %d then __M[_bc[pc+1]] = __M[_bc[pc+2]] << __M[_bc[pc+3]]; pc = pc + 4
          elseif op == %d then __M[_bc[pc+1]] = __M[_bc[pc+2]] >> __M[_bc[pc+3]]; pc = pc + 4
          elseif op == %d then __M[_bc[pc+1]] = (__M[_bc[pc+2]] == __M[_bc[pc+3]]); pc = pc + 4
          elseif op == %d then __M[_bc[pc+1]] = (__M[_bc[pc+2]] < __M[_bc[pc+3]]); pc = pc + 4
          elseif op == %d then __M[_bc[pc+1]] = {}; pc = pc + 2
          elseif op == %d then __M[_bc[pc+1]] = __M[_bc[pc+2]][__M[_bc[pc+3]]]; pc = pc + 4
          elseif op == %d then __M[_bc[pc+1]][__M[_bc[pc+2]]] = __M[_bc[pc+3]]; pc = pc + 4
          elseif op == %d then __M[_bc[pc+1]] = __M[_bc[pc+2]](); pc = pc + 3
          elseif op == %d then __M[_bc[pc+1]] = __M[_bc[pc+2]](__M[_bc[pc+3]]); pc = pc + 4
          elseif op == %d then __M[_bc[pc+1]] = __M[_bc[pc+2]](__M[_bc[pc+3]], __M[_bc[pc+4]]); pc = pc + 5
          else break end
      end
  end
]=], OP_HALT, OP_LOADK, OP_MOVE, OP_ADD, OP_SUB, OP_MUL, OP_DIV, OP_MOD, OP_JMP, OP_JMP_T, OP_XOR, OP_AND, OP_OR, OP_SHL, OP_SHR, OP_EQ, OP_LT, OP_NEWTABLE, OP_GETTABLE, OP_SETTABLE, OP_CALL0, OP_CALL1, OP_CALL2)
    table.insert(code_parts, vm_code)
end

-- Simple compiler for assignments: __M[A] = __M[B] + 10 => VMP Bytes
-- Deep sliding-window compiler for assignments inline within ANY block
local function try_compile_to_vmp(stmt_tokens, var_map, next_v_idx)
    local bc = {}
    local k_table = {}
    local final_tokens = {}
    local i = 1
    local compiled_anything = false

    local function extract_reg(val_str)
        if not val_str or type(val_str) ~= "string" then return nil end
        local r = val_str:match("^__M%[%%%({0,1}([0-9]+)") -- catches raw __M[123] or __M[(((123
        if r then return tonumber(r) end
        return nil
    end

    while i <= #stmt_tokens do
        local matched = false
        local t1 = stmt_tokens[i]
        local t2 = stmt_tokens[i+1]
        local t3 = stmt_tokens[i+2]
        local t4 = stmt_tokens[i+3]
        local t5 = stmt_tokens[i+4]

        local dest_reg = t1.type == "<name>" and var_map[t1.value]
        local is_eq = t2 and (t2.value == "=" or t2.type == "'='")

        if dest_reg and type(dest_reg) == "number" and is_eq then
            -- Pattern: A = B OP C
            if t5 and t3 and t4 then
                local src1_reg = t3.type == "<name>" and var_map[t3.value]
                local src2_reg = t5.type == "<name>" and var_map[t5.value]
                local is_src1_num = t3.type == "<number>" or t3.type == "<integer>"
                local is_src2_num = t5.type == "<number>" or t5.type == "<integer>"

                if type(src1_reg) ~= "number" then src1_reg = nil end
                if type(src2_reg) ~= "number" then src2_reg = nil end

                local op_val = t4.value or t4.type
                if op_val and op_val:sub(1,1)=="'" then op_val = op_val:sub(2,-2) end

                local opcode
                if op_val == "+" then opcode = OP_ADD
                elseif op_val == "-" then opcode = OP_SUB
                elseif op_val == "*" then opcode = OP_MUL
                elseif op_val == "/" then opcode = OP_DIV
                elseif op_val == "%" then opcode = OP_MOD
                end

                if op_val == "~" then opcode = OP_XOR
                elseif op_val == "&" then opcode = OP_AND
                elseif op_val == "|" then opcode = OP_OR
                elseif op_val == "<<" then opcode = OP_SHL
                elseif op_val == ">>" then opcode = OP_SHR
                elseif op_val == "==" then opcode = OP_EQ
                elseif op_val == "<" then opcode = OP_LT
                end

                local is_complex_op = false
                local t6 = stmt_tokens[i+5]
                if t6 and (t6.value == "." or t6.value == ":" or t6.value == "[" or t6.value == "(" or t6.type == "'.'" or t6.type == "':'" or t6.type == "'['" or t6.type == "'('") then
                    is_complex_op = true
                end

                if opcode and (src1_reg or is_src1_num) and (src2_reg or is_src2_num) and not is_complex_op then
                    local reg1 = src1_reg
                    if is_src1_num then
                        table.insert(k_table, obfuscate_number(tonumber(t3.value)))
                        reg1 = 250
                        table.insert(bc, obfuscate_number(OP_LOADK)) table.insert(bc, obfuscate_number(reg1)) table.insert(bc, obfuscate_number(#k_table))
                    end
                    local reg2 = src2_reg
                    if is_src2_num then
                        table.insert(k_table, obfuscate_number(tonumber(t5.value)))
                        reg2 = 251
                        table.insert(bc, obfuscate_number(OP_LOADK)) table.insert(bc, obfuscate_number(reg2)) table.insert(bc, obfuscate_number(#k_table))
                    end
                    table.insert(bc, obfuscate_number(opcode)) table.insert(bc, obfuscate_number(dest_reg)) table.insert(bc, obfuscate_number(reg1)) table.insert(bc, obfuscate_number(reg2))
                    table.insert(bc, obfuscate_number(OP_HALT))

                    table.insert(final_tokens, {token=0, type="<raw>", value="__VMP_EXEC({"..table.concat(bc, ",").."}, {"..table.concat(k_table, ",").."})", line=t1.line})
                    bc = {} k_table = {}
                    i = i + 5
                    matched = true
                    compiled_anything = true
                end
            end

            -- Pattern: A = NUMBER
            local is_complex = false
            if t4 and (t4.value == "." or t4.value == ":" or t4.value == "[" or t4.value == "(" or t4.type == "'.'" or t4.type == "':'" or t4.type == "'['" or t4.type == "'('") then
                is_complex = true
            end

            if not matched and t3 and (t3.type == "<number>" or t3.type == "<integer>") and not is_complex then
                table.insert(k_table, obfuscate_number(tonumber(t3.value)))
                table.insert(bc, obfuscate_number(OP_LOADK)) table.insert(bc, obfuscate_number(dest_reg)) table.insert(bc, obfuscate_number(#k_table)) table.insert(bc, obfuscate_number(OP_HALT))
                table.insert(final_tokens, {token=0, type="<raw>", value="__VMP_EXEC({"..table.concat(bc, ",").."}, {"..table.concat(k_table, ",").."})", line=t1.line})
                bc = {} k_table = {}
                i = i + 3
                matched = true
                compiled_anything = true
            end

            -- Pattern: A = B
            if not matched and t3 and t3.type == "<name>" and type(var_map[t3.value]) == "number" and not is_complex then
                local src_reg = var_map[t3.value]
                table.insert(bc, obfuscate_number(OP_MOVE)) table.insert(bc, obfuscate_number(dest_reg)) table.insert(bc, obfuscate_number(src_reg)) table.insert(bc, obfuscate_number(OP_HALT))
                table.insert(final_tokens, {token=0, type="<raw>", value="__VMP_EXEC({"..table.concat(bc, ",").."}, {})", line=t1.line})
                bc = {}
                i = i + 3
                matched = true
                compiled_anything = true
            end

            -- Pattern: A = {}
            if not matched and t3 and t3.value == "{" and t4 and t4.value == "}" then
                table.insert(bc, obfuscate_number(OP_NEWTABLE)) table.insert(bc, obfuscate_number(dest_reg)) table.insert(bc, obfuscate_number(OP_HALT))
                table.insert(final_tokens, {token=0, type="<raw>", value="__VMP_EXEC({"..table.concat(bc, ",").."}, {})", line=t1.line})
                bc = {}
                i = i + 4
                matched = true
                compiled_anything = true
            end

            -- Pattern: A = B[C]
            if not matched and t3 and t3.type == "<name>" and t4 and (t4.value == "[" or t4.type == "'['") and t5 and t5.type == "<name>" then
                local t6 = stmt_tokens[i+5]
                if t6 and (t6.value == "]" or t6.type == "']'") then
                    local src_table_reg = var_map[t3.value]
                    local src_key_reg = var_map[t5.value]
                    if type(src_table_reg) == "number" and type(src_key_reg) == "number" then
                        table.insert(bc, obfuscate_number(OP_GETTABLE)) table.insert(bc, obfuscate_number(dest_reg)) table.insert(bc, obfuscate_number(src_table_reg)) table.insert(bc, obfuscate_number(src_key_reg)) table.insert(bc, obfuscate_number(OP_HALT))
                        table.insert(final_tokens, {token=0, type="<raw>", value="__VMP_EXEC({"..table.concat(bc, ",").."}, {})", line=t1.line})
                        bc = {}
                        i = i + 6
                        matched = true
                        compiled_anything = true
                    end
                end
            end

            -- Pattern: A = B(C)  (CALL1)
            if not matched and t3 and t3.type == "<name>" and t4 and (t4.value == "(" or t4.type == "'('") and t5 and t5.type == "<name>" then
                local t6 = stmt_tokens[i+5]
                if t6 and (t6.value == ")" or t6.type == "')'") then
                    local func_reg = var_map[t3.value]
                    local arg1_reg = var_map[t5.value]
                    if type(func_reg) == "number" and type(arg1_reg) == "number" then
                        table.insert(bc, obfuscate_number(OP_CALL1)) table.insert(bc, obfuscate_number(dest_reg)) table.insert(bc, obfuscate_number(func_reg)) table.insert(bc, obfuscate_number(arg1_reg)) table.insert(bc, obfuscate_number(OP_HALT))
                        table.insert(final_tokens, {token=0, type="<raw>", value="__VMP_EXEC({"..table.concat(bc, ",").."}, {})", line=t1.line})
                        bc = {}
                        i = i + 6
                        matched = true
                        compiled_anything = true
                    end
                end
            end

            -- Pattern: A = B()  (CALL0)
            if not matched and t3 and t3.type == "<name>" and t4 and (t4.value == "(" or t4.type == "'('") and t5 and (t5.value == ")" or t5.type == "')'") then
                local func_reg = var_map[t3.value]
                if type(func_reg) == "number" then
                    table.insert(bc, obfuscate_number(OP_CALL0)) table.insert(bc, obfuscate_number(dest_reg)) table.insert(bc, obfuscate_number(func_reg)) table.insert(bc, obfuscate_number(OP_HALT))
                    table.insert(final_tokens, {token=0, type="<raw>", value="__VMP_EXEC({"..table.concat(bc, ",").."}, {})", line=t1.line})
                    bc = {}
                    i = i + 5
                    matched = true
                    compiled_anything = true
                end
            end
        end

        -- Pattern: A[B] = C
        if not matched and t1 and t1.type == "<name>" and t2 and (t2.value == "[" or t2.type == "'['") and t3 and t3.type == "<name>" and t4 and (t4.value == "]" or t4.type == "']'") and t5 and (t5.value == "=" or t5.type == "'='") then
            local t6 = stmt_tokens[i+5]
            if t6 and t6.type == "<name>" then
                local dest_table_reg = var_map[t1.value]
                local dest_key_reg = var_map[t3.value]
                local val_reg = var_map[t6.value]

                local t7 = stmt_tokens[i+6]
                local is_complex = false
                if t7 and (t7.value == "." or t7.value == ":" or t7.value == "[" or t7.value == "(" or t7.type == "'.'" or t7.type == "':'" or t7.type == "'['" or t7.type == "'('") then
                    is_complex = true
                end

                if type(dest_table_reg) == "number" and type(dest_key_reg) == "number" and type(val_reg) == "number" and not is_complex then
                    table.insert(bc, obfuscate_number(OP_SETTABLE)) table.insert(bc, obfuscate_number(dest_table_reg)) table.insert(bc, obfuscate_number(dest_key_reg)) table.insert(bc, obfuscate_number(val_reg)) table.insert(bc, obfuscate_number(OP_HALT))
                    table.insert(final_tokens, {token=0, type="<raw>", value="__VMP_EXEC({"..table.concat(bc, ",").."}, {})", line=t1.line})
                    bc = {}
                    i = i + 6
                    matched = true
                    compiled_anything = true
                end
            end
        end

        if not matched then
            table.insert(final_tokens, stmt_tokens[i])
            i = i + 1
        end
    end

    if compiled_anything then return final_tokens end
    return nil
end

local function split_string_anti_hook(str_val, line_num)
    local inner = str_val
    if inner:sub(1,1) == '"' or inner:sub(1,1) == "'" then inner = str_val:sub(2, -2) end
    if #inner == 0 then return {{token=0, type="<raw>", value='""', line=line_num}} end
    local tokens = {{token=0, type="<raw>", value="(", line=line_num}}
    local is_first = true
    local idx = 1
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
        local key = math.random(10, 250)
        local encrypted_byte = byte_val ~ key
        if not is_first then table.insert(tokens, {token=0, type="<raw>", value="..", line=line_num}) end
        is_first = false
        table.insert(tokens, {token=0, type="<raw>", value="(string.char(" .. encrypted_byte .. ") ~ " .. key .. ")", line=line_num})
    end
    table.insert(tokens, {token=0, type="<raw>", value=")", line=line_num})
    return tokens
end

local function is_keyword(v)
    local kw = {["return"]=true, ["if"]=true, ["then"]=true, ["end"]=true, ["local"]=true, ["for"]=true, ["while"]=true, ["do"]=true, ["break"]=true, ["continue"]=true, ["function"]=true, ["in"]=true, ["elseif"]=true, ["else"]=true, ["not"]=true, ["and"]=true, ["or"]=true, ["try"]=true, ["catch"]=true, ["finally"]=true, ["switch"]=true, ["case"]=true, ["default"]=true}
    return kw[v] == true
end

local function extract_params(func_node, var_map, next_v_idx)
    local in_params = false
    -- We only look at tokens up to the FIRST ')' to get parameters
    local flat = lexer.flatten_tree({elements={func_node}})
    for _, t in ipairs(flat) do
        if t.value == "(" or t.type == "'('" then
            in_params = true
        elseif t.value == ")" or t.type == "')'" then
            break -- stop parsing parameters after the first closing parenthesis
        elseif in_params and t.type == "<name>" then
            if not var_map[t.value] then
                var_map[t.value] = next_v_idx
                next_v_idx = next_v_idx + 1
            end
        end
    end
    return next_v_idx
end

local function generate_fractal_coordinates(num_states)
    local coords = {}
    local bounds = {3, 3, 3, 3, 3, 3, 3}
    local curr = {1,1,1,1,1,1,1}
    for i = 1, num_states do
        table.insert(coords, {table.unpack(curr)})
        for d = 7, 1, -1 do
            curr[d] = curr[d] + 1
            if curr[d] <= bounds[d] then break end
            curr[d] = 1
        end
    end
    shuffle_inplace(coords)
    return coords
end

local function flatten_v11(func_node)
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
                local split_tokens = split_string_anti_hook(r.value, r.line)
                for _, st in ipairs(split_tokens) do table.insert(out_stmt, st) end
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
                            local g_toks = split_string_anti_hook('"'..r.value..'"', r.line)
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

                -- ATTEMPT DEEP VMP COMPILATION on the unmapped tokens
                local vmp_stmt = try_compile_to_vmp(rest, var_map, next_v_idx)

                local mapped_rest = {}
                transform_tokens(vmp_stmt or rest, mapped_rest)

                for _, st in ipairs(mapped_rest) do table.insert(new_stmt, st) end

                table.insert(transformed_statements, new_stmt)
                table.insert(snippet_pool, new_stmt)
            end
        else
            -- ATTEMPT DEEP VMP COMPILATION on unmapped tokens
            local vmp_stmt = try_compile_to_vmp(stmt, var_map, next_v_idx)
            local mapped_stmt = {}
            transform_tokens(vmp_stmt or stmt, mapped_stmt)

            table.insert(transformed_statements, mapped_stmt)
            if mapped_stmt[1] and mapped_stmt[1].value ~= "return" and mapped_stmt[1].value ~= "break" then
                table.insert(snippet_pool, mapped_stmt)
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
    local TOTAL_STATES = 350

    local M_Z = {}
    for i=1, DIMENSIONS do M_Z[i] = 100 + i end

    local mt_hook = [[
  local _s = ""
  local _mt = getmetatable(_s) or {}
  _mt.__bxor = function(a, b)
      if type(a) == "string" and type(b) == "number" then return string.char(string.byte(a) ~ b) end
      return a
  end
  if debug and debug.setmetatable then debug.setmetatable(_s, _mt) end
]]
    table.insert(code_parts, "\n" .. mt_hook)
    table.insert(code_parts, "  local __M = {[0]=0}\n  for _i=1, 300 do __M[_i] = 0 end\n")

    local map_args = ""
    for arg_name, arg_idx in pairs(var_map) do
        if type(arg_idx) == "number" and arg_idx < next_v_idx then
            map_args = map_args .. "  if __M"..arg_idx.." ~= nil then __M["..encrypt_index(arg_idx).."] = __M"..arg_idx.." end\n"
        end
    end
    table.insert(code_parts, map_args)

    inject_vm_engine(code_parts)

    local all_coords = generate_fractal_coordinates(TOTAL_STATES)

    local states = {}
    for i = 1, #transformed_statements do
        local c = table.remove(all_coords)
        states[i] = { c = c, stmt = transformed_statements[i], is_bogus = false }
    end

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

    local available_fakes = {}
    for _, fs in ipairs(fake_states) do table.insert(available_fakes, fs) end
    shuffle_inplace(available_fakes)

    for i = 1, #real_states do
        if i < #real_states then
            local hops = math.random(1, 3)
            if hops > #available_fakes then hops = #available_fakes end

            local current = real_states[i]
            for h = 1, hops do
                local r_fake = table.remove(available_fakes)
                current.next_c = r_fake.c
                current = r_fake
            end
            current.next_c = real_states[i+1].c
        else
            real_states[i].next_c = final_exit
        end
    end

    for _, state in ipairs(available_fakes) do
        if #fake_states > 0 then state.next_c = fake_states[math.random(1, #fake_states)].c else state.next_c = final_exit end
    end

    local start_c = #real_states > 0 and real_states[1].c or final_exit

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
                                    local vm_state_jump = {}
                                    local k_table_state = {}
                                    for l=1, DIMENSIONS do
                                        local xor_v = math.random(100, 999)
                                        local add_v = state.next_c[l] - (z_list[l] ~ xor_v)
                                        -- Compile state jump to VM bytecodes instead of raw math!
                                        table.insert(k_table_state, obfuscate_number(xor_v))
                                        table.insert(vm_state_jump, obfuscate_number(OP_LOADK))
                                        table.insert(vm_state_jump, obfuscate_number(250))
                                        table.insert(vm_state_jump, obfuscate_number(#k_table_state))
                                        table.insert(vm_state_jump, obfuscate_number(OP_XOR))
                                        table.insert(vm_state_jump, obfuscate_number(M_Z[l]))
                                        table.insert(vm_state_jump, obfuscate_number(M_Z[l]))
                                        table.insert(vm_state_jump, obfuscate_number(250))

                                        table.insert(k_table_state, obfuscate_number(add_v))
                                        table.insert(vm_state_jump, obfuscate_number(OP_LOADK))
                                        table.insert(vm_state_jump, obfuscate_number(250))
                                        table.insert(vm_state_jump, obfuscate_number(#k_table_state))
                                        table.insert(vm_state_jump, obfuscate_number(OP_ADD))
                                        table.insert(vm_state_jump, obfuscate_number(M_Z[l]))
                                        table.insert(vm_state_jump, obfuscate_number(M_Z[l]))
                                        table.insert(vm_state_jump, obfuscate_number(250))
                                    end
                                    table.insert(vm_state_jump, obfuscate_number(OP_HALT))
                                    table.insert(code_parts, "                                __VMP_EXEC({"..table.concat(vm_state_jump, ",").."}, {"..table.concat(k_table_state, ",").."})\n")
                                    if math.random() > 0.5 then table.insert(code_parts, "                                return __dispatcher()\n")
                                    else table.insert(code_parts, "                                error('LABYRINTH')\n") end
                                end

                                table.insert(code_parts, "                              else\n")
                                if state.is_bogus then
                                    local z_list = {z1, z2, z3, z4, z5, z6, state.c[7]}
                                    local vm_state_jump = {}
                                    local k_table_state = {}
                                    for l=1, DIMENSIONS do
                                        local xor_v = math.random(100, 999)
                                        local add_v = state.next_c[l] - (z_list[l] ~ xor_v)
                                        table.insert(k_table_state, obfuscate_number(xor_v))
                                        table.insert(vm_state_jump, obfuscate_number(OP_LOADK))
                                        table.insert(vm_state_jump, obfuscate_number(250))
                                        table.insert(vm_state_jump, obfuscate_number(#k_table_state))
                                        table.insert(vm_state_jump, obfuscate_number(OP_XOR))
                                        table.insert(vm_state_jump, obfuscate_number(M_Z[l]))
                                        table.insert(vm_state_jump, obfuscate_number(M_Z[l]))
                                        table.insert(vm_state_jump, obfuscate_number(250))

                                        table.insert(k_table_state, obfuscate_number(add_v))
                                        table.insert(vm_state_jump, obfuscate_number(OP_LOADK))
                                        table.insert(vm_state_jump, obfuscate_number(250))
                                        table.insert(vm_state_jump, obfuscate_number(#k_table_state))
                                        table.insert(vm_state_jump, obfuscate_number(OP_ADD))
                                        table.insert(vm_state_jump, obfuscate_number(M_Z[l]))
                                        table.insert(vm_state_jump, obfuscate_number(M_Z[l]))
                                        table.insert(vm_state_jump, obfuscate_number(250))
                                    end
                                    table.insert(vm_state_jump, obfuscate_number(OP_HALT))
                                    table.insert(code_parts, "                                __VMP_EXEC({"..table.concat(vm_state_jump, ",").."}, {"..table.concat(k_table_state, ",").."})\n")
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
    if node.type == "function" then return flatten_v11(node) end
    local out_code = ""
    if not node.elements then return lexer.reconstruct({node}) end

    local skip_next = false
    for i=1, #node.elements do
        if skip_next then skip_next = false
        else
            local el = node.elements[i]
            if el.type == "'local'" and node.elements[i+1] and node.elements[i+1].type == "function" then
                out_code = out_code .. "local " .. flatten_v11(node.elements[i+1]) .. " "
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
local function vmp_example(start_val)
  local obj = { hp = start_val, name = "Matrix" }
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
  print("VMP Result: ", obj.hp, " MP: ", mp)
end
vmp_example(100)
]]

print("--- Original Code ---")
print(code)

print("--- Abyss V12 VMP Obfuscated Code ---")
local refactored = obfuscate(code)

local lines = {}
for line in string.gmatch(refactored, "[^\r\n]+") do table.insert(lines, line) end

-- DEBUG syntax error lines
local f, err = load(refactored)
if err then
    local err_line = err:match(":(%d+): syntax error")
    if err_line then
        err_line = tonumber(err_line)
        print("\n\n=== DUMPING AROUND ERROR LINE " .. err_line .. " ===")
        print(table.concat(lines, "\n", err_line - 5, err_line + 5))
        print("=========================================\n\n")
    end
end

print(table.concat(lines, "\n", 1, 80))
print("-- [ ... " .. (#lines - 80) .. " lines hidden ... ] --")

print("--- Execution Output ---")
if f then
    f()
else
    print("Failed to load refactored code:", err)
end
