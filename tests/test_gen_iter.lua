local t = { 1, 2, 3, "hello", "world" }
local output = {}
for key, value in t do
    table.insert(output, key .. "=" .. value)
end

local expected = { "1=1", "2=2", "3=3", "4=hello", "5=world" }
-- Since iteration order is not guaranteed for tables in general, but for array part it is sequential.
-- However, generalized iteration uses `next`, which iterates keys.
-- Array keys are 1, 2, 3, 4, 5. `next` usually iterates array part in order in Lua implementation, but let's verify.
-- Actually, `next` order is undefined.
-- So we should check if all items are present.

local found = {}
for _, v in ipairs(output) do
    found[v] = true
end

for _, v in ipairs(expected) do
    if not found[v] then
        error("Generalized Iteration failed: missing " .. v)
    end
end
print("Generalized Iteration: Passed")
