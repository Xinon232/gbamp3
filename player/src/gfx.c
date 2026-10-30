/* Halim Jarrar, 2026. GPL-3.0-or-later with the additional permission described in NOTICES.md (linking with the Helix MP3 decoder). v1.7 video pages and switching; v2.1 mode 4 only (see gfx.h). */
#include "gfx.h"
#include "cover.h"
#include "player.h"
#include "player_api.h"
#define R16(a) (*(volatile uint16_t*)(a))
#define REG_DISPCNT R16(0x4000000)
#define REG_VCOUNT R16(0x4000006)
#define REG_IME R16(0x4000208)
#define BG_PALETTE ((volatile uint16_t*)0x05000000)
#define VRAM ((volatile uint16_t*)0x06000000)
#define PAGE1 ((volatile uint16_t*)0x0600A000)
#define DMA3SAD (*(volatile uint32_t*)0x040000D4)
#define DMA3DAD (*(volatile uint32_t*)0x040000D8)
#define DMA3CNT (*(volatile uint32_t*)0x040000DC)
#define DISP_M4 0x1444u   /* mode 4, BG2, sprites; bit 4 selects the shown page */
#define C_WHITE 0x7fff
unsigned gfx_m4=1;   /* v2.1: always (text.c reads it) */
volatile unsigned gfx_flip_state;
static unsigned screen_on=1,blanked;   /* blanked: forced blank from the start until the first picture */
static int dy0=160,dy1,sy0=160,sy1;   /* lines drawn since the last commit / to copy after the switch */
static unsigned pal_n=1;   /* interface palette entries used: 1 .. GFX_UI_COLORS-1 */
/* v2.1: the colours of a cover drawn on the hidden page wait in video memory
 * no page uses (between the pages) and go to the palette when that page is
 * shown, in the same vertical blank (see gfx_cover). */
#define PAL_WAIT ((volatile uint32_t*)0x06009600)
static volatile unsigned pal_waiting;
static void pal_apply(void){
 if(!pal_waiting)return;
 volatile uint32_t *d=(volatile uint32_t*)(void*)(BG_PALETTE+GFX_UI_COLORS);
 for(unsigned i=0;i<COVER_COLORS/2;i++)d[i]=PAL_WAIT[i];   /* CPU copy: the interrupted code may be setting up DMA 3 */
 pal_waiting=0;
}
static void dma32(volatile void *dst,const volatile void *src,unsigned words,unsigned fixed){
 DMA3SAD=(uint32_t)src;DMA3DAD=(uint32_t)dst;DMA3CNT=words|(fixed?0x85000000u:0x84000000u);
}
static void dma16_fixed(volatile void *dst,const volatile void *src,unsigned halfwords){
 DMA3SAD=(uint32_t)src;DMA3DAD=(uint32_t)dst;DMA3CNT=halfwords|0x81000000u;
}
/* v2.0.1: page-to-page copies go in bands of 8 lines (480 words, about 2000
 * cycles). The CPU stops during a DMA transfer, the sound interrupt with it;
 * a whole-screen copy in one transfer (about 41000 cycles, 80 samples) let
 * the 32-sample sound FIFO run dry and screech. Between bands the interrupt
 * runs. */
static void copy_lines(volatile uint16_t *dst,const volatile uint16_t *src,int n){
 while(n>0){int k=n>8?8:n;dma32(dst,src,(unsigned)k*60,0);dst+=k*120;src+=k*120;n-=k;}
}
static volatile uint16_t *shown(void){return (REG_DISPCNT&0x10)?PAGE1:VRAM;}
static volatile uint16_t *hidden(void){return (REG_DISPCNT&0x10)?VRAM:PAGE1;}
static unsigned disp_value(unsigned page){
 unsigned v=DISP_M4|(page?0x10u:0u);v=screen_on?v:v&~0x400u;return blanked?v|0x80u:v;
}
uint16_t gfx_pixel(uint16_t rgb){
 for(unsigned i=1;i<pal_n;i++)if(BG_PALETTE[i]==rgb)return (uint16_t)i;
 if(pal_n>=GFX_UI_COLORS)return 1;
 BG_PALETTE[pal_n]=rgb;return (uint16_t)pal_n++;
}
void gfx_mark(int y0,int y1){if(y0<dy0)dy0=y0;if(y1>dy1)dy1=y1;}
volatile uint16_t *gfx_line(int y){return hidden()+y*120;}
void gfx_fill(int x,int y,int w,int h,uint16_t rgb){
 if(x<0){w+=x;x=0;}if(y<0){h+=y;y=0;}if(x+w>240)w=240-x;if(y+h>160)h=160-y;if(w<=0||h<=0)return;
 unsigned c=gfx_pixel(rgb);volatile uint16_t pair=(uint16_t)(c|c<<8);
 for(int j=y;j<y+h;j++){volatile uint16_t *l=gfx_line(j);int a=x,n=w;
  if(a&1){l[a>>1]=(uint16_t)((l[a>>1]&0x00ff)|c<<8);a++;n--;}
  if(n>=2){dma16_fixed(l+(a>>1),&pair,(unsigned)n>>1);a+=n&~1;n&=1;}
  if(n)l[a>>1]=(uint16_t)((l[a>>1]&0xff00)|c);
 }
 gfx_mark(y,y+h);
}
void gfx_plot(int x,int y,uint16_t rgb){
 if(x<0||x>=240||y<0||y>=160)return;
 unsigned c=gfx_pixel(rgb);volatile uint16_t *p=gfx_line(y)+(x>>1);
 *p=(uint16_t)((x&1)?(*p&0x00ff)|c<<8:(*p&0xff00)|c);gfx_mark(y,y+1);
}
void gfx_copy_lines(int dy,int sy,int n){
 if(n<=0)return;
 copy_lines(hidden()+dy*120,shown()+sy*120,n);gfx_mark(dy,dy+n);
}
unsigned gfx_blanked(void){return blanked;}
static void flip_now(void){
 unsigned v;do v=REG_VCOUNT;while(v<160||v>=220);   /* the vertical blank */
 unsigned ime=REG_IME;REG_IME=0;REG_DISPCNT=(uint16_t)((REG_DISPCNT^0x10)&~0x80u);pal_apply();blanked=0;gfx_flip_state=2;REG_IME=ime;
}
void gfx_flip_irq(void){
 if(gfx_flip_state!=1)return;
 unsigned v=REG_VCOUNT;if(v<160||v>=220)return;
 REG_DISPCNT=(uint16_t)((REG_DISPCNT^0x10)&~0x80u);pal_apply();blanked=0;gfx_flip_state=2;
}
void gfx_ready(void){
 while(gfx_flip_state==1)if(stats.paused)flip_now();   /* otherwise the sound interrupt switches within a frame */
 if(gfx_flip_state==2){
  copy_lines(hidden()+sy0*120,shown()+sy0*120,sy1-sy0);
  sy0=160;sy1=0;gfx_flip_state=0;
 }
}
void gfx_commit(void){
 if(dy1<=dy0)return;
 gfx_ready();
 sy0=dy0;sy1=dy1;dy0=160;dy1=0;gfx_flip_state=1;
 if(stats.paused)flip_now();
}
void gfx_commit_now(void){
 if(dy1<=dy0)return;
 gfx_ready();
 copy_lines(shown()+dy0*120,hidden()+dy0*120,dy1-dy0);dy0=160;dy1=0;pal_apply();
}
void gfx_screen(unsigned on){
 screen_on=on;unsigned ime=REG_IME;REG_IME=0;
 REG_DISPCNT=(uint16_t)(on?REG_DISPCNT|0x400u:REG_DISPCNT&~0x400u);REG_IME=ime;
}
/* The first picture is shown at a page switch, which ends the forced blank
 * (white) the screen starts with; both pages start white. */
void gfx_init(void){
 BG_PALETTE[0]=0;
 unsigned ime=REG_IME;REG_IME=0;
 gfx_flip_state=0;dy0=sy0=160;dy1=sy1=0;blanked=1;
 REG_DISPCNT=(uint16_t)disp_value(0);REG_IME=ime;
 volatile uint32_t w=gfx_pixel(C_WHITE)*0x01010101u;dma32(VRAM,&w,0x13600/4,1);
}
/* v2.1: a cover record (cover.h) on the hidden page at (x, y), x even. Its
 * colours replace the shown cover's when this page is shown (pal_apply). */
void gfx_cover(int x,int y,const volatile uint16_t *rec){
 pal_waiting=0;   /* no switch is pending (gfx_ready): nothing reads the waiting colours now */
 const volatile uint32_t *pal=(const volatile uint32_t*)(rec+COVER_PAL/2);
 for(unsigned i=0;i<COVER_COLORS/2;i++)PAL_WAIT[i]=pal[i];
 pal_waiting=1;
 unsigned base=GFX_UI_COLORS;
 uint32_t add=base*0x01010101u;const volatile uint32_t *src=(const volatile uint32_t*)rec;
 for(int j=0;j<COVER_SIDE;j++){volatile uint32_t *d=(volatile uint32_t*)(void*)(hidden()+(y+j)*120+(x>>1));
  for(int i=0;i<COVER_SIDE/4;i++)d[i]=src[j*(COVER_SIDE/4)+i]+add;}
 gfx_mark(y,y+COVER_SIDE);
}
