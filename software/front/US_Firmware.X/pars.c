#define _PARS_C

#include "main.h"
#include "OLED.h"
#include "pars.h"
#include "isr.h"
#include "EEP.h"
#include "iron.h"

void ParDispStr(int par, int col, int row, int num);
void ParDispNum(int par, int col, int row, int num);
void ParDispNumOff(int par, int col, int row, int num);
void ParDispTemp(int par, int col, int row, int temp);
void ParDispTempStep(int par, int col, int row, int temp);
void ParDispCF(int par, int col, int row, int num);
void ParDispInstr(int par, int col, int row, int num);

const char *StrDispRot[] = {"0   ", "180 "};
const char *StrResume[] = {"OFF ", "KEY ", "HOLD", "BOTH"};
const char *StrButtons[] = {"+/- ", "-/+ "};
const char *StrOffOnAuto[] = {"OFF ", "ON  ", "AUTO"};
const char *StrMenuUp[] = {"KEY+", "KEY-"};

// List of menu items in order of display
//                                 0  1  2 3 4 5 6 7 8 9 10 11 1213 14 1516 17 18 19
const unsigned char MenuOrder[] = {18, 19, 0, 1, 2, 3, 4, 5, 6, 7, 11, 13, 8, 10, 14, 9, 12, 16, 17, 20};

const t_ParDef ParDef[] = {
    //  NAME            DEF  MIN      MAX      IMMEDIATE SUFFIX STRINGS       DISPFUNC

    {"        T1 ", 125, MINTEMP, MAXTEMP, 0, 0, 0, &ParDispTemp}, //0
    {"        T2 ", 150, MINTEMP, MAXTEMP, 0, 0, 0, &ParDispTemp}, //1
    {"        T3 ", 175, MINTEMP, MAXTEMP, 0, 0, 0, &ParDispTemp}, //2
    {"  HOLDER T ", 150, MINTEMP, MAXTEMP, 0, 0, 0, &ParDispTemp}, //3
    {"   SLEEP T ", 125, MINTEMP, MAXTEMP, 0, 0, 0, &ParDispTemp}, //4
    {"     SLEEP ", 20, 0, 255, 0, "s", 0, &ParDispNumOff},        //5
    {"  STAND-BY ", 30, 0, 255, 0, "m", 0, &ParDispNumOff},        //6
    {"    HOLDER ", 2, 0, 2, 0, 0, StrOffOnAuto, &ParDispStr},     //7
    {"   DEGREES ", 0, 0, 1, 1, 0, 0, &ParDispCF},                 //8
    {"BRIGHTNESS ", 15, 1, 15, 1, 0, 0, &ParDispNum},              //9
    {"   BUTTONS ", 0, 0, 1, 0, 0, StrButtons, &ParDispStr},       //10
    {"   WAKE UP ", 0, 0, 3, 0, 0, StrResume, &ParDispStr},        //11
    {"  ROTATION ", 0, 0, 1, 1, 0, StrDispRot, &ParDispStr},       //12
    {"SENSOR FLT ", 0, 0, 255, 0, 0, 0, &ParDispNumOff},           //13
    {" MENU DOWN ", 0, 0, 1, 0, 0, StrMenuUp, &ParDispStr},        //14
    {"     INPUT ", 0, 0, 5, 0, 0, 0, 0},                          //15
    {" CALIBRATE ", 0, 0, 0, 0, 0, 0, 0},                          //16
    {" INST.INFO ", 0, 0, 0, 0, 0, 0, 0},                          //17
    {" TEMP.STEP ", 1, 1, 25, 0, 0, 0, &ParDispTempStep},          //18
    {"   MODEL   ", 0, 0, NB_IRONS, 0, 0, 0, &ParDispInstr},       //19
    {"   VERSION ", 0, 0, 0, 0, 0, 0, 0},                          //20
};

/**
 * @brief Draw an enumerated parameter's string at a pixel column/page row.
 * @pre par and num must index a valid ParDef entry and its Strings table;
 *      the selected text must fit the framebuffer. No bounds checks are made.
 */
void ParDispStr(int par, int col, int row, int num) {
    OLEDPrint816(col, row, ParDef[par].Strings[num], 0);
}

/**
 * @brief Draw AUTO (num <= 0) or a one-based instrument selection on two pages.
 * @note Uses bounded six-character slices of IronDesc(); par is unused.
 *       col is a pixel column and row is an 8-pixel page index.
 */
void ParDispInstr(int par, int col, int row, int num) {
    static const char *AUTO = " AUTO ";

    if (num <= 0) {
        OLEDPrint68(col, row, AUTO, 6);
        OLEDPrint68(col, row + 1, "      ", 6);
        return;
    }
    const char *desc = IronDesc(num - 1); // 24 characters
    OLEDPrint68(col, row, desc, 6);
    OLEDPrint68(col, row + 1, &desc[6], 6);
}

/**
 * @brief Draw a three-position decimal parameter and its optional suffix.
 * @pre par must index ParDef; col is a pixel column and row an 8-pixel page.
 *      Caller supplies a value and coordinates fitting the field/framebuffer.
 */
void ParDispNum(int par, int col, int row, int num) {
    OLEDPrintNum816(col, row, 3, num);
    if (ParDef[par].Suffix)
        OLEDPrint816(col + 24, row, ParDef[par].Suffix, 0);
}

/** @brief Draw OFF for zero, otherwise use ParDispNum() with its same contract. */
void ParDispNumOff(int par, int col, int row, int num) {
    if (num) {
        ParDispNum(par, col, row, num);
    } else {
        OLEDPrint816(col, row, StrOffOnAuto[0], 0);
    }
}

/**
 * @brief Draw the degree symbol and C (zero) or F (nonzero) in a two-page field.
 * @note par is unused; col is a pixel column and row an 8-pixel page index.
 */
void ParDispCF(int par, int col, int row, int num) {
    OLEDWrite(col, 4, row, (void *)degrees4x16, 8);
    OLEDPrint816(col + 4, row, num ? "F" : "C", 0);
}

/**
 * @brief Render an absolute temperature stored in 2 C steps with its unit.
 * @note pars.Deg selects Celsius/Fahrenheit; Fahrenheit includes the +32 offset.
 *       Coordinates and parameter-index requirements are those of ParDispNum().
 */
void ParDispTemp(int par, int col, int row, int temp) {
    if (pars.Deg) {
        ParDispNum(par, col, row, ((temp * 461) >> 7) + 32);
    } else {
        ParDispNum(par, col, row, temp * 2);
    }
    ParDispCF(par, col + 24, row, pars.Deg);
}

/**
 * @brief Render a temperature interval stored in 2 C steps with its unit.
 * @note Fahrenheit conversion is rounded and has no absolute-temperature offset.
 *       Coordinates and parameter-index requirements are those of ParDispNum().
 */
void ParDispTempStep(int par, int col, int row, int temp) {
    if (pars.Deg) {
        ParDispNum(par, col, row, ((temp * 461) + 64) >> 7); //temperature difference: no +32 offset, rounded
    } else {
        ParDispNum(par, col, row, temp * 2);
    }
    ParDispCF(par, col + 24, row, pars.Deg);
}

/**
 * @brief Restore parameter bytes and the last target temperature from EEPROM.
 *
 * Any out-of-range parameter resets the entire parameter block to defaults.
 * Reads the target from a 64-byte ring at addresses 64..127, using an erased
 * predecessor as its marker; defaults to 150 (300 C) if no valid entry is found.
 * @pre Foreground only, after initializing the interrupt-driven I2C engine.
 * @note Blocks through EEPRead(); no EEPROM failure status reaches the caller.
 */
void LoadPars(void) {
    int i;
    UINT8 b, oldb;

    EEPRead(0, (UINT8 *)&pars, sizeof(pars));
    for (i = 0; i < sizeof(pars.b); i++) {
        if ((pars.b[i] < ParDef[i].Min) || (pars.b[i] > ParDef[i].Max)) {
            for (i = 0; i < sizeof(pars.b); i++) {
                pars.b[i] = ParDef[i].Default;
            }
            break;
        }
    }

    TTemp = 150;
    oldb = EEPRead(64 + 63, 0, 1);
    for (i = 0; i < 64; i++) {
        b = EEPRead(i + 64, 0, 1);
        if ((oldb == 0xFF) && (b >= MINTEMP) && (b <= MAXTEMP)) {
            TTemp = b;
            break;
        }
        oldb = b;
    }
}

/**
 * @brief Persist changed parameters and advance the target-temperature ring.
 *
 * Writes only differing parameter bytes. A changed target invalidates the old
 * ring entry with 0xFF and writes the next slot, wrapping across addresses 64..127.
 * This reduces repeated writes but is not a transactional power-fail guarantee.
 * @pre Foreground only with I2C interrupts running and enough supply hold-up time.
 * @note Blocking; final EEPROM internal write completion is not separately polled.
 */
void SavePars(void) {
    int i;
    UINT8 b, oldb;

    for (i = 0; i < sizeof(pars.b); i++) {
        if (EEPRead(i, 0, 1) != pars.b[i])
            EEPWriteImm(i, pars.b[i]);
    }

    oldb = EEPRead(64 + 63, 0, 1);
    for (i = 0; i < 64; i++) {
        b = EEPRead(64 + i, 0, 1);
        if ((oldb == 0xFF) && (b >= MINTEMP) && (b <= MAXTEMP))
            break;
        oldb = b;
    }
    i &= 63;
    b = EEPRead(64 + i, 0, 1);
    if (b != TTemp) {
        EEPWriteImm(i + 64, 0xFF);
        EEPWriteImm(((i + 1) & 63) + 64, TTemp);
    }
}

#undef _PARS_C
