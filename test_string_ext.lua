
-- Test string extension functions

local function assert_eq(a, b, msg)
    if a ~= b then
        error(string.format("Assertion failed: %s ~= %s (%s)", tostring(a), tostring(b), msg or ""))
    end
end

local function assert_table_eq(t1, t2, msg)
    if #t1 ~= #t2 then
        error(string.format("Table length mismatch: %d ~= %d (%s)", #t1, #t2, msg or ""))
    end
    for i = 1, #t1 do
        if t1[i] ~= t2[i] then
            error(string.format("Table element mismatch at %d: %s ~= %s (%s)", i, tostring(t1[i]), tostring(t2[i]), msg or ""))
        end
    end
end

print("Testing string extensions...")

-- split
local t = string.split("a,b,c", ",")
assert_table_eq(t, {"a", "b", "c"}, "split normal")

t = string.split("hello", ",")
assert_table_eq(t, {"hello"}, "split no sep")

t = string.split("a,,c", ",")
assert_table_eq(t, {"a", "", "c"}, "split empty part")

-- trim
assert_eq(string.trim("  abc  "), "abc", "trim both")
assert_eq(string.trim("abc"), "abc", "trim none")
assert_eq(string.trim("   "), "", "trim all")
assert_eq(string.ltrim("  abc  "), "abc  ", "ltrim")
assert_eq(string.rtrim("  abc  "), "  abc", "rtrim")

-- startswith/endswith
assert_eq(string.startswith("hello world", "hello"), true, "startswith true")
assert_eq(string.startswith("hello world", "world"), false, "startswith false")
assert_eq(string.endswith("hello world", "world"), true, "endswith true")
assert_eq(string.endswith("hello world", "hello"), false, "endswith false")

-- contains
assert_eq(string.contains("hello world", "lo wo"), true, "contains true")
assert_eq(string.contains("hello world", "z"), false, "contains false")

-- hex/fromhex
local s = "hello"
local h = string.hex(s)
assert_eq(h, "68656c6c6f", "hex")
assert_eq(string.fromhex(h), s, "fromhex")

-- escape
assert_eq(string.escape("a.b"), "a%.b", "escape dot")
assert_eq(string.escape("%"), "%%", "escape percent")

print("All string extension tests passed!")
