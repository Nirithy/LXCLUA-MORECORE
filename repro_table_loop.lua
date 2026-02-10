local t = {}
for i = 1, 100 do
    t[i] = i
end
print("Table size:", #t)

local t2 = {a=1, b=2, c=3}
for k,v in pairs(t2) do
    print(k,v)
end
