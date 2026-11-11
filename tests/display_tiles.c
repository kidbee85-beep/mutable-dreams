/* SPDX-License-Identifier: MIT OR GPL-3.0-only */
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#ifndef TEST_ROWS
#define TEST_ROWS 30
#endif
#define APP_CANVAS_PX (240u*TEST_ROWS)
static uint16_t screen[240*90];
static unsigned blits;
static void lcd_sync(void){}
static void lcd_blit(uint32_t x,uint32_t y,uint32_t w,uint32_t h,const uint16_t *p){
 assert(x+w<=240&&y+h<=90&&w*h<=APP_CANVAS_PX);for(unsigned j=0;j<h;j++)memcpy(screen+(y+j)*240+x,p+j*w,w*2);blits++;
}
#include "md_gfx_portable.h"
#define APP_BIG_FONT FONT_L
#include "sys_text.c"
int main(){
 for(int face=0;face<3;face++)for(int align=0;align<3;align++)for(unsigned width=120;width<=240;width+=60){
  memset(screen,0x25,sizeof screen);unsigned old=blits;sys_box(0,5,width,face,face==2?"RECOVERY":"RESTORE FROM USB?",C_WHITE,align);assert(blits>old);assert(fwrite(screen,1,sizeof screen,stdout)==sizeof screen);
 }
 sys_box(0,0,0,SYS_BIG,"EMPTY",C_WHITE,0);fprintf(stderr,"PASS 27 actual-font system boxes, tall glyphs, three alignments/widths, zero-width safety; tile rows %d.\n",TEST_ROWS);
}
