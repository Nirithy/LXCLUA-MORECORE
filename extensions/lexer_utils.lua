-- extensions/lexer_utils.lua
-- Pure Lua utilities for the LXCLUA lexer/AST

local lexer = require("lexer")

local lexer_utils = {}

--- Extracts the start and end indices of a function block.
-- @param tokens table: The array of tokens returned by lexer()
-- @param start_idx integer: The index where TK_FUNCTION (or 'function' keyword) is located.
-- @return integer, integer: The start and end indices of the function block.
function lexer_utils.get_function_bounds(tokens, start_idx)
    assert(type(tokens) == "table", "Expected a table of tokens")
    assert(type(start_idx) == "number", "Expected a number for start_idx")

    local tk = tokens[start_idx]
    if not tk or (tk.token ~= lexer.TK_FUNCTION and tk.type ~= "'function'") then
        error("Token at start_idx is not a function")
    end

    -- lexer.get_block_bounds takes tokens and a target index (like a token inside the block)
    -- and finds the tightest block bounds surrounding it.
    -- If we pass start_idx + 1 (inside the function), it will return the function bounds.
    -- Or just use lexer.find_match to find the matching 'end' token.
    local end_idx = lexer.find_match(tokens, start_idx)
    return start_idx, end_idx
end

--- Replaces a subsequence of tokens with a new array of tokens.
-- @param tokens table: The original array of tokens. Modifies in place.
-- @param start_idx integer: The start index to remove.
-- @param end_idx integer: The end index to remove.
-- @param new_tokens table: (Optional) The array of tokens to insert.
function lexer_utils.replace_tokens(tokens, start_idx, end_idx, new_tokens)
    assert(type(tokens) == "table", "Expected a table of tokens")
    assert(type(start_idx) == "number", "Expected a number for start_idx")
    assert(type(end_idx) == "number", "Expected a number for end_idx")

    new_tokens = new_tokens or {}

    -- Calculate how many elements to remove
    local count = end_idx - start_idx + 1

    if count > 0 then
        -- We can just use lexer.remove_tokens
        lexer.remove_tokens(tokens, start_idx, count)
    end

    -- Insert new tokens
    if #new_tokens > 0 then
        lexer.insert_tokens(tokens, start_idx, new_tokens)
    end
    return tokens
end

--- Returns a list of all indices matching a given token ID or string type.
-- @param tokens table: The array of tokens
-- @param target any: The integer token ID or the string token type.
-- @return table: Array of matching indices.
function lexer_utils.find_all_tokens(tokens, target)
    assert(type(tokens) == "table", "Expected a table of tokens")
    assert(type(target) == "number" or type(target) == "string", "Expected a number or string for target")

    return lexer.find_tokens(tokens, target)
end

return lexer_utils
