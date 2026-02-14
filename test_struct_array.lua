
local struct_lib = require "struct"

local status, err = pcall(function()
  local f, load_err = load([[
    struct Point {
      int x;
      int y;
    }

    struct Polygon {
      Point points[5];
    }
  ]])

  if not f then error(load_err) end
  f()
end)

if not status then
  print("Compilation failed: " .. tostring(err))
else
  print("Compilation succeeded")
  local poly = Polygon()
  print("Polygon size:", poly.__size)
  if not poly.__size then error("Polygon size is nil") end

  poly.points[1].x = 10
  poly.points[1].y = 20
  print("poly.points[1].x =", poly.points[1].x)

  if poly.points[1].x ~= 10 then error("Mismatch") end
end
