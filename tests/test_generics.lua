local function assert_eq(a, b, msg)
    if a ~= b then error((msg or "") .. ": " .. tostring(a) .. " ~= " .. tostring(b)) end
end

print("Testing Global Types...")
assert(number == "number", "Global number should be 'number'")
assert(type(string) == "table", "Global string should be table (library)")
assert(type(array) == "table", "Global array should be table")

print("Testing Introspection...")
assert(isgeneric(array) == false)

print("Testing Generic Functions...")
function add(T)(a, b)
    return a + b
end

local add_num = add(number)
assert_eq(add_num(10, 20), 30, "Explicit specialization failed")

-- Testing library type usage
function concat(T)(a, b)
    return a .. b
end
local concat_str = concat(string)
assert_eq(concat_str("hello", "world"), "helloworld", "String library type specialization failed")

local status, err = pcall(function() return add(1, 2) end)
print("Inference call status:", status, err)

print("Testing Arrays...")
local arr_num = array(number)[5]
arr_num[1] = 100
assert_eq(arr_num[1], 100, "Array set/get failed")

local status, err = pcall(function() arr_num[2] = "str" end)
assert(status == false, "Array type check failed")

-- Testing array with library type
local arr_str = array(string)[5]
arr_str[1] = "hello"
assert_eq(arr_str[1], "hello", "Array(string) set/get failed")
local status, err = pcall(function() arr_str[2] = 123 end)
assert(status == false, "Array(string) type check failed")

print("Testing Channels...")
local ch = thread.channel(number)()
local status, err = pcall(function() ch:send("str") end)
assert(status == false, "Channel type check failed")
ch:send(123)
assert_eq(ch:pop(), 123, "Channel send/pop failed")

print("Testing Generic Structs...")
struct Box(T) {
    id = 0
}

local IntBox = Box(number)
local b = IntBox({id = 123})
assert_eq(b.id, 123, "Struct instantiation failed")

-- Testing struct with library type
struct StrBox(T) {
    val = ""
}
local SBox = StrBox(string)
local s = SBox({val = "test"})
assert_eq(s.val, "test", "Struct(string) instantiation failed")

print("All tests passed!")
