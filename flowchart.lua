local ByteCode = require "ByteCode"

local _M = {}

function _M.to_mermaid(func)
    local p = ByteCode.GetProto(func)
    if not p then
        return nil, "Failed to get proto from function"
    end

    local count = ByteCode.GetCodeCount(p)

    local instructions = {}
    local block_starts = { [1] = true }

    -- Pass 1: find basic block boundaries
    for i = 1, count do
        local inst_val = ByteCode.GetCode(p, i)
        local inst = ByteCode.GetInstruction(p, i)
        local op, name = ByteCode.GetOpCode(inst.OP)
        instructions[i] = {op = op, name = name, inst = inst, val = inst_val}

        local is_branch = false
        local target = nil

        if name == "JMP" then
            target = i + 1 + inst.sJ
            is_branch = true
        elseif name == "FORPREP" or name == "TFORPREP" then
            target = i + 1 + inst.Bx
            is_branch = true
        elseif name == "FORLOOP" or name == "TFORLOOP" then
            target = i + 1 - inst.Bx
            is_branch = true
        elseif name:match("^EQ") or name:match("^LT") or name:match("^LE") or name:match("^GT") or name:match("^GE") or name == "TEST" or name == "TESTSET" then
            target = i + 2
            is_branch = true
        elseif name:match("^RETURN") or name == "TAILCALL" then
            is_branch = true
        end

        if target then
            block_starts[target] = true
        end

        if is_branch and i < count then
            block_starts[i + 1] = true
        end
    end

    -- Pass 2: build blocks
    local blocks_in_order = {}
    local current_block = nil
    for i = 1, count do
        if block_starts[i] then
            if current_block then
                current_block.end_pc = i - 1
            end
            current_block = {id = "BB" .. i, start_pc = i, insts = {}, successors = {}}
            table.insert(blocks_in_order, current_block)
        end

        local inst = instructions[i]

        -- Get arguments formatted nicely
        local args = ByteCode.GetArgs(inst.val)
        local args_str = ""
        if args then
            for k, v in pairs(args) do
                if k ~= "OP" then
                    args_str = args_str .. tostring(k) .. "=" .. tostring(v) .. " "
                end
            end
        end

        -- Make instruction human readable
        local line = string.format("%04d %-10s %s", i, inst.name, args_str)

        table.insert(current_block.insts, line)

        -- Last instruction in function
        if i == count then
            current_block.end_pc = count
        end
    end

    -- Pass 3: edges
    for _, block in ipairs(blocks_in_order) do
        local end_pc = block.end_pc
        local inst = instructions[end_pc]
        local name = inst.name

        if name == "JMP" then
            local target = end_pc + 1 + inst.inst.sJ
            table.insert(block.successors, "BB" .. target)
        elseif name == "FORPREP" or name == "TFORPREP" then
            local target = end_pc + 1 + inst.inst.Bx
            table.insert(block.successors, "BB" .. (end_pc + 1))
            table.insert(block.successors, "BB" .. target)
        elseif name == "FORLOOP" or name == "TFORLOOP" then
            local target = end_pc + 1 - inst.inst.Bx
            table.insert(block.successors, "BB" .. (end_pc + 1))
            table.insert(block.successors, "BB" .. target)
        elseif name:match("^EQ") or name:match("^LT") or name:match("^LE") or name:match("^GT") or name:match("^GE") or name == "TEST" or name == "TESTSET" then
            table.insert(block.successors, "BB" .. (end_pc + 1))
            table.insert(block.successors, "BB" .. (end_pc + 2))
        elseif not (name:match("^RETURN") or name == "TAILCALL") then
            if end_pc < count then
                table.insert(block.successors, "BB" .. (end_pc + 1))
            end
        end
    end

    local out = {}
    table.insert(out, "graph TD")
    table.insert(out, "  START([Start]) --> BB1")

    for _, block in ipairs(blocks_in_order) do
        local content = table.concat(block.insts, "<br>")
        -- Escape double quotes in content
        content = content:gsub('"', '\\"')
        table.insert(out, string.format('  %s["%s"]', block.id, content))
        for _, succ in ipairs(block.successors) do
            table.insert(out, string.format("  %s --> %s", block.id, succ))
        end
    end

    return table.concat(out, "\n")
end

return _M