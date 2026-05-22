#pragma once

#include <stdint.h>
#include <stddef.h>

/* Byte-order detection */
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
  #define DB_BIG_ENDIAN 1
#elif defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
  #define DB_LITTLE_ENDIAN 1
#else
  #include <arpa/inet.h>
  #if htonl(1) == 1
    #define DB_BIG_ENDIAN 1
  #else
    #define DB_LITTLE_ENDIAN 1
  #endif
#endif

/* Byte-swap utilities */
static inline uint16_t swap16(uint16_t v) {
    return (uint16_t)((v >> 8) | (v << 8));
}
static inline uint32_t swap32(uint32_t v) {
    return ((v >> 24) | ((v >> 8) & 0xFF00) |
            ((v << 8) & 0xFF0000) | (v << 24));
}
static inline uint64_t swap64(uint64_t v) {
    return ((v >> 56) | ((v >> 40) & 0xFF00) |
            ((v >> 24) & 0xFF0000) | ((v >> 8) & 0xFF000000) |
            ((v << 8) & 0xFF00000000ULL) | ((v << 24) & 0xFF0000000000ULL) |
            ((v << 40) & 0xFF000000000000ULL) | (v << 56));
}

/* Host to Little-Endian (our on-disk format is LE) */
#ifdef DB_LITTLE_ENDIAN
  #define htole16(v) (v)
  #define htole32(v) (v)
  #define htole64(v) (v)
  #define le16toh(v) (v)
  #define le32toh(v) (v)
  #define le64toh(v) (v)
#else
  #define htole16(v) swap16(v)
  #define htole32(v) swap32(v)
  #define htole64(v) swap64(v)
  #define le16toh(v) swap16(v)
  #define le32toh(v) swap32(v)
  #define le64toh(v) swap64(v)
#endif

/* Compiler hints */
#if defined(__GNUC__) || defined(__clang__)
  #define LIKELY(x)   __builtin_expect(!!(x), 1)
  #define UNLIKELY(x) __builtin_expect(!!(x), 0)
  #define PACKED      __attribute__((packed))
  #define ALIGNED(n)  __attribute__((aligned(n)))
#else
  #define LIKELY(x)   (x)
  #define UNLIKELY(x) (x)
  #define PACKED
  #define ALIGNED(n)
#endif
