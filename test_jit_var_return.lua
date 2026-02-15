local function inner(n)
    if n == 1 then return 1
    elseif n == 2 then return 1, 2
    elseif n == 3 then return 1, 2, 3
    else return end
end

local function outer(n)
    return inner(n)
end

print("Testing JIT variable return values...")

local r1 = outer(1)
print("Return 1: " .. tostring(r1))
if r1 == 1 then print("SUCCESS 1") else print("FAILURE 1") end

local r1, r2 = outer(2)
print("Return 2: " .. tostring(r1) .. ", " .. tostring(r2))
if r1 == 1 and r2 == 2 then print("SUCCESS 2") else print("FAILURE 2") end

local r1, r2, r3 = outer(3)
print("Return 3: " .. tostring(r1) .. ", " .. tostring(r2) .. ", " .. tostring(r3))
if r1 == 1 and r2 == 2 and r3 == 3 then print("SUCCESS 3") else print("FAILURE 3") end

-- Force JIT compilation of outer
for i=1, 300 do
    outer(1)
end

print("Running JIT compiled outer...")
local r1 = outer(1)
if r1 == 1 then print("SUCCESS JIT 1") else print("FAILURE JIT 1: " .. tostring(r1)) end

local r1, r2 = outer(2)
if r1 == 1 and r2 == 2 then print("SUCCESS JIT 2") else print("FAILURE JIT 2") end

local r1, r2, r3 = outer(3)
if r1 == 1 and r2 == 2 and r3 == 3 then print("SUCCESS JIT 3") else print("FAILURE JIT 3") end
