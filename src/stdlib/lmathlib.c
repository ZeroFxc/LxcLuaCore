/*
** $Id: lmathlib.c $
** Standard mathematical library
** See Copyright Notice in lua.h
*/

#define lmathlib_c
#define LUA_LIB

#include "lprefix.h"


#include <float.h>
#include <limits.h>
#include <math.h>
#include <stdlib.h>
#include <time.h>

#include "lua.h"

#include "lauxlib.h"
#include "lualib.h"
#include "lmem.h"
#include "llimits.h"

#undef PI
#define PI	(l_mathop(3.141592653589793238462643383279502884))


static int math_abs (lua_State *L) {
  if (lua_isinteger(L, 1)) {
    lua_Integer n = lua_tointeger(L, 1);
    if (n < 0) n = (lua_Integer)(0u - (lua_Unsigned)n);
    lua_pushinteger(L, n);
  }
  else
    lua_pushnumber(L, l_mathop(fabs)(luaL_checknumber(L, 1)));
  return 1;
}

static int math_sin (lua_State *L) {
  lua_pushnumber(L, l_mathop(sin)(luaL_checknumber(L, 1)));
  return 1;
}

static int math_cos (lua_State *L) {
  lua_pushnumber(L, l_mathop(cos)(luaL_checknumber(L, 1)));
  return 1;
}

static int math_tan (lua_State *L) {
  lua_pushnumber(L, l_mathop(tan)(luaL_checknumber(L, 1)));
  return 1;
}

static int math_asin (lua_State *L) {
  lua_pushnumber(L, l_mathop(asin)(luaL_checknumber(L, 1)));
  return 1;
}

static int math_acos (lua_State *L) {
  lua_pushnumber(L, l_mathop(acos)(luaL_checknumber(L, 1)));
  return 1;
}

static int math_atan (lua_State *L) {
  lua_Number y = luaL_checknumber(L, 1);
  lua_Number x = luaL_optnumber(L, 2, 1);
  lua_pushnumber(L, l_mathop(atan2)(y, x));
  return 1;
}


static int math_toint (lua_State *L) {
  int valid;
  lua_Integer n = lua_tointegerx(L, 1, &valid);
  if (l_likely(valid))
    lua_pushinteger(L, n);
  else {
    luaL_checkany(L, 1);
    luaL_pushfail(L);  /* value is not convertible to integer */
  }
  return 1;
}


static void pushnumint (lua_State *L, lua_Number d) {
  lua_Integer n;
  if (lua_numbertointeger(d, &n))  /* does 'd' fit in an integer? */
    lua_pushinteger(L, n);  /* result is integer */
  else
    lua_pushnumber(L, d);  /* result is float */
}


static int math_floor (lua_State *L) {
  if (lua_isinteger(L, 1))
    lua_settop(L, 1);  /* integer is its own floor */
  else {
    lua_Number d = l_mathop(floor)(luaL_checknumber(L, 1));
    pushnumint(L, d);
  }
  return 1;
}


static int math_ceil (lua_State *L) {
  if (lua_isinteger(L, 1))
    lua_settop(L, 1);  /* integer is its own ceiling */
  else {
    lua_Number d = l_mathop(ceil)(luaL_checknumber(L, 1));
    pushnumint(L, d);
  }
  return 1;
}


static int math_fmod (lua_State *L) {
  if (lua_isinteger(L, 1) && lua_isinteger(L, 2)) {
    lua_Integer d = lua_tointeger(L, 2);
    if ((lua_Unsigned)d + 1u <= 1u) {  /* special cases: -1 or 0 */
      luaL_argcheck(L, d != 0, 2, "zero");
      lua_pushinteger(L, 0);  /* avoid overflow with 0x80000... / -1 */
    }
    else
      lua_pushinteger(L, lua_tointeger(L, 1) % d);
  }
  else
    lua_pushnumber(L, l_mathop(fmod)(luaL_checknumber(L, 1),
                                     luaL_checknumber(L, 2)));
  return 1;
}


/*
** next function does not use 'modf', avoiding problems with 'double*'
** (which is not compatible with 'float*') when lua_Number is not
** 'double'.
*/
static int math_modf (lua_State *L) {
  if (lua_isinteger(L ,1)) {
    lua_settop(L, 1);  /* number is its own integer part */
    lua_pushnumber(L, 0);  /* no fractional part */
  }
  else {
    lua_Number n = luaL_checknumber(L, 1);
    /* integer part (rounds toward zero) */
    lua_Number ip = (n < 0) ? l_mathop(ceil)(n) : l_mathop(floor)(n);
    pushnumint(L, ip);
    /* fractional part (test needed for inf/-inf) */
    lua_pushnumber(L, (n == ip) ? l_mathop(0.0) : (n - ip));
  }
  return 2;
}


static int math_sqrt (lua_State *L) {
  lua_pushnumber(L, l_mathop(sqrt)(luaL_checknumber(L, 1)));
  return 1;
}


static int math_ult (lua_State *L) {
  lua_Integer a = luaL_checkinteger(L, 1);
  lua_Integer b = luaL_checkinteger(L, 2);
  lua_pushboolean(L, (lua_Unsigned)a < (lua_Unsigned)b);
  return 1;
}


static int math_log (lua_State *L) {
  lua_Number x = luaL_checknumber(L, 1);
  lua_Number res;
  if (lua_isnoneornil(L, 2))
    res = l_mathop(log)(x);
  else {
    lua_Number base = luaL_checknumber(L, 2);
#if !defined(LUA_USE_C89)
    if (base == l_mathop(2.0))
      res = l_mathop(log2)(x);
    else
#endif
    if (base == l_mathop(10.0))
      res = l_mathop(log10)(x);
    else
      res = l_mathop(log)(x)/l_mathop(log)(base);
  }
  lua_pushnumber(L, res);
  return 1;
}


static int math_exp (lua_State *L) {
  lua_pushnumber(L, l_mathop(exp)(luaL_checknumber(L, 1)));
  return 1;
}

static int math_deg (lua_State *L) {
  lua_pushnumber(L, luaL_checknumber(L, 1) * (l_mathop(180.0) / PI));
  return 1;
}

static int math_rad (lua_State *L) {
  lua_pushnumber(L, luaL_checknumber(L, 1) * (PI / l_mathop(180.0)));
  return 1;
}


static int math_min (lua_State *L) {
  int n = lua_gettop(L);  /* number of arguments */
  int imin = 1;  /* index of current minimum value */
  int i;
  luaL_argcheck(L, n >= 1, 1, "value expected");
  for (i = 2; i <= n; i++) {
    if (lua_compare(L, i, imin, LUA_OPLT))
      imin = i;
  }
  lua_pushvalue(L, imin);
  return 1;
}


static int math_max (lua_State *L) {
  int n = lua_gettop(L);  /* number of arguments */
  int imax = 1;  /* index of current maximum value */
  int i;
  luaL_argcheck(L, n >= 1, 1, "value expected");
  for (i = 2; i <= n; i++) {
    if (lua_compare(L, imax, i, LUA_OPLT))
      imax = i;
  }
  lua_pushvalue(L, imax);
  return 1;
}


static int math_type (lua_State *L) {
  if (lua_type(L, 1) == LUA_TNUMBER)
    lua_pushstring(L, (lua_isinteger(L, 1)) ? "integer" : "float");
  else {
    luaL_checkany(L, 1);
    luaL_pushfail(L);
  }
  return 1;
}



/*
** {=======================================================
** Pseudo-Random Number Generator based on 'xoshiro256**'.
** ========================================================
*/

/*
** This code uses lots of shifts. ANSI C does not allow shifts greater
** than or equal to the width of the type being shifted, so some shifts
** are written in convoluted ways to match that restriction. For
** preprocessor tests, it assumes a width of 32 bits, so the maximum
** shift there is 31 bits.
*/


/* number of binary digits in the mantissa of a float */
#define FIGS	l_floatatt(MANT_DIG)

#if FIGS > 64
/* there are only 64 random bits; use them all */
#undef FIGS
#define FIGS	64
#endif


/*
** LUA_RAND32 forces the use of 32-bit integers in the implementation
** of the PRN generator (mainly for testing).
*/
#if !defined(LUA_RAND32) && !defined(Rand64)

/* try to find an integer type with at least 64 bits */

#if ((ULONG_MAX >> 31) >> 31) >= 3

/* 'long' has at least 64 bits */
#define Rand64		unsigned long
#define SRand64		long

#elif !defined(LUA_USE_C89) && defined(LLONG_MAX)

/* there is a 'long long' type (which must have at least 64 bits) */
#define Rand64		unsigned long long
#define SRand64		long long

#elif ((LUA_MAXUNSIGNED >> 31) >> 31) >= 3

/* 'lua_Unsigned' has at least 64 bits */
#define Rand64		lua_Unsigned
#define SRand64		lua_Integer

#endif

#endif


#if defined(Rand64)  /* { */

/*
** Standard implementation, using 64-bit integers.
** If 'Rand64' has more than 64 bits, the extra bits do not interfere
** with the 64 initial bits, except in a right shift. Moreover, the
** final result has to discard the extra bits.
*/

/* avoid using extra bits when needed */
#define trim64(x)	((x) & 0xffffffffffffffffu)


/* rotate left 'x' by 'n' bits */
static Rand64 rotl (Rand64 x, int n) {
  return (x << n) | (trim64(x) >> (64 - n));
}

static Rand64 nextrand (Rand64 *state) {
  Rand64 state0 = state[0];
  Rand64 state1 = state[1];
  Rand64 state2 = state[2] ^ state0;
  Rand64 state3 = state[3] ^ state1;
  Rand64 res = rotl(state1 * 5, 7) * 9;
  state[0] = state0 ^ state3;
  state[1] = state1 ^ state2;
  state[2] = state2 ^ (state1 << 17);
  state[3] = rotl(state3, 45);
  return res;
}


/*
** Convert bits from a random integer into a float in the
** interval [0,1), getting the higher FIG bits from the
** random unsigned integer and converting that to a float.
** Some old Microsoft compilers cannot cast an unsigned long
** to a floating-point number, so we use a signed long as an
** intermediary. When lua_Number is float or double, the shift ensures
** that 'sx' is non negative; in that case, a good compiler will remove
** the correction.
*/

/* must throw out the extra (64 - FIGS) bits */
#define shift64_FIG	(64 - FIGS)

/* 2^(-FIGS) == 2^-1 / 2^(FIGS-1) */
#define scaleFIG	(l_mathop(0.5) / ((Rand64)1 << (FIGS - 1)))

static lua_Number I2d (Rand64 x) {
  SRand64 sx = (SRand64)(trim64(x) >> shift64_FIG);
  lua_Number res = (lua_Number)(sx) * scaleFIG;
  if (sx < 0)
    res += l_mathop(1.0);  /* correct the two's complement if negative */
  lua_assert(0 <= res && res < 1);
  return res;
}

/* convert a 'Rand64' to a 'lua_Unsigned' */
#define I2UInt(x)	((lua_Unsigned)trim64(x))

/* convert a 'lua_Unsigned' to a 'Rand64' */
#define Int2I(x)	((Rand64)(x))


#else	/* no 'Rand64'   }{ */

/* get an integer with at least 32 bits */
#if LUAI_IS32INT
typedef unsigned int lu_int32;
#else
typedef unsigned long lu_int32;
#endif


/*
** Use two 32-bit integers to represent a 64-bit quantity.
*/
typedef struct Rand64 {
  lu_int32 h;  /* higher half */
  lu_int32 l;  /* lower half */
} Rand64;


/*
** If 'lu_int32' has more than 32 bits, the extra bits do not interfere
** with the 32 initial bits, except in a right shift and comparisons.
** Moreover, the final result has to discard the extra bits.
*/

/* avoid using extra bits when needed */
#define trim32(x)	((x) & 0xffffffffu)


/*
** basic operations on 'Rand64' values
*/

/* build a new Rand64 value */
static Rand64 packI (lu_int32 h, lu_int32 l) {
  Rand64 result;
  result.h = h;
  result.l = l;
  return result;
}

/* return i << n */
static Rand64 Ishl (Rand64 i, int n) {
  lua_assert(n > 0 && n < 32);
  return packI((i.h << n) | (trim32(i.l) >> (32 - n)), i.l << n);
}

/* i1 ^= i2 */
static void Ixor (Rand64 *i1, Rand64 i2) {
  i1->h ^= i2.h;
  i1->l ^= i2.l;
}

/* return i1 + i2 */
static Rand64 Iadd (Rand64 i1, Rand64 i2) {
  Rand64 result = packI(i1.h + i2.h, i1.l + i2.l);
  if (trim32(result.l) < trim32(i1.l))  /* carry? */
    result.h++;
  return result;
}

/* return i * 5 */
static Rand64 times5 (Rand64 i) {
  return Iadd(Ishl(i, 2), i);  /* i * 5 == (i << 2) + i */
}

/* return i * 9 */
static Rand64 times9 (Rand64 i) {
  return Iadd(Ishl(i, 3), i);  /* i * 9 == (i << 3) + i */
}

/* return 'i' rotated left 'n' bits */
static Rand64 rotl (Rand64 i, int n) {
  lua_assert(n > 0 && n < 32);
  return packI((i.h << n) | (trim32(i.l) >> (32 - n)),
               (trim32(i.h) >> (32 - n)) | (i.l << n));
}

/* for offsets larger than 32, rotate right by 64 - offset */
static Rand64 rotl1 (Rand64 i, int n) {
  lua_assert(n > 32 && n < 64);
  n = 64 - n;
  return packI((trim32(i.h) >> n) | (i.l << (32 - n)),
               (i.h << (32 - n)) | (trim32(i.l) >> n));
}

/*
** implementation of 'xoshiro256**' algorithm on 'Rand64' values
*/
static Rand64 nextrand (Rand64 *state) {
  Rand64 res = times9(rotl(times5(state[1]), 7));
  Rand64 t = Ishl(state[1], 17);
  Ixor(&state[2], state[0]);
  Ixor(&state[3], state[1]);
  Ixor(&state[1], state[2]);
  Ixor(&state[0], state[3]);
  Ixor(&state[2], t);
  state[3] = rotl1(state[3], 45);
  return res;
}


/*
** Converts a 'Rand64' into a float.
*/

/* an unsigned 1 with proper type */
#define UONE		((lu_int32)1)


#if FIGS <= 32

/* 2^(-FIGS) */
#define scaleFIG       (l_mathop(0.5) / (UONE << (FIGS - 1)))

/*
** get up to 32 bits from higher half, shifting right to
** throw out the extra bits.
*/
static lua_Number I2d (Rand64 x) {
  lua_Number h = (lua_Number)(trim32(x.h) >> (32 - FIGS));
  return h * scaleFIG;
}

#else	/* 32 < FIGS <= 64 */

/* 2^(-FIGS) = 1.0 / 2^30 / 2^3 / 2^(FIGS-33) */
#define scaleFIG  \
    (l_mathop(1.0) / (UONE << 30) / l_mathop(8.0) / (UONE << (FIGS - 33)))

/*
** use FIGS - 32 bits from lower half, throwing out the other
** (32 - (FIGS - 32)) = (64 - FIGS) bits
*/
#define shiftLOW	(64 - FIGS)

/*
** higher 32 bits go after those (FIGS - 32) bits: shiftHI = 2^(FIGS - 32)
*/
#define shiftHI		((lua_Number)(UONE << (FIGS - 33)) * l_mathop(2.0))


static lua_Number I2d (Rand64 x) {
  lua_Number h = (lua_Number)trim32(x.h) * shiftHI;
  lua_Number l = (lua_Number)(trim32(x.l) >> shiftLOW);
  return (h + l) * scaleFIG;
}

#endif


/* convert a 'Rand64' to a 'lua_Unsigned' */
static lua_Unsigned I2UInt (Rand64 x) {
  return (((lua_Unsigned)trim32(x.h) << 31) << 1) | (lua_Unsigned)trim32(x.l);
}

/* convert a 'lua_Unsigned' to a 'Rand64' */
static Rand64 Int2I (lua_Unsigned n) {
  return packI((lu_int32)((n >> 31) >> 1), (lu_int32)n);
}

#endif  /* } */


/*
** A state uses four 'Rand64' values.
*/
typedef struct {
  Rand64 s[4];
} RanState;


/*
** Project the random integer 'ran' into the interval [0, n].
** Because 'ran' has 2^B possible values, the projection can only be
** uniform when the size of the interval is a power of 2 (exact
** division). So, to get a uniform projection into [0, n], we
** first compute 'lim', the smallest Mersenne number not smaller than
** 'n'. We then project 'ran' into the interval [0, lim].  If the result
** is inside [0, n], we are done. Otherwise, we try with another 'ran',
** until we have a result inside the interval.
*/
static lua_Unsigned project (lua_Unsigned ran, lua_Unsigned n,
                             RanState *state) {
  lua_Unsigned lim = n;  /* to compute the Mersenne number */
  int sh;  /* how much to spread bits to the right in 'lim' */
  /* spread '1' bits in 'lim' until it becomes a Mersenne number */
  for (sh = 1; (lim & (lim + 1)) != 0; sh *= 2)
    lim |= (lim >> sh);  /* spread '1's to the right */
  while ((ran &= lim) > n)  /* project 'ran' into [0..lim] and test */
    ran = I2UInt(nextrand(state->s));  /* not inside [0..n]? try again */
  return ran;
}


static int math_random (lua_State *L) {
  RanState *state = (RanState *)lua_touserdata(L, lua_upvalueindex(1));
  Rand64 rv = nextrand(state->s);
  switch (lua_gettop(L)) {
    case 0: {
      lua_pushnumber(L, I2d(rv));
      return 1;
    }
    case 1: {
      if (lua_isinteger(L, 1)) {
        lua_Integer up = lua_tointeger(L, 1);
        if (up == 0) {
          lua_pushinteger(L, l_castU2S(I2UInt(rv)));
          return 1;
        }
        luaL_argcheck(L, up >= 1, 1, "interval is empty");
        lua_Unsigned p = project(I2UInt(rv), (lua_Unsigned)up - 1, state);
        lua_pushinteger(L, (lua_Integer)(p + 1));
      } else {
        lua_Number up = luaL_checknumber(L, 1);
        luaL_argcheck(L, up >= 1.0, 1, "interval is empty");
        lua_Number r = I2d(rv);
        lua_pushnumber(L, r * up);
      }
      return 1;
    }
    case 2: {
      if (lua_isinteger(L, 1) && lua_isinteger(L, 2)) {
        lua_Integer low = lua_tointeger(L, 1);
        lua_Integer up = lua_tointeger(L, 2);
        luaL_argcheck(L, low <= up, 1, "interval is empty");
        lua_Unsigned p = project(I2UInt(rv), (lua_Unsigned)up - (lua_Unsigned)low, state);
        lua_pushinteger(L, (lua_Integer)(p + (lua_Unsigned)low));
      } else {
        lua_Number low = luaL_checknumber(L, 1);
        lua_Number up = luaL_checknumber(L, 2);
        luaL_argcheck(L, low <= up, 1, "interval is empty");
        lua_Number r = I2d(rv);
        lua_pushnumber(L, low + r * (up - low));
      }
      return 1;
    }
    default: return luaL_error(L, "wrong number of arguments");
  }
}


static void setseed (lua_State *L, Rand64 *state,
                     lua_Unsigned n1, lua_Unsigned n2) {
  int i;
  state[0] = Int2I(n1);
  state[1] = Int2I(0xff);  /* avoid a zero state */
  state[2] = Int2I(n2);
  state[3] = Int2I(0);
  for (i = 0; i < 16; i++)
    nextrand(state);  /* discard initial values to "spread" seed */
  lua_pushinteger(L, l_castU2S(n1));
  lua_pushinteger(L, l_castU2S(n2));
}


/*
** Set a "random" seed. To get some randomness, use the current time
** and the address of 'L' (in case the machine does address space layout
** randomization).
*/


static void randseed (lua_State *L, RanState *state) {
  lua_Unsigned seed1 = (lua_Unsigned)time(NULL);
  lua_Unsigned seed2 = (lua_Unsigned)(size_t)L;
  setseed(L, state->s, seed1, seed2);
}


static int math_randomseed (lua_State *L) {
  RanState *state = (RanState *)lua_touserdata(L, lua_upvalueindex(1));
  if (lua_isnone(L, 1)) {
    randseed(L, state);
  }
  else {
    lua_Integer n1 = luaL_checkinteger(L, 1);
    lua_Integer n2 = luaL_optinteger(L, 2, 0);
    setseed(L, state->s, n1, n2);
  }
  return 2;  /* return seeds */
}


static const luaL_Reg randfuncs[] = {
  {"random", math_random},
  {"randomseed", math_randomseed},
  {NULL, NULL}
};


/*
** Register the random functions and initialize their state.
*/
static void setrandfunc (lua_State *L) {
  RanState *state = (RanState *)lua_newuserdatauv(L, sizeof(RanState), 0);
  randseed(L, state);  /* initialize with a "random" seed */
  lua_pop(L, 2);  /* remove pushed seeds */
  luaL_setfuncs(L, randfuncs, 1);
}

/* }======================================================= */


/*
** {=======================================================
** Deprecated functions (for compatibility only)
** ========================================================
*/
#if defined(LUA_COMPAT_MATHLIB)

static int math_cosh (lua_State *L) {
  lua_pushnumber(L, l_mathop(cosh)(luaL_checknumber(L, 1)));
  return 1;
}

static int math_sinh (lua_State *L) {
  lua_pushnumber(L, l_mathop(sinh)(luaL_checknumber(L, 1)));
  return 1;
}

static int math_tanh (lua_State *L) {
  lua_pushnumber(L, l_mathop(tanh)(luaL_checknumber(L, 1)));
  return 1;
}

static int math_pow (lua_State *L) {
  lua_Number x = luaL_checknumber(L, 1);
  lua_Number y = luaL_checknumber(L, 2);
  lua_pushnumber(L, l_mathop(pow)(x, y));
  return 1;
}

static int math_frexp (lua_State *L) {
  int e;
  lua_pushnumber(L, l_mathop(frexp)(luaL_checknumber(L, 1), &e));
  lua_pushinteger(L, e);
  return 2;
}

static int math_ldexp (lua_State *L) {
  lua_Number x = luaL_checknumber(L, 1);
  int ep = (int)luaL_checkinteger(L, 2);
  lua_pushnumber(L, l_mathop(ldexp)(x, ep));
  return 1;
}

static int math_log10 (lua_State *L) {
  lua_pushnumber(L, l_mathop(log10)(luaL_checknumber(L, 1)));
  return 1;
}

#endif
/* }======================================================= */



static int math_array (lua_State *L) {
  int n = lua_gettop(L);
  if (n < 1) {
    lua_newtable(L);
    return 1;
  }
  
  /* Check if all dimensions are positive integers */
  for (int i = 1; i <= n; i++) {
    if (lua_isinteger(L, i)) {
      lua_Integer dim = lua_tointeger(L, i);
      if (dim <= 0) {
        luaL_argerror(L, i, "dimensions must be positive integers");
      }
    } else if (i < n) {
      luaL_argerror(L, i, "dimensions must be positive integers");
    }
  }
  
  /* The last argument is the initial value (optional) */
  int num_dims = (lua_isinteger(L, n)) ? n : n - 1;
  
  /* Create a stack to keep track of tables and their dimensions */
  typedef struct {
    lua_Integer index;
    lua_Integer size;
  } StackElem;
  
  StackElem *stack = luaM_newvector(L, num_dims, StackElem);
  
  /* Initialize stack */
  for (int i = 0; i < num_dims; i++) {
    stack[i].index = 1;
    stack[i].size = lua_tointeger(L, i + 1);
  }
  
  /* Create the root table */
  lua_newtable(L);
  
  int stack_pos = 0;
  while (stack_pos >= 0) {
    if (stack_pos == num_dims - 1) {
      /* We're at the innermost dimension, fill with value */
      if (lua_isinteger(L, n)) {
        /* No initial value provided, use nil */
        lua_pushnil(L);
      } else {
        /* Use the provided initial value */
        lua_pushvalue(L, n);
      }
      lua_rawseti(L, -2, stack[stack_pos].index);
      stack[stack_pos].index++;
      
      /* Check if we've filled this dimension */
      if (stack[stack_pos].index > stack[stack_pos].size) {
        /* Move up one level */
        if (stack_pos > 0) {
          lua_pop(L, 1);  /* pop the filled table, keep root */
        }
        stack_pos--;
        if (stack_pos >= 0) {
          stack[stack_pos].index++;
        }
      }
    } else {
      /* We need to create a new table for the next dimension */
      if (stack[stack_pos].index > stack[stack_pos].size) {
        /* This dimension is filled, move up */
        if (stack_pos > 0) {
          lua_pop(L, 1);  /* pop the filled table, keep root */
        }
        stack_pos--;
        if (stack_pos >= 0) {
          stack[stack_pos].index++;
        }
      } else {
        /* Create a new table for the next dimension */
        lua_newtable(L);
        /* Duplicate it so it stays on the stack after rawseti */
        lua_pushvalue(L, -1);
        /* Set it in the parent table */
        lua_rawseti(L, -3, stack[stack_pos].index);
        /* Move down to fill the new table */
        stack_pos++;
      }
    }
  }
  
  luaM_freearray(L, stack, num_dims);
  return 1;
}


static void generate_math_expr(luaL_Buffer *b, lua_Number target, int depth) {
  if (depth <= 0) {
    if (target == (lua_Integer)target) {
      char buf[64];
      snprintf(buf, sizeof(buf), "%lld", (long long)(lua_Integer)target);
      luaL_addstring(b, buf);
    } else {
      char buf[64];
      snprintf(buf, sizeof(buf), "%.17g", (double)target);
      luaL_addstring(b, buf);
    }
    return;
  }

  int op = rand() % 4; /* 0: +, 1: -, 2: *, 3: / */
  int left_side = rand() % 2;
  lua_Number random_val = (lua_Number)(1 + (rand() % 100));

  luaL_addchar(b, '(');

  if (left_side) {
    /* Target is on the left, random val on the right */
    lua_Number new_target;
    switch (op) {
      case 0: new_target = target - random_val; break;
      case 1: new_target = target + random_val; break;
      case 2: new_target = target / random_val; break;
      case 3: new_target = target * random_val; break;
    }
    generate_math_expr(b, new_target, depth - 1);

    switch (op) {
      case 0: luaL_addstring(b, " + "); break;
      case 1: luaL_addstring(b, " - "); break;
      case 2: luaL_addstring(b, " * "); break;
      case 3: luaL_addstring(b, " / "); break;
    }

    if (random_val == (lua_Integer)random_val) {
      char buf[64];
      snprintf(buf, sizeof(buf), "%lld", (long long)(lua_Integer)random_val);
      luaL_addstring(b, buf);
    } else {
      char buf[64];
      snprintf(buf, sizeof(buf), "%.17g", (double)random_val);
      luaL_addstring(b, buf);
    }
  } else {
    /* Random val on the left, target on the right */
    lua_Number new_target;
    if (op == 3 && target == 0) {
      op = rand() % 3; /* Avoid dividing by 0 target */
    }

    switch (op) {
      case 0: new_target = target - random_val; break;
      case 1: new_target = random_val - target; break; /* val - target = result => target = val - result */
      case 2: new_target = target / random_val; break;
      case 3: new_target = random_val / target; break; /* val / target = result => target = val / result */
    }

    if (random_val == (lua_Integer)random_val) {
      char buf[64];
      snprintf(buf, sizeof(buf), "%lld", (long long)(lua_Integer)random_val);
      luaL_addstring(b, buf);
    } else {
      char buf[64];
      snprintf(buf, sizeof(buf), "%.17g", (double)random_val);
      luaL_addstring(b, buf);
    }

    switch (op) {
      case 0: luaL_addstring(b, " + "); break;
      case 1: luaL_addstring(b, " - "); break;
      case 2: luaL_addstring(b, " * "); break;
      case 3: luaL_addstring(b, " / "); break;
    }

    generate_math_expr(b, new_target, depth - 1);
  }

  luaL_addchar(b, ')');
}

static int math_toexpr (lua_State *L) {
  lua_Number target = luaL_checknumber(L, 1);
  int depth = (int)luaL_checkinteger(L, 2);

  if (depth < 0) {
    depth = 0;
  } else if (depth > 15) {
    depth = 15;
  }

  luaL_Buffer b;
  luaL_buffinit(L, &b);
  generate_math_expr(&b, target, depth);
  luaL_pushresult(&b);
  return 1;
}

/*
** {=======================================================
** Noise function (Simplex-like gradient noise)
** Supports 1D, 2D, 3D inputs. Returns value in [-1, 1].
** ========================================================
*/

static const unsigned char noise_perm[512] = {
  151,160,137,91,90,15,131,13,201,95,96,53,194,233,7,225,
  140,36,103,30,69,142,8,99,37,240,21,10,23,190,6,148,
  247,120,234,75,0,26,197,62,94,252,219,203,117,35,11,32,
  57,177,33,88,237,149,56,87,174,20,125,136,171,168,68,175,
  74,165,71,134,139,48,27,166,77,146,158,231,83,111,229,122,
  60,211,133,230,220,105,92,41,55,46,245,40,244,102,143,54,
  65,25,63,161,1,216,80,73,209,76,132,187,208,89,18,169,
  200,196,135,130,116,188,159,86,164,100,109,198,173,186,3,64,
  52,217,226,250,124,123,5,202,38,147,118,126,255,82,85,212,
  207,206,59,227,47,16,58,17,182,189,28,42,223,183,170,213,
  119,248,152,2,44,154,163,70,221,153,101,155,167,43,172,9,
  129,22,39,253,19,98,108,110,79,113,224,232,178,185,112,104,
  218,246,97,228,251,34,242,193,238,210,144,12,191,179,162,241,
  81,51,145,235,249,14,239,107,49,192,214,31,181,199,106,157,
  184,84,204,176,115,121,50,45,127,4,150,254,138,236,205,93,
  222,114,67,29,24,72,243,141,128,195,78,66,215,61,156,180,
  151,160,137,91,90,15,131,13,201,95,96,53,194,233,7,225,
  140,36,103,30,69,142,8,99,37,240,21,10,23,190,6,148,
  247,120,234,75,0,26,197,62,94,252,219,203,117,35,11,32,
  57,177,33,88,237,149,56,87,174,20,125,136,171,168,68,175,
  74,165,71,134,139,48,27,166,77,146,158,231,83,111,229,122,
  60,211,133,230,220,105,92,41,55,46,245,40,244,102,143,54,
  65,25,63,161,1,216,80,73,209,76,132,187,208,89,18,169,
  200,196,135,130,116,188,159,86,164,100,109,198,173,186,3,64,
  52,217,226,250,124,123,5,202,38,147,118,126,255,82,85,212,
  207,206,59,227,47,16,58,17,182,189,28,42,223,183,170,213,
  119,248,152,2,44,154,163,70,221,153,101,155,167,43,172,9,
  129,22,39,253,19,98,108,110,79,113,224,232,178,185,112,104,
  218,246,97,228,251,34,242,193,238,210,144,12,191,179,162,241,
  81,51,145,235,249,14,239,107,49,192,214,31,181,199,106,157,
  184,84,204,176,115,121,50,45,127,4,150,254,138,236,205,93,
  222,114,67,29,24,72,243,141,128,195,78,66,215,61,156,180
};

/*
** 平滑曲线：6t^5 - 15t^4 + 10t^3
*/
static lua_Number noise_fade(lua_Number t) {
  return t * t * t * (t * (t * 6 - 15) + 10);
}

/*
** 线性插值
*/
static lua_Number noise_lerp(lua_Number t, lua_Number a, lua_Number b) {
  return a + t * (b - a);
}

/*
** 计算梯度贡献值
** hash 的高4位决定梯度向量，返回值是梯度与距离向量的点积
*/
static lua_Number noise_grad(int hash, lua_Number x, lua_Number y) {
  int h = hash & 3;
  lua_Number u = h < 2 ? x : y;
  lua_Number v = h < 1 || h == 2 ? y : x;
  return ((h & 1) == 0 ? u : -u) + ((h & 2) == 0 ? v : -v);
}

static lua_Number noise_grad3d(int hash, lua_Number x, lua_Number y, lua_Number z) {
  int h = hash & 15;
  lua_Number u = h < 8 ? x : y;
  lua_Number v = h < 4 ? y : (h == 12 || h == 14 ? x : z);
  return ((h & 1) == 0 ? u : -u) + ((h & 2) == 0 ? v : -v);
}

/*
** 2D Perlin-like噪声
*/
static lua_Number noise_2d(lua_Number x, lua_Number y) {
  int xi = (int)l_mathop(floor)(x) & 255;
  int yi = (int)l_mathop(floor)(y) & 255;

  lua_Number xf = x - l_mathop(floor)(x);
  lua_Number yf = y - l_mathop(floor)(y);

  lua_Number u = noise_fade(xf);
  lua_Number v = noise_fade(yf);

  const unsigned char *p = noise_perm;

  int aa = p[p[xi] + yi];
  int ab = p[p[xi] + yi + 1];
  int ba = p[p[xi + 1] + yi];
  int bb = p[p[xi + 1] + yi + 1];

  lua_Number x1 = noise_lerp(u,
    noise_grad(aa, xf, yf),
    noise_grad(ba, xf - 1, yf));
  lua_Number x2 = noise_lerp(u,
    noise_grad(ab, xf, yf - 1),
    noise_grad(bb, xf - 1, yf - 1));

  return noise_lerp(v, x1, x2);
}

/*
** 3D Perlin-like噪声
*/
static lua_Number noise_3d(lua_Number x, lua_Number y, lua_Number z) {
  int xi = (int)l_mathop(floor)(x) & 255;
  int yi = (int)l_mathop(floor)(y) & 255;
  int zi = (int)l_mathop(floor)(z) & 255;

  lua_Number xf = x - l_mathop(floor)(x);
  lua_Number yf = y - l_mathop(floor)(y);
  lua_Number zf = z - l_mathop(floor)(z);

  lua_Number u = noise_fade(xf);
  lua_Number v = noise_fade(yf);
  lua_Number w = noise_fade(zf);

  const unsigned char *p = noise_perm;

  int aaa = p[p[p[xi] + yi] + zi];
  int aba = p[p[p[xi] + yi + 1] + zi];
  int aab = p[p[p[xi] + yi] + zi + 1];
  int abb = p[p[p[xi] + yi + 1] + zi + 1];
  int baa = p[p[p[xi + 1] + yi] + zi];
  int bba = p[p[p[xi + 1] + yi + 1] + zi];
  int bab = p[p[p[xi + 1] + yi] + zi + 1];
  int bbb = p[p[p[xi + 1] + yi + 1] + zi + 1];

  lua_Number x1 = noise_lerp(u,
    noise_grad3d(aaa, xf, yf, zf),
    noise_grad3d(baa, xf - 1, yf, zf));
  lua_Number x2 = noise_lerp(u,
    noise_grad3d(aba, xf, yf - 1, zf),
    noise_grad3d(bba, xf - 1, yf - 1, zf));
  lua_Number y1 = noise_lerp(v, x1, x2);

  lua_Number x3 = noise_lerp(u,
    noise_grad3d(aab, xf, yf, zf - 1),
    noise_grad3d(bab, xf - 1, yf, zf - 1));
  lua_Number x4 = noise_lerp(u,
    noise_grad3d(abb, xf, yf - 1, zf - 1),
    noise_grad3d(bbb, xf - 1, yf - 1, zf - 1));
  lua_Number y2 = noise_lerp(v, x3, x4);

  return noise_lerp(w, y1, y2);
}

/*
** math.noise(x [, y [, z]])
** 生成Perlin噪声值，返回值范围约为[-1, 1]
** - 1个参数: 1D噪声（实际采样2D噪声在(0, x)）
** - 2个参数: 2D噪声
** - 3个参数: 3D噪声
*/
static int math_noise(lua_State *L) {
  int n = lua_gettop(L);
  lua_Number result;
  switch (n) {
    case 1: {
      lua_Number x = luaL_checknumber(L, 1);
      result = noise_2d(l_mathop(0.0), x);
      break;
    }
    case 2: {
      lua_Number x = luaL_checknumber(L, 1);
      lua_Number y = luaL_checknumber(L, 2);
      result = noise_2d(x, y);
      break;
    }
    case 3: {
      lua_Number x = luaL_checknumber(L, 1);
      lua_Number y = luaL_checknumber(L, 2);
      lua_Number z = luaL_checknumber(L, 3);
      result = noise_3d(x, y, z);
      break;
    }
    default:
      return luaL_error(L, "wrong number of arguments");
  }
  lua_pushnumber(L, result);
  return 1;
}

/* }======================================================= */


/*
** {=======================================================
** 扩展工具函数
** ========================================================
*/

/**
 * 将值限制在指定范围内
 * math.clamp(x, min, max) -> 返回限制在[min, max]范围内的x
 */
static int math_clamp(lua_State *L) {
  lua_Number x = luaL_checknumber(L, 1);
  lua_Number lo = luaL_checknumber(L, 2);
  lua_Number hi = luaL_checknumber(L, 3);
  if (lo > hi) { lua_Number tmp = lo; lo = hi; hi = tmp; }
  if (x < lo) x = lo;
  else if (x > hi) x = hi;
  lua_pushnumber(L, x);
  return 1;
}

/**
 * 线性插值
 * math.lerp(a, b, t) -> a + (b - a) * t
 */
static int math_lerp(lua_State *L) {
  lua_Number a = luaL_checknumber(L, 1);
  lua_Number b = luaL_checknumber(L, 2);
  lua_Number t = luaL_checknumber(L, 3);
  lua_pushnumber(L, a + (b - a) * t);
  return 1;
}

/**
 * 四舍五入取整
 * math.round(x) -> 返回x四舍五入后的整数
 */
static int math_round(lua_State *L) {
  lua_Number x = luaL_checknumber(L, 1);
  lua_Number r = l_mathop(floor)(x + l_mathop(0.5));
  if (lua_numbertointeger(r, &(lua_Integer){0})) {
    lua_Integer n;
    lua_numbertointeger(r, &n);
    lua_pushinteger(L, n);
  } else {
    lua_pushnumber(L, r);
  }
  return 1;
}

/**
 * 返回数值的符号
 * math.sign(x) -> 返回-1, 0, 或1
 */
static int math_sign(lua_State *L) {
  lua_Number x = luaL_checknumber(L, 1);
  if (x > 0) lua_pushinteger(L, 1);
  else if (x < 0) lua_pushinteger(L, -1);
  else lua_pushinteger(L, 0);
  return 1;
}

/**
 * 将一个值从一个范围映射到另一个范围
 * math.maprange(x, in_min, in_max, out_min, out_max)
 * 将x从[in_min, in_max]映射到[out_min, out_max]
 */
static int math_maprange(lua_State *L) {
  lua_Number x = luaL_checknumber(L, 1);
  lua_Number in_min = luaL_checknumber(L, 2);
  lua_Number in_max = luaL_checknumber(L, 3);
  lua_Number out_min = luaL_checknumber(L, 4);
  lua_Number out_max = luaL_checknumber(L, 5);
  if (in_max == in_min) {
    lua_pushnumber(L, out_min);
  } else {
    lua_pushnumber(L, out_min + (x - in_min) * (out_max - out_min) / (in_max - in_min));
  }
  return 1;
}

/**
 * 判断是否为2的幂
 * math.ispow2(x) -> 如果x是2的幂返回true
 */
static int math_ispow2(lua_State *L) {
  lua_Integer x = luaL_checkinteger(L, 1);
  if (x <= 0) {
    lua_pushboolean(L, 0);
  } else {
    lua_pushboolean(L, (x & (x - 1)) == 0);
  }
  return 1;
}

/**
 * 返回大于等于x的最小2的幂
 * math.nextpow2(x) -> 返回>=x的最小2的幂
 */
static int math_nextpow2(lua_State *L) {
  lua_Integer x = luaL_checkinteger(L, 1);
  if (x <= 0) {
    lua_pushinteger(L, 1);
    return 1;
  }
  lua_Unsigned u = (lua_Unsigned)x - 1;
  u |= u >> 1;
  u |= u >> 2;
  u |= u >> 4;
  u |= u >> 8;
  u |= u >> 16;
  u |= u >> 32;
  lua_pushinteger(L, (lua_Integer)(u + 1));
  return 1;
}

/* }======================================================= */

static const luaL_Reg mathlib[] = {
  {"abs",   math_abs},
  {"acos",  math_acos},
  {"asin",  math_asin},
  {"atan",  math_atan},
  {"ceil",  math_ceil},
  {"cos",   math_cos},
  {"deg",   math_deg},
  {"exp",   math_exp},
  {"tointeger", math_toint},
  {"floor", math_floor},
  {"fmod",   math_fmod},
  {"ult",   math_ult},
  {"log",   math_log},
  {"max",   math_max},
  {"min",   math_min},
  {"modf",   math_modf},
  {"noise", math_noise},
  {"rad",   math_rad},
  {"sin",   math_sin},
  {"sqrt",  math_sqrt},
  {"tan",   math_tan},
  {"type", math_type},
  {"array", math_array},
  {"toexpr", math_toexpr},
  {"clamp", math_clamp},
  {"lerp", math_lerp},
  {"round", math_round},
  {"sign", math_sign},
  {"maprange", math_maprange},
  {"ispow2", math_ispow2},
  {"nextpow2", math_nextpow2},
#if defined(LUA_COMPAT_MATHLIB)
  {"atan2", math_atan},
  {"cosh",   math_cosh},
  {"sinh",   math_sinh},
  {"tanh",   math_tanh},
  {"pow",   math_pow},
  {"frexp", math_frexp},
  {"ldexp", math_ldexp},
  {"log10", math_log10},
#endif
  /* placeholders */
  {"random", NULL},
  {"randomseed", NULL},
  {"pi", NULL},
  {"huge", NULL},
  {"maxinteger", NULL},
  {"mininteger", NULL},
  {NULL, NULL}
};


/*
** Open math library
*/
LUAMOD_API int luaopen_math (lua_State *L) {
  luaL_newlib(L, mathlib);
  lua_pushnumber(L, PI);
  lua_setfield(L, -2, "pi");
  lua_pushnumber(L, (lua_Number)HUGE_VAL);
  lua_setfield(L, -2, "huge");
  lua_pushinteger(L, LUA_MAXINTEGER);
  lua_setfield(L, -2, "maxinteger");
  lua_pushinteger(L, LUA_MININTEGER);
  lua_setfield(L, -2, "mininteger");
  setrandfunc(L);
  return 1;
}

