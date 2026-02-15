local start = os.clock()
local x = 0
for i = 1, 100000000 do
  x = x + 1
end
local end_time = os.clock()
print(string.format("Result: %d, Time: %.4f", x, end_time - start))
