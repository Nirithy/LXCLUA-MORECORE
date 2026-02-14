superstruct MyMeta [
    name : "MyMeta"
]

local s1 = MyMeta
local s2 = MyMeta

-- Attempt to add them. Since __add is missing, this should raise an error, NOT crash.
local status, err = pcall(function()
    return s1 + s2
end)

if not status then
    print("Caught expected error: " .. err)
else
    print("Error: Operation succeeded unexpectedly!")
end

-- Attempt to index a missing field
local missing = s1.missing_field
if missing == nil then
    print("Missing field is nil (Correct)")
else
    print("Error: Missing field is not nil")
end

print("SuperStruct missing metamethod test passed!")
