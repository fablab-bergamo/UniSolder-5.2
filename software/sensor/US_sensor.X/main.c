/* 
 * File:   main.c
 * Author: Sparky
 *
 * Created on ??????, 2013, ?????? 26, 11:06
 */

#include "config.h"
//#include <stdio.h>
//#include <stdlib.h>
#include <xc.h>
#include <GenericTypeDefs.h>


#define _XTAL_FREQ 8000000
#define TX GP1
#define RX GP4
#define SLEEPOUT GP5

static UINT32_VAL pwd;


/**
 * @brief Run the PIC12F615 holder sensor's carrier and challenge/echo loop.
 *
 * Configures Timer2/CCP carrier PWM, transmits the four-byte "WaKe" pattern
 * least-significant bit first, and shifts RX samples into the same 32-bit word.
 * Uses a 6 ms leader, two 600 us phases per bit, and a 100 ms inter-frame delay.
 * A matching echo asserts SLEEPOUT immediately; eight consecutive nonmatching
 * frames deassert it. The first seven misses preserve the previous output.
 * @note Standalone firmware entry point; blocking timing assumes the configured
 *       8 MHz oscillator and never returns. No interrupts service this protocol.
 */
void main(void) {
    UINT32_VAL RXTX;
    UINT8 i;
    UINT8 inCnt;
    TRISA=0b00011001;
    PORTA=0b00100000;
    ANSEL=0;
    WPU=0;
    T2CON=0b00000100;   //Timer 2 on
    PR2=52;//59;             //40Khz
    CCP1CON=0b00001111; //PWM mode, P1A single output, P1A active low
    CCPR1L=24;//29;          //50% duty
    pwd.v[0]='W';
    pwd.v[1]='a';
    pwd.v[2]='K';
    pwd.v[3]='e';
    inCnt=0;
    while(1){
        TX=0;
        RXTX.Val=pwd.Val;
        __delay_ms(6); //transmit leader
        for(i=0;i<32;i++){
            TX=0;
            __delay_us(600);
            TX=RXTX.bits.b0;
            RXTX.Val>>=1;
            __delay_us(600);
            RXTX.bits.b31=RX;
        }
        TX=1;
        __delay_ms(100);
        if(RXTX.Val==pwd.Val){
            SLEEPOUT=1;
            inCnt=0;
        }
        else
        {
            if(inCnt<255)inCnt++;
            if(inCnt>7){
                SLEEPOUT=0;
            }
        }
    }
}

