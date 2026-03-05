local lexer = require "lexer"

-- This is a comprehensive demo showing how to use the lexer's structural
-- primitives to perform complex Control Flow Flattening (CFF) analysis,
-- jump statement refactoring validation, and scope verification.
-- It demonstrates checking whether goto statements correctly jump within
-- their valid scopes, avoiding jumps into nested blocks which is invalid in Lua.

local code = [[
local function demo_control_flow(x)
    -- Valid forward jump in same scope
    goto forward_jump

    local y = 1

    ::forward_jump::

    if x > 0 then
        ::inside_if::
        print("x is positive")

        -- Valid backward jump within the same block
        if x > 10 then
            x = x - 1
            goto inside_if
        end
    end

    -- Invalid jump: attempting to jump into a nested scope (the if block)
    goto inside_if

    do
        ::inside_do::
        -- Valid jump to an outer scope
        goto forward_jump
    end

    -- Invalid jump: jumping into the do-end block
    goto inside_do
end
]]

local tokens = lexer.lex(code)

print("Comprehensive Control Flow and Scope Validation")
print("===============================================")

for i, t in ipairs(tokens) do
    if t.type == "'goto'" then
        local target_label = tokens[i+1].value
        print(string.format("\n[Line %d] Found 'goto %s'", t.line, target_label))

        local label_idx = lexer.find_label(tokens, target_label)
        if not label_idx then
            print("  => ERROR: Target label not found!")
        else
            -- Collect all enclosing scopes for the goto statement
            local goto_scopes = {}
            local curr_g = i
            while curr_g >= 1 do
                local gs, ge = lexer.get_block_bounds(tokens, curr_g)
                table.insert(goto_scopes, {s=gs, e=ge})
                if gs == 1 and ge == #tokens then break end
                curr_g = gs - 1
            end

            -- Collect all enclosing scopes for the target label
            local label_scopes = {}
            local curr_l = label_idx
            while curr_l >= 1 do
                local ls, le = lexer.get_block_bounds(tokens, curr_l)
                table.insert(label_scopes, {s=ls, e=le})
                if ls == 1 and le == #tokens then break end
                curr_l = ls - 1
            end

            local g_imm = goto_scopes[1]
            local l_imm = label_scopes[1]

            print(string.format("  Goto immediate scope:   tokens [%d, %d]", g_imm.s, g_imm.e))
            print(string.format("  Label immediate scope:  tokens [%d, %d]", l_imm.s, l_imm.e))

            -- Validation Logic:
            -- In Lua, you can jump within the same scope, or out of nested scopes
            -- to an outer scope. You CANNOT jump into a nested scope from the outside.
            -- Therefore, the label's immediate scope must be one of the goto's enclosing scopes.

            local is_valid = false
            for _, gs in ipairs(goto_scopes) do
                if gs.s == l_imm.s and gs.e == l_imm.e then
                    is_valid = true
                    break
                end
            end

            if is_valid then
                print("  => VALID: Jump stays within bounds or jumps to an outer scope.")
            else
                print("  => INVALID: Attempting to jump into a nested scope!")
            end
        end
        print(string.rep("-", 47))
    end
end
