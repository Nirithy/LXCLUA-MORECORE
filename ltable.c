/**
 * @file ltable.c
 * @brief Lua tables (hash) implementation.
 *
 * This file contains the implementation of Lua tables, including
 * hash part, array part, and custom access logging features.
 */

#define ltable_c
#define LUA_CORE

#include "lprefix.h"


/*
** Implementation of tables (aka arrays, objects, or hash tables).
** Tables keep its elements in two parts: an array part and a hash part.
** Non-negative integer keys are all candidates to be kept in the array
** part. The actual size of the array is the largest 'n' such that
** more than half the slots between 1 and n are in use.
** Hash uses a mix of chained scatter table with Brent's variation.
** A main invariant of these tables is that, if an element is not
** in its main position (i.e. the 'original' position that its hash gives
** to it), then the colliding element is in its own main position.
** Hence even when the load factor reaches 100%, performance remains good.
*/

#include <math.h>
#include <limits.h>
#include <string.h>
#include <stdio.h>
#include <time.h>

#include "lua.h"

#include "ldebug.h"
#include "ldo.h"
#include "lgc.h"
#include "lmem.h"
#include "lobject.h"
#include "lstate.h"
#include "lstring.h"
#include "ltable.h"
#include "lvm.h"


/*
** Table access interception functionality
*/
static int table_access_enabled = 0;
static FILE *table_access_log = NULL;
static char table_access_log_path[512] = {0};

#define MAX_FILTER_PATTERNS 32
#define MAX_PATTERN_LENGTH 256
#define MAX_DEDUP_ENTRIES 1024

typedef struct {
  char patterns[MAX_FILTER_PATTERNS][MAX_PATTERN_LENGTH];
  int count;
} FilterPatternList;

typedef struct {
  FilterPatternList include_keys;
  FilterPatternList exclude_keys;
  FilterPatternList include_values;
  FilterPatternList exclude_values;
  FilterPatternList include_ops;
  FilterPatternList exclude_ops;
  FilterPatternList include_key_types;
  FilterPatternList exclude_key_types;
  FilterPatternList include_value_types;
  FilterPatternList exclude_value_types;
  int key_min_int;
  int key_max_int;
  int value_min_int;
  int value_max_int;
  int range_enabled;
  int dedup_enabled;
  int show_only_unique;
} TableAccessFilter;

static TableAccessFilter g_filter = {0};
static int g_filter_enabled = 0;
static char g_dedup_entries[MAX_DEDUP_ENTRIES][512];
static int g_dedup_count = 0;
static int g_intelligent_mode_enabled = 0;
static int g_filter_jnienv_enabled = 0;
static int g_filter_userdata_enabled = 0;

/**
 * @brief Sets the intelligent mode for table access logging.
 *
 * @param enabled 1 to enable, 0 to disable.
 */
LUA_API void luaH_set_intelligent_mode(int enabled) {
  g_intelligent_mode_enabled = enabled;
}

/**
 * @brief Checks if intelligent mode is enabled.
 *
 * @return 1 if enabled, 0 otherwise.
 */
LUA_API int luaH_is_intelligent_mode_enabled(void) {
  return g_intelligent_mode_enabled;
}

/**
 * @brief Sets the filter for JNIEnv access logging.
 *
 * @param enabled 1 to enable filtering, 0 to disable.
 */
LUA_API void luaH_set_filter_jnienv(int enabled) {
  g_filter_jnienv_enabled = enabled;
}

/**
 * @brief Checks if JNIEnv filtering is enabled.
 *
 * @return 1 if enabled, 0 otherwise.
 */
LUA_API int luaH_is_filter_jnienv_enabled(void) {
  return g_filter_jnienv_enabled;
}

/**
 * @brief Sets the filter for Userdata access logging.
 *
 * @param enabled 1 to enable filtering, 0 to disable.
 */
LUA_API void luaH_set_filter_userdata(int enabled) {
  g_filter_userdata_enabled = enabled;
}

/**
 * @brief Checks if Userdata filtering is enabled.
 *
 * @return 1 if enabled, 0 otherwise.
 */
LUA_API int luaH_is_filter_userdata_enabled(void) {
  return g_filter_userdata_enabled;
}

static int is_important_access(const char *key_info, const char *value_info) {
  if (!g_intelligent_mode_enabled) return 1;
  
  if (strncmp(key_info, "INTEGER:", 8) == 0) {
    return 0;
  }
  
  if (strstr(key_info, "BOOLEAN:") == key_info) {
    return 0;
  }
  
  if (strncmp(key_info, "NIL:", 4) == 0) {
    return 0;
  }
  
  if (strncmp(value_info, "NIL", 3) == 0) {
    return 0;
  }
  
  if (g_filter_jnienv_enabled && strstr(key_info, "STRING:_JNIEnv") != NULL) {
    return 0;
  }
  
  if (g_filter_userdata_enabled && strstr(value_info, "USERDATA") != NULL) {
    return 0;
  }
  
  return 1;
}

static int get_type_tag_name(int tag, char *buf, size_t buf_size) {
  switch (tag) {
    case 0: snprintf(buf, buf_size, "NIL"); return 1;
    case 1: snprintf(buf, buf_size, "BOOLEAN"); return 1;
    case 2: snprintf(buf, buf_size, "LIGHTUSERDATA"); return 1;
    case 3: snprintf(buf, buf_size, "NUMBER"); return 1;
    case 4: snprintf(buf, buf_size, "STRING"); return 1;
    case 5: snprintf(buf, buf_size, "TABLE"); return 1;
    case 6: snprintf(buf, buf_size, "FUNCTION"); return 1;
    case 7: snprintf(buf, buf_size, "USERDATA"); return 1;
    case 8: snprintf(buf, buf_size, "THREAD"); return 1;
    default: snprintf(buf, buf_size, "TYPE%d", tag); return 1;
  }
}

static int string_matches_patterns(const char *str, FilterPatternList *list) {
  if (list->count == 0) return 1;
  for (int i = 0; i < list->count; i++) {
    if (strstr(str, list->patterns[i]) != NULL) {
      return 1;
    }
  }
  return 0;
}

static int check_numeric_in_range(long long value, int min_val, int max_val, int range_enabled) {
  if (!range_enabled) return 1;
  return (value >= min_val && value <= max_val);
}

static int is_duplicate_entry(const char *entry) {
  if (!g_filter.dedup_enabled && !g_filter.show_only_unique) return 0;
  for (int i = 0; i < g_dedup_count; i++) {
    if (strcmp(g_dedup_entries[i], entry) == 0) {
      return 1;
    }
  }
  if (g_dedup_count < MAX_DEDUP_ENTRIES - 1) {
    strncpy(g_dedup_entries[g_dedup_count], entry, 511);
    g_dedup_entries[g_dedup_count][511] = '\0';
    g_dedup_count++;
  }
  return 0;
}

static int should_log_access(const char *key_info, const char *value_info, 
                             const char *key_type, const char *value_type, 
                             const char *operation) {
  if (!g_filter_enabled) return 1;
  
  if (!is_important_access(key_info, value_info)) return 0;
  
  if (!string_matches_patterns(key_info, &g_filter.include_keys)) return 0;
  if (!string_matches_patterns(key_info, &g_filter.exclude_keys)) return 0;
  if (!string_matches_patterns(value_info, &g_filter.include_values)) return 0;
  if (!string_matches_patterns(value_info, &g_filter.exclude_values)) return 0;
  if (!string_matches_patterns(operation, &g_filter.include_ops)) return 0;
  if (!string_matches_patterns(operation, &g_filter.exclude_ops)) return 0;
  if (!string_matches_patterns(key_type, &g_filter.include_key_types)) return 0;
  if (!string_matches_patterns(key_type, &g_filter.exclude_key_types)) return 0;
  if (!string_matches_patterns(value_type, &g_filter.include_value_types)) return 0;
  if (!string_matches_patterns(value_type, &g_filter.exclude_value_types)) return 0;
  
  return 1;
}

static void add_pattern(FilterPatternList *list, const char *pattern) {
  if (list->count < MAX_FILTER_PATTERNS - 1 && pattern != NULL) {
    size_t len = strlen(pattern);
    if (len < MAX_PATTERN_LENGTH - 1) {
      strncpy(list->patterns[list->count], pattern, MAX_PATTERN_LENGTH - 1);
      list->patterns[list->count][MAX_PATTERN_LENGTH - 1] = '\0';
      list->count++;
    }
  }
}

static void clear_patterns(FilterPatternList *list) {
  list->count = 0;
  for (int i = 0; i < MAX_FILTER_PATTERNS; i++) {
    list->patterns[i][0] = '\0';
  }
}

/**
 * @brief Clears all table access logging filters.
 */
LUA_API void luaH_clear_access_filters(void) {
  clear_patterns(&g_filter.include_keys);
  clear_patterns(&g_filter.exclude_keys);
  clear_patterns(&g_filter.include_values);
  clear_patterns(&g_filter.exclude_values);
  clear_patterns(&g_filter.include_ops);
  clear_patterns(&g_filter.exclude_ops);
  clear_patterns(&g_filter.include_key_types);
  clear_patterns(&g_filter.exclude_key_types);
  clear_patterns(&g_filter.include_value_types);
  clear_patterns(&g_filter.exclude_value_types);
  g_filter.key_min_int = 0;
  g_filter.key_max_int = 0;
  g_filter.value_min_int = 0;
  g_filter.value_max_int = 0;
  g_filter.range_enabled = 0;
  g_filter.dedup_enabled = 0;
  g_filter.show_only_unique = 0;
  g_dedup_count = 0;
}

/**
 * @brief Enables or disables de-duplication for table access logging.
 *
 * @param enabled 1 to enable, 0 to disable.
 */
LUA_API void luaH_set_dedup_enabled(int enabled) {
  g_filter.dedup_enabled = enabled;
}

/**
 * @brief Sets the "show only unique" mode for table access logging.
 *
 * @param enabled 1 to enable, 0 to disable.
 */
LUA_API void luaH_set_show_unique_only(int enabled) {
  g_filter.show_only_unique = enabled;
  g_filter.dedup_enabled = enabled;
}

/**
 * @brief Resets the de-duplication cache.
 */
LUA_API void luaH_reset_dedup_cache(void) {
  g_dedup_count = 0;
}

LUA_API int luaH_add_include_key_type_filter(const char *type) {
  add_pattern(&g_filter.include_key_types, type);
  return g_filter.include_key_types.count;
}

LUA_API int luaH_add_exclude_key_type_filter(const char *type) {
  add_pattern(&g_filter.exclude_key_types, type);
  return g_filter.exclude_key_types.count;
}

LUA_API int luaH_add_include_value_type_filter(const char *type) {
  add_pattern(&g_filter.include_value_types, type);
  return g_filter.include_value_types.count;
}

LUA_API int luaH_add_exclude_value_type_filter(const char *type) {
  add_pattern(&g_filter.exclude_value_types, type);
  return g_filter.exclude_value_types.count;
}

/**
 * @brief Enables or disables table access logging filters globally.
 *
 * @param enabled 1 to enable, 0 to disable.
 */
LUA_API void luaH_set_access_filter_enabled(int enabled) {
  g_filter_enabled = enabled;
}

LUA_API int luaH_add_include_key_filter(const char *pattern) {
  add_pattern(&g_filter.include_keys, pattern);
  return g_filter.include_keys.count;
}

LUA_API int luaH_add_exclude_key_filter(const char *pattern) {
  add_pattern(&g_filter.exclude_keys, pattern);
  return g_filter.exclude_keys.count;
}

LUA_API int luaH_add_include_value_filter(const char *pattern) {
  add_pattern(&g_filter.include_values, pattern);
  return g_filter.include_values.count;
}

LUA_API int luaH_add_exclude_value_filter(const char *pattern) {
  add_pattern(&g_filter.exclude_values, pattern);
  return g_filter.exclude_values.count;
}

LUA_API int luaH_add_include_op_filter(const char *pattern) {
  add_pattern(&g_filter.include_ops, pattern);
  return g_filter.include_ops.count;
}

LUA_API int luaH_add_exclude_op_filter(const char *pattern) {
  add_pattern(&g_filter.exclude_ops, pattern);
  return g_filter.exclude_ops.count;
}

LUA_API void luaH_set_key_int_range(int min_val, int max_val) {
  g_filter.key_min_int = min_val;
  g_filter.key_max_int = max_val;
  g_filter.range_enabled = 1;
}

LUA_API void luaH_set_value_int_range(int min_val, int max_val) {
  g_filter.value_min_int = min_val;
  g_filter.value_max_int = max_val;
  g_filter.range_enabled = 1;
}

static void open_table_access_log(void) {
  if (table_access_log != NULL) {
    fclose(table_access_log);
    table_access_log = NULL;
  }
  
  if (table_access_log_path[0] == '\0') {
    time_t now = time(NULL);
    struct tm *tm_info = localtime(&now);
    char timestamp[64];
    strftime(timestamp, sizeof(timestamp), "%Y%m%d_%H%M%S", tm_info);
    snprintf(table_access_log_path, sizeof(table_access_log_path), 
             "/sdcard/XCLUA/hackv/table_access_%s.log", timestamp);
  }
  
  table_access_log = fopen(table_access_log_path, "a");
}

static void close_table_access_log(void) {
  if (table_access_log != NULL) {
    fclose(table_access_log);
    table_access_log = NULL;
  }
}

static void log_table_access(const char *operation, const char *key_type, 
                             const char *key_value, const char *value_type,
                             const char *value_info) {
  if (table_access_enabled && table_access_log != NULL) {
    char full_key_info[512] = {0};
    char full_value_info[512] = {0};
    snprintf(full_key_info, sizeof(full_key_info), "%s:%s", key_type, key_value);
    snprintf(full_value_info, sizeof(full_value_info), "%s %s", value_type, value_info);
    
    if (should_log_access(full_key_info, full_value_info, key_type, value_type, operation)) {
      time_t now = time(NULL);
      struct tm *tm_info = localtime(&now);
      char time_buf[64];
      strftime(time_buf, sizeof(time_buf), "%Y-%m-%d %H:%M:%S", tm_info);
      
      fprintf(table_access_log, "[%s] [%s] [%s] KEY:%s %s\n", 
              time_buf, operation, key_type, key_value, 
              value_info ? value_info : "");
      fflush(table_access_log);
    }
  }
}

static const char* get_value_type_name(int value_tag) {
  switch (value_tag) {
    case LUA_VSHRSTR:
    case LUA_VLNGSTR:
      return "STRING";
    case LUA_VNUMINT:
      return "INTEGER";
    case LUA_VNUMFLT:
      return "FLOAT";
    case LUA_VFALSE:
    case LUA_VTRUE:
      return "BOOLEAN";
    case LUA_VNIL:
      return "NIL";
    case LUA_VLCL:
    case LUA_VLCF:
    case LUA_VCCL:
      return "FUNCTION";
    case LUA_VTABLE:
      return "TABLE";
    case LUA_VUSERDATA:
      return "USERDATA";
    default:
      return "TYPE";
  }
}

static void log_key_value(const TValue *key, const TValue *value, const char *operation) {
  char key_buf[256] = {0};
  char value_buf[256] = {0};
  char value_type[32] = {0};
  int key_tag = ttypetag(key);
  
  switch (key_tag) {
    case LUA_VSHRSTR:
    case LUA_VLNGSTR:
      snprintf(key_buf, sizeof(key_buf), "STRING:%s", getstr(tsvalue(key)));
      break;
    case LUA_VNUMINT:
      snprintf(key_buf, sizeof(key_buf), "INTEGER:%lld", (long long)ivalue(key));
      break;
    case LUA_VNIL:
      snprintf(key_buf, sizeof(key_buf), "NIL");
      break;
    case LUA_VNUMFLT:
      snprintf(key_buf, sizeof(key_buf), "FLOAT:%.17g", fltvalue(key));
      break;
    case LUA_VFALSE:
      snprintf(key_buf, sizeof(key_buf), "BOOLEAN:false");
      break;
    case LUA_VTRUE:
      snprintf(key_buf, sizeof(key_buf), "BOOLEAN:true");
      break;
    default:
      snprintf(key_buf, sizeof(key_buf), "TYPE:%d", novariant(key_tag));
      break;
  }
  
  if (value != NULL && !isabstkey(value)) {
    int value_tag = ttypetag(value);
    snprintf(value_type, sizeof(value_type), "%s", get_value_type_name(value_tag));
    switch (value_tag) {
      case LUA_VSHRSTR:
      case LUA_VLNGSTR:
        snprintf(value_buf, sizeof(value_buf), "-> VALUE:STRING(%s)", getstr(tsvalue(value)));
        break;
      case LUA_VNUMINT:
        snprintf(value_buf, sizeof(value_buf), "-> VALUE:INTEGER(%lld)", (long long)ivalue(value));
        break;
      case LUA_VNUMFLT:
        snprintf(value_buf, sizeof(value_buf), "-> VALUE:FLOAT(%.17g)", fltvalue(value));
        break;
      case LUA_VFALSE:
        snprintf(value_buf, sizeof(value_buf), "-> VALUE:BOOLEAN(false)");
        break;
      case LUA_VTRUE:
        snprintf(value_buf, sizeof(value_buf), "-> VALUE:BOOLEAN(true)");
        break;
      case LUA_VNIL:
        snprintf(value_buf, sizeof(value_buf), "-> VALUE:NIL");
        break;
      case LUA_VLCL:
      case LUA_VLCF:
      case LUA_VCCL:
        snprintf(value_buf, sizeof(value_buf), "-> VALUE:FUNCTION");
        break;
      case LUA_VTABLE:
        snprintf(value_buf, sizeof(value_buf), "-> VALUE:TABLE");
        break;
      case LUA_VUSERDATA:
        snprintf(value_buf, sizeof(value_buf), "-> VALUE:USERDATA");
        break;
      default:
        snprintf(value_buf, sizeof(value_buf), "-> VALUE:TYPE(%d)", novariant(value_tag));
        break;
    }
  } else {
    snprintf(value_buf, sizeof(value_buf), "-> NOT_FOUND");
    snprintf(value_type, sizeof(value_type), "NOT_FOUND");
  }
  
  log_table_access(operation, "GENERAL", key_buf, value_type, value_buf);
}


/*
** Only hash parts with at least 2^LIMFORLAST have a 'lastfree' field
** that optimizes finding a free slot. That field is stored just before
** the array of nodes, in the same block. Smaller tables do a complete
** search when looking for a free slot.
*/
#define LIMFORLAST    3  /* log2 of real limit (8) */

/*
** The union 'Limbox' stores 'lastfree' and ensures that what follows it
** is properly aligned to store a Node.
*/
typedef struct { Node *dummy; Node follows_pNode; } Limbox_aux;

typedef union {
  Node *lastfree;
  char padding[offsetof(Limbox_aux, follows_pNode)];
} Limbox;

#define haslastfree(t)     ((t)->lsizenode >= LIMFORLAST)
#define getlastfree(t)     ((cast(Limbox *, (t)->node) - 1)->lastfree)


/*
** MAXABITS is the largest integer such that 2^MAXABITS fits in an
** unsigned int.
*/
#define MAXABITS	cast_int(sizeof(int) * CHAR_BIT - 1)
/*
** MAXASIZEB is the maximum number of elements in the array part such
** that the size of the array fits in 'size_t'.
*/
#define MAXASIZEB	(MAX_SIZET/(sizeof(Value) + 1))


/*
** MAXASIZE is the maximum size of the array part. It is the minimum
** between 2^MAXABITS and MAXASIZEB.
*/
#define MAXASIZE  \
    (((1u << MAXABITS) < MAXASIZEB) ? (1u << MAXABITS) : cast_uint(MAXASIZEB))

/*
** MAXHBITS is the largest integer such that 2^MAXHBITS fits in a
** signed int.
*/
#define MAXHBITS	(MAXABITS - 1)


/*
** MAXHSIZE is the maximum size of the hash part. It is the minimum
** between 2^MAXHBITS and the maximum size such that, measured in bytes,
** it fits in a 'size_t'.
*/
#define MAXHSIZE	luaM_limitN(1 << MAXHBITS, Node)


/*
** When the original hash value is good, hashing by a power of 2
** avoids the cost of '%'.
*/
#define hashpow2(t,n)		(gnode(t, lmod((n), sizenode(t))))

/*
** for other types, it is better to avoid modulo by power of 2, as
** they can have many 2 factors.
*/
#define hashmod(t,n)	(gnode(t, ((n) % ((sizenode(t)-1u)|1u))))


#define hashstr(t,str)		hashpow2(t, (str)->hash)
#define hashboolean(t,p)	hashpow2(t, p)


#define hashpointer(t,p)	hashmod(t, point2uint(p))


#define dummynode		(&dummynode_)

/*
** Common hash part for tables with empty hash parts. That allows all
** tables to have a hash part, avoiding an extra check ("is there a hash
** part?") when indexing. Its sole node has an empty value and a key
** (DEADKEY, NULL) that is different from any valid TValue.
*/
static const Node dummynode_ = {
  {{NULL}, LUA_VEMPTY,  /* value's value and type */
   LUA_TDEADKEY, 0, {NULL}}  /* key type, next, and key value */
};


static const TValue absentkey = {ABSTKEYCONSTANT};


/*
** Hash for integers. To allow a good hash, use the remainder operator
** ('%'). If integer fits as a non-negative int, compute an int
** remainder, which is faster. Otherwise, use an unsigned-integer
** remainder, which uses all bits and ensures a non-negative result.
*/
static Node *hashint (const Table *t, lua_Integer i) {
  lua_Unsigned ui = l_castS2U(i);
  if (ui <= cast_uint(INT_MAX))
    return gnode(t, cast_int(ui) % cast_int((sizenode(t)-1) | 1));
  else
    return hashmod(t, ui);
}


/*
** Hash for floating-point numbers.
** The main computation should be just
**     n = frexp(n, &i); return (n * INT_MAX) + i
** but there are some numerical subtleties.
** In a two-complement representation, INT_MAX may not have an exact
** representation as a float, but INT_MIN does; because the absolute
** value of 'frexp' is smaller than 1 (unless 'n' is inf/NaN), the
** absolute value of the product 'frexp * -INT_MIN' is smaller or equal
** to INT_MAX. Next, the use of 'unsigned int' avoids overflows when
** adding 'i'; the use of '~u' (instead of '-u') avoids problems with
** INT_MIN.
*/
#if !defined(l_hashfloat)
static unsigned l_hashfloat (lua_Number n) {
  int i;
  lua_Integer ni;
  n = l_mathop(frexp)(n, &i) * -cast_num(INT_MIN);
  if (!lua_numbertointeger(n, &ni)) {  /* is 'n' inf/-inf/NaN? */
    lua_assert(luai_numisnan(n) || l_mathop(fabs)(n) == cast_num(HUGE_VAL));
    return 0;
  }
  else {  /* normal case */
    unsigned int u = cast_uint(i) + cast_uint(ni);
    return (u <= cast_uint(INT_MAX) ? u : ~u);
  }
}
#endif


/*
** returns the 'main' position of an element in a table (that is,
** the index of its hash value).
*/
static Node *mainpositionTV (const Table *t, const TValue *key) {
  switch (ttypetag(key)) {
    case LUA_VNUMINT: {
      lua_Integer i = ivalue(key);
      return hashint(t, i);
    }
    case LUA_VNUMFLT: {
      lua_Number n = fltvalue(key);
      return hashmod(t, l_hashfloat(n));
    }
    case LUA_VSHRSTR: {
      TString *ts = tsvalue(key);
      return hashstr(t, ts);
    }
    case LUA_VLNGSTR: {
      TString *ts = tsvalue(key);
      return hashpow2(t, luaS_hashlongstr(ts));
    }
    case LUA_VFALSE:
      return hashboolean(t, 0);
    case LUA_VTRUE:
      return hashboolean(t, 1);
    case LUA_VLIGHTUSERDATA: {
      void *p = pvalue(key);
      return hashpointer(t, p);
    }
    case LUA_VLCF: {
      lua_CFunction f = fvalue(key);
      return hashpointer(t, f);
    }
    default: {
      GCObject *o = gcvalue(key);
      return hashpointer(t, o);
    }
  }
}


l_sinline Node *mainpositionfromnode (const Table *t, Node *nd) {
  TValue key;
  getnodekey(cast(lua_State *, NULL), &key, nd);
  return mainpositionTV(t, &key);
}


/*
** Check whether key 'k1' is equal to the key in node 'n2'. This
** equality is raw, so there are no metamethods. Floats with integer
** values have been normalized, so integers cannot be equal to
** floats. It is assumed that 'eqshrstr' is simply pointer equality,
** so that short strings are handled in the default case.  The flag
** 'deadok' means to accept dead keys as equal to their original values.
** (Only collectable objects can produce dead keys.) Note that dead
** long strings are also compared by identity.  Once a key is dead,
** its corresponding value may be collected, and then another value
** can be created with the same address. If this other value is given
** to 'next', 'equalkey' will signal a false positive. In a regular
** traversal, this situation should never happen, as all keys given to
** 'next' came from the table itself, and therefore could not have been
** collected. Outside a regular traversal, we have garbage in, garbage
** out. What is relevant is that this false positive does not break
** anything.  (In particular, 'next' will return some other valid item
** on the table or nil.)
*/
static int equalkey (const TValue *k1, const Node *n2, int deadok) {
  if (rawtt(k1) != keytt(n2)) {  /* not the same variants? */
    if (keyisshrstr(n2) && ttislngstring(k1)) {
      /* an external string can be equal to a short-string key */
      return luaS_eqlngstr(tsvalue(k1), keystrval(n2));
    }
    else if (deadok && keyisdead(n2) && iscollectable(k1)) {
      /* a collectable value can be equal to a dead key */
      return gcvalue(k1) == gcvalueraw(keyval(n2));
   }
   else
     return 0;  /* otherwise, different variants cannot be equal */
  }
  else {  /* equal variants */
    switch (keytt(n2)) {
      case LUA_VNIL: case LUA_VFALSE: case LUA_VTRUE:
        return 1;
      case LUA_VNUMINT:
        return (ivalue(k1) == keyival(n2));
      case LUA_VNUMFLT:
        return luai_numeq(fltvalue(k1), fltvalueraw(keyval(n2)));
      case LUA_VLIGHTUSERDATA:
        return pvalue(k1) == pvalueraw(keyval(n2));
      case LUA_VLCF:
        return fvalue(k1) == fvalueraw(keyval(n2));
      case ctb(LUA_VLNGSTR):
        return luaS_eqlngstr(tsvalue(k1), keystrval(n2));
      default:
        return gcvalue(k1) == gcvalueraw(keyval(n2));
    }
  }
}


/*
** True if value of 'alimit' is equal to the real size of the array
** part of table 't'. (Otherwise, the array part must be larger than
** 'alimit'.)
*/
#define limitequalsasize(t)	(isrealasize(t) || ispow2((t)->alimit))


/**
 * @brief Returns the real size of the 'array' array.
 *
 * @param t The table.
 * @return The real size of the array part.
 */
LUAI_FUNC unsigned int luaH_realasize (const Table *t) {
  if (limitequalsasize(t))
    return t->alimit;  /* this is the size */
  else {
    unsigned int size = t->alimit;
    /* compute the smallest power of 2 not smaller than 'size' */
    size |= (size >> 1);
    size |= (size >> 2);
    size |= (size >> 4);
    size |= (size >> 8);
#if (UINT_MAX >> 14) > 3  /* unsigned int has more than 16 bits */
    size |= (size >> 16);
#if (UINT_MAX >> 30) > 3
    size |= (size >> 32);  /* unsigned int has more than 32 bits */
#endif
#endif
    size++;
    lua_assert(ispow2(size) && size/2 < t->alimit && t->alimit < size);
    return size;
  }
}


/*
** Check whether real size of the array is a power of 2.
** (If it is not, 'alimit' cannot be changed to any other value
** without changing the real size.)
*/
static int ispow2realasize (const Table *t) {
  return (!isrealasize(t) || ispow2(t->alimit));
}


static unsigned int setlimittosize (Table *t) {
  t->alimit = luaH_realasize(t);
  setrealasize(t);
  return t->alimit;
}


#define limitasasize(t)	check_exp(isrealasize(t), t->alimit)



/*
** "Generic" get version. (Not that generic: not valid for integers,
** which may be in array part, nor for floats with integral values.)
** See explanation about 'deadok' in function 'equalkey'.
*/
static const TValue *getgeneric (Table *t, const TValue *key, int deadok) {
  Node *n = mainpositionTV(t, key);
  for (;;) {  /* check whether 'key' is somewhere in the chain */
    if (equalkey(key, n, deadok))
      return gval(n);  /* that's it */
    else {
      int nx = gnext(n);
      if (nx == 0)
        return &absentkey;  /* not found */
      n += nx;
    }
  }
}


/*
** Return the index 'k' (converted to an unsigned) if it is inside
** the range [1, limit].
*/
static unsigned checkrange (lua_Integer k, unsigned limit) {
  return (l_castS2U(k) - 1u < limit) ? cast_uint(k) : 0;
}


static unsigned int arrayindex (lua_Integer k) {
  if (l_castS2U(k) - 1u < MAXASIZE)  /* 'k' in [1, MAXASIZE]? */
    return cast_uint(k);  /* 'key' is an appropriate array index */
  else
    return 0;
}

/*
** Check whether an integer key is in the array part of a table and
** return its index there, or zero.
*/
#define ikeyinarray(t,k)	checkrange(k, t->alimit)


/*
** Check whether a key is in the array part of a table and return its
** index there, or zero.
*/
static unsigned keyinarray (Table *t, const TValue *key) {
  return (ttisinteger(key)) ? ikeyinarray(t, ivalue(key)) : 0;
}


/*
** returns the index of a 'key' for table traversals. First goes all
** elements in the array part, then elements in the hash part. The
** beginning of a traversal is signaled by 0.
*/
static unsigned int findindex (lua_State *L, Table *t, TValue *key,
                               unsigned int asize) {
  unsigned int i;
  if (ttisnil(key)) return 0;  /* first iteration */
  i = ttisinteger(key) ? arrayindex(ivalue(key)) : 0;
  if (i - 1u < asize)  /* is 'key' inside array part? */
    return i;  /* yes; that's the index */
  else {
    const TValue *n = getgeneric(t, key, 1);
    if (l_unlikely(isabstkey(n)))
      luaG_runerror(L, "invalid key to 'next'");  /* key not found */
    i = cast_int(nodefromval(n) - gnode(t, 0));  /* key index in hash table */
    /* hash elements are numbered after array ones */
    return (i + 1) + asize;
  }
}


/**
 * @brief Returns the next key-value pair in a table traversal.
 *
 * @param L The Lua state.
 * @param t The table.
 * @param key The key of the current element (or nil for the beginning).
 * @return 1 if next element is found, 0 otherwise.
 */
int luaH_next (lua_State *L, Table *t, StkId key) {
  unsigned int asize = luaH_realasize(t);
  unsigned int i = findindex(L, t, s2v(key), asize);  /* find original key */
  for (; i < asize; i++) {  /* try first array part */
    if (!isempty(&t->array[i])) {  /* a non-empty entry? */
      setivalue(s2v(key), i + 1);
      setobj2s(L, key + 1, &t->array[i]);
      return 1;
    }
  }
  for (i -= asize; cast_int(i) < sizenode(t); i++) {  /* hash part */
    if (!isempty(gval(gnode(t, i)))) {  /* a non-empty entry? */
      Node *n = gnode(t, i);
      getnodekey(L, s2v(key), n);
      setobj2s(L, key + 1, gval(n));
      return 1;
    }
  }
  return 0;  /* no more elements */
}


/* Extra space in Node array if it has a lastfree entry */
#define extraLastfree(t)	(haslastfree(t) ? sizeof(Limbox) : 0)

/* 'node' size in bytes */
static size_t sizehash (Table *t) {
  return cast_sizet(sizenode(t)) * sizeof(Node) + extraLastfree(t);
}


static void freehash (lua_State *L, Table *t) {
  if (!isdummy(t))
    luaM_freearray(L, t->node, cast_sizet(sizenode(t)));
}


/*
** {==================================================
** Rehash
** ===================================================
*/


/*
** Structure to count the keys in a table.
** 'total' is the total number of keys in the table.
** 'na' is the number of *array indices* in the table (see 'arrayindex').
** 'deleted' is true if there are deleted nodes in the hash part.
** 'nums' is a "count array" where 'nums[i]' is the number of integer
** keys between 2^(i - 1) + 1 and 2^i. Note that 'na' is the summation
** of 'nums'.
*/

/*
** Check whether it is worth to use 'na' array entries instead of 'nh'
** hash nodes. (A hash node uses ~3 times more memory than an array
** entry: Two values plus 'next' versus one value.) Evaluate with size_t
** to avoid overflows.
*/
#define arrayXhash(na,nh)	(cast_sizet(na) <= cast_sizet(nh) * 3)

/*
** Compute the optimal size for the array part of table 't'.
** This size maximizes the number of elements going to the array part
** while satisfying the condition 'arrayXhash' with the use of memory if
** all those elements went to the hash part.
** 'ct->na' enters with the total number of array indices in the table
** and leaves with the number of keys that will go to the array part;
** return the optimal size for the array part.
*/
static unsigned int computesizes (unsigned int nums[], unsigned int *pna) {
  int i;
  unsigned int twotoi;  /* 2^i (candidate for optimal size) */
  unsigned int a = 0;  /* number of elements smaller than 2^i */
  unsigned int na = 0;  /* number of elements to go to array part */
  unsigned int optimal = 0;  /* optimal size for array part */
  /* loop while keys can fill more than half of total size */
  for (i = 0, twotoi = 1;
       twotoi > 0 && *pna > twotoi / 2;
       i++, twotoi *= 2) {
    a += nums[i];
    if (a > twotoi/2) {  /* more than half elements present? */
      optimal = twotoi;  /* optimal size (till now) */
      na = a;  /* all elements up to 'optimal' will go to array part */
    }
  }
  lua_assert((optimal == 0 || optimal / 2 < na) && na <= optimal);
  *pna = na;
  return optimal;
}


static int countint (lua_Integer key, unsigned int *nums) {
  unsigned int k = arrayindex(key);
  if (k != 0) {  /* is 'key' an appropriate array index? */
    nums[luaO_ceillog2(k)]++;  /* count as such */
    return 1;
  }
  else
    return 0;
}
l_sinline int arraykeyisempty (const Table *t, unsigned key) {
  int tag = *getArrTag(t, key - 1);
  return tagisempty(tag);
}

/*
** Count keys in array part of table 't'.
*/
static unsigned int numusearray (const Table *t, unsigned int *nums) {
  int lg;
  unsigned int ttlg;  /* 2^lg */
  unsigned int ause = 0;  /* summation of 'nums' */
  unsigned int i = 1;  /* count to traverse all array keys */
  unsigned int asize = limitasasize(t);  /* real array size */
  /* traverse each slice */
  for (lg = 0, ttlg = 1; lg <= MAXABITS; lg++, ttlg *= 2) {
    unsigned int lc = 0;  /* counter */
    unsigned int lim = ttlg;
    if (lim > asize) {
      lim = asize;  /* adjust upper limit */
      if (i > lim)
        break;  /* no more elements to count */
    }
    /* count elements in range (2^(lg - 1), 2^lg] */
    for (; i <= lim; i++) {
      if (!isempty(&t->array[i-1]))
        lc++;
    }
    nums[lg] += lc;
    ause += lc;
  }
  return ause;
}


/*
** Count keys in hash part of table 't'. As this only happens during
** a rehash, all nodes have been used. A node can have a nil value only
** if it was deleted after being created.
*/
static int numusehash (const Table *t, unsigned int *nums, unsigned int *pna) {
  int totaluse = 0;  /* total number of elements */
  int ause = 0;  /* elements added to 'nums' (can go to array part) */
  int i = sizenode(t);
  while (i--) {
    Node *n = &t->node[i];
    if (!isempty(gval(n))) {
      if (keyisinteger(n))
        ause += countint(keyival(n), nums);
      totaluse++;
    }
  }
  *pna += ause;
  return totaluse;
}


/*
** Creates an array for the hash part of a table with the given
** size, or reuses the dummy node if size is zero.
** The computation for size overflow is in two steps: the first
** comparison ensures that the shift in the second one does not
** overflow.
*/
static void setnodevector (lua_State *L, Table *t, unsigned size) {
  if (size == 0) {
    t->node = cast(Node *, dummynode);
    t->lsizenode = 0;
    t->lastfree = NULL;  // setdummy
  }
  else {
    int i;
    int lsize = luaO_ceillog2(size);

    if (lsize > MAXHBITS || (1u << lsize) > MAXHSIZE)
      luaG_runerror(L, "table overflow");
    size = twoto(lsize);
    t->node = luaM_newvector(L, size, Node);
    for (i = 0; i < cast_int(size); i++) {
      Node *n = gnode(t, i);
      gnext(n) = 0;
      setnilkey(n);
      setempty(gval(n));
    }
    t->lsizenode = cast_byte(lsize);
    t->lastfree = gnode(t, size);
  }
}

/*
** Insert a key in a table where there is space for that key, the
** key is valid, and the value is not nil.
*/


/*
** (Re)insert all elements from the hash part of 'ot' into table 't'.
*/
static void reinserthash (lua_State *L, Table *ot, Table *t) {
  int j;
  int size = sizenode(ot);
  for (j = 0; j < size; j++) {
    Node *old = gnode(ot, j);
    if (!isempty(gval(old))) {
      /* doesn't need barrier/invalidate cache, as entry was
         already present in the table */
      TValue k;
      getnodekey(L, &k, old);
      luaH_set(L, t, &k, gval(old));
    }
  }
}




/*
** Exchange the hash part of 't1' and 't2'.
*/
static void exchangehashpart (Table *t1, Table *t2) {
  lu_byte lsizenode = t1->lsizenode;
  Node *node = t1->node;
  Node *lastfree = t1->lastfree;
  t1->lsizenode = t2->lsizenode;
  t1->node = t2->node;
  t1->lastfree = t2->lastfree;
  t2->lsizenode = lsizenode;
  t2->node = node;
  t2->lastfree = lastfree;
}


/**
 * @brief Resizes a table for new array and hash sizes.
 *
 * @param L The Lua state.
 * @param t The table to resize.
 * @param newasize The new array size.
 * @param nhsize The new hash size.
 */
void luaH_resize (lua_State *L, Table *t, unsigned int newasize,
                                          unsigned int nhsize) {
  unsigned int i;
  Table newt;  /* to keep the new hash part */
  unsigned int oldasize = setlimittosize(t);
  TValue *newarray;
  /* create new hash part with appropriate size into 'newt' */
  setnodevector(L, &newt, nhsize);
  if (newasize < oldasize) {  /* will array shrink? */
    t->alimit = newasize;  /* pretend array has new size... */
    exchangehashpart(t, &newt);  /* and new hash */
    /* re-insert into the new hash the elements from vanishing slice */
    for (i = newasize; i < oldasize; i++) {
      if (!isempty(&t->array[i]))
        luaH_setint(L, t, i + 1, &t->array[i]);
    }
    t->alimit = oldasize;  /* restore current size... */
    exchangehashpart(t, &newt);  /* and hash (in case of errors) */
  }
  /* allocate new array */
  newarray = luaM_reallocvector(L, t->array, oldasize, newasize, TValue);
  if (l_unlikely(newarray == NULL && newasize > 0)) {  /* allocation failed? */
    freehash(L, &newt);  /* release new hash part */
    luaM_error(L);  /* raise error (with array unchanged) */
  }
  /* allocation ok; initialize new part of the array */
  exchangehashpart(t, &newt);  /* 't' has the new hash ('newt' has the old) */
  t->array = newarray;  /* set new array part */
  t->alimit = newasize;
  for (i = oldasize; i < newasize; i++)  /* clear new slice of the array */
     setempty(&t->array[i]);
  /* re-insert elements from old hash part into new parts */
  reinserthash(L, &newt, t);  /* 'newt' now has the old hash */
  freehash(L, &newt);  /* free old hash part */
}


/**
 * @brief Resizes the array part of a table.
 *
 * @param L The Lua state.
 * @param t The table to resize.
 * @param nasize The new array size.
 */
void luaH_resizearray (lua_State *L, Table *t, unsigned int nasize) {
  int nsize = allocsizenode(t);
  luaH_resize(L, t, nasize, nsize);
}

/*
** nums[i] = number of keys 'k' where 2^(i - 1) < k <= 2^i
*/
static void rehash (lua_State *L, Table *t, const TValue *ek) {
  unsigned int asize;  /* optimal size for array part */
  unsigned int na;  /* number of keys in the array part */
  unsigned int nums[MAXABITS + 1];
  int i;
  int totaluse;
  for (i = 0; i <= MAXABITS; i++) nums[i] = 0;  /* reset counts */
  setlimittosize(t);
  na = numusearray(t, nums);  /* count keys in array part */
  totaluse = na;  /* all those keys are integer keys */
  totaluse += numusehash(t, nums, &na);  /* count keys in hash part */
  /* count extra key */
  if (ttisinteger(ek))
    na += countint(ivalue(ek), nums);
  totaluse++;
  /* compute new size for array part */
  asize = computesizes(nums, &na);
  /* resize the table to new computed sizes */
  luaH_resize(L, t, asize, totaluse - na);
}



/*
** }==================================================
*/


/**
 * @brief Creates a new table.
 *
 * @param L The Lua state.
 * @return The new table.
 */
Table *luaH_new (lua_State *L) {
  GCObject *o = luaC_newobj(L, LUA_VTABLE, sizeof(Table));
  Table *t = gco2t(o);
  t->metatable = NULL;
  t->flags = cast_byte(maskflags);  /* table has no metamethod fields */
  t->array = NULL;
  t->alimit = 0;
  t->using_next = NULL;
  l_rwlock_init(&t->lock);
  setnodevector(L, t, 0);
  return t;
}


/**
 * @brief Frees a table.
 *
 * @param L The Lua state.
 * @param t The table to free.
 */
void luaH_free (lua_State *L, Table *t) {
  freehash(L, t);
  luaM_freearray(L, t->array, luaH_realasize(t));
  l_rwlock_destroy(&t->lock);
  luaM_free(L, t);
}


static Node *getfreepos (Table *t) {
  if (!isdummy(t)) {
    while (t->lastfree > t->node) {
      t->lastfree--;
      if (keyisnil(t->lastfree))
        return t->lastfree;
    }
  }
  return NULL;  /* could not find a free place */
}




/*
** Inserts a new key into a hash table; first, check whether key's main
** position is free. If not, check whether colliding node is in its main
** position or not: if it is not, move colliding node to an empty place
** and put new key in its main position; otherwise (colliding node is in
** its main position), new key goes to an empty position. Return 0 if
** could not insert key (could not find a free space).
*/
static void luaH_newkey (lua_State *L, Table *t, const TValue *key,
                                                 TValue *value) {
  Node *mp;
  TValue aux;
  if (l_unlikely(ttisnil(key)))
    luaG_runerror(L, "table index is nil");
  else if (ttisfloat(key)) {
    lua_Number f = fltvalue(key);
    lua_Integer k;
    if (luaV_flttointeger(f, &k, F2Ieq)) {  /* does key fit in an integer? */
      setivalue(&aux, k);
      key = &aux;  /* insert it as an integer */
    }
    else if (l_unlikely(luai_numisnan(f)))
      luaG_runerror(L, "table index is NaN");
  }
  if (ttisnil(value))
    return;  /* do not insert nil values */
  mp = mainpositionTV(t, key);
  if (!isempty(gval(mp)) || isdummy(t)) {  /* main position is taken? */
    Node *othern;
    Node *f = getfreepos(t);  /* get a free place */
    if (f == NULL) {  /* cannot find a free place? */
      rehash(L, t, key);  /* grow table */
      /* whatever called 'newkey' takes care of TM cache */
      luaH_set(L, t, key, value);  /* insert key into grown table */
      return;
    }
    lua_assert(!isdummy(t));
    othern = mainpositionfromnode(t, mp);
    if (othern != mp) {  /* is colliding node out of its main position? */
      /* yes; move colliding node into free position */
      while (othern + gnext(othern) != mp)  /* find previous */
        othern += gnext(othern);
      gnext(othern) = cast_int(f - othern);  /* rechain to point to 'f' */
      *f = *mp;  /* copy colliding node into free pos. (mp->next also goes) */
      if (gnext(mp) != 0) {
        gnext(f) += cast_int(mp - f);  /* correct 'next' */
        gnext(mp) = 0;  /* now 'mp' is free */
      }
      setempty(gval(mp));
    }
    else {  /* colliding node is in its own main position */
      /* new node will go into free position */
      if (gnext(mp) != 0)
        gnext(f) = cast_int((mp + gnext(mp)) - f);  /* chain new position */
      else lua_assert(gnext(f) == 0);
      gnext(mp) = cast_int(f - mp);
      mp = f;
    }
  }
  setnodekey(L, mp, key);
  luaC_barrierback(L, obj2gco(t), key);
  lua_assert(isempty(gval(mp)));
  setobj2t(L, gval(mp), value);
}


/**
 * @brief Retrieves a value from a table with an integer key.
 *
 * @param t The table.
 * @param key The integer key.
 * @return The value associated with the key, or absentkey if not found.
 */
const TValue *luaH_getint (Table *t, lua_Integer key) {
  lua_Unsigned alimit = t->alimit;
  if (l_castS2U(key) - 1u < alimit)  /* 'key' in [1, t->alimit]? */
    return &t->array[key - 1];
  else if (!isrealasize(t) &&  /* key still may be in the array part? */
           (((l_castS2U(key) - 1u) & ~(alimit - 1u)) < alimit)) {
    t->alimit = cast_uint(key);  /* probably '#t' is here now */
    return &t->array[key - 1];
  }
  else {  /* key is not in the array part; check the hash */
    Node *n = hashint(t, key);
    for (;;) {  /* check whether 'key' is somewhere in the chain */
      if (keyisinteger(n) && keyival(n) == key)
        return gval(n);  /* that's it */
      else {
        int nx = gnext(n);
        if (nx == 0) break;
        n += nx;
      }
    }
    return &absentkey;
  }



}
static const TValue *getintfromhash (Table *t, lua_Integer key) {
  Node *n = hashint(t, key);
  lua_assert(!ikeyinarray(t, key));
  for (;;) {  /* check whether 'key' is somewhere in the chain */
    if (keyisinteger(n) && keyival(n) == key)
      return gval(n);  /* that's it */
    else {
      int nx = gnext(n);
      if (nx == 0) break;
      n += nx;
    }
  }
  return &absentkey;
}
static int hashkeyisempty (Table *t, lua_Unsigned key) {
    const TValue *val = getintfromhash(t, l_castU2S(key));
    return isempty(val);
}
/*
** search function for short strings
*/
const TValue *luaH_getshortstr (Table *t, TString *key) {
  Node *n = hashstr(t, key);
  lua_assert(key->tt == LUA_VSHRSTR);
  for (;;) {  /* check whether 'key' is somewhere in the chain */
    if (keyisshrstr(n) && eqshrstr(keystrval(n), key))
      return gval(n);  /* that's it */
    else {
      int nx = gnext(n);
      if (nx == 0)
        return &absentkey;  /* not found */
      n += nx;
    }
  }
}


/**
 * @brief Retrieves a value from a table with a string key.
 *
 * @param t The table.
 * @param key The string key.
 * @return The value associated with the key, or absentkey if not found.
 */
const TValue *luaH_getstr (Table *t, TString *key) {
  if (key->tt == LUA_VSHRSTR)
    return luaH_getshortstr(t, key);
  else {  /* for long strings, use generic case */
    TValue ko;
    setsvalue(cast(lua_State *, NULL), &ko, key);
    return getgeneric(t, &ko, 0);
  }
}


/**
 * @brief Main search function for retrieving a value from a table.
 *
 * @param t The table.
 * @param key The key.
 * @return The value associated with the key, or absentkey if not found.
 */
const TValue *luaH_get (Table *t, const TValue *key) {
  const TValue *result;
  switch (ttypetag(key)) {
    case LUA_VSHRSTR: result = luaH_getshortstr(t, tsvalue(key)); break;
    case LUA_VNUMINT: result = luaH_getint(t, ivalue(key)); break;
    case LUA_VNIL: result = &absentkey; break;
    case LUA_VNUMFLT: {
      lua_Integer k;
      if (luaV_flttointeger(fltvalue(key), &k, F2Ieq))
        result = luaH_getint(t, k);
      else
        result = getgeneric(t, key, 0);
      break;
    }
    default:
      result = getgeneric(t, key, 0);
      break;
  }
  if (table_access_enabled && result != &absentkey) {
    log_key_value(key, result, "GET");
  } else if (table_access_enabled) {
    log_key_value(key, result, "GET");
  }
  return result;
}


/**
 * @brief Finishes a raw "set table" operation.
 *
 * 'slot' is where the value should have been (the result of a previous "get table").
 *
 * @param L The Lua state.
 * @param t The table.
 * @param key The key.
 * @param slot The slot where the value should be.
 * @param value The value to set.
 */
void luaH_finishset (lua_State *L, Table *t, const TValue *key,
                                   const TValue *slot, TValue *value) {
  if (isabstkey(slot))
    luaH_newkey(L, t, key, value);
  else
    setobj2t(L, cast(TValue *, slot), value);
}


/**
 * @brief Inserts or updates a value in a table.
 *
 * @param L The Lua state.
 * @param t The table.
 * @param key The key.
 * @param value The value.
 */
void luaH_set (lua_State *L, Table *t, const TValue *key, TValue *value) {
  const TValue *slot = luaH_get(t, key);
  if (table_access_enabled) {
    log_key_value(key, value, "SET");
  }
  luaH_finishset(L, t, key, slot, value);
}


/**
 * @brief Inserts or updates a value in a table with an integer key.
 *
 * @param L The Lua state.
 * @param t The table.
 * @param key The integer key.
 * @param value The value.
 */
void luaH_setint (lua_State *L, Table *t, lua_Integer key, TValue *value) {
  const TValue *p = luaH_getint(t, key);
  if (table_access_enabled) {
    TValue k;
    setivalue(&k, key);
    log_key_value(&k, value, "SET");
  }
  if (isabstkey(p)) {
    TValue k;
    setivalue(&k, key);
    luaH_newkey(L, t, &k, value);
  }
  else
    setobj2t(L, cast(TValue *, p), value);
}


/*
** Try to find a boundary in the hash part of table 't'. From the
** caller, we know that 'asize + 1' is present. We want to find a larger
** key that is absent from the table, so that we can do a binary search
** between the two keys to find a boundary. We keep doubling 'j' until
** we get an absent index.  If the doubling would overflow, we try
** LUA_MAXINTEGER. If it is absent, we are ready for the binary search.
** ('j', being max integer, is larger or equal to 'i', but it cannot be
** equal because it is absent while 'i' is present.) Otherwise, 'j' is a
** boundary. ('j + 1' cannot be a present integer key because it is not
** a valid integer in Lua.)
** About 'rnd': If we used a fixed algorithm, a bad actor could fill
** a table with only the keys that would be probed, in such a way that
** a small table could result in a huge length. To avoid that, we use
** the state's seed as a source of randomness. For the first probe,
** we "randomly double" 'i' by adding to it a random number roughly its
** width.
*/
static lua_Unsigned hash_search (Table *t, lua_Unsigned j) {
  lua_Unsigned i;
  if (j == 0) j++;  /* the caller ensures 'j + 1' is present */
  do {
    i = j;  /* 'i' is a present index */
    if (j <= l_castS2U(LUA_MAXINTEGER) / 2)
      j *= 2;
    else {
      j = LUA_MAXINTEGER;
      if (isempty(luaH_getint(t, j)))  /* t[j] not present? */
        break;  /* 'j' now is an absent index */
      else  /* weird case */
        return j;  /* well, max integer is a boundary... */
    }
  } while (!isempty(luaH_getint(t, j)));  /* repeat until an absent t[j] */
  /* i < j  &&  t[i] present  &&  t[j] absent */
  while (j - i > 1u) {  /* do a binary search between them */
    lua_Unsigned m = (i + j) / 2;
    if (isempty(luaH_getint(t, m))) j = m;
    else i = m;
  }
  return i;
}



static unsigned int binsearch (const TValue *array, unsigned int i,
                                                    unsigned int j) {
  while (j - i > 1u) {  /* binary search */
    unsigned int m = (i + j) / 2;
    if (isempty(&array[m - 1])) j = m;
    else i = m;
  }
  return i;
}


/* return a border, saving it as a hint for next call */
static lua_Unsigned newhint (Table *t, unsigned hint) {
  lua_assert(hint <= t->asize);
  *lenhint(t) = hint;
  return hint;
}


/**
 * @brief Returns the length of the table (the 'n' in t[1]..t[n]).
 *
 * @param t The table.
 * @return The length of the table.
 */
lua_Unsigned luaH_getn (Table *t) {
  unsigned int limit = t->alimit;
  if (limit > 0 && isempty(&t->array[limit - 1])) {  /* (1)? */
    /* there must be a boundary before 'limit' */
    if (limit >= 2 && !isempty(&t->array[limit - 2])) {
      /* 'limit - 1' is a boundary; can it be a new limit? */
      if (ispow2realasize(t) && !ispow2(limit - 1)) {
        t->alimit = limit - 1;
        setnorealasize(t);  /* now 'alimit' is not the real size */
      }
      return limit - 1;
    }
    else {  /* must search for a boundary in [0, limit] */
      unsigned int boundary = binsearch(t->array, 0, limit);
      /* can this boundary represent the real size of the array? */
      if (ispow2realasize(t) && boundary > luaH_realasize(t) / 2) {
        t->alimit = boundary;  /* use it as the new limit */
        setnorealasize(t);
      }
      return boundary;
    }
  }
  /* 'limit' is zero or present in table */
  if (!limitequalsasize(t)) {  /* (2)? */
    /* 'limit' > 0 and array has more elements after 'limit' */
    if (isempty(&t->array[limit]))  /* 'limit + 1' is empty? */
      return limit;  /* this is the boundary */
    /* else, try last element in the array */
    limit = luaH_realasize(t);
    if (isempty(&t->array[limit - 1])) {  /* empty? */
      /* there must be a boundary in the array after old limit,
         and it must be a valid new limit */
      unsigned int boundary = binsearch(t->array, t->alimit, limit);
      t->alimit = boundary;
      return boundary;
    }
    /* else, new limit is present in the table; check the hash part */
  }
  /* (3) 'limit' is the last element and either is zero or present in table */
  lua_assert(limit == luaH_realasize(t) &&
             (limit == 0 || !isempty(&t->array[limit - 1])));
  if (isdummy(t) || isempty(luaH_getint(t, cast(lua_Integer, limit + 1))))
    return limit;  /* 'limit + 1' is absent */
  else  /* 'limit + 1' is also present */
    return hash_search(t, limit);
}






#if defined(LUA_DEBUG)

/* export this function for the test library */

Node *luaH_mainposition (const Table *t, const TValue *key) {
  return mainpositionTV(t, key);
}

#endif


/**
 * @brief Enables or disables table access logging.
 *
 * @param L The Lua state.
 * @param enable 1 to enable, 0 to disable.
 * @return 1 on success, 0 on failure.
 */
int luaH_enable_access_log (lua_State *L, int enable) {
  (void)L;
  if (enable && !table_access_enabled) {
    open_table_access_log();
    if (table_access_log == NULL) {
      return 0;
    }
    fprintf(table_access_log, "\n========== TABLE ACCESS LOG ENABLED ==========\n");
    fflush(table_access_log);
  } else if (!enable && table_access_enabled) {
    fprintf(table_access_log, "========== TABLE ACCESS LOG DISABLED ==========\n\n");
    fflush(table_access_log);
    close_table_access_log();
  }
  table_access_enabled = enable;
  return 1;
}

/**
 * @brief Returns the path to the current table access log file.
 *
 * @param L The Lua state.
 * @return The log file path.
 */
const char *luaH_get_log_path (lua_State *L) {
  (void)L;
  return table_access_log_path;
}

const TValue *luaH_get_optimized (Table *t, const TValue *key) {
  switch (ttypetag(key)) {
    case LUA_VSHRSTR: return luaH_getshortstr(t, tsvalue(key));
    case LUA_VNUMINT: return luaH_getint(t, ivalue(key));
    case LUA_VNIL: return &absentkey;
    case LUA_VNUMFLT: {
      lua_Integer k;
      if (luaV_flttointeger(fltvalue(key), &k, F2Ieq))
        return luaH_getint(t, k);
      else
        return getgeneric(t, key, 0);
    }
    default:
      return getgeneric(t, key, 0);
  }
}
