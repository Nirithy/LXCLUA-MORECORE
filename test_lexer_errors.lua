local lexer = require("lexer")

local function test_err(name, func)
    local ok, err = pcall(func)
    if not ok then
        print(name .. " failed correctly: " .. tostring(err))
    else
        print(name .. " unexpectedly succeeded")
    end
end

test_err("reconstruct with non-table token", function()
    lexer.reconstruct({ "not a token table" })
end)

test_err("reconstruct with missing token field", function()
    lexer.reconstruct({ { value = "hello" } })
end)

test_err("build_tree with non-table token", function()
    lexer.build_tree({ "not a token table" })
end)

test_err("find_match with missing token field", function()
    lexer.find_match({ { token = lexer.TK_IF }, { value = "hello" }, { token = lexer.TK_END } }, 1)
end)
