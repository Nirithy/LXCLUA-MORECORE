local function recursive_fib(n)
  if n < 2 then return n end
  return recursive_fib(n-1) + recursive_fib(n-2)
end
