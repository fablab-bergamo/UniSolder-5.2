#define _PID_C

#include <GenericTypeDefs.h>
#include "PID.h"
#include "isr.h"
#include "iron.h"
#include "main.h"
#include "sensorMath.h"

/* Duty is an unsigned 24-bit fraction; the stored integral is always nonnegative. */
#define PID_DUTY_MAX 0x00FFFFFFL

/**
 * @brief Divide signed fixed-point state by 2^bits, truncating toward zero.
 * @param[in] value Any INT32 value, including its minimum representable value.
 * @param[in] bits Shift-equivalent count in [0, 30]; PID callers use at most 17.
 * @note Unlike the old magnitude/negation macro, division never negates INT32_MIN.
 *       This intentionally differs from arithmetic right shift for negative odd
 *       values. Constant power-of-two divisors can be optimized by the compiler.
 */
static inline INT32 PIDDivPow2Trunc(INT32 value, unsigned int bits) {
    return value / ((INT32)1 << bits);
}

/**
 * @brief Divide a wide compensation intermediate by 2^bits, rounding downward.
 * @param[in] value Signed product; widening must occur before multiplication.
 * @param[in] bits Count in [0, 63]; compensation uses the constants 15 and 11.
 * @note Preserves XC32's arithmetic-right-shift rounding without relying on a
 *       signed shift or negating INT64_MIN. For negative value, the complement
 *       represents -value - 1 without signed overflow; -1 - its shifted value
 *       supplies floor rounding. This needs no 64-bit division in the ISR.
 *       Keep both compensation stages: combining them changes quantization.
 */
static inline INT64 PIDDivPow2Floor64(INT64 value, unsigned int bits) {
    if (value >= 0)
        return (INT64)((UINT64)value >> bits);
    return -1 - (INT64)(~(UINT64)value >> bits);
}

void PIDInit() {
    int i;
    CJTemp = -273 * 2;
    for (i = 2; i--;) {
        PIDVars[i].Starting = 1;
        PIDVars[i].PIDDuty = 0;
        PIDVars[i].PIDDutyP = 0;
        PIDVars[i].PIDDutyI = 0;
        PIDVars[i].PIDDutyFull = 0;
        PIDVars[i].PWM = 0;
        PIDVars[i].OffDelay = 1600;
        PIDVars[i].Power = 3; //1/8 power as a start
    }
};

void PIDTasks() {
    t_SensorConfig *CJC = (t_SensorConfig *)IronPars.ColdJunctionSensorConfig;
    if (CJC && ADCData.VCJ && ADCData.VCJ < 1023) {
        INT32 t = GetSensorTemperature(ADCData.VCJ, CJC);
        if (t > -273 * 2 && t < 200) {
            CJTemp = t;
        } else {
            CJTemp = -273 * 2;
        }
    } else if (!CJC) {
        CJTemp = -273 * 2;
    }
    ADCData.VCJ = 0;
}

#define assertin(a, b, c) \
    if (b < c) {          \
        if (a < b)        \
            a = b;        \
        if (a > c)        \
            a = c;        \
    } else {              \
        if (a < c)        \
            a = c;        \
        if (a > b)        \
            a = b;        \
    }

void PID(int PIDStep) {
    int i, w, AVG;
    INT32 dw, pdt;
    t_PIDVars *PV;
    t_IronConfig *IC;
    int WSL;
    int dual = !!IronPars.Config[1].SensorConfig.Type;

    AVG = ADCAVG;
    PV = (t_PIDVars *)&PIDVars[PIDStep];
    IC = (t_IronConfig *)&IronPars.Config[PIDStep];
    if (dual) {
        AVG--;
    } else {
        PV = (t_PIDVars *)&PIDVars[0];
        IC = (t_IronConfig *)&IronPars.Config[0];
    }
    WSL = IC->WSLen;
    if (WSL < 0)
        WSL = dual ? 4 : 8;

    if (PV->NoHeater || PV->NoSensor || PV->Starting || PV->ShortCircuit || PV->LastTTemp != CTTemp)
        PV->DestinationReached = 0;
    PV->LastTTemp = CTTemp;

    if (PIDStep) {
        dw = ADCData.VRT;
        dw *= 147;
        dw >>= 8;
        dw -= 200;
        RTAvg -= PIDDivPow2Trunc(RTAvg, ADCAVG);
        RTAvg += dw;
        CRTemp = PIDDivPow2Trunc(RTAvg, ADCAVG + 1);
    }

    if (PV->NoHeater)
        PV->NoHeaterCnt = (1 << (AVG + 1));
    if (PV->NoHeaterCnt)
        PV->NoHeaterCnt--;
    if (PV->NoHeater) {
        PV->HInitData = 1;
        PV->HVAvg = 0;
        PV->HIAvg = 0;
        PV->HRAvg = 0x7FFF;
        PV->HPAvg = 0;
        PV->HPMax = 0;
        PV->OffDelay = 1600;
    } else {
        if (PV->HInitData)
            PV->HRAvg = PV->HR << AVG;
        if (PV->HNewData) {
            if (PV->HInitData) {
                PV->HVAvg = PV->HV << AVG;
                PV->HIAvg = PV->HI << AVG;
                PV->HRAvg = PV->HR << AVG;
                PV->HPAvg = PV->HP << AVG;
                PV->HPMax = PV->HP;
                PV->HInitData = 0;
            }
            PV->HVAvg -= PV->HVAvg >> AVG;
            PV->HVAvg += PV->HV;
            PV->HIAvg -= PV->HIAvg >> AVG;
            PV->HIAvg += PV->HI;
            PV->HRAvg -= PV->HRAvg >> AVG;
            PV->HRAvg += PV->HR;
            PV->HPAvg -= PV->HPAvg >> AVG;
            PV->HPAvg += PV->HP;
            if (PV->HPMax < PV->HP)
                PV->HPMax = PV->HP;
            PV->HNewData = 0;
        }
        if (PV->NoSensor || PV->ShortCircuit)
            PV->HPMax = 0;
    }

    PV->ADCTemp[1] = PV->ADCTemp[0];
    PV->ADCTemp[0] = ADCData.VTEMP[1];

    PV->WSCorr = 0;
    if (IC->WSLen) {
        if (PV->Starting || PV->NoHeaterCnt || PV->NoSensor || PV->ShortCircuit) {
            for (i = 8; i--;) {
                PV->WSDelta[i].cnt = 0;
                PV->WSDelta[i].val = 0;
                PV->WSMul = 32768;
                PV->WSTemp = 0;
            }
        } else {
            //if(PV->DestinationReached == 0) for(i = 8; i--;) PV->WSDelta[i].cnt = 0;
            if (ADCData.HeaterOn) {
                if (PV->OffCnt > 1) {
                    i = min(PV->OffCnt, 8);
                    w = PV->WSDelta[0].val;
                    if (i > PV->WSDelta[0].cnt) {
                        PV->WSDelta[0].cnt = i;
                        /* Two 10-bit samples differ by at most 1023. Multiplying
                         * by two is exact in INT32 even for a negative difference;
                         * left-shifting that negative difference is undefined C. */
                        PV->WSDelta[0].val = (PV->ADCTemp[1] - PV->ADCTemp[0]) * 2;
                    } else if (i == PV->WSDelta[0].cnt) {
                        PV->WSDelta[0].val = PIDDivPow2Trunc(PV->WSDelta[0].val, 1);
                        PV->WSDelta[0].val += (PV->ADCTemp[1] - PV->ADCTemp[0]);
                    }
                    w -= PV->WSDelta[0].val;
                    for (i = 1; i < 8; i++)
                        PV->WSDelta[i].val -= w;
                }
                PV->LastOn = PV->ADCTemp[0];
                PV->OffCnt = 0;
                PV->WSCorr = PIDDivPow2Trunc(PV->WSDelta[0].val, 1);
            } else {
                if (PV->OffCnt < 8) {
                    if (PV->OffCnt > 0) {
                        if (PV->WSDelta[PV->OffCnt].cnt == 0) {
                            PV->WSDelta[PV->OffCnt].val = PIDDivPow2Trunc(PV->WSDelta[0].val, 1) - (PV->ADCTemp[0] - PV->LastOn);
                            PV->WSDelta[PV->OffCnt].cnt = 1;
                        } else {
                            PV->WSDelta[PV->OffCnt].val = PIDDivPow2Trunc(PV->WSDelta[PV->OffCnt].val, 1);
                        }
                        PV->WSDelta[PV->OffCnt].val += PIDDivPow2Trunc(PV->WSDelta[0].val, 1) - (PV->ADCTemp[0] - PV->LastOn);
                        assertin(PV->WSDelta[PV->OffCnt].val, 0, PV->WSDelta[PV->OffCnt - 1].val);
                        if (PV->OffCnt == 1) {
                            //WSMul = multiplier for damping waveshaping
                            if (PV->WSDelta[0].val != 0) {
                                /* WSDelta[0] remains within +/-2046; assertin()
                                 * bounds WSDelta[1] between zero and that value.
                                 * Thus numerator * 65536 fits signed 32 bits.
                                 * Clamp while signed, before storing in unsigned
                                 * WSMul; testing WSMul < 0 cannot detect negatives.
                                 * A zero denominator deliberately retains the last
                                 * multiplier, as in the original controller. */
                                INT32 ratio = ((INT32)PV->WSDelta[1].val * 65536L) / (INT32)PV->WSDelta[0].val;
                                if (ratio < 0)
                                    ratio = 0;
                                if (ratio > 32768)
                                    ratio = 32768;
                                PV->WSMul = (UINT32)ratio;
                            }
                        }
                    }
                    if (IC->WSLen < 0) {
                        //damping waveshaping
                        if (PV->WSDelta[0].val > 0) {
                            dw = (PV->WSMul * 1152) >> 10;
                            for (i = 1; i < PV->OffCnt; i++) {
                                dw = (dw * ((PV->WSMul * 1700) >> 10)) >> 16;
                            }
                        } else {
                            dw = PV->WSMul;
                            for (i = 1; i < PV->OffCnt; i++) {
                                dw = (dw * PV->WSMul) >> 16;
                            }
                        }
                        PV->WSCorr = PIDDivPow2Trunc((((INT32)PV->WSDelta[0].val) * dw), 17);
                    } else {
                        //sample waveshaping
                        if (PV->OffCnt < IC->WSLen)
                            PV->WSCorr = PIDDivPow2Trunc(PV->WSDelta[PV->OffCnt].val, 1);
                    }
                }
            }
            if (PV->OffCnt < 255)
                PV->OffCnt++;
        }
    }

    /* Use the correction directly for conversion, retaining its legacy x1024
     * stored scale. The old multiply/shift round trip relied on signed right
     * shift when the correction was negative. ADC-derived corrections fit here. */
    w = PV->WSCorr;
    PV->WSCorr *= 1024;

    w += PV->ADCTemp[0];
    if (w < 0)
        w = 0;

    //heater resistance compensation for series TC
    //compensation = (HRCompCurrent * 19.6 * Gain * HRAvg)/65536
    //compensation = (((HRCompCurrent * Gain * 20070) / 32767) * HRAvg) / 2048;
    /* A 16-bit gain can overflow the first 32-bit product even though the final
     * correction fits. Widen before multiplying, not after, and preserve both
     * original floor-rounding stages (also for negative compensation current).
     * With ADC-derived HR <= 32767 and the shipped profiles' |HRCompCurrent| <= 10,
     * the final correction fits INT32 even for a full-range 16-bit gain. */
    INT64 compensation = (INT64)IC->SensorConfig.Gain * IC->HRCompCurrent * 20070;
    compensation = PIDDivPow2Floor64(compensation, 15);
    compensation *= PV->HRAvg >> AVG;
    w -= (INT32)PIDDivPow2Floor64(compensation, 11);

    dw = GetSensorTemperature(w, &IC->SensorConfig);

    if (dw > 1023)
        dw = 1023;
    if (dw < 0)
        dw = 0;
    PV->CTemp[0] = dw;

    // >>> At this point in CTemp[0] we have temperature in degrees Celsius, multiplied by 2. <<<

    if (PV->Starting || PV->NoHeaterCnt) {
        PV->Starting = 0;
        i = (1 << ADCAVG);
        while (i--) {
            PV->TBuff[i] = PV->CTemp[0];
            PV->SlopeBuff[i] = PV->CTemp[0];
        }
        PV->TAvgP[0] = PV->CTemp[0];
        PV->TAvgP[1] = PV->CTemp[0];
        PV->TAvg = PV->CTemp[0];
        i = AVG;
        while (i--)
            PV->TAvg += PV->TAvg;
        PV->TAvgF[0] = PV->TAvg;
        PV->TAvgF[1] = PV->TAvg;
        PV->TSlope = 0;
        PV->TBPos = 0;
        PV->SBPos = 0;
    }

    //TAvg = averaged temperature
    PV->TAvg -= PV->TBuff[PV->TBPos];
    PV->TAvg += PV->CTemp[0];

    //TBuff is array with most recent temperatures, used for TAvg calculation
    PV->TBuff[PV->TBPos] = PV->CTemp[0];
    PV->TBPos++;
    PV->TBPos &= (1 << AVG) - 1;

    //TAvgF = software RC filtered temperature
    PV->TAvgF[1] = PV->TAvgF[0];
    PV->TAvgF[0] -= PV->TAvgF[0] >> AVG;
    PV->TAvgF[0] += PV->CTemp[0];

    //SlopeBuff is array with most recent averaged temperatures, used for TSlope calculation
    PV->SlopeBuff[PV->SBPos] = PV->TAvg;
    PV->SBPos++;
    PV->SBPos &= (1 << AVG) - 1;

    //TSlope is current temperature slope, used with DGain to calculate current predicted temperature for PI controller
    PV->TSlope -= PIDDivPow2Trunc(PV->TSlope, 2);
    PV->TSlope += PV->SlopeBuff[(PV->SBPos - 1) & ((1 << AVG) - 1)];
    PV->TSlope -= PV->SlopeBuff[PV->SBPos];

    dw = ((INT32)IC->PID_DGain) * PIDDivPow2Trunc(PV->TSlope, 2);
    dw = PIDDivPow2Trunc(dw, AVG + 2);

    //TAvgP is array with current end previous temperature for PI controller, TAvgP=TAvgF + (TSlope * DGain) / 4
    PV->TAvgP[1] = PV->TAvgP[0];
    PV->TAvgP[0] = (PV->TAvgF[0] >> AVG);
    PV->TAvgP[0] += dw;
    if (PV->TAvgP[0] < 0)
        PV->TAvgP[0] = 0;
    if (PV->TAvgP[0] > 2047)
        PV->TAvgP[0] = 2047;

    if (PV->DestinationReached == 0 && WSL > 0) {
        if (!PV->NoHeater && !PV->NoSensor && !PV->Starting && !PV->ShortCircuit) {
            w = CTTemp << 2;
            if (((PV->TAvgP[0] >= w) && (PV->TAvgP[1] < w)) || ((PV->CTemp[0] >= w) && (PV->CTemp[1] < w))) {
                //Shut off the power when destination is reached in order to calculate waveshaping shape.
                PV->WSDelta[0].cnt = WSL - 1;
                PV->KeepOff = WSL + 1; //(1 << AVG) + 1;
                PV->DestinationReached = 1;
            }
        }

        if (PV->DestinationReached == 0) {
            w = PV->TAvgF[0] >> AVG;
            w -= PV->WSTemp;
            if (w <= -99 || w > 99) {
                PV->WSTemp += w; //PV->TAvgF[0] >> AVG;
                PV->WSDelta[0].cnt = WSL - 1;
                PV->KeepOff = WSL + 1; //(1 << AVG) + 1;
            }
        }

        if (PV->KeepOff && (PV->NoHeater || PV->ShortCircuit || PV->NoSensor))
            PV->KeepOff = 0;
    }

    //Delta stores the clamped error in half-degree Celsius units, scaled by 32.
    PV->Delta[1] = PV->Delta[0];
    PV->Delta[0] = CTTemp << 2;
    PV->Delta[0] -= PV->TAvgP[0];
    if (PV->Delta[0] > 511)
        PV->Delta[0] = 511;
    if (PV->Delta[0] < -511)
        PV->Delta[0] = -511;
    /* Error is already clamped to +/-511, so scaling by 32 is representable
     * (+/-16352). Multiplication also defines the negative-error case in ISO C. */
    PV->Delta[0] *= 32;

    /* PIDInit() starts I at zero; every path below keeps I in [0, 0x00FFFFFF].
     * Even a full UINT16 KI gives |Delta * KI| <= 1,071,628,320, so this addition
     * stays within INT32 before clamping (maximum 1,088,405,535). No 64-bit
     * integration is needed in the priority-7 ISR under these explicit bounds.
     * Clamp the lower bound independently of P's sign: with KP == 0, P is zero
     * even for negative error, so the old P-negative-only clamp allowed I to
     * decrease until it overflowed and generated a positive heater request. */
    PV->PIDDutyI += PV->Delta[0] * (int)IC->PID_KI;
    if (PV->PIDDutyI < 0)
        PV->PIDDutyI = 0;
    if ((PV->PIDDutyP = PV->Delta[0] * (int)IC->PID_KP) >= 0) {
        dw = PID_DUTY_MAX;
        if (PV->PIDDutyP > dw)
            PV->PIDDutyP = dw;
        dw -= PV->PIDDutyP;
        if (PV->PIDDutyI > dw || (PV->DestinationReached == 0 && PV->Delta[0] > (100 << 6)))
            PV->PIDDutyI = dw;
    } else {
        dw = -(PV->Delta[0]) * (int)IC->PID_OVSGain;
        if (dw > 4095)
            dw = 4095;
        dw <<= 12;
        dw = PID_DUTY_MAX - dw;
        if (PV->PIDDutyI > dw)
            PV->PIDDutyI = dw;
    }
    pdt = PV->PIDDutyP + PV->PIDDutyI;
    if (pdt < 0)
        pdt = 0;
    if ((IC->SensorConfig.Type == SENSOR_UNDEFINED) || (IC->SensorConfig.Type == SENSOR_NONE) || PV->NoSensor)
        pdt = 0;

    PV->PIDDutyFull = pdt; //normalized duty

    //Duty=Duty * rated power / peak power
    pdt += 0x7FL;
    pdt >>= 8;
    if (PV->HP) {
        dw = PV->HPAvg >> AVG;
        if (dw > 0) { //HPAvg is zeroed on NoHeater while HP keeps its last value
            /* Rounded duty is at most 65536; multiplying by a UINT16 power
             * limit fits UINT32 (<= 0xFFFF0000) but may overflow INT32. Divide
             * while unsigned, then cap before narrowing. The later current
             * limiter is always below 65535, so this cap does not alter its
             * result. Preserve the existing zero-average-power fallback. */
            UINT32 ratedDuty = ((UINT32)pdt * IC->PID_PMax) / (UINT32)dw;
            pdt = ratedDuty > 65535U ? 65535 : (INT32)ratedDuty;
        }
        if (mainFlags.TipChange || PV->NoHeater || PV->NoSensor || PV->ShortCircuit) {
            PV->HPMax = 0;
            PV->Power = 3;
        } else {
            dw = PV->HPMax;
            if (PV->Power > 0 && dw < (INT32)IC->PID_PMax)
                PV->Power--;
            dw >>= 1;
            if (PV->Power < 3 && dw >= (INT32)IC->PID_PMax) {
                PV->HPMax = dw;
                PV->Power++;
            }
        }
    }

    dw = 0xFA00L;
    if (PV->HI) {
        dw = PV->HIAvg >> AVG;
        if (dw > 192) {
            dw = (INT32)(65535 * 192) / dw;
        } else {
            dw = 0xFA00;
        }
    }
    if (pdt > dw)
        pdt = dw;

    pdt <<= 8;
    if (PV->NoHeater)
        pdt = dual ? 0x51EB8 : 0x28F5C; //PWM = once per 2 seconds in order to detect heater resistance on open heater
    if (PV->ShortCircuit)
        pdt = dual ? 0x20C48 : 0x10624; //PWM = once per 5 seconds in order to detect heater resistance on short circuit
    if (mainFlags.TipChange) {
        pdt = 0; //Don't turn on power while instrument is in tip-change holder
        PV->PWM = 0;
    }
    PV->PIDDuty = pdt;
}

#undef _PID_C
