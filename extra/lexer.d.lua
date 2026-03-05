---@meta lexer
---@class lexer
---@field TK_ADDEQ integer
---@field TK_AND integer
---@field TK_ARROW integer
---@field TK_ASM integer
---@field TK_ASYNC integer
---@field TK_AWAIT integer
---@field TK_BANDEQ integer
---@field TK_BOOL integer
---@field TK_BOREQ integer
---@field TK_BREAK integer
---@field TK_BXOREQ integer
---@field TK_CASE integer
---@field TK_CATCH integer
---@field TK_CHAR integer
---@field TK_COMMAND integer
---@field TK_CONCAT integer
---@field TK_CONCATEQ integer
---@field TK_CONCEPT integer
---@field TK_CONST integer
---@field TK_CONTINUE integer
---@field TK_DBCOLON integer
---@field TK_DEFAULT integer
---@field TK_DEFER integer
---@field TK_DIVEQ integer
---@field TK_DO integer
---@field TK_DOLLAR integer
---@field TK_DOLLDOLL integer
---@field TK_DOTS integer
---@field TK_DOUBLE integer
---@field TK_ELSE integer
---@field TK_ELSEIF integer
---@field TK_END integer
---@field TK_ENUM integer
---@field TK_EOS integer
---@field TK_EQ integer
---@field TK_EXPORT integer
---@field TK_FALSE integer
---@field TK_FINALLY integer
---@field TK_FLT integer
---@field TK_FOR integer
---@field TK_FUNCTION integer
---@field TK_GE integer
---@field TK_GLOBAL integer
---@field TK_GOTO integer
---@field TK_IDIV integer
---@field TK_IDIVEQ integer
---@field TK_IF integer
---@field TK_IN integer
---@field TK_INT integer
---@field TK_INTERPSTRING integer
---@field TK_IS integer
---@field TK_KEYWORD integer
---@field TK_LAMBDA integer
---@field TK_LE integer
---@field TK_LET integer
---@field TK_LOCAL integer
---@field TK_LONG integer
---@field TK_MEAN integer
---@field TK_MODEQ integer
---@field TK_MULEQ integer
---@field TK_NAME integer
---@field TK_NAMESPACE integer
---@field TK_NE integer
---@field TK_NIL integer
---@field TK_NOT integer
---@field TK_NULLCOAL integer
---@field TK_OPERATOR integer
---@field TK_OPTCHAIN integer
---@field TK_OR integer
---@field TK_PIPE integer
---@field TK_PLUSPLUS integer
---@field TK_RAWSTRING integer
---@field TK_REPEAT integer
---@field TK_REQUIRES integer
---@field TK_RETURN integer
---@field TK_REVPIPE integer
---@field TK_SAFEPIPE integer
---@field TK_SHL integer
---@field TK_SHLEQ integer
---@field TK_SHR integer
---@field TK_SHREQ integer
---@field TK_SPACESHIP integer
---@field TK_STRING integer
---@field TK_STRUCT integer
---@field TK_SUBEQ integer
---@field TK_SUPERSTRUCT integer
---@field TK_SWITCH integer
---@field TK_TAKE integer
---@field TK_THEN integer
---@field TK_TRUE integer
---@field TK_TRY integer
---@field TK_TYPE_FLOAT integer
---@field TK_TYPE_INT integer
---@field TK_UNTIL integer
---@field TK_USING integer
---@field TK_VOID integer
---@field TK_WALRUS integer
---@field TK_WHEN integer
---@field TK_WHILE integer
---@field TK_WITH integer
local lexer = {}

---@class TokenInfo
---@field token integer The token ID (e.g., lexer.TK_IF).
---@field type string The string representation of the token type (e.g., "'if'").
---@field line integer The line number where the token occurred.
---@field value? any The semantic value of the token (e.g., for TK_NAME, TK_STRING, TK_INT).

---@class ASTNode
---@field type string The type of the node.
---@field elements (TokenInfo|ASTNode)[] The children of this node, which can be tokens or sub-nodes.

--- Lexes the given code into a flat array of tokens.
---@param code string The Lua source code to lex.
---@return TokenInfo[]
function lexer.lex(code) end

--- Obfuscates the given code.
---@param code string
---@param options? { cff: boolean, bogus_branches: boolean, string_encryption: boolean }
---@return string
function lexer.obfuscate(code, options) end

--- Finds the matching closer (e.g., 'end', '}') for the block opened at start_idx.
---@param tokens TokenInfo[]
---@param start_idx integer
---@return integer? index The index of the matching closing token, or nil.
function lexer.find_match(tokens, start_idx) end

--- Builds a nested Abstract Syntax Tree from a flat list of tokens.
---@param tokens TokenInfo[]
---@return ASTNode
function lexer.build_tree(tokens) end

--- Flattens an AST back into a list of tokens.
---@param tree ASTNode
---@param out? TokenInfo[] Optional array to append to.
---@return TokenInfo[]
function lexer.flatten_tree(tree, out) end

--- Converts a token ID to its string representation.
---@param token_id integer
---@return string?
function lexer.token2str(token_id) end

--- Returns an iterator that yields line, token ID, token type, and semantic value for each token.
---@param code string
---@return fun(): integer, integer, string?, any
function lexer.gmatch(code) end

--- Reconstructs Lua source code from a list of tokens.
---@param tokens TokenInfo[]
---@return string
function lexer.reconstruct(tokens) end

--- Scans the token stream for tokens matching a specific token ID or string type.
---@param tokens TokenInfo[] The token stream.
---@param target integer|string The target token ID or string type.
---@return integer[] indices The indices of the matching tokens.
function lexer.find_tokens(tokens, target) end

--- Safely inserts a single token or an array of tokens at the specified index within the token stream.
---@param tokens TokenInfo[] The token stream (modified in-place).
---@param index integer The index at which to insert.
---@param new_tokens TokenInfo|TokenInfo[] The token(s) to insert.
function lexer.insert_tokens(tokens, index, new_tokens) end

--- Removes a specified number of tokens starting from a given index.
---@param tokens TokenInfo[] The token stream (modified in-place).
---@param index integer The index at which to start removing.
---@param count? integer The number of tokens to remove (defaults to 1).
function lexer.remove_tokens(tokens, index, count) end

return setmetatable(lexer, {
    ---@param self lexer
    ---@param code string
    ---@return TokenInfo[]
    __call = function(self, code) end
})
