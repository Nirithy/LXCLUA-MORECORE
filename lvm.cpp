/**
 * @file lvm.c
 * @brief Lua virtual machine implementation.
 *
 * This file contains the implementation of the Lua virtual machine (VM).
 * It handles the execution of Lua bytecode instructions.
 */

#define lvm_c
#define LUA_CORE

#include "lprefix.h"

#include <float.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
** C23 feature detection
*/
#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 202311L
#define LUA_C23 1
#else
#define LUA_C23 0
#endif

/*
** C23 nullptr support
*/
#if LUA_C23
#define LUA_NULLPTR nullptr
#else
#define LUA_NULLPTR NULL
#endif


#include "lua.h"

#include "lapi.h"
#include "ldebug.h"
#include "ldo.h"
#include "lfunc.h"
#include "lgc.h"
#include "lobject.h"
#include "lopcodes.h"
#include "lstate.h"
#include "lstring.h"
#include "ltable.h"
#include "ltm.h"
#include "lvm.h"
#include "lclass.h"
#include "lobfuscate.h"
#include "lthread.h"
#include "lstruct.h"
#include "lnamespace.h"
#include "lbigint.h"

static int try_add(lua_Integer a, lua_Integer b, lua_Integer *r) {
    if ((b > 0 && a > LUA_MAXINTEGER - b) || (b < 0 && a < LUA_MININTEGER - b)) return 0;
    *r = a + b; return 1;
}
static int try_sub(lua_Integer a, lua_Integer b, lua_Integer *r) {
    if ((b < 0 && a > LUA_MAXINTEGER + b) || (b > 0 && a < LUA_MININTEGER + b)) return 0;
    *r = a - b; return 1;
}
static int try_mul(lua_Integer a, lua_Integer b, lua_Integer *r) {
    if (a == 0 || b == 0) { *r = 0; return 1; }
    if (a > 0 && b > 0 && a > LUA_MAXINTEGER / b) return 0;
    if (a > 0 && b < 0 && b < LUA_MININTEGER / a) return 0;
    if (a < 0 && b > 0 && a < LUA_MININTEGER / b) return 0;
    if (a < 0 && b < 0 && a < LUA_MAXINTEGER / b) return 0;
    *r = a * b; return 1;
}


/*
** By default, use jump tables in the main interpreter loop on gcc
** and compatible compilers.
*/
#if !defined(LUA_USE_JUMPTABLE)
#if defined(__GNUC__)
#define LUA_USE_JUMPTABLE	1
#else
#define LUA_USE_JUMPTABLE	0
#endif
#endif



/* limit for table tag-method chains (to avoid infinite loops) */
#define MAXTAGLOOP	2000


/*
** 'l_intfitsf' checks whether a given integer is in the range that
** can be converted to a float without rounding. Used in comparisons.
*/

/* number of bits in the mantissa of a float */
#define NBM		(l_floatatt(MANT_DIG))

/*
** Check whether some integers may not fit in a float, testing whether
** (maxinteger >> NBM) > 0. (That implies (1 << NBM) <= maxinteger.)
** (The shifts are done in parts, to avoid shifting by more than the size
** of an integer. In a worst case, NBM == 113 for long double and
** sizeof(long) == 32.)
*/
#if ((((LUA_MAXINTEGER >> (NBM / 4)) >> (NBM / 4)) >> (NBM / 4)) \
	>> (NBM - (3 * (NBM / 4))))  >  0

/* limit for integers that fit in a float */
#define MAXINTFITSF	((lua_Unsigned)1 << NBM)

/* check whether 'i' is in the interval [-MAXINTFITSF, MAXINTFITSF] */
#define l_intfitsf(i)	((MAXINTFITSF + l_castS2U(i)) <= (2 * MAXINTFITSF))

#else  /* all integers fit in a float precisely */

#define l_intfitsf(i)	1

#endif


/**
 * @brief Try to convert a value from string to a number value.
 *
 * If the value is not a string or is a string not representing
 * a valid numeral (or if coercions from strings to numbers
 * are disabled via macro 'cvt2num'), do not modify 'result'
 * and return 0.
 *
 * @param obj The object to convert.
 * @param result Pointer to store the result.
 * @return 1 on success, 0 on failure.
 */
static int l_strton (const TValue *obj, TValue *result) {
  lua_assert(obj != result);
  if (!cvt2num(obj))  /* is object not a string? */
    return 0;
  else {
    TString *st = tsvalue(obj);
    return (luaO_str2num(getstr(st), result) == tsslen(st) + 1);
  }
}


/**
 * @brief Tries to convert a value to a float.
 *
 * The float case is already handled by the macro 'tonumber'.
 *
 * @param obj The object to convert.
 * @param n Pointer to store the result.
 * @return 1 on success, 0 on failure.
 */
int luaV_tonumber_ (const TValue *obj, lua_Number *n) {
  TValue v;
  if (ttisinteger(obj)) {
    *n = cast_num(ivalue(obj));
    return 1;
  }
  else if (ttispointer(obj)) {
    *n = cast_num((L_P2I)ptrvalue(obj));
    return 1;
  }
  else if (l_strton(obj, &v)) {  /* string coercible to number? */
    *n = nvalue(&v);  /* convert result of 'luaO_str2num' to a float */
    return 1;
  }
  else
    return 0;  /* conversion failed */
}


/**
 * @brief Tries to convert a float to an integer, rounding according to 'mode'.
 *
 * @param n The float number.
 * @param p Pointer to store the result.
 * @param mode The rounding mode (F2Ieq, F2Ifloor, F2Iceil).
 * @return 1 on success, 0 on failure.
 */
int luaV_flttointeger (lua_Number n, lua_Integer *p, F2Imod mode) {
  lua_Number f = l_floor(n);
  if (n != f) {  /* not an integral value? */
    if (mode == F2Ieq) return 0;  /* fails if mode demands integral value */
    else if (mode == F2Iceil)  /* needs ceil? */
      f += 1;  /* convert floor to ceil (remember: n != f) */
  }
  return lua_numbertointeger(f, p);
}


/**
 * @brief Tries to convert a value to an integer, rounding according to 'mode', without string coercion.
 *
 * @param obj The object to convert.
 * @param p Pointer to store the result.
 * @param mode The rounding mode.
 * @return 1 on success, 0 on failure.
 */
int luaV_tointegerns (const TValue *obj, lua_Integer *p, F2Imod mode) {
  if (ttisfloat(obj))
    return luaV_flttointeger(fltvalue(obj), p, mode);
  else if (ttisinteger(obj)) {
    *p = ivalue(obj);
    return 1;
  }
  else if (ttispointer(obj)) {
    *p = cast(lua_Integer, (L_P2I)ptrvalue(obj));
    return 1;
  }
  else
    return 0;
}


/**
 * @brief Tries to convert a value to an integer.
 *
 * @param obj The object to convert.
 * @param p Pointer to store the result.
 * @param mode The rounding mode.
 * @return 1 on success, 0 on failure.
 */
int luaV_tointeger (const TValue *obj, lua_Integer *p, F2Imod mode) {
  TValue v;
  if (l_strton(obj, &v))  /* does 'obj' point to a numerical string? */
    obj = &v;  /* change it to point to its corresponding number */
  return luaV_tointegerns(obj, p, mode);
}


/**
 * @brief Try to convert a 'for' limit to an integer, preserving the semantics of the loop.
 *
 * Return true if the loop must not run; otherwise, '*p'
 * gets the integer limit.
 * (The following explanation assumes a positive step; it is valid for
 * negative steps mutatis mutandis.)
 * If the limit is an integer or can be converted to an integer,
 * rounding down, that is the limit.
 * Otherwise, check whether the limit can be converted to a float. If
 * the float is too large, clip it to LUA_MAXINTEGER.  If the float
 * is too negative, the loop should not run, because any initial
 * integer value is greater than such limit; so, the function returns
 * true to signal that. (For this latter case, no integer limit would be
 * correct; even a limit of LUA_MININTEGER would run the loop once for
 * an initial value equal to LUA_MININTEGER.)
 *
 * @param L The Lua state.
 * @param init The initial value.
 * @param lim The limit value.
 * @param p Pointer to store the integer limit.
 * @param step The step value.
 * @return 1 if the loop must not run, 0 otherwise.
 */
static int forlimit (lua_State *L, lua_Integer init, const TValue *lim,
                                   lua_Integer *p, lua_Integer step) {
  if (!luaV_tointeger(lim, p, (step < 0 ? F2Iceil : F2Ifloor))) {
    /* not coercible to in integer */
    lua_Number flim;  /* try to convert to float */
    if (!tonumber(lim, &flim)) /* cannot convert to float? */
      luaG_forerror(L, lim, "limit");
    /* else 'flim' is a float out of integer bounds */
    if (luai_numlt(0, flim)) {  /* if it is positive, it is too large */
      if (step < 0) return 1;  /* initial value must be less than it */
      *p = LUA_MAXINTEGER;  /* truncate */
    }
    else {  /* it is less than min integer */
      if (step > 0) return 1;  /* initial value must be greater than it */
      *p = LUA_MININTEGER;  /* truncate */
    }
  }
  return (step > 0 ? init > *p : init < *p);  /* not to run? */
}


/**
 * @brief Prepare a numerical for loop (opcode OP_FORPREP).
 *
 * Return true to skip the loop. Otherwise,
 * after preparation, stack will be as follows:
 *   ra : internal index (safe copy of the control variable)
 *   ra + 1 : loop counter (integer loops) or limit (float loops)
 *   ra + 2 : step
 *   ra + 3 : control variable
 *
 * @param L The Lua state.
 * @param ra The register base.
 * @return 1 to skip the loop, 0 otherwise.
 */
static int forprep (lua_State *L, StkId ra) {
  TValue *pinit = s2v(ra);
  TValue *plimit = s2v(ra + 1);
  TValue *pstep = s2v(ra + 2);
  if (ttisinteger(pinit) && ttisinteger(pstep)) { /* integer loop? */
    lua_Integer init = ivalue(pinit);
    lua_Integer step = ivalue(pstep);
    lua_Integer limit;
    if (step == 0)
      luaG_runerror(L, "'for' step is zero");
    setivalue(s2v(ra + 3), init);  /* control variable */
    if (forlimit(L, init, plimit, &limit, step))
      return 1;  /* skip the loop */
    else {  /* prepare loop counter */
      lua_Unsigned count;
      if (step > 0) {  /* ascending loop? */
        count = l_castS2U(limit) - l_castS2U(init);
        if (step != 1)  /* avoid division in the too common case */
          count /= l_castS2U(step);
      }
      else {  /* step < 0; descending loop */
        count = l_castS2U(init) - l_castS2U(limit);
        /* 'step+1' avoids negating 'mininteger' */
        count /= l_castS2U(-(step + 1)) + 1u;
      }
      /* store the counter in place of the limit (which won't be
         needed anymore) */
      setivalue(plimit, l_castU2S(count));
    }
  }
  else {  /* try making all values floats */
    lua_Number init; lua_Number limit; lua_Number step;
    if (l_unlikely(!tonumber(plimit, &limit)))
      luaG_forerror(L, plimit, "limit");
    if (l_unlikely(!tonumber(pstep, &step)))
      luaG_forerror(L, pstep, "step");
    if (l_unlikely(!tonumber(pinit, &init)))
      luaG_forerror(L, pinit, "initial value");
    if (step == 0)
      luaG_runerror(L, "'for' step is zero");
    if (luai_numlt(0, step) ? luai_numlt(limit, init)
                            : luai_numlt(init, limit))
      return 1;  /* skip the loop */
    else {
      /* make sure internal values are all floats */
      setfltvalue(plimit, limit);
      setfltvalue(pstep, step);
      setfltvalue(s2v(ra), init);  /* internal index */
      setfltvalue(s2v(ra + 3), init);  /* control variable */
    }
  }
  return 0;
}


/**
 * @brief Execute a step of a float numerical for loop.
 *
 * Returns true iff the loop must continue. (The integer case is
 * written online with opcode OP_FORLOOP, for performance.)
 *
 * @param ra The register base.
 * @return 1 if the loop must continue, 0 otherwise.
 */
static int floatforloop (StkId ra) {
  lua_Number step = fltvalue(s2v(ra + 2));
  lua_Number limit = fltvalue(s2v(ra + 1));
  lua_Number idx = fltvalue(s2v(ra));  /* internal index */
  idx = luai_numadd(L, idx, step);  /* increment index */
  if (luai_numlt(0, step) ? luai_numle(idx, limit)
                          : luai_numle(limit, idx)) {
    chgfltvalue(s2v(ra), idx);  /* update internal index */
    setfltvalue(s2v(ra + 3), idx);  /* and control variable */
    return 1;  /* jump back */
  }
  else
    return 0;  /* finish the loop */
}


static int luaV_ptr_read(lua_State *L, const void *p, const char *key_str, StkId val) {
  switch (key_str[0]) {
    case 'i':
      if (strcmp(key_str, "int") == 0 || strcmp(key_str, "i32") == 0 || strcmp(key_str, "int32") == 0) {
        setivalue(s2v(val), *(const int*)p); return 1;
      }
      if (strcmp(key_str, "i16") == 0 || strcmp(key_str, "int16") == 0) {
        setivalue(s2v(val), *(const short*)p); return 1;
      }
      if (strcmp(key_str, "i8") == 0 || strcmp(key_str, "int8") == 0) {
        setivalue(s2v(val), *(const char*)p); return 1;
      }
      if (strcmp(key_str, "i64") == 0 || strcmp(key_str, "int64") == 0) {
        setivalue(s2v(val), *(const long*)p); return 1;
      }
      break;
    case 'u':
      if (strcmp(key_str, "uint") == 0 || strcmp(key_str, "u32") == 0 || strcmp(key_str, "uint32") == 0 || strcmp(key_str, "unsigned int") == 0) {
        setivalue(s2v(val), *(const unsigned int*)p); return 1;
      }
      if (strcmp(key_str, "u8") == 0 || strcmp(key_str, "uint8") == 0 || strcmp(key_str, "uchar") == 0 || strcmp(key_str, "unsigned char") == 0) {
        setivalue(s2v(val), *(const unsigned char*)p); return 1;
      }
      if (strcmp(key_str, "u16") == 0 || strcmp(key_str, "uint16") == 0 || strcmp(key_str, "ushort") == 0 || strcmp(key_str, "unsigned short") == 0) {
        setivalue(s2v(val), *(const unsigned short*)p); return 1;
      }
      if (strcmp(key_str, "ulong") == 0 || strcmp(key_str, "u64") == 0 || strcmp(key_str, "uint64") == 0 || strcmp(key_str, "unsigned long") == 0) {
        setivalue(s2v(val), (lua_Integer)*(const unsigned long*)p); return 1;
      }
      break;
    case 'f':
      if (strcmp(key_str, "float") == 0 || strcmp(key_str, "f32") == 0) {
        setfltvalue(s2v(val), *(const float*)p); return 1;
      }
      if (strcmp(key_str, "f64") == 0) {
        setfltvalue(s2v(val), *(const double*)p); return 1;
      }
      break;
    case 'd':
      if (strcmp(key_str, "double") == 0) {
        setfltvalue(s2v(val), *(const double*)p); return 1;
      }
      break;
    case 'c':
      if (strcmp(key_str, "char") == 0) {
        setivalue(s2v(val), *(const char*)p); return 1;
      }
      if (strcmp(key_str, "cstr") == 0) {
        const char *s = *(const char**)p;
        if (s == NULL) setnilvalue(s2v(val));
        else setsvalue(L, s2v(val), luaS_new(L, s));
        return 1;
      }
      break;
    case 'b':
      if (strcmp(key_str, "byte") == 0) {
        setivalue(s2v(val), *(const unsigned char*)p); return 1;
      }
      break;
    case 's':
      if (strcmp(key_str, "short") == 0) {
        setivalue(s2v(val), *(const short*)p); return 1;
      }
      if (strcmp(key_str, "size_t") == 0) {
        setivalue(s2v(val), (lua_Integer)*(const size_t*)p); return 1;
      }
      if (strcmp(key_str, "str") == 0) {
        const char *s = *(const char**)p;
        if (s == NULL) setnilvalue(s2v(val));
        else setsvalue(L, s2v(val), luaS_new(L, s));
        return 1;
      }
      break;
    case 'l':
      if (strcmp(key_str, "long") == 0) {
        setivalue(s2v(val), *(const long*)p); return 1;
      }
      break;
    case 'p':
      if (strcmp(key_str, "ptr") == 0 || strcmp(key_str, "pointer") == 0) {
        setptrvalue(s2v(val), *(void**)p); return 1;
      }
      break;
  }
  return 0;
}

static int luaV_ptr_write(lua_State *L, void *p, const char *key_str, TValue *val) {
  lua_Integer i;
  lua_Number n;
  switch (key_str[0]) {
    case 'i':
      if (strcmp(key_str, "int") == 0 || strcmp(key_str, "i32") == 0 || strcmp(key_str, "int32") == 0) {
        if (tointegerns(val, &i)) *(int*)p = (int)i; else luaG_runerror(L, "expected integer"); return 1;
      }
      if (strcmp(key_str, "i16") == 0 || strcmp(key_str, "int16") == 0) {
        if (tointegerns(val, &i)) *(short*)p = (short)i; else luaG_runerror(L, "expected integer"); return 1;
      }
      if (strcmp(key_str, "i8") == 0 || strcmp(key_str, "int8") == 0) {
        if (tointegerns(val, &i)) *(char*)p = (char)i; else luaG_runerror(L, "expected integer"); return 1;
      }
      if (strcmp(key_str, "i64") == 0 || strcmp(key_str, "int64") == 0) {
        if (tointegerns(val, &i)) *(long*)p = (long)i; else luaG_runerror(L, "expected integer"); return 1;
      }
      break;
    case 'u':
      if (strcmp(key_str, "uint") == 0 || strcmp(key_str, "u32") == 0 || strcmp(key_str, "uint32") == 0 || strcmp(key_str, "unsigned int") == 0) {
        if (tointegerns(val, &i)) *(unsigned int*)p = (unsigned int)i; else luaG_runerror(L, "expected integer"); return 1;
      }
      if (strcmp(key_str, "u8") == 0 || strcmp(key_str, "uint8") == 0 || strcmp(key_str, "uchar") == 0 || strcmp(key_str, "unsigned char") == 0) {
        if (tointegerns(val, &i)) *(unsigned char*)p = (unsigned char)i; else luaG_runerror(L, "expected integer"); return 1;
      }
      if (strcmp(key_str, "u16") == 0 || strcmp(key_str, "uint16") == 0 || strcmp(key_str, "ushort") == 0 || strcmp(key_str, "unsigned short") == 0) {
        if (tointegerns(val, &i)) *(unsigned short*)p = (unsigned short)i; else luaG_runerror(L, "expected integer"); return 1;
      }
      if (strcmp(key_str, "ulong") == 0 || strcmp(key_str, "u64") == 0 || strcmp(key_str, "uint64") == 0 || strcmp(key_str, "unsigned long") == 0) {
        if (tointegerns(val, &i)) *(unsigned long*)p = (unsigned long)i; else luaG_runerror(L, "expected integer"); return 1;
      }
      break;
    case 'f':
      if (strcmp(key_str, "float") == 0 || strcmp(key_str, "f32") == 0) {
        if (tonumberns(val, n)) *(float*)p = (float)n; else luaG_runerror(L, "expected number"); return 1;
      }
      if (strcmp(key_str, "f64") == 0) {
        if (tonumberns(val, n)) *(double*)p = (double)n; else luaG_runerror(L, "expected number"); return 1;
      }
      break;
    case 'd':
      if (strcmp(key_str, "double") == 0) {
        if (tonumberns(val, n)) *(double*)p = (double)n; else luaG_runerror(L, "expected number"); return 1;
      }
      break;
    case 'c':
      if (strcmp(key_str, "char") == 0) {
        if (tointegerns(val, &i)) *(char*)p = (char)i; else luaG_runerror(L, "expected integer"); return 1;
      }
      if (strcmp(key_str, "cstr") == 0) {
        if (ttisstring(val)) *(const char**)p = getstr(tsvalue(val));
        else if (ttisnil(val)) *(char**)p = NULL;
        else luaG_runerror(L, "expected string or nil");
        return 1;
      }
      break;
    case 'b':
      if (strcmp(key_str, "byte") == 0) {
        if (tointegerns(val, &i)) *(unsigned char*)p = (unsigned char)i; else luaG_runerror(L, "expected integer"); return 1;
      }
      break;
    case 's':
      if (strcmp(key_str, "short") == 0) {
        if (tointegerns(val, &i)) *(short*)p = (short)i; else luaG_runerror(L, "expected integer"); return 1;
      }
      if (strcmp(key_str, "size_t") == 0) {
        if (tointegerns(val, &i)) *(size_t*)p = (size_t)i; else luaG_runerror(L, "expected integer"); return 1;
      }
      if (strcmp(key_str, "str") == 0) {
        if (ttisstring(val)) *(const char**)p = getstr(tsvalue(val));
        else if (ttisnil(val)) *(char**)p = NULL;
        else luaG_runerror(L, "expected string or nil");
        return 1;
      }
      break;
    case 'l':
      if (strcmp(key_str, "long") == 0) {
        if (tointegerns(val, &i)) *(long*)p = (long)i; else luaG_runerror(L, "expected integer"); return 1;
      }
      break;
    case 'p':
      if (strcmp(key_str, "ptr") == 0 || strcmp(key_str, "pointer") == 0) {
        if (ttispointer(val)) *(void**)p = ptrvalue(val);
        else if (ttisnil(val)) *(void**)p = NULL;
        else luaG_runerror(L, "expected pointer or nil");
        return 1;
      }
      break;
  }
  return 0;
}

/**
 * @brief Finishes the table access 'val = t[key]'.
 *
 * If 'slot' is NULL, 't' is not a table; otherwise, 'slot' points to
 * t[k] entry (which must be empty).
 *
 * @param L The Lua state.
 * @param t The table or object being indexed.
 * @param key The key.
 * @param val Where to store the result.
 * @param slot Pointer to the slot in the table (if found but empty), or NULL.
 */
void luaV_finishget (lua_State *L, const TValue *t, TValue *key, StkId val,
                      const TValue *slot) {
  int loop;  /* counter to avoid infinite loops */
  const TValue *tm;  /* metamethod */
  for (loop = 0; loop < MAXTAGLOOP; loop++) {
    if (slot == LUA_NULLPTR) {
      if (ttistable(t)) {
         Table *h = hvalue(t);

         if (h->using_next) {
            Namespace *ns = h->using_next;
            do {
               Table *nth = ns->data;
               if (nth) {
                  l_rwlock_rdlock(&nth->lock);
                  const TValue *res = luaH_get(nth, key);
                  if (!isempty(res)) {
                     setobj2s(L, val, res);
                     l_rwlock_unlock(&nth->lock);
                     return;
                  }
                  l_rwlock_unlock(&nth->lock);
               }
               ns = ns->using_next;
            } while (ns);
         }

         l_rwlock_rdlock(&h->lock);
         const TValue *res = luaH_get(h, key);
         if (!isempty(res)) {
            setobj2s(L, val, res);
            l_rwlock_unlock(&h->lock);
            return;
         }
         tm = fasttm(L, h->metatable, TM_INDEX);
         if (tm == NULL) tm = fasttm(L, h->metatable, TM_MINDEX);
         if (tm == NULL && h->metatable == NULL) {
            tm = fasttm(L, G(L)->mt[LUA_TTABLE], TM_INDEX);
         }
         if (tm == NULL) {
            l_rwlock_unlock(&h->lock);
            setnilvalue(s2v(val));
            return;
         }
         l_rwlock_unlock(&h->lock);
      } else if (ttisnamespace(t)) {
        Namespace *ns = nsvalue(t);
        do {
           Table *h = ns->data;
           if (h) {
              l_rwlock_rdlock(&h->lock);
              const TValue *res = luaH_get(h, key);
              if (!isempty(res)) {
                 setobj2s(L, val, res);
                 l_rwlock_unlock(&h->lock);
                 return;
              }
              l_rwlock_unlock(&h->lock);
           }
           ns = ns->using_next;
        } while (ns);
        setnilvalue(s2v(val));
        return;
      } else if (ttisstruct(t)) {
        luaS_structindex(L, t, key, val);
        return;
      } else if (ttispointer(t)) {
        if (ttisinteger(key)) {
          unsigned char *p = (unsigned char *)ptrvalue(t);
          setivalue(s2v(val), p[ivalue(key)]);
          return;
        } else if (ttisstring(key)) {
          const char *k = getstr(tsvalue(key));
          if (luaV_ptr_read(L, ptrvalue(t), k, val)) return;
        }
        /* Fall through to generic metatable lookup */
        tm = luaT_gettmbyobj(L, t, TM_INDEX);
        if (l_unlikely(notm(tm)))
          luaG_typeerror(L, t, "index");
      } else {
        if (ttisstring(t) && ttisinteger(key)) {
          size_t l = tsslen(tsvalue(t));
          lua_Integer idx = ivalue(key);
          if (idx < 0) idx += l + 1;
          if (idx >= 1 && idx <= (lua_Integer)l) {
            setsvalue2s(L, val, luaS_newlstr(L, getstr(tsvalue(t)) + idx - 1, 1));
            return;
          } else {
            setnilvalue(s2v(val));
            return;
          }
        }
        tm = luaT_gettmbyobj(L, t, TM_INDEX);
        if (l_unlikely(notm(tm)))
          luaG_typeerror(L, t, "index");  /* no metamethod */
        /* else will try the metamethod */
      }
    }
    else {  /* 't' is a table */
      Table *h = hvalue(t);
      lua_assert(isempty(slot));

      if (h->using_next) {
         Namespace *ns = h->using_next;
         do {
            Table *nth = ns->data;
            if (nth) {
               l_rwlock_rdlock(&nth->lock);
               const TValue *res = luaH_get(nth, key);
               if (!isempty(res)) {
                  setobj2s(L, val, res);
                  l_rwlock_unlock(&nth->lock);
                  return;
               }
               l_rwlock_unlock(&nth->lock);
            }
            ns = ns->using_next;
         } while (ns);
      }

      l_rwlock_rdlock(&h->lock);
      tm = fasttm(L, h->metatable, TM_INDEX);  /* table's metamethod */
      if (tm == LUA_NULLPTR) /* no __index? try __mindex */
        tm = fasttm(L, h->metatable, TM_MINDEX);
      if (tm == LUA_NULLPTR && h->metatable == LUA_NULLPTR) {
        tm = fasttm(L, G(L)->mt[LUA_TTABLE], TM_INDEX);
      }
      if (tm == LUA_NULLPTR) {  /* no metamethod? */
        l_rwlock_unlock(&h->lock);
        setnilvalue(s2v(val));  /* result is nil */
        return;
      }
      l_rwlock_unlock(&h->lock);
      /* else will try the metamethod */
    }
    if (ttisfunction(tm)) {  /* is metamethod a function? */
      luaT_callTMres(L, tm, t, key, val);  /* call it */
      return;
    }
    t = tm;  /* else try to access 'tm[key]' */
    if (ttistable(t)) {
      Table *h = hvalue(t);
      l_rwlock_rdlock(&h->lock);
      const TValue *res = luaH_get(h, key);
      if (!isempty(res)) {
        setobj2s(L, val, res);
        l_rwlock_unlock(&h->lock);
        return;
      }
      l_rwlock_unlock(&h->lock);
    }
    /* else repeat (tail call 'luaV_finishget') */
  }
  luaG_runerror(L, "'__index' chain too long; possible loop");
}


/**
 * @brief Finishes a table assignment 't[key] = val'.
 *
 * If 'slot' is NULL, 't' is not a table. Otherwise, 'slot' points
 * to the entry 't[key]', or to a value with an absent key if there
 * is no such entry. (The value at 'slot' must be empty, otherwise
 * 'luaV_fastget' would have done the job.)
 *
 * @param L The Lua state.
 * @param t The table or object being indexed.
 * @param key The key.
 * @param val The value to assign.
 * @param slot Pointer to the slot in the table, or NULL.
 */
void luaV_finishset (lua_State *L, const TValue *t, TValue *key,
                     TValue *val, const TValue *slot) {
  int loop;  /* counter to avoid infinite loops */
  for (loop = 0; loop < MAXTAGLOOP; loop++) {
    const TValue *tm;  /* '__newindex' metamethod */
    if (slot != LUA_NULLPTR) {  /* is 't' a table? */
      Table *h = hvalue(t);  /* save 't' table */
      lua_assert(isempty(slot));  /* slot must be empty */

      if (h->using_next) {
         Namespace *ns = h->using_next;
         while (ns) {
            Table *nth = ns->data;
            if (nth) {
               l_rwlock_rdlock(&nth->lock);
               const TValue *res = luaH_get(nth, key);
               if (!isempty(res) && !isabstkey(res)) {
                  l_rwlock_unlock(&nth->lock);
                  l_rwlock_wrlock(&nth->lock);
                  res = luaH_get(nth, key);
                  if (!isempty(res) && !isabstkey(res)) {
                     setobj2t(L, cast(TValue *, res), val);
                     luaC_barrierback(L, obj2gco(nth), val);
                     l_rwlock_unlock(&nth->lock);
                     return;
                  }
                  l_rwlock_unlock(&nth->lock);
               } else {
                  l_rwlock_unlock(&nth->lock);
               }
            }
            ns = ns->using_next;
         }
      }

      l_rwlock_rdlock(&h->lock);
      tm = fasttm(L, h->metatable, TM_NEWINDEX);  /* get metamethod */
      l_rwlock_unlock(&h->lock);
      if (tm == LUA_NULLPTR) {  /* no metamethod? */
        l_rwlock_wrlock(&h->lock); /* Lock for writing */
        /* Re-check slot? Calling luaH_finishset which might re-search if slot is absent key? */
        /* luaH_finishset calls luaH_newkey if slot is abstract. */
        /* But slot was passed in. It might be invalid now if we unlocked? */
        /* YES. The 'slot' pointer passed to luaV_finishset came from a previous lookup */
        /* which might have been unlocked. */
        /* So 'slot' is potentially dangling! */
        /* We must re-get the slot inside the write lock. */

        const TValue *newslot = luaH_get(h, key); /* Re-lookup */

        sethvalue2s(L, L->top.p, h);  /* anchor 't' */
        L->top.p++;  /* assume EXTRA_STACK */
        luaH_finishset(L, h, key, newslot, val);  /* set new value */
        L->top.p--;
        invalidateTMcache(h);
        luaC_barrierback(L, obj2gco(h), val);
        l_rwlock_unlock(&h->lock);
        return;
      }
      /* else will try the metamethod */
    }
    else {  /* not a table? or slot is NULL */
      if (ttisnamespace(t)) {
         Namespace *ns = nsvalue(t);
         Namespace *first = ns;
         while (ns) {
            Table *h = ns->data;
            if (h) {
               l_rwlock_rdlock(&h->lock);
               const TValue *res = luaH_get(h, key);
               if (!isempty(res) && !isabstkey(res)) {
                  l_rwlock_unlock(&h->lock);
                  /* Found existing key, update it */
                  l_rwlock_wrlock(&h->lock);
                  res = luaH_get(h, key); /* Re-check under write lock */
                  if (!isempty(res) && !isabstkey(res)) {
                     setobj2t(L, cast(TValue *, res), val);
                     luaC_barrierback(L, obj2gco(h), val);
                     l_rwlock_unlock(&h->lock);
                     return;
                  }
                  l_rwlock_unlock(&h->lock);
               } else {
                  l_rwlock_unlock(&h->lock);
               }
            }
            ns = ns->using_next;
         }
         /* Not found, create in first namespace */
         ns = first;
         if (ns && ns->data) {
            Table *h = ns->data;
            l_rwlock_wrlock(&h->lock);
            luaH_set(L, h, key, val);
            luaC_barrierback(L, obj2gco(h), val);
            l_rwlock_unlock(&h->lock);
            return;
         }
         return;
      }
      if (ttisstruct(t)) {
        luaS_structnewindex(L, t, key, val);
        return;
      }
      else if (ttispointer(t)) {
        if (ttisinteger(key)) {
          if (ttisinteger(val)) {
            unsigned char *p = (unsigned char *)ptrvalue(t);
            p[ivalue(key)] = (unsigned char)ivalue(val);
            return;
          }
          luaG_runerror(L, "pointer value must be integer");
        } else if (ttisstring(key)) {
          const char *k = getstr(tsvalue(key));
          if (luaV_ptr_write(L, ptrvalue(t), k, val)) return;
        }
        /* Fall through to generic metatable lookup */
        tm = luaT_gettmbyobj(L, t, TM_NEWINDEX);
        if (l_unlikely(notm(tm)))
          luaG_typeerror(L, t, "index");
      }
      else if (ttistable(t)) {
         Table *h = hvalue(t);
         l_rwlock_wrlock(&h->lock);
         const TValue *res = luaH_get(h, key);
         if (!isempty(res) && !isabstkey(res)) {
            setobj2t(L, cast(TValue *, res), val);
            luaC_barrierback(L, obj2gco(h), val);
            l_rwlock_unlock(&h->lock);
            return;
         }
         l_rwlock_unlock(&h->lock);
         // Empty, check TM
         l_rwlock_rdlock(&h->lock);
         tm = fasttm(L, h->metatable, TM_NEWINDEX);
         l_rwlock_unlock(&h->lock);
         if (tm == NULL) {
            l_rwlock_wrlock(&h->lock);
            const TValue *newslot = luaH_get(h, key);
            sethvalue2s(L, L->top.p, h);
            L->top.p++;
            luaH_finishset(L, h, key, newslot, val);
            L->top.p--;
            invalidateTMcache(h);
            luaC_barrierback(L, obj2gco(h), val);
            l_rwlock_unlock(&h->lock);
            return;
         }
      } else {
         tm = luaT_gettmbyobj(L, t, TM_NEWINDEX);
         if (l_unlikely(notm(tm)))
           luaG_typeerror(L, t, "index");
      }
    }
    /* try the metamethod */
    if (ttisfunction(tm)) {
      luaT_callTM(L, tm, t, key, val);
      return;
    }
    t = tm;  /* else repeat assignment over 'tm' */
    if (ttistable(t)) {
       Table *h = hvalue(t);
       l_rwlock_wrlock(&h->lock);
       const TValue *res = luaH_get(h, key);
       if (!isempty(res) && !isabstkey(res)) {
          /* luaV_finishfastset just does setobj2t and barrier */
          setobj2t(L, cast(TValue *, res), val);
          luaC_barrierback(L, obj2gco(h), val);
          l_rwlock_unlock(&h->lock);
          return;
       }
       l_rwlock_unlock(&h->lock);
       /* else loop */
    }
    /* else 'return luaV_finishset(L, t, key, val, slot)' (loop) */
  }
  luaG_runerror(L, "'__newindex' chain too long; possible loop");
}


/*
** Function to be used for 0-terminated string order comparison
*/
#if !defined(l_strcoll)
#define l_strcoll	strcoll
#endif


/**
 * @brief Compare two strings 'ts1' x 'ts2'.
 *
 * Returning an integer less-equal-greater than zero if 'ts1' is less-equal-greater than 'ts2'.
 * The code is a little tricky because it allows '\0' in the strings
 * and it uses 'strcoll' (to respect locales) for each segment
 * of the strings. Note that segments can compare equal but still
 * have different lengths.
 *
 * @param ts1 The first string.
 * @param ts2 The second string.
 * @return An integer less-equal-greater than zero.
 */
static int l_strcmp (const TString *ts1, const TString *ts2) {
  const char *s1 = getstr(ts1);
  size_t rl1 = tsslen(ts1);  /* real length */
  const char *s2 = getstr(ts2);
  size_t rl2 = tsslen(ts2);
  for (;;) {  /* for each segment */
    int temp = strcoll(s1, s2);
    if (temp != 0)  /* not equal? */
      return temp;  /* done */
    else {  /* strings are equal up to a '\0' */
      size_t zl1 = strlen(s1);  /* index of first '\0' in 's1' */
      size_t zl2 = strlen(s2);  /* index of first '\0' in 's2' */
      if (zl2 == rl2)  /* 's2' is finished? */
        return (zl1 == rl1) ? 0 : 1;  /* check 's1' */
      else if (zl1 == rl1)  /* 's1' is finished? */
        return -1;  /* 's1' is less than 's2' ('s2' is not finished) */
      /* both strings longer than 'zl'; go on comparing after the '\0' */
      zl1++; zl2++;
      s1 += zl1; rl1 -= zl1; s2 += zl2; rl2 -= zl2;
    }
  }
}


/**
 * @brief Check whether integer 'i' is less than float 'f'.
 *
 * If 'i' has an exact representation as a float ('l_intfitsf'), compare numbers as
 * floats. Otherwise, use the equivalence 'i < f <=> i < ceil(f)'.
 * If 'ceil(f)' is out of integer range, either 'f' is greater than
 * all integers or less than all integers.
 * (The test with 'l_intfitsf' is only for performance; the else
 * case is correct for all values, but it is slow due to the conversion
 * from float to int.)
 * When 'f' is NaN, comparisons must result in false.
 *
 * @param i The integer.
 * @param f The float.
 * @return 1 if i < f, 0 otherwise.
 */
l_sinline int LTintfloat (lua_Integer i, lua_Number f) {
  if (l_intfitsf(i))
    return luai_numlt(cast_num(i), f);  /* compare them as floats */
  else {  /* i < f <=> i < ceil(f) */
    lua_Integer fi;
    if (luaV_flttointeger(f, &fi, F2Iceil))  /* fi = ceil(f) */
      return i < fi;   /* compare them as integers */
    else  /* 'f' is either greater or less than all integers */
      return f > 0;  /* greater? */
  }
}


/**
 * @brief Check whether integer 'i' is less than or equal to float 'f'.
 *
 * See comments on previous function.
 *
 * @param i The integer.
 * @param f The float.
 * @return 1 if i <= f, 0 otherwise.
 */
l_sinline int LEintfloat (lua_Integer i, lua_Number f) {
  if (l_intfitsf(i))
    return luai_numle(cast_num(i), f);  /* compare them as floats */
  else {  /* i <= f <=> i <= floor(f) */
    lua_Integer fi;
    if (luaV_flttointeger(f, &fi, F2Ifloor))  /* fi = floor(f) */
      return i <= fi;   /* compare them as integers */
    else  /* 'f' is either greater or less than all integers */
      return f > 0;  /* greater? */
  }
}


/**
 * @brief Check whether float 'f' is less than integer 'i'.
 *
 * See comments on previous function.
 *
 * @param f The float.
 * @param i The integer.
 * @return 1 if f < i, 0 otherwise.
 */
l_sinline int LTfloatint (lua_Number f, lua_Integer i) {
  if (l_intfitsf(i))
    return luai_numlt(f, cast_num(i));  /* compare them as floats */
  else {  /* f < i <=> floor(f) < i */
    lua_Integer fi;
    if (luaV_flttointeger(f, &fi, F2Ifloor))  /* fi = floor(f) */
      return fi < i;   /* compare them as integers */
    else  /* 'f' is either greater or less than all integers */
      return f < 0;  /* less? */
  }
}


/**
 * @brief Check whether float 'f' is less than or equal to integer 'i'.
 *
 * See comments on previous function.
 *
 * @param f The float.
 * @param i The integer.
 * @return 1 if f <= i, 0 otherwise.
 */
l_sinline int LEfloatint (lua_Number f, lua_Integer i) {
  if (l_intfitsf(i))
    return luai_numle(f, cast_num(i));  /* compare them as floats */
  else {  /* f <= i <=> ceil(f) <= i */
    lua_Integer fi;
    if (luaV_flttointeger(f, &fi, F2Iceil))  /* fi = ceil(f) */
      return fi <= i;   /* compare them as integers */
    else  /* 'f' is either greater or less than all integers */
      return f < 0;  /* less? */
  }
}


/**
 * @brief Return 'l < r', for numbers.
 *
 * @param l The left operand.
 * @param r The right operand.
 * @return 1 if l < r, 0 otherwise.
 */
l_sinline int LTnum (const TValue *l, const TValue *r) {
  lua_assert(ttisnumber(l) && ttisnumber(r));
  if (ttisbigint(l) || ttisbigint(r)) {
      return luaB_compare((TValue*)l, (TValue*)r) < 0;
  }
  if (ttisinteger(l)) {
    lua_Integer li = ivalue(l);
    if (ttisinteger(r))
      return li < ivalue(r);  /* both are integers */
    else  /* 'l' is int and 'r' is float */
      return LTintfloat(li, fltvalue(r));  /* l < r ? */
  }
  else {
    lua_Number lf = fltvalue(l);  /* 'l' must be float */
    if (ttisfloat(r))
      return luai_numlt(lf, fltvalue(r));  /* both are float */
    else  /* 'l' is float and 'r' is int */
      return LTfloatint(lf, ivalue(r));
  }
}


/**
 * @brief Return 'l <= r', for numbers.
 *
 * @param l The left operand.
 * @param r The right operand.
 * @return 1 if l <= r, 0 otherwise.
 */
l_sinline int LEnum (const TValue *l, const TValue *r) {
  lua_assert(ttisnumber(l) && ttisnumber(r));
  if (ttisbigint(l) || ttisbigint(r)) {
      return luaB_compare((TValue*)l, (TValue*)r) <= 0;
  }
  if (ttisinteger(l)) {
    lua_Integer li = ivalue(l);
    if (ttisinteger(r))
      return li <= ivalue(r);  /* both are integers */
    else  /* 'l' is int and 'r' is float */
      return LEintfloat(li, fltvalue(r));  /* l <= r ? */
  }
  else {
    lua_Number lf = fltvalue(l);  /* 'l' must be float */
    if (ttisfloat(r))
      return luai_numle(lf, fltvalue(r));  /* both are float */
    else  /* 'l' is float and 'r' is int */
      return LEfloatint(lf, ivalue(r));
  }
}


/**
 * @brief return 'l < r' for non-numbers.
 *
 * @param L The Lua state.
 * @param l The left operand.
 * @param r The right operand.
 * @return 1 if l < r, 0 otherwise.
 */
static int lessthanothers (lua_State *L, const TValue *l, const TValue *r) {
  lua_assert(!ttisnumber(l) || !ttisnumber(r));
  if (ttisstring(l) && ttisstring(r))  /* both are strings? */
    return l_strcmp(tsvalue(l), tsvalue(r)) < 0;
  else if (ttispointer(l) && ttispointer(r))
    return (L_P2I)ptrvalue(l) < (L_P2I)ptrvalue(r);
  else
    return luaT_callorderTM(L, l, r, TM_LT);
}


/**
 * @brief Main operation less than; return 'l < r'.
 *
 * @param L The Lua state.
 * @param l Left operand.
 * @param r Right operand.
 * @return 1 if l < r, 0 otherwise.
 */
int luaV_lessthan (lua_State *L, const TValue *l, const TValue *r) {
  if (ttisnumber(l) && ttisnumber(r))  /* both operands are numbers? */
    return LTnum(l, r);
  else return lessthanothers(L, l, r);
}


/**
 * @brief return 'l <= r' for non-numbers.
 *
 * @param L The Lua state.
 * @param l The left operand.
 * @param r The right operand.
 * @return 1 if l <= r, 0 otherwise.
 */
static int lessequalothers (lua_State *L, const TValue *l, const TValue *r) {
  lua_assert(!ttisnumber(l) || !ttisnumber(r));
  if (ttisstring(l) && ttisstring(r))  /* both are strings? */
    return l_strcmp(tsvalue(l), tsvalue(r)) <= 0;
  else if (ttispointer(l) && ttispointer(r))
    return (L_P2I)ptrvalue(l) <= (L_P2I)ptrvalue(r);
  else
    return luaT_callorderTM(L, l, r, TM_LE);
}


/**
 * @brief Main operation less than or equal to; return 'l <= r'.
 *
 * @param L The Lua state.
 * @param l Left operand.
 * @param r Right operand.
 * @return 1 if l <= r, 0 otherwise.
 */
int luaV_lessequal (lua_State *L, const TValue *l, const TValue *r) {
  if (ttisnumber(l) && ttisnumber(r))  /* both operands are numbers? */
    return LEnum(l, r);
  else return lessequalothers(L, l, r);
}


/**
 * @brief Main operation for equality of Lua values; return 't1 == t2'.
 *
 * L == LUA_NULLPTR means raw equality (no metamethods).
 *
 * @param L The Lua state (optional, for metamethods).
 * @param t1 First value.
 * @param t2 Second value.
 * @return 1 if equal, 0 otherwise.
 */
int luaV_equalobj (lua_State *L, const TValue *t1, const TValue *t2) {
  const TValue *tm;
  if (ttypetag(t1) != ttypetag(t2)) {  /* not the same variant? */
    if (ttype(t1) != ttype(t2) || ttype(t1) != LUA_TNUMBER)
      return 0;  /* only numbers can be equal with different variants */
    else {  /* two numbers with different variants */
      /* One of them is an integer. If the other does not have an
         integer value, they cannot be equal; otherwise, compare their
         integer values. */
      lua_Integer i1, i2;
      return (luaV_tointegerns(t1, &i1, F2Ieq) &&
              luaV_tointegerns(t2, &i2, F2Ieq) &&
              i1 == i2);
    }
  }
  /* values have same type and same variant */
  switch (ttypetag(t1)) {
    case LUA_VNIL: case LUA_VFALSE: case LUA_VTRUE: return 1;
    case LUA_VNUMINT: return (ivalue(t1) == ivalue(t2));
    case LUA_VNUMFLT: return luai_numeq(fltvalue(t1), fltvalue(t2));
    case LUA_VNUMBIG: return luaB_compare((TValue*)t1, (TValue*)t2) == 0;
    case LUA_VLIGHTUSERDATA: return pvalue(t1) == pvalue(t2);
    case LUA_VPOINTER: return ptrvalue(t1) == ptrvalue(t2);
    case LUA_VLCF: return fvalue(t1) == fvalue(t2);
    case LUA_VSHRSTR: return eqshrstr(tsvalue(t1), tsvalue(t2));
    case LUA_VLNGSTR: return luaS_eqlngstr(tsvalue(t1), tsvalue(t2));
    case LUA_VSTRUCT: return luaS_structeq(t1, t2);
    case LUA_VUSERDATA: {
      if (uvalue(t1) == uvalue(t2)) return 1;
      else if (L == LUA_NULLPTR) return 0;
      tm = fasttm(L, uvalue(t1)->metatable, TM_EQ);
      if (tm == LUA_NULLPTR)
        tm = fasttm(L, uvalue(t2)->metatable, TM_EQ);
      break;  /* will try TM */
    }
    case LUA_VTABLE: {
      if (hvalue(t1) == hvalue(t2)) return 1;
      else if (L == LUA_NULLPTR) return 0;
      tm = fasttm(L, hvalue(t1)->metatable, TM_EQ);
      if (tm == LUA_NULLPTR)
        tm = fasttm(L, hvalue(t2)->metatable, TM_EQ);
      break;  /* will try TM */
    }
    default:
      return gcvalue(t1) == gcvalue(t2);
  }
  if (tm == LUA_NULLPTR)  /* no TM? */
    return 0;  /* objects are different */
  else {
    luaT_callTMres(L, tm, t1, t2, L->top.p);  /* call TM */
    return !l_isfalse(s2v(L->top.p));
  }
}


/* macro used by 'luaV_concat' to ensure that element at 'o' is a string */
#define tostring(L,o)  \
	(ttisstring(o) || ((cvt2str(o) || ttisboolean(o)) && (luaO_tostring(L, o), 1)))

#define isemptystr(o)	(ttisshrstring(o) && tsvalue(o)->shrlen == 0)

/**
 * @brief Copy strings in stack from top - n up to top - 1 to buffer.
 *
 * @param top The stack top.
 * @param n Number of strings to copy.
 * @param buff The output buffer.
 */
static void copy2buff (StkId top, int n, char *buff) {
  size_t tl = 0;  /* size already copied */
  do {
    TString *st = tsvalue(s2v(top - n));
    size_t l = tsslen(st);  /* length of string being copied */
    memcpy(buff + tl, getstr(st), l * sizeof(char));
    tl += l;
  } while (--n > 0);
}


/**
 * @brief Main operation for concatenation: concat 'total' values in the stack.
 *
 * Concatenates 'total' values in the stack, from 'L->top.p - total' up to 'L->top.p - 1'.
 *
 * @param L The Lua state.
 * @param total Number of elements to concatenate.
 */
void luaV_concat (lua_State *L, int total) {
  if (total == 1)
    return;  /* "all" values already concatenated */
  do {
    StkId top = L->top.p;
    int n = 2;  /* number of elements handled in this pass (at least 2) */
    if (!(ttisstring(s2v(top - 2)) || cvt2str(s2v(top - 2)) || ttisboolean(s2v(top - 2))) ||
        !tostring(L, s2v(top - 1)))
      luaT_tryconcatTM(L);  /* may invalidate 'top' */
    else if (isemptystr(s2v(top - 1)))  /* second operand is empty? */
      cast_void(tostring(L, s2v(top - 2)));  /* result is first operand */
    else if (isemptystr(s2v(top - 2))) {  /* first operand is empty string? */
      setobjs2s(L, top - 2, top - 1);  /* result is second op. */
    }
    else {
      /* at least two non-empty string values; get as many as possible */
      size_t tl = tsslen(tsvalue(s2v(top - 1)));
      TString *ts;
      /* collect total length and number of strings */
      for (n = 1; n < total && tostring(L, s2v(top - n - 1)); n++) {
        size_t l = tsslen(tsvalue(s2v(top - n - 1)));
        if (l_unlikely(l >= MAX_SIZE - sizeof(TString) - tl)) {
          L->top.p = top - total;  /* pop strings to avoid wasting stack */
          luaG_runerror(L, "string length overflow");
        }
        tl += l;
      }
      if (tl <= LUAI_MAXSHORTLEN) {  /* is result a short string? */
        char buff[LUAI_MAXSHORTLEN];
        copy2buff(top, n, buff);  /* copy strings to buffer */
        ts = luaS_newlstr(L, buff, tl);
      }
      else {  /* long string; copy strings directly to final result */
        ts = luaS_createlngstrobj(L, tl);
        copy2buff(top, n, getlngstr(ts));
      }
      setsvalue2s(L, top - n, ts);  /* create result */
    }
    total -= n - 1;  /* got 'n' strings to create one new */
    L->top.p -= n - 1;  /* popped 'n' strings and pushed one */
  } while (total > 1);  /* repeat until only 1 result left */
}


/**
 * @brief Main operation 'ra = #rb'.
 *
 * @param L The Lua state.
 * @param ra Destination register.
 * @param rb Source value.
 */
void luaV_objlen (lua_State *L, StkId ra, const TValue *rb) {
  const TValue *tm;
  switch (ttypetag(rb)) {
    case LUA_VTABLE: {
      Table *h = hvalue(rb);
      tm = fasttm(L, h->metatable, TM_LEN);
      if (tm) break;  /* metamethod? break switch to call it */
      setivalue(s2v(ra), luaH_getn(h));  /* else primitive len */
      return;
    }
    case LUA_VSHRSTR: {
      setivalue(s2v(ra), tsvalue(rb)->shrlen);
      return;
    }
    case LUA_VLNGSTR: {
      setivalue(s2v(ra), tsvalue(rb)->u.lnglen);
      return;
    }
    default: {  /* try metamethod */
      tm = luaT_gettmbyobj(L, rb, TM_LEN);
      if (l_unlikely(notm(tm)))  /* no metamethod? */
        luaG_typeerror(L, rb, "get length of");
      break;
    }
  }
  luaT_callTMres(L, tm, rb, rb, ra);
}


/**
 * @brief Integer division; return 'm // n', that is, floor(m/n).
 *
 * C division truncates its result (rounds towards zero).
 * 'floor(q) == trunc(q)' when 'q >= 0' or when 'q' is integer,
 * otherwise 'floor(q) == trunc(q) - 1'.
 *
 * @param L The Lua state.
 * @param m Dividend.
 * @param n Divisor.
 * @return The result of integer division.
 */
lua_Integer luaV_idiv (lua_State *L, lua_Integer m, lua_Integer n) {
  if (l_unlikely(l_castS2U(n) + 1u <= 1u)) {  /* special cases: -1 or 0 */
    if (n == 0)
      luaG_runerror(L, "[!] 错误: 尝试除以零");
    return intop(-, 0, m);   /* n==-1; avoid overflow with 0x80000...//-1 */
  }
  else {
    lua_Integer q = m / n;  /* perform C division */
    if ((m ^ n) < 0 && m % n != 0)  /* 'm/n' would be negative non-integer? */
      q -= 1;  /* correct result for different rounding */
    return q;
  }
}


/**
 * @brief Integer modulus; return 'm % n'.
 *
 * Assume that C '%' with negative operands follows C99 behavior.
 *
 * @param L The Lua state.
 * @param m Dividend.
 * @param n Divisor.
 * @return The result of modulus.
 */
lua_Integer luaV_mod (lua_State *L, lua_Integer m, lua_Integer n) {
  if (l_unlikely(l_castS2U(n) + 1u <= 1u)) {  /* special cases: -1 or 0 */
    if (n == 0)
      luaG_runerror(L, "[!] 错误: 尝试对零取模 (n%%0)");
    return 0;   /* m % -1 == 0; avoid overflow with 0x80000...%-1 */
  }
  else {
    lua_Integer r = m % n;
    if (r != 0 && (r ^ n) < 0)  /* 'm/n' would be non-integer negative? */
      r += n;  /* correct result for different rounding */
    return r;
  }
}


/**
 * @brief Float modulus.
 *
 * @param L The Lua state.
 * @param m Dividend.
 * @param n Divisor.
 * @return The result of modulus.
 */
lua_Number luaV_modf (lua_State *L, lua_Number m, lua_Number n) {
  lua_Number r;
  luai_nummod(L, m, n, r);
  return r;
}


/* number of bits in an integer */
#define NBITS	cast_int(sizeof(lua_Integer) * CHAR_BIT)


/**
 * @brief Shift left operation.
 *
 * Shift right just negates 'y'.
 *
 * @param x The value to shift.
 * @param y The shift amount.
 * @return The shifted value.
 */
lua_Integer luaV_shiftl (lua_Integer x, lua_Integer y) {
  if (y < 0) {  /* shift right? */
    if (y <= -NBITS) return 0;
    else return intop(>>, x, -y);
  }
  else {  /* shift left */
    if (y >= NBITS) return 0;
    else return intop(<<, x, y);
  }
}


/**
 * @brief Create a new Lua closure, push it in the stack, and initialize its upvalues.
 *
 * @param L The Lua state.
 * @param p The function prototype.
 * @param encup The enclosing upvalues.
 * @param base The stack base.
 * @param ra The register to store the closure.
 */
static void pushclosure (lua_State *L, Proto *p, UpVal **encup, StkId base,
                         StkId ra) {
  int nup = p->sizeupvalues;
  Upvaldesc *uv = p->upvalues;
  int i;
  LClosure *ncl = luaF_newLclosure(L, nup);
  ncl->p = p;
  setclLvalue2s(L, ra, ncl);  /* anchor new closure in stack */
  for (i = 0; i < nup; i++) {  /* fill in its upvalues */
    if (uv[i].instack)  /* upvalue refers to local variable? */
      ncl->upvals[i] = luaF_findupval(L, base + uv[i].idx);
    else  /* get upvalue from enclosing function */
      ncl->upvals[i] = encup[uv[i].idx];
    luaC_objbarrier(L, ncl, ncl->upvals[i]);
  }
}

static void pushconcept (lua_State *L, Proto *p, UpVal **encup, StkId base,
                         StkId ra) {
  int nup = p->sizeupvalues;
  Upvaldesc *uv = p->upvalues;
  int i;
  Concept *ncl = luaF_newconcept(L, nup);
  ncl->p = p;
  setclConceptValue(L, s2v(ra), ncl);  /* anchor new closure in stack */
  for (i = 0; i < nup; i++) {  /* fill in its upvalues */
    if (uv[i].instack)  /* upvalue refers to local variable? */
      ncl->upvals[i] = luaF_findupval(L, base + uv[i].idx);
    else  /* get upvalue from enclosing function */
      ncl->upvals[i] = encup[uv[i].idx];
    luaC_objbarrier(L, ncl, ncl->upvals[i]);
  }
}


/**
 * @brief Finish execution of an opcode interrupted by a yield.
 *
 * @param L The Lua state.
 */
void luaV_finishOp (lua_State *L) {
  CallInfo *ci = L->ci;
  StkId base = ci->func.p + 1;
  Instruction inst = *(ci->u.l.savedpc - 1);  /* interrupted instruction */
  OpCode op = GET_OPCODE(inst);
  switch (op) {  /* finish its execution */
    case OP_MMBIN: case OP_MMBINI: case OP_MMBINK: {
      setobjs2s(L, base + GETARG_A(*(ci->u.l.savedpc - 2)), --L->top.p);
      break;
    }
    case OP_UNM: case OP_BNOT: case OP_LEN:
    case OP_GETTABUP: case OP_GETTABLE: case OP_GETI:
    case OP_GETFIELD: case OP_SELF: {
      setobjs2s(L, base + GETARG_A(inst), --L->top.p);
      break;
    }
    case OP_LT: case OP_LE:
    case OP_LTI: case OP_LEI:
    case OP_GTI: case OP_GEI:
    case OP_EQ: {  /* note that 'OP_EQI'/'OP_EQK' cannot yield */
      int res = !l_isfalse(s2v(L->top.p - 1));
      L->top.p--;
#if defined(LUA_COMPAT_LT_LE)
      if (ci->callstatus & CIST_LEQ) {  /* "<=" using "<" instead? */
        ci->callstatus ^= CIST_LEQ;  /* clear mark */
        res = !res;  /* negate result */
      }
#endif
      lua_assert(GET_OPCODE(*ci->u.l.savedpc) == OP_JMP);
      if (res != GETARG_k(inst))  /* condition failed? */
        ci->u.l.savedpc++;  /* skip jump instruction */
      break;
    }
    case OP_CONCAT: {
      StkId top = L->top.p - 1;  /* top when 'luaT_tryconcatTM' was called */
      int a = GETARG_A(inst);      /* first element to concatenate */
      int total = cast_int(top - 1 - (base + a));  /* yet to concatenate */
      setobjs2s(L, top - 2, top);  /* put TM result in proper position */
      L->top.p = top - 1;  /* top is one after last element (at top-2) */
      luaV_concat(L, total);  /* concat them (may yield again) */
      break;
    }
    case OP_CLOSE: {  /* yielded closing variables */
      ci->u.l.savedpc--;  /* repeat instruction to close other vars. */
      break;
    }
    case OP_RETURN: {  /* yielded closing variables */
      StkId ra = base + GETARG_A(inst);
      /* adjust top to signal correct number of returns, in case the
         return is "up to top" ('isIT') */
      L->top.p = ra + ci->u2.nres;
      /* repeat instruction to close other vars. and complete the return */
      ci->u.l.savedpc--;
      break;
    }
    default: {
      /* only these other opcodes can yield */
      lua_assert(op == OP_TFORCALL || op == OP_CALL ||
           op == OP_TAILCALL || op == OP_SETTABUP || op == OP_SETTABLE ||
           op == OP_SETI || op == OP_SETFIELD);
      break;
    }
  }
}



/**
 * @brief 'in' operation implementation.
 *
 * @param L The Lua state.
 * @param ra Destination register.
 * @param a The key.
 * @param b The table or string.
 */
static void inopr (lua_State *L, StkId ra, TValue *a, TValue *b) {
  if (ttisstring(a) && ttisstring(b)) {
    const char *s1 = getstr(tsvalue(a));
    const char *s2 = getstr(tsvalue(b));
    size_t l1 = tsslen(tsvalue(a));
    size_t l2 = tsslen(tsvalue(b));
    int found = 0;
    if (l1 <= l2) {
       size_t i;
       for (i = 0; i <= l2 - l1; i++) {
          if (memcmp(s2 + i, s1, l1) == 0) {
             found = 1;
             break;
          }
       }
    }
    if (found) setbtvalue(s2v(ra)); else setbfvalue(s2v(ra));
  } else {
    if (l_unlikely(!ttistable(b))) {
      luaG_runerror(L, "expected second 'in' operand to be table or string");
    }
    const TValue *res = luaH_get(hvalue(b), a);
    if (!ttisnil(res)) {
      setbtvalue(s2v(ra));
    } else {
      setbfvalue(s2v(ra));
    }
  }
}

/*
** {=======================================================
** Macros for arithmetic/bitwise/comparison opcodes in 'luaV_execute'
**
** All these macros are to be used exclusively inside the main
** iterpreter loop (function luaV_execute) and may access directly
** the local variables of that function (L, i, pc, ci, etc.).
** ========================================================
*/

#define l_addi(L,a,b)	intop(+, a, b)
#define l_subi(L,a,b)	intop(-, a, b)
#define l_muli(L,a,b)	intop(*, a, b)
#define l_band(a,b)	intop(&, a, b)
#define l_bor(a,b)	intop(|, a, b)
#define l_bxor(a,b)	intop(^, a, b)

#define l_lti(a,b)	(a < b)
#define l_lei(a,b)	(a <= b)
#define l_gti(a,b)	(a > b)
#define l_gei(a,b)	(a >= b)


/**
 * @brief Arithmetic operations with immediate operands.
 *
 * 'iop' is the integer operation, 'fop' is the float operation.
 */
#define op_arithI(L,iop,fop) {  \
  StkId ra = RA(i); \
  TValue *v1 = vRB(i);  \
  int imm = GETARG_sC(i);  \
  if (ttisinteger(v1)) {  \
    lua_Integer iv1 = ivalue(v1);  \
    pc++; setivalue(s2v(ra), iop(L, iv1, imm));  \
  }  \
  else if (ttisfloat(v1)) {  \
    lua_Number nb = fltvalue(v1);  \
    lua_Number fimm = cast_num(imm);  \
    pc++; setfltvalue(s2v(ra), fop(L, nb, fimm)); \
  }}

#define op_arith_overflow_aux(L,v1,v2,tryop,fop,bigop) {  \
  StkId ra = RA(i); \
  if (ttisinteger(v1) && ttisinteger(v2)) {  \
    lua_Integer i1 = ivalue(v1); lua_Integer i2 = ivalue(v2); \
    lua_Integer r; \
    if (tryop(i1, i2, &r)) { \
       pc++; setivalue(s2v(ra), r); \
    } else { \
       bigop(L, v1, v2, s2v(ra)); \
       pc++; \
    } \
  }  \
  else if (ttisbigint(v1) || ttisbigint(v2)) { \
      bigop(L, v1, v2, s2v(ra)); \
      pc++; \
  } \
  else op_arithf_aux(L, v1, v2, fop); }

#define op_arith_overflow(L,tryop,fop,bigop) {  \
  TValue *v1 = vRB(i);  \
  TValue *v2 = vRC(i);  \
  op_arith_overflow_aux(L, v1, v2, tryop, fop, bigop); }

#define op_arith_overflow_K(L,tryop,fop,bigop) {  \
  TValue *v1 = vRB(i);  \
  TValue *v2 = KC(i); lua_assert(ttisnumber(v2));  \
  op_arith_overflow_aux(L, v1, v2, tryop, fop, bigop); }

#define op_arith_overflow_I(L,tryop,fop,bigop) {  \
  StkId ra = RA(i); \
  TValue *v1 = vRB(i);  \
  int imm = GETARG_sC(i);  \
  if (ttisinteger(v1)) {  \
    lua_Integer iv1 = ivalue(v1);  \
    lua_Integer r; \
    if (tryop(iv1, imm, &r)) { \
       pc++; setivalue(s2v(ra), r); \
    } else { \
       TValue vimm; setivalue(&vimm, imm); \
       bigop(L, v1, &vimm, s2v(ra)); \
       pc++; \
    } \
  }  \
  else if (ttisbigint(v1)) { \
      TValue vimm; setivalue(&vimm, imm); \
      bigop(L, v1, &vimm, s2v(ra)); \
      pc++; \
  } \
  else if (ttisfloat(v1)) {  \
    lua_Number nb = fltvalue(v1);  \
    lua_Number fimm = cast_num(imm);  \
    pc++; setfltvalue(s2v(ra), fop(L, nb, fimm)); \
  }}


/**
 * @brief Auxiliary function for arithmetic operations over floats and others
 * with two register operands.
 */
#define op_arithf_aux(L,v1,v2,fop) {  \
  lua_Number n1; lua_Number n2;  \
  if (tonumberns(v1, n1) && tonumberns(v2, n2)) {  \
    pc++; setfltvalue(s2v(ra), fop(L, n1, n2));  \
  }}


/**
 * @brief Arithmetic operations over floats and others with register operands.
 */
#define op_arithf(L,fop) {  \
  StkId ra = RA(i); \
  TValue *v1 = vRB(i);  \
  TValue *v2 = vRC(i);  \
  op_arithf_aux(L, v1, v2, fop); }


/**
 * @brief Arithmetic operations with K operands for floats.
 */
#define op_arithfK(L,fop) {  \
  StkId ra = RA(i); \
  TValue *v1 = vRB(i);  \
  TValue *v2 = KC(i); lua_assert(ttisnumber(v2));  \
  op_arithf_aux(L, v1, v2, fop); }


/**
 * @brief Arithmetic operations over integers and floats.
 */
#define op_arith_aux(L,v1,v2,iop,fop) {  \
  StkId ra = RA(i); \
  if (ttisinteger(v1) && ttisinteger(v2)) {  \
    lua_Integer i1 = ivalue(v1); lua_Integer i2 = ivalue(v2);  \
    pc++; setivalue(s2v(ra), iop(L, i1, i2));  \
  }  \
  else op_arithf_aux(L, v1, v2, fop); }


/**
 * @brief Arithmetic operations with register operands.
 */
#define op_arith(L,iop,fop) {  \
  TValue *v1 = vRB(i);  \
  TValue *v2 = vRC(i);  \
  op_arith_aux(L, v1, v2, iop, fop); }


/**
 * @brief Arithmetic operations with K operands.
 */
#define op_arithK(L,iop,fop) {  \
  TValue *v1 = vRB(i);  \
  TValue *v2 = KC(i); lua_assert(ttisnumber(v2));  \
  op_arith_aux(L, v1, v2, iop, fop); }


/**
 * @brief Bitwise operations with constant operand.
 */
#define op_bitwiseK(L,op) {  \
  StkId ra = RA(i); \
  TValue *v1 = vRB(i);  \
  TValue *v2 = KC(i);  \
  lua_Integer i1;  \
  lua_Integer i2 = ivalue(v2);  \
  if (tointegerns(v1, &i1)) {  \
    pc++; setivalue(s2v(ra), op(i1, i2));  \
  }}


/**
 * @brief Bitwise operations with register operands.
 */
#define op_bitwise(L,op) {  \
  StkId ra = RA(i); \
  TValue *v1 = vRB(i);  \
  TValue *v2 = vRC(i);  \
  lua_Integer i1; lua_Integer i2;  \
  if (tointegerns(v1, &i1) && tointegerns(v2, &i2)) {  \
    pc++; setivalue(s2v(ra), op(i1, i2));  \
  }}


/**
 * @brief Order operations with register operands.
 *
 * 'opn' actually works for all numbers, but the fast track improves performance for integers.
 */
#define op_order(L,opi,opn,other) {  \
  StkId ra = RA(i); \
  int cond;  \
  TValue *rb = vRB(i);  \
  if (ttisinteger(s2v(ra)) && ttisinteger(rb)) {  \
    lua_Integer ia = ivalue(s2v(ra));  \
    lua_Integer ib = ivalue(rb);  \
    cond = opi(ia, ib);  \
  }  \
  else if (ttisnumber(s2v(ra)) && ttisnumber(rb))  \
    cond = opn(s2v(ra), rb);  \
  else  \
    Protect(cond = other(L, s2v(ra), rb));  \
  docondjump(); }


/**
 * @brief Order operations with immediate operand.
 *
 * (Immediate operand is always small enough to have an exact representation as a float.)
 */
#define op_orderI(L,opi,opf,inv,tm) {  \
  StkId ra = RA(i); \
  int cond;  \
  int im = GETARG_sB(i);  \
  if (ttisinteger(s2v(ra)))  \
    cond = opi(ivalue(s2v(ra)), im);  \
  else if (ttisfloat(s2v(ra))) {  \
    lua_Number fa = fltvalue(s2v(ra));  \
    lua_Number fim = cast_num(im);  \
    cond = opf(fa, fim);  \
  }  \
  else {  \
    int isf = GETARG_C(i);  \
    Protect(cond = luaT_callorderiTM(L, s2v(ra), im, inv, isf, tm));  \
  }  \
  docondjump(); }

/* }======================================================= */


/*
** {=======================================================
** Function 'luaV_execute': main interpreter loop
** ========================================================
*/

/*
** some macros for common tasks in 'luaV_execute'
*/


#define RA(i)	(base+GETARG_A(i))
#define vRA(i)	s2v(RA(i))
#define RB(i)	(base+GETARG_B(i))
#define vRB(i)	s2v(RB(i))
#define KB(i)	(k+GETARG_B(i))
#define RC(i)	(base+GETARG_C(i))
#define vRC(i)	s2v(RC(i))
#define KC(i)	(k+GETARG_C(i))
#define RKC(i)	((TESTARG_k(i)) ? k + GETARG_C(i) : s2v(base + GETARG_C(i)))



#define updatetrap(ci)  (trap = ci->u.l.trap)

#define updatebase(ci)	(base = ci->func.p + 1)


#define updatestack(ci)  \
	{ if (l_unlikely(trap)) { updatebase(ci); ra = RA(i); } }


/*
** Execute a jump instruction. The 'updatetrap' allows signals to stop
** tight loops. (Without it, the local copy of 'trap' could never change.)
*/
#define dojump(ci,i,e)	{ pc += GETARG_sJ(i) + e; updatetrap(ci); }


/* for test instructions, execute the jump instruction that follows it */
#define donextjump(ci)	{ Instruction ni = *pc; dojump(ci, ni, 1); }

/*
** do a conditional jump: skip next instruction if 'cond' is not what
** was expected (parameter 'k'), else do next instruction, which must
** be a jump.
*/
#define docondjump()	if (cond != GETARG_k(i)) pc++; else donextjump(ci);


/*
** Correct global 'pc'.
*/
#define savepc(L)	(ci->u.l.savedpc = pc)


/*
** Whenever code can raise errors, the global 'pc' and the global
** 'top' must be correct to report occasional errors.
*/
#define savestate(L,ci)		(savepc(L), L->top.p = ci->top.p)


/*
** Protect code that, in general, can raise errors, reallocate the
** stack, and change the hooks.
*/
#define Protect(exp)  (savestate(L,ci), (exp), updatetrap(ci))

/* special version that does not change the top */
#define ProtectNT(exp)  (savepc(L), (exp), updatetrap(ci))

/*
** Protect code that can only raise errors. (That is, it cannot change
** the stack or hooks.)
*/
#define halfProtect(exp)  (savestate(L,ci), (exp))

/*
** macro executed during Lua functions at points where the
** function can yield.
*/
#if !defined(luai_threadyield)
#define luai_threadyield(L)	{lua_unlock(L); lua_lock(L);}
#endif

/* 'c' is the limit of live values in the stack */
#define checkGC(L,c)  \
	{ luaC_condGC(L, (savepc(L), L->top.p = (c)), \
                         updatetrap(ci)); \
           luai_threadyield(L); }


/* fetch an instruction and prepare its execution */
#define vmfetch()	{ \
  if (l_unlikely(trap)) {  /* stack reallocation or hooks? */ \
    trap = luaG_traceexec(L, pc);  /* handle hooks */ \
    updatebase(ci);  /* correct stack */ \
  } \
  i = *(pc++); \
}

#define vmdispatch(o)	switch(o)
#define vmcase(l)	case l:
#define vmbreak		break


/**
 * @brief Main virtual machine execution loop.
 *
 * This function interprets the bytecode instructions. It uses a dispatch
 * loop (switch-case) or a jump table (if enabled) to execute each instruction.
 *
 * @param L The Lua state.
 * @param ci The CallInfo for the function being executed.
 */
void luaV_execute (lua_State *L, CallInfo *ci) {
  LClosure *cl;
  TValue *k;
  StkId base;
  const Instruction *pc;
  int trap;
#if LUA_USE_JUMPTABLE
#include "ljumptab.h"
#endif
 startfunc:
  trap = L->hookmask;
 returning:  /* trap already set */
  cl = ci_func(ci);

  /** VM protection detection: If the function enables VM protection, use a custom VM interpreter */
  if (cl->p->difierline_mode & OBFUSCATE_VM_PROTECT) {
    int vm_result = luaO_executeVM(L, cl->p);
    if (vm_result == 0) {
      /** VM execution successful */
      /* Check if we should return to C or continue in interpreter */
      /* The finished function is at L->ci->next (if L->ci was updated to previous) */
      /* Note: luaO_executeVM updates L->ci to previous before returning 0 */
      if (L->ci->next->callstatus & CIST_FRESH)
        return;
      else {
        ci = L->ci;
        goto returning;
      }
    }
    /** vm_result == 1 means fallback to native VM */
  }
  
  k = cl->p->k;
  pc = ci->u.l.savedpc;
  if (l_unlikely(trap))
    trap = luaG_tracecall(L);
  base = ci->func.p + 1;
  /* main loop of interpreter */
  for (;;) {
    Instruction i;  /* instruction being executed */
    vmfetch();
    #if 0
    { /* low-level line tracing for debugging Lua */
      #include "lopnames.h"
      int pcrel = pcRel(pc, cl->p);
      printf("line: %d; %s (%d)\n", luaG_getfuncline(cl->p, pcrel),
             opnames[GET_OPCODE(i)], pcrel);
    }
    #endif
    lua_assert(base == ci->func.p + 1);
    lua_assert(base <= L->top.p && L->top.p <= L->stack_last.p);
    /* invalidate top for instructions not expecting it */
    lua_assert(isIT(i) || (cast_void(L->top.p = base), 1));
    vmdispatch (GET_OPCODE(i)) {
      vmcase(OP_MOVE) {
        StkId ra = RA(i);
        setobjs2s(L, ra, RB(i));
        vmbreak;
      }
      vmcase(OP_LOADI) {
        StkId ra = RA(i);
        lua_Integer b = GETARG_sBx(i);
        setivalue(s2v(ra), b);
        vmbreak;
      }
      vmcase(OP_LOADF) {
        StkId ra = RA(i);
        int b = GETARG_sBx(i);
        setfltvalue(s2v(ra), cast_num(b));
        vmbreak;
      }
      vmcase(OP_LOADK) {
        StkId ra = RA(i);
        TValue *rb = k + GETARG_Bx(i);
        setobj2s(L, ra, rb);
        vmbreak;
      }
      vmcase(OP_LOADKX) {
        StkId ra = RA(i);
        TValue *rb;
        rb = k + GETARG_Ax(*pc); pc++;
        setobj2s(L, ra, rb);
        vmbreak;
      }
      vmcase(OP_LOADFALSE) {
        StkId ra = RA(i);
        setbfvalue(s2v(ra));
        vmbreak;
      }
      vmcase(OP_LFALSESKIP) {
        StkId ra = RA(i);
        setbfvalue(s2v(ra));
        pc++;  /* skip next instruction */
        vmbreak;
      }
      vmcase(OP_LOADTRUE) {
        StkId ra = RA(i);
        setbtvalue(s2v(ra));
        vmbreak;
      }
      vmcase(OP_LOADNIL) {
        StkId ra = RA(i);
        int b = GETARG_B(i);
        do {
          setnilvalue(s2v(ra++));
        } while (b--);
        vmbreak;
      }
      vmcase(OP_GETUPVAL) {
        StkId ra = RA(i);
        int b = GETARG_B(i);
        setobj2s(L, ra, cl->upvals[b]->v.p);
        vmbreak;
      }
      vmcase(OP_SETUPVAL) {
        StkId ra = RA(i);
        UpVal *uv = cl->upvals[GETARG_B(i)];
        setobj(L, uv->v.p, s2v(ra));
        luaC_barrier(L, uv, s2v(ra));
        vmbreak;
      }
      vmcase(OP_GETTABUP) {
        StkId ra = RA(i);
        TValue *upval = cl->upvals[GETARG_B(i)]->v.p;
        TValue *rc = KC(i);
        TString *key = tsvalue(rc);  /* key must be a short string */
        if (ttistable(upval)) {
           Table *h = hvalue(upval);
           l_rwlock_rdlock(&h->lock);
           const TValue *res = luaH_getshortstr(h, key);
           if (!isempty(res)) {
              setobj2s(L, ra, res);
              l_rwlock_unlock(&h->lock);
           } else {
              l_rwlock_unlock(&h->lock);
              Protect(luaV_finishget(L, upval, rc, ra, NULL));
           }
        }
        else
          Protect(luaV_finishget(L, upval, rc, ra, NULL));
        vmbreak;
      }
      vmcase(OP_GETTABLE) {
        StkId ra = RA(i);
        TValue *rb = vRB(i);
        TValue *rc = vRC(i);
        if (ttistable(rb)) {
           Table *h = hvalue(rb);
           l_rwlock_rdlock(&h->lock);
           const TValue *res = luaH_get(h, rc);
           if (!isempty(res)) {
              setobj2s(L, ra, res);
              l_rwlock_unlock(&h->lock);
           } else {
              l_rwlock_unlock(&h->lock);
              Protect(luaV_finishget(L, rb, rc, ra, NULL));
           }
        }
        else
          Protect(luaV_finishget(L, rb, rc, ra, NULL));
        vmbreak;
      }
      vmcase(OP_GETI) {
        StkId ra = RA(i);
        TValue *rb = vRB(i);
        int c = GETARG_C(i);
        if (ttistable(rb)) {
           Table *h = hvalue(rb);
           l_rwlock_rdlock(&h->lock);
           const TValue *res = luaH_getint(h, c);
           if (!isempty(res)) {
              setobj2s(L, ra, res);
              l_rwlock_unlock(&h->lock);
           } else {
              l_rwlock_unlock(&h->lock);
              TValue key;
              setivalue(&key, c);
              Protect(luaV_finishget(L, rb, &key, ra, NULL));
           }
        }
        else {
          TValue key;
          setivalue(&key, c);
          Protect(luaV_finishget(L, rb, &key, ra, NULL));
        }
        vmbreak;
      }
      vmcase(OP_GETFIELD) {
        StkId ra = RA(i);
        TValue *rb = vRB(i);
        TValue *rc = KC(i);
        TString *key = tsvalue(rc);  /* key must be a short string */
        if (ttistable(rb)) {
           Table *h = hvalue(rb);
           l_rwlock_rdlock(&h->lock);
           const TValue *res = luaH_getshortstr(h, key);
           if (!isempty(res)) {
              setobj2s(L, ra, res);
              l_rwlock_unlock(&h->lock);
           } else {
              l_rwlock_unlock(&h->lock);
              Protect(luaV_finishget(L, rb, rc, ra, NULL));
           }
        }
        else
          Protect(luaV_finishget(L, rb, rc, ra, NULL));
        vmbreak;
      }
      vmcase(OP_SETTABUP) {
        TValue *upval = cl->upvals[GETARG_A(i)]->v.p;
        TValue *rb = KB(i);
        TValue *rc = RKC(i);
        TString *key = tsvalue(rb);  /* key must be a short string */
        if (ttistable(upval)) {
           Table *h = hvalue(upval);
           l_rwlock_wrlock(&h->lock);
           const TValue *res = luaH_getshortstr(h, key);
           if (!isempty(res) && !isabstkey(res)) {
              setobj2t(L, cast(TValue *, res), rc);
              luaC_barrierback(L, obj2gco(h), rc);
              l_rwlock_unlock(&h->lock);
           } else {
              l_rwlock_unlock(&h->lock);
              Protect(luaV_finishset(L, upval, rb, rc, NULL));
           }
        }
        else
          Protect(luaV_finishset(L, upval, rb, rc, NULL));
        vmbreak;
      }
      vmcase(OP_SETTABLE) {
        StkId ra = RA(i);
        TValue *rb = vRB(i);  /* key (table is in 'ra') */
        TValue *rc = RKC(i);  /* value */
        if (ttistable(s2v(ra))) {
           Table *h = hvalue(s2v(ra));
           l_rwlock_wrlock(&h->lock);
           const TValue *res = luaH_get(h, rb);
           if (!isempty(res) && !isabstkey(res)) {
              setobj2t(L, cast(TValue *, res), rc);
              luaC_barrierback(L, obj2gco(h), rc);
              l_rwlock_unlock(&h->lock);
           } else {
              l_rwlock_unlock(&h->lock);
              Protect(luaV_finishset(L, s2v(ra), rb, rc, NULL));
           }
        }
        else
          Protect(luaV_finishset(L, s2v(ra), rb, rc, NULL));
        vmbreak;
      }
      vmcase(OP_SETI) {
        StkId ra = RA(i);
        int c = GETARG_B(i);
        TValue *rc = RKC(i);
        if (ttistable(s2v(ra))) {
           Table *h = hvalue(s2v(ra));
           l_rwlock_wrlock(&h->lock);
           const TValue *res = luaH_getint(h, c);
           if (!isempty(res) && !isabstkey(res)) {
              setobj2t(L, cast(TValue *, res), rc);
              luaC_barrierback(L, obj2gco(h), rc);
              l_rwlock_unlock(&h->lock);
           } else {
              l_rwlock_unlock(&h->lock);
              TValue key;
              setivalue(&key, c);
              Protect(luaV_finishset(L, s2v(ra), &key, rc, NULL));
           }
        } else {
          TValue key;
          setivalue(&key, c);
          Protect(luaV_finishset(L, s2v(ra), &key, rc, NULL));
        }
        vmbreak;
      }
      vmcase(OP_SETFIELD) {
        StkId ra = RA(i);
        TValue *rb = KB(i);
        TValue *rc = RKC(i);
        TString *key = tsvalue(rb);  /* key must be a short string */
        if (ttistable(s2v(ra))) {
           Table *h = hvalue(s2v(ra));
           l_rwlock_wrlock(&h->lock);
           const TValue *res = luaH_getshortstr(h, key);
           if (!isempty(res) && !isabstkey(res)) {
              setobj2t(L, cast(TValue *, res), rc);
              luaC_barrierback(L, obj2gco(h), rc);
              l_rwlock_unlock(&h->lock);
           } else {
              l_rwlock_unlock(&h->lock);
              Protect(luaV_finishset(L, s2v(ra), rb, rc, NULL));
           }
        }
        else
          Protect(luaV_finishset(L, s2v(ra), rb, rc, NULL));
        vmbreak;
      }
      vmcase(OP_NEWTABLE) {
        StkId ra = RA(i);
        unsigned b = cast_uint(GETARG_B(i));  /* log2(hash size) + 1 */
        unsigned c = cast_uint(GETARG_C(i));  /* array size */
        Table *t;
        if (b > 0)
          b = 1u << (b - 1);  /* hash size is 2^(b - 1) */
        if (TESTARG_k(i)) {  /* non-zero extra argument? */
          lua_assert(GETARG_Ax(*pc) != 0);
          /* add it to array size */
          c += cast_uint(GETARG_Ax(*pc)) * (MAXARG_C + 1);
        }
        pc++;  /* skip extra argument */
        L->top.p = ra + 1;  /* correct top in case of emergency GC */
        t = luaH_new(L);  /* memory allocation */
        sethvalue2s(L, ra, t);
        if (b != 0 || c != 0)
          luaH_resize(L, t, c, b);  /* idem */
        checkGC(L, ra + 1);
        vmbreak;
      }
      vmcase(OP_LINKNAMESPACE) {
        StkId ra = RA(i);
        TValue *rb = vRB(i);
        if (ttisnamespace(s2v(ra)) && ttisnamespace(rb)) {
           Namespace *ns = nsvalue(s2v(ra));
           Namespace *target = nsvalue(rb);
           ns->using_next = target;
           luaC_objbarrier(L, ns, target);
        }
        else if (ttistable(s2v(ra)) && ttisnamespace(rb)) {
           Table *t = hvalue(s2v(ra));
           Namespace *target = nsvalue(rb);
           t->using_next = target;
           luaC_objbarrier(L, t, target);
        }
        vmbreak;
      }
      vmcase(OP_NEWNAMESPACE) {
        StkId ra = RA(i);
        TString *name = tsvalue(&k[GETARG_Bx(i)]);
        Namespace *ns = luaN_new(L, name);
        setnsvalue(L, s2v(ra), ns);
        checkGC(L, ra + 1);
        vmbreak;
      }
      vmcase(OP_SELF) {
        StkId ra = RA(i);
        TValue *rb = vRB(i);
        TValue *rc = RKC(i);
        TString *key = tsvalue(rc);  /* key must be a string */
        setobj2s(L, ra + 1, rb);
        if (ttistable(rb)) {
           Table *h = hvalue(rb);
           l_rwlock_rdlock(&h->lock);
           const TValue *res = luaH_getstr(h, key);
           if (!isempty(res)) {
              setobj2s(L, ra, res);
              l_rwlock_unlock(&h->lock);
           } else {
              l_rwlock_unlock(&h->lock);
              Protect(luaV_finishget(L, rb, rc, ra, NULL));
           }
        }
        else
          Protect(luaV_finishget(L, rb, rc, ra, NULL));
        vmbreak;
      }
      vmcase(OP_ADDI) {
        TValue *v1 = vRB(i);
        int imm = GETARG_sC(i);
        if (ttispointer(v1)) {
           StkId ra = RA(i);
           setptrvalue(s2v(ra), (char *)ptrvalue(v1) + imm);
           pc++;
        } else {
           op_arith_overflow_I(L, try_add, luai_numadd, luaB_add);
        }
        vmbreak;
      }
      vmcase(OP_ADDK) {
        TValue *v1 = vRB(i);
        TValue *v2 = KC(i);
        if (ttispointer(v1) && ttisinteger(v2)) {
          StkId ra = RA(i);
          setptrvalue(s2v(ra), (char *)ptrvalue(v1) + ivalue(v2));
          pc++;
        } else {
          op_arith_overflow_K(L, try_add, luai_numadd, luaB_add);
        }
        vmbreak;
      }
      vmcase(OP_SUBK) {
        TValue *v1 = vRB(i);
        TValue *v2 = KC(i);
        if (ttispointer(v1) && ttisinteger(v2)) {
          StkId ra = RA(i);
          setptrvalue(s2v(ra), (char *)ptrvalue(v1) - ivalue(v2));
          pc++;
        } else {
          op_arith_overflow_K(L, try_sub, luai_numsub, luaB_sub);
        }
        vmbreak;
      }
      vmcase(OP_MULK) {
        op_arith_overflow_K(L, try_mul, luai_nummul, luaB_mul);
        vmbreak;
      }
      vmcase(OP_MODK) {
        savestate(L, ci);  /* in case of division by 0 */
        op_arithK(L, luaV_mod, luaV_modf);
        vmbreak;
      }
      vmcase(OP_POWK) {
        op_arithfK(L, luai_numpow);
        vmbreak;
      }
      vmcase(OP_DIVK) {
        op_arithfK(L, luai_numdiv);
        vmbreak;
      }
      vmcase(OP_IDIVK) {
        savestate(L, ci);  /* in case of division by 0 */
        op_arithK(L, luaV_idiv, luai_numidiv);
        vmbreak;
      }
      vmcase(OP_BANDK) {
        op_bitwiseK(L, l_band);
        vmbreak;
      }
      vmcase(OP_BORK) {
        op_bitwiseK(L, l_bor);
        vmbreak;
      }
      vmcase(OP_BXORK) {
        op_bitwiseK(L, l_bxor);
        vmbreak;
      }
      vmcase(OP_SHLI) {
        StkId ra = RA(i);
        TValue *rb = vRB(i);
        int ic = GETARG_sC(i);
        lua_Integer ib;
        if (tointegerns(rb, &ib)) {
          pc++; setivalue(s2v(ra), luaV_shiftl(ic, ib));
        }
        vmbreak;
      }
      vmcase(OP_SHRI) {
        StkId ra = RA(i);
        TValue *rb = vRB(i);
        int ic = GETARG_sC(i);
        lua_Integer ib;
        if (tointegerns(rb, &ib)) {
          pc++; setivalue(s2v(ra), luaV_shiftl(ib, -ic));
        }
        vmbreak;
      }
      vmcase(OP_ADD) {
        TValue *v1 = vRB(i);
        TValue *v2 = vRC(i);
        if (ttispointer(v1) && ttisinteger(v2)) {
          StkId ra = RA(i);
          setptrvalue(s2v(ra), (char *)ptrvalue(v1) + ivalue(v2));
          pc++;
        } else if (ttisinteger(v1) && ttispointer(v2)) {
          StkId ra = RA(i);
          setptrvalue(s2v(ra), (char *)ptrvalue(v2) + ivalue(v1));
          pc++;
        } else {
          op_arith_overflow_aux(L, v1, v2, try_add, luai_numadd, luaB_add);
        }
        vmbreak;
      }
      vmcase(OP_SUB) {
        TValue *v1 = vRB(i);
        TValue *v2 = vRC(i);
        if (ttispointer(v1) && ttisinteger(v2)) {
          StkId ra = RA(i);
          setptrvalue(s2v(ra), (char *)ptrvalue(v1) - ivalue(v2));
          pc++;
        } else if (ttispointer(v1) && ttispointer(v2)) {
          StkId ra = RA(i);
          setivalue(s2v(ra), (char *)ptrvalue(v1) - (char *)ptrvalue(v2));
          pc++;
        } else {
          op_arith_overflow_aux(L, v1, v2, try_sub, luai_numsub, luaB_sub);
        }
        vmbreak;
      }
      vmcase(OP_MUL) {
        op_arith_overflow(L, try_mul, luai_nummul, luaB_mul);
        vmbreak;
      }
      vmcase(OP_MOD) {
        savestate(L, ci);  /* in case of division by 0 */
        op_arith(L, luaV_mod, luaV_modf);
        vmbreak;
      }
      vmcase(OP_POW) {
        op_arithf(L, luai_numpow);
        vmbreak;
      }
      vmcase(OP_DIV) {  /* float division (always with floats) */
        op_arithf(L, luai_numdiv);
        vmbreak;
      }
      vmcase(OP_IDIV) {  /* floor division */
        savestate(L, ci);  /* in case of division by 0 */
        op_arith(L, luaV_idiv, luai_numidiv);
        vmbreak;
      }
      vmcase(OP_BAND) {
        op_bitwise(L, l_band);
        vmbreak;
      }
      vmcase(OP_BOR) {
        op_bitwise(L, l_bor);
        vmbreak;
      }
      vmcase(OP_BXOR) {
        op_bitwise(L, l_bxor);
        vmbreak;
      }
      vmcase(OP_SHL) {
        op_bitwise(L, luaV_shiftl);
        vmbreak;
      }
      vmcase(OP_SHR) {
        op_bitwise(L, luaV_shiftr);
        vmbreak;
      }
      vmcase(OP_SPACESHIP) {
        /*
        ** Three-way comparison operator (spaceship operator): a <=> b
        ** Returns: -1 if a < b, 0 if a == b, 1 if a > b
        ** Supports number and string comparisons.
        */
        StkId ra = RA(i);
        TValue *rb = vRB(i);
        TValue *rc = vRC(i);
        lua_Integer result;
        
        if (ttisinteger(rb) && ttisinteger(rc)) {
          /* integer comparison */
          lua_Integer ib = ivalue(rb);
          lua_Integer ic = ivalue(rc);
          result = (ib < ic) ? -1 : ((ib > ic) ? 1 : 0);
        }
        else if (ttisnumber(rb) && ttisnumber(rc)) {
          /* number comparison */
          lua_Number nb, nc;
          if (ttisinteger(rb)) {
            nb = cast_num(ivalue(rb));
          } else {
            nb = fltvalue(rb);
          }
          if (ttisinteger(rc)) {
            nc = cast_num(ivalue(rc));
          } else {
            nc = fltvalue(rc);
          }
          result = (nb < nc) ? -1 : ((nb > nc) ? 1 : 0);
        }
        else if (ttisstring(rb) && ttisstring(rc)) {
          /* string comparison */
          int cmp = l_strcmp(tsvalue(rb), tsvalue(rc));
          result = (cmp < 0) ? -1 : ((cmp > 0) ? 1 : 0);
        }
        else {
          /* type mismatch or unsupported type */
          Protect(luaG_ordererror(L, rb, rc));
          result = 0;  /* never reached */
        }
        
        setivalue(s2v(ra), result);
        vmbreak;
      }
      vmcase(OP_MMBIN) {
        StkId ra = RA(i);
        Instruction pi = *(pc - 2);  /* original arith. expression */
        TValue *rb = vRB(i);
        TMS tm = (TMS)GETARG_C(i);
        StkId result = RA(pi);
        lua_assert(OP_ADD <= GET_OPCODE(pi) && GET_OPCODE(pi) <= OP_SHR);
        Protect(luaT_trybinTM(L, s2v(ra), rb, result, tm));
        vmbreak;
      }
      vmcase(OP_MMBINI) {
        StkId ra = RA(i);
        Instruction pi = *(pc - 2);  /* original arith. expression */
        int imm = GETARG_sB(i);
        TMS tm = (TMS)GETARG_C(i);
        int flip = GETARG_k(i);
        StkId result = RA(pi);
        Protect(luaT_trybiniTM(L, s2v(ra), imm, flip, result, tm));
        vmbreak;
      }
      vmcase(OP_MMBINK) {
        StkId ra = RA(i);
        Instruction pi = *(pc - 2);  /* original arith. expression */
        TValue *imm = KB(i);
        TMS tm = (TMS)GETARG_C(i);
        int flip = GETARG_k(i);
        StkId result = RA(pi);
        Protect(luaT_trybinassocTM(L, s2v(ra), imm, flip, result, tm));
        vmbreak;
      }
      vmcase(OP_UNM) {
        StkId ra = RA(i);
        TValue *rb = vRB(i);
        lua_Number nb;
        if (ttisinteger(rb)) {
          lua_Integer ib = ivalue(rb);
          setivalue(s2v(ra), intop(-, 0, ib));
        }
        else if (tonumberns(rb, nb)) {
          setfltvalue(s2v(ra), luai_numunm(L, nb));
        }
        else
          Protect(luaT_trybinTM(L, rb, rb, ra, TM_UNM));
        vmbreak;
      }
      vmcase(OP_BNOT) {
        StkId ra = RA(i);
        TValue *rb = vRB(i);
        lua_Integer ib;
        if (tointegerns(rb, &ib)) {
          setivalue(s2v(ra), intop(^, ~l_castS2U(0), ib));
        }
        else
          Protect(luaT_trybinTM(L, rb, rb, ra, TM_BNOT));
        vmbreak;
      }
      vmcase(OP_NOT) {
        StkId ra = RA(i);
        TValue *rb = vRB(i);
        if (l_isfalse(rb))
          setbtvalue(s2v(ra));
        else
          setbfvalue(s2v(ra));
        vmbreak;
      }
      vmcase(OP_LEN) {
        StkId ra = RA(i);
        Protect(luaV_objlen(L, ra, vRB(i)));
        vmbreak;
      }
      vmcase(OP_CONCAT) {
        StkId ra = RA(i);
        int n = GETARG_B(i);  /* number of elements to concatenate */
        L->top.p = ra + n;  /* mark the end of concat operands */
        ProtectNT(luaV_concat(L, n));
        checkGC(L, L->top.p); /* 'luaV_concat' ensures correct top */
        vmbreak;
      }
      vmcase(OP_CLOSE) {
        StkId ra = RA(i);
        lua_assert(!GETARG_B(i));  /* 'close must be alive */
        Protect(luaF_close(L, ra, LUA_OK, 1));
        vmbreak;
      }
      vmcase(OP_TBC) {
        StkId ra = RA(i);
        /* create new to-be-closed upvalue */
        halfProtect(luaF_newtbcupval(L, ra));
        vmbreak;
      }
      vmcase(OP_JMP) {
        dojump(ci, i, 0);
        vmbreak;
      }
      vmcase(OP_EQ) {
        StkId ra = RA(i);
        int cond;
        TValue *rb = vRB(i);
        Protect(cond = luaV_equalobj(L, s2v(ra), rb));
        docondjump();
        vmbreak;
      }
      vmcase(OP_LT) {
        op_order(L, l_lti, LTnum, lessthanothers);
        vmbreak;
      }
      vmcase(OP_LE) {
        op_order(L, l_lei, LEnum, lessequalothers);
        vmbreak;
      }
      vmcase(OP_EQK) {
        StkId ra = RA(i);
        TValue *rb = KB(i);
        /* basic types do not use '__eq'; we can use raw equality */
        int cond = luaV_rawequalobj(s2v(ra), rb);
        docondjump();
        vmbreak;
      }
      vmcase(OP_EQI) {
        StkId ra = RA(i);
        int cond;
        int im = GETARG_sB(i);
        if (ttisinteger(s2v(ra)))
          cond = (ivalue(s2v(ra)) == im);
        else if (ttisfloat(s2v(ra)))
          cond = luai_numeq(fltvalue(s2v(ra)), cast_num(im));
        else
          cond = 0;  /* other types cannot be equal to a number */
        docondjump();
        vmbreak;
      }
      vmcase(OP_LTI) {
        op_orderI(L, l_lti, luai_numlt, 0, TM_LT);
        vmbreak;
      }
      vmcase(OP_LEI) {
        op_orderI(L, l_lei, luai_numle, 0, TM_LE);
        vmbreak;
      }
      vmcase(OP_GTI) {
        op_orderI(L, l_gti, luai_numgt, 1, TM_LT);
        vmbreak;
      }
      vmcase(OP_GEI) {
        op_orderI(L, l_gei, luai_numge, 1, TM_LE);
        vmbreak;
      }
      vmcase(OP_TEST) {
        StkId ra = RA(i);
        int cond = !l_isfalse(s2v(ra));
        docondjump();
        vmbreak;
      }
      vmcase(OP_TESTSET) {
        StkId ra = RA(i);
        TValue *rb = vRB(i);
        if (l_isfalse(rb) == GETARG_k(i))
          pc++;
        else {
          setobj2s(L, ra, rb);
          donextjump(ci);
        }
        vmbreak;
      }
      vmcase(OP_CALL) {
        StkId ra = RA(i);
        CallInfo *newci;
        int b = GETARG_B(i);
        int nresults = GETARG_C(i) - 1;
        if (b != 0)  /* fixed number of arguments? */
          L->top.p = ra + b;  /* top signals number of arguments */
        /* else previous instruction set top */
        savepc(L);  /* in case of errors */
        
        if (ra <= L->top.p && ttisLclosure(s2v(ra))) {
          LClosure *cl = clLvalue(s2v(ra));
          Proto *p = cl->p;
          if (p->is_sleeping) {
            int nargs = cast_int(L->top.p - ra) - 1;
            if (p->call_queue == NULL) {
              p->call_queue = luaF_newcallqueue(L);
            }
            luaF_callqueuepush(L, p->call_queue, nargs);
            L->top.p = ra + nresults + 1;
            if (nresults >= 0) {
              for (int j = 1; j <= nresults; j++) {
                setnilvalue(s2v(ra + j - 1));
              }
            }
            vmbreak;
          }
        }
        
        if ((newci = luaD_precall(L, ra, nresults)) == LUA_NULLPTR)
          updatetrap(ci);  /* C call; nothing else to be done */
        else {  /* Lua call: run function in this same C frame */
          ci = newci;
          goto startfunc;
        }
        vmbreak;
      }
      vmcase(OP_TAILCALL) {
        StkId ra = RA(i);
        int b = GETARG_B(i);  /* number of arguments + 1 (function) */
        int n;  /* number of results when calling a C function */
        int nparams1 = GETARG_C(i);
        /* delta is virtual 'func' - real 'func' (vararg functions) */
        int delta = (nparams1) ? ci->u.l.nextraargs + nparams1 : 0;
        if (b != 0)
          L->top.p = ra + b;
        else  /* previous instruction set top */
          b = cast_int(L->top.p - ra);
        savepc(ci);  /* several calls here can raise errors */
        if (TESTARG_k(i)) {
          luaF_closeupval(L, base);  /* close upvalues from current call */
          lua_assert(L->tbclist.p < base);  /* no pending tbc variables */
          lua_assert(base == ci->func.p + 1);
        }
        if ((n = luaD_pretailcall(L, ci, ra, b, delta)) < 0)  /* Lua function? */
          goto startfunc;  /* execute the callee */
        else {  /* C function? */
          ci->func.p -= delta;  /* restore 'func' (if vararg) */
          luaD_poscall(L, ci, n);  /* finish caller */
          updatetrap(ci);  /* 'luaD_poscall' can change hooks */
          goto ret;  /* caller returns after the tail call */
        }
      }
      vmcase(OP_RETURN) {
        StkId ra = RA(i);
        int n = GETARG_B(i) - 1;  /* number of results */
        int nparams1 = GETARG_C(i);
        if (n < 0)  /* not fixed? */
          n = cast_int(L->top.p - ra);  /* get what is available */
        savepc(ci);
        if (TESTARG_k(i)) {  /* may there be open upvalues? */
          ci->u2.nres = n;  /* save number of returns */
          if (L->top.p < ci->top.p)
            L->top.p = ci->top.p;
          luaF_close(L, base, CLOSEKTOP, 1);
          updatetrap(ci);
          updatestack(ci);
        }
        if (nparams1)  /* vararg function? */
          ci->func.p -= ci->u.l.nextraargs + nparams1;
        L->top.p = ra + n;  /* set call for 'luaD_poscall' */
        luaD_poscall(L, ci, n);
        updatetrap(ci);  /* 'luaD_poscall' can change hooks */
        goto ret;
      }
      vmcase(OP_RETURN0) {
        if (l_unlikely(L->hookmask)) {
          StkId ra = RA(i);
          L->top.p = ra;
          savepc(ci);
          luaD_poscall(L, ci, 0);  /* no hurry... */
          trap = 1;
        }
        else {  /* do the 'poscall' here */
          int nres;
          L->ci = ci->previous;  /* back to caller */
          L->top.p = base - 1;
          for (nres = ci->nresults; l_unlikely(nres > 0); nres--)
            setnilvalue(s2v(L->top.p++));  /* all results are nil */
        }
        goto ret;
      }
      vmcase(OP_RETURN1) {
        if (l_unlikely(L->hookmask)) {
          StkId ra = RA(i);
          L->top.p = ra + 1;
          savepc(ci);
          luaD_poscall(L, ci, 1);  /* no hurry... */
          trap = 1;
        }
        else {  /* do the 'poscall' here */
          int nres = ci->nresults;
          L->ci = ci->previous;  /* back to caller */
          if (nres == 0)
            L->top.p = base - 1;  /* asked for no results */
          else {
            StkId ra = RA(i);
            setobjs2s(L, base - 1, ra);  /* at least this result */
            L->top.p = base;
            for (; l_unlikely(nres > 1); nres--)
              setnilvalue(s2v(L->top.p++));  /* complete missing results */
          }
        }
       ret:  /* return from a Lua function */
        if (ci->callstatus & CIST_FRESH)
          return;  /* end this frame */
        else {
          ci = ci->previous;
          goto returning;  /* continue running caller in this frame */
        }
      }
      vmcase(OP_FORLOOP) {
        StkId ra = RA(i);
        if (ttisinteger(s2v(ra + 2))) {  /* integer loop? */
          lua_Unsigned count = l_castS2U(ivalue(s2v(ra + 1)));
          if (count > 0) {  /* still more iterations? */
            lua_Integer step = ivalue(s2v(ra + 2));
            lua_Integer idx = ivalue(s2v(ra));  /* internal index */
            chgivalue(s2v(ra + 1), count - 1);  /* update counter */
            idx = intop(+, idx, step);  /* add step to index */
            chgivalue(s2v(ra), idx);  /* update internal index */
            setivalue(s2v(ra + 3), idx);  /* and control variable */
            pc -= GETARG_Bx(i);  /* jump back */
          }
        }
        else if (floatforloop(ra))  /* float loop */
          pc -= GETARG_Bx(i);  /* jump back */
        updatetrap(ci);  /* allows a signal to break the loop */
        vmbreak;
      }
      vmcase(OP_FORPREP) {
        StkId ra = RA(i);
        savestate(L, ci);  /* in case of errors */
        if (forprep(L, ra))
          pc += GETARG_Bx(i) + 1;  /* skip the loop */
        vmbreak;
      }
      vmcase(OP_TFORPREP) {
       StkId ra = RA(i);
        /* implicit pairs */
        if (ttistable(s2v(ra))
          && l_likely(!fasttm(L, hvalue(s2v(ra))->metatable, TM_CALL))
        ) {
          setobjs2s(L, ra + 1, ra);
          setfvalue(s2v(ra), luaB_next);
        }
        /* create to-be-closed upvalue (if needed) */
        halfProtect(luaF_newtbcupval(L, ra + 3));
        pc += GETARG_Bx(i);
        i = *(pc++);  /* go to next instruction */
        lua_assert(GET_OPCODE(i) == OP_TFORCALL && ra == RA(i));
        goto l_tforcall;
      }
      vmcase(OP_TFORCALL) {
       l_tforcall: {
        StkId ra = RA(i);
        /* 'ra' has the iterator function, 'ra + 1' has the state,
           'ra + 2' has the control variable, and 'ra + 3' has the
           to-be-closed variable. The call will use the stack after
           these values (starting at 'ra + 4')
        */
        /* push function, state, and control variable */
        memcpy(ra + 4, ra, 3 * sizeof(*ra));
        L->top.p = ra + 4 + 3;
        ProtectNT(luaD_call(L, ra + 4, GETARG_C(i)));  /* do the call */
        updatestack(ci);  /* stack may have changed */
        i = *(pc++);  /* go to next instruction */
        lua_assert(GET_OPCODE(i) == OP_TFORLOOP && ra == RA(i));
        goto l_tforloop;
      }}
      vmcase(OP_TFORLOOP) {
       l_tforloop: {
        StkId ra = RA(i);
        if (!ttisnil(s2v(ra + 4))) {  /* continue loop? */
          setobjs2s(L, ra + 2, ra + 4);  /* save control variable */
          pc -= GETARG_Bx(i);  /* jump back */
        }
        vmbreak;
      }}
      vmcase(OP_SETLIST) {
        StkId ra = RA(i);
        int n = GETARG_B(i);
        unsigned int last = GETARG_C(i);
        Table *h = hvalue(s2v(ra));
        if (n == 0)
          n = cast_int(L->top.p - ra) - 1;  /* get up to the top */
        else
          L->top.p = ci->top.p;  /* correct top in case of emergency GC */
        last += n;
        if (TESTARG_k(i)) {
          last += GETARG_Ax(*pc) * (MAXARG_C + 1);
          pc++;
        }
        if (last > luaH_realasize(h))  /* needs more space? */
          luaH_resizearray(L, h, last);  /* preallocate it at once */
        for (; n > 0; n--) {
          TValue *val = s2v(ra + n);
          setobj2t(L, &h->array[last - 1], val);
          last--;
          luaC_barrierback(L, obj2gco(h), val);
        }
        vmbreak;
      }
      vmcase(OP_CLOSURE) {
        StkId ra = RA(i);
        Proto *p = cl->p->p[GETARG_Bx(i)];
        halfProtect(pushclosure(L, p, cl->upvals, base, ra));
        checkGC(L, ra + 1);
        vmbreak;
      }
      vmcase(OP_NEWCONCEPT) {
        StkId ra = RA(i);
        Proto *p = cl->p->p[GETARG_Bx(i)];
        halfProtect(pushconcept(L, p, cl->upvals, base, ra));
        checkGC(L, ra + 1);
        vmbreak;
      }
      vmcase(OP_VARARG) {
        StkId ra = RA(i);
        int n = GETARG_C(i) - 1;  /* required results (-1 means all) */
        int vatab = GETARG_k(i) ? GETARG_B(i) : -1;
        Protect(luaT_getvarargs(L, ci, ra, n));
        vmbreak;
      }
      vmcase(OP_GETVARG) {
        StkId ra = RA(i);
        TValue *rc = vRC(i);
        luaT_getvararg(L, ci, ra, rc);
        vmbreak;
      }
      vmcase(OP_ERRNNIL) {
        TValue *ra = vRA(i);
        if (!ttisnil(ra))
          halfProtect(luaG_errnnil(L, cl, GETARG_Bx(i)));
        vmbreak;
      }
      vmcase(OP_VARARGPREP) {
        ProtectNT(luaT_adjustvarargs(L, GETARG_A(i), ci, cl->p));
        if (l_unlikely(trap)) {  /* previous "Protect" updated trap */
          luaD_hookcall(L, ci);
          L->oldpc = 1;  /* next opcode will be seen as a "new" line */
        }
        updatebase(ci);  /* function has new base after adjustment */
        vmbreak;
      }
      vmcase(OP_IS) {
        /*
        ** OP_IS: Check if R[A] is of type K[B]
        ** R[A] is K[B] - checks if type of R[A] matches string K[B]
        ** Supports __type metamethod for custom type names
        */
        TValue *ra = vRA(i);
        TValue *rb = KB(i);
        const char *typename_expected;
        const char *typename_actual;
        int cond;
        
        /* Expected type name must be a string */
        lua_assert(ttisstring(rb));
        typename_expected = getstr(tsvalue(rb));
        
        /* Try to get __type metamethod */
        const TValue *tm = luaT_gettmbyobj(L, ra, TM_TYPE);
        if (!notm(tm) && ttisstring(tm)) {
          /* Use type name from __type metamethod */
          typename_actual = getstr(tsvalue(tm));
        }
        else {
          /* Use standard type name */
          typename_actual = luaT_objtypename(L, ra);
        }
        
        /* Compare type names */
        cond = (strcmp(typename_actual, typename_expected) == 0);
        docondjump();
        vmbreak;
      }
      vmcase(OP_TESTNIL) {
        /*
        ** OP_TESTNIL: Nil test instruction (for optional chaining)
        ** Format: OP_TESTNIL A B k
        ** Function: if (R[B] is nil) != k then pc++
        ** Arguments:
        **   A - Unused (reserved)
        **   B - Source register (value to test)
        **   k - Condition flag (0: skip if nil, 1: skip if not nil)
        ** Use cases:
        **   k=1 for optional chaining: a?.b -> skip JMP if not nil
        **   k=0 for null coalescing: a ?? b -> skip JMP if nil
        */
        TValue *rb = vRB(i);
        if (ttisnil(rb) != GETARG_k(i))
          pc++;  /* Condition not met, skip next instruction */
        vmbreak;
      }
      /*
      ** =====================================================================
      ** Object-Oriented System Opcodes
      ** =====================================================================
      */
      vmcase(OP_NEWCLASS) {
        /*
        ** Create new class
        ** Format: OP_NEWCLASS A Bx
        ** Function: R[A] := create new class with name K[Bx]
        */
        TString *classname = tsvalue(&k[GETARG_Bx(i)]);
        
        /* Manually save state */
        savepc(L);
        
        /* Call luaC_newclass, creates class table at stack top */
        luaC_newclass(L, classname);
        
        /* Re-fetch ra after potential stack reallocation */
        base = ci->func.p + 1;
        StkId ra = RA(i);
        
        /* Move created class from top to target register */
        setobj2s(L, ra, s2v(L->top.p - 1));
        L->top.p--;  /* Pop class table */
        
        updatetrap(ci);
        checkGC(L, ra + 1);
        vmbreak;
      }
      vmcase(OP_INHERIT) {
        /*
        ** Set class inheritance
        ** Format: OP_INHERIT A B
        ** Function: R[A].__parent := R[B]
        */
        StkId ra = RA(i);
        TValue *rb = vRB(i);
        /* Protect call */
        savestate(L, ci);
        setobj2s(L, L->top.p, s2v(ra));
        L->top.p++;
        setobj2s(L, L->top.p, rb);
        L->top.p++;
        /* Call inherit function */
        luaC_inherit(L, -2, -1);
        L->top.p -= 2;
        updatetrap(ci);
        vmbreak;
      }
      vmcase(OP_GETSUPER) {
        /*
        ** Get super method
        ** Format: OP_GETSUPER A B C
        ** Function: R[A] := R[B].__parent[K[C]:shortstring]
        */
        StkId ra = RA(i);
        TValue *rb = vRB(i);
        TString *key = tsvalue(&k[GETARG_C(i)]);
        /* Protect call */
        savestate(L, ci);
        setobj2s(L, L->top.p, rb);
        L->top.p++;
        /* Call super get function */
        luaC_super(L, -1, key);
        base = ci->func.p + 1;
        ra = RA(i);
        setobj2s(L, ra, s2v(L->top.p - 1));
        L->top.p -= 2;
        updatetrap(ci);
        vmbreak;
      }
      vmcase(OP_SETMETHOD) {
        /*
        ** Set class method
        ** Format: OP_SETMETHOD A B C
        ** Function: R[A][K[B]:shortstring] := R[C]
        */
        StkId ra = RA(i);
        TString *key = tsvalue(&k[GETARG_B(i)]);
        TValue *rc = vRC(i);
        /* Protect call */
        savestate(L, ci);
        setobj2s(L, L->top.p, s2v(ra));
        L->top.p++;
        setobj2s(L, L->top.p, rc);
        L->top.p++;
        /* Call set method function */
        luaC_setmethod(L, -2, key, -1);
        L->top.p -= 2;
        updatetrap(ci);
        vmbreak;
      }
      vmcase(OP_SETSTATIC) {
        /*
        ** Set static member
        ** Format: OP_SETSTATIC A B C
        ** Function: R[A].__static[K[B]:shortstring] := R[C]
        */
        StkId ra = RA(i);
        TString *key = tsvalue(&k[GETARG_B(i)]);
        TValue *rc = vRC(i);
        /* Protect call */
        savestate(L, ci);
        setobj2s(L, L->top.p, s2v(ra));
        L->top.p++;
        setobj2s(L, L->top.p, rc);
        L->top.p++;
        /* Call set static function */
        luaC_setstatic(L, -2, key, -1);
        L->top.p -= 2;
        updatetrap(ci);
        vmbreak;
      }
      vmcase(OP_NEWOBJ) {
        /*
        ** Create class instance
        ** Format: OP_NEWOBJ A B C
        ** Function: R[A] := R[B](args...), C-1 arguments
        */
        StkId ra = RA(i);
        TValue *rb = vRB(i);
        int nargs = GETARG_C(i) - 1;
        /* Protect call */
        savestate(L, ci);
        /* Push class */
        setobj2s(L, L->top.p, rb);
        L->top.p++;
        /* Copy arguments */
        for (int j = 0; j < nargs; j++) {
          setobj2s(L, L->top.p, s2v(ra + 1 + j));
          L->top.p++;
        }
        /* Call new object function */
        luaC_newobject(L, -(nargs + 1), nargs);
        base = ci->func.p + 1;
        ra = RA(i);
        setobj2s(L, ra, s2v(L->top.p - 1));
        L->top.p -= (nargs + 2);
        updatetrap(ci);
        checkGC(L, ra + 1);
        vmbreak;
      }
      vmcase(OP_GETPROP) {
        /*
        ** Get property (considering inheritance chain)
        ** Format: OP_GETPROP A B C
        ** Function: R[A] := R[B][K[C]:shortstring]
        */
        StkId ra = RA(i);
        TValue *rb = vRB(i);
        TString *key = tsvalue(&k[GETARG_C(i)]);
        /* Protect call */
        savestate(L, ci);
        setobj2s(L, L->top.p, rb);
        L->top.p++;
        /* Call get property function */
        luaC_getprop(L, -1, key);
        base = ci->func.p + 1;
        ra = RA(i);
        setobj2s(L, ra, s2v(L->top.p - 1));
        L->top.p -= 2;
        updatetrap(ci);
        vmbreak;
      }
      vmcase(OP_SETPROP) {
        /*
        ** Set object property
        ** Format: OP_SETPROP A B C
        ** Function: R[A][K[B]:shortstring] := RK(C)
        */
        StkId ra = RA(i);
        TString *key = tsvalue(&k[GETARG_B(i)]);
        TValue *rc = RKC(i);
        /* Protect call */
        savestate(L, ci);
        setobj2s(L, L->top.p, s2v(ra));
        L->top.p++;
        setobj2s(L, L->top.p, rc);
        L->top.p++;
        /* Call set property function */
        luaC_setprop(L, -2, key, -1);
        L->top.p -= 2;
        updatetrap(ci);
        vmbreak;
      }
      vmcase(OP_INSTANCEOF) {
        /*
        ** Check instance type
        ** Format: OP_INSTANCEOF A B C k
        ** Function: if ((R[A] instanceof R[B]) ~= k) then pc++
        */

        /* Ensure stack has enough space */
        luaD_checkstack(L, 2);

        /* Re-fetch registers */
        base = ci->func.p + 1;
        StkId ra = RA(i);
        TValue *rb = vRB(i);
        int result;

        /* Protect call */
        savestate(L, ci);
        setobj2s(L, L->top.p, s2v(ra));
        L->top.p++;
        setobj2s(L, L->top.p, rb);
        L->top.p++;
        /* Check instanceof */
        result = luaC_instanceof(L, -2, -1);
        L->top.p -= 2;
        updatetrap(ci);
        if (result != GETARG_k(i))
          pc++;  /* Condition not met, skip */
        vmbreak;
      }
      vmcase(OP_IMPLEMENT) {
        /*
        ** Implement interface
        ** Format: OP_IMPLEMENT A B
        ** Function: R[A] implements R[B]
        */
        StkId ra = RA(i);
        TValue *rb = vRB(i);
        /* Protect call */
        savestate(L, ci);
        setobj2s(L, L->top.p, s2v(ra));
        L->top.p++;
        setobj2s(L, L->top.p, rb);
        L->top.p++;
        /* Call implement function */
        luaC_implement(L, -2, -1);
        L->top.p -= 2;
        updatetrap(ci);
        vmbreak;
      }
      vmcase(OP_SETIFACEFLAG) {
        /*
        ** Set interface flag
        ** Format: OP_SETIFACEFLAG A
        ** Function: Mark R[A] as an interface
        */
        StkId ra = RA(i);
        if (ttistable(s2v(ra))) {
          Table *t = hvalue(s2v(ra));
          /* Set __flags field */
          TValue key, val;
          setsvalue(L, &key, luaS_newliteral(L, "__flags"));
          /* Get current flags */
          const TValue *oldflags = luaH_getstr(t, tsvalue(&key));
          lua_Integer flags = ttisinteger(oldflags) ? ivalue(oldflags) : 0;
          flags |= CLASS_FLAG_INTERFACE;
          setivalue(&val, flags);
          luaH_set(L, t, &key, &val);
        }
        vmbreak;
      }
      vmcase(OP_ADDMETHOD) {
        /*
        ** Add method signature to interface
        ** Format: OP_ADDMETHOD A B C
        ** Function: R[A].__methods[K[B]] := C (param count)
        */
        StkId ra = RA(i);
        TString *method_name = tsvalue(&k[GETARG_B(i)]);
        int param_count = GETARG_C(i);
        if (ttistable(s2v(ra))) {
          Table *t = hvalue(s2v(ra));
          /* Get __methods table */
          TValue key;
          setsvalue(L, &key, luaS_newliteral(L, "__methods"));
          const TValue *methods_tv = luaH_getstr(t, tsvalue(&key));
          if (ttistable(methods_tv)) {
            Table *methods = hvalue(methods_tv);
            /* Set method signature */
            TValue method_key, method_val;
            setsvalue(L, &method_key, method_name);
            setivalue(&method_val, param_count);
            luaH_set(L, methods, &method_key, &method_val);
          }
        }
        vmbreak;
      }
      vmcase(OP_IN) {
        StkId ra = RA(i);
        TValue *a = vRB(i);
        TValue *b = vRC(i);
        inopr(L, ra, a, b);
        vmbreak;
      }
      vmcase(OP_SLICE) {
        /*
        ** Slice operation - Python-style slice t[start:end:step]
        ** Format: OP_SLICE A B C
        ** Function: R[A] := slice(R[B], R[B+1], R[B+2], R[B+3])
        **   B = Source table
        **   B+1 = start (nil -> 1)
        **   B+2 = end (nil -> #t)
        **   B+3 = step (nil -> 1)
        **   C = Flags (reserved)
        **
        ** Supports negative indices. Result includes end element.
        */
        StkId ra = RA(i);
        int b = GETARG_B(i);
        StkId base_reg = base + b;
        TValue *src_table = s2v(base_reg);
        TValue *start_val = s2v(base_reg + 1);
        TValue *end_val = s2v(base_reg + 2);
        TValue *step_val = s2v(base_reg + 3);
        
        Table *t;
        Table *result_t;
        lua_Integer tlen;
        lua_Integer start_idx, end_idx, step;
        lua_Integer result_idx;
        
        /* Check if source is a table */
        if (l_unlikely(!ttistable(src_table))) {
          luaG_typeerror(L, src_table, "slice");
        }
        t = hvalue(src_table);
        tlen = luaH_getn(t);
        
        /* Parse start */
        if (ttisnil(start_val)) {
          start_idx = 1;
        }
        else if (ttisinteger(start_val)) {
          start_idx = ivalue(start_val);
        }
        else if (ttisfloat(start_val)) {
          lua_Number n = fltvalue(start_val);
          lua_Integer ni;
          if (luaV_flttointeger(n, &ni, F2Ieq)) {
            start_idx = ni;
          }
          else {
            luaG_runerror(L, "slice start index must be integer");
          }
        }
        else {
          luaG_runerror(L, "slice start index must be integer or nil");
        }
        
        /* Parse end */
        if (ttisnil(end_val)) {
          end_idx = tlen;
        }
        else if (ttisinteger(end_val)) {
          end_idx = ivalue(end_val);
        }
        else if (ttisfloat(end_val)) {
          lua_Number n = fltvalue(end_val);
          lua_Integer ni;
          if (luaV_flttointeger(n, &ni, F2Ieq)) {
            end_idx = ni;
          }
          else {
            luaG_runerror(L, "slice end index must be integer");
          }
        }
        else {
          luaG_runerror(L, "slice end index must be integer or nil");
        }
        
        /* Parse step */
        if (ttisnil(step_val)) {
          step = 1;
        }
        else if (ttisinteger(step_val)) {
          step = ivalue(step_val);
        }
        else if (ttisfloat(step_val)) {
          lua_Number n = fltvalue(step_val);
          lua_Integer ni;
          if (luaV_flttointeger(n, &ni, F2Ieq)) {
            step = ni;
          }
          else {
            luaG_runerror(L, "slice step must be integer");
          }
        }
        else {
          luaG_runerror(L, "slice step must be integer or nil");
        }
        
        if (step == 0) {
          luaG_runerror(L, "slice step cannot be 0");
        }
        
        /* Handle negative indices */
        if (start_idx < 0) {
          start_idx = tlen + start_idx + 1;
        }
        if (end_idx < 0) {
          end_idx = tlen + end_idx + 1;
        }
        
        /* Clamp indices */
        if (step > 0) {
          if (start_idx < 1) start_idx = 1;
          if (end_idx > tlen) end_idx = tlen;
        }
        else {
          if (start_idx > tlen) start_idx = tlen;
          if (end_idx < 1) end_idx = 1;
        }
        
        /* Create result table */
        L->top.p = ra + 1;
        result_t = luaH_new(L);
        sethvalue2s(L, ra, result_t);
        
        /* Copy elements */
        result_idx = 1;
        if (step > 0) {
          lua_Integer idx;
          for (idx = start_idx; idx <= end_idx; idx += step) {
            const TValue *val = luaH_getint(t, idx);
            if (!ttisnil(val)) {
              TValue temp;
              setobj(L, &temp, val);
              luaH_setint(L, result_t, result_idx, &temp);
            }
            result_idx++;
          }
        }
        else {  /* step < 0 */
          lua_Integer idx;
          for (idx = start_idx; idx >= end_idx; idx += step) {
            const TValue *val = luaH_getint(t, idx);
            if (!ttisnil(val)) {
              TValue temp;
              setobj(L, &temp, val);
              luaH_setint(L, result_t, result_idx, &temp);
            }
            result_idx++;
          }
        }
        
        checkGC(L, ra + 1);
        vmbreak;
      }
      vmcase(OP_NOP) {
        /*
        ** No Operation
        ** Used for padding or obfuscation.
        */
        UNUSED(GETARG_A(i));
        UNUSED(GETARG_B(i));
        UNUSED(GETARG_C(i));
        vmbreak;
      }
      vmcase(OP_CASE) {
        StkId ra = RA(i);
        TValue rb; setobj(L, &rb, vRB(i)); /* copy B before it might be overwritten */
        TValue rc; setobj(L, &rc, vRC(i)); /* copy C before it might be overwritten */
        Table *t;
        L->top.p = ra + 1;  /* correct top in case of emergency GC */
        t = luaH_new(L);  /* memory allocation */
        sethvalue2s(L, ra, t);
        /* t[1] = RB */
        luaH_setint(L, t, 1, &rb);
        /* t[2] = RC */
        luaH_setint(L, t, 2, &rc);
        checkGC(L, ra + 1);
        vmbreak;
      }
      vmcase(OP_EXTRAARG) {
        lua_assert(0);
        vmbreak;
      }
    }
  }
}

/* }======================================================= */

Instruction luaV_getinst(const Proto *p, int pc) {
  return p->code[pc];
}
