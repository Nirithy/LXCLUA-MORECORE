local output = {}
for i = 1, 10 do
    if i == 5 then
        goto continue
    end
    table.insert(output, i)
    ::continue::
end

local expected = { 1, 2, 3, 4, 6, 7, 8, 9, 10 }
if #output ~= #expected then error("Continue Label failed: count mismatch") end
for i = 1, #expected do
    if output[i] ~= expected[i] then
        error("Continue Label failed at " .. i .. ": " .. output[i])
    end
end
print("Continue Label: Passed")
