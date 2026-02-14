#define lbigint_c
#define LUA_CORE

#include "lprefix.h"

#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#include "lua.h"
#include "lobject.h"
#include "lstate.h"
#include "lgc.h"
#include "lmem.h"
#include "lbigint.h"
#include "lstring.h"
#include "lvm.h"
#include "ldebug.h"

#define BIGINT_BASE_BITS 32
#define BIGINT_BASE ((l_uint64)1 << BIGINT_BASE_BITS)

TBigInt *luaB_new(lua_State *L, unsigned int len) {
  size_t size = sizeof(TBigInt) + (len > 0 ? (len - 1) : 0) * sizeof(l_uint32);
  GCObject *o = luaC_newobj(L, LUA_VNUMBIG, size);
  TBigInt *b = gco2big(o);
  b->len = len;
  b->sign = 1;
  memset(b->buff, 0, len * sizeof(l_uint32));
  return b;
}

static void big_normalize(TBigInt *b) {
  while (b->len > 0 && b->buff[b->len - 1] == 0) {
    b->len--;
  }
  if (b->len == 0) b->sign = 1; /* Zero is always positive */
}

/* Copies src to dst (allocated). dst must be large enough. */
static void big_copy(TBigInt *dst, const TBigInt *src) {
  dst->sign = src->sign;
  dst->len = src->len;
  if (src->len > 0)
    memcpy(dst->buff, src->buff, src->len * sizeof(l_uint32));
}

void luaB_fromint(lua_State *L, lua_Integer i, TValue *res) {
  TBigInt *b = luaB_new(L, 2); /* 64-bit int fits in 2 32-bit limbs */
  if (i < 0) {
    b->sign = -1;
    /* Handle min integer specially or just negate */
    if (i == LUA_MININTEGER) {
        /* -(-2^63) = 2^63. 0x8000...000 */
        /* 2^63 in base 2^32 is: low=0, high=0x80000000 */
        b->buff[0] = 0;
        b->buff[1] = 0x80000000;
        b->len = 2;
        setbigvalue(L, res, b);
        return;
    }
    i = -i;
  } else {
    b->sign = 1;
  }

  l_uint64 ui = (l_uint64)i;
  b->buff[0] = (l_uint32)(ui & 0xFFFFFFFF);
  b->buff[1] = (l_uint32)(ui >> 32);
  big_normalize(b);
  setbigvalue(L, res, b);
}

/* Helper: Get a BigInt view of a TValue.
   If it's an integer, allocate a temporary BigInt, push it to stack, and return it.
   Returns NULL if conversion fails.
   'pushes' is incremented if a value is pushed to stack.
*/
static TBigInt *to_bigint(lua_State *L, TValue *v, int *pushes) {
  if (ttisbigint(v)) {
    return bigvalue(v);
  } else if (ttisinteger(v)) {
    TBigInt *b = luaB_new(L, 2);
    setbigvalue(L, s2v(L->top.p), b);
    L->top.p++; /* anchor */
    (*pushes)++;

    lua_Integer i = ivalue(v);
    if (i < 0) {
      b->sign = -1;
      if (i == LUA_MININTEGER) {
        b->buff[0] = 0; b->buff[1] = 0x80000000; b->len = 2;
        return b;
      }
      i = -i;
    } else {
      b->sign = 1;
    }
    b->buff[0] = (l_uint32)((l_uint64)i & 0xFFFFFFFF);
    b->buff[1] = (l_uint32)((l_uint64)i >> 32);
    big_normalize(b);
    return b;
  }
  return NULL;
}

/* Compare magnitudes. Returns -1 if |a| < |b|, 0 if equal, 1 if |a| > |b| */
static int cmp_abs(const TBigInt *a, const TBigInt *b) {
  if (a->len != b->len) return a->len < b->len ? -1 : 1;
  for (int i = a->len - 1; i >= 0; i--) {
    if (a->buff[i] != b->buff[i]) return a->buff[i] < b->buff[i] ? -1 : 1;
  }
  return 0;
}

int luaB_compare(TValue *v1, TValue *v2) {
  /* Assumes v1 and v2 are BigInt or Integer */
  /* We can't easily use to_bigint here because we don't have L for allocation if needed.
     But wait, if we are in luaB_compare, one of them MUST be BigInt (dispatch logic).
     Actually we generally need L to allocate temporary bigints for comparison if mixed.
     But we can do "virtual" comparison without allocation. */

  int s1, s2;
  unsigned int l1, l2;
  l_uint32 buf1[2], buf2[2]; /* For integer conversion */
  const l_uint32 *d1, *d2;

  if (ttisbigint(v1)) {
    TBigInt *b = bigvalue(v1);
    s1 = b->sign; l1 = b->len; d1 = b->buff;
  } else {
    lua_Integer i = ivalue(v1);
    if (i < 0) { s1 = -1; if (i!=LUA_MININTEGER) i = -i; } else s1 = 1;
    if (i == 0) l1 = 0;
    else if (i == LUA_MININTEGER) { buf1[0]=0; buf1[1]=0x80000000; l1=2; }
    else {
        buf1[0] = (l_uint32)i; buf1[1] = (l_uint32)(i>>32);
        l1 = (buf1[1] != 0) ? 2 : 1;
    }
    d1 = buf1;
  }

  if (ttisbigint(v2)) {
    TBigInt *b = bigvalue(v2);
    s2 = b->sign; l2 = b->len; d2 = b->buff;
  } else {
    lua_Integer i = ivalue(v2);
    if (i < 0) { s2 = -1; if (i!=LUA_MININTEGER) i = -i; } else s2 = 1;
    if (i == 0) l2 = 0;
    else if (i == LUA_MININTEGER) { buf2[0]=0; buf2[1]=0x80000000; l2=2; }
    else {
        buf2[0] = (l_uint32)i; buf2[1] = (l_uint32)(i>>32);
        l2 = (buf2[1] != 0) ? 2 : 1;
    }
    d2 = buf2;
  }

  if (s1 != s2) return s1 < s2 ? -1 : 1;

  int cmp = 0;
  if (l1 != l2) cmp = l1 < l2 ? -1 : 1;
  else {
    for (int i = l1 - 1; i >= 0; i--) {
      if (d1[i] != d2[i]) { cmp = d1[i] < d2[i] ? -1 : 1; break; }
    }
  }

  return s1 == 1 ? cmp : -cmp;
}

/* dst = |a| + |b| */
static void add_abs(TBigInt *dst, const TBigInt *a, const TBigInt *b) {
  unsigned int len = a->len > b->len ? a->len : b->len;
  l_uint64 carry = 0;
  for (unsigned int i = 0; i < len || carry; i++) {
    l_uint64 sum = carry;
    if (i < a->len) sum += a->buff[i];
    if (i < b->len) sum += b->buff[i];
    dst->buff[i] = (l_uint32)sum;
    carry = sum >> 32;
  }
  dst->len = len + (carry ? 1 : 0);
  if (dst->buff[dst->len-1] == 0 && dst->len > 0) big_normalize(dst); /* Should generally not happen in add unless 0 */
  else {
      /* Re-check len if carry caused extension */
      if (dst->buff[len] != 0) dst->len = len + 1;
      else dst->len = len;
  }
  big_normalize(dst);
}

/* dst = |a| - |b|. Assumes |a| >= |b| */
static void sub_abs(TBigInt *dst, const TBigInt *a, const TBigInt *b) {
  long long borrow = 0;
  for (unsigned int i = 0; i < a->len; i++) {
    long long diff = (long long)a->buff[i] - borrow;
    if (i < b->len) diff -= b->buff[i];

    if (diff < 0) {
      diff += BIGINT_BASE;
      borrow = 1;
    } else {
      borrow = 0;
    }
    dst->buff[i] = (l_uint32)diff;
  }
  dst->len = a->len;
  big_normalize(dst);
}

void luaB_add(lua_State *L, TValue *v1, TValue *v2, TValue *res) {
  int pushes = 0;
  TBigInt *b1 = to_bigint(L, v1, &pushes);
  TBigInt *b2 = to_bigint(L, v2, &pushes);

  if (b1 && b2) {
    unsigned int max_len = (b1->len > b2->len ? b1->len : b2->len) + 1;
    TBigInt *r = luaB_new(L, max_len);

    if (b1->sign == b2->sign) {
      add_abs(r, b1, b2);
      r->sign = b1->sign;
    } else {
      int cmp = cmp_abs(b1, b2);
      if (cmp >= 0) {
        sub_abs(r, b1, b2);
        r->sign = b1->sign;
      } else {
        sub_abs(r, b2, b1);
        r->sign = b2->sign;
      }
    }
    setbigvalue(L, res, r);
  } else {
    setnilvalue(res);
  }
  L->top.p -= pushes;
}

void luaB_sub(lua_State *L, TValue *v1, TValue *v2, TValue *res) {
  int pushes = 0;
  TBigInt *b1 = to_bigint(L, v1, &pushes);
  TBigInt *b2 = to_bigint(L, v2, &pushes);

  if (b1 && b2) {
    unsigned int max_len = (b1->len > b2->len ? b1->len : b2->len) + 1;
    TBigInt *r = luaB_new(L, max_len);

    if (b1->sign != b2->sign) {
      add_abs(r, b1, b2);
      r->sign = b1->sign;
    } else {
      int cmp = cmp_abs(b1, b2);
      if (cmp >= 0) {
        sub_abs(r, b1, b2);
        r->sign = b1->sign;
      } else {
        sub_abs(r, b2, b1);
        r->sign = -b1->sign;
      }
    }
    setbigvalue(L, res, r);
  } else {
    setnilvalue(res);
  }
  L->top.p -= pushes;
}

void luaB_mul(lua_State *L, TValue *v1, TValue *v2, TValue *res) {
  int pushes = 0;
  TBigInt *b1 = to_bigint(L, v1, &pushes);
  TBigInt *b2 = to_bigint(L, v2, &pushes);

  if (b1 && b2) {
    if (b1->len == 0 || b2->len == 0) {
        TBigInt *r = luaB_new(L, 0);
        setbigvalue(L, res, r);
        L->top.p -= pushes;
        return;
    }
    unsigned int len = b1->len + b2->len;
    TBigInt *r = luaB_new(L, len);

    for (unsigned int i = 0; i < b1->len; i++) {
      l_uint64 carry = 0;
      for (unsigned int j = 0; j < b2->len; j++) {
        l_uint64 tmp = (l_uint64)b1->buff[i] * b2->buff[j] + r->buff[i + j] + carry;
        r->buff[i + j] = (l_uint32)tmp;
        carry = tmp >> 32;
      }
      r->buff[i + b2->len] = (l_uint32)carry;
    }

    r->sign = b1->sign * b2->sign;
    big_normalize(r);
    setbigvalue(L, res, r);
  } else {
    setnilvalue(res);
  }
  L->top.p -= pushes;
}

void luaB_tostring(lua_State *L, TValue *obj) {
  if (!ttisbigint(obj)) return;
  ptrdiff_t offset = savestack(L, obj); /* Save stack offset */
  TBigInt *b = bigvalue(obj);

  if (b->len == 0) {
    setsvalue(L, obj, luaS_newliteral(L, "0"));
    return;
  }

  /* Convert to decimal string */
  /* Estimate size: 10 digits per 32 bits is safe (2^32 approx 4e9) */
  int estimated = b->len * 10 + 2;
  char *buff = luaM_newvector(L, estimated, char);
  int pos = estimated - 1;
  buff[pos] = '\0';

  /* Work on a copy since we modify it */
  TBigInt *temp = luaB_new(L, b->len);
  setbigvalue(L, s2v(L->top.p), temp); /* Anchor temp */
  L->top.p++;

  /* Reload b because allocation might have invalidated pointer if it triggered GC?
     luaB_new uses luaC_newobj -> no GC check.
     But let's be safe and reload 'b' from 'obj' which we can restore.
  */
  obj = s2v(restorestack(L, offset));
  b = bigvalue(obj);
  big_copy(temp, b);

  while (temp->len > 0) {
    /* Divide by 10, keep remainder */
    l_uint64 rem = 0;
    for (int i = temp->len - 1; i >= 0; i--) {
      l_uint64 cur = temp->buff[i] + (rem << 32);
      temp->buff[i] = (l_uint32)(cur / 10);
      rem = cur % 10;
    }
    big_normalize(temp);
    buff[--pos] = (char)('0' + rem);
  }

  if (b->sign < 0) buff[--pos] = '-';

  TString *s = luaS_new(L, buff + pos);
  obj = s2v(restorestack(L, offset));
  setsvalue(L, obj, s);

  luaM_freearray(L, buff, estimated);
  L->top.p--; /* Pop temp */
}

/* Stubs for others */
void luaB_div(lua_State *L, TValue *v1, TValue *v2, TValue *res) {
  luaG_runerror(L, "BigInt division not supported");
}

void luaB_mod(lua_State *L, TValue *v1, TValue *v2, TValue *res) {
  luaG_runerror(L, "BigInt modulus not supported");
}

void luaB_pow(lua_State *L, TValue *v1, TValue *v2, TValue *res) {
  luaG_runerror(L, "BigInt power not supported");
}

int luaB_tryconvert(lua_State *L, TValue *obj) {
    return 0;
}

lua_Number luaB_bigtonumber(const TValue *obj) {
    if (!ttisbigint(obj)) return 0.0;
    TBigInt *b = bigvalue(obj);
    if (b->len == 0) return 0.0;

    lua_Number res = 0.0;
    /* Process from most significant limb */
    for (int i = b->len - 1; i >= 0; i--) {
        res = res * 4294967296.0 + (lua_Number)b->buff[i];
    }

    if (b->sign < 0) res = -res;
    return res;
}
