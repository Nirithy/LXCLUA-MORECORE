-- example_struct.lua

-- 1. Simple Struct
struct Point {
  int x;
  int y;
}

print("Created Point struct")

-- 2. Nested Struct
struct Rect {
  Point p1;
  Point p2;
  int color = 0xFF0000;
}

print("Created Rect struct")

-- 3. Array in Struct
struct Polygon {
  Point vertices[10];
  int count;
}

print("Created Polygon struct")

-- 4. Instantiation & Usage
local p = Point()
p.x = 10
p.y = 20
print("Point p:", p.x, p.y)
if p.x ~= 10 or p.y ~= 20 then error("Point access failed") end

local r = Rect()
r.p1.x = 100
r.p1.y = 200
r.p2.x = 300
r.p2.y = 400
print("Rect r.p1:", r.p1.x, r.p1.y)
print("Rect r.color:", string.format("0x%X", r.color))

if r.p1.x ~= 100 then error("Nested struct access failed") end
if r.color ~= 0xFF0000 then error("Default value failed") end

-- 5. Array Usage
local poly = Polygon()
poly.count = 3
poly.vertices[1].x = 1
poly.vertices[1].y = 1
poly.vertices[2].x = 5
poly.vertices[2].y = 1
poly.vertices[3].x = 3
poly.vertices[3].y = 5

print("Polygon v2:", poly.vertices[2].x, poly.vertices[2].y)

if poly.vertices[3].y ~= 5 then error("Array struct access failed") end

-- 6. Generic Struct (Factory)
-- When using generics, the Type parameter T is called as T() to get the default value.
-- For primitives, we can pass a function that returns the default value.
local function IntType() return 0 end

struct Box(T) {
  T value;
}

local IntBox = Box(IntType)
local b = IntBox()
b.value = 123
print("IntBox value:", b.value)

-- 7. Global array usage
-- array() is a global helper
local arr = array("int")[5]
arr[1] = 42
print("Global array[1]:", arr[1])

print("All tests passed!")
