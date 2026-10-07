#ifndef __OLED_H
#define __OLED_H			  	 

#include "stm32f4xx_hal.h"
#define u8 uint8_t
#define u16 uint16_t
#define u32 uint32_t

#define OLED_DC_Pin GPIO_PIN_11
#define OLED_RES_Pin GPIO_PIN_12
#define OLED_SDA_Pin GPIO_PIN_13
#define OLED_SCL_Pin GPIO_PIN_14


extern u8 OLED_GRAM[128][8];

//Oled port macro definition
#define OLED_RST_Clr() HAL_GPIO_WritePin(GPIOD,OLED_RES_Pin,GPIO_PIN_RESET)  //RST
#define OLED_RST_Set() HAL_GPIO_WritePin(GPIOD,OLED_RES_Pin,GPIO_PIN_SET)   //RST

#define OLED_RS_Clr()  HAL_GPIO_WritePin(GPIOD,OLED_DC_Pin,GPIO_PIN_RESET)    //DC
#define OLED_RS_Set()  HAL_GPIO_WritePin(GPIOD,OLED_DC_Pin,GPIO_PIN_SET)  //DC

#define OLED_SCLK_Clr()  HAL_GPIO_WritePin(GPIOD,OLED_SCL_Pin,GPIO_PIN_RESET)  //SCL
#define OLED_SCLK_Set()  HAL_GPIO_WritePin(GPIOD,OLED_SCL_Pin,GPIO_PIN_SET)    //SCL

#define OLED_SDIN_Clr()  HAL_GPIO_WritePin(GPIOD,OLED_SDA_Pin,GPIO_PIN_RESET)    //SDA
#define OLED_SDIN_Set()  HAL_GPIO_WritePin(GPIOD,OLED_SDA_Pin,GPIO_PIN_SET)    //SDA

#define OLED_CMD  0	//Command
#define OLED_DATA 1	//Data

#define OLED_Height			64
#define OLED_Width			128

//Oled control function
void OLED_WR_Byte(u8 dat,u8 cmd);	    
void OLED_Display_On(void);
void OLED_Display_Off(void);
void OLED_Refresh_Gram(void);		   				   		    
void OLED_Init(void);
void OLED_Clear(void);
void OLED_DrawPoint(u8 x,u8 y,u8 t);
void OLED_ShowChar(u8 x,u8 y,u8 chr,u8 size,u8 mode);
void OLED_ShowNumber(u8 x,u8 y,u32 num,u8 len,u8 size);
void OLED_ShowString(u8 x,u8 y,const u8 *p);
void OLED_ShowString16(u8 x,u8 y,const u8 *p);
void OLED_Refresh_Line(void);

#endif  
	 
