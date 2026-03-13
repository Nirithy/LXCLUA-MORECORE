1. **Define the new token in `llex.h`**
   - Add `TK_NULLCOALEQ` to the `RESERVED` enum. It should be placed together with other compound assignment operators, or at the end of the other terminal symbols before `<number>`.

2. **Update the lexer to recognize `??=` in `llex.c`**
   - In `llex`, around line 1170, inside the `case '?':` switch block, after checking for `?.` and `??`, modify the check for `??` to see if there is an `=`.
   - Update:
     ```c
     else if (check_next1(ls, '?')) {
        if (check_next1(ls, '=')) return TK_NULLCOALEQ; /* '??=' 空值合并赋值运算符 */
        else return TK_NULLCOAL;  /* '??' 空值合并运算符 */
     }
     ```

3. **Update `getcompoundop` in `lparser.c`**
   - Around line 11484 in `lparser.c`, add `case TK_NULLCOALEQ: return OPR_NULLCOAL;` so the parser recognizes it as a compound assignment operator.

4. **Update `operatorstat` and `txtToken` related debug arrays (optional, but good for completeness)**
   - Add it to `luaX_tokens` array in `llex.c`.
   - Consider updating `operatorstat` and `$$` macro logic if they support stringification of the operator.
