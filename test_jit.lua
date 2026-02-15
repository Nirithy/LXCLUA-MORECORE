local function hot_loop()
    local sum = 0
    for i = 1, 10000 do
        sum = sum + i
    end
    return sum
end

print("Starting JIT test...")
local start = os.clock()
local result = hot_loop()
local elapsed = os.clock() - start
print("Result: " .. result)
print("Time: " .. elapsed)
if result == 50005000 then
    print("SUCCESS")
else
    print("FAILURE")
end
