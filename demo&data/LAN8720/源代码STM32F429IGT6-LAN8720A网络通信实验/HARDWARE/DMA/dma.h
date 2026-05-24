#ifndef __DMA_H
#define	__DMA_H	   
#include "sys.h"
//////////////////////////////////////////////////////////////////////////////////	 
//本程序只供学习使用，未经作者许可，不得用于其它任何用途
//YTCE STM32开发板
//DMA 驱动代码	   
//原野数码电子@YTCE
//技术论坛:s8088.taobao.com 
//Copyright(C) 原野数码电子  							  
////////////////////////////////////////////////////////////////////////////////// 	 
 

void MYDMA_Config(DMA_Stream_TypeDef *DMA_Streamx,u8 chx,u32 par,u32 mar,u16 ndtr);//配置DMAx_CHx
void MYDMA_Enable(DMA_Stream_TypeDef *DMA_Streamx,u16 ndtr);	//使能一次DMA传输		   
#endif






























