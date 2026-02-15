local function fib(n)
  if n < 2 then return n end
  return fib(n-1) + fib(n-2)
end

local start = os.clock()
local res = fib(35)
local end_time = os.clock()
print(string.format("Result: %d, Time: %.4f", res, end_time - start))
