#pragma once

#define MIN(a, b)       ((a) < (b) ? (a) : (b))
#define MAX(a, b)       ((a) > (b) ? (a) : (b))
#define ARRAY_SIZE(arr)  (sizeof(arr) / sizeof((arr)[0]))

#define UNUSED(x)       ((void)(x))
#define UNUSED_FN       __attribute__((unused))

#define CONTAINER_OF(ptr, type, member) \
    ((type*)((char*)(ptr) - offsetof(type, member)))

#define SWAP(a, b, tmp) do { tmp = a; a = b; b = tmp; } while (0)

#define BIT_SET(n, bit)    ((n) |= (1 << (bit)))
#define BIT_CLEAR(n, bit)  ((n) &= ~(1 << (bit)))
#define BIT_TEST(n, bit)   (!!((n) & (1 << (bit))))
