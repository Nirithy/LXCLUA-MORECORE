print("Running README feature tests...")

-- 1. Continue
print("1. Testing continue...")
local sum = 0
for i = 1, 5 do
    if i == 3 then continue end
    sum = sum + i
end
-- 1+2+4+5 = 12
assert(sum == 12, "continue failed: sum was " .. sum)
print("   continue passed.")

-- 2. Walrus Operator (:=)
print("2. Testing walrus operator...")
local x
if (x := 100) > 50 then
    assert(x == 100, "walrus assignment failed")
end
assert(x == 100, "walrus scope failed")
print("   walrus passed.")

-- 3. With Statement (C-style block syntax)
print("3. Testing with statement...")
local obj = { value = 42 }
local result = 0
with (obj) {
    result = value
}
assert(result == 42, "with statement failed: result was " .. tostring(result))
print("   with passed.")

-- 4. Concept
print("4. Testing concept...")
concept IsPositive(x)
    return x > 0
end
assert(IsPositive(10) == true, "concept positive failed")
assert(IsPositive(-5) == false, "concept negative failed")
print("   concept passed.")

-- 5. Superstruct
print("5. Testing superstruct...")
superstruct Point [
    x: 0,
    y: 0,
    ["move"]: function(self, dx, dy)
        self.x = self.x + dx
        self.y = self.y + dy
    end
]
-- Point is a global variable now
Point.x = 10
Point:move(5, 5)
assert(Point.x == 15, "superstruct logic failed")
print("   superstruct passed.")

-- 6. Export
print("6. Testing export syntax...")
export function myExport() return "ok" end
export local myVal = 10
export struct MyStruct { int x; }
assert(myExport() == "ok", "export function failed")
print("   export syntax passed.")

print("All README feature tests passed!")
