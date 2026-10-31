#define _PIC32MX564F128H_C

#include <xc.h>
// DEVCFG3
// USERID = No Setting
#pragma config FSRSSEL = PRIORITY_7     // SRS Select (SRS Priority 7)
#pragma config FCANIO = OFF             // CAN I/O Pin Select (Alternate CAN I/O)
#pragma config FUSBIDIO = OFF           // USB USID Selection (Controlled by Port Function)
#pragma config FVBUSONIO = OFF          // USB VBUS OFF Selection (Not Controlled by USB Module)

// DEVCFG2
#pragma config FPLLIDIV = DIV_2         // PLL Input Divider (2x Divider)
#pragma config FPLLMUL = MUL_20         // PLL Multiplier (20x Multiplier)
#pragma config UPLLIDIV = DIV_2         // USB PLL Input Divider (2x Divider)
#pragma config UPLLEN = ON              // USB PLL Enable (Enabled)
#pragma config FPLLODIV = DIV_1         // System PLL Output Clock Divider (PLL Divide by 1)

// DEVCFG1
#pragma config FNOSC = PRIPLL           // Oscillator Selection Bits (Primary Osc w/PLL (XT+,HS+,EC+PLL))
#pragma config FSOSCEN = OFF            // Secondary Oscillator Enable (Disabled)
#pragma config IESO = OFF               // Internal/External Switch Over (Disabled)
#pragma config POSCMOD = XT             // Primary Oscillator Configuration (XT osc mode)
#pragma config OSCIOFNC = OFF           // CLKO Output Signal Active on the OSCO Pin (Disabled)
#pragma config FPBDIV = DIV_2           // Peripheral Clock Divisor (Pb_Clk is Sys_Clk/1)
#pragma config FCKSM = CSDCMD           // Clock Switching and Monitor Selection (Clock Switch Disable, FSCM Disabled)
#pragma config WDTPS = PS1048576        // Watchdog Timer Postscaler (1:1048576)
#pragma config FWDTEN = OFF             // Watchdog Timer Enable (WDT Disabled (SWDTEN Bit Controls))

// DEVCFG0
#pragma config DEBUG = OFF              // Background Debugger Enable (Debugger is disabled)
#pragma config ICESEL = ICS_PGx2        // ICE/ICD Comm Channel Select (ICE EMUC2/EMUD2 pins shared with PGC2/PGD2)
#pragma config PWP = OFF                // Program Flash Write Protect (Disable)
#pragma config BWP = OFF                // Boot Flash Write Protect bit (Protection Disabled)
#pragma config CP = OFF                 // Code Protect (Protection Disabled)


#include <GenericTypeDefs.h>
#include "PIC32MX564F128H.h"
#include <peripheral/int.h>
#include <peripheral/nvm.h>
#include "usb/usb.h"
#include "crc.h"

typedef struct
{
	UINT32 Address;
	UINT16 RecDataLen;
	UINT8* Data;
}T_FLASH_RECORD;

/**
 * @brief Busy-wait for a core-timer interval without changing interrupt state.
 * @param[in] a Tick count at CORETIMER_FREQ; unsigned subtraction tolerates wrap.
 * @pre Core timer running and not reset during the wait.
 */
void DelayTicks(UINT32 a){
    UINT32 StartTime;
    StartTime=ReadCoreTimer();
    while((UINT32)(ReadCoreTimer()-StartTime)<a){};
}

/**
 * @brief Initialize bootloader clocks/GPIO with heater drive and interrupts off.
 * @note Establishes core-timer delays and speaker hardware; USB/display setup
 *       follows only when main() decides to remain in the recovery loop.
 */
void mcuInit(void)
{
    INTDisableInterrupts();
    OpenCoreTimer(0xFFFFFFFF);
    SYSTEMConfigWaitStates(80000000);
    
    CNPUE = 0;

    TRISB = 0b1110000000111111;
    TRISC = 0b1001000000000000;
    TRISD = 0b0000000000100000;
    TRISE = 0;
    TRISF = 0;
    TRISG = 0b1000111111;

    ODCB = 0;
    ODCC = 0;
    ODCD = 0;
    ODCE = 0;
    ODCF = 0;
    ODCG = 0;

    LATB = 0b1000000000000000;
    LATC = 0;
    LATD = 0b0000100001010000;
    LATE = 0;
    LATF = 0;
    LATG = 0;

    HCH = 0;
    ID_3S = 0;
    ID_OUT = 0;
    HEATER = 0;
    CBANDA = 1;
    CBANDB = 1;
    CHSEL1 = 0;
    CHSEL2 = 1;
    CHPOL = 0;

    OpenTimer3(T3_ON | T3_IDLE_STOP | T3_GATE_OFF | T3_PS_1_256 | T3_SOURCE_INT, 40);
    OpenOC1(OC_ON | OC_IDLE_STOP | OC_TIMER_MODE16 | OC_TIMER3_SRC | OC_PWM_FAULT_PIN_DISABLE, 0x20, 0);
    SPKOFF;
}

/**
 * @brief Disable USB module/interrupts and clear its pending interrupt status.
 * @note Preserves the previous CPU interrupt-enable state; outstanding USB
 *       transfers are abandoned. Called before reset from the recovery service.
 */
void mcuDisableUSB(void)
{
    unsigned int IntState;
    IntState = INTDisableInterrupts();
    USBDisableInterrupts();
    USBModuleDisable();
    USBClearUSBInterrupt();
    INTRestoreInterrupts(IntState);
}

/**
 * @brief Call the application's reset entry at USER_APP_RESET_ADDRESS.
 * @pre Caller has validated the application and established startup-safe state.
 * @note Does not validate flash, disable USB, or reset peripherals itself.
 *       Application startup is expected not to return to this call site.
 */
void mcuJumpToApp(void)
{
	void (*fptr)(void);
	fptr = (void (*)(void))USER_APP_RESET_ADDRESS;
	fptr();
}

/**
 * @brief Erase the configured application-flash and application-vector pages.
 * @return Status from the last attempted NVMErasePage() call (zero is success).
 * @note Blocking/destructive; erasing application flash also removes its CRC word.
 * @warning An application-region failure stops that loop, but the vector loop
 *          still runs and can overwrite the failure status. A zero return is
 *          therefore not proof that every requested page was erased successfully.
 */
UINT mcuEraseFlash(void){
    UINT result = 1;
    UINT32 i;
    UINT32 pFlash = APP_FLASH_BASE_ADDRESS;

    for( i = 0; i < ((APP_FLASH_END_ADDRESS - APP_FLASH_BASE_ADDRESS + 1)/FLASH_PAGE_SIZE); i++ )
    {
        result = NVMErasePage( (void *)(pFlash + i*FLASH_PAGE_SIZE) );
        if(result)break;
    }
    
    pFlash = APP_IVT_BASE_ADDRESS;
    for( i = 0; i < ((APP_IVT_END_ADDRESS - APP_IVT_BASE_ADDRESS + 1)/FLASH_PAGE_SIZE); i++ )
    {
        result = NVMErasePage( (void *)(pFlash + i*FLASH_PAGE_SIZE) );
        if(result)break;
    }
    
    return result;
}

/**
 * @brief Program a binary address/length/data record using NVM word or row writes.
 * @param[in] RecordData 32-bit-aligned buffer containing a physical UINT32 address,
 *                      a UINT32 length (stored internally as UINT16), and data.
 * @return Last NVM write status, or zero when no write/error was reported. The
 *         word path silently skips ranges outside its permitted address checks.
 * @pre Caller validates buffer length, word alignment, and every destination.
 *      The HID transport must constrain the record to its actual report payload.
 * @warning The aligned 512-byte row path bypasses the word path's region/config
 *          guards. This function is not a complete flash-range validator; do not
 *          expose it to unchecked records or interpret zero as full acceptance.
 */
UINT mcuWriteFlashRecord(void * RecordData)
{
    T_FLASH_RECORD lFR;
    void* ProgAddress;
    void* ProgAddressEnd;
    UINT32 WrData;
    UINT Result = 0;

    lFR.Address = ((UINT32 *)RecordData)[0];
    lFR.RecDataLen = ((UINT32 *)RecordData)[1];
    lFR.Data = (UINT8*)&((UINT32 *)RecordData)[2];

    while(lFR.RecDataLen)
    {
        ProgAddress = PA_TO_KVA0(lFR.Address);
        ProgAddressEnd = &((char*)ProgAddress)[lFR.RecDataLen];
        if((lFR.RecDataLen>=512) && ((lFR.Address & 511L)==0))
        {
            Result=NVMWriteRow(ProgAddress,lFR.Data);
            lFR.Address+=512;
            lFR.RecDataLen-=512;
            lFR.Data+=512;
        }
        else
        {
            if((((ProgAddress >= (void *)APP_FLASH_BASE_ADDRESS) && (ProgAddressEnd <= (void *)APP_FLASH_END_ADDRESS)) ||
                ((ProgAddress >= (void *)APP_IVT_BASE_ADDRESS) && (ProgAddressEnd <= (void *)APP_IVT_END_ADDRESS))) &&
               ((ProgAddressEnd <= (void*)DEV_CONFIG_REG_BASE_ADDRESS) || (ProgAddress > (void*)DEV_CONFIG_REG_END_ADDRESS)))
            {
                WrData = 0xFFFFFFFF;
                memcpy(&WrData,lFR.Data,min(4,lFR.RecDataLen));
                Result = NVMWriteWord(ProgAddress, WrData);
            }
            lFR.Address += 4;
            lFR.Data += 4;
            if(lFR.RecDataLen > 3)
            {
                lFR.RecDataLen -= 4;
            }
            else
            {
                lFR.RecDataLen = 0;
            }
        }
        if(Result)break;
    }
    return Result;
}

/**
 * @brief Compute the application CRC and attempt to store its validity word.
 * @return Computed CRC-16 (seed 1234), widened to UINT32; not a write-status code.
 * @pre All intended records have been written and verified before this request.
 * @note CRC excludes the final four-byte validity word and uses uncached reads.
 *       NVMWriteWord() status is ignored here; the caller cannot infer persistence
 *       from the returned CRC alone. The separate application IVT is not covered.
 */
UINT32 mcuProgramComplete(){
    UINT32 pCRC;
    pCRC = CalculateCRC(1234, (void *)KVA0_TO_KVA1(APP_FLASH_BASE_ADDRESS), (APP_FLASH_END_ADDRESS - APP_FLASH_BASE_ADDRESS) - 3);
    NVMWriteWord((void *)(APP_FLASH_END_ADDRESS - 3), pCRC);
    return pCRC;
}

/**
 * @brief Compare the application's computed CRC against its stored validity word.
 * @return 1 on equality, otherwise 0.
 * @note Uses seed 1234 and excludes the last four flash bytes. Validates only
 *       that region's CRC; does not authenticate code or validate the separate IVT.
 */
UINT mcuValidAppPresent()
{
    UINT32 pCRC;
    pCRC = CalculateCRC(1234, (void *)KVA0_TO_KVA1(APP_FLASH_BASE_ADDRESS), (APP_FLASH_END_ADDRESS - APP_FLASH_BASE_ADDRESS) - 3);
    if(pCRC == (*((UINT32 *)KVA0_TO_KVA1(APP_FLASH_END_ADDRESS - 3))))return 1;
    return 0;
}

/**
 * @brief Busy-wait for the bootloader OLED's SPI channel to become idle.
 * @pre SPI configured; no timeout or peripheral-error recovery is provided.
 */
void mcuSPIWait(){
    while(mcuSPIIsBusy());
}


#undef _PIC32MX564F128H_C
