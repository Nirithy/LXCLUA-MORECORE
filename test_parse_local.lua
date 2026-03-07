local lexer = require("lexer")
local function test()
    local ast = lexer.build_tree(lexer("local mp = 50"))
    print(#ast.elements)

    local stmts = lexer.split_statements(ast.elements)
    local n, r = lexer.parse_local(stmts[1])
    print(n[1])

    local ast2 = lexer.build_tree(lexer("obj.hp = obj.hp + step"))
    local stmts2 = lexer.split_statements(ast2.elements)
    local n2, r2 = lexer.parse_local(stmts2[1])
    print(n2, r2)
end
test()
