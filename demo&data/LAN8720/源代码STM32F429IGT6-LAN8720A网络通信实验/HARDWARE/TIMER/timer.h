#ifndef __TIMER_H
#define __TIMER_H
#include "sys.h"
//////////////////////////////////////////////////////////////////////////////////	 
//本程序只供学习使用，未经作者许可，不得用于其它任何用途
//YTCE STM32开发板
//定时器 驱动代码	   
//原野数码电子@YTCE
//技术论坛:s8088.taobao.com 
////////////////////////////////////////////////////////////////////////////////// 	 
			 
//通过改变TIM3->CCR4的值来改变占空比，从而控制LED0的亮度
#define LED0_PWM_VAL TIM3->CCR4    
//TIM9 CH2作为PWM DAC的输出通道 
#define PWM_DAC_VAL  TIM9->CCR2  

void TIM3_Int_Init(u16 arr,u16 psc);
void TIM3_PWM_Init(u32 arr,u32 psc);
void TIM5_CH1_Cap_Init(u32 arr,u16 psc);
void TIM9_CH2_PWM_Init(u16 arr,u16 psc);
void TIM6_Int_Init(u16 arr,u16 psc);		
#endif























