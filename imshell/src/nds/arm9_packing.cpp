#include <stdio.h>
#include <string.h>
#include "arm9_setting.h"
#include "arm9_packing.h"

void clearscreen(void)
{
	printf("\x1b[2J");
	printf("\x1b[H");
}

void clearinfo(void)
{
	printf("\x1b[12;0H");
	for (int i = 1; i <= 8; i++)
	{
		printf("\x1b[K");
		printf("\x1b[1B");
	}
}

void printinfo(std::string str)
{
	clearinfo();
	printf("\x1b[12;0H%s", str.c_str());
}

void printnext(std::string str)
{
	printf("\x1b[1;0H");
	printf("\x1b[K");
	printf("\x1b[1;0HNext -> %s", str.c_str());
}

void printscore(int blacknum, int whitenum)
{
	printf("\x1b[5;0H");
	printf("\x1b[K");
	printf("\x1b[5;0HBlack = %02d | White = %02d", blacknum, whitenum);
}

void initFrameBuffer(void)
{
	videoSetMode(MODE_FB0);
	vramSetBankA(VRAM_A_LCD);
}

void initSubVideoConsole(void)
{
	static PrintConsole topScreen;
	static PrintConsole bottomScreen;

	videoSetMode(MODE_0_2D);
	videoSetModeSub(MODE_0_2D);

	vramSetBankA(VRAM_A_MAIN_BG);
	vramSetBankC(VRAM_C_SUB_BG);

	consoleInit(&topScreen, 3, BgType_Text4bpp, BgSize_T_256x256, 31, 0, true, true);
	consoleInit(&bottomScreen, 3, BgType_Text4bpp, BgSize_T_256x256, 31, 0, false, true);

	consoleSelect(&bottomScreen); // after swap it will go to topscreen
								  // printf("\n\n\tHello DS dev'rs\n");
								  // printf("\twww.drunkencoders.com\n");
								  // printf("\twww.devkitpro.org");
}

void printlog(std::string str, ...)
{
	// the message itself may contain '%' (e.g. coordinates), so it must
	// not be used as the printf format; extra variadic args are ignored
	std::printf("%s\n", str.c_str());
}

/*
https://stackoverflow.com/questions/7536434/how-would-one-draw-to-the-sub-display-of-a-ds-as-if-it-was-a-framebuffer
A sample to make sub screen as if it is frame buffer
#include <nds.h>

int main(void)
{
int x, y;

//set the mode to allow for an extended rotation background
videoSetMode(MODE_5_2D);
videoSetModeSub(MODE_5_2D);

//allocate a vram bank for each display
vramSetBankA(VRAM_A_MAIN_BG);
vramSetBankC(VRAM_C_SUB_BG);

//create a background on each display
int bgMain = bgInit(3, BgType_Bmp16, BgSize_B16_256x256, 0,0);
int bgSub = bgInitSub(3, BgType_Bmp16, BgSize_B16_256x256, 0,0);

u16* videoMemoryMain = bgGetGfxPtr(bgMain);
u16* videoMemorySub = bgGetGfxPtr(bgSub);


//initialize it with a color
for(x = 0; x < 256; x++)
	for(y = 0; y < 256; y++)
	{
		videoMemoryMain[x + y * 256] = ARGB16(1, 31, 0, 0);
		videoMemorySub[x + y * 256] = ARGB16(1, 0, 0, 31);
	}

while(1)
{
	swiWaitForVBlank();
}
*/