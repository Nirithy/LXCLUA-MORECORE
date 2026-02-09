val = 2
res = 0
switch val do
    case 1: res = res + 1
    default: res = res + 100
    case 2: res = res + 2
end
print("Res: " .. res)
assert(res == 2)
