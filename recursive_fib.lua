local function fib(n)
  if n < 2 then return n end
  return fib(n - 1) + fib(n - 2)
end

local start = os.clock()
local res = fib(30)
local dt = os.clock() - start

print("递归 fib(30) = ", res)
print(string.format("耗时: \t%.8f \t秒", dt))
