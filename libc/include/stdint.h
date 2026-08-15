#ifndef MYOS_STDINT_H
#define MYOS_STDINT_H 1

/*
 * stdint.h
 *
 * Freestanding implementation intended for GCC/i686-elf.
 *
 * Uses GCC predefined target type/limit macros so the definitions
 * follow the compiler's ABI instead of assuming the underlying
 * C types manually.
 */


/* ============================================================
 * Exact-width integer types
 * ============================================================ */

#ifdef __INT8_TYPE__
typedef __INT8_TYPE__ int8_t;
#endif

#ifdef __UINT8_TYPE__
typedef __UINT8_TYPE__ uint8_t;
#endif

#ifdef __INT16_TYPE__
typedef __INT16_TYPE__ int16_t;
#endif

#ifdef __UINT16_TYPE__
typedef __UINT16_TYPE__ uint16_t;
#endif

#ifdef __INT32_TYPE__
typedef __INT32_TYPE__ int32_t;
#endif

#ifdef __UINT32_TYPE__
typedef __UINT32_TYPE__ uint32_t;
#endif

#ifdef __INT64_TYPE__
typedef __INT64_TYPE__ int64_t;
#endif

#ifdef __UINT64_TYPE__
typedef __UINT64_TYPE__ uint64_t;
#endif


/* ============================================================
 * Minimum-width integer types
 * ============================================================ */

typedef __INT_LEAST8_TYPE__   int_least8_t;
typedef __UINT_LEAST8_TYPE__  uint_least8_t;

typedef __INT_LEAST16_TYPE__  int_least16_t;
typedef __UINT_LEAST16_TYPE__ uint_least16_t;

typedef __INT_LEAST32_TYPE__  int_least32_t;
typedef __UINT_LEAST32_TYPE__ uint_least32_t;

typedef __INT_LEAST64_TYPE__  int_least64_t;
typedef __UINT_LEAST64_TYPE__ uint_least64_t;


/* ============================================================
 * Fast integer types
 * ============================================================ */

typedef __INT_FAST8_TYPE__   int_fast8_t;
typedef __UINT_FAST8_TYPE__  uint_fast8_t;

typedef __INT_FAST16_TYPE__  int_fast16_t;
typedef __UINT_FAST16_TYPE__ uint_fast16_t;

typedef __INT_FAST32_TYPE__  int_fast32_t;
typedef __UINT_FAST32_TYPE__ uint_fast32_t;

typedef __INT_FAST64_TYPE__  int_fast64_t;
typedef __UINT_FAST64_TYPE__ uint_fast64_t;


/* ============================================================
 * Pointer-sized integer types
 * ============================================================ */

typedef __INTPTR_TYPE__  intptr_t;
typedef __UINTPTR_TYPE__ uintptr_t;


/* ============================================================
 * Greatest-width integer types
 * ============================================================ */

typedef __INTMAX_TYPE__  intmax_t;
typedef __UINTMAX_TYPE__ uintmax_t;


/* ============================================================
 * Exact-width limits
 * ============================================================ */

#ifdef __INT8_MAX__
#define INT8_MAX   __INT8_MAX__
#define INT8_MIN   (-INT8_MAX - 1)
#endif

#ifdef __UINT8_MAX__
#define UINT8_MAX  __UINT8_MAX__
#endif


#ifdef __INT16_MAX__
#define INT16_MAX  __INT16_MAX__
#define INT16_MIN  (-INT16_MAX - 1)
#endif

#ifdef __UINT16_MAX__
#define UINT16_MAX __UINT16_MAX__
#endif


#ifdef __INT32_MAX__
#define INT32_MAX  __INT32_MAX__
#define INT32_MIN  (-INT32_MAX - 1)
#endif

#ifdef __UINT32_MAX__
#define UINT32_MAX __UINT32_MAX__
#endif


#ifdef __INT64_MAX__
#define INT64_MAX  __INT64_MAX__
#define INT64_MIN  (-INT64_MAX - 1)
#endif

#ifdef __UINT64_MAX__
#define UINT64_MAX __UINT64_MAX__
#endif


/* ============================================================
 * Minimum-width limits
 * ============================================================ */

#define INT_LEAST8_MAX    __INT_LEAST8_MAX__
#define INT_LEAST8_MIN    (-INT_LEAST8_MAX - 1)
#define UINT_LEAST8_MAX   __UINT_LEAST8_MAX__

#define INT_LEAST16_MAX   __INT_LEAST16_MAX__
#define INT_LEAST16_MIN   (-INT_LEAST16_MAX - 1)
#define UINT_LEAST16_MAX  __UINT_LEAST16_MAX__

#define INT_LEAST32_MAX   __INT_LEAST32_MAX__
#define INT_LEAST32_MIN   (-INT_LEAST32_MAX - 1)
#define UINT_LEAST32_MAX  __UINT_LEAST32_MAX__

#define INT_LEAST64_MAX   __INT_LEAST64_MAX__
#define INT_LEAST64_MIN   (-INT_LEAST64_MAX - 1)
#define UINT_LEAST64_MAX  __UINT_LEAST64_MAX__


/* ============================================================
 * Fast integer limits
 * ============================================================ */

#define INT_FAST8_MAX     __INT_FAST8_MAX__
#define INT_FAST8_MIN     (-INT_FAST8_MAX - 1)
#define UINT_FAST8_MAX    __UINT_FAST8_MAX__

#define INT_FAST16_MAX    __INT_FAST16_MAX__
#define INT_FAST16_MIN    (-INT_FAST16_MAX - 1)
#define UINT_FAST16_MAX   __UINT_FAST16_MAX__

#define INT_FAST32_MAX    __INT_FAST32_MAX__
#define INT_FAST32_MIN    (-INT_FAST32_MAX - 1)
#define UINT_FAST32_MAX   __UINT_FAST32_MAX__

#define INT_FAST64_MAX    __INT_FAST64_MAX__
#define INT_FAST64_MIN    (-INT_FAST64_MAX - 1)
#define UINT_FAST64_MAX   __UINT_FAST64_MAX__


/* ============================================================
 * Pointer-sized limits
 * ============================================================ */

#define INTPTR_MAX        __INTPTR_MAX__
#define INTPTR_MIN        (-INTPTR_MAX - 1)
#define UINTPTR_MAX       __UINTPTR_MAX__

/*
 * Maximum value representable by size_t.
 * GCC provides this from the target ABI.
 */

#ifdef __SIZE_MAX__
#define SIZE_MAX          __SIZE_MAX__
#endif

/* ============================================================
 * Greatest-width limits
 * ============================================================ */

#define INTMAX_MAX        __INTMAX_MAX__
#define INTMAX_MIN        (-INTMAX_MAX - 1)
#define UINTMAX_MAX       __UINTMAX_MAX__


/* ============================================================
 * Integer constant-expression macros
 * ============================================================ */

#ifdef __INT8_C
#define INT8_C(value)     __INT8_C(value)
#endif

#ifdef __UINT8_C
#define UINT8_C(value)    __UINT8_C(value)
#endif

#ifdef __INT16_C
#define INT16_C(value)    __INT16_C(value)
#endif

#ifdef __UINT16_C
#define UINT16_C(value)   __UINT16_C(value)
#endif

#ifdef __INT32_C
#define INT32_C(value)    __INT32_C(value)
#endif

#ifdef __UINT32_C
#define UINT32_C(value)   __UINT32_C(value)
#endif

#ifdef __INT64_C
#define INT64_C(value)    __INT64_C(value)
#endif

#ifdef __UINT64_C
#define UINT64_C(value)   __UINT64_C(value)
#endif

#define INTMAX_C(value)   __INTMAX_C(value)
#define UINTMAX_C(value)  __UINTMAX_C(value)


#endif /* MYOS_STDINT_H */