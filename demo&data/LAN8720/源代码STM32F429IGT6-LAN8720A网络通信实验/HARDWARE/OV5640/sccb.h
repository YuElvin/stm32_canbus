#ifndef __SCCB_H
#define __SCCB_H
#include "sys.h"
//////////////////////////////////////////////////////////////////////////////////	 
//本程序只供学习使用，未经作者许可，不得用于其它任何用途
//YTCE STM32开发板
//OV系列摄像头 SCCB 驱动代码	   
//原野数码电子@YTCE
//技术论坛:s8088.taobao.com 							  
////////////////////////////////////////////////////////////////////////////////// 


//IO方向设置
#define SCCB_SDA_IN()  {GPIOB->MODER&=~(3<<(3*2));GPIOB->MODER|=0<<3*2;}	//PB3 输入
#define SCCB_SDA_OUT() {GPIOB->MODER&=~(3<<(3*2));GPIOB->MODER|=1<<3*2;} 	//PB3 输出


//IO操作函数	 
#define SCCB_SCL    		PBout(4)	 	//SCL
#define SCCB_SDA    		PBout(3) 		//SDA	 

#define SCCB_READ_SDA    	PBin(3)  		//输入SDA     

///////////////////////////////////////////
void SCCB_Init(void);
void SCCB_Start(void);
void SCCB_Stop(void);
void SCCB_No_Ack(void);
u8 SCCB_WR_Byte(u8 dat);
u8 SCCB_RD_Byte(void); 
#endif













