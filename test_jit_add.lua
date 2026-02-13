local function add(a, b)
  return a + b
end

print("Testing add(10, 20)")
local res = add(10, 20)
print("Result:", res)
if res == 30 then
  print("SUCCESS")
else
  print("FAILURE")
end
