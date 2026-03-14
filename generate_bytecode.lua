local dump_code = string.dump(function()
    print("New Segmented Architecture")
    local function inner()
        print("Inner Function 1")
    end
    local function inner2()
        print("Inner Function 2")
    end
    inner()
    inner2()
end)

local file = io.open("test.luac", "wb")
file:write(dump_code)
file:close()
print("Bytecode generated to test.luac")
