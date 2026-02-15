local function add(a, b)
    return a + b
end

local function test()
    local sum = 0
    for i = 1, 1000 do
        sum = sum + add(i, i)
    end
    return sum
end

print("Testing JIT return values...")
local result = test()
print("Result: " .. result)
if result == 1000 * 1001 then
    print("SUCCESS")
else
    print("FAILURE: Expected " .. (1000 * 1001) .. ", got " .. result)
end
