/* Exercise the production PID implementation with controlled acquisition inputs.
 * Only hardware headers and sensor conversion are substituted. This is an
 * arithmetic regression test, not an ADC/USB/interrupt or thermal simulation. */
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Skip hardware-only headers when PID.c includes them. Use its real profile and
 * PID structures, and let PID.h define its normal globals in this translation unit. */
#define ISR_H
#define MAIN_H
#define _PID_C
#include "PID.h"

volatile t_IronPars IronPars;
static struct {
    unsigned int VRT;
    unsigned int VCJ;
    unsigned int VTEMP[4];
    int HeaterOn;
} ADCData;
static struct {
    int TipChange;
} mainFlags;
static INT32 sensorTemperature;
static int sensorInput;

/** @brief Record the ADC-domain argument and return a controlled half-degree reading. */
INT32 GetSensorTemperature(int input, t_SensorConfig *config) {
    (void)config;
    sensorInput = input;
    return sensorTemperature;
}

#define min(a, b) ((a) < (b) ? (a) : (b))
#include "PID.c"

#define CHECK(condition)                                                    \
    do {                                                                    \
        if (!(condition)) {                                                 \
            fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #condition); \
            exit(EXIT_FAILURE);                                             \
        }                                                                   \
    } while (0)

/** @brief Reset all shared test state to a healthy 250 C, two-channel-capable fixture. */
static void Reset(int dual) {
    memset((void *)PIDVars, 0, sizeof PIDVars);
    memset((void *)&IronPars, 0, sizeof IronPars);
    memset(&ADCData, 0, sizeof ADCData);
    memset(&mainFlags, 0, sizeof mainFlags);
    RTAvg = CRTemp = 0;
    CTTemp = 125;
    sensorTemperature = 500;
    ADCData.VTEMP[1] = 500;
    PIDInit();
    for (int ch = 0; ch < 2; ch++) {
        t_IronConfig *ic = (t_IronConfig *)&IronPars.Config[ch];
        t_PIDVars *pv = (t_PIDVars *)&PIDVars[ch];
        int avg = ADCAVG - !!dual;
        ic->SensorConfig.Type = ch && !dual ? SENSOR_UNDEFINED : SENSOR_TC;
        ic->SensorConfig.Gain = 128;
        ic->PID_KP = 2000;
        ic->PID_KI = 32;
        ic->PID_PMax = 120;
        pv->HR = 100;
        pv->HRAvg = 100 * (1 << avg);
        pv->HP = 100;
        pv->HPAvg = 100 * (1 << avg);
    }
}

/** @brief Verify signed division direction and minimum-integer handling. */
static void TestRounding(void) {
    CHECK(sizeof(INT32) == 4 && sizeof(int) == 4);
    CHECK(PIDDivPow2Trunc(INT32_MIN, 0) == INT32_MIN);
    CHECK(PIDDivPow2Trunc(INT32_MIN, 1) == -1073741824);
    CHECK(PIDDivPow2Trunc(INT32_MIN, 30) == -2);
    CHECK(PIDDivPow2Trunc(-3, 1) == -1);
    CHECK(PIDDivPow2Trunc(3, 1) == 1);
    CHECK(PIDDivPow2Floor64(-3, 1) == -2);
    CHECK(PIDDivPow2Floor64(3, 1) == 1);
    CHECK(PIDDivPow2Floor64(INT64_MIN, 0) == INT64_MIN);
    CHECK(PIDDivPow2Floor64(INT64_MIN, 62) == -2);
    CHECK(PIDDivPow2Floor64(INT64_MAX, 62) == 1);
    CHECK(PIDDivPow2Floor64(INT64_MIN, 63) == -1);
    CHECK(PIDDivPow2Floor64(INT64_MAX, 63) == 0);
}

/** @brief Reproduce the old call-41 overflow with valid PC tuning and exercise both channels. */
static void TestZeroKp(void) {
    for (int dual = 0; dual <= 1; dual++) {
        Reset(dual);
        CTTemp = 0;
        for (int ch = 0; ch <= dual; ch++) {
            IronPars.Config[ch].PID_KP = 0;
            IronPars.Config[ch].PID_KI = 3277;
        }
        for (int tick = 0; tick < 2000; tick++) {
            for (int ch = 0; ch <= dual; ch++) {
                PID(ch);
                CHECK(PIDVars[ch].PIDDutyI == 0);
                CHECK(PIDVars[ch].PIDDutyFull == 0);
                CHECK(PIDVars[ch].PIDDuty == 0);
            }
        }
    }
}

/** @brief Check signed waveform differences, Q16 ratio limits, and the zero-denominator hold. */
static void TestWaveform(void) {
    Reset(0);
    IronPars.Config[0].WSLen = -1;
    PIDVars[0].Starting = 0;
    PIDVars[0].OffCnt = 2;
    PIDVars[0].ADCTemp[0] = 400;
    ADCData.HeaterOn = 1;
    PID(0);
    CHECK(PIDVars[0].WSDelta[0].val == -200);
    CHECK(PIDVars[0].WSCorr == -100 * 1024);

    for (int sign = -1; sign <= 1; sign += 2) {
        for (int quarter = 0; quarter <= 1; quarter++) {
            Reset(0);
            IronPars.Config[0].WSLen = -1;
            PIDVars[0].Starting = 0;
            PIDVars[0].OffCnt = 1;
            PIDVars[0].WSDelta[0].val = sign * 200;
            PIDVars[0].LastOn = 500 - sign * (quarter ? 75 : 0);
            PID(0);
            CHECK(PIDVars[0].WSMul == (quarter ? 16384U : 32768U));
        }
    }
    Reset(0);
    IronPars.Config[0].WSLen = -1;
    PIDVars[0].Starting = 0;
    PIDVars[0].OffCnt = 1;
    PIDVars[0].WSMul = 1234;
    PID(0);
    CHECK(PIDVars[0].WSMul == 1234);
}

/** @brief Check compensation goldens on LP64 hosts and 32-bit MIPS, including negative rounding. */
static void TestCompensation(void) {
    static const struct {
        UINT16 gain;
        INT16 current;
        int expected;
    } cases[] = {{110, 10, 468}, {202, 10, 440}, {65535, 10, -19099}, {65535, -10, 20100}};
    for (unsigned int i = 0; i < sizeof cases / sizeof cases[0]; i++) {
        Reset(0);
        IronPars.Config[0].SensorConfig.Gain = cases[i].gain;
        IronPars.Config[0].HRCompCurrent = cases[i].current;
        PID(0);
        CHECK(sensorInput == cases[i].expected);
    }
}

/** @brief Exercise the full-width power product and keep missing-power behavior unchanged. */
static void TestPowerNormalization(void) {
    Reset(0);
    CTTemp = 225;
    sensorTemperature = 0;
    IronPars.Config[0].PID_KP = 65535;
    IronPars.Config[0].PID_KI = 0;
    IronPars.Config[0].PID_PMax = 65535;
    PIDVars[0].HP = 1;
    PIDVars[0].HPAvg = 1 << ADCAVG;
    PID(0);
    CHECK(PIDVars[0].PIDDutyFull == PID_DUTY_MAX);
    CHECK(PIDVars[0].PIDDuty == 0xFA0000U);
    PIDVars[0].HPAvg = 0;
    PID(0);
    CHECK(PIDVars[0].PIDDuty == 0xFA0000U);
}

/** @brief Preserve sparse open/short detection pulses and tip-change suppression. */
static void TestFaultOutputs(void) {
    for (int dual = 0; dual <= 1; dual++) {
        for (int ch = 0; ch <= dual; ch++) {
            Reset(dual);
            PIDVars[ch].NoHeater = 1;
            PID(ch);
            CHECK(PIDVars[ch].PIDDuty == (dual ? 0x51EB8U : 0x28F5CU));
            PIDVars[ch].ShortCircuit = 1;
            PID(ch);
            CHECK(PIDVars[ch].PIDDuty == (dual ? 0x20C48U : 0x10624U));
            mainFlags.TipChange = 1;
            PIDVars[ch].PWM = 12345;
            PID(ch);
            CHECK(PIDVars[ch].PIDDuty == 0 && PIDVars[ch].PWM == 0);
            mainFlags.TipChange = 0;
            PIDVars[ch].NoHeater = PIDVars[ch].ShortCircuit = 0;
            PIDVars[ch].NoSensor = 1;
            PID(ch);
            CHECK(PIDVars[ch].PIDDuty == 0);
        }
    }
}

/** @brief Sweep controller coefficient boundaries under changing synthetic temperatures. */
static void TestBounds(void) {
    static const UINT16 coefficients[] = {0, 1, 32, 3277, 32767, 65535};
    for (int dual = 0; dual <= 1; dual++) {
        for (unsigned int kp = 0; kp < sizeof coefficients / sizeof coefficients[0]; kp++) {
            for (unsigned int ki = 0; ki < sizeof coefficients / sizeof coefficients[0]; ki++) {
                Reset(dual);
                for (int ch = 0; ch <= dual; ch++) {
                    IronPars.Config[ch].PID_KP = coefficients[kp];
                    IronPars.Config[ch].PID_KI = coefficients[ki];
                    IronPars.Config[ch].PID_DGain = 255;
                    IronPars.Config[ch].PID_OVSGain = 65535;
                }
                for (int tick = 0; tick < 256; tick++) {
                    sensorTemperature = (tick * 137) & 1023;
                    CTTemp = tick % 3 ? 225 : 0;
                    for (int ch = 0; ch <= dual; ch++) {
                        PID(ch);
                        CHECK(PIDVars[ch].Delta[0] >= -16352 && PIDVars[ch].Delta[0] <= 16352);
                        CHECK(PIDVars[ch].PIDDutyI >= 0 && PIDVars[ch].PIDDutyI <= PID_DUTY_MAX);
                        CHECK(PIDVars[ch].PIDDutyFull <= PID_DUTY_MAX);
                        CHECK(PIDVars[ch].PIDDuty <= 0xFA0000U);
                    }
                }
            }
        }
    }
}

/** @brief Fold a numeric state word into a host-endianness-independent trace digest. */
static UINT32 TraceWord(UINT32 digest, UINT32 value) {
    return (digest ^ value) * 16777619U;
}

/**
 * @brief Replay nominal single/dual-channel inputs across all waveform modes.
 * @note Uses nonzero KP and ordinary profile/gain/power ranges, where fixes must
 *       preserve the old controller's integer results. Sensor conversion remains
 *       stubbed, but its corrected ADC argument participates in the digest.
 */
static UINT32 NominalTrace(void) {
    static const INT8 modes[] = {0, -1, 1, 8};
    UINT32 digest = 2166136261U;
    for (int dual = 0; dual <= 1; dual++) {
        for (unsigned int mode = 0; mode < sizeof modes / sizeof modes[0]; mode++) {
            Reset(dual);
            for (int ch = 0; ch <= dual; ch++) {
                IronPars.Config[ch].WSLen = modes[mode];
                IronPars.Config[ch].HRCompCurrent = 10;
                IronPars.Config[ch].SensorConfig.Gain = 110;
                IronPars.Config[ch].PID_DGain = 30;
            }
            for (int tick = 0; tick < 512; tick++) {
                sensorTemperature = (tick * 31) & 1023;
                ADCData.VTEMP[1] = (tick * 17) & 1023;
                ADCData.VRT = 600;
                ADCData.HeaterOn = tick % 11 < 3;
                CTTemp = tick < 256 ? 125 : 175;
                for (int ch = 0; ch <= dual; ch++) {
                    PID(ch);
                    t_PIDVars *pv = (t_PIDVars *)&PIDVars[ch];
                    digest = TraceWord(digest, pv->PIDDuty);
                    digest = TraceWord(digest, pv->PIDDutyFull);
                    digest = TraceWord(digest, (UINT32)pv->PIDDutyI);
                    digest = TraceWord(digest, (UINT32)pv->TAvgP[0]);
                    digest = TraceWord(digest, (UINT32)pv->TSlope);
                    digest = TraceWord(digest, (UINT32)pv->WSCorr);
                    digest = TraceWord(digest, pv->WSMul);
                    digest = TraceWord(digest, (UINT32)sensorInput);
                    if (pv->KeepOff)
                        pv->KeepOff--;
                }
            }
        }
    }
    return digest;
}

/** @brief Run arithmetic regressions, or print a nominal trace for baseline comparison. */
int main(int argc, char **argv) {
    if (argc == 2 && strcmp(argv[1], "--trace") == 0) {
        printf("nominal trace: %08x\n", (unsigned int)NominalTrace());
        return 0;
    }
    TestRounding();
    TestZeroKp();
    TestWaveform();
    TestCompensation();
    TestPowerNormalization();
    TestFaultOutputs();
    TestBounds();
    /* Golden obtained from commit 2f01bd3 on MIPS/QEMU at -O0 and -O2.
     * Includes 6144 calls across single/dual channels and four waveform modes. */
    CHECK(NominalTrace() == 0x3F3C3606U);
    puts("PID arithmetic regressions passed");
    return 0;
}
