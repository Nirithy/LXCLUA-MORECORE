local lexer = require("lexer")

local _M = {}

-- Generates a purely mathematical dynamically evaluated opaque predicate block
-- It evaluates math logic that always evaluates to true, used to mask transitions.
function _M.generate_opaque_predicate()
    -- Generate two random variables
    local a_val = math.random(1, 100)
    local b_val = math.random(1, 100)

    local v1 = string.char(math.random(97, 122)) .. string.char(math.random(97, 122))
    local v2 = string.char(math.random(97, 122)) .. string.char(math.random(97, 122))

    local condition = string.format("((%s * %s) - (%s + %s)) == %d", v1, v2, v1, v2, (a_val * b_val) - (a_val + b_val))

    local code = string.format("local %s = %d; local %s = %d; if %s then", v1, a_val, v2, b_val, condition)
    local tokens = lexer.lex(code)

    return tokens
end

-- Virtualizes global variable accesses mapping `_G` to a specific register (e.g., `__M[0]`)
function _M.virtualize_globals(tokens, global_reg_name)
    local result = {}
    -- Deep copy the tokens for modification
    for i, t in ipairs(tokens) do
        table.insert(result, {
            token = t.token,
            type = t.type,
            value = t.value,
            line = t.line
        })
    end

    -- Known built-ins and keywords to ignore (very basic list for example purposes)
    local builtins = {
        ["print"] = true, ["require"] = true, ["math"] = true, ["string"] = true, ["table"] = true,
        ["coroutine"] = true, ["os"] = true, ["io"] = true, ["debug"] = true, ["_G"] = true,
        ["assert"] = true, ["error"] = true, ["getmetatable"] = true, ["setmetatable"] = true,
        ["ipairs"] = true, ["pairs"] = true, ["next"] = true, ["pcall"] = true, ["xpcall"] = true,
        ["select"] = true, ["tonumber"] = true, ["tostring"] = true, ["type"] = true,
        ["unpack"] = true, ["rawget"] = true, ["rawset"] = true, ["rawlen"] = true, ["rawequal"] = true,
        ["module"] = true, ["load"] = true, ["loadfile"] = true, ["loadstring"] = true, ["dofile"] = true,
        ["collectgarbage"] = true
    }

    local local_vars = {}

    -- Simplistic approach to tracking locals: just find 'local x'
    for i = 1, #result do
        if result[i].type == "'local'" and result[i+1] and result[i+1].type == "<name>" then
            local_vars[result[i+1].value] = true
        end
    end

    -- we want to iterate backwards because replacing tokens changes the table length and indices
    local i = #result
    while i >= 1 do
        local t = result[i]
        -- If it's a name, not a local, and not a field access (e.g. not `obj.name`)
        if t.type == "<name>" then
            local is_field_access = false
            if i > 1 and result[i-1].type == "'.'" then
                is_field_access = true
            elseif i > 1 and result[i-1].type == "':'" then
                is_field_access = true
            end

            if not is_field_access and not local_vars[t.value] and builtins[t.value] then
                -- Replace `print` with `__M[0]["print"]`
                local replacement_code = string.format('%s["%s"]', global_reg_name, t.value)
                local rep_tokens = lexer.lex(replacement_code)
                lexer.replace_tokens(result, i, i, rep_tokens)
            end
        end
        i = i - 1
    end

    return result
end

-- String encryption via pure math string resolution using `debug.setmetatable` on `""`.
-- It avoids `string.char` function calls.
function _M.encrypt_string(str, key)
    local chars = {}
    for i = 1, #str do
        local b = string.byte(str, i)
        local enc = b ~ key
        table.insert(chars, tostring(enc))
    end

    -- The output creates a math resolution relying on string metatable `__sub` being overridden.
    -- For example: `"" - (enc ~ key) - (enc2 ~ key)`
    local expr = '""'
    for _, enc in ipairs(chars) do
        expr = expr .. string.format(' - (%s ~ %d)', enc, key)
    end

    return expr
end

return _M
