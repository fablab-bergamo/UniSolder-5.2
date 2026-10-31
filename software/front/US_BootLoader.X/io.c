#define _IO_C

#include <xc.h>
#include "typedefs.h"
#include "mcu.h"
#include "io.h"
#include "crc.h"
#include "usb/usb.h"
#include "usb/usb_driver.h"
#include "usb/usb_function_hid.h"

#define IO_IDLE 0
#define IO_BUSY 1

static UINT8 IO_STATUS;

void ProcessIO();

/**
 * @brief Initialize recovery HID state and arm the first 64-byte OUT report.
 * @pre MCU setup complete; call once before polling IOTasks().
 */
void IOInit(){
    USBDriverInit();
    IO_STATUS=IO_IDLE;
    RXP.Command=0;
    USBOutHandle = HIDRxPacket(HID_EP, (BYTE *)&RXP, 64);
}

/**
 * @brief Poll USB events and service recovery commands when configured/awake.
 * @note Foreground only; call regularly to maintain enumeration and HID traffic.
 */
void IOTasks(){
    USBDeviceTasks();
    if((USBDeviceState < CONFIGURED_STATE)||(USBSuspendControl==1)) return;
    ProcessIO();   // This is where all the actual bootloader related data transfer/self programming takes place
}

/**
 * @brief Execute one received bootloader command when the IN endpoint is free.
 *
 * Realigns the one-byte wire command's payload for 32-bit field access, processes
 * erase/program/CRC/reset requests, and rearms OUT after any response is queued.
 * Flash-changing commands require the fixed 0x43211234 key; this is an accidental
 * operation guard, not authentication. Reset commands do not send a reply.
 * @pre Configured USB, exclusive RXP/TXP ownership, and a well-formed report.
 * @warning Payload lengths and arbitrary CRC-read addresses are not validated
 *          here. The host must not treat the key as a memory-access boundary.
 */
void ProcessIO(){
    if(IO_STATUS == IO_IDLE){
        if(!HIDRxHandleBusy(USBOutHandle)){
            IO_STATUS = IO_BUSY;
        }
    }
    if(IO_STATUS == IO_BUSY){
        if(!HIDTxHandleBusy(USBInHandle)){
            UINT8 Secured;
            /*memory move necessary because the command is on 1 byte, could be avoided by using 4 bytes*/
            memmove((void*)(&RXP.RawData[4]), (void*)(&RXP.RawData[1]), 63);
            Secured = (RXP.Secure.Key == 0x43211234);
            TXP.Command = RXP.Command;
            TXP.Data[0] = 0xFF;
            switch(RXP.Command){
                case DEV_GET_INFO:
                    TXP.DevInfo.DevID = DEVICE_ID;
                    TXP.DevInfo.VerMin = DEVICE_MINOR_VERSION;
                    TXP.DevInfo.VerMaj = DEVICE_MAJOR_VERSION;
                    break;
                case DEV_GET_OPERATING_MODE:
                    TXP.Data[0] = 0x10;
                    break;
                case BL_GET_INFO:
                    TXP.Data[0] = BOOTLOADER_MINOR_VERSION;
                    TXP.Data[1] = BOOTLOADER_MAJOR_VERSION;
                    break;
                case BL_ERASE_FLASH:
                    if(Secured) TXP.Data[0] = mcuEraseFlash();
                    break;
                case BL_PROGRAM_FLASH:
                    if(Secured) TXP.Data[0] = mcuWriteFlashRecord((void*)RXP.Secure.Data);
                    break;
                case BL_PROGRAM_COMPLETE:
                    if(Secured) TXP.Data32[0] = mcuProgramComplete();
                    break;
                case BL_READ_CRC:
                    TXP.ProgCRC.CRC = CalculateCRC(0, (void *)RXP.ProgCRC.Addr, RXP.ProgCRC.Len);
                    break;
                case DEV_RESET:
                case BL_JUMP_TO_APP:
                    TXP.Command = 0;
                    mcuDisableInterrupts();
                    mcuDisableUSB();
                    if(RXP.Command == DEV_RESET){
                        mcuReset();
                    }
                    else{
                        mcuReset();
                        //mcuJumpToApp();
                    }
                    break;
                default:
                    TXP.Command = 0;
                    break;
            }
            RXP.Command = 0;
            if(TXP.Command != 0){
                memmove((void*)&TXP.RawData[1], (void*)&TXP.RawData[4], 63);
                USBInHandle = HIDTxPacket(HID_EP, (BYTE *)&TXP, 64);
            }
            IO_STATUS=IO_IDLE;
            USBOutHandle = HIDRxPacket(HID_EP, (BYTE *)&RXP, 64);
        }
    }
}

#undef _IO_C
