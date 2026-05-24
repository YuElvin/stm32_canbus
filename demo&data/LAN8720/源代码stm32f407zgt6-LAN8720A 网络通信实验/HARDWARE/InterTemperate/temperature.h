#ifndef _ADC_H
#define _ADC_H
#include "sys.h"
//////////////////////////////////////////////////////////////////////////////////	 
//本程序只供学习使用，未经作者许可，不得用于其它任何用途
//YTCE STM32F407开发板
//ADC 驱动代码	   
//原野数码电子@YTCE
 							  
////////////////////////////////////////////////////////////////////////////////// 	 

#define ADC_CH5  5  //通道5
#define ADC_TEMP 16 //通道16 温度传感器的ADC通道

void Adc_Temperate_Init(void);				//温度传感器初始化
static u16 Get_Adc(u8 ch);
static u16 Get_Adc_Average(u8 ch,u8 times);
short Get_Temprate(void);  //获取温度值
#endif

