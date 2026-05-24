#ifndef __ADC_H
#define __ADC_H	
#include "sys.h" 
//////////////////////////////////////////////////////////////////////////////////	 
//本程序只供学习使用，未经作者许可，不得用于其它任何用途
//YTCE STM32开发板
//ADC 驱动代码	   
//YTCE@YTCE
//技术论坛:s8088.taobao.com 
//Copyright(C) 原野数码电子  
////////////////////////////////////////////////////////////////////////////////// 
 
#define ADC_CH5  		5 		 	//通道5	   	      	    
#define ADC_CH_TEMP  	18 		 	//通道18,内部温度传感器专用通道	   	    
	   									   
void Adc_Init(void); 				//ADC初始化
u16  Get_Adc(u8 ch); 				//获得某个通道值 
u16 Get_Adc_Average(u8 ch,u8 times);//得到某个通道给定次数采样的平均值  
short Get_Temprate(void);			//读取内部温度传感器值 
#endif 















