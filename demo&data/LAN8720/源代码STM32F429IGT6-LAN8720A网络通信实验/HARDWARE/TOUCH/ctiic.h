#ifndef __MYCT_IIC_H
#define __MYCT_IIC_H
#include "sys.h"	    
//////////////////////////////////////////////////////////////////////////////////	 
//本程序只供学习使用，未经作者许可，不得用于其它任何用途
//YTCE STM32开发板
//电容触摸屏-IIC 驱动代码	   
//原野数码电子@YTCE
//技术论坛:s8088.taobao.com 
////////////////////////////////////////////////////////////////////////////////// 	

//IO方向设置
#define CT_SDA_IN()  {GPIOI->MODER&=~(3<<(2*3));GPIOI->MODER|=0<<2*3;}	//PF11输入模式
#define CT_SDA_OUT() {GPIOI->MODER&=~(3<<(2*3));GPIOI->MODER|=1<<2*3;} 	//PF11输出模式
//IO操作函数	 
#define CT_IIC_SCL    PHout(6) 	//SCL
#define CT_IIC_SDA    PIout(3) 	//SDA	 
#define CT_READ_SDA   PIin(3)  	//输入SDA 
 

//IIC所有操作函数
void CT_IIC_Init(void);                	//初始化IIC的IO口				 
void CT_IIC_Start(void);				//发送IIC开始信号
void CT_IIC_Stop(void);	  				//发送IIC停止信号
void CT_IIC_Send_Byte(u8 txd);			//IIC发送一个字节
u8 CT_IIC_Read_Byte(unsigned char ack);	//IIC读取一个字节
u8 CT_IIC_Wait_Ack(void); 				//IIC等待ACK信号
void CT_IIC_Ack(void);					//IIC发送ACK信号
void CT_IIC_NAck(void);					//IIC不发送ACK信号

#endif







