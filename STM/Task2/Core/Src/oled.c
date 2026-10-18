/*
 * oled.c
 *
 * SSD1306 128x64 OLED driver: bit-banged SPI (pins in oled.h), a frame buffer
 * in OLED_GRAM and text in the 12x6 and 16x8 fonts from oledfont.h. Drawing
 * only changes OLED_GRAM; OLED_Refresh_Gram() sends it to the screen.
 */
#include <oled.h>
#include "stdlib.h"
#include <oledfont.h>

uint8_t OLED_GRAM[128][8]; // [column][page], 8 rows per page

/* Sends the whole frame buffer to the screen, one page (8 rows) at a time. */
void OLED_Refresh_Gram(void)
{
    uint8_t i,n;
    for(i=0;i<8;i++)
    {
        OLED_WR_Byte (0xb0+i,OLED_CMD);  // page address i
        OLED_WR_Byte (0x00,OLED_CMD);    // column start, low nibble 0
        OLED_WR_Byte (0x10,OLED_CMD);    // column start, high nibble 0
        for(n=0;n<128;n++)OLED_WR_Byte(OLED_GRAM[n][i],OLED_DATA);
    }
}

/* Clocks out one byte, MSB first. cmd (OLED_CMD or OLED_DATA) sets the DC pin. */
void OLED_WR_Byte(uint8_t dat,uint8_t cmd)
{
    uint8_t i;
    if(cmd)
        OLED_RS_Set();
    else
        OLED_RS_Clr();
    for(i=0;i<8;i++)
    {
        OLED_SCLK_Clr();
        if(dat&0x80)
            OLED_SDIN_Set();
        else
            OLED_SDIN_Clr();
        OLED_SCLK_Set();
        dat<<=1;
    }
    OLED_RS_Set();
}

/* Clears the frame buffer and the screen. */
void OLED_Clear(void)
{
    uint8_t i,n;
    for(i=0;i<8;i++)for(n=0;n<128;n++)OLED_GRAM[n][i]=0X00;
    OLED_Refresh_Gram();
}

/* Turns the display on (charge pump on, then display on). */
void OLED_Display_On(void)
{
    OLED_WR_Byte(0X8D,OLED_CMD);  // Charge pump setting
    OLED_WR_Byte(0X14,OLED_CMD);  // Charge pump on
    OLED_WR_Byte(0XAF,OLED_CMD);  // Display on
}

/* Turns the display off (charge pump off, then display off). */
void OLED_Display_Off(void)
{
    OLED_WR_Byte(0X8D,OLED_CMD);  // Charge pump setting
    OLED_WR_Byte(0X10,OLED_CMD);  // Charge pump off
    OLED_WR_Byte(0XAE,OLED_CMD);  // Display off
}

/* Sets (t = 1) or clears (t = 0) one pixel in the frame buffer. */
void OLED_DrawPoint(uint8_t x,uint8_t y,uint8_t t)
{
    uint8_t pos,bx,temp=0;
    if(x>127||y>63)return; // off screen
    pos=7-y/8;
    bx=y%8;
    temp=1<<(7-bx);
    if(t)OLED_GRAM[x][pos]|=temp;
    else OLED_GRAM[x][pos]&=~temp;
}

/* Draws one character with its top-left corner at (x, y). size 12 uses the
 * 12x6 font, any other size the 16x8 font. mode 1 = normal, 0 = inverted.
 * chr must be in ' '..'~' (the font tables have no other characters). */
void OLED_ShowChar(uint8_t x,uint8_t y,uint8_t chr,uint8_t size,uint8_t mode)
{
    uint8_t temp,t,t1;
    uint8_t y0=y;
    chr=chr-' ';
    for(t=0;t<size;t++)
    {
        if(size==12)temp=oled_asc2_1206[chr][t];   // 12x6 font
        else temp=oled_asc2_1608[chr][t];          // 16x8 font
        for(t1=0;t1<8;t1++)
        {
            if(temp&0x80)OLED_DrawPoint(x,y,mode);
            else OLED_DrawPoint(x,y,!mode);
            temp<<=1;
            y++;
            if((y-y0)==size)
            {
                y=y0;
                x++;
                break;
            }
        }
    }
}

/* m to the power n. */
uint32_t oled_pow(uint8_t m,uint8_t n)
{
    uint32_t result=1;
    while(n--)result*=m;
    return result;
}

/* Shows num as len digits, leading zeros as spaces (the last digit is always
 * shown). */
void OLED_ShowNumber(uint8_t x,uint8_t y,uint32_t num,uint8_t len,uint8_t size)
{
    uint8_t t,temp;
    uint8_t enshow=0;
    for(t=0;t<len;t++)
    {
        temp=(num/oled_pow(10,len-t-1))%10;
        if(enshow==0&&t<(len-1))
        {
            if(temp==0)
            {
                OLED_ShowChar(x+(size/2)*t,y,' ',size,1);
                continue;
            }else enshow=1;

        }
        OLED_ShowChar(x+(size/2)*t,y,temp+'0',size,1);
    }
}

/* Shows a string in the 12x6 font, 8 pixels per character. Past the right
 * edge it wraps 16 pixels down; past the bottom it clears the screen and
 * carries on from the top left. */
void OLED_ShowString(uint8_t x,uint8_t y,const uint8_t *p)
{
#define MAX_CHAR_POSX 122
#define MAX_CHAR_POSY 58
    while(*p!='\0')
    {
        if(x>MAX_CHAR_POSX){x=0;y+=16;}
        if(y>MAX_CHAR_POSY){y=x=0;OLED_Clear();}
        OLED_ShowChar(x,y,*p,12,1);
        x+=8;
        p++;
    }
}

/* Resets the panel, sends the SSD1306 set-up commands and clears the screen. */
void OLED_Init(void)
{
    // Switch the LSE oscillator off so PC14/PC15 can be used as IO. The OLED
    // pins are on GPIOD, so the OLED itself does not need this.
    HAL_PWR_EnableBkUpAccess();   // allow access to the backup domain
    __HAL_RCC_LSE_CONFIG(RCC_LSE_OFF);
    HAL_PWR_DisableBkUpAccess();

    OLED_RST_Clr();
    HAL_Delay(100);
    OLED_RST_Set();

    OLED_WR_Byte(0xAE,OLED_CMD);  // Display off

    OLED_WR_Byte(0xD5,OLED_CMD);  // Display clock divide ratio / oscillator frequency
    OLED_WR_Byte(80,OLED_CMD);    //   80 = 0x50: divide ratio 1, oscillator 5 (reset value is 0x80)
    OLED_WR_Byte(0xA8,OLED_CMD);  // Multiplex ratio
    OLED_WR_Byte(0X3F,OLED_CMD);  //   0x3F: 1/64 duty
    OLED_WR_Byte(0xD3,OLED_CMD);  // Display offset
    OLED_WR_Byte(0X00,OLED_CMD);  //   0

    OLED_WR_Byte(0x40,OLED_CMD);  // Display start line 0

    OLED_WR_Byte(0x8D,OLED_CMD);  // Charge pump setting
    OLED_WR_Byte(0x14,OLED_CMD);  //   on
    OLED_WR_Byte(0x20,OLED_CMD);  // Memory addressing mode
    OLED_WR_Byte(0x02,OLED_CMD);  //   page addressing (reset value)
    OLED_WR_Byte(0xA1,OLED_CMD);  // Segment remap: column 127 -> SEG0
    OLED_WR_Byte(0xC0,OLED_CMD);  // COM scan direction: normal, COM0 to COM63
    OLED_WR_Byte(0xDA,OLED_CMD);  // COM pins hardware configuration
    OLED_WR_Byte(0x12,OLED_CMD);  //   alternative COM pins, no left/right remap

    OLED_WR_Byte(0x81,OLED_CMD);  // Contrast
    OLED_WR_Byte(0xEF,OLED_CMD);  //   0xEF of 0..255 (reset value 0x7F)
    OLED_WR_Byte(0xD9,OLED_CMD);  // Pre-charge period
    OLED_WR_Byte(0xf1,OLED_CMD);  //   phase 1 = 1, phase 2 = 15 DCLKs
    OLED_WR_Byte(0xDB,OLED_CMD);  // VCOMH deselect level
    OLED_WR_Byte(0x30,OLED_CMD);  //   [6:4] = 011: 0.83 x Vcc

    OLED_WR_Byte(0xA4,OLED_CMD);  // Display follows the RAM contents
    OLED_WR_Byte(0xA6,OLED_CMD);  // Normal (not inverted) display
    OLED_WR_Byte(0xAF,OLED_CMD);  // Display on
    OLED_Clear();
}
