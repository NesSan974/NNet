#ifndef __NES_DS_H__
#define __NES_DS_H__

/*
    -- dynamique array

    struct da {
        struct item *items;

        size_t capacity;
        size_t count;
    }
 */

#define nesds_daappend(da, item)                                                                                  \
    do {                                                                                                          \
        if ((da).count >= (da).capacity) {                                                                        \
            if ((da).capacity == 0) {                                                                             \
                (da).capacity = 256;                                                                              \
            } else {                                                                                              \
                (da).capacity *= 2;                                                                               \
            }                                                                                                     \
            (da).items = realloc((da).items, (da).capacity * sizeof(item));                                       \
        }                                                                                                         \
        (da).items[(da).count++] = (item);                                                                        \
    } while (0)

#define nesds_daremove(da, i)                                                                                     \
    do {                                                                                                          \
        size_t j = (i);                                                                                           \
        if (j < (da).count)                                                                                       \
            (da).items[j] = (da).items[--(da).count];                                                             \
        else if (j == (da).count)                                                                                 \
            (da).count -= 1;                                                                                      \
    } while (0)

/*
    -- Circular Buffer

    struct cb {
      struct item *items;

      size_t head;
      size_t tail;
      size_t count;
      size_t capacity;
    };

 */

#define nesds_cbinit(cb, fixed_size)                                                                              \
    do {                                                                                                          \
        cb.count = 0;                                                                                             \
        cb.capacity = fixed_size;                                                                                 \
        cb.items = malloc(fixed_size * sizeof(*cb.items));                                                        \
    } while (0)

#define nesds_cbenqueue(cb, item)                                                                                 \
    do {                                                                                                          \
        if ((cb).count < (cb).capacity) {                                                                         \
            (cb).items[(cb).tail] = (item);                                                                       \
            (cb).tail = ((cb).tail + 1) % (cb).capacity;                                                          \
            (cb).count++;                                                                                         \
        }                                                                                                         \
    } while (0)

#define nesds_cbdequeue(cb, out)                                                                                  \
    do {                                                                                                          \
        if ((cb).count > 0) {                                                                                     \
            (out) = (cb).items[cb.head];                                                                          \
            (cb).head = ((cb).head + 1) % (cb).capacity;                                                          \
            (cb).count--;                                                                                         \
        }                                                                                                         \
    } while (0)

#define nesds_cbpeek(cb, out)                                                                                     \
    do {                                                                                                          \
        if ((cb).count > 0) {                                                                                     \
            (out) = &(cb).items[(cb).head];                                                                       \
        }                                                                                                         \
    } while (0)

#ifndef NESDS_NO_SHORT_NAMES

#define cbinit nesds_cbinit
#define cbenqueue nesds_cbenqueue
#define cbdequeue nesds_cbdequeue
#define cbpeek nesds_cbpeek

#define daappend nesds_daappend

#endif

#endif // __NES_DS_H__
