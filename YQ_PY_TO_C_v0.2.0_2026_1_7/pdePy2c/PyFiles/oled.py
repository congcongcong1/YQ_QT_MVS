# -*- coding: utf-8 -*-
# oled.py (优化版：适配 C 语言指针与 ASCII 逻辑)
#include "stm32f10x.h"
# ========== 1. 核心转译辅助函数 ==========
# 必须保留，供转译器识别

# 模拟 C 的取地址 (&var)
def byref(x):
    return x

# 标记外部函数，转译器将跳过生成函数体
def extern(func):
    return func

# ========== 2. STM32 宏定义与类型桩 ==========
# 模拟头文件内容，转译器检测到全大写变量会跳过生成定义

ENABLE = 1
DISABLE = 0
GPIO_Mode_Out_OD = 0x14
GPIO_Speed_50MHz = 0x03
GPIO_Pin_8 = 0x0100
GPIO_Pin_9 = 0x0200
RCC_APB2Periph_GPIOB = 0x00000008
GPIOB = 0x40010C00

class GPIO_InitTypeDef:
    def __init__(self):
        self.GPIO_Pin = 0
        self.GPIO_Speed = 0
        self.GPIO_Mode = 0

# ========== 3. 外部库函数声明 ==========
# 使用 @extern 防止生成空函数体覆盖库函数

@extern
def RCC_APB2PeriphClockCmd(RCC_APB2Periph, NewState):
    pass

@extern
def GPIO_Init(GPIOx, GPIO_InitStruct):
    pass

@extern
def GPIO_WriteBit(GPIOx, GPIO_Pin, BitVal):
    pass

# ========== 4. 字库数据 ==========
# 仅保留部分作为示例，实际开发请补全
OLED_F8x16 = [
    [0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
     0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00], # ' '
    [0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
     0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00], # ! (placeholder)
     # ... 请在此处补充完整字库 ...
]

# ========== 5. 驱动逻辑实现 ==========

def OLED_W_SCL(x):
    GPIO_WriteBit(GPIOB, GPIO_Pin_8, x)

def OLED_W_SDA(x):
    GPIO_WriteBit(GPIOB, GPIO_Pin_9, x)

def OLED_I2C_Init():
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB, ENABLE)
    
    GPIO_InitStructure = GPIO_InitTypeDef()
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_Out_OD
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz
    
    # 修正点：使用 byref 传递地址 (&GPIO_InitStructure)
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_8
    GPIO_Init(GPIOB, byref(GPIO_InitStructure))
    
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_9
    GPIO_Init(GPIOB, byref(GPIO_InitStructure))
    
    OLED_W_SCL(1)
    OLED_W_SDA(1)

def OLED_I2C_Start():
    OLED_W_SDA(1)
    OLED_W_SCL(1)
    OLED_W_SDA(0)
    OLED_W_SCL(0)

def OLED_I2C_Stop():
    OLED_W_SDA(0)
    OLED_W_SCL(1)
    OLED_W_SDA(1)

def OLED_I2C_SendByte(Byte):
    for i in range(8):
        # 修正点：保留位运算逻辑，转译器会正确处理
        bit_val = Byte & (0x80 >> i)
        if bit_val != 0:
            OLED_W_SDA(1)
        else:
            OLED_W_SDA(0)
        OLED_W_SCL(1)
        OLED_W_SCL(0)
    
    OLED_W_SCL(1)
    OLED_W_SCL(0)

def OLED_WriteCommand(Command):
    OLED_I2C_Start()
    OLED_I2C_SendByte(0x78)
    OLED_I2C_SendByte(0x00)
    OLED_I2C_SendByte(Command)
    OLED_I2C_Stop()

def OLED_WriteData(Data):
    OLED_I2C_Start()
    OLED_I2C_SendByte(0x78)
    OLED_I2C_SendByte(0x40)
    OLED_I2C_SendByte(Data)
    OLED_I2C_Stop()

def OLED_SetCursor(Y, X):
    OLED_WriteCommand(0xB0 | Y)
    OLED_WriteCommand(0x10 | ((X & 0xF0) >> 4))
    OLED_WriteCommand(0x00 | (X & 0x0F))

def OLED_Clear():
    for j in range(8):
        OLED_SetCursor(j, 0)
        for i in range(128):
            OLED_WriteData(0x00)

def OLED_ShowChar(Line, Column, Char):
    # 修正点：移除 ord()。
    # 在 C 语言中，Char 本身就是 int 类型 (ASCII码)，直接减 32 即可。
    char_index = Char - 32
    
    # 简单的边界保护
    if char_index < 0: char_index = 0
    # 注意：如果 OLED_F8x16 为空或较短，这里需要根据实际长度调整
    # if char_index >= len(OLED_F8x16): char_index = 0

    OLED_SetCursor((Line - 1) * 2, (Column - 1) * 8)
    for i in range(8):
        OLED_WriteData(OLED_F8x16[char_index][i])
        
    OLED_SetCursor((Line - 1) * 2 + 1, (Column - 1) * 8)
    for i in range(8):
        OLED_WriteData(OLED_F8x16[char_index][i + 8])

def OLED_ShowString(Line, Column, String):
    # String 在 C 中是 char*，可以直接下标访问
    n = len(String)
    for i in range(n):
        OLED_ShowChar(Line, Column + i, String[i])

def OLED_Pow(X, Y):
    Result = 1
    while Y > 0:
        Y = Y - 1
        Result = Result * X
    return Result

def OLED_ShowNum(Line, Column, Number, Length):
    for i in range(Length):
        # 修正点：保留 // (转译器处理为取整除法)
        # 修正点：移除 chr()，直接传递计算出的 ASCII 值 (digit + 48) 给 OLED_ShowChar
        # 在 C 中，'0' 等于 48，所以 digit + 48 就是对应字符的 ASCII
        digit = (Number // OLED_Pow(10, Length - i - 1)) % 10
        OLED_ShowChar(Line, Column + i, digit + 48)

def OLED_ShowSignedNum(Line, Column, Number, Length):
    Number1 = 0
    if Number >= 0:
        OLED_ShowChar(Line, Column, 43) # '+' ASCII is 43
        Number1 = Number
    else:
        OLED_ShowChar(Line, Column, 45) # '-' ASCII is 45
        Number1 = -Number
        
    for i in range(Length):
        digit = (Number1 // OLED_Pow(10, Length - i - 1)) % 10
        OLED_ShowChar(Line, Column + i + 1, digit + 48)

def OLED_ShowHexNum(Line, Column, Number, Length):
    for i in range(Length):
        SingleNumber = (Number // OLED_Pow(16, Length - i - 1)) % 16
        if SingleNumber < 10:
            OLED_ShowChar(Line, Column + i, SingleNumber + 48)
        else:
            OLED_ShowChar(Line, Column + i, SingleNumber - 10 + 65) # 'A' is 65

def OLED_ShowBinNum(Line, Column, Number, Length):
    for i in range(Length):
        digit = (Number // OLED_Pow(2, Length - i - 1)) % 2
        OLED_ShowChar(Line, Column + i, digit + 48)

def OLED_Init():
    # 上电延时
    for i in range(1000):
        for j in range(1000):
            pass 
            
    OLED_I2C_Init()
    
    OLED_WriteCommand(0xAE)
    OLED_WriteCommand(0xD5)
    OLED_WriteCommand(0x80)
    OLED_WriteCommand(0xA8)
    OLED_WriteCommand(0x3F)
    OLED_WriteCommand(0xD3)
    OLED_WriteCommand(0x00)
    OLED_WriteCommand(0x40)
    OLED_WriteCommand(0xA1)
    OLED_WriteCommand(0xC8)
    OLED_WriteCommand(0xDA)
    OLED_WriteCommand(0x12)
    OLED_WriteCommand(0x81)
    OLED_WriteCommand(0xCF)
    OLED_WriteCommand(0xD9)
    OLED_WriteCommand(0xF1)
    OLED_WriteCommand(0xDB)
    OLED_WriteCommand(0x30)
    OLED_WriteCommand(0xA4)
    OLED_WriteCommand(0xA6)
    OLED_WriteCommand(0x8D)
    OLED_WriteCommand(0x14)
    OLED_WriteCommand(0xAF)
    
    OLED_Clear()