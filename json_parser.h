#ifndef JSON_PARSER_H
#define JSON_PARSER_H

#include "lua.h"

/**
 * @brief Converts JSON to a Lua table.
 *
 * @param L Lua state.
 * @param json JSON string.
 * @param len JSON string length.
 * @param out Output buffer.
 * @param outlen Output buffer length.
 * @return 1 on success, 0 on failure.
 */
int json_to_lua(lua_State *L, const char *json, size_t len, char *out, size_t outlen);

#endif /* JSON_PARSER_H */
