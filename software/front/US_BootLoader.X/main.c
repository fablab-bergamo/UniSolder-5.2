/* 
 * File:   main.c
 * Author: Sparky
 * Compiler: Microchip XC8 1.12
 *
 * Created on ??????, 2013, ?????? 5, 3:15
 */
#define _MAIN_C

#include "mcu.h"
#include "usb/usb.h"
#include "usb/usb_function_hid.h"
#include "io.h"
#include "OLED.h"

void displayTasts(void);

/**
 * @brief Select a validated application or enter the polled USB recovery loop.
 *
 * Starts the application only if its stored CRC matches and NVMDATA does not
 * contain the application's bootloader-request magic. A pressed B2 requires
 * release before the core-timer threshold to remain here; holding it through
 * the threshold still launches a valid application. Clears the request magic
 * before initializing the recovery display and HID command service.
 * @note Firmware entry point; normal execution never returns.
 */
int main(void){
    
    mcuInit();
    
    if(mcuValidAppPresent() && NVMDATA != 0x6193471A){
        //B2 is button 2 and has pull down resistor.
        //If button is pressed stay in bootloader.
        if(!B2){
            mcuJumpToApp();
        }
        else{
            WriteCoreTimer(0);
            while(B2 && ReadCoreTimer() < 80000000){}
            if(B2 || ReadCoreTimer() >= 80000000) mcuJumpToApp();
        }
    }
    
    NVMDATA = 0xFFFFFFFF;
    OLEDInit();
    IOInit();

    while(1){
        displayTasts();
        IOTasks();
    }
}

/**
 * @brief Animate two OLED pages once per 4096 recovery-loop calls.
 * @note Cadence depends on polling/USB workload, not elapsed time. Modifies
 *       OLEDBUFF pages 3/4 and sends the full framebuffer; foreground only.
 */
void displayTasts(void){
    volatile static int CT;
    volatile static int CTT;
    int i;
    CT++;
    if((CT & 4095) == 4095){
        CTT++;
        i=128;
        while(i--){
            OLEDBUFF.B[3][i]=((CTT+i) & 8)?0xFF:0;
            OLEDBUFF.B[4][i]=~OLEDBUFF.B[3][i];
        }
        OLEDUpdate();
    }
}

#undef _MAIN_C
