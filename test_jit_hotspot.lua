local function heavy_calc(n)
  local sum = 0
  for i = 1, n do
    sum = sum + i
  end
  return sum
end

local start = os.clock()
print("Running heavy calculation loop...")
print("Result:", heavy_calc(100000000)) -- Increased 10x
print("Time:", os.clock() - start)

local function recursive_fib(n)
  if n < 2 then return n end
  return recursive_fib(n-1) + recursive_fib(n-2)
end

start = os.clock()
print("Running recursive Fibonacci(35)...")
print("Fib(35):", recursive_fib(35))
print("Time:", os.clock() - start)
