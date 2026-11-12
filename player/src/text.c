/* Halim Jarrar, 2026. GPL-3.0-or-later with the additional permission described in NOTICES.md (linking with the Helix MP3 decoder), except the included SuperFW renderer/UTF-8 helpers
 * (David Guillen Fandos, GPL-3.0-or-later) and GBAVocab's fold table (GPL-3.0). */
#include "text.h"
#include <string.h>
#include "utf_util.c"
#include "font_render.c"
#include "fold_table.h"
#define VRAM ((volatile uint16_t*)0x06000000)
/* v1.7 mode 4 (gfx.c): weak, so host tests that link only this file keep mode 3. */
extern unsigned gfx_m4 __attribute__((weak));
volatile uint16_t *gfx_line(int y) __attribute__((weak));
uint16_t gfx_pixel(uint16_t rgb) __attribute__((weak));
void gfx_mark(int y0,int y1) __attribute__((weak));
void gfx_plot(int x,int y,uint16_t rgb) __attribute__((weak));
#define M4 (&gfx_m4&&gfx_m4)
#define DMA3SAD (*(volatile uint32_t*)0x040000D4)
#define DMA3DAD (*(volatile uint32_t*)0x040000D8)
#define DMA3CNT (*(volatile uint32_t*)0x040000DC)
/* Only the UTF-8/lookup parts of the SuperFW renderer are used; its 8-bit
 * framebuffer routines are unreferenced and removed by --gc-sections. */
extern const unsigned char font_pack_latin[];
static const uint32_t empty_pack[2] = {0x00014f46u, 8u};
void *font_base_addr = (void *)font_pack_latin;
void *reader_font_base_addr = (void *)empty_pack;

uint32_t text_next(const char **s){
 const unsigned char *p=(const unsigned char*)*s;unsigned a=p[0];
 if(a<0x80){*s+=a?1:0;return a;}
 unsigned n=a>=0xc2&&a<=0xdf?1:a>=0xe0&&a<=0xef?2:a>=0xf0&&a<=0xf4?3:0;
 if(!n){*s+=1;return 0xfffd;}
 uint32_t cp=a&((1u<<(6-n))-1);
 for(unsigned i=1;i<=n;i++){unsigned b=p[i];if(b<0x80||b>0xbf){*s+=1;return 0xfffd;}cp=(cp<<6)|(b&63);}
 if(cp<(n==1?0x80u:n==2?0x800u:0x10000u)||cp>0x10ffff||(cp>=0xd800&&cp<=0xdfff)){*s+=1;return 0xfffd;}
 *s+=n+1;return cp;
}
size_t utf8_put(char *d,size_t cap,uint32_t c){
 if(c<0x80){if(cap<1)return 0;d[0]=(char)c;return 1;}
 if(c<0x800){if(cap<2)return 0;d[0]=(char)(0xc0|c>>6);d[1]=(char)(0x80|(c&63));return 2;}
 if(c<0x10000){if(cap<3)return 0;d[0]=(char)(0xe0|c>>12);d[1]=(char)(0x80|((c>>6)&63));d[2]=(char)(0x80|(c&63));return 3;}
 if(cap<4)return 0;d[0]=(char)(0xf0|c>>18);d[1]=(char)(0x80|((c>>12)&63));d[2]=(char)(0x80|((c>>6)&63));d[3]=(char)(0x80|(c&63));return 4;
}
/* Typographic punctuation absent from the font maps to its ASCII form. */
static uint32_t display_code(uint32_t c){
 if(c==0x2018||c==0x2019||c==0x201a||c==0x2032)return '\'';
 if(c==0x201c||c==0x201d||c==0x201e||c==0x2033)return '"';
 if(c>=0x2010&&c<=0x2015)return '-';
 if(c==0x00a0||c==0x2009||c==0x202f)return ' ';
 return c;
}
/* ASCII lookups are cached (the renderer searches its font blocks per call). */
static struct {const uint16_t *d;uint8_t w,adv;} ascii[95];
static int glyph_lookup(uint32_t c,t_char_render_info *g);
static int glyph(uint32_t c,t_char_render_info *g){
 if(c>=32&&c<127){
  unsigned i=c-32;
  if(!ascii[i].adv){int a=glyph_lookup(c,g);if(g->nchars==1&&a<256){ascii[i].d=g->data[0];ascii[i].w=(uint8_t)g->char_width;ascii[i].adv=(uint8_t)a;}return a;}
  g->char_width=ascii[i].w;g->nchars=1;g->data[0]=ascii[i].d;g->spacing_cols=(unsigned)ascii[i].adv-ascii[i].w;return ascii[i].adv;
 }
 return glyph_lookup(c,g);
}
static int glyph_lookup(uint32_t c,t_char_render_info *g){
 c=display_code(c);
 if(c==0x2026)c='.';
 if(!lookup_chptr(c,g))lookup_chptr(MISSING_CHAR,g);
 return (int)(g->char_width+g->spacing_cols);
}
unsigned text_width(const char *s){
 unsigned w=0;while(*s){uint32_t c=text_next(&s);t_char_render_info g;int a=glyph(c,&g);w+=(unsigned)a*(display_code(c)==0x2026?3:1);}
 return w;
}
/* Rendering goes through a column buffer: glyph lookups once per character,
 * then plain row-major stores (much cheaper from ROM than per-glyph loops). */
static uint16_t cols[240];
/* Column bits of text pixels [from, from+width); returns columns set (rest 0). */
static unsigned collect(const char *s,unsigned from,unsigned width){
 unsigned px=0,end=from+width,n=0;
 while(*s&&px<end){
  uint32_t c=text_next(&s);t_char_render_info g;unsigned a=(unsigned)glyph(c,&g);
  unsigned rep=display_code(c)==0x2026?3:1;
  for(unsigned r=0;r<rep&&px<end;r++){
   if(px+a<=from){px+=a;continue;}
   if(px>=from&&px+a<=end&&g.char_width<=a){   /* v1.8: whole glyph inside: straight copy */
    uint16_t *d=cols+(px-from);const uint16_t *s0=g.data[0];unsigned w=g.char_width,i=0;
    if(g.nchars==1)for(;i<w;i++)d[i]=s0[i];
    else for(;i<w;i++){uint16_t b=s0[i];for(unsigned k=1;k<g.nchars;k++)b|=g.data[k][i];d[i]=b;}
    for(;i<a;i++)d[i]=0;
    px+=a;n=px-from;continue;
   }
   for(unsigned i=0;i<a&&px<end;i++,px++){
    if(px<from)continue;
    uint16_t bits=0;if(i<g.char_width){bits=g.data[0][i];for(unsigned k=1;k<g.nchars;k++)bits|=g.data[k][i];}
    cols[px-from]=bits;n=px-from+1;
   }
  }
 }
 for(unsigned k=n;k<width;k++)cols[k]=0;
 return n;
}
static int window(int x,int x0,int x1,unsigned skip,unsigned *from,unsigned *width){
 if(x0<0)x0=0;if(x1>240)x1=240;
 int origin=x-(int)skip,start=origin>x0?origin:x0;
 if(start>=x1)return -1;
 *from=(unsigned)(start-origin);*width=(unsigned)(x1-start);return start;
}
void blit_cols(volatile uint16_t *dst,const uint16_t *cols,unsigned n,unsigned fg,unsigned bg,unsigned opaque);
void blit_pairs(volatile uint16_t *dst,const uint16_t *cols,unsigned pairs,unsigned fg,unsigned bg);
/* Mode 4: one column of 16 pixels at x (the half of each halfword it owns). */
static void col8(int x,int y,uint16_t bits,unsigned fg,unsigned bg,unsigned opaque){
 for(unsigned j=0;j<16;j++,bits>>=1){if(!opaque&&!(bits&1))continue;
  volatile uint16_t *p=gfx_line(y+(int)j)+(x>>1);unsigned c=(bits&1)?fg:bg;
  *p=(uint16_t)((x&1)?(*p&0x00ff)|c<<8:(*p&0xff00)|c);}
}
void text_draw(int x,int y,int x0,int x1,const char *s,unsigned skip,uint16_t color){
 if(y<0||y>160-TEXT_HEIGHT)return;
 unsigned from,width;int start=window(x,x0,x1,skip,&from,&width);if(start<0)return;
 unsigned n=collect(s,from,width);
 if(M4){unsigned c=gfx_pixel(color);for(unsigned k=0;k<n;k++)if(cols[k])col8(start+(int)k,y,cols[k],c,0,0);gfx_mark(y,y+16);return;}
 blit_cols(VRAM+y*240+start,cols,n,color,0,0);
}
/* Opaque variant: every pixel of the [x0,x1) x 16 box is written once (glyph
 * pixels fg, the rest bg), so redraws never flash an empty field. */
void text_draw_bg(int x,int y,int x0,int x1,const char *s,unsigned skip,uint16_t fg,uint16_t bg){
 if(y<0||y>160-TEXT_HEIGHT)return;
 unsigned from,width;int start=window(x,x0,x1,skip,&from,&width);if(start<0)return;
 collect(s,from,width);
 if(M4){   /* pixel pairs in IWRAM; an odd first / last column on its own */
  unsigned f=gfx_pixel(fg),b=gfx_pixel(bg),k=0;int a=start;
  if(a&1){col8(a,y,cols[0],f,b,1);a++;k=1;}
  unsigned pairs=(width-k)/2;if(pairs)blit_pairs(gfx_line(y)+(a>>1),cols+k,pairs,f,b);
  k+=2*pairs;if(k<width)col8(start+(int)k,y,cols[k],f,b,1);
  gfx_mark(y,y+16);return;
 }
 blit_cols(VRAM+y*240+start,cols,width,fg,bg,1);
}
void text_cursor(struct TextCursor *c,const char *s,unsigned skip){
 c->s=s;c->a=c->i=c->rep=0;
 while(skip){uint16_t unused=text_cursor_next(c);(void)unused;skip--;}
}
uint16_t text_cursor_next(struct TextCursor *c){
 while(c->i>=c->a){
  if(c->rep){c->rep--;c->i=0;continue;}
  if(!*c->s){c->a=c->i=0;return 0;}
  uint32_t ch=text_next(&c->s);t_char_render_info g;c->a=(unsigned)glyph(ch,&g);c->i=0;
  c->rep=display_code(ch)==0x2026?2:0;c->w=g.char_width;c->n=g.nchars;
  for(unsigned k=0;k<3;k++)c->d[k]=k<g.nchars?g.data[k]:0;
 }
 unsigned i=c->i++;uint16_t bits=0;
 if(i<c->w){bits=c->d[0][i];for(unsigned k=1;k<c->n;k++)bits|=c->d[k][i];}
 return bits;
}
size_t text_fit(const char *s,unsigned width,unsigned *whole){
 unsigned total=text_width(s);if(whole)*whole=total<=width;
 if(total<=width){size_t n=0;while(s[n])n++;return n;}
 t_char_render_info g;unsigned dots=(unsigned)glyph('.',&g)*3,w=0;const char *p=s,*last=s;
 while(*p){const char *q=p;uint32_t c=text_next(&q);unsigned a=(unsigned)glyph(c,&g)*(display_code(c)==0x2026?3:1);
  if(w+a+dots>width)break;w+=a;p=q;last=p;}
 return (size_t)(last-s);
}

/* ---- 5x7 small font (original gbamp3 glyphs; lowercase as in gbavocab v2.0) ---- */
static const unsigned char upper5[36][7]={{14,17,17,31,17,17,17},{30,17,17,30,17,17,30},{14,17,16,16,16,17,14},{30,17,17,17,17,17,30},{31,16,16,30,16,16,31},{31,16,16,30,16,16,16},{14,17,16,23,17,17,15},{17,17,17,31,17,17,17},{14,4,4,4,4,4,14},{7,2,2,2,18,18,12},{17,18,20,24,20,18,17},{16,16,16,16,16,16,31},{17,27,21,21,17,17,17},{17,25,21,19,17,17,17},{14,17,17,17,17,17,14},{30,17,17,30,16,16,16},{14,17,17,17,21,18,13},{30,17,17,30,20,18,17},{15,16,16,14,1,1,30},{31,4,4,4,4,4,4},{17,17,17,17,17,17,14},{17,17,17,17,17,10,4},{17,17,17,21,21,27,17},{17,17,10,4,10,17,17},{17,17,10,4,4,4,4},{31,1,2,4,8,16,31},
 {14,17,19,21,25,17,14},{4,12,4,4,4,4,14},{14,17,1,2,4,8,31},{30,1,1,14,1,1,30},{2,6,10,18,31,2,2},{31,16,16,30,1,1,30},{14,16,16,30,17,17,14},{31,1,2,4,8,8,8},{14,17,17,14,17,17,14},{14,17,17,15,1,1,14}};
static const unsigned char lower5[26][7]={{0,0,14,1,15,17,15},{16,16,30,17,17,17,30},{0,0,14,17,16,17,14},{1,1,15,17,17,17,15},{0,0,14,17,31,16,14},{6,8,8,28,8,8,8},{0,15,17,17,15,1,14},{16,16,30,17,17,17,17},{4,0,12,4,4,4,14},{2,0,6,2,2,18,12},{16,16,18,20,24,20,18},{12,4,4,4,4,4,14},{0,0,26,21,21,21,21},
 {0,0,30,17,17,17,17},{0,0,14,17,17,17,14},{0,0,30,17,30,16,16},{0,0,15,17,15,1,1},{0,0,22,25,16,16,16},{0,0,15,16,14,1,30},{8,8,28,8,8,9,6},{0,0,17,17,17,19,13},{0,0,17,17,17,10,4},{0,0,17,17,21,21,10},{0,0,17,10,4,10,17},{0,0,17,17,15,1,14},{0,0,31,2,4,8,31}};
/* g j p q y: two extra rows below the baseline (rows 7-8), bowls on the x-height. */
static const unsigned char descend5[5][9]={{0,0,15,17,17,17,15,1,14},{2,0,6,2,2,2,2,18,12},{0,0,30,17,17,17,30,16,16},{0,0,15,17,17,17,15,1,1},{0,0,17,17,17,17,15,1,14}};
static unsigned small_bits(uint32_t c,int j){
 {const char *d="gjpqy";for(int k=0;k<5;k++)if(c==(uint32_t)d[k])return descend5[k][j];}
 if(j>=7)return 0;
 if(c>='a'&&c<='z')return lower5[c-'a'][j];
 if(c>='A'&&c<='Z')return upper5[c-'A'][j];
 if(c>='0'&&c<='9')return upper5[26+c-'0'][j];
 switch(c){
  case '-':return j==3?14:0;case '.':return j==6?4:0;case ',':return j==5?4:j==6?8:0;
  case ':':return (j==2||j==5)?4:0;case '/':return 1u<<(j<5?j:4);case '(':return (j==0||j==6)?2:4;
  case ')':return (j==0||j==6)?8:4;case '_':return j==6?31:0;case '\'':return j<2?4:0;
  case '!':return j==5?0:4;case '?':{static const unsigned char q[7]={14,17,1,2,4,0,4};return q[j];}
  case '&':{static const unsigned char a[7]={12,18,20,8,21,18,13};return a[j];}
  case '+':return j==3?31:(j>=1&&j<=5)?4:0;case '#':return (j==1||j==5)?31:10;
  case '"':return j<2?10:0;case '@':{static const unsigned char at[7]={14,17,23,21,23,16,14};return at[j];}
  case '%':{static const unsigned char pc[7]={24,25,2,4,8,19,3};return pc[j];}
  case '>':{static const unsigned char gt[7]={8,4,2,1,2,4,8};return gt[j];}
  case '*':{static const unsigned char st[7]={0,4,21,14,21,4,0};return st[j];}
  case '=':return (j==2||j==4)?31:0;case ';':return j==2||j==5?4:j==6?8:0;
  default:return 0;
 }
}
/* Latin capitals with diacritics keep their case (the folding table maps to
 * lowercase base letters). */
static int latin_upper(uint32_t c){
 if(c>=0xc0&&c<=0xde)return c!=0xd7;
 if(c>=0x100&&c<=0x137)return !(c&1);
 if(c>=0x139&&c<=0x148)return c&1;
 if(c>=0x14a&&c<=0x177)return !(c&1);
 if(c==0x178)return 1;
 if(c>=0x179&&c<=0x17e)return c&1;
 return 0;
}
static uint32_t base_letter(uint32_t c){
 if(c<0x80)return c;
 unsigned lo=0,hi=FOLD_MAPPINGS;
 while(lo<hi){unsigned m=(lo+hi)/2;if(fold_mappings[m].from<c)lo=m+1;else hi=m;}
 if(lo<FOLD_MAPPINGS&&fold_mappings[lo].from==c){uint32_t b=fold_mappings[lo].to;return latin_upper(c)&&b>='a'&&b<='z'?b-32:b;}
 return c==0xfffd?'?':c<0x300?'?':0;
}
unsigned small_width(const char *s){unsigned w=0;while(*s){uint32_t c=text_next(&s);if(base_letter(c))w+=SMALL_ADVANCE;}return w?w-1:0;}
/* v1.8: the rows of a glyph once per character (the lookup chain ran per row),
 * turned into 5 columns and drawn through the column blitter (IWRAM) like the
 * large font: per-pixel stores from ROM made the small texts of Now Playing
 * cost more than the rest of the screen. */
static unsigned small_cols(uint32_t c,uint16_t col[5]){
 unsigned char r[SMALL_HEIGHT+SMALL_DESCENT];const unsigned char *t=0;unsigned rows=7;
 switch(c){case 'g':t=descend5[0];rows=9;break;case 'j':t=descend5[1];rows=9;break;case 'p':t=descend5[2];rows=9;break;
  case 'q':t=descend5[3];rows=9;break;case 'y':t=descend5[4];rows=9;break;default:break;}
 if(!t){if(c>='a'&&c<='z')t=lower5[c-'a'];else if(c>='A'&&c<='Z')t=upper5[c-'A'];else if(c>='0'&&c<='9')t=upper5[26+c-'0'];
  else{for(int j=0;j<7;j++)r[j]=(unsigned char)small_bits(c,j);t=r;}}
 unsigned c0=0,c1=0,c2=0,c3=0,c4=0;
 for(unsigned j=0;j<rows;j++){unsigned b=t[j];if(!b)continue;unsigned m=1u<<j;
  if(b&16)c0|=m;if(b&8)c1|=m;if(b&4)c2|=m;if(b&2)c3|=m;if(b&1)c4|=m;}
 col[0]=(uint16_t)c0;col[1]=(uint16_t)c1;col[2]=(uint16_t)c2;col[3]=(uint16_t)c3;col[4]=(uint16_t)c4;
 return c0|c1|c2|c3|c4;
}
void small_draw(int x,int y,const char *s,uint16_t color){
 if(x<0||y<0||y>160-SMALL_HEIGHT-SMALL_DESCENT)return;
 int x1=x;unsigned n=0;
 while(*s&&x1<236){
  uint32_t c=base_letter(text_next(&s));if(!c)continue;
  small_cols(c,cols+n);cols[n+5]=0;n+=SMALL_ADVANCE;x1+=SMALL_ADVANCE;
 }
 if(x+(int)n>240)n=(unsigned)(240-x);
 if(M4){unsigned f=gfx_pixel(color);for(unsigned k=0;k<n;k++)if(cols[k])col8(x+(int)k,y,cols[k],f,0,0);gfx_mark(y,y+SMALL_HEIGHT+SMALL_DESCENT);return;}
 blit_cols(VRAM+y*240+x,cols,n,color,0,0);
}

/* ---- folding (port of GBAVocab dictionary_fold::Iterator) ---- */
void fold_init(struct Fold *f,const char *s){f->p=s;f->latin=0;}
/* 12.0: one code point folded (the folder listing folds names it holds as
 * UTF-16). 0: dropped, a combining accent after a Latin letter. */
uint32_t fold_point(struct Fold *f,uint32_t c){
 if(f->latin&&c>=0x300&&c<=0x36f)return 0;
 if(c>=0xc0&&c<=0x1eff){unsigned lo=0,hi=FOLD_MAPPINGS;
  while(lo<hi){unsigned m=(lo+hi)/2;if(fold_mappings[m].from<c)lo=m+1;else hi=m;}
  if(lo<FOLD_MAPPINGS&&fold_mappings[lo].from==c)c=fold_mappings[lo].to;}
 if(c>='A'&&c<='Z')c+=32;
 else if((c>=0x391&&c<=0x3a9)||(c>=0x410&&c<=0x42f))c+=32;   /* Greek, Cyrillic capitals */
 else if(c>=0x400&&c<=0x40f)c+=80;
 f->latin=c>='a'&&c<='z';return c;
}
uint32_t fold_next(struct Fold *f){
 for(;;){
  uint32_t c=text_next(&f->p);
  if(!c){f->latin=0;return 0;}
  if((c=fold_point(f,c)))return c;
 }
}
int fold_compare(const char *a,const char *b,int prefix){
 struct Fold x,y;fold_init(&x,a);fold_init(&y,b);
 for(;;){uint32_t q=fold_next(&y);if(prefix&&!q)return 0;uint32_t p=fold_next(&x);if(p!=q)return p<q?-1:1;if(!p)return 0;}
}
size_t fold_key(char *d,size_t cap,const char *s){
 struct Fold f;fold_init(&f,s);size_t n=0;
 for(;;){uint32_t c=fold_next(&f);if(!c)break;size_t k=utf8_put(d+n,cap-n,c);if(!k)break;n+=k;}
 for(size_t i=n;i<cap;i++)d[i]=0;
 return n;
}
