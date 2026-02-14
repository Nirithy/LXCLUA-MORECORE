#define lbigint_c
#define LUA_CORE

#include "lprefix.h"

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <math.h>

#include "lua.h"
#include "lobject.h"
#include "lstate.h"
#include "lgc.h"
#include "lmem.h"
#include "lbigint.h"
#include "lstring.h"
#include "lvm.h"
#include "ldebug.h"
#include "ldo.h"

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

TBigFloat *luaB_newbigfloat(lua_State *L, unsigned int len) {
  size_t size = sizeof(TBigFloat) + (len > 0 ? (len - 1) : 0) * sizeof(l_uint32);
  GCObject *o = luaC_newobj(L, LUA_VNUMFLTBIG, size);
  TBigFloat *b = gco2bigflt(o);
  b->len = len;
  b->sign = 1;
  b->exp = 0;
  memset(b->buff, 0, len * sizeof(l_uint32));
  return b;
}

static void big_normalize(TBigInt *b) {
  while (b->len > 0 && b->buff[b->len - 1] == 0) {
    b->len--;
  }
  if (b->len == 0) b->sign = 1; /* Zero is always positive */
}

static void bigflt_normalize(TBigFloat *b) {
  /* Remove leading zeros (MSBs) */
  while (b->len > 0 && b->buff[b->len - 1] == 0) {
    b->len--;
  }
  if (b->len == 0) {
      b->sign = 1; /* Zero is always positive */
      b->exp = 0;  /* Canonical zero */
      return;
  }

  /* Remove trailing zeros (LSBs) to canonicalize. */
  /* If buff[0] == 0, we can divide by 2^32 (shift right by 1 limb) and increase exp by log10(2^32)? */
  /* No, base is 10^exp. Mantissa is base 2^32. */
  /* Removing LSB 0s changes value by / 2^32. */
  /* We cannot easily adjust base-10 exponent by removing base-2^32 zeros unless they align. */
  /* However, we can remove factors of 10 if we implement division by 10. */
  /* That's expensive. Let's just keep as is for now, but maybe ensure string conversion handles it? */
  /* Actually, to fix 500...00e-450 -> 5e-401, we need to divide mantissa by 10^k and add k to exp. */
  /* Doing this on every operation might be slow. */
  /* But let's try to do it for small factors (e.g. trailing zeros in decimal representation of mantissa). */
  /* For now, just leave it. The main fix for display is in tostring. */
}

/* Copies src to dst (allocated). dst must be large enough. */
static void big_copy(TBigInt *dst, const TBigInt *src) {
  dst->sign = src->sign;
  dst->len = src->len;
  if (src->len > 0)
    memcpy(dst->buff, src->buff, src->len * sizeof(l_uint32));
}

static void bigflt_copy(TBigFloat *dst, const TBigFloat *src) {
  dst->sign = src->sign;
  dst->len = src->len;
  dst->exp = src->exp;
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

static TBigFloat *to_bigfloat(lua_State *L, TValue *v, int *pushes) {
  if (ttisbigfloat(v)) {
    return bigfltvalue(v);
  }
  TBigFloat *bf = luaB_newbigfloat(L, 1);
  /* anchor */
  setbigfltvalue(L, s2v(L->top.p), bf);
  L->top.p++;
  (*pushes)++;

  if (ttisinteger(v)) {
     /* int -> BigFloat (exp 0) */
     lua_Integer i = ivalue(v);
     if (i == 0) { bf->len = 0; bf->sign = 1; bf->exp = 0; return bf; }
     if (i < 0) { bf->sign = -1; if (i!=LUA_MININTEGER) i = -i; } else bf->sign = 1;
     if (i == LUA_MININTEGER) {
         bf->buff[0] = 0;
         /* Need more space? newbigfloat allocs size for len. */
         /* But we allocated len=1. Realloc needed? */
         /* 2 limbs for 64bit. */
         /* We should alloc 2 initially or realloc. */
         /* Let's realloc or alloc 2 safe. */
         /* Helper: assume max 2. */
         /* Actually, luaB_newbigfloat(L, 2) is safer default for int. */
         /* But we already allocated. */
         /* Let's just create a new one with len 2 and replace. */
         TBigFloat *bf2 = luaB_newbigfloat(L, 2);
         bf2->sign = -1; bf2->exp = 0;
         bf2->buff[0] = 0; bf2->buff[1] = 0x80000000;
         setbigfltvalue(L, s2v(L->top.p - 1), bf2);
         return bf2;
     }
     bf->buff[0] = (l_uint32)(i & 0xFFFFFFFF);
     if ((i >> 32) != 0) {
         /* Need 2 limbs */
         TBigFloat *bf2 = luaB_newbigfloat(L, 2);
         bf2->sign = bf->sign; bf2->exp = 0;
         bf2->buff[0] = bf->buff[0];
         bf2->buff[1] = (l_uint32)(i >> 32);
         setbigfltvalue(L, s2v(L->top.p - 1), bf2);
         return bf2;
     }
     bf->len = 1;
     bf->exp = 0;
  } else if (ttisbigint(v)) {
     TBigInt *bi = bigvalue(v);
     TBigFloat *bf2 = luaB_newbigfloat(L, bi->len);
     setbigfltvalue(L, s2v(L->top.p - 1), bf2); /* replace dummy */
     bf2->sign = bi->sign;
     bf2->len = bi->len;
     bf2->exp = 0;
     if (bi->len > 0) memcpy(bf2->buff, bi->buff, bi->len * sizeof(l_uint32));
     return bf2;
  } else if (ttisfloat(v)) {
     /* double -> BigFloat */
     char buff[64];
     sprintf(buff, "%.17g", fltvalue(v));
     luaB_str2bigfloat(L, buff, s2v(L->top.p - 1));
     return bigfltvalue(s2v(L->top.p - 1));
  } else {
     L->top.p--;
     (*pushes)--;
     return NULL;
  }
  return bigfltvalue(s2v(L->top.p - 1));
}

static void big_mul_raw(l_uint32 *dst, const l_uint32 *a, unsigned int alen,
                    const l_uint32 *b, unsigned int blen) {
    memset(dst, 0, (alen + blen) * sizeof(l_uint32));
    for (unsigned int i = 0; i < alen; i++) {
      l_uint64 carry = 0;
      for (unsigned int j = 0; j < blen; j++) {
        l_uint64 tmp = (l_uint64)a[i] * b[j] + dst[i + j] + carry;
        dst[i + j] = (l_uint32)tmp;
        carry = tmp >> 32;
      }
      dst[i + blen] = (l_uint32)carry;
    }
}

static TBigFloat *bigflt_scale(lua_State *L, TBigFloat *b, lua_Integer scale_diff) {
    /* Returns b * 10^scale_diff. scale_diff > 0. */

    if (scale_diff == 0) {
        TBigFloat *res = luaB_newbigfloat(L, b->len);
        bigflt_copy(res, b);
        return res;
    }

    /* Compute 10^k */
    /* 10^k has approx k * log2(10) / 32 limbs. log2(10)~3.32. k/9.6 limbs. */
    unsigned int est_len = (unsigned int)(scale_diff / 9) + 2;
    l_uint32 *pow_buff = luaM_newvector(L, est_len, l_uint32);
    unsigned int pow_len = 1;
    pow_buff[0] = 1;

    TBigInt *base = luaB_new(L, 1);
    base->buff[0] = 10; base->len = 1; base->sign = 1;
    setbigvalue(L, s2v(L->top.p), base); L->top.p++; /* Anchor */

    TBigInt *res_pow = luaB_new(L, 1);
    res_pow->buff[0] = 1; res_pow->len = 1; res_pow->sign = 1;
    setbigvalue(L, s2v(L->top.p), res_pow); L->top.p++; /* Anchor */

    lua_Integer exp = scale_diff;

    while (exp > 0) {
        if (exp & 1) {
            /* res = res * base */
            unsigned int rlen = res_pow->len + base->len;
            TBigInt *nr = luaB_new(L, rlen);
            big_mul_raw(nr->buff, res_pow->buff, res_pow->len, base->buff, base->len);
            nr->sign = 1;
            big_normalize(nr);
            /* Replace res_pow on stack */
            setbigvalue(L, s2v(L->top.p - 1), nr);
            res_pow = nr;
        }
        exp >>= 1;
        if (exp > 0) {
            /* base = base * base */
            unsigned int rlen = base->len + base->len;
            TBigInt *nb = luaB_new(L, rlen);
            big_mul_raw(nb->buff, base->buff, base->len, base->buff, base->len);
            nb->sign = 1;
            big_normalize(nb);
            setbigvalue(L, s2v(L->top.p - 2), nb); /* base is at top-2 */
            base = nb;
        }
    }

    /* Multiply b->mantissa * res_pow */
    unsigned int fin_len = b->len + res_pow->len;
    TBigFloat *final_res = luaB_newbigfloat(L, fin_len);
    big_mul_raw(final_res->buff, b->buff, b->len, res_pow->buff, res_pow->len);
    final_res->sign = b->sign;
    final_res->exp = b->exp;

    bigflt_normalize(final_res);

    L->top.p -= 2; /* Pop base, res_pow */
    return final_res;
}

int luaB_compare(lua_State *L, TValue *v1, TValue *v2) {
  /* Copy inputs to protect against stack realloc */
  TValue k1 = *v1; TValue k2 = *v2;

  if (ttisbigfloat(&k1) || ttisbigfloat(&k2)) {
      luaD_checkstack(L, 6); /* ensure stack space for pushes */

      int pushes = 0;
      TBigFloat *f1 = to_bigfloat(L, &k1, &pushes);
      TBigFloat *f2 = to_bigfloat(L, &k2, &pushes);
      int res = 0;
      if (f1 && f2) {
          if (f1->sign != f2->sign) {
              res = f1->sign < f2->sign ? -1 : 1;
          } else {
              int sign = f1->sign;
              lua_Integer e1 = f1->exp;
              lua_Integer e2 = f2->exp;

              double mag1 = f1->len * 32.0 + e1 * 3.3219;
              double mag2 = f2->len * 32.0 + e2 * 3.3219;

              if (fabs(mag1 - mag2) > 64) {
                  if (mag1 > mag2) res = sign;
                  else res = -sign;
              } else {
                  lua_Integer min_e = e1 < e2 ? e1 : e2;
                  TBigFloat *bf1 = f1;
                  TBigFloat *bf2 = f2;

                  if (e1 > min_e) bf1 = bigflt_scale(L, f1, e1 - min_e);
                  if (e2 > min_e) bf2 = bigflt_scale(L, f2, e2 - min_e);

                  if (bf1 != f1) { setbigfltvalue(L, s2v(L->top.p), bf1); L->top.p++; pushes++; }
                  if (bf2 != f2) { setbigfltvalue(L, s2v(L->top.p), bf2); L->top.p++; pushes++; }

                  int cmp = 0;
                  if (bf1->len != bf2->len) cmp = bf1->len < bf2->len ? -1 : 1;
                  else {
                      for (int i = bf1->len - 1; i >= 0; i--) {
                          if (bf1->buff[i] != bf2->buff[i]) {
                              cmp = bf1->buff[i] < bf2->buff[i] ? -1 : 1;
                              break;
                          }
                      }
                  }

                  if (sign == 1) res = cmp;
                  else res = -cmp;
              }
          }
      }
      L->top.p -= pushes;
      return res;
  }

  /* Assumes v1 and v2 are BigInt or Integer */
  int s1, s2;
  unsigned int l1, l2;
  l_uint32 buf1[2], buf2[2];
  const l_uint32 *d1, *d2;

  if (ttisbigint(&k1)) {
    TBigInt *b = bigvalue(&k1);
    s1 = b->sign; l1 = b->len; d1 = b->buff;
  } else {
    lua_Integer i = ivalue(&k1);
    if (i < 0) { s1 = -1; if (i!=LUA_MININTEGER) i = -i; } else s1 = 1;
    if (i == 0) l1 = 0;
    else if (i == LUA_MININTEGER) { buf1[0]=0; buf1[1]=0x80000000; l1=2; }
    else {
        buf1[0] = (l_uint32)i; buf1[1] = (l_uint32)(i>>32);
        l1 = (buf1[1] != 0) ? 2 : 1;
    }
    d1 = buf1;
  }

  if (ttisbigint(&k2)) {
    TBigInt *b = bigvalue(&k2);
    s2 = b->sign; l2 = b->len; d2 = b->buff;
  } else {
    lua_Integer i = ivalue(&k2);
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

static void add_abs_raw(l_uint32 *dst, unsigned int *dlen,
                        const l_uint32 *a, unsigned int alen,
                        const l_uint32 *b, unsigned int blen) {
  unsigned int len = alen > blen ? alen : blen;
  l_uint64 carry = 0;
  for (unsigned int i = 0; i < len || carry; i++) {
    l_uint64 sum = carry;
    if (i < alen) sum += a[i];
    if (i < blen) sum += b[i];
    dst[i] = (l_uint32)sum;
    carry = sum >> 32;
  }
  *dlen = len + (carry ? 1 : 0);
  while (*dlen > 0 && dst[*dlen - 1] == 0) (*dlen)--;
}

static void sub_abs_raw(l_uint32 *dst, unsigned int *dlen,
                        const l_uint32 *a, unsigned int alen,
                        const l_uint32 *b, unsigned int blen) {
  long long borrow = 0;
  for (unsigned int i = 0; i < alen; i++) {
    long long diff = (long long)a[i] - borrow;
    if (i < blen) diff -= b[i];

    if (diff < 0) {
      diff += BIGINT_BASE;
      borrow = 1;
    } else {
      borrow = 0;
    }
    dst[i] = (l_uint32)diff;
  }
  *dlen = alen;
  while (*dlen > 0 && dst[*dlen - 1] == 0) (*dlen)--;
}

static void add_abs(TBigInt *dst, const TBigInt *a, const TBigInt *b) {
    add_abs_raw(dst->buff, &dst->len, a->buff, a->len, b->buff, b->len);
    if (dst->len == 0) dst->sign = 1;
}

static void sub_abs(TBigInt *dst, const TBigInt *a, const TBigInt *b) {
    sub_abs_raw(dst->buff, &dst->len, a->buff, a->len, b->buff, b->len);
    if (dst->len == 0) dst->sign = 1;
}

static void luaB_add_bigfloat(lua_State *L, TValue *v1, TValue *v2, TValue *res) {
      int pushes = 0;
      TBigFloat *f1 = to_bigfloat(L, v1, &pushes);
      TBigFloat *f2 = to_bigfloat(L, v2, &pushes);
      if (f1 && f2) {
          lua_Integer e1 = f1->exp;
          lua_Integer e2 = f2->exp;
          lua_Integer min_e = e1 < e2 ? e1 : e2;
          TBigFloat *bf1 = f1;
          TBigFloat *bf2 = f2;

          if (e1 > min_e) bf1 = bigflt_scale(L, f1, e1 - min_e);
          if (e2 > min_e) bf2 = bigflt_scale(L, f2, e2 - min_e);

          if (bf1 != f1) { setbigfltvalue(L, s2v(L->top.p), bf1); L->top.p++; pushes++; }
          if (bf2 != f2) { setbigfltvalue(L, s2v(L->top.p), bf2); L->top.p++; pushes++; }

          unsigned int max_len = (bf1->len > bf2->len ? bf1->len : bf2->len) + 1;
          TBigFloat *r = luaB_newbigfloat(L, max_len);
          r->exp = min_e;

          if (bf1->sign == bf2->sign) {
              add_abs_raw(r->buff, &r->len, bf1->buff, bf1->len, bf2->buff, bf2->len);
              r->sign = bf1->sign;
          } else {
              int cmp = 0;
              if (bf1->len != bf2->len) cmp = bf1->len < bf2->len ? -1 : 1;
              else {
                  for (int i = bf1->len - 1; i >= 0; i--) {
                      if (bf1->buff[i] != bf2->buff[i]) {
                          cmp = bf1->buff[i] < bf2->buff[i] ? -1 : 1;
                          break;
                      }
                  }
              }

              if (cmp >= 0) {
                  sub_abs_raw(r->buff, &r->len, bf1->buff, bf1->len, bf2->buff, bf2->len);
                  r->sign = bf1->sign;
              } else {
                  sub_abs_raw(r->buff, &r->len, bf2->buff, bf2->len, bf1->buff, bf1->len);
                  r->sign = bf2->sign;
              }
          }
          bigflt_normalize(r);
          setbigfltvalue(L, res, r);
      } else {
          setnilvalue(res);
      }
      L->top.p -= pushes;
}

void luaB_add(lua_State *L, TValue *v1, TValue *v2, TValue *res) {
  /* Copy inputs and protect res */
  TValue k1 = *v1; TValue k2 = *v2;
  ptrdiff_t res_off = savestack(L, res);
  luaD_checkstack(L, 6);
  res = s2v(restorestack(L, res_off));

  if (ttisbigfloat(&k1) || ttisbigfloat(&k2)) {
      luaB_add_bigfloat(L, &k1, &k2, res);
      return;
  }

  int pushes = 0;
  TBigInt *b1 = to_bigint(L, &k1, &pushes);
  TBigInt *b2 = to_bigint(L, &k2, &pushes);

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
    /* Fallback to BigFloat if inputs are numbers but not BigInt compatible (e.g. double) */
    if (ttisnumber(&k1) && ttisnumber(&k2)) {
        L->top.p -= pushes;
        luaB_add_bigfloat(L, &k1, &k2, res);
        return;
    }
    setnilvalue(res);
  }
  L->top.p -= pushes;
}

static void luaB_sub_bigfloat(lua_State *L, TValue *v1, TValue *v2, TValue *res) {
      int pushes = 0;
      TBigFloat *f1 = to_bigfloat(L, v1, &pushes);
      TBigFloat *f2 = to_bigfloat(L, v2, &pushes);
      if (f1 && f2) {
          lua_Integer e1 = f1->exp;
          lua_Integer e2 = f2->exp;
          lua_Integer min_e = e1 < e2 ? e1 : e2;
          TBigFloat *bf1 = f1;
          TBigFloat *bf2 = f2;

          if (e1 > min_e) bf1 = bigflt_scale(L, f1, e1 - min_e);
          if (e2 > min_e) bf2 = bigflt_scale(L, f2, e2 - min_e);

          if (bf1 != f1) { setbigfltvalue(L, s2v(L->top.p), bf1); L->top.p++; pushes++; }
          if (bf2 != f2) { setbigfltvalue(L, s2v(L->top.p), bf2); L->top.p++; pushes++; }

          unsigned int max_len = (bf1->len > bf2->len ? bf1->len : bf2->len) + 1;
          TBigFloat *r = luaB_newbigfloat(L, max_len);
          r->exp = min_e;

          if (bf1->sign != bf2->sign) {
              add_abs_raw(r->buff, &r->len, bf1->buff, bf1->len, bf2->buff, bf2->len);
              r->sign = bf1->sign;
          } else {
              int cmp = 0;
              if (bf1->len != bf2->len) cmp = bf1->len < bf2->len ? -1 : 1;
              else {
                  for (int i = bf1->len - 1; i >= 0; i--) {
                      if (bf1->buff[i] != bf2->buff[i]) {
                          cmp = bf1->buff[i] < bf2->buff[i] ? -1 : 1;
                          break;
                      }
                  }
              }
              if (cmp >= 0) {
                  sub_abs_raw(r->buff, &r->len, bf1->buff, bf1->len, bf2->buff, bf2->len);
                  r->sign = bf1->sign;
              } else {
                  sub_abs_raw(r->buff, &r->len, bf2->buff, bf2->len, bf1->buff, bf1->len);
                  r->sign = -bf1->sign;
              }
          }
          bigflt_normalize(r);
          setbigfltvalue(L, res, r);
      } else {
          setnilvalue(res);
      }
      L->top.p -= pushes;
}

void luaB_sub(lua_State *L, TValue *v1, TValue *v2, TValue *res) {
  TValue k1 = *v1; TValue k2 = *v2;
  ptrdiff_t res_off = savestack(L, res);
  luaD_checkstack(L, 6);
  res = s2v(restorestack(L, res_off));

  if (ttisbigfloat(&k1) || ttisbigfloat(&k2)) {
      luaB_sub_bigfloat(L, &k1, &k2, res);
      return;
  }

  int pushes = 0;
  TBigInt *b1 = to_bigint(L, &k1, &pushes);
  TBigInt *b2 = to_bigint(L, &k2, &pushes);

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
    if (ttisnumber(&k1) && ttisnumber(&k2)) {
        L->top.p -= pushes;
        luaB_sub_bigfloat(L, &k1, &k2, res);
        return;
    }
    setnilvalue(res);
  }
  L->top.p -= pushes;
}

static void luaB_mul_bigfloat(lua_State *L, TValue *v1, TValue *v2, TValue *res) {
      int pushes = 0;
      TBigFloat *f1 = to_bigfloat(L, v1, &pushes);
      TBigFloat *f2 = to_bigfloat(L, v2, &pushes);
      if (f1 && f2) {
          if (f1->len == 0 || f2->len == 0) {
              TBigFloat *r = luaB_newbigfloat(L, 0);
              setbigfltvalue(L, res, r);
          } else {
              unsigned int len = f1->len + f2->len;
              TBigFloat *r = luaB_newbigfloat(L, len);
              big_mul_raw(r->buff, f1->buff, f1->len, f2->buff, f2->len);
              r->sign = f1->sign * f2->sign;
              r->exp = f1->exp + f2->exp;
              bigflt_normalize(r);
              setbigfltvalue(L, res, r);
          }
      } else {
          setnilvalue(res);
      }
      L->top.p -= pushes;
}

void luaB_mul(lua_State *L, TValue *v1, TValue *v2, TValue *res) {
  TValue k1 = *v1; TValue k2 = *v2;
  ptrdiff_t res_off = savestack(L, res);
  luaD_checkstack(L, 6);
  res = s2v(restorestack(L, res_off));

  if (ttisbigfloat(&k1) || ttisbigfloat(&k2)) {
      luaB_mul_bigfloat(L, &k1, &k2, res);
      return;
  }

  int pushes = 0;
  TBigInt *b1 = to_bigint(L, &k1, &pushes);
  TBigInt *b2 = to_bigint(L, &k2, &pushes);

  if (b1 && b2) {
    if (b1->len == 0 || b2->len == 0) {
        TBigInt *r = luaB_new(L, 0);
        setbigvalue(L, res, r);
        L->top.p -= pushes;
        return;
    }
    unsigned int len = b1->len + b2->len;
    TBigInt *r = luaB_new(L, len);

    big_mul_raw(r->buff, b1->buff, b1->len, b2->buff, b2->len);

    r->sign = b1->sign * b2->sign;
    big_normalize(r);
    setbigvalue(L, res, r);
  } else {
    if (ttisnumber(&k1) && ttisnumber(&k2)) {
        L->top.p -= pushes;
        luaB_mul_bigfloat(L, &k1, &k2, res);
        return;
    }
    setnilvalue(res);
  }
  L->top.p -= pushes;
}

static void bigflt_mul_10_add(lua_State *L, TBigFloat **bp, int digit, TValue *anchor_slot) {
  /* *bp = *bp * 10 + digit. */
  TBigFloat *b = *bp;
  l_uint64 carry = digit;
  for (unsigned int i = 0; i < b->len; i++) {
    l_uint64 val = (l_uint64)b->buff[i] * 10 + carry;
    b->buff[i] = (l_uint32)val;
    carry = val >> 32;
  }

  if (carry > 0) {
      /* Extend */
      TBigFloat *nb = luaB_newbigfloat(L, b->len + 1);
      bigflt_copy(nb, b);
      nb->len = b->len + 1;
      nb->buff[b->len] = (l_uint32)carry;
      *bp = nb;
      /* Anchor immediately */
      setbigfltvalue(L, anchor_slot, nb);
  }
}

void luaB_str2bigfloat(lua_State *L, const char *s, TValue *res) {
    while (*s == ' ') s++;
    int sign = 1;
    if (*s == '-') { sign = -1; s++; }
    else if (*s == '+') { s++; }

    TBigFloat *b = luaB_newbigfloat(L, 1);
    setbigfltvalue(L, res, b);

    lua_Integer exp = 0;
    int has_dot = 0;

    while (*s) {
        if (*s >= '0' && *s <= '9') {
            int d = *s - '0';
            TBigFloat *curr = bigfltvalue(res);
            bigflt_mul_10_add(L, &curr, d, res);
            if (has_dot) exp--;
        } else if (*s == '.') {
            if (has_dot) break;
            has_dot = 1;
        } else if (*s == 'e' || *s == 'E') {
            s++;
            int e_sign = 1;
            if (*s == '-') { e_sign = -1; s++; }
            else if (*s == '+') { s++; }

            lua_Integer e_val = 0;
            while (*s >= '0' && *s <= '9') {
                e_val = e_val * 10 + (*s - '0');
                s++;
            }
            exp += e_sign * e_val;
            break;
        } else {
            break;
        }
        s++;
    }

    b = bigfltvalue(res);
    b->sign = sign;
    b->exp = exp;
    bigflt_normalize(b);
}

void luaB_tostring_prec(lua_State *L, const TValue *obj, int precision, TValue *res) {
    /* Formats BigFloat/BigInt/etc to string with precision decimals */
    if (ttisbigfloat(obj)) {
        TBigFloat *b = bigfltvalue(obj);
        if (b->len == 0) {
           setsvalue(L, res, luaS_newliteral(L, "0"));
           return;
        }

        /* 1. Get exact decimal string of mantissa */
        int estimated = b->len * 10 + 2;
        char *buff = luaM_newvector(L, estimated, char);
        int pos = estimated - 1;
        buff[pos] = '\0';

        TBigFloat *temp = luaB_newbigfloat(L, b->len);
        setbigfltvalue(L, s2v(L->top.p), temp); L->top.p++; /* Anchor */
        bigflt_copy(temp, b);

        while (temp->len > 0) {
            l_uint64 rem = 0;
            for (int i = temp->len - 1; i >= 0; i--) {
                l_uint64 cur = temp->buff[i] + (rem << 32);
                temp->buff[i] = (l_uint32)(cur / 10);
                rem = cur % 10;
            }
            bigflt_normalize(temp);
            buff[--pos] = (char)('0' + rem);
        }
        L->top.p--; /* Pop temp */

        char *mant_str = buff + pos;
        size_t mant_len = strlen(mant_str);
        lua_Integer exp = b->exp;

        /* Value is: mant_str * 10^exp */
        /* Example: mant="123", exp=-1 -> 12.3 */
        /* We want fixed point format with 'precision' digits. */

        /* decimal_point_pos relative to end of string */
        /* value = integer_part . fractional_part */
        /* mant * 10^exp */
        /* decimal point is at offset 'exp' from end of string. */

        /* Construct a large buffer for formatted output */
        /* If exp >= 0, just append zeros. */
        /* If exp < 0, insert dot. */

        /* Let's construct a full string representation first */
        /* Max length? */
        /* If exp is -400, we need "0.000...(399)...123" */
        /* If precision is small, we truncate. */
        /* If precision is large, we pad. */

        /* Case 1: exp >= 0. Integer. */
        /* Result: mant_str + "0"*exp + "." + "0"*prec */

        char *out_buff;
        size_t out_size;

        if (exp >= 0) {
            size_t zeros = (size_t)exp;
            size_t total_len = (b->sign<0?1:0) + mant_len + zeros + (precision > 0 ? 1 + precision : 0);
            out_buff = luaM_newvector(L, total_len + 1, char);
            char *p = out_buff;
            if (b->sign < 0) *p++ = '-';
            memcpy(p, mant_str, mant_len); p += mant_len;
            memset(p, '0', zeros); p += zeros;
            if (precision > 0) {
                *p++ = '.';
                memset(p, '0', precision); p += precision;
            }
            *p = '\0';
            setsvalue(L, res, luaS_new(L, out_buff));
            luaM_freearray(L, out_buff, total_len + 1);
        } else {
            /* exp < 0 */
            /* dec point is -exp digits from the right of mant_str */
            /* exp = -1, mant="123" -> 12.3 */
            /* exp = -3, mant="123" -> 0.123 */
            /* exp = -5, mant="123" -> 0.00123 */

            lua_Integer decimal_pos = mant_len + exp;
            /* decimal_pos is number of digits before dot */

            /* We need to generate integer part and fractional part */

            /* Check if we just have 0.something */
            int needs_leading_zeros = (decimal_pos <= 0);
            size_t leading_zeros_count = needs_leading_zeros ? (size_t)(-decimal_pos) : 0;

            /* Integer part: */
            /* If decimal_pos > 0, it's mant_str[0..decimal_pos] */
            /* Else it is "0" */

            /* Fractional part: */
            /* All remaining digits + maybe padding zeros */

            /* We construct the string "int.frac" then truncate/pad to 'precision' */

            /* Estimated max len? */
            /* int part len + 1 + precision */

            /* Integer part length */
            size_t int_part_len = (decimal_pos > 0) ? (size_t)decimal_pos : 1; /* "0" */

            size_t total_len = (b->sign<0?1:0) + int_part_len + (precision > 0 ? 1 + precision : 0);
            out_buff = luaM_newvector(L, total_len + 1, char);
            char *p = out_buff;

            if (b->sign < 0) *p++ = '-';

            /* Write integer part */
            if (decimal_pos > 0) {
                memcpy(p, mant_str, decimal_pos);
                p += decimal_pos;
            } else {
                *p++ = '0';
            }

            if (precision > 0) {
                *p++ = '.';
                /* Write fractional part up to precision */
                /* Frac digits start at index: max(0, decimal_pos) in mant_str? */
                /* If decimal_pos = 2, mant="1234", frac="34" */
                /* If decimal_pos = -2, mant="12", leading 0s=2. Val=0.0012 */
                /* Frac part is "0012" */

                size_t written = 0;

                /* Leading zeros in frac? */
                if (decimal_pos < 0) {
                    size_t lz = (size_t)(-decimal_pos); /* e.g. 2 for 0.0012 */
                    size_t to_write = (lz < (size_t)precision) ? lz : (size_t)precision;
                    memset(p, '0', to_write);
                    p += to_write;
                    written += to_write;
                }

                if (written < (size_t)precision) {
                    /* Digits from mant_str */
                    size_t start_idx = (decimal_pos > 0) ? (size_t)decimal_pos : 0;
                    if (start_idx < mant_len) {
                        size_t avail = mant_len - start_idx;
                        size_t needed = (size_t)precision - written;
                        size_t to_write = (avail < needed) ? avail : needed;
                        memcpy(p, mant_str + start_idx, to_write);
                        p += to_write;
                        written += to_write;
                    }
                }

                if (written < (size_t)precision) {
                    /* Pad with trailing zeros */
                    size_t needed = (size_t)precision - written;
                    memset(p, '0', needed);
                    p += needed;
                }
            }
            *p = '\0';

            setsvalue(L, res, luaS_new(L, out_buff));
            luaM_freearray(L, out_buff, total_len + 1);
        }

        luaM_freearray(L, buff, estimated);
        return;
    }

    if (ttisbigint(obj)) {
        /* Integer formatted with precision. */
        /* Just format integer then append .000... */
        /* Use normal tostring then append */
        /* But we need to use a temp TValue for result of tostring? */
        /* No, we can reuse logic. */
        /* Just for simplicity, convert BigInt to string then append */

        TValue temp = *obj;
        TValue str_val;
        luaB_tostring(L, &temp); /* changes temp to string */
        /* Note: luaB_tostring might assume obj is on stack/anchored? */
        /* luaB_tostring implementation uses savestack/restorestack on 'obj'. */
        /* So 'temp' MUST be on stack? */
        /* No, luaB_tostring takes TValue*. But implementation does savestack(L, obj). */
        /* If obj is not in stack, savestack is undefined behavior or wrong. */
        /* Wait, lbigint.c: luaB_tostring: ptrdiff_t offset = savestack(L, obj); */
        /* This implies obj MUST be in the stack. */

        /* So we cannot simply call luaB_tostring on a local struct. */
        /* We must push it. */

        setobj2s(L, L->top.p, obj);
        L->top.p++;
        luaB_tostring(L, s2v(L->top.p - 1));
        /* Now top-1 is string */
        const char *s = getstr(tsvalue(s2v(L->top.p - 1)));

        if (precision > 0) {
            size_t slen = strlen(s);
            size_t total_len = slen + 1 + precision;
            char *buff = luaM_newvector(L, total_len + 1, char);
            memcpy(buff, s, slen);
            buff[slen] = '.';
            memset(buff + slen + 1, '0', precision);
            buff[total_len] = '\0';
            setsvalue(L, res, luaS_new(L, buff));
            luaM_freearray(L, buff, total_len + 1);
        } else {
            setsvalue(L, res, tsvalue(s2v(L->top.p - 1)));
        }
        L->top.p--;
        return;
    }
}

void luaB_tostring(lua_State *L, TValue *obj) {
  if (ttisbigfloat(obj)) {
      ptrdiff_t offset = savestack(L, obj);
      luaD_checkstack(L, 6); /* stack safety */
      /* obj might be invalid, use restorestack */
      obj = s2v(restorestack(L, offset));

      TBigFloat *b = bigfltvalue(obj);
      if (b->len == 0) {
         setsvalue(L, obj, luaS_newliteral(L, "0"));
         return;
      }

      int estimated = b->len * 10 + 2;
      char *buff = luaM_newvector(L, estimated, char);
      int pos = estimated - 1;
      buff[pos] = '\0';

      TBigFloat *temp = luaB_newbigfloat(L, b->len);
      setbigfltvalue(L, s2v(L->top.p), temp);
      L->top.p++;

      obj = s2v(restorestack(L, offset)); /* reload b/obj */
      b = bigfltvalue(obj);
      bigflt_copy(temp, b);

      while (temp->len > 0) {
        l_uint64 rem = 0;
        for (int i = temp->len - 1; i >= 0; i--) {
          l_uint64 cur = temp->buff[i] + (rem << 32);
          temp->buff[i] = (l_uint32)(cur / 10);
          rem = cur % 10;
        }
        bigflt_normalize(temp);
        buff[--pos] = (char)('0' + rem);
      }

      /* Normalize string output: Remove trailing zeros from mantissa and adjust exponent */
      char *start = buff + pos;
      size_t len = strlen(start);
      lua_Integer exp_adj = 0;
      while (len > 1 && start[len-1] == '0') {
          len--;
          exp_adj++;
      }
      start[len] = '\0'; /* Truncate zeros */

      lua_Integer final_exp = b->exp + exp_adj;

      char exp_buff[32];
      sprintf(exp_buff, "%lld", (long long)final_exp);
      size_t mant_len = len;
      size_t exp_len = strlen(exp_buff);
      size_t total_len = (b->sign < 0 ? 1 : 0) + mant_len + 1 + exp_len;

      char *final_buff = luaM_newvector(L, total_len + 1, char);
      char *p = final_buff;
      if (b->sign < 0) *p++ = '-';
      memcpy(p, start, mant_len); p += mant_len;
      *p++ = 'e';
      memcpy(p, exp_buff, exp_len); p += exp_len;
      *p = '\0';

      TString *s = luaS_newlstr(L, final_buff, total_len);

      obj = s2v(restorestack(L, offset));
      setsvalue(L, obj, s);

      luaM_freearray(L, final_buff, total_len + 1);
      luaM_freearray(L, buff, estimated);
      L->top.p--;
      return;
  }

  if (!ttisbigint(obj)) return;
  ptrdiff_t offset = savestack(L, obj);
  luaD_checkstack(L, 6);
  obj = s2v(restorestack(L, offset));

  TBigInt *b = bigvalue(obj);

  if (b->len == 0) {
    setsvalue(L, obj, luaS_newliteral(L, "0"));
    return;
  }

  int estimated = b->len * 10 + 2;
  char *buff = luaM_newvector(L, estimated, char);
  int pos = estimated - 1;
  buff[pos] = '\0';

  TBigInt *temp = luaB_new(L, b->len);
  setbigvalue(L, s2v(L->top.p), temp);
  L->top.p++;

  obj = s2v(restorestack(L, offset));
  b = bigvalue(obj);
  big_copy(temp, b);

  while (temp->len > 0) {
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
  L->top.p--;
}

static void big_div_raw(l_uint32 *q, l_uint32 *r,
                        const l_uint32 *u, unsigned int ulen,
                        const l_uint32 *v, unsigned int vlen) {
    /* Same as previous */
    memset(q, 0, ulen * sizeof(l_uint32));
    memset(r, 0, vlen * sizeof(l_uint32));

    int nbits = ulen * 32;
    while (nbits > 0 && (u[(nbits-1)/32] & (1U << ((nbits-1)%32))) == 0) nbits--;

    for (int i = nbits - 1; i >= 0; i--) {
        l_uint32 carry = 0;
        for (unsigned int j = 0; j < vlen; j++) {
            l_uint64 val = ((l_uint64)r[j] << 1) | carry;
            r[j] = (l_uint32)val;
            carry = (l_uint32)(val >> 32);
        }
        int u_bit = (u[i/32] >> (i%32)) & 1;
        r[0] |= u_bit;

        int ge = 0;
        if (carry) ge = 1;
        else {
            ge = 1;
            for (int j = vlen - 1; j >= 0; j--) {
                if (r[j] != v[j]) {
                    ge = r[j] > v[j];
                    break;
                }
            }
        }

        if (ge) {
            long long borrow = 0;
            for (unsigned int j = 0; j < vlen; j++) {
                long long diff = (long long)r[j] - borrow - v[j];
                if (diff < 0) {
                    diff += BIGINT_BASE;
                    borrow = 1;
                } else {
                    borrow = 0;
                }
                r[j] = (l_uint32)diff;
            }
            q[i/32] |= (1U << (i%32));
        }
    }
}

void luaB_div(lua_State *L, TValue *v1, TValue *v2, TValue *res) {
    TValue k1 = *v1; TValue k2 = *v2;
    ptrdiff_t res_off = savestack(L, res);
    luaD_checkstack(L, 6);
    res = s2v(restorestack(L, res_off));

    if (ttisnumber(&k1) && ttisnumber(&k2)) {
        int pushes = 0;
        TBigFloat *f1 = to_bigfloat(L, &k1, &pushes);
        TBigFloat *f2 = to_bigfloat(L, &k2, &pushes);
        if (f1 && f2) {
            if (f2->len == 0) {
                luaG_runerror(L, "attempt to divide by zero");
            }
            if (f1->len == 0) {
                setbigfltvalue(L, res, f1);
                L->top.p -= pushes;
                return;
            }

            unsigned int prec_limbs = (f1->len > f2->len ? f1->len : f2->len) + 4;
            if (prec_limbs < 4) prec_limbs = 4;

            int bits1 = f1->len * 32;
            int bits2 = f2->len * 32;
            int target_bits = prec_limbs * 32;
            int needed_bits = target_bits - bits1 + bits2;
            int k = 0;
            if (needed_bits > 0) {
                k = (int)(needed_bits * 0.30103) + 2;
            }

            TBigFloat *bf1 = f1;
            if (k > 0) {
                bf1 = bigflt_scale(L, f1, k);
                setbigfltvalue(L, s2v(L->top.p), bf1); L->top.p++; pushes++;
            }

            unsigned int qlen = bf1->len;
            TBigFloat *q = luaB_newbigfloat(L, qlen);

            l_uint32 *r_buff = luaM_newvector(L, f2->len, l_uint32);
            big_div_raw(q->buff, r_buff, bf1->buff, bf1->len, f2->buff, f2->len);
            luaM_freearray(L, r_buff, f2->len);

            q->sign = f1->sign * f2->sign;
            q->exp = f1->exp - f2->exp - k;

            bigflt_normalize(q);
            setbigfltvalue(L, res, q);
        } else {
            setnilvalue(res);
        }
        L->top.p -= pushes;
    } else {
        setnilvalue(res);
    }
}

void luaB_mod(lua_State *L, TValue *v1, TValue *v2, TValue *res) {
  /* Only BigInt support for now */
  TValue k1 = *v1; TValue k2 = *v2;
  ptrdiff_t res_off = savestack(L, res);
  luaD_checkstack(L, 6);
  res = s2v(restorestack(L, res_off));

  if (ttisbigint(&k1) && ttisbigint(&k2)) {
      TBigInt *b1 = bigvalue(&k1);
      TBigInt *b2 = bigvalue(&k2);
      if (b2->len == 0) luaG_runerror(L, "attempt to perform 'mod' by zero");

      TBigInt *q = luaB_new(L, b1->len);
      TBigInt *r = luaB_new(L, b2->len);
      setbigvalue(L, s2v(L->top.p), q); L->top.p++;
      setbigvalue(L, s2v(L->top.p), r); L->top.p++;

      big_div_raw(q->buff, r->buff, b1->buff, b1->len, b2->buff, b2->len);

      TBigInt *res_bi = r;
      res_bi->sign = b1->sign;
      big_normalize(res_bi);

      if (res_bi->len > 0 && res_bi->sign != b2->sign) {
          TBigInt *new_r = luaB_new(L, b2->len > res_bi->len ? b2->len : res_bi->len);
          sub_abs_raw(new_r->buff, &new_r->len, b2->buff, b2->len, res_bi->buff, res_bi->len);
          new_r->sign = b2->sign;
          setbigvalue(L, res, new_r);
      } else {
          setbigvalue(L, res, res_bi);
      }
      L->top.p -= 2;
      return;
  }

  luaG_runerror(L, "BigFloat modulus not supported yet");
}

void luaB_pow(lua_State *L, TValue *v1, TValue *v2, TValue *res) {
  /* Copy and protect */
  TValue k1 = *v1; TValue k2 = *v2;
  ptrdiff_t res_off = savestack(L, res);
  luaD_checkstack(L, 6);
  res = s2v(restorestack(L, res_off));

  lua_Integer exp;
  if (luaV_tointeger(&k2, &exp, F2Ieq)) {
      if (exp < 0) {
          luaG_runerror(L, "Negative power not implemented for BigInt (result is BigFloat)");
      }
      if (exp == 0) {
          if (ttisbigfloat(&k1)) {
              TBigFloat *r = luaB_newbigfloat(L, 1);
              r->buff[0] = 1; r->len = 1; r->sign = 1; r->exp = 0;
              setbigfltvalue(L, res, r);
          } else {
              TBigInt *r = luaB_new(L, 1);
              r->buff[0] = 1; r->len = 1; r->sign = 1;
              setbigvalue(L, res, r);
          }
          return;
      }

      int pushes = 0;
      /* k1 is input base. Use it directly or copy to temp slot? */
      /* k1 is local TValue. Not GCObject. */
      /* Base object might be pointed to by k1. */

      /* Initialize result */
      if (ttisbigfloat(&k1)) {
          TBigFloat *r = luaB_newbigfloat(L, 1);
          r->buff[0] = 1; r->len = 1; r->sign = 1; r->exp = 0;
          setbigfltvalue(L, s2v(L->top.p), r); L->top.p++; pushes++;
      } else {
          TBigInt *r = luaB_new(L, 1);
          r->buff[0] = 1; r->len = 1; r->sign = 1;
          setbigvalue(L, s2v(L->top.p), r); L->top.p++; pushes++;
      }

      /* Anchor base if needed? No, we just need to pass it to mul. */
      /* But mul takes TValue. */
      /* We can create a temp slot for base to use iterative mul? */
      /* Actually we can use local TValue k1. */

      TValue v_res; /* Local handle for result on stack */
      TValue *res_ptr = s2v(L->top.p - 1); /* Pointer to result on stack */

      /* We also need a slot for 'base' because it changes in loop (base = base*base) */
      setobj2s(L, L->top.p, &k1); L->top.p++; pushes++;
      TValue *base_ptr = s2v(L->top.p - 1);

      while (exp > 0) {
          if (exp & 1) {
              luaB_mul(L, res_ptr, base_ptr, res_ptr);
          }
          exp >>= 1;
          if (exp > 0) {
              luaB_mul(L, base_ptr, base_ptr, base_ptr);
          }
      }

      /* Move result to res */
      setobj(L, res, res_ptr);
      L->top.p -= pushes;
      return;
  }

  luaG_runerror(L, "BigInt/BigFloat power only supports integer exponents");
}

int luaB_tryconvert(lua_State *L, TValue *obj) {
    return 0;
}

lua_Number luaB_bigtonumber(const TValue *obj) {
    if (!ttisbigint(obj)) return 0.0;
    TBigInt *b = bigvalue(obj);
    if (b->len == 0) return 0.0;

    lua_Number res = 0.0;
    for (int i = b->len - 1; i >= 0; i--) {
        res = res * 4294967296.0 + (lua_Number)b->buff[i];
    }

    if (b->sign < 0) res = -res;
    return res;
}

lua_Number luaB_bigflttonumber(const TValue *obj) {
    if (!ttisbigfloat(obj)) return 0.0;
    TBigFloat *b = bigfltvalue(obj);
    if (b->len == 0) return 0.0;

    lua_Number res = 0.0;
    for (int i = b->len - 1; i >= 0; i--) {
        res = res * 4294967296.0 + (lua_Number)b->buff[i];
    }

    if (b->exp != 0) {
        res = res * pow(10.0, (double)b->exp);
    }

    if (b->sign < 0) res = -res;
    return res;
}
