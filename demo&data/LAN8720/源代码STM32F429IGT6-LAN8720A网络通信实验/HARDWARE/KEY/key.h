#ifndef __KEY_H
#define __KEY_H	 
#include "sys.h" 
//////////////////////////////////////////////////////////////////////////////////	 
//本程序只供学习使用，未经作者许可，不得用于其它任何用途
//YTCE STM32开发板
//按键输入驱动代码	   
//原野数码电子@YTCE
//技术论坛:s8088.taobao.com
//Copyright(C) 原野数码电子 							  
////////////////////////////////////////////////////////////////////////////////// 	 


#define KEY0 		PHin(3)   	//PH3
#define KEY1 		PHin(2)		//PH2 
#define KEY2 		PCin(13)	//PC13
#define WK_UP 		PAin(0)		//PA0 

#define KEY0_PRES 	1	//KEY0按下
#define KEY1_PRES	2	//KEY1按下
#define KEY2_PRES	3	//KEY2按下
#define WKUP_PRES   4	//KEY_UP按下(即WK_UP)

void KEY_Init(void);	//IO初始化
u8 KEY_Scan(u8);  		//按键扫描函数					    
#endif
