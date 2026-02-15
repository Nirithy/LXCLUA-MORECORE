
local function fib(n)
  if n < 2 then return n end
  return fib(n-1) + fib(n-2)
end

local start = os.clock()
fib(35)
local duration = os.clock() - start
print(string.format("fib(35) took %.4f seconds", duration))
