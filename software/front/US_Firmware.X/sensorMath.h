/* 
 * File:   sensorMath.h
 * Author: Sparky
 *
 * Created on ??????, 2015, ??? 17, 19:35
 */

#ifndef SENSORMATH_H
#define SENSORMATH_H

#include <GenericTypeDefs.h>
#include "typedefs.h"
#include "iron.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Target-specific view of a binary32 coefficient's sign/exponent/mantissa.
 * @note Assumes XC32's packed bitfield allocation, 32-bit unsigned int, and
 *       binary32 float representation. Verify these before changing toolchains;
 *       this union layout is not guaranteed by ISO C.
 */
typedef union {
    struct __PACKED {
        unsigned int m : 23;
        unsigned int e : 8;
        unsigned int s : 1;
    };
    float f;
} SFLOAT;

/**
 * @brief Nonnegative extended-precision magnitude used by the sensor polynomial.
 * @note A nonzero mantissa is normalized toward bit 31; e uses the coefficient
 *       conversion's biased exponent convention. Signs are accumulated separately.
 */
typedef struct {
    UINT32 m;
    INT32 e;
} ExtFloat;

/**
 * @brief Extract a binary32 coefficient's magnitude into a writable ExtFloat.
 * @note Sign is handled separately by the caller. Macro arguments must be simple
 *       variables/expressions without side effects; this uses target C extensions.
 */
#define float2ExtFloat(ef, f)                               \
    {                                                       \
        ef.m = (((UINT32)((SFLOAT)f).m) & 0x7FFFFFUL) << 8; \
        if ((ef.e = ((SFLOAT)f).e))                         \
            ef.m |= 0x80000000;                             \
    }

/**
 * @brief Divide an ExtFloat magnitude in place by a nonzero unsigned divisor.
 * @pre ui != 0; ef is a writable ExtFloat lvalue. No argument may have side effects.
 * @note Integer division truncates the mantissa; a zero result resets the exponent.
 */
#define ExtFloatDivByUInt(ef, ui)         \
    {                                     \
        if (ef.m /= ui) {                 \
            while (ef.m < 0x80000000UL) { \
                ef.m <<= 1;               \
                ef.e--;                   \
            }                             \
        } else {                          \
            ef.e = 0;                     \
        }                                 \
    }

/**
 * @brief Multiply magnitude ef1 by ef2 in place, using a 64-bit intermediate.
 * @pre Writable ef1, normalized operands, and arguments without side effects.
 * @note Retains the high 32 product bits and normalizes; no IEEE exception handling.
 */
#define ExtFloatMul(ef1, ef2)                                  \
    {                                                          \
        if ((ef1.m = ((UINT64)ef1.m * (UINT64)ef2.m) >> 32)) { \
            ef1.e += ef2.e - 126;                              \
            while (ef1.m < 0x80000000UL) {                     \
                ef1.m <<= 1;                                   \
                ef1.e--;                                       \
            }                                                  \
        } else {                                               \
            ef1.e = 0;                                         \
        }                                                      \
    }

/**
 * @brief Add nonnegative magnitude ef2 to ef1 in place after exponent alignment.
 * @pre Writable ef1, normalized operands, and arguments without side effects.
 * @note Contributions separated by at least 31 exponent steps are discarded;
 *       signed subtraction is performed separately by GetSensorTemperature().
 */
#define ExtFloatAdd(ef1, ef2)                                                  \
    {                                                                          \
        if (ef1.e >= ef2.e) {                                                  \
            if ((ef1.e - ef2.e) < 31) {                                        \
                if ((ef1.m = (ef1.m >> 1) + (ef2.m >> (ef1.e - ef2.e + 1)))) { \
                    if (ef1.m >= 0x80000000UL) {                               \
                        ef1.e++;                                               \
                    } else {                                                   \
                        ef1.m <<= 1;                                           \
                    }                                                          \
                } else {                                                       \
                    ef1.e = 0;                                                 \
                }                                                              \
            }                                                                  \
        } else {                                                               \
            if ((ef2.e - ef1.e) < 31) {                                        \
                if ((ef1.m = (ef2.m >> 1) + (ef1.m >> (ef2.e - ef1.e + 1)))) { \
                    ef1.e = ef2.e;                                             \
                    if (ef1.m >= 0x80000000UL) {                               \
                        ef1.e++;                                               \
                    } else {                                                   \
                        ef1.m <<= 1;                                           \
                    }                                                          \
                } else {                                                       \
                    ef1.e = 0;                                                 \
                }                                                              \
            } else {                                                           \
                ef1 = ef2;                                                     \
            }                                                                  \
        }                                                                      \
    }

#ifndef _SENSORMATH_C
#define SENSORMATH_H_EXTERN extern
#else
#define SENSORMATH_H_EXTERN
#endif

INT32 GetSensorTemperature(int input, t_SensorConfig *SC);

#undef SENSORMATH_H_EXTERN

#ifdef __cplusplus
}
#endif

#endif /* SENSORMATH_H */
