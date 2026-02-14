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
#include "sha256.h"


/**
 * @brief Helper to derive key from timestamp (for ZIO decryption).
 */
static void nirithy_derive_key(uint64_t timestamp, uint8_t *key) {
  uint8_t input[32];
  uint8_t digest[SHA256_DIGEST_SIZE];

  memcpy(input, &timestamp, 8);
  memcpy(input + 8, "NirithySalt", 11);

  SHA256(input, 19, digest);
  memcpy(key, digest, 16); /* Use first 16 bytes as AES-128 key */
}

/**
 * @brief Initializes decryption state for ZIO.
 */
void luaZ_init_decrypt (ZIO *z, uint64_t timestamp, const uint8_t *iv) {
  uint8_t key[16];
  nirithy_derive_key(timestamp, key);
  AES_init_ctx_iv(&z->ctx, key, iv);
  z->keystream_idx = 16;
  z->encrypted = 1;
}

/**
 * @brief Reads and decrypts a byte from the stream.
 *
 * Should only be called via zgetc macro when z->encrypted is true.
 * Assumes z->n > 0.
 */
int luaZ_read_decrypt (ZIO *z) {
  uint8_t b = cast_uchar(*(z->p++));

  if (z->keystream_idx >= 16) {
    int i;
    /* Generate new keystream block */
    memcpy(z->keystream, z->ctx.Iv, 16);
    AES_ECB_encrypt(&z->ctx, z->keystream);

    /* Increment IV */
    for (i = 15; i >= 0; --i) {
      if (z->ctx.Iv[i] == 255) {
        z->ctx.Iv[i] = 0;
        continue;
      }
      z->ctx.Iv[i]++;
      break;
    }
    z->keystream_idx = 0;
  }

  b ^= z->keystream[z->keystream_idx++];
  return b;
}


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

  if (z->encrypted) {
    return luaZ_read_decrypt(z);
  }

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
  z->encrypted = 0;
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

    /* If encrypted, we must decrypt byte-by-byte (or block-by-block if optimized)
       Since our decryption logic is byte-based (AES-CTR), we can iterate.
       Optimization: decrypt directly into 'b' if possible?
       Our decrypt function reads from z->p and increments it.
    */
    if (z->encrypted) {
      uint8_t *dst = (uint8_t *)b;
      size_t i;
      m = (n <= z->n) ? n : z->n;

      for (i = 0; i < m; i++) {
        dst[i] = (uint8_t)luaZ_read_decrypt(z);
      }
      z->n -= m; /* n decremented by caller of read_decrypt? No, read_decrypt only decs p. */
                 /* wait, zgetc macro decs n. luaZ_read needs to dec n. */
                 /* luaZ_read_decrypt accesses z->p directly but DOES NOT decrement z->n. */
                 /* So we handle n here. */

      b = (char *)b + m;
      n -= m;
    } else {
      m = (n <= z->n) ? n : z->n;  /* min. between n and z->n */
      memcpy(b, z->p, m);
      z->n -= m;
      z->p += m;
      b = (char *)b + m;
      n -= m;
    }
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
