function vfunc(...args)
    local output = {}
    for i, arg in ipairs(args) do
        table.insert(output, arg)
    end
    return output
end

local res = vfunc("Hello", "World", 123)
if res[1] ~= "Hello" then error("Named Varargs failed: " .. tostring(res[1])) end
if res[2] ~= "World" then error("Named Varargs failed: " .. tostring(res[2])) end
if res[3] ~= 123 then error("Named Varargs failed: " .. tostring(res[3])) end
print("Named Varargs: Passed")
