#include "stm32f10x.h"
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <math.h>

int ENABLE = 1;
int DISABLE = 0;
uint16_t GPIO_Mode_Out_OD = 0x14;
uint16_t GPIO_Mode_Out_PP = 0x10;
uint16_t GPIO_Speed_50MHz = 0x03;
uint16_t GPIO_Pin_0 = 0x0001;
uint16_t GPIO_Pin_1 = 0x0002;
uint16_t GPIO_Pin_2 = 0x0004;
uint16_t GPIO_Pin_8 = 0x0100;
uint16_t GPIO_Pin_9 = 0x0200;
uint16_t RCC_APB2Periph_GPIOA = 0x00000004;
uint16_t RCC_APB2Periph_GPIOB = 0x00000008;
uint32_t GPIOA = 0x40010800;
uint32_t GPIOB = 0x40010C00;

void LED_Init();
void LED1_ON();
void LED1_OFF();
void LED1_Turn();
void LED2_ON();
void LED2_OFF();
void LED2_Turn();

void LED_Init() {
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE);
    GPIO_InitTypeDef GPIO_InitStructure = (GPIO_InitTypeDef){};
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_Out_PP;
    GPIO_InitStructure.GPIO_Pin = (GPIO_Pin_1 | GPIO_Pin_2);
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOA, &(GPIO_InitStructure));
    GPIO_SetBits(GPIOA, (GPIO_Pin_1 | GPIO_Pin_2));
    return;
}

void LED1_ON() {
    GPIO_ResetBits(GPIOA, GPIO_Pin_1);
    return;
}

void LED1_OFF() {
    GPIO_SetBits(GPIOA, GPIO_Pin_1);
    return;
}

void LED1_Turn() {
    if ((GPIO_ReadOutputDataBit(GPIOA, GPIO_Pin_1) == 0)) {
        GPIO_SetBits(GPIOA, GPIO_Pin_1);
    } else {
        GPIO_ResetBits(GPIOA, GPIO_Pin_1);
    }
    return;
}

void LED2_ON() {
    GPIO_ResetBits(GPIOA, GPIO_Pin_2);
    return;
}

void LED2_OFF() {
    GPIO_SetBits(GPIOA, GPIO_Pin_2);
    return;
}

void LED2_Turn() {
    if ((GPIO_ReadOutputDataBit(GPIOA, GPIO_Pin_2) == 0)) {
        GPIO_SetBits(GPIOA, GPIO_Pin_2);
    } else {
        GPIO_ResetBits(GPIOA, GPIO_Pin_2);
    }
    return;
}


void pyc_LED_pdePycProc() {
    return;
}

int main() {
    pyc_LED_pdePycProc();
    return 0;
}