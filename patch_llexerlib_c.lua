local f = io.open("llexerlib.c", "r")
local content = f:read("*a")
f:close()

local c_code = [[
static int lexer_find_match(lua_State *L) {
    luaL_checktype(L, 1, LUA_TTABLE);
    int start_idx = luaL_checkinteger(L, 2);
    int num_tokens = luaL_len(L, 1);

    if (start_idx < 1 || start_idx > num_tokens) {
        lua_pushnil(L);
        return 1;
    }

    lua_rawgeti(L, 1, start_idx);
    if (!lua_istable(L, -1)) {
        lua_pop(L, 1);
        lua_pushnil(L);
        return 1;
    }
    lua_getfield(L, -1, "token");
    int start_tk = lua_tointeger(L, -1);
    lua_pop(L, 2);

    int target_tk = -1;
    int is_block = 0;

    if (start_tk == TK_WHILE || start_tk == TK_FOR) {
        for (int i = start_idx + 1; i <= num_tokens; i++) {
            lua_rawgeti(L, 1, i);
            if (lua_istable(L, -1)) {
                lua_getfield(L, -1, "token");
                int tk = lua_tointeger(L, -1);
                lua_pop(L, 1);
                if (tk == TK_DO) {
                    start_idx = i;
                    start_tk = TK_DO;
                    lua_pop(L, 1);
                    break;
                }
            }
            lua_pop(L, 1);
        }
    }

    if (start_tk == TK_IF || start_tk == TK_FUNCTION || start_tk == TK_DO || start_tk == TK_SWITCH || start_tk == TK_TRY) {
        target_tk = TK_END;
        is_block = 1;
    } else if (start_tk == TK_REPEAT) {
        target_tk = TK_UNTIL;
        is_block = 1;
    } else if (start_tk == '(') {
        target_tk = ')';
    } else if (start_tk == '{') {
        target_tk = '}';
    } else if (start_tk == '[') {
        target_tk = ']';
    } else {
        lua_pushnil(L);
        return 1;
    }

    int depth = 1;
    for (int i = start_idx + 1; i <= num_tokens; i++) {
        lua_rawgeti(L, 1, i);
        if (lua_istable(L, -1)) {
            lua_getfield(L, -1, "token");
            int tk = lua_tointeger(L, -1);
            lua_pop(L, 1);

            if (is_block) {
                if (tk == TK_IF || tk == TK_FUNCTION || tk == TK_DO || tk == TK_SWITCH || tk == TK_TRY || tk == TK_REPEAT) {
                    depth++;
                } else if (tk == TK_END || tk == TK_UNTIL) {
                    depth--;
                }
            } else {
                if (tk == start_tk) {
                    depth++;
                } else if (tk == target_tk) {
                    depth--;
                }
            }

            if (depth == 0) {
                lua_pop(L, 1);
                lua_pushinteger(L, i);
                return 1;
            }
        }
        lua_pop(L, 1);
    }

    lua_pushnil(L);
    return 1;
}

static int lexer_build_tree(lua_State *L) {
    luaL_checktype(L, 1, LUA_TTABLE);
    int num_tokens = luaL_len(L, 1);

    lua_newtable(L); /* root node */
    lua_pushstring(L, "root");
    lua_setfield(L, -2, "type");
    lua_newtable(L);
    lua_setfield(L, -2, "elements");

    lua_newtable(L); /* stack */
    lua_pushvalue(L, -2); /* push root */
    lua_rawseti(L, -2, 1);
    int stack_len = 1;

    for (int i = 1; i <= num_tokens; i++) {
        lua_rawgeti(L, 1, i); /* t */
        if (!lua_istable(L, -1)) {
            lua_pop(L, 1);
            continue;
        }

        lua_getfield(L, -1, "token");
        int tk = lua_tointeger(L, -1);
        lua_pop(L, 1);

        /* current = stack[#stack] */
        lua_rawgeti(L, -2, stack_len);
        lua_getfield(L, -1, "elements");
        int elements_idx = lua_gettop(L);

        int is_closer = (tk == TK_END || tk == TK_UNTIL);
        int is_opener = (tk == TK_FUNCTION || tk == TK_IF || tk == TK_WHILE || tk == TK_FOR || tk == TK_REPEAT || tk == TK_SWITCH || tk == TK_TRY);

        if (tk == TK_DO) {
            /* get current type */
            lua_getfield(L, -2, "type");
            const char *ctype = lua_tostring(L, -1);
            lua_pop(L, 1);

            lua_newtable(L); /* new_node */

            lua_getfield(L, -5, "type"); /* t.type */
            if (lua_isstring(L, -1)) {
                size_t len;
                const char *ttype = lua_tolstring(L, -1, &len);
                if (len >= 2 && ttype[0] == '\'' && ttype[len-1] == '\'') {
                    lua_pushlstring(L, ttype + 1, len - 2);
                } else {
                    lua_pushstring(L, ttype);
                }
            } else {
                lua_pushstring(L, "");
            }
            lua_setfield(L, -3, "type");
            lua_pop(L, 1); /* pop t.type */

            lua_newtable(L); /* new_node.elements */
            lua_pushvalue(L, -6); /* push t */
            lua_rawseti(L, -2, 1);
            lua_setfield(L, -2, "elements");

            /* insert into current.elements */
            int elen = luaL_len(L, elements_idx);
            lua_pushvalue(L, -1);
            lua_rawseti(L, elements_idx, elen + 1);

            /* insert into stack */
            stack_len++;
            lua_pushvalue(L, -1);
            lua_rawseti(L, -6, stack_len);

            lua_pop(L, 1); /* pop new_node */

        } else if (is_closer) {
            /* table.insert(current.elements, t) */
            int elen = luaL_len(L, elements_idx);
            lua_pushvalue(L, -4); /* t */
            lua_rawseti(L, elements_idx, elen + 1);

            if (stack_len > 1) {
                /* table.remove(stack) */
                lua_pushnil(L);
                lua_rawseti(L, -4, stack_len);
                stack_len--;

                lua_getfield(L, -2, "type");
                const char *ctype = lua_tostring(L, -1);
                lua_pop(L, 1);

                if (ctype && strcmp(ctype, "do") == 0 && stack_len > 1) {
                    lua_rawgeti(L, -3, stack_len);
                    lua_getfield(L, -1, "type");
                    const char *ptype = lua_tostring(L, -1);
                    lua_pop(L, 2);

                    if (ptype && (strcmp(ptype, "while") == 0 || strcmp(ptype, "for") == 0)) {
                        lua_pushnil(L);
                        lua_rawseti(L, -4, stack_len);
                        stack_len--;
                    }
                }
            }

        } else if (is_opener) {
            lua_newtable(L); /* new_node */

            lua_getfield(L, -5, "type"); /* t.type */
            if (lua_isstring(L, -1)) {
                size_t len;
                const char *ttype = lua_tolstring(L, -1, &len);
                if (len >= 2 && ttype[0] == '\'' && ttype[len-1] == '\'') {
                    lua_pushlstring(L, ttype + 1, len - 2);
                } else {
                    lua_pushstring(L, ttype);
                }
            } else {
                lua_pushstring(L, "");
            }
            lua_setfield(L, -3, "type");
            lua_pop(L, 1);

            lua_newtable(L); /* new_node.elements */
            lua_pushvalue(L, -6); /* push t */
            lua_rawseti(L, -2, 1);
            lua_setfield(L, -2, "elements");

            /* insert into current.elements */
            int elen = luaL_len(L, elements_idx);
            lua_pushvalue(L, -1);
            lua_rawseti(L, elements_idx, elen + 1);

            /* insert into stack */
            stack_len++;
            lua_pushvalue(L, -1);
            lua_rawseti(L, -6, stack_len);

            lua_pop(L, 1); /* pop new_node */
        } else {
            /* table.insert(current.elements, t) */
            int elen = luaL_len(L, elements_idx);
            lua_pushvalue(L, -4); /* t */
            lua_rawseti(L, elements_idx, elen + 1);
        }

        lua_pop(L, 2); /* pop elements and current */
        lua_pop(L, 1); /* pop t */
    }

    lua_pop(L, 1); /* pop stack */
    return 1; /* root is on top */
}

static void flatten_tree_helper(lua_State *L, int node_idx, int out_idx) {
    lua_getfield(L, node_idx, "elements");
    if (lua_istable(L, -1)) {
        int len = luaL_len(L, -1);
        for (int i = 1; i <= len; i++) {
            lua_rawgeti(L, -1, i);
            int el_idx = lua_gettop(L);
            lua_getfield(L, el_idx, "elements");
            if (lua_istable(L, -1)) {
                lua_pop(L, 1); /* pop elements */
                flatten_tree_helper(L, el_idx, out_idx);
            } else {
                lua_pop(L, 1); /* pop nil */
                int out_len = luaL_len(L, out_idx);
                lua_pushvalue(L, el_idx);
                lua_rawseti(L, out_idx, out_len + 1);
            }
            lua_pop(L, 1); /* pop el */
        }
    }
    lua_pop(L, 1); /* pop elements */
}

static int lexer_flatten_tree(lua_State *L) {
    luaL_checktype(L, 1, LUA_TTABLE);
    if (lua_gettop(L) >= 2 && lua_istable(L, 2)) {
        lua_pushvalue(L, 2);
    } else {
        lua_newtable(L);
    }
    int out_idx = lua_gettop(L);
    flatten_tree_helper(L, 1, out_idx);
    return 1;
}
]]

local replace_func_list = content:find("static const luaL_Reg lexer_lib%[%] = {")
local pre_func_list = content:sub(1, replace_func_list - 1)
local post_func_list = content:sub(replace_func_list)

post_func_list = post_func_list:gsub(
    "static const luaL_Reg lexer_lib%[%] = {",
    "static const luaL_Reg lexer_lib[] = {\n    {\"find_match\", lexer_find_match},\n    {\"build_tree\", lexer_build_tree},\n    {\"flatten_tree\", lexer_flatten_tree},"
)

local new_content = pre_func_list .. c_code .. "\n" .. post_func_list

f = io.open("llexerlib.c", "w")
f:write(new_content)
f:close()
