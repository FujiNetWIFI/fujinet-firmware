/* s100_ring.h -- single-producer single-consumer rings between the bus side
 * (core1, or the SIMH I/O handler) and the link side (core0, or the SIMH poll). */
#ifndef S100_RING_H
#define S100_RING_H

#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>

#define S100_RING(name, type)                                                       \
    typedef struct {                                                                \
        _Atomic uint32_t head; /* producer */                                       \
        _Atomic uint32_t tail; /* consumer */                                       \
        uint32_t mask;                                                              \
        type *buf;                                                                  \
    } name##_t;                                                                     \
    static inline void name##_init(name##_t *r, type *buf, uint32_t size)           \
    {                                                                               \
        r->buf = buf;                                                               \
        r->mask = size - 1;                                                         \
        atomic_store_explicit(&r->head, 0, memory_order_relaxed);                   \
        atomic_store_explicit(&r->tail, 0, memory_order_relaxed);                   \
    }                                                                               \
    static inline uint32_t name##_count(name##_t *r)                                \
    {                                                                               \
        return atomic_load_explicit(&r->head, memory_order_acquire) -               \
               atomic_load_explicit(&r->tail, memory_order_acquire);                \
    }                                                                               \
    static inline uint32_t name##_space(name##_t *r)                                \
    {                                                                               \
        return r->mask + 1 - name##_count(r);                                       \
    }                                                                               \
    static inline bool name##_put(name##_t *r, type v)                              \
    {                                                                               \
        uint32_t h = atomic_load_explicit(&r->head, memory_order_relaxed);          \
        if (h - atomic_load_explicit(&r->tail, memory_order_acquire) > r->mask)     \
            return false;                                                           \
        r->buf[h & r->mask] = v;                                                    \
        atomic_store_explicit(&r->head, h + 1, memory_order_release);               \
        return true;                                                                \
    }                                                                               \
    static inline bool name##_get(name##_t *r, type *v)                             \
    {                                                                               \
        uint32_t t = atomic_load_explicit(&r->tail, memory_order_relaxed);          \
        if (t == atomic_load_explicit(&r->head, memory_order_acquire))              \
            return false;                                                           \
        *v = r->buf[t & r->mask];                                                   \
        atomic_store_explicit(&r->tail, t + 1, memory_order_release);               \
        return true;                                                                \
    }                                                                               \
    /* consumer only */                                                             \
    static inline void name##_drain(name##_t *r)                                    \
    {                                                                               \
        atomic_store_explicit(&r->tail,                                             \
                              atomic_load_explicit(&r->head, memory_order_acquire), \
                              memory_order_release);                                \
    }

S100_RING(ring8, uint8_t)
S100_RING(ring16, uint16_t)

#endif /* S100_RING_H */
