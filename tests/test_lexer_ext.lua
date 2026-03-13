local lexer = require("lexer")
local utils = require("extensions.lexer_utils")

print("--- Testing lexer primitives ---")
local code = [[
local a = 10
local b = 20
print(a + b, a - b)
]]

local tokens = lexer.lex(code)

print("\nOriginal Tokens:")
print(lexer.reconstruct(tokens))

-- 1. Test extract_tokens
-- Assuming `local b = 20` is somewhere around tokens 7 to 11
local target_start, target_end = 0, 0
for i, t in ipairs(tokens) do
    if t.type == "<name>" and t.value == "b" and tokens[i-1] and tokens[i-1].type == "'local'" then
        target_start = i - 1
        target_end = i + 2
        break
    end
end

if target_start > 0 then
    local extracted = lexer.extract_tokens(tokens, target_start, target_end)
    print("\nExtracted tokens:")
    print(lexer.reconstruct(extracted))
end

-- 2. Test replace_tokens
local rep_tokens = lexer.lex("local replacement = 99")
lexer.replace_tokens(tokens, target_start, target_end, rep_tokens)

print("\nAfter replacement:")
print(lexer.reconstruct(tokens))


-- 3. Test split_sequence
local split_code = [[ a, b, c = fn(1, 2), 3, {4, 5} ]]
local split_tokens = lexer.lex(split_code)
local parts = lexer.split_sequence(split_tokens, "','")
print("\nSplit sequences (by ','):")
for i, seq in ipairs(parts) do
    print(string.format("  Part %d: %s", i, lexer.reconstruct(seq)))
end

print("\n--- Testing lexer_utils ---")

-- 4. Test generate_opaque_predicate
local predicate_tokens = utils.generate_opaque_predicate()
print("\nGenerated Opaque Predicate Block:")
print(lexer.reconstruct(predicate_tokens))

-- 5. Test encrypt_string
local encrypted = utils.encrypt_string("hello", 0x42)
print("\nEncrypted 'hello' with key 0x42:")
print(encrypted)

-- 6. Test virtualize_globals
local glob_code = [[
local a = 1
print(a)
math.randomseed(os.time())
]]
local glob_tokens = lexer.lex(glob_code)
local virt_tokens = utils.virtualize_globals(glob_tokens, "__M[0]")
print("\nVirtualize Globals:")
print(lexer.reconstruct(virt_tokens))

print("\nAll tests ran successfully!")
