/*
** $Id: llex.c $
** Lexical Analyzer
** See Copyright Notice in lua.h
*/

#define llex_c
#define LUA_CORE

#include "lprefix.h"


#include <locale.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include "lua.h"

#include "lctype.h"
#include "ldebug.h"
#include "ldo.h"
#include "lgc.h"
#include "llex.h"
#include "lobject.h"
#include "lparser.h"
#include "lstate.h"
#include "lstring.h"
#include "ltable.h"
#include "lzio.h"
#include "aes.h"
#include "sha256.h"



#define next(ls)	(ls->curpos++, ls->current = zgetc(ls->z))


/* minimum size for string buffer */
#if !defined(LUA_MINBUFFER)
#define LUA_MINBUFFER   32
#endif


#define currIsNewline(ls)	(ls->current == '\n' || ls->current == '\r')

/* Duplicate helper functions for llex.c */
static const char* nirithy_b64 = "9876543210zyxwvutsrqponmlkjihgfedcbaZYXWVUTSRQPONMLKJIHGFEDCBA-_";

static int nirithy_b64_val(char c) {
  const char *p = strchr(nirithy_b64, c);
  if (p) return (int)(p - nirithy_b64);
  return -1;
}

static unsigned char* nirithy_decode(const char* input, size_t input_len, size_t* out_len) {
  size_t len;
  unsigned char* out;
  size_t i, j;

  if (input_len % 4 != 0) return NULL;
  len = input_len / 4 * 3;
  if (input_len > 0 && input[input_len - 1] == '=') len--;
  if (input_len > 1 && input[input_len - 2] == '=') len--;
  out = (unsigned char*)malloc(len);
  if (!out) return NULL;

  for (i = 0, j = 0; i < input_len; i += 4) {
    int a = input[i] == '=' ? 0 : nirithy_b64_val(input[i]);
    int b = input[i+1] == '=' ? 0 : nirithy_b64_val(input[i+1]);
    int c = input[i+2] == '=' ? 0 : nirithy_b64_val(input[i+2]);
    int d = input[i+3] == '=' ? 0 : nirithy_b64_val(input[i+3]);
    uint32_t triple;

    if (a < 0 || b < 0 || c < 0 || d < 0) {
      free(out);
      return NULL;
    }
    triple = (uint32_t)((a << 18) + (b << 12) + (c << 6) + d);
    if (j < len) out[j++] = (triple >> 16) & 0xFF;
    if (j < len) out[j++] = (triple >> 8) & 0xFF;
    if (j < len) out[j++] = (triple) & 0xFF;
  }
  *out_len = len;
  return out;
}

static void nirithy_derive_key(uint64_t timestamp, uint8_t *key) {
  uint8_t input[32];
  uint8_t digest[SHA256_DIGEST_SIZE];

  memcpy(input, &timestamp, 8);
  memcpy(input + 8, "NirithySalt", 11);

  SHA256(input, 19, digest);
  memcpy(key, digest, 16);
}

static void nirithy_decrypt(unsigned char* data, size_t len, uint64_t timestamp, const uint8_t* iv) {
  uint8_t key[16];
  struct AES_ctx ctx;

  nirithy_derive_key(timestamp, key);
  AES_init_ctx_iv(&ctx, key, iv);
  AES_CTR_xcrypt_buffer(&ctx, data, (uint32_t)len);
}

typedef struct LoadState {
  int is_string;
  union {
    struct {
      int n;
      FILE *f;
      char buff[1024];
    } f;
    struct {
      const char *s;
      size_t size;
      char *to_free; /* buffer to free */
    } s;
  } u;
} LoadState;

static const char *getReader (lua_State *L, void *ud, size_t *size) {
  LoadState *ls = (LoadState *)ud;
  (void)L;
  if (ls->is_string) {
    if (ls->u.s.size == 0) return NULL;
    *size = ls->u.s.size;
    ls->u.s.size = 0;
    return ls->u.s.s;
  } else {
    if (ls->u.f.n > 0) {
      *size = ls->u.f.n;
      ls->u.f.n = 0;
    } else {
      if (feof(ls->u.f.f)) return NULL;
      *size = fread(ls->u.f.buff, 1, sizeof(ls->u.f.buff), ls->u.f.f);
    }
    return ls->u.f.buff;
  }
}

void luaX_pushincludefile(LexState *ls, const char *filename) {
  FILE *f = fopen(filename, "r");
  if (f == NULL) {
    luaX_syntaxerror(ls, luaO_pushfstring(ls->L, "cannot open file '%s'", filename));
  }

  /* Check signature */
  char sig[9];
  int is_encrypted = 0;
  if (fread(sig, 1, 9, f) == 9 && memcmp(sig, "Nirithy==", 9) == 0) {
    is_encrypted = 1;
  }

  LoadState *lf = luaM_new(ls->L, LoadState);

  if (is_encrypted) {
    /* Read whole file */
    long fsize;
    size_t payload_len;
    char *payload;
    size_t bin_len;
    unsigned char *bin;
    uint64_t timestamp;
    uint8_t iv[16];
    unsigned char *data;
    size_t data_len;

    fseek(f, 0, SEEK_END);
    fsize = ftell(f);
    fseek(f, 9, SEEK_SET); /* Skip signature */

    payload_len = fsize - 9;
    payload = (char*)malloc(payload_len + 1);
    if (!payload || fread(payload, 1, payload_len, f) != payload_len) {
        if (payload) free(payload);
        fclose(f);
        luaM_free(ls->L, lf);
        luaX_syntaxerror(ls, "failed to read encrypted file");
    }
    fclose(f); /* File no longer needed */

    bin = nirithy_decode(payload, payload_len, &bin_len);
    free(payload);

    if (!bin || bin_len <= 24) {
        if (bin) free(bin);
        luaM_free(ls->L, lf);
        luaX_syntaxerror(ls, "failed to decode encrypted file");
    }

    memcpy(&timestamp, bin, 8);
    memcpy(iv, bin + 8, 16);

    data = bin + 24;
    data_len = bin_len - 24;

    nirithy_decrypt(data, data_len, timestamp, iv);

    lf->is_string = 1;
    lf->u.s.s = (const char*)data;
    lf->u.s.size = data_len;
    lf->u.s.to_free = (char*)bin;

  } else {
    rewind(f);
    lf->is_string = 0;
    lf->u.f.f = f;
    lf->u.f.n = 0;
  }

  IncludeState *inc = luaM_new(ls->L, IncludeState);
  inc->z = ls->z;
  inc->buff = ls->buff;
  inc->linenumber = ls->linenumber;
  inc->lastline = ls->lastline;
  inc->source = ls->source;
  inc->prev = ls->inc_stack;
  ls->inc_stack = inc;

  ZIO *z = luaM_new(ls->L, ZIO);
  luaZ_init(ls->L, z, getReader, lf);

  ls->z = z;
  ls->linenumber = 1;
  ls->lastline = 1;
  ls->source = luaS_new(ls->L, filename);

  next(ls); /* Read first char */
}

static void luaX_popincludefile(LexState *ls) {
  IncludeState *inc = ls->inc_stack;
  if (inc) {
    /* Free current ZIO resources */
    LoadState *lf = (LoadState *)ls->z->data;

    if (lf->is_string) {
      if (lf->u.s.to_free) free(lf->u.s.to_free);
    } else {
      fclose(lf->u.f.f);
    }

    luaM_free(ls->L, lf);
    luaM_free(ls->L, ls->z);

    /* Restore state */
    ls->z = inc->z;
    ls->linenumber = inc->linenumber;
    ls->lastline = inc->lastline;
    ls->source = inc->source;
    ls->inc_stack = inc->prev;

    luaM_free(ls->L, inc);
  }
}

void luaX_addalias(LexState *ls, TString *name, Token *tokens, int ntokens) {
  Alias *a = luaM_new(ls->L, Alias);
  a->name = name;
  a->tokens = tokens;
  a->ntokens = ntokens;
  a->next = ls->aliases;
  ls->aliases = a;
}


/* ORDER RESERVED */
static const char* const luaX_warnNames[] = {
  "all",
  "var-shadow",
  "global-shadow",
  "type-mismatch",
  "unreachable-code",
  "excessive-arguments",
  "bad-practice",
  "possible-typo",
  "non-portable-code",
  "non-portable-bytecode",
  "non-portable-name",
  "implicit-global",
  "unannotated-fallthrough",
  "discarded-return",
  "field-shadow",
  "unused",
  NULL
};

static const char *const luaX_tokens [] = {
    "and", "asm", "async", "await", "bool", "break", "case", "catch", "char", "command", "concept", "const", "continue", "default", "defer", "do", "double", "else", "elseif",
    "end", "enum", "export", "false", "finally", "float", "for", "function", "global", "goto", "if", "in", "int", "is", "keyword", "lambda", "local", "long", "namespace", "nil", "not", "operator", "or",
    "repeat", "requires",
    "return", "struct", "switch", "take", "then", "true", "try", "until", "using", "void", "when", "while", "with",
    "//", "..", "...", "==", ">=", "<=", "~",  "<<", ">>", "|>", "<|", "|?>",
    "::", "<eof>",
    "<let>", "=>", ":=", "->",
    /* 复合赋值运算符 */
    "+=", "-=", "*=", "/=", "//=", "%=", "&=", "|=", "~=", ">>=", "<<=", "..=", "++",
    "?.", "??", "<=>", "$", "$$",
    "<number>", "<integer>", "<name>", "<string>", "<interpstring>", "<rawstring>"
};


#define save_and_next(ls) (save(ls, ls->current), next(ls))


static l_noret lexerror (LexState *ls, const char *msg, int token);

static void process_warning_comment(LexState *ls, const char *comment) {
  if (strncmp(comment, "@warnings", 15) != 0) return;
  comment += 15;

  if (*comment == ':') comment++;
  else if (*comment == ' ') comment++;
  else return;

  /* Skip spaces */
  while (*comment == ' ') comment++;

  if (strstr(comment, "disable-next")) {
    ls->disable_warnings_next_line = ls->linenumber + 1;
    return;
  }

  /* Split by comma */
  char buffer[256];
  const char *start = comment;
  while (*start) {
    const char *end = strchr(start, ',');
    if (!end) end = start + strlen(start);

    size_t len = end - start;
    if (len >= sizeof(buffer)) len = sizeof(buffer) - 1;
    memcpy(buffer, start, len);
    buffer[len] = '\0';

    /* Trim spaces from buffer */
    char *p = buffer;
    while (*p == ' ') p++;
    char *endp = p + strlen(p) - 1;
    while (endp > p && *endp == ' ') *endp-- = '\0';

    /* Process directive: enable-TYPE, disable-TYPE, error-TYPE */
    WarningState state = WS_ON;
    const char *name = p;
    if (strncmp(p, "enable-", 7) == 0) {
      state = WS_ON;
      name = p + 7;
    } else if (strncmp(p, "disable-", 8) == 0) {
      state = WS_OFF;
      name = p + 8;
    } else if (strncmp(p, "error-", 6) == 0) {
      state = WS_ERROR;
      name = p + 6;
    }

    int i;
    for (i = 0; i < WT_COUNT; i++) {
      if (strcmp(name, luaX_warnNames[i]) == 0) {
        if (i == WT_ALL) {
          int j;
          for (j = 0; j < WT_COUNT; j++) ls->warnings.states[j] = state;
        } else {
          ls->warnings.states[i] = state;
        }
        break;
      }
    }

    if (*end == '\0') break;
    start = end + 1;
  }
}

static void save (LexState *ls, int c) {
  Mbuffer *b = ls->buff;
  if (luaZ_bufflen(b) + 1 > luaZ_sizebuffer(b)) {
    size_t newsize;
    if (luaZ_sizebuffer(b) >= MAX_SIZE/2)
      lexerror(ls, "lexical element too long", 0);
    newsize = luaZ_sizebuffer(b) * 2;
    luaZ_resizebuffer(ls->L, b, newsize);
  }
  b->buffer[luaZ_bufflen(b)++] = cast_char(c);
}

static void saveA (LexState *ls, int c) {
  Mbuffer *b = ls->lastbuff;
  if (luaZ_bufflen(b) + 1 > luaZ_sizebuffer(b)) {
    size_t newsize;
    if (luaZ_sizebuffer(b) >= MAX_SIZE/2)
      lexerror(ls, "lexical element too long", 0);
    newsize = luaZ_sizebuffer(b) * 2;
    luaZ_resizebuffer(ls->L, b, newsize);
  }
  b->buffer[luaZ_bufflen(b)++] = cast_char(c);
}


void luaX_init (lua_State *L) {
  int i;
  TString *e = luaS_newliteral(L, LUA_ENV);  /* create env name */
  luaC_fix(L, obj2gco(e));  /* never collect this name */
  for (i=0; i<NUM_RESERVED; i++) {
    TString *ts = luaS_new(L, luaX_tokens[i]);
    luaC_fix(L, obj2gco(ts));  /* reserved words are never collected */
    ts->extra = cast_byte(i+1);  /* reserved word */
  }
}

void luaX_warning (LexState *ls, const char *msg, WarningType wt) {
  if (ls->linenumber == ls->disable_warnings_next_line) return;
  if (ls->warnings.states[wt] == WS_OFF) return;

  const char *warnName = luaX_warnNames[wt];
  if (ls->warnings.states[wt] == WS_ERROR) {
    const char *err = luaO_pushfstring(ls->L, "%s [error: %s]", msg, warnName);
    luaX_syntaxerror(ls, err);
  } else {
    const char *formatted = luaO_pushfstring(ls->L, "%s:%d: warning: %s [%s]\n",
                                             getstr(ls->source), ls->linenumber, msg, warnName);
    lua_warning(ls->L, formatted, 0);
    lua_pop(ls->L, 1);
  }
}

const char *luaX_token2str (LexState *ls, int token) {
  if (token < FIRST_RESERVED) {  /* single-byte symbols? */
    if (lisprint(token))
      return luaO_pushfstring(ls->L, "'%c'", token);
    else  /* control character */
      return luaO_pushfstring(ls->L, "'<\\%d>'", token);
  }
  else {
    const char *s = luaX_tokens[token - FIRST_RESERVED];
    if (token < TK_EOS)  /* fixed format (symbols and reserved words)? */
      return luaO_pushfstring(ls->L, "'%s'", s);
    else  /* names, strings, and numerals */
      return s;
  }
}


static const char *txtToken22 (LexState *ls, int token) {
  switch (token) {
    case TK_NAME:
    {
      if(ls->lastbuff!=NULL){
        saveA(ls, '\0');
        return luaO_pushfstring(ls->L, "'%s'", luaZ_buffer(ls->lastbuff));
      }else {
        save(ls, '\0');
        return luaO_pushfstring(ls->L, "'%s'", luaZ_buffer(ls->buff));
      }
    }
      case TK_STRING:
      {
        return luaO_pushfstring(ls->L, "'%s'", "<STRING>");
      }
    case TK_FLT: {
        return luaO_pushfstring(ls->L, "'%d'",(long long) ls->t.seminfo.r);
    }
    case TK_INT: {
      return luaO_pushfstring(ls->L, "'%d'", (long long)ls->t.seminfo.i);
    }
    default:
      return luaX_token2str(ls, token);
  }
}

static const char *txtToken (LexState *ls, int token) {
  switch (token) {
    case TK_NAME: case TK_STRING:
    case TK_FLT: case TK_INT:
      save(ls, '\0');
      return luaO_pushfstring(ls->L, "'%s'", luaZ_buffer(ls->buff));
    default:
      return luaX_token2str(ls, token);
  }
}

static char *get_source_line(LexState *ls, int line, int *col) {
  if (getstr(ls->source)[0] != '@') return NULL;
  FILE *f = fopen(getstr(ls->source) + 1, "rb");
  if (f == NULL) return NULL;

  int current_line = 1;
  long line_start_offset = 0;
  int c;
  long offset = 0;

  while (current_line < line) {
     while ((c = fgetc(f)) != EOF && c != '\n') {
        offset++;
     }
     if (c == EOF) { fclose(f); return NULL; }
     offset++; /* newline */
     current_line++;
     line_start_offset = offset;
  }

  size_t bufsize = 128;
  char *buffer = (char*)malloc(bufsize);
  if (!buffer) { fclose(f); return NULL; }

  size_t len = 0;
  while ((c = fgetc(f)) != EOF && c != '\n' && c != '\r') {
      if (len + 1 >= bufsize) {
          bufsize *= 2;
          char *newbuf = (char*)realloc(buffer, bufsize);
          if (!newbuf) { free(buffer); fclose(f); return NULL; }
          buffer = newbuf;
      }
      buffer[len++] = (char)c;
  }
  buffer[len] = '\0';
  fclose(f);

  if (col) {
      *col = ls->tokpos - (int)line_start_offset;
      if (*col < 0) *col = 0;
      if (*col > (int)len) *col = (int)len;
  }

  return buffer;
}

static l_noret lexerror (LexState *ls, const char *msg, int token) {
  msg = luaG_addinfo(ls->L, msg, ls->source, ls->linenumber);

  int col = 0;
  char *line_content = get_source_line(ls, ls->linenumber, &col);
  if (line_content) {
      luaO_pushfstring(ls->L, "%s\n    %d | %s\n      | ", msg, ls->linenumber, line_content);
      char *spaces = (char*)malloc(col + 1);
      if (spaces) {
        memset(spaces, ' ', col);
        spaces[col] = '\0';
        lua_pushstring(ls->L, spaces);
        free(spaces);
      } else {
        lua_pushstring(ls->L, "");
      }
      luaO_pushfstring(ls->L, "^ here");
      lua_concat(ls->L, 3);
      free(line_content);
  } else if (token) {
    luaO_pushfstring(ls->L,
                     "=============================\n"
                     "[X] [Lua语法错误]\n\n"
                     "    词法位置: %d\n"
                     "    行号: %d\n"
                     "    报错位置的附近的代码: %s\n"
                     "    错误描述: %s\n"
                     "    错误位置附近: %s\n\n"
                     "[Tip] 解决方法:\n"
                     "    1. 检查语法错误位置\n"
                     "    2. 确认括号、引号配对正确\n"
                     "    3. 检查关键字使用是否正确\n"
                     "=============================",
                     ls->tokpos,ls->lastline,
                     txtToken22(ls,ls->lasttoken),msg, txtToken(ls, token));
  }
  luaD_throw(ls->L, LUA_ERRSYNTAX);
}


l_noret luaX_syntaxerror (LexState *ls, const char *msg) {
  lexerror(ls, msg, ls->t.token);
}


/*
** Creates a new string and anchors it in scanner's table so that it
** will not be collected until the end of the compilation; by that time
** it should be anchored somewhere. It also internalizes long strings,
** ensuring there is only one copy of each unique string.  The table
** here is used as a set: the string enters as the key, while its value
** is irrelevant. We use the string itself as the value only because it
** is a TValue readily available. Later, the code generation can change
** this value.
*/
TString *luaX_newstring (LexState *ls, const char *str, size_t l) {
  lua_State *L = ls->L;
  TString *ts = luaS_newlstr(L, str, l);  /* create new string */
  const TValue *o = luaH_getstr(ls->h, ts);
  if (!ttisnil(o))  /* string already present? */
    ts = keystrval(nodefromval(o));  /* get saved copy */
  else {  /* not in use yet */
    TValue *stv = s2v(L->top.p++);  /* reserve stack space for string */
    setsvalue(L, stv, ts);  /* temporarily anchor the string */
    luaH_finishset(L, ls->h, stv, o, stv);  /* t[string] = string */
    /* table is not a metatable, so it does not need to invalidate cache */
    luaC_checkGC(L);
    L->top.p--;  /* remove string from stack */
  }
  return ts;
}


/*
** increment line number and skips newline sequence (any of
** \n, \r, \n\r, or \r\n)
*/
static void inclinenumber (LexState *ls) {
  int old = ls->current;
  lua_assert(currIsNewline(ls));
  next(ls);  /* skip '\n' or '\r' */
  if (currIsNewline(ls) && ls->current != old)
    next(ls);  /* skip '\n\r' or '\r\n' */
  if (++ls->linenumber >= MAX_INT)
    lexerror(ls, "chunk has too many lines", 0);
}


void luaX_setinput (lua_State *L, LexState *ls, ZIO *z, TString *source,
                    int firstchar) {
  ls->t.token = 0;
  ls->L = L;
  ls->current = firstchar;
  ls->lookahead.token = TK_EOS;  /* no look-ahead token */
  ls->lookahead2.token = TK_EOS; /* no look-ahead token */
  ls->z = z;
  ls->fs = NULL;
  ls->linenumber = 1;
  ls->lastline = 1;
  ls->source = source;
  ls->envn = luaS_newliteral(L, LUA_ENV);  /* get env name */

  /* Initialize preprocessor fields */
  ls->aliases = NULL;
  ls->inc_stack = NULL;
  ls->pending_tokens = NULL;
  ls->npending = 0;
  ls->defines = NULL;

  /* Initialize warnings */
  ls->disable_warnings_next_line = -1;
  {
    int i;
    for (i = 0; i < WT_COUNT; i++) ls->warnings.states[i] = WS_ON;
    ls->warnings.states[WT_GLOBAL_SHADOW] = WS_OFF;
    ls->warnings.states[WT_NON_PORTABLE_CODE] = WS_OFF;
    ls->warnings.states[WT_NON_PORTABLE_BYTECODE] = WS_OFF;
    ls->warnings.states[WT_NON_PORTABLE_NAME] = WS_OFF;
    ls->warnings.states[WT_IMPLICIT_GLOBAL] = WS_OFF;
    ls->warnings.states[WT_ALL] = WS_OFF;
  }

#if defined(LUA_COMPAT_GLOBAL)
  /* compatibility mode: "global" is not a reserved word */
  ls->glbn = luaS_newliteral(L, "global");  /* get "global" string */
  ls->glbn->extra = 0;  /* mark it as not reserved */
#endif
  luaZ_resizebuffer(ls->L, ls->buff, LUA_MINBUFFER);  /* initialize buffer */
}



/*
** ============================================
** LEXICAL ANALYZER
** ============================================
*/


static int check_next1 (LexState *ls, int c) {
  if (ls->current == c) {
    next(ls);
    return 1;
  }
  else return 0;
}


/*
** Check whether current char is in set 'set' (with two chars) and
** saves it
*/
static int check_next2 (LexState *ls, const char *set) {
  lua_assert(set[2] == '\0');
  if (ls->current == set[0] || ls->current == set[1]) {
    save_and_next(ls);
    return 1;
  }
  else return 0;
}


/* LUA_NUMBER */
/*
** This function is quite liberal in what it accepts, as 'luaO_str2num'
** will reject ill-formed numerals. Roughly, it accepts the following
** pattern:
**
**   %d(%x|%.|([Ee][+-]?))* | 0[Xx](%x|%.|([Pp][+-]?))*
**
** The only tricky part is to accept [+-] only after a valid exponent
** mark, to avoid reading '3-4' or '0xe+1' as a single number.
**
** The caller might have already read an initial dot.
*/
static int read_numeral (LexState *ls, SemInfo *seminfo) {
  TValue obj;
  const char *expo = "Ee";
  int first = ls->current;
  lua_assert(lisdigit(ls->current));
  save_and_next(ls);
  if (first == '0' && check_next2(ls, "xX"))  /* hexadecimal? */
    expo = "Pp";
  else if (first == '0' && check_next2(ls, "bB"))  /* binary? */
    expo = NULL;
  else if (first == '0' && check_next2(ls, "oO"))  /* octal? */
    expo = NULL;
  for (;;) {
    if (expo && check_next2(ls, expo))  /* exponent mark? */
      check_next2(ls, "-+");  /* optional exponent sign */
    else if (ls->current == '_')  /* underscore as visual separator? */
      next(ls);  /* skip underscore, don't save it */
    else if (lisxdigit(ls->current) || ls->current == '.')  /* '%x|%.' */
      save_and_next(ls);
    else break;
  }
  if (lislalpha(ls->current))  /* is numeral touching a letter? */
    save_and_next(ls);  /* force an error */
  save(ls, '\0');
  if (luaO_str2num(luaZ_buffer(ls->buff), &obj) == 0)  /* format error? */
    lexerror(ls, "malformed number", TK_FLT);
  if (ttisinteger(&obj)) {
    seminfo->i = ivalue(&obj);
    return TK_INT;
  }
  else {
    lua_assert(ttisfloat(&obj));
    seminfo->r = fltvalue(&obj);
    return TK_FLT;
  }
}


/*
** read a sequence '[=*[' or ']=*]', leaving the last bracket. If
** sequence is well formed, return its number of '='s + 2; otherwise,
** return 1 if it is a single bracket (no '='s and no 2nd bracket);
** otherwise (an unfinished '[==...') return 0.
*/
static size_t skip_sep (LexState *ls) {
  size_t count = 0;
  int s = ls->current;
  lua_assert(s == '[' || s == ']');
  save_and_next(ls);
  while (ls->current == '=') {
    save_and_next(ls);
    count++;
  }
  return (ls->current == s) ? count + 2
         : (count == 0) ? 1
         : 0;
}


static void read_long_string (LexState *ls, SemInfo *seminfo, size_t sep) {
  int line = ls->linenumber;  /* initial line (for error message) */
  save_and_next(ls);  /* skip 2nd '[' */
  if (currIsNewline(ls))  /* string starts with a newline? */
    inclinenumber(ls);  /* skip it */
  for (;;) {
    switch (ls->current) {
      case EOZ: {  /* error */
        const char *what = (seminfo ? "string" : "comment");
        const char *msg = luaO_pushfstring(ls->L,
                     "unfinished long %s (starting at line %d)", what, line);
        lexerror(ls, msg, TK_EOS);
        break;  /* to avoid warnings */
      }
      case ']': {
        if (skip_sep(ls) == sep) {
          save_and_next(ls);  /* skip 2nd ']' */
          goto endloop;
        }
        break;
      }
      case '\n': case '\r': {
        save(ls, '\n');
        inclinenumber(ls);
        if (!seminfo) luaZ_resetbuffer(ls->buff);  /* avoid wasting space */
        break;
      }
      default: {
        if (seminfo) save_and_next(ls);
        else next(ls);
      }
    }
  } endloop:
  if (seminfo)
    seminfo->ts = luaX_newstring(ls, luaZ_buffer(ls->buff) + sep,
                                     luaZ_bufflen(ls->buff) - 2 * sep);
}


static void esccheck (LexState *ls, int c, const char *msg) {
  if (!c) {
    if (ls->current != EOZ)
      save_and_next(ls);  /* add current to buffer for error message */
    lexerror(ls, msg, TK_STRING);
  }
}


static int gethexa (LexState *ls) {
  save_and_next(ls);
  esccheck (ls, lisxdigit(ls->current), "hexadecimal digit expected");
  return luaO_hexavalue(ls->current);
}


static int readhexaesc (LexState *ls) {
  int r = gethexa(ls);
  r = (r << 4) + gethexa(ls);
  luaZ_buffremove(ls->buff, 2);  /* remove saved chars from buffer */
  return r;
}


static unsigned long readutf8esc (LexState *ls) {
  unsigned long r;
  int i = 4;  /* chars to be removed: '\', 'u', '{', and first digit */
  save_and_next(ls);  /* skip 'u' */
  esccheck(ls, ls->current == '{', "missing '{'");
  r = gethexa(ls);  /* must have at least one digit */
  while (cast_void(save_and_next(ls)), lisxdigit(ls->current)) {
    i++;
    esccheck(ls, r <= (0x7FFFFFFFu >> 4), "UTF-8 value too large");
    r = (r << 4) + luaO_hexavalue(ls->current);
  }
  esccheck(ls, ls->current == '}', "missing '}'");
  next(ls);  /* skip '}' */
  luaZ_buffremove(ls->buff, i);  /* remove saved chars from buffer */
  return r;
}


static void utf8esc (LexState *ls) {
  char buff[UTF8BUFFSZ];
  int n = luaO_utf8esc(buff, readutf8esc(ls));
  for (; n > 0; n--)  /* add 'buff' to string */
    save(ls, buff[UTF8BUFFSZ - n]);
}


static int readdecesc (LexState *ls) {
  int i;
  int r = 0;  /* result accumulator */
  for (i = 0; i < 3 && lisdigit(ls->current); i++) {  /* read up to 3 digits */
    r = 10*r + ls->current - '0';
    save_and_next(ls);
  }
  esccheck(ls, r <= UCHAR_MAX, "decimal escape too large");
  luaZ_buffremove(ls->buff, i);  /* remove read digits from buffer */
  return r;
}


static void read_string (LexState *ls, int del, SemInfo *seminfo, int *has_interpolation) {
  save_and_next(ls);  /* keep delimiter (for error messages) */
  while (ls->current != del) {
    switch (ls->current) {
      case EOZ:
        lexerror(ls, "unfinished string", TK_EOS);
        break;  /* to avoid warnings */
      case '\n':
      case '\r':
        lexerror(ls, "unfinished string", TK_STRING);
        break;  /* to avoid warnings */
      case '$': {
        next(ls);  /* skip '$' */
        if (ls->current == '$') {
          /* 
          ** $$ 转义序列: 输出一个字面量 $
          ** 用于: "Price: $$100" -> "Price: $100"
          ** 或者: "$${name}" -> "${name}" (字面量)
          */
          next(ls);  /* 跳过第二个 $ */
          save(ls, '$');  /* 只保存一个 $ */
        }
        else if (ls->current == '{') {
          /* 
          ** 统一插值语法: ${...}
          ** - ${name} 表示简单变量
          ** - ${[expr]} 表示复杂表达式
          ** 解析器通过检查 { 后是否紧跟 [ 来区分
          */
          *has_interpolation = 1;
          save(ls, '$');  /* 保存$符号 */
          save_and_next(ls);  /* 保存{并跳过 */
          /* 收集直到匹配的 } */
          int depth = 1;
          while (depth > 0 && ls->current != EOZ) {
            if (ls->current == '{') depth++;
            else if (ls->current == '}') depth--;
            if (depth > 0) save_and_next(ls);
            else { save_and_next(ls); }  /* 保存最后的 } */
          }
        }
        else {
          save(ls, '$');  /* 普通$符号（未跟{ 或 $） */
        }
        break;
      }
      case '\\': {  /* escape sequences */
        int c;  /* final character to be saved */
        save_and_next(ls);  /* keep '\\' for error messages */
        switch (ls->current) {
          case 'a': c = '\a'; goto read_save;
          case 'b': c = '\b'; goto read_save;
          case 'f': c = '\f'; goto read_save;
          case 'n': c = '\n'; goto read_save;
          case 'r': c = '\r'; goto read_save;
          case 't': c = '\t'; goto read_save;
          case 'v': c = '\v'; goto read_save;
          case 'x': c = readhexaesc(ls); goto read_save;
          case 'u': utf8esc(ls);  goto no_save;
          case '\n': case '\r':
            inclinenumber(ls); c = '\n'; goto only_save;
          case '\\': case '\"': case '\'':
            c = ls->current; goto read_save;
          case EOZ: goto no_save;  /* will raise an error next loop */
          case 'z': {  /* zap following span of spaces */
            luaZ_buffremove(ls->buff, 1);  /* remove '\\' */
            next(ls);  /* skip the 'z' */
            while (lisspace(ls->current)) {
              if (currIsNewline(ls)) inclinenumber(ls);
              else next(ls);
            }
            goto no_save;
          }
          default: {
            esccheck(ls, lisdigit(ls->current), "invalid escape sequence");
            c = readdecesc(ls);  /* digital escape '\ddd' */
            goto only_save;
          }
        }
       read_save:
         next(ls);
         /* go through */
       only_save:
         luaZ_buffremove(ls->buff, 1);  /* remove '\\' */
         save(ls, c);
         /* go through */
       no_save: break;
      }
      default:
        save_and_next(ls);
    }
  }
  save_and_next(ls);  /* skip delimiter */
  seminfo->ts = luaX_newstring(ls, luaZ_buffer(ls->buff) + 1,
                                   luaZ_bufflen(ls->buff) - 2);
}


/*
** 读取原生字符串 _raw"..." 或 _raw'...'
** 不处理转义字符，直接读取原始内容
** 适合正则表达式、文件路径等场景
** 
** @param ls 词法状态
** @param del 分隔符（" 或 '）
** @param seminfo 语义信息输出
*/
static void read_rawstring (LexState *ls, int del, SemInfo *seminfo) {
  save_and_next(ls);  /* keep delimiter (for error messages) */
  while (ls->current != del) {
    switch (ls->current) {
      case EOZ:
        lexerror(ls, "unfinished raw string", TK_EOS);
        break;  /* to avoid warnings */
      case '\n':
      case '\r':
        lexerror(ls, "unfinished raw string", TK_RAWSTRING);
        break;  /* to avoid warnings */
      default:
        /* 原生字符串：直接保存字符，不处理转义 */
        save_and_next(ls);
    }
  }
  save_and_next(ls);  /* skip delimiter */
  seminfo->ts = luaX_newstring(ls, luaZ_buffer(ls->buff) + 1,
                                   luaZ_bufflen(ls->buff) - 2);
}


/*
** 读取原生长字符串 _raw[[...]] 或 _raw[=[...]=] 等
** 不处理转义字符，直接读取原始内容
** 
** @param ls 词法状态
** @param seminfo 语义信息输出
** @param sep 分隔符长度（等号数量+2）
*/
static void read_raw_long_string (LexState *ls, SemInfo *seminfo, size_t sep) {
  int line = ls->linenumber;  /* initial line (for error message) */
  save_and_next(ls);  /* skip 2nd '[' */
  if (currIsNewline(ls))  /* string starts with a newline? */
    inclinenumber(ls);  /* skip it */
  for (;;) {
    switch (ls->current) {
      case EOZ: {  /* error */
        const char *msg = luaO_pushfstring(ls->L,
                     "unfinished raw long string (starting at line %d)", line);
        lexerror(ls, msg, TK_EOS);
        break;  /* to avoid warnings */
      }
      case ']': {
        if (skip_sep(ls) == sep) {
          save_and_next(ls);  /* skip 2nd ']' */
          goto endloop;
        }
        break;
      }
      case '\n': case '\r': {
        save(ls, '\n');
        inclinenumber(ls);
        break;
      }
      default: {
        /* 原生字符串：直接保存字符，不处理转义 */
        save_and_next(ls);
      }
    }
  } endloop:
  seminfo->ts = luaX_newstring(ls, luaZ_buffer(ls->buff) + sep,
                                   luaZ_bufflen(ls->buff) - 2 * sep);
}


static int llex (LexState *ls, SemInfo *seminfo) {
  if (ls->pending_tokens) {
    if (ls->pending_idx < ls->npending) {
      Token *t = &ls->pending_tokens[ls->pending_idx++];
      *seminfo = t->seminfo;
      return t->token;
    }
    ls->pending_tokens = NULL;
    ls->npending = 0;
  }

  luaZ_resetbuffer(ls->buff);
  for (;;) {
    switch (ls->current) {
      case '\n': case '\r': {  /* line breaks */
        inclinenumber(ls);
        break;
      }
      case ' ': case '\f': case '\t': case '\v': {  /* spaces */
        next(ls);
        break;
      }
      case '-': {  /* '-' or '--' (comment) or '->' (arrow) or '-=' (复合赋值) */
        next(ls);
        if (ls->current == '>') {  /* '->' arrow */
          next(ls);
          return TK_ARROW;
        }
        if (ls->current == '=') {  /* '-=' 减法赋值 */
          next(ls);
          return TK_SUBEQ;
        }
        if (ls->current != '-') return '-';
        /* else is a comment */
        next(ls);
        if (ls->current == '[') {  /* long comment? */
          size_t sep = skip_sep(ls);
          luaZ_resetbuffer(ls->buff);  /* 'skip_sep' may dirty the buffer */
          if (sep >= 2) {
            read_long_string(ls, NULL, sep);  /* skip long comment */
            luaZ_resetbuffer(ls->buff);  /* previous call may dirty the buff. */
            break;
          }
        }
        /* else short comment */
        luaZ_resetbuffer(ls->buff);
        while (!currIsNewline(ls) && ls->current != EOZ) {
          save(ls, ls->current);
          next(ls);  /* skip until end of line (or end of file) */
        }
     
        save(ls, '\0');
        const char *comment = luaZ_buffer(ls->buff);
        const char *directive = strstr(comment, "@warnings");
        if (directive) {
           process_warning_comment(ls, directive);
        }
        luaZ_resetbuffer(ls->buff); /* Reset for next token */
        break;
      }
      case '[': {  /* long string or simply '[' */
        size_t sep = skip_sep(ls);
        if (sep >= 2) {
          read_long_string(ls, seminfo, sep);
          return TK_STRING;
        }
        else if (sep == 0)  /* '[=...' missing second bracket? */
          lexerror(ls, "invalid long string delimiter", TK_STRING);
        return '[';
      }
      case '=': {
        next(ls);
        if (check_next1(ls, '=')) return TK_EQ;  /* '==' */
        else if (check_next1(ls, '>')) return TK_MEAN;  /* '=>' */
        else return '=';
      }
      case '<': {
        next(ls);
        if (ls->current == '=') {  /* '<=' 或 '<=>' */
          next(ls);
          if (check_next1(ls, '>')) return TK_SPACESHIP;  /* '<=>' 三路比较 */
          else return TK_LE;  /* '<=' */
        }
        else if (check_next1(ls, '|')) return TK_REVPIPE;  /* '<|' 反向管道 */
        else if (ls->current == '<') {  /* '<<' 或 '<<=' */
          next(ls);
          if (check_next1(ls, '=')) return TK_SHLEQ;  /* '<<=' 左移赋值 */
          else return TK_SHL;  /* '<<' */
        }
        else return '<';
      }
      case '>': {
        next(ls);
        if (check_next1(ls, '=')) return TK_GE;  /* '>=' */
        else if (ls->current == '>') {  /* '>>' 或 '>>=' */
          next(ls);
          if (check_next1(ls, '=')) return TK_SHREQ;  /* '>>=' 右移赋值 */
          else return TK_SHR;  /* '>>' */
        }
        else return '>';
      }
      case '/': {
        next(ls);
        if (ls->current == '/') {  /* '//' 或 '//=' */
          next(ls);
          if (check_next1(ls, '=')) return TK_IDIVEQ;  /* '//=' 整除赋值 */
          else return TK_IDIV;  /* '//' */
        }
        else if (check_next1(ls, '=')) return TK_DIVEQ;  /* '/=' 除法赋值 */
        else return '/';
      }
      case '~': {
        next(ls);
        if (check_next1(ls, '=')) return TK_NE;  /* '~=' */
        else return '~';
      }
      case '!':{
        next(ls);
        if(check_next1(ls,'=')) return TK_NE;
        else return TK_NOT;
      }
      case '&':{
        next(ls);
        if(check_next1(ls,'&')) return TK_AND;
        else if(check_next1(ls,'=')) return TK_BANDEQ;  /* '&=' 位与赋值 */
        else return '&';
      }
      case '?':{
        next(ls);
        if (check_next1(ls, '.')) return TK_OPTCHAIN;  /* '?.' 可选链运算符 */
        else if (check_next1(ls, '?')) return TK_NULLCOAL;  /* '??' 空值合并运算符 */
        else return '?';
      }
      case '+':{  /* '+' 或 '+=' 或 '++' */
        next(ls);
        if(check_next1(ls,'=')) return TK_ADDEQ;  /* '+=' 加法赋值 */
        else if(check_next1(ls,'+')) return TK_PLUSPLUS;  /* '++' 自增 */
        else return '+';
      }
      case '*':{  /* '*' 或 '*=' */
        next(ls);
        if(check_next1(ls,'=')) return TK_MULEQ;  /* '*=' 乘法赋值 */
        else return '*';
      }
      case '%':{  /* '%' 或 '%=' */
        next(ls);
        if(check_next1(ls,'=')) return TK_MODEQ;  /* '%=' 取模赋值 */
        else return '%';
      }
      case '@':{
        next(ls);
        return TK_OR;
      }
      case '$':{  /* '$' 宏调用前缀 或 '$$' 运算符调用前缀 */
        next(ls);
        if (ls->current == '$') {
          next(ls);
          return TK_DOLLDOLL;  /* $$ 运算符调用 */
        }
        return TK_DOLLAR;
      }
      case '|':{
        next(ls);
        if(check_next1(ls,'|')) return TK_OR;  /* '||' */
        else if(ls->current == '?') {  /* '|?' 可能是安全管道 '|?>' */
          next(ls);
          if(check_next1(ls,'>')) return TK_SAFEPIPE;  /* '|?>' 安全管道 */
          else return '|';  /* 回退处理错误，这里简化为返回 '|' */
        }
        else if(check_next1(ls,'>')) return TK_PIPE;  /* '|>' */
        else if(check_next1(ls,'=')) return TK_BOREQ;  /* '|=' 位或赋值 */
        else return '|';  /* single '|' */
      }
      case ':': {
        next(ls);
        if (check_next1(ls, ':')) return TK_DBCOLON;  /* '::' */
        else if (check_next1(ls, '=')) return TK_WALRUS;  /* ':=' */
        else return ':';
      }
      case '"': case '\'': {  /* short literal strings */
        int has_interpolation = 0;
        read_string(ls, ls->current, seminfo, &has_interpolation);
        if (has_interpolation) return TK_INTERPSTRING;
        else return TK_STRING;
      }
      case '.': {  /* '.', '..', '...', '..=' 或数字 */
        save_and_next(ls);
        if (check_next1(ls, '.')) {
          if (check_next1(ls, '.'))
            return TK_DOTS;   /* '...' */
          else if (check_next1(ls, '='))
            return TK_CONCATEQ;  /* '..=' 连接赋值 */
          else return TK_CONCAT;   /* '..' */
        }
        else if (!lisdigit(ls->current)) return '.';
        else return read_numeral(ls, seminfo);
      }
      case '0': case '1': case '2': case '3': case '4':
      case '5': case '6': case '7': case '8': case '9': {
        return read_numeral(ls, seminfo);
      }
      case EOZ: {
        if (ls->inc_stack) {
          luaX_popincludefile(ls);
          continue;
        }
        return TK_EOS;
      }
      default: {
        if (lislalpha(ls->current)) {  /* identifier or reserved word? */
          TString *ts;
          do {
            save_and_next(ls);
          } while (lislalnum(ls->current));
          
          /* 检查是否是 _raw 原生字符串前缀 */
          size_t len = luaZ_bufflen(ls->buff);
          const char *buff = luaZ_buffer(ls->buff);
          if (len == 4 && 
              buff[0] == '_' && buff[1] == 'r' && buff[2] == 'a' && buff[3] == 'w') {
            /* 可能是原生字符串 _raw"..." 或 _raw[[...]] */
            if (ls->current == '"' || ls->current == '\'') {
              /* _raw"..." 或 _raw'...' 原生字符串 */
              luaZ_resetbuffer(ls->buff);  /* 清空缓冲区，不需要 _raw 前缀 */
              read_rawstring(ls, ls->current, seminfo);
              return TK_RAWSTRING;
            }
            else if (ls->current == '[') {
              /* 先 lookahead 检查下一个字符，不消耗当前 '[' 
               * 只有 _raw[[ 或 _raw[= 才是原生长字符串
               * _raw[k] 这种应该作为标识符 _raw 处理
               */
              int next_char = (ls->z->n > 0) ? cast_uchar(*(ls->z->p)) : EOZ;
              if (next_char == '[' || next_char == '=') {
                /* 是 _raw[[...]] 或 _raw[=[...]=] 形式 */
                luaZ_resetbuffer(ls->buff);  /* 清空缓冲区 */
                size_t sep = skip_sep(ls);
                if (sep >= 2) {
                  read_raw_long_string(ls, seminfo, sep);
                  return TK_RAWSTRING;
                }
              }
              /* 不是长字符串，作为标识符 _raw 处理 */
            }
          }
          
          ts = luaX_newstring(ls, luaZ_buffer(ls->buff),
                                  luaZ_bufflen(ls->buff));
          seminfo->ts = ts;
          if (isreserved(ts))  /* reserved word? */
            return ts->extra - 1 + FIRST_RESERVED;
          else {
            /* Check for alias */
            Alias *a = ls->aliases;
            while (a) {
              if (a->name == ts) {
                if (a->ntokens > 0) {
                  ls->pending_tokens = a->tokens;
                  ls->npending = a->ntokens;
                  ls->pending_idx = 1;
                  *seminfo = a->tokens[0].seminfo;
                  return a->tokens[0].token;
                }
                break;
              }
              a = a->next;
            }
            return TK_NAME;
          }
        }
        else {  /* single-char tokens ('+', '*', '%', '{', '}', ...) */
          int c = ls->current;
          next(ls);
          return c;
        }
      }
    }
  }
}


void luaX_next (LexState *ls) {
  ls->lastline = ls->linenumber;
  ls->lasttoken = ls->t.token;
  ls->lastbuff = ls->buff;
  ls->tokpos = ls->curpos;
  if (ls->lookahead.token != TK_EOS) {  /* is there a look-ahead token? */
    ls->t = ls->lookahead;  /* use this one */
    if (ls->lookahead2.token != TK_EOS) {
       ls->lookahead = ls->lookahead2;
       ls->lookahead2.token = TK_EOS;
    } else {
       ls->lookahead.token = TK_EOS;  /* and discharge it */
    }
  }
  else
    ls->t.token = llex(ls, &ls->t.seminfo);  /* read next token */
}


int luaX_lookahead (LexState *ls) {
  if (ls->lookahead.token != TK_EOS)
    return ls->lookahead.token;
  ls->lookahead.token = llex(ls, &ls->lookahead.seminfo);
  return ls->lookahead.token;
}

int luaX_lookahead2 (LexState *ls) {
  if (ls->lookahead.token == TK_EOS)
    ls->lookahead.token = llex(ls, &ls->lookahead.seminfo);
  if (ls->lookahead2.token != TK_EOS)
    return ls->lookahead2.token;
  ls->lookahead2.token = llex(ls, &ls->lookahead2.seminfo);
  return ls->lookahead2.token;
}
