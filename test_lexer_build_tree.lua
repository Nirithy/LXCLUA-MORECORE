local lexer = require("lexer")
local code = [[
if a == 1 then
    print("hello")
end
]]
local tokens = lexer(code)
local tree = lexer.build_tree(tokens)
print("Tree elements: ", #tree.elements)
for i, el in ipairs(tree.elements) do
    print(i, el.type)
end
