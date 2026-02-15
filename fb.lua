local function fib(n)
    if n <= 1 then
        return n
    end
    return fib(n-1) + fib(n-2)
end

local start = os.clock()
local res = fib(30)
local cost = os.clock() - start

print("递归 fib(30) =", res)
print("耗时:", string.format("%.8f", cost), "秒")


--wneidn