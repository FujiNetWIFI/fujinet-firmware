/* check.h -- the host tests' assertion macro. */
#ifndef CHECK_H
#define CHECK_H

#include <stdio.h>
#include <stdlib.h>

static int check_fails;

#define CHECK(cond)                                                              \
    do {                                                                         \
        if (!(cond)) {                                                           \
            fprintf(stderr, "%s:%d: CHECK(%s) failed\n", __FILE__, __LINE__, #cond); \
            check_fails++;                                                       \
        }                                                                        \
    } while (0)

#define CHECK_EQ(a, b)                                                           \
    do {                                                                         \
        long _a = (long)(a), _b = (long)(b);                                     \
        if (_a != _b) {                                                          \
            fprintf(stderr, "%s:%d: %s == %ld, expected %s == %ld\n", __FILE__,  \
                    __LINE__, #a, _a, #b, _b);                                   \
            check_fails++;                                                       \
        }                                                                        \
    } while (0)

static inline int check_done(const char *name)
{
    if (check_fails) {
        fprintf(stderr, "%s: %d check(s) failed\n", name, check_fails);
        return 1;
    }
    printf("%s: PASS\n", name);
    return 0;
}

#endif
