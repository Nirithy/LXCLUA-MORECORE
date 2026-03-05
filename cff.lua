local lexer = require("lexer")

local _M = {}

local function flatten_function(func_node)
    local body_tokens = {}
    local params_ended = false
    local func_start = {}

    -- Extract the raw tokens from the function node to use our new C utilities.
    for i, el in ipairs(func_node.elements) do
        local flat_el = lexer.flatten_tree({elements={el}})
        for _, t in ipairs(flat_el) do
            if not params_ended then
                table.insert(func_start, t)
                if t.type == "')'" then
                    params_ended = true
                end
            elseif i == #func_node.elements and t.type == "'end'" then
                -- skip final end, we will inject our own
            else
                table.insert(body_tokens, t)
            end
        end
    end

    -- Let C do the heavy lifting of correctly grouping statements by line and brace level!
    local statements = lexer.split_statements(body_tokens)

    local hoisted_locals = {}
    local transformed_statements = {}

    for _, stmt in ipairs(statements) do
        -- Let C parse the local declarations correctly!
        local names, rest = lexer.parse_local(stmt)
        if names then
            for _, n in ipairs(names) do
                table.insert(hoisted_locals, n)
            end
            if #rest > 0 then
                -- Synthesize a new token array for the assignment: "a, b = 1, 2"
                local new_stmt = {}
                for idx, n in ipairs(names) do
                    table.insert(new_stmt, {token=lexer.TK_NAME, type="<name>", value=n, line=stmt[1].line})
                    if idx < #names then
                        table.insert(new_stmt, {token=string.byte(','), type="','", line=stmt[1].line})
                    end
                end
                for _, r in ipairs(rest) do
                    table.insert(new_stmt, r)
                end
                table.insert(transformed_statements, new_stmt)
            end
        else
            table.insert(transformed_statements, stmt)
        end
    end

    local code_parts = {}
    table.insert(code_parts, lexer.reconstruct(func_start))

    if #hoisted_locals > 0 then
        -- deduplicate locals
        local seen = {}
        local unique_locals = {}
        for _, v in ipairs(hoisted_locals) do
            if not seen[v] then
                seen[v] = true
                table.insert(unique_locals, v)
            end
        end
        table.insert(code_parts, "\n  local " .. table.concat(unique_locals, ", "))
    end

    table.insert(code_parts, "\n  local __cff_state = 1\n  while true do\n    switch __cff_state do\n")

    for i, stmt in ipairs(transformed_statements) do
        local is_return = false
        local is_break = false
        for _, t in ipairs(stmt) do
            if t.type == "'return'" then is_return = true end
            if t.type == "'break'" then is_break = true end
        end

        table.insert(code_parts, "      case " .. i .. ":\n        ")

        -- we also need to add spaces between operators if reconstructing from bare tokens,
        -- but lexer.reconstruct does this mostly well.
        table.insert(code_parts, lexer.reconstruct(stmt) .. "\n")

        if not is_return and not is_break then
            table.insert(code_parts, "        __cff_state = " .. (i + 1) .. "\n        break\n")
        end
    end
    table.insert(code_parts, "      default:\n        return\n    end\n  end\nend\n")

    return table.concat(code_parts)
end

local function walk(node)
    if node.type == "function" then
        return flatten_function(node)
    end
    local out_code = ""
    if not node.elements then
        return lexer.reconstruct({node})
    end

    local skip_next = false
    for i=1, #node.elements do
        if skip_next then
            skip_next = false
        else
            local el = node.elements[i]
            if el.type == "'local'" and node.elements[i+1] and node.elements[i+1].type == "function" then
                out_code = out_code .. "local " .. flatten_function(node.elements[i+1]) .. " "
                skip_next = true
            else
                out_code = out_code .. walk(el) .. " "
            end
        end
    end
    return out_code
end

function _M.obfuscate(code)
    local tokens = lexer(code)
    local tree = lexer.build_tree(tokens)
    return walk(tree)
end

return _M
