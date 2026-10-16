#include <oled.h>
#include "stdlib.h"
#include <oledfont.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "robot_config.h"
#if STUNTS
#include "motion.h"
#endif

uint8_t OLED_GRAM[128][8];
void OLED_Refresh_Gram(void)
{
	uint8_t i,n;
	for(i=0;i<8;i++)
	{
		OLED_WR_Byte (0xb0+i,OLED_CMD);
		OLED_WR_Byte (0x00,OLED_CMD);
		OLED_WR_Byte (0x10,OLED_CMD);
		for(n=0;n<128;n++)OLED_WR_Byte(OLED_GRAM[n][i],OLED_DATA);
	}
}

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

/**************************************************************************
Clear OLED
**************************************************************************/
void OLED_Clear(void)
{
	uint8_t i,n;
	for(i=0;i<8;i++)for(n=0;n<128;n++)OLED_GRAM[n][i]=0X00;
	OLED_Refresh_Gram();//Refresh
}

 /**************************************************************************
Turn On Display
**************************************************************************/
void OLED_Display_On(void)
{
	OLED_WR_Byte(0X8D,OLED_CMD);  //SET DCDC Command
	OLED_WR_Byte(0X14,OLED_CMD);  //DCDC ON
	OLED_WR_Byte(0XAF,OLED_CMD);  //DISPLAY ON
}
/**************************************************************************
Turn Off Display
**************************************************************************/
void OLED_Display_Off(void)
{
	OLED_WR_Byte(0X8D,OLED_CMD);  //SET DCDC Command
	OLED_WR_Byte(0X10,OLED_CMD);  //DCDC OFF
	OLED_WR_Byte(0XAE,OLED_CMD);  //DISPLAY OFF
}

/**************************************************************************
Draw A Point
**************************************************************************/
void OLED_DrawPoint(uint8_t x,uint8_t y,uint8_t t)
{
	uint8_t pos,bx,temp=0;
	if(x>127||y>63)return;//Out of reach
	pos=7-y/8;
	bx=y%8;
	temp=1<<(7-bx);
	if(t)OLED_GRAM[x][pos]|=temp;
	else OLED_GRAM[x][pos]&=~temp;
}
/**************************************************************************
Show Char
**************************************************************************/
void OLED_ShowChar(uint8_t x,uint8_t y,uint8_t chr,uint8_t size,uint8_t mode)
{
	uint8_t temp,t,t1;
	uint8_t y0=y;
	chr=chr-' ';
    for(t=0;t<size;t++)
    {
		if(size==12)temp=oled_asc2_1206[chr][t];  //1206 Size
		else temp=oled_asc2_1608[chr][t];		 //1608 Size
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

uint32_t oled_pow(uint8_t m,uint8_t n)
{
	uint32_t result=1;
	while(n--)result*=m;
	return result;
}

/**************************************************************************
Show Two Number
**************************************************************************/
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
/**************************************************************************
Show The String
**************************************************************************/
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

void OLED_Init(void)
{
	HAL_PWR_EnableBkUpAccess(); //Enable access to the RTC and Backup Register
	__HAL_RCC_LSE_CONFIG(RCC_LSE_OFF); //turn OFF the LSE oscillator, LSERDY flag goes low after 6 LSE oscillator clock cycles.
	                                   //LSE oscillator switch off to let PC13 PC14 PC15 be IO


	HAL_PWR_DisableBkUpAccess();

	OLED_RST_Clr();
	HAL_Delay(100);
	OLED_RST_Set();

	OLED_WR_Byte(0xAE,OLED_CMD); //Off Display

	OLED_WR_Byte(0xD5,OLED_CMD); //Set Oscillator Division
	OLED_WR_Byte(80,OLED_CMD);    //[3:0]: divide ratio of the DCLK, [7:4], set the oscillator frequency. Reset
	OLED_WR_Byte(0xA8,OLED_CMD); //multiplex ratio
	OLED_WR_Byte(0X3F,OLED_CMD); //duty = 0X3F(1/64)
	OLED_WR_Byte(0xD3,OLED_CMD);  //set display offset
	OLED_WR_Byte(0X00,OLED_CMD); //0

	OLED_WR_Byte(0x40,OLED_CMD); //set display start line [5:0]- from 0-63. RESET

	OLED_WR_Byte(0x8D,OLED_CMD); //Set charge pump
	OLED_WR_Byte(0x14,OLED_CMD); //Enable Charge Pump
	OLED_WR_Byte(0x20,OLED_CMD); //Set Memory Addressing Mode
	OLED_WR_Byte(0x02,OLED_CMD); //Page Addressing Mode (RESET)
	OLED_WR_Byte(0xA1,OLED_CMD); //Set segment remap, bit0:0,0->0;1,0->127;
	OLED_WR_Byte(0xC0,OLED_CMD); //Set COM Output Scan Direction
	OLED_WR_Byte(0xDA,OLED_CMD); //Set COM Pins
	OLED_WR_Byte(0x12,OLED_CMD); //[5:4] setting

	OLED_WR_Byte(0x81,OLED_CMD); //Contrast Control
	OLED_WR_Byte(0xEF,OLED_CMD); //1~256; Default: 0X7F
	OLED_WR_Byte(0xD9,OLED_CMD); //Set Pre-charge Period
	OLED_WR_Byte(0xf1,OLED_CMD); //[3:0],PHASE 1;[7:4],PHASE 2;
	OLED_WR_Byte(0xDB,OLED_CMD); //Set VCOMH
	OLED_WR_Byte(0x30,OLED_CMD);  //[6:4] 000,0.65*vcc;001,0.77*vcc;011,0.83*vcc;

	OLED_WR_Byte(0xA4,OLED_CMD); //Enable display outputs according to the GDDRAM contents
	OLED_WR_Byte(0xA6,OLED_CMD); //Set normal display
	OLED_WR_Byte(0xAF,OLED_CMD); //DISPLAY ON
	OLED_Clear();
}

/**************************************************************************
Drawing into OLED_GRAM only (one OLED_Refresh_Gram per frame: no flicker)
**************************************************************************/
void OLED_ClearGram(void)
{
	for (int i = 0; i < 8; i++) for (int n = 0; n < 128; n++) OLED_GRAM[n][i] = 0;
}

void OLED_Pixel(int x, int y, u8 t)
{
	if (x < 0 || x > 127 || y < 0 || y > 63) return;
	OLED_DrawPoint((u8)x, (u8)y, t);
}

void OLED_FillRect(int x0, int y0, int x1, int y1, u8 t)
{
	if (x0 > x1) { int k = x0; x0 = x1; x1 = k; }
	if (y0 > y1) { int k = y0; y0 = y1; y1 = k; }
	for (int x = x0; x <= x1; x++) for (int y = y0; y <= y1; y++) OLED_Pixel(x, y, t);
}

void OLED_Line(int x0, int y0, int x1, int y1, u8 t)
{
	int dx = abs(x1 - x0), sx = (x0 < x1) ? 1 : -1;
	int dy = -abs(y1 - y0), sy = (y0 < y1) ? 1 : -1;
	int err = dx + dy;
	for (;;) {
		OLED_Pixel(x0, y0, t);
		if (x0 == x1 && y0 == y1) break;
		int e2 = 2 * err;
		if (e2 >= dy) { err += dy; x0 += sx; }
		if (e2 <= dx) { err += dx; y0 += sy; }
	}
}

void OLED_Circle(int cx, int cy, int r, u8 t)
{
	int x = r, y = 0, err = 1 - r;
	while (x >= y) {
		OLED_Pixel(cx + x, cy + y, t); OLED_Pixel(cx - x, cy + y, t);
		OLED_Pixel(cx + x, cy - y, t); OLED_Pixel(cx - x, cy - y, t);
		OLED_Pixel(cx + y, cy + x, t); OLED_Pixel(cx - y, cy + x, t);
		OLED_Pixel(cx + y, cy - x, t); OLED_Pixel(cx - y, cy - x, t);
		y++;
		if (err < 0) err += 2 * y + 1;
		else { x--; err += 2 * (y - x) + 1; }
	}
}

/* One pixel of a font glyph: size 12 = 6 columns x 12 rows, size 16 = 8 x 16.
 * Both store two bytes per column, top row in bit 7 of the first byte. */
static u8 glyphPixel(u8 chr, u8 size, int col, int row)
{
	if (chr < ' ' || chr > '~') chr = ' ';
	const unsigned char *g = (size == 12) ? oled_asc2_1206[chr - ' '] : oled_asc2_1608[chr - ' '];
	return (u8)((g[col * 2 + row / 8] >> (7 - row % 8)) & 1);
}

int OLED_Text(int x, int y, const char *s, u8 size)
{
	int w = (size == 12) ? 6 : 8, h = (size == 12) ? 12 : 16;
	for (; *s; s++, x += (size == 12) ? 7 : 8)
		for (int c = 0; c < w; c++)
			for (int r = 0; r < h; r++)
				if (glyphPixel((u8)*s, size, c, r)) OLED_Pixel(x + c, y + r, 1);
	return x;
}

void OLED_TextBig(int x, int y, const char *s, u8 scale)
{
	for (; *s; s++, x += 8 * scale)
		for (int c = 0; c < 8; c++)
			for (int r = 0; r < 16; r++)
				if (glyphPixel((u8)*s, 16, c, r))
					OLED_FillRect(x + c * scale, y + r * scale, x + c * scale + scale - 1, y + r * scale + scale - 1, 1);
}

void OLED_Invert(u8 on)
{
	OLED_WR_Byte(on ? 0xA7 : 0xA6, OLED_CMD);
}

#if OLED_EYES
/**************************************************************************
Robot eyes: two rounded eyes that blink, glance around, and look at what
the sensors see. Moods: alert (something ahead), flinch (very close),
happy and dizzy (after a stunt), sleepy (nothing happening for a while).
**************************************************************************/
static uint32_t eyeRnd = 2463534242u;
static float eRand(void) { eyeRnd ^= eyeRnd << 13; eyeRnd ^= eyeRnd >> 17; eyeRnd ^= eyeRnd << 5; return (float)(eyeRnd & 0xFFFF) / 65535.0f; }   // 0..1

static void fillRoundRect(int x0, int y0, int w, int h, int r, u8 t)
{
	if (w <= 0 || h <= 0) return;
	if (r > w / 2) r = w / 2;
	if (r > h / 2) r = h / 2;
	for (int row = 0; row < h; row++) {
		int d = (row < r) ? r - row : (row >= h - r) ? row - (h - 1 - r) : 0;   // rows into a corner
		int inset = (d > 0) ? r - (int)sqrtf((float)(r * r - d * d)) : 0;
		OLED_Line(x0 + inset, y0 + row, x0 + w - 1 - inset, y0 + row, t);
	}
}

static void fillEllipse(int cx, int cy, int rx, int ry, u8 t)
{
	for (int y = -ry; y <= ry; y++) {
		float f = 1.0f - (float)(y * y) / (float)(ry * ry);
		int hw = (f > 0.0f) ? (int)((float)rx * sqrtf(f)) : 0;
		OLED_Line(cx - hw, cy + y, cx + hw, cy + y, t);
	}
}

static void spiral(int cx, int cy, float phase, int rmax)
{
	int px = cx, py = cy;
	for (int i = 1; i <= 40; i++) {
		float a = (float)i * 0.24f + phase, r = (float)rmax * (float)i / 40.0f;
		int x = cx + (int)(r * cosf(a)), y = cy + (int)(r * sinf(a));
		OLED_Line(px, py, x, y, 1);
		OLED_Line(px, py + 1, x, y + 1, 1);      // 2 px thick
		px = x; py = y;
	}
}

static void chevron(int cx, int cy, int s, int dir)  // '>' (dir +1) or '<' (dir -1), 3 px thick
{
	for (int k = -1; k <= 1; k++) {
		OLED_Line(cx - dir * s, cy - s + k, cx + dir * s, cy + k, 1);
		OLED_Line(cx + dir * s, cy + k, cx - dir * s, cy + s + k, 1);
	}
}

void OLED_Eyes(const EyesInput_t *in, uint32_t now)
{
	// gaze offset, eye size factors: current (smoothed) and target
	static float gx = 0, gy = 0, sw = 1, sh = 1;
	static float tgx = 0, tgy = 0;
	static uint32_t nextSaccade = 0, nextBlink = 0, blinkAt = 0, lastStim = 0, wakeUntil = 0;
	static u8 doubleBlink = 0, asleep = 0;

	float tsw = 1.0f, tsh = 1.0f;
	int mood = 0;                                       // 0 normal, 1 alert, 2 flinch, 3 happy, 4 dizzy, 5 sleepy
	int irL = (in->irL < 45.0f), irR = (in->irR < 45.0f);
	int near = (in->us > 0.0f && in->us < 60.0f);
	int turning = (in->yawRate > 40.0f || in->yawRate < -40.0f);

	if (irL || irR || near || turning || in->awake || in->happy || in->dizzy) {
		if (asleep) wakeUntil = now + 700;              // woken up: a startled look
		asleep = 0;
		lastStim = now;
	} else if (now - lastStim > 30000u) {
		asleep = 1;
	}

	if (in->dizzy) {
		mood = 4;
	} else if (in->happy) {
		mood = 3; tgx = 0; tgy = -3;
	} else if (turning) {                               // look into the turn
		tgx = (in->yawRate > 0.0f) ? -16.0f : 16.0f; tgy = 0;
	} else if (irL || irR) {                            // something beside the robot
		tgx = (irL && irR) ? 0.0f : (irL ? -16.0f : 16.0f); tgy = 2;
		if (irL && irR) { tsw = 1.15f; tsh = 1.15f; }
	} else if (near) {                                  // something ahead: stare, bigger as it comes closer
		tgx = 0; tgy = 0;
		if (in->us < 15.0f) mood = 2;
		else { mood = 1; float k = (60.0f - in->us) / 45.0f; tsw = 1.0f + 0.25f * k; tsh = 1.0f + 0.3f * k; }
	} else if (asleep) {
		mood = 5; tgx = 0; tgy = 6;
	} else if ((int32_t)(now - nextSaccade) >= 0) {     // idle: glance around
		if (eRand() < 0.3f) { tgx = 0; tgy = 0; }
		else { tgx = (eRand() - 0.5f) * 30.0f; tgy = (eRand() - 0.5f) * 14.0f; }
		nextSaccade = now + 1200u + (uint32_t)(eRand() * 2800.0f);
	}
	if ((int32_t)(wakeUntil - now) > 0) { tsw = 1.25f; tsh = 1.3f; }

	gx += (tgx - gx) * 0.35f; gy += (tgy - gy) * 0.35f;     // quick saccade, then settle
	sw += (tsw - sw) * 0.3f;  sh += (tsh - sh) * 0.3f;

	// blinking (slow and heavy when sleepy, none while dizzy / flinching)
	float lid = 1.0f;
	if (nextBlink == 0) nextBlink = now + 1500u;
	if ((int32_t)(now - nextBlink) >= 0 && mood != 4 && mood != 2) {
		blinkAt = now;
		if (!doubleBlink && eRand() < 0.15f) { doubleBlink = 1; nextBlink = now + 260u; }
		else { doubleBlink = 0; nextBlink = now + ((mood == 5) ? 4000u : 2000u) + (uint32_t)(eRand() * 4000.0f); }
	}
	uint32_t bt = now - blinkAt, blen = (mood == 5) ? 400u : 150u;
	if (blinkAt != 0 && bt < blen) {
		float half = (float)blen / 2.0f;
		lid = fabsf((float)bt - half) / half;
		if (lid < 0.08f) lid = 0.08f;
	}

	const int cyBase = 32, cxL = 38, cxR = 90, baseW = 36, baseH = 36;
	for (int e = 0; e < 2; e++) {
		int cx = ((e == 0) ? cxL : cxR) + (int)gx;
		int cy = cyBase + (int)gy;
		// perspective: the eye on the side it looks to is a little bigger
		float persp = 1.0f + ((e == 0) ? -gx : gx) * 0.006f;
		int w = (int)((float)baseW * sw * persp), h = (int)((float)baseH * sh * persp * lid);
		if (h < 3) h = 3;
		switch (mood) {
		case 2:                                             // flinch: > <
			chevron(cx, cy, 11, (e == 0) ? 1 : -1);
			break;
		case 4:                                             // dizzy: spinning spirals
			spiral(cx, cy, (float)now * 0.012f * ((e == 0) ? 1.0f : -1.0f), 17);
			break;
		case 3:                                             // happy: ^ ^
			fillEllipse(cx, cy + 4, w / 2, h / 2, 1);                // a round eye ...
			fillEllipse(cx, cy + 4 + h / 4, w / 2 + 2, h / 2 - 2, 0);  // ... with its lower part cut away: ^
			break;
		default:
			fillRoundRect(cx - w / 2, cy - h / 2, w, h, 9, 1);
			if (mood == 5) {                                // sleepy: heavy lids
				OLED_FillRect(cx - w / 2 - 1, cy - h / 2 - 1, cx + w / 2 + 1, cy - h / 2 + h * 11 / 20, 0);
			}
			break;
		}
	}
	if (mood == 5) {                                        // z z z drifting up
		int t = (int)((now / 120u) % 24u);
		OLED_Text(112, 22 - t, "z", 12);
		if (t > 8) OLED_Text(118, 30 - t, "z", 12);
	}
}
#endif

#if STUNTS
/**************************************************************************
Stunt screens (team video): 3-2-1-GO, live spin view, result card
**************************************************************************/
static const char *stuntName(char k)
{
	switch (k) {
	case 'H': return "DRIFT";
	case 'F': return "FLICK";
	case 'P': return "SLIDE";
	case 'T': return "TORNADO";
	case 'D': return "DONUT";
	case 'J': return "J-TURN";
	case 'W': return "SHIMMY";
	case 'R': return "GROUP 10";      // the reveal ends facing the team sign
	case 'V': return "PIVOT";
	case 'O': return "DONUTS";
	default:  return "STUNT";
	}
}

static u8 screenInverted = 0;

static void setInvert(u8 on)
{
	if (on != screenInverted) { OLED_Invert(on); screenInverted = on; }
}

/* Tyre smoke: a few short-lived dots thrown off the car's rear corners */
#define SMOKE_N 40
static struct { float x, y, vx, vy; int8_t life; } smoke[SMOKE_N];
static uint32_t rnd = 12345;
static float frand(void) { rnd = rnd * 1103515245u + 12345u; return (float)((rnd >> 9) & 0xFFFF) / 65535.0f - 0.5f; }

static void smokeEmit(float x, float y, float vx, float vy)
{
	for (int i = 0; i < SMOKE_N; i++) {
		if (smoke[i].life <= 0) {
			smoke[i].x = x; smoke[i].y = y;
			smoke[i].vx = vx + 0.6f * frand(); smoke[i].vy = vy + 0.6f * frand();
			smoke[i].life = (int8_t)(7 + (int)(6.0f * (frand() + 0.5f)));
			return;
		}
	}
}

static void smokeStep(void)
{
	for (int i = 0; i < SMOKE_N; i++) {
		if (smoke[i].life <= 0) continue;
		smoke[i].x += smoke[i].vx; smoke[i].y += smoke[i].vy;
		smoke[i].vx *= 0.85f; smoke[i].vy *= 0.85f;
		smoke[i].life--;
		int x = (int)smoke[i].x, y = (int)smoke[i].y;
		if (smoke[i].life > 6) OLED_FillRect(x, y, x + 1, y + 1, 1);   // fresh puff: 2x2
		else if ((smoke[i].life + i) & 1) OLED_Pixel(x, y, 1);          // fading: flickering dot
	}
}

/* The car seen from above, nose up when relHeading = 0, turning with the robot */
static void drawCar(int cx, int cy, float relHeadingDeg, int rateDps)
{
	float a = relHeadingDeg * 0.0174533f;
	float fx = -sinf(a), fy = -cosf(a);                 // nose direction on screen (y down)
	float rx = cosf(a),  ry = -sinf(a);                 // the car's right side
	const float L = 11.0f, W = 6.0f;
	int px[4], py[4];
	const float sl[4] = { L, L, -L, -L }, sw[4] = { -W, W, W, -W };
	for (int k = 0; k < 4; k++) {
		px[k] = cx + (int)lroundf(fx * sl[k] + rx * sw[k]);
		py[k] = cy + (int)lroundf(fy * sl[k] + ry * sw[k]);
	}
	for (int k = 0; k < 4; k++) OLED_Line(px[k], py[k], px[(k + 1) & 3], py[(k + 1) & 3], 1);
	// windscreen and a pointed nose
	int wx0 = cx + (int)lroundf(fx * 4 - rx * (W - 2)), wy0 = cy + (int)lroundf(fy * 4 - ry * (W - 2));
	int wx1 = cx + (int)lroundf(fx * 4 + rx * (W - 2)), wy1 = cy + (int)lroundf(fy * 4 + ry * (W - 2));
	OLED_Line(wx0, wy0, wx1, wy1, 1);
	OLED_Line(px[0], py[0], cx + (int)lroundf(fx * (L + 4)), cy + (int)lroundf(fy * (L + 4)), 1);
	OLED_Line(px[1], py[1], cx + (int)lroundf(fx * (L + 4)), cy + (int)lroundf(fy * (L + 4)), 1);
	// rear wheels
	for (int k = 2; k < 4; k++) OLED_FillRect(px[k] - 1, py[k] - 1, px[k], py[k], 1);

	// smoke: thrown outwards from the rear corners while spinning, else a trail behind
	int spinning = (rateDps > 120 || rateDps < -120);
	for (int k = 2; k < 4; k++) {
		float vx, vy;
		if (spinning) { vx = (float)(px[k] - cx) * 0.15f; vy = (float)(py[k] - cy) * 0.15f; }
		else          { vx = -fx * 2.2f; vy = -fy * 2.2f; }
		smokeEmit((float)px[k], (float)py[k], vx, vy);
		if (spinning) smokeEmit((float)px[k], (float)py[k], vx * 1.5f, vy * 1.5f);
	}
}

static void centredText(int y, const char *s, u8 size)
{
	int w = (int)strlen(s) * ((size == 12) ? 7 : 8);
	OLED_Text((128 - w) / 2, y, s, size);
}

static void sparkle(int x, int y)
{
	OLED_Pixel(x, y, 1);
	OLED_Pixel(x - 2, y, 1); OLED_Pixel(x + 2, y, 1);
	OLED_Pixel(x, y - 2, 1); OLED_Pixel(x, y + 2, 1);
}

void OLED_StuntScreen(const StuntStatus_t *s, uint32_t frame)
{
	char t[24];
	OLED_ClearGram();

	if (s->state == 1) {                                 // ---- countdown: 3 . 2 . 1 . GO!
		setInvert(0);
		centredText(0, stuntName(s->kind), 12);
		int ms = (int)s->countdownMs;
		if (ms > 150) {
			int beat = (ms + 499) / 500;                    // 3, 2, 1
			int into = 500 - (ms - (beat - 1) * 500);       // ms into this beat
			t[0] = (char)('0' + ((beat > 9) ? 9 : beat)); t[1] = 0;
			OLED_TextBig(52, 14, t, 3);                     // 24 x 48
			OLED_Circle(64, 38, 30 - into / 25, 1);         // ring closing in on the beat
		} else {
			OLED_TextBig(40, 20, "GO!", 2);
			for (int k = 0; k < 8; k++) {                   // burst
				float a = (float)k * 0.785398f;
				OLED_Line(64 + (int)(30 * cosf(a)), 38 + (int)(15 * sinf(a)),
				          64 + (int)(38 * cosf(a)), 38 + (int)(21 * sinf(a)), 1);
			}
		}
		for (int i = 0; i < SMOKE_N; i++) smoke[i].life = 0;
		return;
	}

	if (s->state == 2) {                                 // ---- live: name, yaw, rate, spinning car
		setInvert(0);
		OLED_Text(0, 0, stuntName(s->kind), 16);
		sprintf(t, "%+ld", (long)s->yawDeg);
		int x = OLED_Text(0, 18, t, 16);
		OLED_Text(x + 1, 22, "deg", 12);
		long r = (long)((s->rateDps < 0) ? -s->rateDps : s->rateDps);
		if (s->sat && (frame & 4)) strcpy(t, "MAX!");       // gyro at its limit: blink
		else sprintf(t, "%ld d/s", r);
		OLED_Text(0, 36, t, 12);
		// spin-rate bar (full scale 1000 deg/s) with a tick at the peak so far
		OLED_Line(0, 50, 68, 50, 1); OLED_Line(0, 58, 68, 58, 1);
		OLED_Line(0, 50, 0, 58, 1);  OLED_Line(68, 50, 68, 58, 1);
		int w = (int)(r * 66 / 1000); if (w > 66) w = 66;
		if (w > 0) OLED_FillRect(1, 52, w, 56, 1);
		int pk = (int)((long)s->rateMax * 66 / 1000); if (pk > 66) pk = 66;
		if (pk > 0) OLED_Line(pk + 1, 49, pk + 1, 59, 1);
		// progress through the stunt's segments
		if (s->nSeg > 0) OLED_FillRect(0, 62, (int)((s->seg + 1) * 127 / s->nSeg), 63, 1);
		// compass ring with the car turning live in it
		OLED_Circle(100, 30, 25, 1);
		for (int k = 0; k < 8; k++) {
			float a = (float)k * 0.785398f;
			OLED_Line(100 + (int)(25 * cosf(a)), 30 + (int)(25 * sinf(a)),
			          100 + (int)(21 * cosf(a)), 30 + (int)(21 * sinf(a)), 1);
		}
		smokeStep();
		drawCar(100, 30, s->relHeading, (int)s->rateDps);
		return;
	}

	// ---- result card: flash, big name, the numbers, sparkles
	setInvert((s->sinceEnd < 600 && ((s->sinceEnd / 150) & 1) == 0) ? 1 : 0);
	const char *n = stuntName(s->kind);
	int nw = (int)strlen(n) * 16;
	if (nw <= 128) OLED_TextBig((128 - nw) / 2, 0, n, 2);
	else           centredText(8, n, 16);
	if (s->aborted) {
		centredText(36, "ABORTED", 16);
	} else {
		sprintf(t, "SPIN %+ld deg", (long)s->yawDeg);
		centredText(34, t, 12);
		sprintf(t, "PEAK %ld%sd/s %lu.%lus", (long)s->rateMax, s->sat ? "+" : "", (unsigned long)(s->ms / 1000), (unsigned long)((s->ms % 1000) / 100));
		centredText(48, t, 12);
	}
	if (frame & 2) { sparkle(2, 40); sparkle(125, 58); sparkle(125, 34); }
	else           { sparkle(2, 60); sparkle(2, 30); sparkle(125, 46); }
}

void OLED_StuntScreenOff(void)
{
	setInvert(0);
}
#endif
