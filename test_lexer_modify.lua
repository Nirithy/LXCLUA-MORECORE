local lexer = require("lexer")
local code = [[
local a = 1
local b = 2
print(a+b)
]]
local tokens = lexer(code)

local function dump_tree(node, indent)
    indent = indent or ""
    print(indent .. "Node type: " .. tostring(node.type) .. " elements: " .. tostring(#node.elements))
    for i, el in ipairs(node.elements) do
        if el.token then
            print(indent .. "  Token: " .. el.type .. " val: " .. tostring(el.value))
        else
            dump_tree(el, indent .. "  ")
        end
    end
end

local tree = lexer.build_tree(tokens)
dump_tree(tree)
