#include "led.h" 
//////////////////////////////////////////////////////////////////////////////////	 
//本程序只供学习使用，未经作者许可，不得用于其它任何用途
//YTCE STM32开发板
//LED驱动代码	   
//原野数码电子@YTCE
//技术论坛:s8088.taobao.com 			  
////////////////////////////////////////////////////////////////////////////////// 	 

//初始化PB0和PB1为输出口.并使能这两个口的时钟		    
//LED IO初始化
void LED_Init(void)
{    	 
	RCC->AHB1ENR|=1<<1;//使能PORTB时钟 
	GPIO_Set(GPIOB,PIN0|PIN1,GPIO_MODE_OUT,GPIO_OTYPE_PP,GPIO_SPEED_100M,GPIO_PUPD_PU); //PB0,PB1设置
	LED0=1;//LED0关闭
	LED1=1;//LED1关闭
}






