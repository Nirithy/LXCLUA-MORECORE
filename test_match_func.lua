local function match(x)
    return string.match(x, "%d+")
end

print(match("hello 123 world"))
