#ifndef __WKUP_H
#define __WKUP_H	 
#include "sys.h"
//////////////////////////////////////////////////////////////////////////////////	 
//本程序只供学习使用，未经作者许可，不得用于其它任何用途
//YTCE STM32F407开发板
//待机唤醒 代码	   
//原野数码电子@YTCE
//技术论坛:s8088.taobao.com 
//Copyright(C) 原野数码电子  						  
//////////////////////////////////////////////////////////////////////////////////
					    
#define WKUP_KD PAin(0)			//PA0 检测是否外部WK_UP按键按下
	 
u8 Check_WKUP(void);  			//检测WKUP脚的信号
void WKUP_Init(void); 			//PA0 WKUP唤醒初始化
void Sys_Enter_Standby(void);	//系统进入待机模式
#endif


