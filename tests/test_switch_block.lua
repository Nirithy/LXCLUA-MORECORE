
-- Basic Switch
print("Testing Basic Switch...")
local val = 1
local res = 0
switch val do
    case 1:
        res = 1
        break
    case 2:
        res = 2
        break
end
assert(res == 1)

val = 2
switch val do
    case 1:
        res = 1
        break
    case 2:
        res = 2
        break
end
assert(res == 2)

-- Fallthrough
print("Testing Fallthrough...")
val = 1
res = 0
switch val do
    case 1:
        res = res + 1
    case 2:
        res = res + 2
    case 3:
        res = res + 3
        break
    default:
        res = res + 100
end
-- 1 falls through to 2, then to 3. (1+2+3 = 6)
assert(res == 6)

-- Break
print("Testing Break...")
val = 1
res = 0
switch val do
    case 1:
        res = 10
        break
    case 2:
        res = 20
end
assert(res == 10)

-- Default
print("Testing Default...")
val = 99
res = 0
switch val do
    case 1:
        res = 1
    default:
        res = 999
end
assert(res == 999)

-- Default in middle with break
print("Testing Default in Middle (with break)...")
val = 50
res = 0
switch val do
    case 1:
        res = 1
    default:
        res = 500
        break
    case 2:
        res = 2
end
assert(res == 500)

-- Default Fallthrough
print("Testing Default Fallthrough...")
val = 50
res = 0
switch val do
    case 1:
        res = 1
    default:
        res = 500
    case 2:
        res = res + 2
end
-- default falls through to case 2? Yes.
assert(res == 502)

-- Default in middle skipping check (Bug Regression Test)
print("Testing Default in Middle Skipping Check...")
val = 2
res = 0
switch val do
    case 1: res = res + 1
    default: res = res + 100
    case 2: res = res + 2
end
-- Should be 2 (matches case 2, skips default).
assert(res == 2)

-- Multiple values
print("Testing Multiple Values...")
val = 2
res = 0
switch val do
    case 1, 2, 3:
        res = 123
        break
    case 4:
        res = 4
end
assert(res == 123)

val = 3
res = 0
switch val do
    case 1, 2, 3:
        res = 123
        break
end
assert(res == 123)

-- Switch Expression
print("Testing Switch Expression...")
local x = switch 2 do
    case 1 -> "one"
    case 2 -> "two"
    case 3 -> "three"
end
assert(x == "two")

-- Switch Expression Fallthrough (Shorthand syntax)
x = switch 5 do
    case 4, 5, 6 -> "4-6"
end
assert(x == "4-6")

-- Implicit Default Nil
x = switch 99 do
    case 1 -> "one"
end
assert(x == nil)

print("All switch tests passed!")
