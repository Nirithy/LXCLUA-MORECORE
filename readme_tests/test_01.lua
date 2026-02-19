-- 复合赋值与自增
local a = 10
a += 5          -- a = 15
a++             -- a = 16

-- 三路比较 (spaceship operator)
-- 返回 -1 (小于), 0 (等于), 1 (大于)
local cmp = 10 <=> 20  -- -1

-- 空值合并 (Null Coalescing)
local val = nil
local res = val ?? "default"  -- "default"

-- 可选链 (Optional Chaining)
do
    local config = { server = { port = 8080 } }
    local port = config?.server?.port
end
do
    local config = { server = { port = 8080 } }
    local timeout = config?.client?.timeout
end

-- 管道操作符 (Pipe Operator)
-- x |> f 等价于 f(x)
local result = "hello" |> string.upper |> print  -- HELLO

-- 安全管道 (Safe Pipe)
-- x |?> f 等价于 x and f(x)
local maybe_nil = nil
-- maybe_nil |?> print  -- (什么都不打印)
