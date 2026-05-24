#ifndef __SPI_H
#define __SPI_H
#include "sys.h"
//////////////////////////////////////////////////////////////////////////////////	 
//本程序只供学习使用，未经作者许可，不得用于其它任何用途
//YTCE STM32开发板 
//SPI 驱动代码	   
//原野数码电子@YTCE
//技术论坛:s8088.taobao.com 
////////////////////////////////////////////////////////////////////////////////// 	 

				    
// SPI总线速度设置 
#define SPI_SPEED_2   		0
#define SPI_SPEED_4   		1
#define SPI_SPEED_8   		2
#define SPI_SPEED_16  		3
#define SPI_SPEED_32 		4
#define SPI_SPEED_64 		5
#define SPI_SPEED_128 		6
#define SPI_SPEED_256 		7
						  	    													  
void SPI5_Init(void);			 //初始化SPI口
void SPI5_SetSpeed(u8 SpeedSet); //设置SPI速度   
u8 SPI5_ReadWriteByte(u8 TxData);//SPI总线读写一个字节
void SPI2_Init(void);
void SPI2_SetSpeed(u8 SpeedSet);
u8 SPI2_ReadWriteByte(u8 TxData);
#endif

