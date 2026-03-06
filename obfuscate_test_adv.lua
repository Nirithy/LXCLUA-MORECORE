local x = 0
while x < 3 do
    print("Loop", x)
    x = x + 1
end

local function add(a, b)
    return a + b
end

local res = add(10, 20)
print("Result of add:", res)

goto skip
print("This should not print")
::skip::
print("Goto success!")
