local function test(n)
  return n + 1
end
local start = os.clock()
for i=1, 100000 do
  test(i)
end
print("Done")
