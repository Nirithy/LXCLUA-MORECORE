local GLR = require("glr")
local lexer = require("lexer")

-- Expanded Lisp Grammar
local lisp_grammar = {
    program = {
        {"expr", "program_tail"},
        {"expr"}
    },
    program_tail = {
        {"program"}
    },
    expr = {
        {"atom"},
        {"list"}
    },
    list = {
        {GLR.terminal_type("'('"), "expr_list", GLR.terminal_type("')'")},
        {GLR.terminal_type("'('"), GLR.terminal_type("')'")}
    },
    expr_list = {
        {"expr", "expr_list"},
        {"expr"}
    },
    atom = {
        {GLR.terminal_type("<identifier>")},
        {GLR.terminal_type("<name>")},
        {GLR.terminal_type("<integer>")},
        {GLR.terminal_type("<string>")},
        {GLR.terminal_type("'+'")},
        {GLR.terminal_type("'-'")},
        {GLR.terminal_type("'*'")},
        {GLR.terminal_type("'/'")},
        {GLR.terminal_type("'%'")},
        {GLR.terminal_type("'=='")},
        {GLR.terminal_type("'~='")},
        {GLR.terminal_type("'<'")},
        {GLR.terminal_type("'>'")},
        {GLR.terminal_type("'<='")},
        {GLR.terminal_type("'>='")},
        {GLR.terminal_type("'='")},
        {GLR.terminal_type("'if'")},
        {GLR.terminal_type("'while'")},
        {GLR.terminal_type("'true'")},
        {GLR.terminal_type("'false'")},
        {GLR.terminal_type("'and'")},
        {GLR.terminal_type("'or'")},
        {GLR.terminal_type("'not'")},
        {GLR.terminal_type("'return'")},
        {GLR.terminal_type("'goto'")},
        {GLR.terminal_type("'::'")},
        {GLR.terminal_type("'function'")},
        {GLR.terminal_type("'lambda'")},
        {GLR.terminal_type("'label'")}
    }
}

local function parse_lisp(code)
    local tokens = lexer(code)
    local parses = GLR.parse(lisp_grammar, "program", tokens)
    if #parses == 0 then
        print("Lisp syntax error. Tokens:")
        for i, t in ipairs(tokens) do print(i, t.type, t.value) end
        error("Syntax error parsing Lisp code")
    end
    return parses[1]
end

-- Environment system for closures
local function make_env(parent)
    return { vars = {}, parent = parent }
end

local function env_set(env, name, val)
    env.vars[name] = val
end

local function env_get(env, name)
    if env.vars[name] ~= nil then
        return env.vars[name]
    elseif env.parent then
        return env_get(env.parent, name)
    else
        return nil
    end
end

-- Evaluator with support for closures, goto, return
local function eval_ast(ast, env, context)
    if not ast then return nil end
    context = context or { returning = false, return_val = nil, goto_label = nil, in_goto = false }

    if ast.type == "program" then
        local last_val = nil
        for _, child in ipairs(ast.children) do
            if child.type == "expr" then
                last_val = eval_ast(child, env, context)
                if context.returning or context.in_goto then return last_val end
            elseif child.type == "program_tail" then
                last_val = eval_ast(child.children[1], env, context)
                if context.returning or context.in_goto then return last_val end
            end
        end
        return last_val
    elseif ast.type == "expr" then
        return eval_ast(ast.children[1], env, context)
    elseif ast.type == "atom" then
        local token = ast.children[1]
        local val = token.value
        local t_type = token.type
        if t_type == "<integer>" then return tonumber(val)
        elseif t_type == "<string>" then return val
        else
            local sym_val = val or t_type:sub(2, -2)
            if sym_val == "true" then return true end
            if sym_val == "false" then return false end
            local e_val = env_get(env, sym_val)
            if e_val ~= nil then return e_val end
            return sym_val
        end
    elseif ast.type == "list" then
        if #ast.children == 2 then return nil end -- empty list ()
        local expr_list = ast.children[2]

        -- Extract operator name for special forms
        local op_node = expr_list.children[1]
        local op_val = nil
        if op_node.type == "expr" and op_node.children[1].type == "atom" then
            local token = op_node.children[1].children[1]
            op_val = token.value or token.type:sub(2, -2)
        end

        -- Special Forms
        if op_val == "setq" or op_val == "set" or op_val == "=" then
            local var_name_ast = expr_list.children[2].children[1].children[1].children[1]
            local var_name = var_name_ast.value or var_name_ast.type:sub(2, -2)
            local val_ast = expr_list.children[2].children[2].children[1]
            local val = eval_ast(val_ast, env, context)
            if context.in_goto then return nil end
            env_set(env, var_name, val)
            return val
        elseif op_val == "if" then
            local cond_ast = expr_list.children[2].children[1]
            local then_ast = expr_list.children[2].children[2].children[1]
            local else_ast = expr_list.children[2].children[2].children[2] and expr_list.children[2].children[2].children[2].children[1] or nil

            local cond = eval_ast(cond_ast, env, context)
            if context.in_goto then return nil end
            if cond then
                return eval_ast(then_ast, env, context)
            else
                if else_ast then
                    return eval_ast(else_ast, env, context)
                end
                return nil
            end
        elseif op_val == "while" then
            local cond_ast = expr_list.children[2].children[1]
            local body_ast = expr_list.children[2].children[2].children[1]
            local last_val = nil
            while eval_ast(cond_ast, env, context) do
                if context.in_goto or context.returning then break end
                last_val = eval_ast(body_ast, env, context)
                if context.returning then break end
            end
            return last_val
        elseif op_val == "progn" or op_val == "do" then
            local last_val = nil
            local start_cur = expr_list.children[2]

            -- Simplified evaluation: we just step through
            local c = start_cur
            while c do
                local expr_ast = c.children[1]

                if context.in_goto then
                    -- Search for label
                    local is_label = false
                    if expr_ast.type == "expr" and expr_ast.children[1].type == "list" then
                        local inner_list = expr_ast.children[1]
                        if #inner_list.children > 2 then
                            local label_op_node = inner_list.children[2].children[1]
                            if label_op_node.type == "expr" and label_op_node.children[1].type == "atom" then
                                local ltok = label_op_node.children[1].children[1]
                                local lop = ltok.value or ltok.type:sub(2, -2)
                                if lop == "label" then
                                    local lname_ast = inner_list.children[2].children[2].children[1].children[1].children[1]
                                    local lname = lname_ast.value or lname_ast.type:sub(2, -2)
                                    if lname == context.goto_label then
                                        context.in_goto = false
                                        context.goto_label = nil
                                        is_label = true
                                    end
                                end
                            end
                        end
                    end
                    if not is_label then
                        c = c.children[2]
                        if not c and context.in_goto then
                            return last_val
                        end
                        -- Instead of 'goto continue', we just iterate the loop
                    else
                        -- We found the label, execute it normally and resume
                        last_val = eval_ast(expr_ast, env, context)
                        c = c.children[2]
                    end
                else
                    -- Normal execution
                    last_val = eval_ast(expr_ast, env, context)
                    if context.returning then return last_val end
                    if context.in_goto then
                        -- A goto was hit. Restart the block scan from top to allow backward goto within progn
                        c = start_cur
                    else
                        c = c.children[2]
                    end
                end
            end
            return last_val
        elseif op_val == "label" then
            return nil
        elseif op_val == "goto" then
            local label_name_ast = expr_list.children[2].children[1].children[1].children[1]
            local label_name = label_name_ast.value or label_name_ast.type:sub(2, -2)
            context.in_goto = true
            context.goto_label = label_name
            return nil
        elseif op_val == "return" then
            local ret_val = nil
            if expr_list.children[2] then
                ret_val = eval_ast(expr_list.children[2].children[1], env, context)
            end
            context.returning = true
            context.return_val = ret_val
            return ret_val
        elseif op_val == "lambda" or op_val == "function" then
            local params_ast = expr_list.children[2].children[1]
            local body_ast = expr_list.children[2].children[2].children[1]

            local params = {}
            if params_ast.type == "expr" and params_ast.children[1].type == "list" then
                local plist = params_ast.children[1]
                if #plist.children > 2 then
                    local pcur = plist.children[2]
                    while pcur do
                        local patom = pcur.children[1].children[1].children[1]
                        table.insert(params, patom.value or patom.type:sub(2, -2))
                        pcur = pcur.children[2]
                    end
                end
            end

            return function(...)
                local call_env = make_env(env)
                local args = {...}
                for i, p in ipairs(params) do
                    env_set(call_env, p, args[i])
                end
                local call_context = { returning = false, return_val = nil, in_goto = false, goto_label = nil }
                local res = eval_ast(body_ast, call_env, call_context)
                if call_context.returning then
                    return call_context.return_val
                end
                return res
            end
        end

        -- Regular Function Call
        local args = {}
        local function collect_args(el)
            local res = eval_ast(el.children[1], env, context)
            table.insert(args, res)
            if #el.children > 1 then
                collect_args(el.children[2])
            end
        end
        collect_args(expr_list)
        if context.in_goto or context.returning then return nil end

        local op = args[1]
        if type(op) == "function" then
            return op(unpack(args, 2))
        end

        local func = env_get(env, op)
        if type(func) == "function" then
            return func(unpack(args, 2))
        end

        if op == nil and #args > 0 then
            return nil
        end

        error("Unknown function or operator: " .. tostring(op))
    end
end

local function run_lisp(code, custom_env)
    local ast = parse_lisp(code)
    local root_env = make_env(nil)

    local default_vars = {
        ["+"] = function(a, b) return a + b end,
        ["-"] = function(a, b) return a - b end,
        ["*"] = function(a, b) return a * b end,
        ["/"] = function(a, b) return a / b end,
        ["%"] = function(a, b) return a % b end,
        ["=="] = function(a, b) return a == b end,
        ["~="] = function(a, b) return a ~= b end,
        ["<"] = function(a, b) return a < b end,
        [">"] = function(a, b) return a > b end,
        ["<="] = function(a, b) return a <= b end,
        [">="] = function(a, b) return a >= b end,
        ["and"] = function(a, b) return a and b end,
        ["or"] = function(a, b) return a or b end,
        ["not"] = function(a) return not a end,
        ["print"] = print
    }

    for k, v in pairs(default_vars) do
        env_set(root_env, k, v)
    end
    if custom_env then
        for k, v in pairs(custom_env) do
            env_set(root_env, k, v)
        end
    end

    return eval_ast(ast, root_env)
end

return {
    parse = parse_lisp,
    run = run_lisp,
    grammar = lisp_grammar,
    env_get = env_get,
    env_set = env_set,
    make_env = make_env
}
