#include "stm32f10x.h"
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <math.h>

int OLED_F8x16[] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
int OLED_F8x16_h = 2;
int OLED_F8x16_w = 16;

void OLED_W_SCL(int x);
void OLED_W_SDA(int x);
void OLED_I2C_Init();
void OLED_I2C_Start();
void OLED_I2C_Stop();
void OLED_I2C_SendByte(int Byte);
void OLED_WriteCommand(int Command);
void OLED_WriteData(int Data);
void OLED_SetCursor(int Y, int X);
void OLED_Clear();
void OLED_ShowChar(int Line, int Column, int Char);
void OLED_ShowString(int Line, int Column, int *String, int n);
int OLED_Pow(int X, int Y);
void OLED_ShowNum(int Line, int Column, int Number, int Length);
void OLED_ShowSignedNum(int Line, int Column, int Number, int Length);
void OLED_ShowHexNum(int Line, int Column, int Number, int Length);
void OLED_ShowBinNum(int Line, int Column, int Number, int Length);
void OLED_Init();

void OLED_W_SCL(int x) {
    GPIO_WriteBit(GPIOB, GPIO_Pin_8, x);
    return;
}

void OLED_W_SDA(int x) {
    GPIO_WriteBit(GPIOB, GPIO_Pin_9, x);
    return;
}

void OLED_I2C_Init() {
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB, ENABLE);
    GPIO_InitTypeDef GPIO_InitStructure = (GPIO_InitTypeDef){};
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_Out_OD;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_8;
    GPIO_Init(GPIOB, &(GPIO_InitStructure));
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_9;
    GPIO_Init(GPIOB, &(GPIO_InitStructure));
    OLED_W_SCL(1);
    OLED_W_SDA(1);
    return;
}

void OLED_I2C_Start() {
    OLED_W_SDA(1);
    OLED_W_SCL(1);
    OLED_W_SDA(0);
    OLED_W_SCL(0);
    return;
}

void OLED_I2C_Stop() {
    OLED_W_SDA(0);
    OLED_W_SCL(1);
    OLED_W_SDA(1);
    return;
}

void OLED_I2C_SendByte(int Byte) {
    int i;
    for (i = 0; i < 8; i += 1) {
        int bit_val = (Byte & (0x80 >> i));
        if ((bit_val != 0)) {
            OLED_W_SDA(1);
        } else {
            OLED_W_SDA(0);
        }
        OLED_W_SCL(1);
        OLED_W_SCL(0);
    }
    OLED_W_SCL(1);
    OLED_W_SCL(0);
    return;
}

void OLED_WriteCommand(int Command) {
    OLED_I2C_Start();
    OLED_I2C_SendByte(0x78);
    OLED_I2C_SendByte(0x00);
    OLED_I2C_SendByte(Command);
    OLED_I2C_Stop();
    return;
}

void OLED_WriteData(int Data) {
    OLED_I2C_Start();
    OLED_I2C_SendByte(0x78);
    OLED_I2C_SendByte(0x40);
    OLED_I2C_SendByte(Data);
    OLED_I2C_Stop();
    return;
}

void OLED_SetCursor(int Y, int X) {
    OLED_WriteCommand((0xB0 | Y));
    OLED_WriteCommand((0x10 | ((X & 0xF0) >> 4)));
    OLED_WriteCommand((0x00 | (X & 0x0F)));
    return;
}

void OLED_Clear() {
    int j;
    for (j = 0; j < 8; j += 1) {
        OLED_SetCursor(j, 0);
        int i;
        for (i = 0; i < 128; i += 1) {
            OLED_WriteData(0x00);
        }
    }
    return;
}

void OLED_ShowChar(int Line, int Column, int Char) {
    int char_index = (Char - 32);
    if ((char_index < 0)) {
        char_index = 0;
    }
    OLED_SetCursor(((Line - 1) * 2), ((Column - 1) * 8));
    int i;
    for (i = 0; i < 8; i += 1) {
        OLED_WriteData(OLED_F8x16[(char_index) * (OLED_F8x16_w) + (i)]);
    }
    OLED_SetCursor((((Line - 1) * 2) + 1), ((Column - 1) * 8));
    for (i = 0; i < 8; i += 1) {
        OLED_WriteData(OLED_F8x16[(char_index) * (OLED_F8x16_w) + ((i + 8))]);
    }
    return;
}

void OLED_ShowString(int Line, int Column, int *String, int n) {
    int i;
    for (i = 0; i < n; i += 1) {
        OLED_ShowChar(Line, (Column + i), String[i]);
    }
    return;
}

int OLED_Pow(int X, int Y) {
    int Result = 1;
    while ((Y > 0)) {
        Y = (Y - 1);
        Result = (Result * X);
    }
    return Result;
}

void OLED_ShowNum(int Line, int Column, int Number, int Length) {
    int i;
    for (i = 0; i < Length; i += 1) {
        int digit = (((int)floor((double)(Number) / (double)(OLED_Pow(10, ((Length - i) - 1))))) % 10);
        OLED_ShowChar(Line, (Column + i), (digit + 48));
    }
    return;
}

void OLED_ShowSignedNum(int Line, int Column, int Number, int Length) {
    int Number1 = 0;
    if ((Number >= 0)) {
        OLED_ShowChar(Line, Column, 43);
        Number1 = Number;
    } else {
        OLED_ShowChar(Line, Column, 45);
        Number1 = (-Number);
    }
    int i;
    for (i = 0; i < Length; i += 1) {
        int digit = (((int)floor((double)(Number1) / (double)(OLED_Pow(10, ((Length - i) - 1))))) % 10);
        OLED_ShowChar(Line, ((Column + i) + 1), (digit + 48));
    }
    return;
}

void OLED_ShowHexNum(int Line, int Column, int Number, int Length) {
    int i;
    for (i = 0; i < Length; i += 1) {
        int SingleNumber = (((int)floor((double)(Number) / (double)(OLED_Pow(16, ((Length - i) - 1))))) % 16);
        if ((SingleNumber < 10)) {
            OLED_ShowChar(Line, (Column + i), (SingleNumber + 48));
        } else {
            OLED_ShowChar(Line, (Column + i), ((SingleNumber - 10) + 65));
        }
    }
    return;
}

void OLED_ShowBinNum(int Line, int Column, int Number, int Length) {
    int i;
    for (i = 0; i < Length; i += 1) {
        int digit = (((int)floor((double)(Number) / (double)(OLED_Pow(2, ((Length - i) - 1))))) % 2);
        OLED_ShowChar(Line, (Column + i), (digit + 48));
    }
    return;
}

void OLED_Init() {
    int i;
    for (i = 0; i < 1000; i += 1) {
        int j;
        for (j = 0; j < 1000; j += 1) {
        }
    }
    OLED_I2C_Init();
    OLED_WriteCommand(0xAE);
    OLED_WriteCommand(0xD5);
    OLED_WriteCommand(0x80);
    OLED_WriteCommand(0xA8);
    OLED_WriteCommand(0x3F);
    OLED_WriteCommand(0xD3);
    OLED_WriteCommand(0x00);
    OLED_WriteCommand(0x40);
    OLED_WriteCommand(0xA1);
    OLED_WriteCommand(0xC8);
    OLED_WriteCommand(0xDA);
    OLED_WriteCommand(0x12);
    OLED_WriteCommand(0x81);
    OLED_WriteCommand(0xCF);
    OLED_WriteCommand(0xD9);
    OLED_WriteCommand(0xF1);
    OLED_WriteCommand(0xDB);
    OLED_WriteCommand(0x30);
    OLED_WriteCommand(0xA4);
    OLED_WriteCommand(0xA6);
    OLED_WriteCommand(0x8D);
    OLED_WriteCommand(0x14);
    OLED_WriteCommand(0xAF);
    OLED_Clear();
    return;
}


void pyc_oled_pdePycProc() {
    return;
}

int main() {
    pyc_oled_pdePycProc();
    return 0;
}