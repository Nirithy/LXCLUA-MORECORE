local lexer = require("lexer")
local code = [[
if a == 1 then
    print("hello")
elseif b == 2 then
    print("world")
else
    print("!")
end
]]
local tokens = lexer(code)
local tree = lexer.build_tree(tokens)
local function dump_tree(node, indent)
    indent = indent or ""
    print(indent .. "Node type: " .. tostring(node.type) .. " elements: " .. tostring(node.elements and #node.elements or 0))
    if not node.elements then return end
    for i, el in ipairs(node.elements) do
        if el.token then
            print(indent .. "  Token: " .. el.type .. " val: " .. tostring(el.value))
        else
            dump_tree(el, indent .. "  ")
        end
    end
end
dump_tree(tree)
