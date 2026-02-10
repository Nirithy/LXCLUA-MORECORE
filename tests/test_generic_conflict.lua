print("Testing Generic Conflict Resolution...")

-- Test case from issue
local onClick = function(view, value, valueType, layoutParams, views)
    local _G = { value = function(v) return "G:"..v end }
    views = { value = function(v) return "V:"..v end }

    -- The problematic syntax: (exp)(args) inside a function body
    local result = valueType == "string" and function(v)
      return (views["value"] or _G["value"])(v)
    end or value

    return result
end

local f = onClick(nil, "val", "string", nil, nil)
local res = f("arg")
assert(res == "V:arg", "Expected V:arg, got " .. tostring(res))

-- Test generic factory works
function Generic(T)(val)
    return val
end

local GNum = Generic(number)
assert(GNum(10) == 10)

-- Test anonymous generic factory (if supported?)
-- Lua parser supports anonymous functions, but generic factory usually requires name?
-- Looking at lparser.c: funcstat -> funcname -> body
-- body -> if (ls->t.token == '(') ...
-- So anonymous function can be generic too?
-- function(T) (args) ... end

local GenAnon = function(T)(val) return val end
local GStr = GenAnon(string)
assert(GStr("s") == "s")

print("Generic Conflict Resolution Tests Passed!")
