/* Host-test substitutes for Microchip integer aliases, never used by firmware builds. */
#ifndef TEST_GENERIC_TYPE_DEFS_H
#define TEST_GENERIC_TYPE_DEFS_H

#include <stdint.h>

typedef int8_t INT8;
typedef uint8_t UINT8;
typedef int16_t INT16;
typedef uint16_t UINT16;
typedef int32_t INT32;
typedef uint32_t UINT32;
typedef int64_t INT64;
typedef uint64_t UINT64;
typedef union {
    UINT16 Val;
    UINT8 v[2];
} UINT16_VAL;

#endif
