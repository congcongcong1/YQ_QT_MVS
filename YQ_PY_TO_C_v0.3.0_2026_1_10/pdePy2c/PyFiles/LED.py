# -*- coding: utf-8 -*-
# LED.py (适配新版转译器规则)
#include "stm32f10x.h"
# ========== 核心辅助函数 ==========

# 1. 模拟 C 的取地址操作 (&var)
# 转译器会将其翻译为: &x
def byref(x):
    return x

# 2. 外部函数标记装饰器
# 转译器检测到 @extern 后，将跳过生成该函数的实体代码
# 仅将其视为已存在的外部函数 (extern)
def extern(func):
    return func

# ========== 宏定义 (模拟 C 的 #define) ==========
# 转译器规则：检测到全大写全局变量赋值，且值为 int/hex，
# 将视为宏定义，不在 .c 中生成 "int VAR = ..."，
# 而是假设头文件中已有定义。

ENABLE = 1
DISABLE = 0

# GPIO 模式
GPIO_Mode_Out_OD = 0x14
GPIO_Mode_Out_PP = 0x10
GPIO_Speed_50MHz = 0x03

# GPIO 引脚定义 (位掩码)
GPIO_Pin_0 = 0x0001
GPIO_Pin_1 = 0x0002
GPIO_Pin_2 = 0x0004
GPIO_Pin_8 = 0x0100
GPIO_Pin_9 = 0x0200

# 外设时钟掩码
RCC_APB2Periph_GPIOA = 0x00000004
RCC_APB2Periph_GPIOB = 0x00000008

# 端口基地址
GPIOA = 0x40010800
GPIOB = 0x40010C00

# ========== 结构体模拟 ==========
class GPIO_InitTypeDef:
    def __init__(self):
        # 对应 C 结构体的字段
        self.GPIO_Pin = 0
        self.GPIO_Speed = 0
        self.GPIO_Mode = 0

# ========== 库函数桩 (Stub) ==========
# 使用 @extern 标记，告诉转译器：
# "这是外部库函数，不要给我生成空的 void func() {}，我要用库里的原版！"

@extern
def RCC_APB2PeriphClockCmd(RCC_APB2Periph, NewState):
    pass

@extern
def GPIO_Init(GPIOx, GPIO_InitStruct):
    pass

@extern
def GPIO_SetBits(GPIOx, GPIO_Pin):
    pass

@extern
def GPIO_ResetBits(GPIOx, GPIO_Pin):
    pass

@extern
def GPIO_WriteBit(GPIOx, GPIO_Pin, BitVal):
    pass

@extern
def GPIO_ReadOutputDataBit(GPIOx, GPIO_Pin):
    return 0

# ========== 接口实现 ==========

def LED_Init():
    """LED初始化"""
    # 开启时钟
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE)
    
    # GPIO初始化
    GPIO_InitStructure = GPIO_InitTypeDef()
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_Out_PP
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_1 | GPIO_Pin_2
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz
    
    # 关键修改：使用 byref 传递结构体地址
    # Python: byref(struct) -> C: &struct
    GPIO_Init(GPIOA, byref(GPIO_InitStructure))
    
    # 设置默认电平
    GPIO_SetBits(GPIOA, GPIO_Pin_1 | GPIO_Pin_2)

def LED1_ON():
    GPIO_ResetBits(GPIOA, GPIO_Pin_1)

def LED1_OFF():
    GPIO_SetBits(GPIOA, GPIO_Pin_1)

def LED1_Turn():
    if GPIO_ReadOutputDataBit(GPIOA, GPIO_Pin_1) == 0:
        GPIO_SetBits(GPIOA, GPIO_Pin_1)
    else:
        GPIO_ResetBits(GPIOA, GPIO_Pin_1)

def LED2_ON():
    GPIO_ResetBits(GPIOA, GPIO_Pin_2)

def LED2_OFF():
    GPIO_SetBits(GPIOA, GPIO_Pin_2)

def LED2_Turn():
    if GPIO_ReadOutputDataBit(GPIOA, GPIO_Pin_2) == 0:
        GPIO_SetBits(GPIOA, GPIO_Pin_2)
    else:
        GPIO_ResetBits(GPIOA, GPIO_Pin_2)