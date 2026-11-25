/* fuji_ring.h -- single-producer single-consumer rings between a card's bus side
 * (core1, or an emulator's I/O handler) and its link side (core0, or the
 * emulator's poll). The __atomic builtins keep them valid C and C++, so the MAME
 * grafts compile them too. */
#ifndef FUJI_RING_H
#define FUJI_RING_H

#include <stdbool.h>
#include <stdint.h>

#define FUJI_RING(name, type)                                                       \
    typedef struct {                                                                \
        uint32_t head; /* producer */                                               \
        uint32_t tail; /* consumer */                                               \
        uint32_t mask;                                                              \
        type *buf;                                                                  \
    } name##_t;                                                                     \
    static inline void name##_init(name##_t *ring, type *buf, uint32_t size)        \
    {                                                                               \
        ring->buf = buf;                                                            \
        ring->mask = size - 1;                                                      \
        __atomic_store_n(&ring->head, 0, __ATOMIC_RELAXED);                         \
        __atomic_store_n(&ring->tail, 0, __ATOMIC_RELAXED);                         \
    }                                                                               \
    static inline uint32_t name##_count(name##_t *ring)                             \
    {                                                                               \
        return __atomic_load_n(&ring->head, __ATOMIC_ACQUIRE) -                     \
               __atomic_load_n(&ring->tail, __ATOMIC_ACQUIRE);                      \
    }                                                                               \
    static inline uint32_t name##_space(name##_t *ring)                             \
    {                                                                               \
        return ring->mask + 1 - name##_count(ring);                                 \
    }                                                                               \
    static inline bool name##_put(name##_t *ring, type val)                         \
    {                                                                               \
        uint32_t head = __atomic_load_n(&ring->head, __ATOMIC_RELAXED);             \
        if (head - __atomic_load_n(&ring->tail, __ATOMIC_ACQUIRE) > ring->mask)     \
            return false;                                                           \
        ring->buf[head & ring->mask] = val;                                         \
        __atomic_store_n(&ring->head, head + 1, __ATOMIC_RELEASE);                  \
        return true;                                                                \
    }                                                                               \
    static inline bool name##_get(name##_t *ring, type *val)                        \
    {                                                                               \
        uint32_t tail = __atomic_load_n(&ring->tail, __ATOMIC_RELAXED);             \
        if (tail == __atomic_load_n(&ring->head, __ATOMIC_ACQUIRE))                 \
            return false;                                                           \
        *val = ring->buf[tail & ring->mask];                                        \
        __atomic_store_n(&ring->tail, tail + 1, __ATOMIC_RELEASE);                  \
        return true;                                                                \
    }                                                                               \
    /* consumer only */                                                             \
    static inline void name##_drain(name##_t *ring)                                 \
    {                                                                               \
        __atomic_store_n(&ring->tail, __atomic_load_n(&ring->head, __ATOMIC_ACQUIRE), \
                         __ATOMIC_RELEASE);                                         \
    }

FUJI_RING(ring8, uint8_t)
FUJI_RING(ring16, uint16_t)

#endif /* FUJI_RING_H */
