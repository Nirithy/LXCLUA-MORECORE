/**
 * @file lzio.c
 * @brief Buffered streams implementation.
 *
 * This file contains functions for buffered input, used by the parser.
 */

#define lzio_c
#define LUA_CORE

#include "lprefix.h"


#include <string.h>

#include "lua.h"

#include "lapi.h"
#include "llimits.h"
#include "lmem.h"
#include "lstate.h"
#include "lzio.h"


/**
 * @brief Fills the buffer of the input stream.
 *
 * Calls the reader function to get more data.
 *
 * @param z The input stream.
 * @return The first character of the new buffer, or EOZ if end of stream.
 */
int luaZ_fill (ZIO *z) {
  size_t size;
  lua_State *L = z->L;
  const char *buff;
  lua_unlock(L);
  buff = z->reader(L, z->data, &size);
  lua_lock(L);
  if (buff == NULL || size == 0)
    return EOZ;
  z->n = size - 1;  /* discount char being returned */
  z->p = buff;
  return cast_uchar(*(z->p++));
}


/**
 * @brief Initializes a buffered input stream.
 *
 * @param L The Lua state.
 * @param z The input stream to initialize.
 * @param reader The reader function.
 * @param data User data for the reader.
 */
void luaZ_init (lua_State *L, ZIO *z, lua_Reader reader, void *data) {
  z->L = L;
  z->reader = reader;
  z->data = data;
  z->n = 0;
  z->p = NULL;
}


/* --------------------------------------------------------------- read --- */

static int checkbuffer (ZIO *z) {
  if (z->n == 0) {  /* no bytes in buffer? */
    if (luaZ_fill(z) == EOZ)  /* try to read more */
      return 0;  /* no more input */
    else {
      z->n++;  /* luaZ_fill consumed first byte; put it back */
      z->p--;
    }
  }
  return 1;  /* now buffer has something */
}


/**
 * @brief Reads `n` bytes from the input stream.
 *
 * @param z The input stream.
 * @param b Buffer to store the read bytes.
 * @param n Number of bytes to read.
 * @return The number of bytes read (0 if successful, non-zero if EOF).
 */
size_t luaZ_read (ZIO *z, void *b, size_t n) {
  while (n) {
    size_t m;
    if (!checkbuffer(z))
      return n;  /* no more input; return number of missing bytes */
    m = (n <= z->n) ? n : z->n;  /* min. between n and z->n */
    memcpy(b, z->p, m);
    z->n -= m;
    z->p += m;
    b = (char *)b + m;
    n -= m;
  }
  return 0;
}


/**
 * @brief Returns the address of the current buffer and ensures it has at least `n` bytes.
 *
 * @param z The input stream.
 * @param n The required number of bytes.
 * @return Pointer to the buffer, or NULL if there are not enough bytes.
 */
const void *luaZ_getaddr (ZIO* z, size_t n) {
  const void *res;
  if (!checkbuffer(z))
    return NULL;  /* no more input */
  if (z->n < n)  /* not enough bytes? */
    return NULL;  /* block not whole; cannot give an address */
  res = z->p;  /* get block address */
  z->n -= n;  /* consume these bytes */
  z->p += n;
  return res;
}
