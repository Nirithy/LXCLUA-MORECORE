-- Test SuperStruct

print("Creating SuperStruct...")
superstruct MyMeta [
    name : "MyMeta",
    version : 1,
    ["__index"] : function(t, k)
        return "Found in MyMeta: " .. k
    end
]

print("SuperStruct created.")
local ss = MyMeta

-- Test direct access (SuperStruct as a dictionary)
print("Testing direct access...")
local n = ss.name
print("ss.name:", n)
assert(n == "MyMeta")

local v = ss.version
print("ss.version:", v)
assert(v == 1)

-- Test direct mutation
print("Testing mutation...")
ss.new_field = "hello"
print("ss.new_field:", ss.new_field)
assert(ss.new_field == "hello")

ss.version = 2
print("ss.version (updated):", ss.version)
assert(ss.version == 2)

-- Test as metatable
print("Testing as metatable...")
local t = {}
setmetatable(t, ss)

local res = t.missing_key
print("t.missing_key:", res)
assert(res == "Found in MyMeta: missing_key")

print("All SuperStruct tests passed!")
