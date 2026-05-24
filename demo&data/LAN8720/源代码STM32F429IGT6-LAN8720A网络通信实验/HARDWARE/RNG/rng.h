#ifndef __RNG_H
#define __RNG_H	 
#include "sys.h" 
//////////////////////////////////////////////////////////////////////////////////	 
//本程序只供学习使用，未经作者许可，不得用于其它任何用途
//YTCE STM32开发板
//RNG(随机数发生器)驱动代码	
//原野数码电子@YTCE
//技术论坛:s8088.taobao.com 
//Copyright(C) 原野数码电子  							  
////////////////////////////////////////////////////////////////////////////////// 	 

	
u8 RNG_Init(void);			//RNG初始化 
u32 RNG_Get_RandomNum(void);//得到随机数
int RNG_Get_RandomRange(int min,int max);//得到属于某个范围内的随机数
#endif

















