#ifndef __AP3216C_H
#define __AP3216C_H
#include "sys.h"
//////////////////////////////////////////////////////////////////////////////////	 
//本程序只供学习使用，未经作者许可，不得用于其它任何用途
//YTCE STM32开发板
//AP3216C驱动代码	   
//原野数码电子@YTCE
//技术论坛:s8088.taobao.com 							  
////////////////////////////////////////////////////////////////////////////////// 	

#define AP3216C_ADDR    0X3C	//AP3216C器件IIC地址(左移了一位)


u8 AP3216C_Init(void); 
u8 AP3216C_WriteOneByte(u8 reg,u8 data);
u8 AP3216C_ReadOneByte(u8 reg);
void AP3216C_ReadData(u16* ir,u16* ps,u16* als);
#endif
