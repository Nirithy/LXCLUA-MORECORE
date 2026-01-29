/*
** $Id: liolib.c $
** Standard I/O (and system) library
** See Copyright Notice in lua.h
*/

#define liolib_c
#define LUA_LIB

#include "lprefix.h"


#include <ctype.h>
#include <errno.h>
#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifndef _WIN32
#include <sys/mman.h>
#endif

#include "lua.h"

#include "lauxlib.h"
#include "lualib.h"




/*
** Change this macro to accept other modes for 'fopen' besides
** the standard ones.
*/
#if !defined(l_checkmode)

/* accepted extensions to 'mode' in 'fopen' */
#if !defined(L_MODEEXT)
#define L_MODEEXT	"b"
#endif

/* Check whether 'mode' matches '[rwa]%+?[L_MODEEXT]*' */
static int l_checkmode (const char *mode) {
  return (*mode != '\0' && strchr("rwa", *(mode++)) != NULL &&
         (*mode != '+' || ((void)(++mode), 1)) &&  /* skip if char is '+' */
         (strspn(mode, L_MODEEXT) == strlen(mode)));  /* check extensions */
}

#endif

/*
** {===========================================
** l_popen spawns a new process connected to the current
** one through the file streams.
** ============================================
*/

#if !defined(l_popen)		/* { */

#if defined(LUA_USE_POSIX)	/* { */

#define l_popen(L,c,m)		(fflush(NULL), popen(c,m))
#define l_pclose(L,file)	(pclose(file))

#elif defined(LUA_USE_WINDOWS)	/* }{ */

#define l_popen(L,c,m)		(_popen(c,m))
#define l_pclose(L,file)	(_pclose(file))

#if !defined(l_checkmodep)
/* Windows accepts "[rw][bt]?" as valid modes */
#define l_checkmodep(m)	((m[0] == 'r' || m[0] == 'w') && \
  (m[1] == '\0' || ((m[1] == 'b' || m[1] == 't') && m[2] == '\0')))
#endif

#else				/* }{ */

/* ISO C definitions */
#define l_popen(L,c,m)  \
	  ((void)c, (void)m, \
	  luaL_error(L, "'popen' not supported"), \
	  (FILE*)0)
#define l_pclose(L,file)		((void)L, (void)file, -1)

#endif				/* } */

#endif				/* } */


#if !defined(l_checkmodep)
/* By default, Lua accepts only "r" or "w" as valid modes */
#define l_checkmodep(m)        ((m[0] == 'r' || m[0] == 'w') && m[1] == '\0')
#endif

/* }=========================================== */


#if !defined(l_getc)		/* { */

#if defined(LUA_USE_POSIX)
#define l_getc(f)		getc_unlocked(f)
#define l_lockfile(f)		flockfile(f)
#define l_unlockfile(f)		funlockfile(f)
#else
#define l_getc(f)		getc(f)
#define l_lockfile(f)		((void)0)
#define l_unlockfile(f)		((void)0)
#endif

#endif				/* } */


/*
** {===========================================
** l_fseek: configuration for longer offsets
** ============================================
*/

#if !defined(l_fseek)		/* { */

#if defined(LUA_USE_POSIX)	/* { */

#include <sys/types.h>

#define l_fseek(f,o,w)		fseeko(f,o,w)
#define l_ftell(f)		ftello(f)
#define l_seeknum		off_t

#elif defined(LUA_USE_WINDOWS) && !defined(_CRTIMP_TYPEINFO) \
   && defined(_MSC_VER) && (_MSC_VER >= 1400)	/* }{ */

/* Windows (but not DDK) and Visual C++ 2005 or higher */
#define l_fseek(f,o,w)		_fseeki64(f,o,w)
#define l_ftell(f)		_ftelli64(f)
#define l_seeknum		__int64

#else				/* }{ */

/* ISO C definitions */
#define l_fseek(f,o,w)		fseek(f,o,w)
#define l_ftell(f)		ftell(f)
#define l_seeknum		long

#endif				/* } */

#endif				/* } */

/* }=========================================== */



#define IO_PREFIX	"_IO_"
#define IOPREF_LEN	(sizeof(IO_PREFIX)/sizeof(char) - 1)
#define IO_INPUT	(IO_PREFIX "input")
#define IO_OUTPUT	(IO_PREFIX "output")


typedef luaL_Stream LStream;


#define tolstream(L)	((LStream *)luaL_checkudata(L, 1, LUA_FILEHANDLE))

#define isclosed(p)	((p)->closef == NULL)


static int io_type (lua_State *L) {
  LStream *p;
  luaL_checkany(L, 1);
  p = (LStream *)luaL_testudata(L, 1, LUA_FILEHANDLE);
  if (p == NULL)
    luaL_pushfail(L);  /* not a file */
  else if (isclosed(p))
    lua_pushliteral(L, "closed file");
  else
    lua_pushliteral(L, "file");
  return 1;
}


static int f_tostring (lua_State *L) {
  LStream *p = tolstream(L);
  if (isclosed(p))
    lua_pushliteral(L, "file (closed)");
  else
    lua_pushfstring(L, "file (%p)", p->f);
  return 1;
}


static FILE *tofile (lua_State *L) {
  LStream *p = tolstream(L);
  if (l_unlikely(isclosed(p)))
    luaL_error(L, "[!] 错误: 尝试使用已关闭的文件");
  lua_assert(p->f);
  return p->f;
}


/*
** When creating file handles, always creates a 'closed' file handle
** before opening the actual file; so, if there is a memory error, the
** handle is in a consistent state.
*/
static LStream *newprefile (lua_State *L) {
  LStream *p = (LStream *)lua_newuserdatauv(L, sizeof(LStream), 0);
  p->closef = NULL;  /* mark file handle as 'closed' */
  luaL_setmetatable(L, LUA_FILEHANDLE);
  return p;
}


/*
** Calls the 'close' function from a file handle. The 'volatile' avoids
** a bug in some versions of the Clang compiler (e.g., clang 3.0 for
** 32 bits).
*/
static int aux_close (lua_State *L) {
  LStream *p = tolstream(L);
  volatile lua_CFunction cf = p->closef;
  p->closef = NULL;  /* mark stream as closed */
  return (*cf)(L);  /* close it */
}


static int f_close (lua_State *L) {
  tofile(L);  /* make sure argument is an open stream */
  return aux_close(L);
}


static int io_close (lua_State *L) {
  if (lua_isnone(L, 1))  /* no argument? */
    lua_getfield(L, LUA_REGISTRYINDEX, IO_OUTPUT);  /* use default output */
  return f_close(L);
}


static int f_gc (lua_State *L) {
  LStream *p = tolstream(L);
  if (!isclosed(p) && p->f != NULL)
    aux_close(L);  /* ignore closed and incompletely open files */
  return 0;
}


/*
** function to close regular files
*/
static int io_fclose (lua_State *L) {
  LStream *p = tolstream(L);
  errno = 0;
  return luaL_fileresult(L, (fclose(p->f) == 0), NULL);
}


static LStream *newfile (lua_State *L) {
  LStream *p = newprefile(L);
  p->f = NULL;
  p->closef = &io_fclose;
  return p;
}


static void opencheck (lua_State *L, const char *fname, const char *mode) {
  LStream *p = newfile(L);
  p->f = fopen(fname, mode);
  if (l_unlikely(p->f == NULL))
    luaL_error(L, "无法打开文件 '%s' (%s)", fname, strerror(errno));
}


static int io_open (lua_State *L) {
  const char *filename = luaL_checkstring(L, 1);
  const char *mode = luaL_optstring(L, 2, "r");
  LStream *p = newfile(L);
  const char *md = mode;  /* to traverse/check mode */
  luaL_argcheck(L, l_checkmode(md), 2, "invalid mode");
  errno = 0;
  p->f = fopen(filename, mode);
  return (p->f == NULL) ? luaL_fileresult(L, 0, filename) : 1;
}


/*
** function to close 'popen' files
*/
static int io_pclose (lua_State *L) {
  LStream *p = tolstream(L);
  errno = 0;
  return luaL_execresult(L, l_pclose(L, p->f));
}


static int io_popen (lua_State *L) {
  const char *filename = luaL_checkstring(L, 1);
  const char *mode = luaL_optstring(L, 2, "r");
  LStream *p = newprefile(L);
  luaL_argcheck(L, l_checkmodep(mode), 2, "invalid mode");
  errno = 0;
  p->f = l_popen(L, filename, mode);
  p->closef = &io_pclose;
  return (p->f == NULL) ? luaL_fileresult(L, 0, filename) : 1;
}


static int io_tmpfile (lua_State *L) {
  LStream *p = newfile(L);
  errno = 0;
  p->f = tmpfile();
  return (p->f == NULL) ? luaL_fileresult(L, 0, NULL) : 1;
}


static FILE *getiofile (lua_State *L, const char *findex) {
  LStream *p;
  lua_getfield(L, LUA_REGISTRYINDEX, findex);
  p = (LStream *)lua_touserdata(L, -1);
  if (l_unlikely(isclosed(p)))
    luaL_error(L, "default %s file is closed", findex + IOPREF_LEN);
  return p->f;
}


static int g_iofile (lua_State *L, const char *f, const char *mode) {
  if (!lua_isnoneornil(L, 1)) {
    const char *filename = lua_tostring(L, 1);
    if (filename)
      opencheck(L, filename, mode);
    else {
      tofile(L);  /* check that it's a valid file handle */
      lua_pushvalue(L, 1);
    }
    lua_setfield(L, LUA_REGISTRYINDEX, f);
  }
  /* return current value */
  lua_getfield(L, LUA_REGISTRYINDEX, f);
  return 1;
}


static int io_input (lua_State *L) {
  return g_iofile(L, IO_INPUT, "r");
}


static int io_output (lua_State *L) {
  return g_iofile(L, IO_OUTPUT, "w");
}


static int io_readline (lua_State *L);


/*
** maximum number of arguments to 'f:lines'/'io.lines' (it + 3 must fit
** in the limit for upvalues of a closure)
*/
#define MAXARGLINE	250

/*
** Auxiliary function to create the iteration function for 'lines'.
** The iteration function is a closure over 'io_readline', with
** the following upvalues:
** 1) The file being read (first value in the stack)
** 2) the number of arguments to read
** 3) a boolean, true iff file has to be closed when finished ('toclose')
** *) a variable number of format arguments (rest of the stack)
*/
static void aux_lines (lua_State *L, int toclose) {
  int n = lua_gettop(L) - 1;  /* number of arguments to read */
  luaL_argcheck(L, n <= MAXARGLINE, MAXARGLINE + 2, "too many arguments");
  lua_pushvalue(L, 1);  /* file */
  lua_pushinteger(L, n);  /* number of arguments to read */
  lua_pushboolean(L, toclose);  /* close/not close file when finished */
  lua_rotate(L, 2, 3);  /* move the three values to their positions */
  lua_pushcclosure(L, io_readline, 3 + n);
}


static int f_lines (lua_State *L) {
  tofile(L);  /* check that it's a valid file handle */
  aux_lines(L, 0);
  return 1;
}


/*
** Return an iteration function for 'io.lines'. If file has to be
** closed, also returns the file itself as a second result (to be
** closed as the state at the exit of a generic for).
*/
static int io_lines (lua_State *L) {
  int toclose;
  if (lua_isnone(L, 1)) lua_pushnil(L);  /* at least one argument */
  if (lua_isnil(L, 1)) {  /* no file name? */
    lua_getfield(L, LUA_REGISTRYINDEX, IO_INPUT);  /* get default input */
    lua_replace(L, 1);  /* put it at index 1 */
    tofile(L);  /* check that it's a valid file handle */
    toclose = 0;  /* do not close it after iteration */
  }
  else {  /* open a new file */
    const char *filename = luaL_checkstring(L, 1);
    opencheck(L, filename, "r");
    lua_replace(L, 1);  /* put file at index 1 */
    toclose = 1;  /* close it after iteration */
  }
  aux_lines(L, toclose);  /* push iteration function */
  if (toclose) {
    lua_pushnil(L);  /* state */
    lua_pushnil(L);  /* control */
    lua_pushvalue(L, 1);  /* file is the to-be-closed variable (4th result) */
    return 4;
  }
  else
    return 1;
}


/*
** {===========================================
** READ
** ============================================
*/


/* maximum length of a numeral */
#if !defined (L_MAXLENNUM)
#define L_MAXLENNUM     200
#endif


/* auxiliary structure used by 'read_number' */
typedef struct {
  FILE *f;  /* file being read */
  int c;  /* current character (look ahead) */
  int n;  /* number of elements in buffer 'buff' */
  char buff[L_MAXLENNUM + 1];  /* +1 for ending '\0' */
} RN;


/*
** Add current char to buffer (if not out of space) and read next one
*/
static int nextc (RN *rn) {
  if (l_unlikely(rn->n >= L_MAXLENNUM)) {  /* buffer overflow? */
    rn->buff[0] = '\0';  /* invalidate result */
    return 0;  /* fail */
  }
  else {
    rn->buff[rn->n++] = rn->c;  /* save current char */
    rn->c = l_getc(rn->f);  /* read next one */
    return 1;
  }
}


/*
** Accept current char if it is in 'set' (of size 2)
*/
static int test2 (RN *rn, const char *set) {
  if (rn->c == set[0] || rn->c == set[1])
    return nextc(rn);
  else return 0;
}


/*
** Read a sequence of (hex)digits
*/
static int readdigits (RN *rn, int hex) {
  int count = 0;
  while ((hex ? isxdigit(rn->c) : isdigit(rn->c)) && nextc(rn))
    count++;
  return count;
}


/*
** Read a number: first reads a valid prefix of a numeral into a buffer.
** Then it calls 'lua_stringtonumber' to check whether the format is
** correct and to convert it to a Lua number.
*/
static int read_number (lua_State *L, FILE *f) {
  RN rn;
  int count = 0;
  int hex = 0;
  char decp[2];
  rn.f = f; rn.n = 0;
  decp[0] = lua_getlocaledecpoint();  /* get decimal point from locale */
  decp[1] = '.';  /* always accept a dot */
  l_lockfile(rn.f);
  do { rn.c = l_getc(rn.f); } while (isspace(rn.c));  /* skip spaces */
  test2(&rn, "-+");  /* optional sign */
  if (test2(&rn, "00")) {
    if (test2(&rn, "xX")) hex = 1;  /* numeral is hexadecimal */
    else count = 1;  /* count initial '0' as a valid digit */
  }
  count += readdigits(&rn, hex);  /* integral part */
  if (test2(&rn, decp))  /* decimal point? */
    count += readdigits(&rn, hex);  /* fractional part */
  if (count > 0 && test2(&rn, (hex ? "pP" : "eE"))) {  /* exponent mark? */
    test2(&rn, "-+");  /* exponent sign */
    readdigits(&rn, 0);  /* exponent digits */
  }
  ungetc(rn.c, rn.f);  /* unread look-ahead char */
  l_unlockfile(rn.f);
  rn.buff[rn.n] = '\0';  /* finish string */
  if (l_likely(lua_stringtonumber(L, rn.buff)))
    return 1;  /* ok, it is a valid number */
  else {  /* invalid format */
   lua_pushnil(L);  /* "result" to be removed */
   return 0;  /* read fails */
  }
}


static int test_eof (lua_State *L, FILE *f) {
  int c = getc(f);
  ungetc(c, f);  /* no-op when c == EOF */
  lua_pushliteral(L, "");
  return (c != EOF);
}


static int read_line (lua_State *L, FILE *f, int chop) {
  luaL_Buffer b;
  int c;
  luaL_buffinit(L, &b);
  do {  /* may need to read several chunks to get whole line */
    char *buff = luaL_prepbuffer(&b);  /* preallocate buffer space */
    int i = 0;
    l_lockfile(f);  /* no memory errors can happen inside the lock */
    while (i < LUAL_BUFFERSIZE && (c = l_getc(f)) != EOF && c != '\n')
      buff[i++] = c;  /* read up to end of line or buffer limit */
    l_unlockfile(f);
    luaL_addsize(&b, i);
  } while (c != EOF && c != '\n');  /* repeat until end of line */
  if (!chop && c == '\n')  /* want a newline and have one? */
    luaL_addchar(&b, c);  /* add ending newline to result */
  luaL_pushresult(&b);  /* close buffer */
  /* return ok if read something (either a newline or something else) */
  return (c == '\n' || lua_rawlen(L, -1) > 0);
}


static void read_all (lua_State *L, FILE *f) {
  size_t nr;
  luaL_Buffer b;
  luaL_buffinit(L, &b);
  do {  /* read file in chunks of LUAL_BUFFERSIZE bytes */
    char *p = luaL_prepbuffer(&b);
    nr = fread(p, sizeof(char), LUAL_BUFFERSIZE, f);
    luaL_addsize(&b, nr);
  } while (nr == LUAL_BUFFERSIZE);
  luaL_pushresult(&b);  /* close buffer */
}


static int read_chars (lua_State *L, FILE *f, size_t n) {
  size_t nr;  /* number of chars actually read */
  char *p;
  luaL_Buffer b;
  luaL_buffinit(L, &b);
  p = luaL_prepbuffsize(&b, n);  /* prepare buffer to read whole block */
  nr = fread(p, sizeof(char), n, f);  /* try to read 'n' chars */
  luaL_addsize(&b, nr);
  luaL_pushresult(&b);  /* close buffer */
  return (nr > 0);  /* true iff read something */
}


static int g_read (lua_State *L, FILE *f, int first) {
  int nargs = lua_gettop(L) - 1;
  int n, success;
  clearerr(f);
  errno = 0;
  if (nargs == 0) {  /* no arguments? */
    success = read_line(L, f, 1);
    n = first + 1;  /* to return 1 result */
  }
  else {
    /* ensure stack space for all results and for auxlib's buffer */
    luaL_checkstack(L, nargs+LUA_MINSTACK, "too many arguments");
    success = 1;
    for (n = first; nargs-- && success; n++) {
      if (lua_type(L, n) == LUA_TNUMBER) {
        size_t l = (size_t)luaL_checkinteger(L, n);
        success = (l == 0) ? test_eof(L, f) : read_chars(L, f, l);
      }
      else {
        const char *p = luaL_checkstring(L, n);
        if (*p == '*') p++;  /* skip optional '*' (for compatibility) */
        switch (*p) {
          case 'n':  /* number */
            success = read_number(L, f);
            break;
          case 'l':  /* line */
            success = read_line(L, f, 1);
            break;
          case 'L':  /* line with end-of-line */
            success = read_line(L, f, 0);
            break;
          case 'a':  /* file */
            read_all(L, f);  /* read entire file */
            success = 1; /* always success */
            break;
          default:
            return luaL_argerror(L, n, "invalid format");
        }
      }
    }
  }
  if (ferror(f))
    return luaL_fileresult(L, 0, NULL);
  if (!success) {
    lua_pop(L, 1);  /* remove last result */
    luaL_pushfail(L);  /* push nil instead */
  }
  return n - first;
}


static int io_read (lua_State *L) {
  return g_read(L, getiofile(L, IO_INPUT), 1);
}


static int f_read (lua_State *L) {
  return g_read(L, tofile(L), 2);
}


/*
** Iteration function for 'lines'.
*/
static int io_readline (lua_State *L) {
  LStream *p = (LStream *)lua_touserdata(L, lua_upvalueindex(1));
  int i;
  int n = (int)lua_tointeger(L, lua_upvalueindex(2));
  if (isclosed(p))  /* file is already closed? */
    return luaL_error(L, "文件已关闭");
  lua_settop(L , 1);
  luaL_checkstack(L, n, "too many arguments");
  for (i = 1; i <= n; i++)  /* push arguments to 'g_read' */
    lua_pushvalue(L, lua_upvalueindex(3 + i));
  n = g_read(L, p->f, 2);  /* 'n' is number of results */
  lua_assert(n > 0);  /* should return at least a nil */
  if (lua_toboolean(L, -n))  /* read at least one value? */
    return n;  /* return them */
  else {  /* first result is false: EOF or error */
    if (n > 1) {  /* is there error information? */
      /* 2nd result is error message */
      return luaL_error(L, "%s", lua_tostring(L, -n + 1));
    }
    if (lua_toboolean(L, lua_upvalueindex(3))) {  /* generator created file? */
      lua_settop(L, 0);  /* clear stack */
      lua_pushvalue(L, lua_upvalueindex(1));  /* push file at index 1 */
      aux_close(L);  /* close it */
    }
    return 0;
  }
}

/* }=========================================== */


static int g_write (lua_State *L, FILE *f, int arg) {
  int nargs = lua_gettop(L) - arg;
  int status = 1;
  errno = 0;
  for (; nargs--; arg++) {
    if (lua_type(L, arg) == LUA_TNUMBER) {
      /* optimization: could be done exactly as for strings */
      int len = lua_isinteger(L, arg)
                ? fprintf(f, LUA_INTEGER_FMT,
                             (LUAI_UACINT)lua_tointeger(L, arg))
                : fprintf(f, LUA_NUMBER_FMT,
                             (LUAI_UACNUMBER)lua_tonumber(L, arg));
      status = status && (len > 0);
    }
    else {
      size_t l;
      const char *s = luaL_checklstring(L, arg, &l);
      status = status && (fwrite(s, sizeof(char), l, f) == l);
    }
  }
  if (l_likely(status))
    return 1;  /* file handle already on stack top */
  else
    return luaL_fileresult(L, status, NULL);
}


static int io_write (lua_State *L) {
  return g_write(L, getiofile(L, IO_OUTPUT), 1);
}


static int f_write (lua_State *L) {
  FILE *f = tofile(L);
  lua_pushvalue(L, 1);  /* push file at the stack top (to be returned) */
  return g_write(L, f, 2);
}


static int f_seek (lua_State *L) {
  static const int mode[] = {SEEK_SET, SEEK_CUR, SEEK_END};
  static const char *const modenames[] = {"set", "cur", "end", NULL};
  FILE *f = tofile(L);
  int op = luaL_checkoption(L, 2, "cur", modenames);
  lua_Integer p3 = luaL_optinteger(L, 3, 0);
  l_seeknum offset = (l_seeknum)p3;
  luaL_argcheck(L, (lua_Integer)offset == p3, 3,
                  "not an integer in proper range");
  errno = 0;
  op = l_fseek(f, offset, mode[op]);
  if (l_unlikely(op))
    return luaL_fileresult(L, 0, NULL);  /* error */
  else {
    lua_pushinteger(L, (lua_Integer)l_ftell(f));
    return 1;
  }
}


static int f_setvbuf (lua_State *L) {
  static const int mode[] = {_IONBF, _IOFBF, _IOLBF};
  static const char *const modenames[] = {"no", "full", "line", NULL};
  FILE *f = tofile(L);
  int op = luaL_checkoption(L, 2, NULL, modenames);
  lua_Integer sz = luaL_optinteger(L, 3, LUAL_BUFFERSIZE);
  int res;
  errno = 0;
  res = setvbuf(f, NULL, mode[op], (size_t)sz);
  return luaL_fileresult(L, res == 0, NULL);
}



static int io_flush (lua_State *L) {
  FILE *f = getiofile(L, IO_OUTPUT);
  errno = 0;
  return luaL_fileresult(L, fflush(f) == 0, NULL);
}


static int f_flush (lua_State *L) {
  FILE *f = tofile(L);
  errno = 0;
  return luaL_fileresult(L, fflush(f) == 0, NULL);
}


/**
 * 按行号读取文件的特定行
 * 功能描述：读取文件中指定行号的内容，支持文本和二进制模式
 * 参数说明：filename - 文件名, line_num - 行号(从1开始), mode - 可选,"b"为二进制模式
 * 返回值说明：文本模式返回字符串，二进制模式返回字节table，失败返回nil和错误信息
 */
static int io_readline_n (lua_State *L) {
  const char *filename = luaL_checkstring(L, 1);
  lua_Integer line_num = luaL_checkinteger(L, 2);
  const char *mode = luaL_optstring(L, 3, "t");
  int binary_mode = (mode[0] == 'b' || mode[0] == 'B');
  FILE *f;
  int current_line = 1;
  luaL_Buffer b;
  int c;
  
  if (line_num < 1) {
    luaL_pushfail(L);
    lua_pushliteral(L, "行号必须大于0");
    return 2;
  }
  
  errno = 0;
  f = fopen(filename, binary_mode ? "rb" : "r");
  if (f == NULL) {
    return luaL_fileresult(L, 0, filename);
  }
  
  /* 跳过前面的行 */
  while (current_line < line_num) {
    c = fgetc(f);
    if (c == EOF) {
      fclose(f);
      luaL_pushfail(L);
      lua_pushfstring(L, "文件只有 %d 行", current_line - 1);
      return 2;
    }
    if (c == '\n') current_line++;
  }
  
  if (binary_mode) {
    /* 二进制模式：返回字节table */
    lua_newtable(L);
    int idx = 1;
    while ((c = fgetc(f)) != EOF && c != '\n') {
      lua_pushinteger(L, (unsigned char)c);
      lua_rawseti(L, -2, idx++);
    }
  } else {
    /* 文本模式：返回字符串 */
    luaL_buffinit(L, &b);
    while ((c = fgetc(f)) != EOF && c != '\n') {
      luaL_addchar(&b, c);
    }
    luaL_pushresult(&b);
  }
  
  fclose(f);
  return 1;
}


/**
 * 按行号写入/替换文件的特定行
 * 功能描述：写入或替换文件中指定行号的内容，支持文本和二进制模式
 * 参数说明：filename - 文件名, line_num - 行号(从1开始), content - 字符串或字节table, mode - 可选,"b"为二进制
 * 返回值说明：成功返回true，失败返回nil和错误信息
 */
static int io_writeline_n (lua_State *L) {
  const char *filename = luaL_checkstring(L, 1);
  lua_Integer line_num = luaL_checkinteger(L, 2);
  const char *mode = luaL_optstring(L, 4, "t");
  int binary_mode = (mode[0] == 'b' || mode[0] == 'B');
  FILE *f_in, *f_out;
  char *temp_filename;
  int current_line = 1;
  int c;
  size_t fname_len;
  const char *content = NULL;
  size_t content_len = 0;
  
  if (line_num < 1) {
    luaL_pushfail(L);
    lua_pushliteral(L, "行号必须大于0");
    return 2;
  }
  
  /* 获取内容 */
  if (binary_mode) {
    luaL_checktype(L, 3, LUA_TTABLE);
  } else {
    content = luaL_checklstring(L, 3, &content_len);
  }
  
  /* 创建临时文件名 */
  fname_len = strlen(filename);
  temp_filename = (char *)lua_newuserdata(L, fname_len + 5);
  strcpy(temp_filename, filename);
  strcat(temp_filename, ".tmp");
  
  errno = 0;
  f_in = fopen(filename, binary_mode ? "rb" : "r");
  if (f_in == NULL) {
    /* 文件不存在，创建新文件 */
    f_out = fopen(filename, binary_mode ? "wb" : "w");
    if (f_out == NULL) {
      return luaL_fileresult(L, 0, filename);
    }
    /* 写入空行直到目标行 */
    for (lua_Integer i = 1; i < line_num; i++) {
      fputc('\n', f_out);
    }
    /* 写入内容 */
    if (binary_mode) {
      lua_Integer len = luaL_len(L, 3);
      for (lua_Integer i = 1; i <= len; i++) {
        lua_rawgeti(L, 3, i);
        fputc((unsigned char)lua_tointeger(L, -1), f_out);
        lua_pop(L, 1);
      }
    } else {
      fwrite(content, 1, content_len, f_out);
    }
    fputc('\n', f_out);
    fclose(f_out);
    lua_pushboolean(L, 1);
    return 1;
  }
  
  f_out = fopen(temp_filename, binary_mode ? "wb" : "w");
  if (f_out == NULL) {
    fclose(f_in);
    return luaL_fileresult(L, 0, temp_filename);
  }
  
  /* 复制并替换指定行 */
  while ((c = fgetc(f_in)) != EOF) {
    if (current_line == line_num) {
      /* 写入新内容 */
      if (binary_mode) {
        lua_Integer len = luaL_len(L, 3);
        for (lua_Integer i = 1; i <= len; i++) {
          lua_rawgeti(L, 3, i);
          fputc((unsigned char)lua_tointeger(L, -1), f_out);
          lua_pop(L, 1);
        }
      } else {
        fwrite(content, 1, content_len, f_out);
      }
      /* 跳过原来的行内容 */
      while (c != '\n' && c != EOF) {
        c = fgetc(f_in);
      }
      if (c == '\n') {
        fputc('\n', f_out);
      }
      current_line++;
    } else {
      fputc(c, f_out);
      if (c == '\n') current_line++;
    }
  }
  
  /* 如果目标行超过文件行数，追加空行 */
  while (current_line < line_num) {
    fputc('\n', f_out);
    current_line++;
  }
  if (current_line == line_num) {
    if (binary_mode) {
      lua_Integer len = luaL_len(L, 3);
      for (lua_Integer i = 1; i <= len; i++) {
        lua_rawgeti(L, 3, i);
        fputc((unsigned char)lua_tointeger(L, -1), f_out);
        lua_pop(L, 1);
      }
    } else {
      fwrite(content, 1, content_len, f_out);
    }
    fputc('\n', f_out);
  }
  
  fclose(f_in);
  fclose(f_out);
  
  /* 替换原文件 */
  remove(filename);
  rename(temp_filename, filename);
  
  lua_pushboolean(L, 1);
  return 1;
}


/**
 * 文件句柄的按行号读取
 * 功能描述：从文件句柄读取指定行号的内容，支持文本和二进制模式
 * 参数说明：line_num - 行号(从1开始), mode - 可选,"b"为二进制模式
 * 返回值说明：文本模式返回字符串，二进制模式返回字节table，失败返回nil
 */
static int f_readline_n (lua_State *L) {
  FILE *f = tofile(L);
  lua_Integer line_num = luaL_checkinteger(L, 2);
  const char *mode = luaL_optstring(L, 3, "t");
  int binary_mode = (mode[0] == 'b' || mode[0] == 'B');
  int current_line = 1;
  luaL_Buffer b;
  int c;
  long saved_pos;
  
  if (line_num < 1) {
    luaL_pushfail(L);
    lua_pushliteral(L, "行号必须大于0");
    return 2;
  }
  
  /* 保存当前位置 */
  saved_pos = ftell(f);
  rewind(f);
  
  /* 跳过前面的行 */
  while (current_line < line_num) {
    c = fgetc(f);
    if (c == EOF) {
      fseek(f, saved_pos, SEEK_SET);
      luaL_pushfail(L);
      return 1;
    }
    if (c == '\n') current_line++;
  }
  
  if (binary_mode) {
    /* 二进制模式：返回字节table */
    lua_newtable(L);
    int idx = 1;
    while ((c = fgetc(f)) != EOF && c != '\n') {
      lua_pushinteger(L, (unsigned char)c);
      lua_rawseti(L, -2, idx++);
    }
  } else {
    /* 文本模式：返回字符串 */
    luaL_buffinit(L, &b);
    while ((c = fgetc(f)) != EOF && c != '\n') {
      luaL_addchar(&b, c);
    }
    luaL_pushresult(&b);
  }
  
  /* 恢复位置 */
  fseek(f, saved_pos, SEEK_SET);
  return 1;
}


/**
 * 获取文件总行数
 * 功能描述：统计文件中的总行数
 * 参数说明：filename - 文件名
 * 返回值说明：返回行数整数
 */
static int io_linecount (lua_State *L) {
  const char *filename = luaL_checkstring(L, 1);
  FILE *f;
  int count = 0;
  int c;
  int has_content = 0;
  
  errno = 0;
  f = fopen(filename, "r");
  if (f == NULL) {
    return luaL_fileresult(L, 0, filename);
  }
  
  while ((c = fgetc(f)) != EOF) {
    has_content = 1;
    if (c == '\n') count++;
  }
  
  /* 如果文件有内容但最后一行没有换行符，也算一行 */
  if (has_content && c != '\n') count++;
  
  fclose(f);
  lua_pushinteger(L, count);
  return 1;
}


/**
 * 读取文件指定范围的行
 * 功能描述：读取文件中从start_line到end_line的所有行
 * 参数说明：filename - 文件名, start - 起始行(从1开始), end - 结束行, mode - 可选,"b"为二进制
 * 返回值说明：返回包含所有行的table，每行为字符串或字节table
 */
static int io_readlines_range (lua_State *L) {
  const char *filename = luaL_checkstring(L, 1);
  lua_Integer start_line = luaL_checkinteger(L, 2);
  lua_Integer end_line = luaL_checkinteger(L, 3);
  const char *mode = luaL_optstring(L, 4, "t");
  int binary_mode = (mode[0] == 'b' || mode[0] == 'B');
  FILE *f;
  int current_line = 1;
  int c;
  int result_idx = 1;
  luaL_Buffer b;
  
  if (start_line < 1) {
    luaL_pushfail(L);
    lua_pushliteral(L, "起始行号必须大于0");
    return 2;
  }
  if (end_line < start_line) {
    luaL_pushfail(L);
    lua_pushliteral(L, "结束行号必须大于等于起始行号");
    return 2;
  }
  
  errno = 0;
  f = fopen(filename, binary_mode ? "rb" : "r");
  if (f == NULL) {
    return luaL_fileresult(L, 0, filename);
  }
  
  /* 创建结果table */
  lua_newtable(L);
  
  /* 跳过前面的行 */
  while (current_line < start_line) {
    c = fgetc(f);
    if (c == EOF) {
      fclose(f);
      return 1;  /* 返回空table */
    }
    if (c == '\n') current_line++;
  }
  
  /* 读取范围内的所有行 */
  while (current_line <= end_line) {
    if (binary_mode) {
      /* 二进制模式：每行为字节table */
      lua_newtable(L);
      int byte_idx = 1;
      while ((c = fgetc(f)) != EOF && c != '\n') {
        lua_pushinteger(L, (unsigned char)c);
        lua_rawseti(L, -2, byte_idx++);
      }
      lua_rawseti(L, -2, result_idx++);
    } else {
      /* 文本模式：每行为字符串 */
      luaL_buffinit(L, &b);
      while ((c = fgetc(f)) != EOF && c != '\n') {
        luaL_addchar(&b, c);
      }
      luaL_pushresult(&b);
      lua_rawseti(L, -2, result_idx++);
    }
    
    if (c == EOF) break;
    current_line++;
  }
  
  fclose(f);
  return 1;
}


/**
 * 写入文件指定范围的行
 * 功能描述：替换文件中从start_line到end_line的所有行
 * 参数说明：filename - 文件名, start - 起始行, end - 结束行, lines - 行table, mode - 可选,"b"为二进制
 * 返回值说明：成功返回true，失败返回nil和错误信息
 */
static int io_writelines_range (lua_State *L) {
  const char *filename = luaL_checkstring(L, 1);
  lua_Integer start_line = luaL_checkinteger(L, 2);
  lua_Integer end_line = luaL_checkinteger(L, 3);
  luaL_checktype(L, 4, LUA_TTABLE);
  const char *mode = luaL_optstring(L, 5, "t");
  int binary_mode = (mode[0] == 'b' || mode[0] == 'B');
  FILE *f_in, *f_out;
  char *temp_filename;
  int current_line = 1;
  int c;
  size_t fname_len;
  lua_Integer lines_count;
  
  if (start_line < 1) {
    luaL_pushfail(L);
    lua_pushliteral(L, "起始行号必须大于0");
    return 2;
  }
  if (end_line < start_line) {
    luaL_pushfail(L);
    lua_pushliteral(L, "结束行号必须大于等于起始行号");
    return 2;
  }
  
  lines_count = luaL_len(L, 4);
  
  /* 创建临时文件名 */
  fname_len = strlen(filename);
  temp_filename = (char *)lua_newuserdata(L, fname_len + 5);
  strcpy(temp_filename, filename);
  strcat(temp_filename, ".tmp");
  
  errno = 0;
  f_in = fopen(filename, binary_mode ? "rb" : "r");
  f_out = fopen(temp_filename, binary_mode ? "wb" : "w");
  
  if (f_out == NULL) {
    if (f_in) fclose(f_in);
    return luaL_fileresult(L, 0, temp_filename);
  }
  
  if (f_in == NULL) {
    /* 文件不存在，创建新文件 */
    for (lua_Integer i = 1; i < start_line; i++) {
      fputc('\n', f_out);
    }
    /* 写入新行 */
    for (lua_Integer i = 1; i <= lines_count; i++) {
      lua_rawgeti(L, 4, i);
      if (binary_mode && lua_istable(L, -1)) {
        lua_Integer len = luaL_len(L, -1);
        for (lua_Integer j = 1; j <= len; j++) {
          lua_rawgeti(L, -1, j);
          fputc((unsigned char)lua_tointeger(L, -1), f_out);
          lua_pop(L, 1);
        }
      } else {
        size_t slen;
        const char *s = lua_tolstring(L, -1, &slen);
        if (s) fwrite(s, 1, slen, f_out);
      }
      fputc('\n', f_out);
      lua_pop(L, 1);
    }
    fclose(f_out);
    lua_pushboolean(L, 1);
    return 1;
  }
  
  /* 复制start_line之前的行 */
  while (current_line < start_line) {
    c = fgetc(f_in);
    if (c == EOF) break;
    fputc(c, f_out);
    if (c == '\n') current_line++;
  }
  
  /* 补充空行（如果原文件行数不够） */
  while (current_line < start_line) {
    fputc('\n', f_out);
    current_line++;
  }
  
  /* 写入新行 */
  for (lua_Integer i = 1; i <= lines_count; i++) {
    lua_rawgeti(L, 4, i);
    if (binary_mode && lua_istable(L, -1)) {
      lua_Integer len = luaL_len(L, -1);
      for (lua_Integer j = 1; j <= len; j++) {
        lua_rawgeti(L, -1, j);
        fputc((unsigned char)lua_tointeger(L, -1), f_out);
        lua_pop(L, 1);
      }
    } else {
      size_t slen;
      const char *s = lua_tolstring(L, -1, &slen);
      if (s) fwrite(s, 1, slen, f_out);
    }
    fputc('\n', f_out);
    lua_pop(L, 1);
  }
  
  /* 跳过原文件中被替换的行 */
  while (current_line <= end_line) {
    c = fgetc(f_in);
    if (c == EOF) break;
    if (c == '\n') current_line++;
  }
  
  /* 复制剩余的行 */
  while ((c = fgetc(f_in)) != EOF) {
    fputc(c, f_out);
  }
  
  fclose(f_in);
  fclose(f_out);
  
  /* 替换原文件 */
  remove(filename);
  rename(temp_filename, filename);
  
  lua_pushboolean(L, 1);
  return 1;
}


/*
** functions for 'io' library
*/
static void serialize_value (lua_State *L, FILE *f, int depth) {
  if (depth > 10) {
    fprintf(f, "[...]\n");
    return;
  }
  
  switch (lua_type(L, -1)) {
    case LUA_TNIL:
      fprintf(f, "nil\n");
      break;
    
    case LUA_TBOOLEAN:
      fprintf(f, lua_toboolean(L, -1) ? "true\n" : "false\n");
      break;
    
    case LUA_TNUMBER:
      if (lua_isinteger(L, -1))
        fprintf(f, LUA_INTEGER_FMT "\n", (LUAI_UACINT)lua_tointeger(L, -1));
      else
        fprintf(f, LUA_NUMBER_FMT "\n", (LUAI_UACNUMBER)lua_tonumber(L, -1));
      break;
    
    case LUA_TSTRING:
      {
        size_t len;
        const char *s = lua_tolstring(L, -1, &len);
        fprintf(f, "\"");
        fwrite(s, sizeof(char), len, f);
        fprintf(f, "\"\n");
      }
      break;
    
    case LUA_TTABLE:
      {
        fprintf(f, "{\n");
        lua_pushnil(L);
        while (lua_next(L, -2)) {
          for (int i = 0; i < depth + 1; i++) fprintf(f, "  ");
          serialize_value(L, f, depth + 1);
          fprintf(f, " = ");
          serialize_value(L, f, depth + 1);
        }
        for (int i = 0; i < depth; i++) fprintf(f, "  ");
        fprintf(f, "}\n");
      }
      break;
    
    default:
      fprintf(f, "%s\n", luaL_typename(L, -1));
      break;
  }
}

static int io_saveall (lua_State *L) {
  const char *filename = luaL_checkstring(L, 1);
  FILE *f = fopen(filename, "w");
  if (f == NULL) {
    return luaL_fileresult(L, 0, filename);
  }
  
  int n = lua_gettop(L);
  for (int i = 2; i <= n; i++) {
    serialize_value(L, f, 0);
  }
  
  fclose(f);
  lua_pushboolean(L, 1);
  return 1;
}

#ifndef _WIN32
static int io_mmap (lua_State *L) {
  void *addr;
  if (lua_isnoneornil(L, 1))
    addr = NULL;
  else {
    luaL_checktype(L, 1, LUA_TLIGHTUSERDATA);
    addr = lua_touserdata(L, 1);
  }
  size_t len = (size_t)luaL_checkinteger(L, 2);
  int prot = (int)luaL_optinteger(L, 3, PROT_READ);
  int flags = (int)luaL_optinteger(L, 4, MAP_PRIVATE);
  
  int fd = -1;
  if (!lua_isnoneornil(L, 5)) {
    if (lua_isnumber(L, 5)) {
      fd = (int)luaL_checkinteger(L, 5);
    } else {
      LStream *p = (LStream *)luaL_checkudata(L, 5, LUA_FILEHANDLE);
      if (l_unlikely(isclosed(p)))
        luaL_error(L, "bad file handle");
      fd = fileno(p->f);
    }
  }
  
  off_t offset = (off_t)luaL_optinteger(L, 6, 0);
  
  void *result = mmap(addr, len, prot, flags, fd, offset);
  if (result == MAP_FAILED) {
    lua_pushnil(L);
    lua_pushfstring(L, "mmap failed: %s", strerror(errno));
    return 2;
  }
  
  lua_pushlightuserdata(L, result);
  return 1;
}

static int io_munmap (lua_State *L) {
  luaL_checktype(L, 1, LUA_TLIGHTUSERDATA);
  void *addr = lua_touserdata(L, 1);
  size_t len = (size_t)luaL_checkinteger(L, 2);
  
  if (munmap(addr, len) != 0) {
    lua_pushboolean(L, 0);
    lua_pushfstring(L, "munmap failed: %s", strerror(errno));
    return 2;
  }
  
  lua_pushboolean(L, 1);
  return 1;
}
#endif

static const luaL_Reg iolib[] = {
  {"close", io_close},
  {"flush", io_flush},
  {"input", io_input},
  {"linecount", io_linecount},
  {"lines", io_lines},
#ifndef _WIN32
  {"mmap", io_mmap},
  {"munmap", io_munmap},
#endif
  {"open", io_open},
  {"output", io_output},
  {"popen", io_popen},
  {"read", io_read},
  {"readline", io_readline_n},
  {"saveall", io_saveall},
  {"tmpfile", io_tmpfile},
  {"type", io_type},
  {"write", io_write},
  {"writeline", io_writeline_n},
  {"readlines", io_readlines_range},
  {"writelines", io_writelines_range},
  {NULL, NULL}
};


/*
** methods for file handles
*/
static const luaL_Reg meth[] = {
  {"read", f_read},
  {"readline", f_readline_n},
  {"write", f_write},
  {"lines", f_lines},
  {"flush", f_flush},
  {"seek", f_seek},
  {"close", f_close},
  {"setvbuf", f_setvbuf},
  {NULL, NULL}
};


/*
** metamethods for file handles
*/
static const luaL_Reg metameth[] = {
  {"__index", NULL},  /* placeholder */
  {"__gc", f_gc},
  {"__close", f_gc},
  {"__tostring", f_tostring},
  {NULL, NULL}
};


static void createmeta (lua_State *L) {
  luaL_newmetatable(L, LUA_FILEHANDLE);  /* metatable for file handles */
  luaL_setfuncs(L, metameth, 0);  /* add metamethods to new metatable */
  luaL_newlibtable(L, meth);  /* create method table */
  luaL_setfuncs(L, meth, 0);  /* add file methods to method table */
  lua_setfield(L, -2, "__index");  /* metatable.__index = method table */
  lua_pop(L, 1);  /* pop metatable */
}


/*
** function to (not) close the standard files stdin, stdout, and stderr
*/
static int io_noclose (lua_State *L) {
  LStream *p = tolstream(L);
  p->closef = &io_noclose;  /* keep file opened */
  luaL_pushfail(L);
  lua_pushliteral(L, "cannot close standard file");
  return 2;
}


static void createstdfile (lua_State *L, FILE *f, const char *k,
                           const char *fname) {
  LStream *p = newprefile(L);
  p->f = f;
  p->closef = &io_noclose;
  if (k != NULL) {
    lua_pushvalue(L, -1);
    lua_setfield(L, LUA_REGISTRYINDEX, k);  /* add file to registry */
  }
  lua_setfield(L, -2, fname);  /* add file to module */
}


LUAMOD_API int luaopen_io (lua_State *L) {
  luaL_newlib(L, iolib);  /* new module */
  createmeta(L);
  
#ifndef _WIN32
  lua_pushinteger(L, PROT_READ); lua_setfield(L, -2, "PROT_READ");
  lua_pushinteger(L, PROT_WRITE); lua_setfield(L, -2, "PROT_WRITE");
  lua_pushinteger(L, PROT_EXEC); lua_setfield(L, -2, "PROT_EXEC");
  lua_pushinteger(L, PROT_NONE); lua_setfield(L, -2, "PROT_NONE");
  
  lua_pushinteger(L, MAP_SHARED); lua_setfield(L, -2, "MAP_SHARED");
  lua_pushinteger(L, MAP_PRIVATE); lua_setfield(L, -2, "MAP_PRIVATE");
  lua_pushinteger(L, MAP_ANONYMOUS); lua_setfield(L, -2, "MAP_ANONYMOUS");
  lua_pushinteger(L, MAP_FIXED); lua_setfield(L, -2, "MAP_FIXED");
  lua_pushinteger(L, MAP_FIXED_NOREPLACE); lua_setfield(L, -2, "MAP_FIXED_NOREPLACE");
#endif
  
  lua_pushinteger(L, SEEK_SET); lua_setfield(L, -2, "SEEK_SET");
  lua_pushinteger(L, SEEK_CUR); lua_setfield(L, -2, "SEEK_CUR");
  lua_pushinteger(L, SEEK_END); lua_setfield(L, -2, "SEEK_END");
  
  /* create (and set) default files */
  createstdfile(L, stdin, IO_INPUT, "stdin");
  createstdfile(L, stdout, IO_OUTPUT, "stdout");
  createstdfile(L, stderr, NULL, "stderr");
  return 1;
}

