local lexer = require("lexer")
local code = [[
function test()
  print("hello")
end
]]
local tokens = lexer(code)

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

local tree = lexer.build_tree(tokens)
dump_tree(tree)
