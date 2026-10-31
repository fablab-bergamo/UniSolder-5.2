#define _EEP_C

#include "isr.h"

/**
 * @brief Transfer a byte range to EEPROM through the interrupt-driven I2C engine.
 * @param[in] a EEPROM byte address; 0xFFFF is reserved as the idle marker.
 * @param[in] b Readable buffer of at least c bytes; retained until return.
 * @param[in] c Byte count; zero performs no transfer.
 * @pre Call from foreground code with I2C interrupts able to run.
 * @note Blocks without a timeout. Completion means the I2C engine consumed the
 *       buffer, not that the final EEPROM internal write cycle has been polled.
 */
void EEPWrite(UINT16 a, UINT8 *b, UINT16 c) {
    if (c) {
        while (EEPAddrW != 0xFFFF) {
        };
        EEPAddrW = a;
        EEPDataW = b;
        EEPCntW = c;
        I2CAddCommands(I2C_EEPWRITE);
        while (EEPAddrW != 0xFFFF) {
        };
    }
}

/**
 * @brief Transfer one byte to EEPROM, keeping its local buffer alive until sent.
 * @param[in] a EEPROM byte address, excluding the 0xFFFF idle marker.
 * @param[in] b Byte to store.
 * @pre Call from foreground code with I2C interrupts able to run.
 * @note Has the same unbounded waits and write-cycle semantics as EEPWrite().
 */
void EEPWriteImm(UINT16 a, UINT8 b) {
    while (EEPAddrW != 0xFFFF) {
    };
    EEPAddrW = a;
    EEPDataW = &b;
    EEPCntW = 1;
    I2CAddCommands(I2C_EEPWRITE);
    while (EEPAddrW != 0xFFFF) {
    };
}

/**
 * @brief Read EEPROM bytes, or a single byte when no destination is supplied.
 * @param[in] a EEPROM byte address, excluding the 0xFFFF idle marker.
 * @param[out] b Writable buffer of at least c bytes; NULL selects a local byte
 *                and reduces a nonzero request to one byte.
 * @param[in] c Requested byte count; zero performs no transfer.
 * @return Last byte read, or 0xFF for a zero-length request; not an error status.
 * @pre Call from foreground code with I2C interrupts able to run.
 * @note Blocks without a timeout; device failures are not reported to the caller.
 */
UINT8 EEPRead(UINT16 a, UINT8 *b, UINT16 c) {
    UINT8 lb;
    if (c) {
        if (b == 0) {
            b = &lb;
            c = 1;
        }
        while (EEPAddrR != 0xFFFF)
            ;
        EEPAddrR = a;
        EEPDataR = b;
        EEPCntR = c;
        I2CAddCommands(I2C_EEPREAD);
        while (EEPAddrR != 0xFFFF)
            ;
        return b[c - 1];
    }
    return 0xFF;
}

#undef __EEP_C
