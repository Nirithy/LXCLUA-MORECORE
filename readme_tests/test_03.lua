-- 箭头函数 (表达式形式)
local add = (a, b) => a + b
print(add(10, 20))  -- 30

-- 箭头函数 (语句块形式)
local log = (msg) => print("[LOG]: " .. msg)

-- Lambda 表达式
-- local sq = lambda(x) => x * x

-- C 风格强类型函数
-- int sum(int a, int b) {
--     return a + b;
-- }

-- 泛型函数
-- function<T> identity(x)
--     return x
-- end

-- 异步函数
-- async function fetchData(url)
--     local data = await http.get(url)
--     return data
-- end
