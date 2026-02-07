#include "lua.h"
#include "lauxlib.h"
#include <string.h>

/**
 * @file json_parser.c
 * @brief Simple JSON parser for Lua.
 */

/**
 * @brief Converts JSON to a Lua table.
 * Supports strings, numbers, booleans, nested objects, and arrays.
 */
int json_to_lua(lua_State *L, const char *json, size_t len, char *out, size_t outlen) {
  const char *p = json;
  char *q = out;
  const char *end = json + len;

  /* 状态变量 */
  int depth = 0;
  int in_string = 0;
  int escape = 0;
  int in_array = 0;
  int array_index = 1;
  int need_comma = 0;
  int parsing_key = 1; /* 1: 解析键名, 0: 解析值 */
  int after_colon = 0;

  /* 跳过所有空白字符，找到第一个非空白字符 */
  while (p < end && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) {
    p++;
  }

  /* 检查第一个非空白字符是否为{或[ */
  if (p >= end || (*p != '{' && *p != '[')) {
    return 0; /* 不是JSON对象或数组 */
  }

  /* 写入Lua表的开始 */
  if (q + 2 >= out + outlen) return 0;
  *q++ = '{';
  *q++ = '\n';
  depth = 1;
  p++;
  
  /* 如果是数组，初始化数组相关状态 */
  if (p[-1] == '[') {
    in_array = 1;
    array_index = 1;
    parsing_key = 0;
    after_colon = 0;
  }

  /* 确保有足够的空间写入，每次循环检查 */
  while (p < end && depth > 0 && (q + 32) < out + outlen) {
    /* 跳过空白字符 */
    while (p < end && !in_string && !escape && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) {
      p++;
    }

    if (escape) {
      /* 处理转义字符 */
      *q++ = *p++;
      escape = 0;
      continue;
    }

    if (in_string) {
      /* 处理字符串内容 */
      if (*p == '\\') {
        escape = 1;
        *q++ = *p++;
      } else if (*p == '"') {
        /* 字符串结束 */
        *q++ = *p++;
        in_string = 0;
        
        if (parsing_key) {
          /* 键名结束，写入键值分隔符 */
          *q++ = ']';
          *q++ = '\t';
          *q++ = '\t';
          *q++ = '\t';
          *q++ = '=';
          *q++ = ' ';
          parsing_key = 0;
          after_colon = 1;
        } else {
          /* 值结束 */
          parsing_key = 1;
          need_comma = 1;
          after_colon = 0;
        }
      } else {
        /* 普通字符 */
        *q++ = *p++;
      }
    } else {
      /* 不在字符串中 */
      switch (*p) {
        case '{': {
          /* 对象开始 */
          if (need_comma) {
            *q++ = ',';
            *q++ = '\n';
            need_comma = 0;
          }
          if (in_array) {
            /* 写入数组索引 */
            for (int i = 0; i < depth; i++) {
              *q++ = '\t';
            }
            *q++ = '[';
            /* 使用sprintf将数组索引转换为字符串，支持任意大小的数字 */
          char idx_str[32];
          int idx_len = sprintf(idx_str, "%d", array_index);
          memcpy(q, idx_str, idx_len);
          q += idx_len;
            *q++ = ']';
            *q++ = ' ';
            *q++ = '=';
            *q++ = ' ';
            array_index++;
          } else if (after_colon) {
            /* 写入缩进 */
            for (int i = 0; i < depth; i++) {
              *q++ = '\t';
            }
          }
          *q++ = '{';
          *q++ = '\n';
          depth++;
          parsing_key = 1;
          after_colon = 0;
          p++;
          break;
        }
        case '}': {
          /* 对象结束 */
          depth--;
          p++;
          *q++ = '\n';
          /* 写入缩进 */
          for (int i = 0; i < depth; i++) {
            *q++ = '\t';
          }
          *q++ = '}';
          need_comma = 1;
          parsing_key = 1;
          after_colon = 0;
          break;
        }
        case '[': {
          /* 数组开始 */
          if (need_comma) {
            *q++ = ',';
            *q++ = '\n';
            need_comma = 0;
          }
          if (in_array) {
            /* 写入数组索引 */
            for (int i = 0; i < depth; i++) {
              *q++ = '\t';
            }
            *q++ = '[';
            /* 使用sprintf将数组索引转换为字符串，支持任意大小的数字 */
            char idx_str[32];
            int idx_len = sprintf(idx_str, "%d", array_index);
            memcpy(q, idx_str, idx_len);
            q += idx_len;
            *q++ = ']';
            *q++ = ' ';
            *q++ = '=';
            *q++ = ' ';
            array_index++;
          } else if (after_colon) {
            /* 写入缩进 */
            for (int i = 0; i < depth; i++) {
              *q++ = '\t';
            }
          }
          *q++ = '{';
          *q++ = '\n';
          depth++;
          in_array = 1;
          array_index = 1;
          parsing_key = 0;
          after_colon = 0;
          p++;
          break;
        }
        case ']': {
          /* 数组结束 */
          depth--;
          p++;
          *q++ = '\n';
          /* 写入缩进 */
          for (int i = 0; i < depth; i++) {
            *q++ = '\t';
          }
          *q++ = '}';
          in_array = 0;
          need_comma = 1;
          parsing_key = 1;
          after_colon = 0;
          break;
        }
        case '"': {
          /* 字符串开始 */
          if (need_comma) {
            *q++ = ',';
            *q++ = '\n';
            need_comma = 0;
          }
          if (in_array) {
            /* 写入数组索引 */
            for (int i = 0; i < depth; i++) {
              *q++ = '\t';
            }
            *q++ = '[';
            /* 使用sprintf将数组索引转换为字符串，支持任意大小的数字 */
            char idx_str[32];
            int idx_len = sprintf(idx_str, "%d", array_index);
            memcpy(q, idx_str, idx_len);
            q += idx_len;
            *q++ = ']';
            *q++ = ' ';
            *q++ = '=';
            *q++ = ' ';
            array_index++;
          } else if (parsing_key) {
            /* 写入缩进 */
            for (int i = 0; i < depth; i++) {
              *q++ = '\t';
            }
            *q++ = '[';
          } else if (after_colon) {
            /* 写入缩进 */
            for (int i = 0; i < depth; i++) {
              *q++ = '\t';
            }
          }
          *q++ = *p++;
          in_string = 1;
          break;
        }
        case ':': {
          /* 冒号，不需要处理，因为我们在字符串结束时已经处理了 */
          p++;
          break;
        }
        case ',': {
          /* 逗号，准备下一个键值对或数组元素 */
          p++;
          if (in_array) {
            parsing_key = 0;
          } else {
            parsing_key = 1;
          }
          after_colon = 0;
          break;
        }
        case 't': {
          /* true值 */
          if (need_comma) {
            *q++ = ',';
            *q++ = '\n';
            need_comma = 0;
          }
          if (in_array) {
            /* 写入数组索引 */
            for (int i = 0; i < depth; i++) {
              *q++ = '\t';
            }
            *q++ = '[';
            /* 使用sprintf将数组索引转换为字符串，支持任意大小的数字 */
            char idx_str[32];
            int idx_len = sprintf(idx_str, "%d", array_index);
            memcpy(q, idx_str, idx_len);
            q += idx_len;
            *q++ = ']';
            *q++ = ' ';
            *q++ = '=';
            *q++ = ' ';
            array_index++;
          } else if (after_colon) {
            /* 写入缩进 */
            for (int i = 0; i < depth; i++) {
              *q++ = '\t';
            }
          }
          memcpy(q, "true", 4);
          q += 4;
          p += 4;
          need_comma = 1;
          parsing_key = 1;
          after_colon = 0;
          break;
        }
        case 'f': {
          /* false值 */
          if (need_comma) {
            *q++ = ',';
            *q++ = '\n';
            need_comma = 0;
          }
          if (in_array) {
            /* 写入数组索引 */
            for (int i = 0; i < depth; i++) {
              *q++ = '\t';
            }
            *q++ = '[';
            /* 使用sprintf将数组索引转换为字符串，支持任意大小的数字 */
            char idx_str[32];
            int idx_len = sprintf(idx_str, "%d", array_index);
            memcpy(q, idx_str, idx_len);
            q += idx_len;
            *q++ = ']';
            *q++ = ' ';
            *q++ = '=';
            *q++ = ' ';
            array_index++;
          } else if (after_colon) {
            /* 写入缩进 */
            for (int i = 0; i < depth; i++) {
              *q++ = '\t';
            }
          }
          memcpy(q, "false", 5);
          q += 5;
          p += 5;
          need_comma = 1;
          parsing_key = 1;
          after_colon = 0;
          break;
        }
        default: {
          /* 数字值 */
          if (*p >= '0' && *p <= '9') {
            if (need_comma) {
              *q++ = ',';
              *q++ = '\n';
              need_comma = 0;
            }
            if (in_array) {
              /* 写入数组索引 */
              for (int i = 0; i < depth; i++) {
                *q++ = '\t';
              }
              *q++ = '[';
              /* 使用sprintf将数组索引转换为字符串，支持任意大小的数字 */
            char idx_str[32];
            int idx_len = sprintf(idx_str, "%d", array_index);
            memcpy(q, idx_str, idx_len);
            q += idx_len;
              *q++ = ']';
              *q++ = ' ';
              *q++ = '=';
              *q++ = ' ';
              array_index++;
            } else if (after_colon) {
              /* 写入缩进 */
              for (int i = 0; i < depth; i++) {
                *q++ = '\t';
              }
            }
            /* 复制数字 */
            while (p < end && (*p >= '0' && *p <= '9' || *p == '.' || *p == '-' || *p == 'e' || *p == 'E')) {
              *q++ = *p++;
            }
            need_comma = 1;
            parsing_key = 1;
            after_colon = 0;
          } else {
            /* 跳过其他字符 */
            p++;
          }
          break;
        }
      }
    }
  }

  /* 检查是否成功解析 */
  if (depth != 0) {
    return 0; /* 解析失败 */
  }

  /* 只写入分号，不需要再写入结束符 */
  if ((q + 2) < out + outlen) {
    *q++ = ';';
    *q = '\0';
    return 1;
  } else {
    return 0; /* 缓冲区不足 */
  }
}