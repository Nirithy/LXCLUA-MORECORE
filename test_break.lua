local function test()
    local x = 1
    local function disp()
        switch (x) do
            case 1:
                while true do
                    break
                end
                print("After while")
                x = 2
                break
            case 2:
                print("Case 2")
                break
        end
    end
    disp()
end
test()
