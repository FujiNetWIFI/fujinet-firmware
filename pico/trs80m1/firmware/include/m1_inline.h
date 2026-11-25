/* m1_inline.h -- core1 serves a bus cycle without a call (checksram --no-calls), and
 * compilers differ on what they inline, so everything on its path is forced inline. */
#ifndef M1_INLINE_H
#define M1_INLINE_H

#if defined(__GNUC__)
#define M1_INLINE static inline __attribute__((always_inline))
#else
#define M1_INLINE static inline
#endif

#endif /* M1_INLINE_H */
