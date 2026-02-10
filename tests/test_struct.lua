local struct_lib = require "struct"

struct Point {
  x = 0.0,
  y = 0.0
}

local p1 = Point()
local x = p1.x
print("x:", x, type(x))
assert(x == 0.0)
assert(p1.y == 0.0)

p1.x = 10
p1.y = 20
assert(p1.x == 10)
assert(p1.y == 20)

local p2 = p1 -- copy
assert(p2.x == 10)
p2.x = 30
assert(p2.x == 30)
assert(p1.x == 10)

assert(isstruct(p1))
assert(isinstance(p1, Point))
assert(type(p1) == "struct")

-- Nested struct
struct Rect {
  origin = Point(),
  width = 0.0,
  height = 0.0
}

local r = Rect()
local o = r.origin
o.x = 50
r.origin = o
local o2 = r.origin
assert(o2.x == 50)

print("All struct tests passed!")
