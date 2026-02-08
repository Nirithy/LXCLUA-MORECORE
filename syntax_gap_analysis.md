# Syntax Gap Analysis: XCLUA vs Official Lua

This analysis compares the current XCLUA repository with the official Lua repository (targeting Lua 5.4/5.5 master branch) to identify missing syntactic features or "sugar".

## Key Findings

### 1. Missing Prefixed Attributes for `local` Variables
Official Lua 5.4 introduced the ability to place attributes *before* the variable name list in a `local` declaration, applying the attribute to all variables in the list.

*   **Official Lua Syntax:**
    ```lua
    local <const> x, y = 1, 2  -- Both x and y are const
    local <close> f = io.open(...)
    ```

*   **Current XCLUA Syntax:**
    XCLUA's parser (`localstat`) expects the variable name first.
    ```lua
    local x <const>, y <const> = 1, 2
    ```
    Attempting `local <const> x` in XCLUA will likely result in a syntax error because it expects a name immediately after `local`.

    *Note: XCLUA supports `global <const> x`, so this inconsistency is specific to `local`.*

### 2. Implementation Divergences (Not necessarily "Missing")

*   **Multiple `<close>` Variables:**
    *   **Official Lua:** Explicitly forbids multiple to-be-closed variables in a single local statement (e.g., `local <close> x, <close> y` raises an error).
    *   **XCLUA:** Allows multiple `<close>` variables in a single statement. This is an enhancement over the official version.

*   **`const` Keyword:**
    *   **Official Lua:** Does not reserve `const`. It uses `<const>` attribute.
    *   **XCLUA:** Reserves `const` as a keyword, allowing `const x = 1` as syntactic sugar for `local x <const> = 1`.

*   **Internal Loop Structure:**
    *   **Official Lua:** Uses 3 internal state variables for generic `for` loops.
    *   **XCLUA:** Uses 4 internal state variables. This is an internal implementation detail likely related to the custom features or protection mechanisms.

### 3. Synchronized Features
XCLUA has successfully synchronized the following recent/upcoming features from Official Lua:
*   **`global` Keyword:** Fully implemented, including attribute support (`global <const> x`).
*   **Bitwise Operators:** Standard 5.3+ operators (`//`, `<<`, etc.) are present.
*   **Integer Loops:** Standard integer for loop optimizations are present.

## Recommendation
To fully synchronize with standard Lua 5.4 syntax sugar, the `localstat` function in `lparser.c` should be updated to check for an attribute *before* parsing the first variable name, similar to how `globalstat` is implemented.
