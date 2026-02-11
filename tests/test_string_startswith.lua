local function assert_eq(a, b, msg)
    if a ~= b then
        error(msg .. ": expected " .. tostring(b) .. ", got " .. tostring(a))
    end
end

local function assert_error(fn, msg)
    local ok, err = pcall(fn)
    if ok then
        error("expected error but got success: " .. (msg or ""))
    end
end

print("Testing string.startswith...")

-- Basic matching
assert_eq(string.startswith("hello", "he"), true, "basic match")
assert_eq(string.startswith("hello", "hello"), true, "full match")
assert_eq(string.startswith("hello", ""), true, "empty prefix")

-- Basic non-matching
assert_eq(string.startswith("hello", "ha"), false, "basic mismatch")
assert_eq(string.startswith("hello", "ello"), false, "match but not at start")

-- Length cases
assert_eq(string.startswith("a", "abc"), false, "prefix longer than string")
assert_eq(string.startswith("", "a"), false, "empty string, non-empty prefix")
assert_eq(string.startswith("", ""), true, "empty string, empty prefix")

-- Case sensitivity
assert_eq(string.startswith("Hello", "he"), false, "case sensitivity 1")
assert_eq(string.startswith("hello", "He"), false, "case sensitivity 2")

-- Embedded zeros
assert_eq(string.startswith("a\0b", "a\0"), true, "embedded zero match")
assert_eq(string.startswith("a\0b", "a\0b"), true, "embedded zero full match")
assert_eq(string.startswith("a\0b", "a\0c"), false, "embedded zero mismatch")

-- Object oriented style
assert_eq(("hello"):startswith("he"), true, "OO style match")
assert_eq(("hello"):startswith("ha"), false, "OO style mismatch")

-- Argument type checking
-- Note: Numbers are automatically converted to strings in Lua's string library functions.
assert_error(function() string.startswith({}, "a") end, "table subject")
assert_error(function() string.startswith("a", {}) end, "table prefix")
assert_error(function() string.startswith(true, "a") end, "boolean subject")
assert_error(function() string.startswith("a", false) end, "boolean prefix")
assert_error(function() string.startswith(nil, "a") end, "nil subject")
assert_error(function() string.startswith("a", nil) end, "nil prefix")

print("All string.startswith tests passed!")
