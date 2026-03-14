local function add(a, b) return a + b end
local code = string.dump(add)
local f = load(code)
print("Result: " .. f(10, 20))
