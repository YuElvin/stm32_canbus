#ifndef __WDG_H
#define __WDG_H
#include "sys.h"
//////////////////////////////////////////////////////////////////////////////////	 
//本程序只供学习使用，未经作者许可，不得用于其它任何用途
//YTCE STM32开发板
//看门狗 驱动代码	   
//原野数码电子@YTCE
//技术论坛:s8088.taobao.com 
//Copyright(C) 原野数码电子 2  
////////////////////////////////////////////////////////////////////////////////// 	 


void IWDG_Init(u8 prer,u16 rlr);
void IWDG_Feed(void);
void WWDG_Init(u8 tr,u8 wr,u8 fprer);
void WWDG_Set_Counter(u8 cnt);
#endif



























