/* Halim Jarrar, 2026. GPL-3.0-or-later with the additional permission described in NOTICES.md (linking with the Helix MP3 decoder). gbamp3 v0.8.2 user interface (v2.1: mode 4 bitmap, every screen).
 * White background, black text, light-blue selected row, centered 5x7 headers,
 * SuperFW 16 px primary text. Screens form a stack; lists share one widget. */
#include <string.h>
#include "player_api.h"
#include "tags.h"
#include "player.h"
#include "input.h"
#include "text.h"
#include "library.h"
#include "playlist.h"
#include "folder.h"
#include "writer_glue.h"
#include "gfx.h"

#define VRAM ((volatile uint16_t*)0x06000000)
#define C_WHITE 0x7fff
#define C_BLACK 0x0000
#define C_BLUE 0x7f55   /* RGB(170,210,255) */
#define C_GREY 0x4210
#define C_LIGHT 0x6318
#define C_DIM 0x56b5
#define C_SEP C_GREY   /* 11.0 separator line, as gbavocab's settings line */
#define ROW_Y 16
#define ROW_H 17
#define ROWS 8
#define DEPTH 12
enum {T_HOME,T_NOW,T_SETTINGS,T_ABOUT,T_SONGS,T_GROUPS,T_ARTIST,T_FOLDER,T_PLAYLISTS,T_PLAYLIST,
      T_ADDTO,T_OPTIONS,T_PLOPTIONS,T_CONFIRM,T_INFO,T_NAME,T_SEARCH,T_SECRET,T_RESTART,
      T_COPTIONS};   /* v1.9: options of an album, artist, genre or folder */
enum {F_FOLDER=1,F_PLAY=2,F_GREY=4,F_SEP=8};   /* 11.0 F_SEP: a grey line on the row's top pixel line (after + New Playlist / + Add Songs) */
enum {NAME_CREATE,NAME_CREATE_ADD,NAME_RENAME,NAME_FILE,NAME_TAG};   /* v1.9 NAME_FILE: rename a song file or folder; v1.9.1 NAME_TAG: album / artist / genre */
struct Screen {
 uint8_t type,aux,list_rows,pad;
 uint32_t sel,top,count;
 uint32_t p0,p1,p2,p3;
 char title[PLAYLIST_NAME];
 char path[STORAGE_PATH];
 uint8_t has_src;   /* options: opt_src holds the list it was opened from */
};
/* Only one Song Options and one Search screen can be on the stack at a time. */
static struct QueueSource opt_src;static uint32_t opt_src_index;
/* v1.9: the songs an options screen acts on (album, artist, genre, folder,
 * playlist or one song): one at a time, like opt_src. */
enum {CK_ALBUM,CK_ARTIST,CK_GENRE,CK_FOLDER,CK_PLAYLIST,CK_SONG};
enum {CO_PLAY,CO_SHUFFLE,CO_NEXT,CO_ADD,CO_INFO,CO_RENAME,CO_DELETE,CO_CONVERT};   /* v2.0 Convert */
static struct QueueSource coll_src;static uint8_t coll_kind;static uint32_t coll_albums;static char coll_path[STORAGE_PATH];
/* v2.0 conversion batches (see "v2.0 conversion" below). */
enum {CB_QUEUE,CB_FOLDER,CB_ALL};
struct ConvBatch {uint8_t kind,mode;const struct QueueSource *src;const char *folder;uint32_t n,ms;};   /* src: coll_src; folder: coll_path */
static struct ConvBatch cb,cbt;   /* cb: the batch being confirmed / run; cbt: visibility checks */
static int conv_source(const char *path);
static unsigned conv_all_shown(void);
static unsigned coll_convertible(void);
static struct ConvBatch *coll_batch(struct ConvBatch *b);
static void conv_confirm(unsigned levels);
static char search_query[64];
static struct Screen stack[DEPTH];
static unsigned depth,full_dirty,row_dirty,locked,frame_count,message_frames,search_clear;
/* v2.2: after unlocking, every button is ignored until all of them have been
 * up for UNLOCK_QUIET frames: a bouncing START contact (down-up-down within a
 * frame or two on real buttons) must not count as a second START press. */
#define UNLOCK_QUIET 6
/* v2.2 Secret Settings > Hold START to unlock: START held this many frames
 * unlocks the lock screen (sleep: player_sleep waits for it). */
#define UNLOCK_HOLD 60
static unsigned unlock_guard,unlock_quiet;
static unsigned restart_failed;
static const char *message;
static struct Screen *top(void){return &stack[depth-1];}
/* v1.6 letter box: a 42 x 41 box centred on the list (x 99-140, y 55-95),
 * drawn in a 64 x 64 16-colour sprite (tiles 960-1023 at 0x06017800, past
 * the staging rows). As a sprite it lies over the list: the rows are painted
 * whole and move under it, and it fades out with hardware alpha blending. */
#define BOX_X 99
#define BOX_Y 55
#define BOX_W 42
#define BOX_H 41
#define BOX_FADE 12    /* fade-out: 0.2 s at 60 Hz */
#define C_BOX 0x2108   /* RGB(64,64,64) */
#define BOX_TILES ((volatile uint16_t*)0x06017800)
#define OAM ((volatile uint16_t*)0x07000000)
#define OBJ_PALETTE ((volatile uint16_t*)0x05000200)
#define REG_BLDCNT (*(volatile uint16_t*)0x04000050)
#define REG_BLDALPHA (*(volatile uint16_t*)0x04000052)
static unsigned box_on,box_frames,box_fade;   /* box_frames: video frames left (0: kept while L is held); box_fade: fade frames left */
static char box_label[8];
/* v1.6: the entry drawn selected in a long list and whether the marquee has
 * moved its text; the display follows s->sel / s->top (see present()). */
static uint32_t hl_sel;
static unsigned rc_need,rc_unchecked;   /* rc_unchecked: playlist rows whose song file is not checked yet */   /* rc_need: long-list rows whose text is read when they are painted */
static int long_list(const struct Screen *s);
static void ensure_visible(struct Screen *s);
static void box_off(void);
/* Song lists: a permutation range, or (v1.2) Search in word mode. */
static int list_song_id(const struct Screen *s,uint32_t i,uint32_t *id){
 if(s->type==T_SEARCH&&s->p2)return library_word_song(s->p1,s->p3,s->p0,i,id);
 return library_perm(s->p0,s->p1+i,id);
}
static void list_source(struct QueueSource *q,const struct Screen *s){
 if(s->type==T_SEARCH&&s->p2){q->kind=QUEUE_WORDS;q->perm=(uint8_t)s->p0;q->start=s->p1;q->playlist=s->p3;}
 else{q->kind=QUEUE_LIBRARY;q->perm=(uint8_t)s->p0;q->start=s->p1;}
 q->count=s->count;q->generation=library_generation();
}

/* ---------- drawing primitives ---------- */
/* Bulk stores use DMA3 one row at a time (at most 120 words, ~600 cycles of
 * bus hold), far cheaper than Thumb stores fetched from cartridge ROM. */
#define DMA3SAD (*(volatile uint32_t*)0x040000D4)
#define DMA3DAD (*(volatile uint32_t*)0x040000D8)
#define DMA3CNT (*(volatile uint32_t*)0x040000DC)
static void dma_words(volatile void *dst,const volatile void *src,unsigned words,unsigned fixed){
 DMA3SAD=(uint32_t)src;DMA3DAD=(uint32_t)dst;DMA3CNT=words|(fixed?0x85000000u:0x84000000u);
}
static void fill(int x,int y,int w,int h,uint16_t c){gfx_fill(x,y,w,h,c);}   /* the hidden page (v2.1: mode 4 only) */
static void header(const char *t){fill(0,0,240,ROW_Y,C_WHITE);small_draw((240-(int)small_width(t))/2,5,t,C_BLACK);}
/* v1.8: on white, painted opaque (every caller has a white area there): the
 * transparent mode-4 path writes pixel by pixel from ROM and took about a
 * frame per line. */
static void center_text(int y,const char *t,uint16_t c){unsigned w=text_width(t);text_draw_bg((240-(int)w)/2,y,0,240,t,0,c,C_WHITE);}
static void icon_play(int x,int y,uint16_t c){for(int j=0;j<9;j++){int w=j<5?j+1:9-j;fill(x,y+j,w,1,c);}}
static void icon_pause(int x,int y,uint16_t c){fill(x,y,3,9,c);fill(x+5,y,3,9,c);}
static void icon_folder(int x,int y,uint16_t c,uint16_t bg){fill(x,y,4,2,c);fill(x,y+2,10,7,c);fill(x+1,y+3,8,5,bg);}   /* v1.6: inside in the row colour */
static void icon_note(int x,int y,uint16_t c){fill(x+10,y,2,18,c);fill(x+12,y,6,2,c);fill(x+12,y+2,3,2,c);fill(x+4,y+15,7,5,c);fill(x+5,y+14,5,7,c);}
/* Text truncated with an ellipsis to fit [x, x+w). */
static void text_fit_draw(int x,int y,int w,const char *s,uint16_t c){
 static char buf[160];unsigned whole;size_t n=text_fit(s,(unsigned)w,&whole);
 if(whole){text_draw_bg(x,y,x,x+w,s,0,c,C_WHITE);return;}   /* v1.8: opaque on white, as center_text */
 if(n>sizeof(buf)-4)n=sizeof(buf)-4;memcpy(buf,s,n);memcpy(buf+n,"...",4);text_draw_bg(x,y,x,x+w,buf,0,c,C_WHITE);
}

/* ---------- marquee (selected row / Now Playing title) ---------- */
static int mq_off,mq_wait,mq_hold,mq_w=-1;
static void marquee_reset(void){mq_off=0;mq_wait=60;mq_hold=0;mq_w=-1;}
/* 0 no change, 1 scrolled one pixel, 2 back at the start (redraw). Scrolls to
 * the end, holds, restarts. */
static int marquee_step(const char *s,unsigned avail){
 if(mq_w<0)mq_w=(int)text_width(s);
 if((unsigned)mq_w<=avail)return 0;
 if(mq_wait){mq_wait--;return 0;}
 if(mq_w-(int)avail<=mq_off){if(++mq_hold<60)return 0;mq_hold=0;mq_off=0;mq_wait=60;return 2;}
 if(frame_count&1)return 0;
 mq_off++;return 1;
}
/* Apply a step to the box [x0,x1) at y: the text redrawn from its offset
 * (v1.7: 8-bit pixels share halfwords, so no 1-pixel shift). */
static void marquee_apply(int r,const char *s,int x0,int x1,int y,uint16_t fg,uint16_t bg){
 if(r==3){text_draw_bg(x0,y,x0,x1,s,1,fg,bg);return;}   /* v1.6: first move of a row shown with "..." */
 text_draw_bg(x0,y,x0,x1,s,r==2?0:(unsigned)mq_off,fg,bg);
}

/* ---------- row provider ---------- */
static struct LibSong song;
static struct LibGroup group;
static struct PlaylistEntry entry;
static struct DirCursor dirs,songs;
static char name_buf[STORAGE_PATH];
static const char *const home_rows[]={"Now Playing","Playlists","Folders","Search","Artists","Albums","Genres","Songs"};
static const char *const field_names[]={"Song Title","Filename","Album","Artist"};
static const char *const mode_names[]={"Normal","Repeat All","Repeat One","Shuffle"};
/* Playback Mode first; Update Library, Output, Volume Boost, Search Field, About follow. */
static const char *const settings_rows[]={"Playback Mode","Update Library","Output","Volume Boost","Search Field","About"};
static int options_current(const struct Screen *s);
static int menu_kind(const struct Screen *s){
 switch(s->type){case T_HOME:return MENU_HOME;case T_SETTINGS:return MENU_SETTINGS;
  case T_OPTIONS:return MENU_OPTIONS;case T_PLOPTIONS:return MENU_PLAYLIST_OPTIONS;default:return -1;}
}
static unsigned menu_id(const struct Screen *s,unsigned i){
 int kind=menu_kind(s);if(kind<0)return i;
 menu_order_files=player_file_ops();   /* v1.9: Song Options Rename / Delete */
 menu_order_convert=(s->type==T_SETTINGS&&conv_all_shown()?1u:0u)|(s->type==T_OPTIONS&&conv_source(s->path)?2u:0u)|(s->type==T_PLOPTIONS&&s->p2?4u:0u);   /* v2.0 */
 uint8_t ids[8];unsigned n=menu_order_visible(player_menus(),(unsigned)kind,
  s->type==T_OPTIONS&&s->p2,s->type==T_OPTIONS&&s->aux,ids);
 return i<n?ids[i]:0;
}
static unsigned menu_position(const struct Screen *s,unsigned id){
 for(unsigned i=0;i<s->count;i++)if(menu_id(s,i)==id)return i;
 return 0;
}
static unsigned dir_count(void){int n=folder_count(&dirs);return n>0?(unsigned)n:0;}
static const char *group_name(unsigned kind,const struct LibSong *s){
 return kind==GROUP_ARTISTS?library_artist(s->artist):kind==GROUP_GENRES?library_genre(s->genre):library_album(s->album);
}
static int options_current(const struct Screen *s){const struct NowPlaying *n=player_now();return player_loaded()&&n->valid&&n->key==storage_key(s->path);}
static unsigned option_count(const struct Screen *s){unsigned f=player_file_ops();return (s->p2?3u:4u)+(s->aux?1u:0u)+(f&1)+(f>>1&1)+(conv_source(s->path)?1u:0u);}
/* v1.9 collection options: Play, Shuffle Play, Play Next, Add to Playlist,
 * Info; Rename (folders) and Delete when switched on in Secret Settings. */
static unsigned coll_rename(void){return (coll_kind==CK_FOLDER||coll_kind<=CK_GENRE)&&(player_file_ops()&1);}   /* v1.9.1: albums, artists, genres too */
static unsigned coll_conv;   /* v2.0: the open collection options show Convert (after Info) */
static unsigned coll_count(void){return 5u+coll_conv+coll_rename()+(player_file_ops()>>1&1);}
static unsigned coll_id(uint32_t i){if(i<5)return i;i-=5;if(coll_conv){if(!i)return CO_CONVERT;i--;}return i==0&&coll_rename()?CO_RENAME:CO_DELETE;}
static const char *conv_label(void){return player_conversion()==2?"Convert to MP3":"Convert to GAC";}
static const char *option_label(const struct Screen *s,unsigned id){
 static const char *const o[]={"Play","Play Next","Add to Playlist","Song Info","Remove from Playlist","Rename","Delete"};
 if(id==OPTION_CONVERT)return conv_label();
 return id==OPTION_PLAY&&s->p2?"Play / Pause":o[id];
}
/* v1.6 Show songs as File Names: the file name without ".mp3" / ".gac". */
static const char *file_label(const char *path){
 char *b=name_buf;const char *n=path;   /* name_buf: row() and entry_key() copy the label at once */for(const char *p=path;*p;p++)if(*p=='/')n=p+1;
 unsigned k=strlen(n);if(k>=sizeof(name_buf))k=sizeof(name_buf)-1;memmove(b,n,k);b[k]=0;
 if(k>4&&b[k-4]=='.'){char e[4];for(unsigned j=0;j<3;j++){char c=b[k-3+j];e[j]=c>='A'&&c<='Z'?(char)(c+32):c;}e[3]=0;
  if(!strcmp(e,"mp3")||!strcmp(e,"gac"))b[k-4]=0;}
 return b;
}
static const char *song_label(const struct LibSong *s){return player_song_files()?file_label(s->path):s->title;}
/* v1.8: "+ New Playlist" is the last row of Playlists / Add to Playlist, or
 * the first (Secret Settings reordering, A + Up); pl_index: the playlist of
 * row i. */
static uint32_t new_row(const struct Screen *s){return player_new_playlist_top()?0:s->count-1;}
/* 11.0 Add to Playlist from a song in a playlist leaves that playlist out
 * (p0: its index + 1, 0 none). */
static uint32_t pl_index_of(const struct Screen *s,uint32_t i){uint32_t x=player_new_playlist_top()?i-1:i;if(s->type==T_ADDTO&&s->p0&&x>=s->p0-1)x++;return x;}
#define pl_index(i) pl_index_of(s,i)
/* 11.0: "+ Add Songs" in every playlist: the last row, or the first (Secret
 * Settings reordering, A + Up; own position, new_playlist_top bit 2). */
static unsigned add_top(void){return player_add_songs_top();}
static uint32_t add_row(const struct Screen *s){return add_top()?0:s->count-1;}
/* 11.0: playlist entries found missing while the list is shown are hidden
 * (Update Library removes them from the playlist): their entry indices,
 * ascending, for the open playlist pl_hide_id. */
#define PL_HIDE 128
static uint32_t pl_hide_id,pl_hide[PL_HIDE];static unsigned pl_hide_n;
static void pl_hide_reset(uint32_t id){pl_hide_id=id;pl_hide_n=0;}
/* Entry index of song row `row` (not the + Add Songs row) of playlist screen s. */
static uint32_t pl_entry(const struct Screen *s,uint32_t row){
 uint32_t e=row-(add_top()?1u:0u);
 if(s->p0==pl_hide_id)for(unsigned k=0;k<pl_hide_n&&pl_hide[k]<=e;k++)e++;
 return e;
}
static unsigned pl_hide_add(uint32_t e){
 if(pl_hide_n==PL_HIDE)return 0;unsigned k=pl_hide_n;while(k&&pl_hide[k-1]>e){pl_hide[k]=pl_hide[k-1];k--;}
 pl_hide[k]=e;pl_hide_n++;return 1;
}
/* Entry `e` of playlist `id` was removed: later hidden entries move up. */
static void pl_hide_removed(uint32_t id,uint32_t e){
 if(id!=pl_hide_id)return;unsigned n=0;
 for(unsigned k=0;k<pl_hide_n;k++){if(pl_hide[k]==e)continue;pl_hide[n++]=pl_hide[k]>e?pl_hide[k]-1:pl_hide[k];}
 pl_hide_n=n;
}
/* The separator: on the first playlist (song) row below the action row on
 * top, or on the action row at the bottom; none without other rows. */
static int sep_row(const struct Screen *s,uint32_t i,unsigned on_top){return s->count>1&&i==(on_top?1u:s->count-1);}
/* Fill row text/flags for list index i of screen s. */
static void row(const struct Screen *s,uint32_t i,char *out,unsigned cap,uint8_t *flag,unsigned *key){
 *flag=0;*key=0;out[0]=0;
 const char *t="";
 switch(s->type){
  case T_HOME:t=home_rows[menu_id(s,i)];break;
  case T_SETTINGS:
   i=menu_id(s,i);
   if(i==2){t=player_stereo()?"Output: Stereo":"Output: Mono";break;}
   if(i==3){t=player_boost_on()?"Volume Boost: On":"Volume Boost: Off";break;}
   if(i==6){t=player_song_files()?"Show songs as: File Names":"Show songs as: Titles";break;}
   if(i==SETTING_CONVERT_ALL){t="Convert all";break;}   /* v2.0 */
   if(i==4||i==0){const char *v=i==4?field_names[player_search_field()]:mode_names[player_mode()];const char *l=i==4?"Search Field: ":"Playback Mode: ";
    unsigned a=strlen(l),b=strlen(v);if(a+b<cap){memcpy(out,l,a);memcpy(out+a,v,b+1);}return;}
   t=settings_rows[i];break;
  case T_ARTIST:t=i?"Albums":"All Songs";break;
  case T_OPTIONS:t=option_label(s,menu_id(s,i));break;
  case T_PLOPTIONS:{static const char *const o[]={"Play","Rename","Delete Playlist","Shuffle Play","Play Next","Add to Playlist","Info"};unsigned id=menu_id(s,i);t=id==PLOPT_CONVERT?conv_label():o[id];break;}
  case T_COPTIONS:{static const char *const o[]={"Play","Shuffle Play","Play Next","Add to Playlist","Info","Rename","Delete"};unsigned id=coll_id(i);t=id==CO_CONVERT?conv_label():o[id];break;}
  case T_SECRET:{static const char *const types[]={"Show Songs: MP3 + GAC","Show Songs: only MP3","Show Songs: only GAC"};
   t=s->p0==i?"Sure?":i==0?(player_menus()->enabled&MENU_ORDER_ON?"Reorder menus (A+DPAD): On":"Reorder menus (A+DPAD): Off"):
   i==1?(player_auto_update()?"Auto-update library: On":"Auto-update library: Off"):
   i==2?types[player_song_types()]:i==3?(player_letter_box()==1?"Letter Box: On":player_letter_box()==2?"Letter Box: Only L+DPAD":"Letter Box: Off"):
   i==4?(player_file_ops()&1?"Rename files: On":"Rename files: Off"):i==5?(player_file_ops()&2?"Delete files: On":"Delete files: Off"):
   i==6?(player_conversion()==1?"Conversion: MP3>GAC":player_conversion()==2?"Conversion: GAC>MP3":"Conversion: Off"):
   i==7?(player_hold_unlock()?"Hold START to unlock: On":"Hold START to unlock: Off"):
   i==8?"Delete configuration":"Delete library index";break;}
  case T_CONFIRM:t=i?"Yes":"No";break;
  case T_SONGS:case T_SEARCH:{uint32_t id;
   if(list_song_id(s,i,&id)||library_song(id,&song)){t="?";break;}
   t=song_label(&song);*key=storage_key(song.path);break;}
  case T_GROUPS:
   if(library_group(s->p0,s->p1+i,&group)||library_song(group.rep,&song)){t="?";break;}
   t=group_name(s->p0,&song);break;
  case T_FOLDER:{unsigned d=dir_count();
   if(i<d){if(folder_name(&dirs,i,name_buf,sizeof(name_buf))){t="?";break;}*flag=F_FOLDER;t=name_buf;break;}
   if(folder_name(&songs,i-d,name_buf,sizeof(name_buf))){t="?";break;}
   static char full[STORAGE_PATH];if(folder_join(full,s->path,name_buf))*key=storage_key(full);t=name_buf;break;}
  case T_PLAYLISTS:case T_ADDTO:
   if(sep_row(s,i,player_new_playlist_top()))*flag|=F_SEP;
   if(i==new_row(s)){t="+ New Playlist";break;}
   if(playlist_info(pl_index(i),0,name_buf))t="?";else t=name_buf;break;
  case T_PLAYLIST:
   if(sep_row(s,i,add_top()))*flag|=F_SEP;
   if(i==add_row(s)){t="+ Add Songs";break;}
   if(playlist_entry_quick(s->p0,pl_entry(s,i),&entry)){t="?";break;}   /* v1.6: file checked later (check_rows) */
   t=player_song_files()&&entry.path[0]?file_label(entry.path):entry.title[0]?entry.title:"?";if(!entry.valid)*flag|=F_GREY;else *key=storage_key(entry.path);break;
  default:break;
 }
 unsigned n=strlen(t);if(n>=cap)n=cap-1;memcpy(out,t,n);out[n]=0;
 if(*key){const struct NowPlaying *np=player_now();if(player_loaded()&&np->valid&&np->key==*key)*flag|=F_PLAY;}
}
static int is_list(const struct Screen *s){return s->type!=T_NOW&&s->type!=T_ABOUT&&s->type!=T_INFO&&s->type!=T_NAME;}
/* v2.0: the conversion question has three lines of text above No / Yes. */
static int conv_question(const struct Screen *s){return s->type==T_CONFIRM&&s->aux==2;}
static int row_y0(const struct Screen *s){return s->type==T_SEARCH?ROW_Y+ROW_H+1:conv_question(s)?ROW_Y+3*ROW_H+4:ROW_Y;}
static unsigned rows_visible(const struct Screen *s){return s->type==T_SEARCH?ROWS-1:conv_question(s)?2:ROWS;}

/* Page cache for the visible rows. */
static struct {const struct Screen *owner;uint32_t top,gen;unsigned valid;char text[ROWS][META_TITLE];uint8_t flag[ROWS];unsigned key[ROWS];} rc;
static void cache_page(struct Screen *s){
 if(rc.valid&&rc.owner==s&&rc.top==s->top)return;
 rc.owner=s;rc.top=s->top;rc.valid=1;rc_need=0;rc_unchecked=s->type==T_PLAYLIST?(1u<<rows_visible(s))-1:0;
 for(unsigned r=0;r<rows_visible(s);r++){uint32_t i=s->top+r;if(i<s->count){row(s,i,rc.text[r],META_TITLE,&rc.flag[r],&rc.key[r]);player_service();}else{rc.text[r][0]=0;rc.flag[r]=0;}}
}
static void invalidate_rows(void){rc.valid=0;rc_need=0;rc_unchecked=0;}
static void refresh_play_flags(void){
 const struct NowPlaying *np=player_now();
 for(unsigned r=0;r<ROWS;r++){uint8_t f=rc.flag[r]&~F_PLAY;if(rc.key[r]&&player_loaded()&&np->valid&&np->key==rc.key[r])f|=F_PLAY;if(f!=rc.flag[r]){rc.flag[r]=f;row_dirty|=1u<<r;}}
}
static void marquee_row(struct Screen *s,unsigned r){
 if(row_dirty&(1u<<r))return;   /* not painted yet */
 if(box_on)return;              /* v1.5: no marquee while the letter box shows */
 int y=row_y0(s)+(int)r*ROW_H;uint8_t f=rc.flag[r];int right=f&(F_PLAY|F_FOLDER)?222:234;
 int step=marquee_step(rc.text[r],(unsigned)(right-6));
 if(step==1&&mq_off==1)step=3;
 if(step)marquee_apply(step,rc.text[r],6,right,y+1,(f&F_GREY)?C_DIM:C_BLACK,C_BLUE);
}
/* v1.6: one list row at line y (a visible row or a staging row), every pixel
 * written once with its final colour: top line, margins, text box with its
 * background, icon. Painting the background first and the text afterwards
 * could show an empty coloured bar for a frame. A selected row looks like an
 * unselected one on light blue ("..." included) until its marquee starts,
 * so a list move can switch the bar by recolouring (scroll.c). */
static void paint_row(int y,const char *text,uint8_t f,int sel,int valid){
 uint16_t bg=sel?C_BLUE:C_WHITE;
 fill(0,y,240,1,valid&&(f&F_SEP)?C_SEP:bg);fill(0,y+1,6,ROW_H-1,bg);   /* 11.0: the separator stays grey above a selected row */
 if(!valid){fill(6,y+1,234,ROW_H-1,bg);return;}
 int right=f&(F_PLAY|F_FOLDER)?222:234;uint16_t col=(f&F_GREY)?C_DIM:C_BLACK;
 if(sel&&mq_off&&text_width(text)>(unsigned)(right-6))text_draw_bg(6,y+1,6,right,text,(unsigned)mq_off,col,bg);
 else{static char buf[160];unsigned whole;size_t n=text_fit(text,(unsigned)(right-6),&whole);const char *p=text;
  if(!whole){if(n>sizeof(buf)-4)n=sizeof(buf)-4;memcpy(buf,p,n);memcpy(buf+n,"...",4);p=buf;}
  text_draw_bg(6,y+1,6,right,p,0,col,bg);}
 fill(right,y+1,240-right,ROW_H-1,bg);
 if(f&F_PLAY)icon_play(226,y+4,C_BLACK);else if(f&F_FOLDER)icon_folder(226,y+4,C_BLACK,bg);
}
/* v1.6: a playlist row is shown at once; whether its song file still exists
 * is checked afterwards, one visible row per UI frame while the list is idle
 * and enough audio is buffered (the check searches the song's folder on the
 * card). A missing song turns grey. Playback checks every file itself. */
static void check_rows(struct Screen *s){
 unsigned v=rows_visible(s);
 if(!rc_unchecked||s->type!=T_PLAYLIST||rc_need||(row_dirty&((1u<<v)-1))||!rc.valid||rc.owner!=s||(player_is_playing()&&!player_scroll_ok()))return;   /* 11.0: also with no music loaded */
 unsigned r=0;while(!(rc_unchecked>>r&1))r++;rc_unchecked&=~(1u<<r);
 if(r>=v||rc.top+r>=s->count||rc.top+r==add_row(s))return;
 uint32_t e=pl_entry(s,rc.top+r);
 if(!playlist_entry_quick(s->p0,e,&entry)&&entry.path[0]&&!playlist_exists(entry.path)){
  /* 11.0: a missing song is hidden (grey only when too many are hidden). */
  if(s->p0!=pl_hide_id)pl_hide_reset(s->p0);
  if(pl_hide_add(e)){uint32_t row=rc.top+r;s->count--;if(s->sel>row||s->sel>=s->count)s->sel--;
   if(s->top+v>s->count)s->top=s->count>v?s->count-v:0;ensure_visible(s);invalidate_rows();marquee_reset();return;}
  rc.flag[r]=(uint8_t)((rc.flag[r]&~F_PLAY)|F_GREY);rc.key[r]=0;row_dirty|=1u<<r;}
}
static void draw_row(struct Screen *s,unsigned r){
 int ll=long_list(s);uint32_t i=(ll?rc.top:s->top)+r,hl=ll?hl_sel:s->sel;
 if(rc_need&(1u<<r)){rc_need&=~(1u<<r);
  if(i>=s->count){rc.text[r][0]=0;rc.flag[r]=0;rc.key[r]=0;}
  else{row(s,i,rc.text[r],META_TITLE,&rc.flag[r],&rc.key[r]);player_service();
   if(s->type==T_PLAYLIST)rc_unchecked|=1u<<r;}}   /* a card read may wait: keep audio fed */
 int sel=i==hl&&i<s->count;
 paint_row(row_y0(s)+(int)r*ROW_H,rc.text[r],rc.flag[r],sel,i<s->count);
}
static const char *empty_text(const struct Screen *s){
 if(player_fatal()==200&&s->type!=T_PLAYLIST)return "No SD card";   /* card missing or unreadable */
 switch(s->type){case T_PLAYLIST:return "Empty playlist";case T_SEARCH:return library_count()?"No results":"No music found";
  case T_FOLDER:return strcmp(s->path,"0:/gbamp3")?"Empty folder":"No music found";default:return "No music found";}
}
/* Rows are painted progressively (each row paints its own background), so
 * no UI frame blocks the decoder for long and every frame reads the keys. */
#define ROWS_PER_FRAME 3
/* v1.7: with 1/4 s of audio buffered (or none playing) all rows are painted
 * in one frame, so a jump shows sooner; the picture appears whole either way. */
static void draw_rows(struct Screen *s){
 /* v1.8: behind the blank of a mode change (leaving Now Playing) every row
  * is painted now, the music topped up between rows. */
 unsigned blank=gfx_blanked(),done=0,most=blank||player_read_ahead_ok()?ROWS:ROWS_PER_FRAME;
 for(unsigned r=0;r<rows_visible(s)&&done<most;r++)if(row_dirty&(1u<<r)){row_dirty&=~(1u<<r);draw_row(s,r);done++;if(blank)player_service_floor();}
}
static unsigned back_from_now,last_full=~0u;   /* v1.8: the screen drawn before this full redraw was Now Playing */
static void draw_list(struct Screen *s,int full){
 /* v1.6: a long list opens with its header at once; rows are read when they
  * are painted, 3 per frame (reading all 8 first kept the old screen up for
  * up to a third of a second in a playlist while MP3 plays). */
 /* v1.8: back from Now Playing the page cache still holds these rows (only
  * the play mark may have moved): no card reads behind the blank. */
 if(full&&long_list(s)&&!(back_from_now&&rc.valid&&rc.owner==s&&rc.top==s->top)){rc.owner=s;rc.top=s->top;rc.valid=1;rc_need=(1u<<rows_visible(s))-1;rc_unchecked=0;}
 else cache_page(s);
 if(full&&back_from_now)refresh_play_flags();
 if(full){hl_sel=s->sel;
  if(s->type==T_HOME){fill(0,0,240,ROW_Y,C_WHITE);small_draw((240-(int)small_width("gbamp3"))/2,5,"gbamp3",C_BLACK);}
  else header(s->title);
  if(conv_question(s)){void draw_conv_confirm(void);draw_conv_confirm();}   /* v2.0 */
  if(!s->count){fill(0,ROW_Y,240,160-ROW_Y,C_WHITE);row_dirty=0;
   center_text(72,empty_text(s),C_BLACK);if(s->type==T_SEARCH||s->type==T_SONGS)if(!library_count()){static const char h[]="Select: Settings > Update Library";small_draw((240-(int)small_width(h))/2,100,h,C_GREY);}return;}
  /* Clear below the last entry now (one DMA pass); entries follow, a few per frame. */
  unsigned used=s->count-s->top<rows_visible(s)?s->count-s->top:rows_visible(s);
  int bottom=row_y0(s)+(int)used*ROW_H;fill(0,bottom,240,160-bottom,C_WHITE);
  row_dirty=(1u<<used)-1;rc_need&=row_dirty;   /* 11.0: rows past the end are never read (a short playlist was never checked) */
 }
 draw_rows(s);
}

/* ---------- Now Playing ---------- */
/* The song block (cover, title/artist/album, progress, time) sits NP_DY lower
 * so it is centred between the header and the technical line. */
#define NP_DY 14
static unsigned np_key,np_sec,np_playing,np_mode,np_stereo,np_boost,np_error;
static void mmss(char *o,unsigned s){unsigned m=s/60;char t[12];unsigned n=0;do{t[n++]=(char)('0'+m%10);m/=10;}while(m);unsigned k=0;while(n)o[k++]=t[--n];o[k++]=':';o[k++]=(char)('0'+(s%60)/10);o[k++]=(char)('0'+s%10);o[k]=0;}
static void rate_text(char *o,unsigned hz){unsigned t=(hz+50)/100;unsigned w=t/10,k=0;char d[8];unsigned n=0;do{d[n++]=(char)('0'+w%10);w/=10;}while(w);while(n)o[k++]=d[--n];o[k++]='.';o[k++]=(char)('0'+t%10);memcpy(o+k," kHz",5);}
static void np_art(const struct NowPlaying *n){
 const volatile unsigned short *art=storage_cover(n->key);
 /* v2.1: 64x64 colour numbers and their palette (gfx_cover). */
 if(art)gfx_cover(8,18+NP_DY,art);
 else{fill(8,18+NP_DY,64,64,C_LIGHT);icon_note(28,40+NP_DY,C_GREY);}
}
/* full: the whole block (new song, play/pause or mode change). Otherwise
 * only the bar and the elapsed time change, and every pixel is written once
 * with its final colour: clearing first made the bar flicker. */
static void np_time(const struct NowPlaying *n,int full){
 unsigned sec=player_elapsed(),total=n->meta.duration_ms/1000;
 char t[16];
 if(full)fill(0,88+NP_DY,240,26,C_WHITE);
 if(n->meta.duration_ms){unsigned w=total?sec*224/total:0;if(w>224)w=224;fill(8,90+NP_DY,(int)w,5,C_BLACK);fill(8+(int)w,90+NP_DY,224-(int)w,5,C_LIGHT);}
 if(full){
  /* The icon shows what A does (v0.9.5): pause bars while playing, play otherwise. */
  if(player_is_playing())icon_pause(8,101+NP_DY,C_BLACK);else icon_play(8,101+NP_DY,C_BLACK);
  if(n->meta.duration_ms){mmss(t,total);small_draw(232-(int)small_width(t),102+NP_DY,t,C_BLACK);}
  const char *m=mode_names[player_mode()];small_draw((240-(int)small_width(m))/2,102+NP_DY,m,C_GREY);
 }else fill(22,100+NP_DY,48,13,C_WHITE);   /* elapsed time only */
 mmss(t,sec);small_draw(22,102+NP_DY,t,C_BLACK);
}
static void np_tech(const struct NowPlaying *n){
 fill(0,140,240,20,C_WHITE);
 static char line[48];char r[16];
 unsigned hz=player_is_playing()||player_elapsed()?player_output_hz():(n->meta.format==FORMAT_GAC?n->meta.rate:32768);
 unsigned mono=player_is_playing()||player_elapsed()?player_output_mono():(!player_stereo()||(n->meta.format==FORMAT_GAC&&n->meta.channels==1));
 rate_text(r,hz);
 const char *f=n->meta.format==FORMAT_GAC?"GAC":"MP3";unsigned k=0;
 memcpy(line,f,3);k=3;memcpy(line+k,"   ",3);k+=3;unsigned rl=strlen(r);memcpy(line+k,r,rl);k+=rl;memcpy(line+k,"   ",3);k+=3;
 const char *c=mono?"Mono":"Stereo";unsigned cl=strlen(c);memcpy(line+k,c,cl+1);k+=cl;
 /* Volume Boost level, only above 0 dB. */
 unsigned b=player_boost_level();if(b){static const char *const db[]={"","   +3dB","   +6dB","   +9dB"};memcpy(line+k,db[b],8);}
 small_draw((240-(int)small_width(line))/2,146,line,C_GREY);
}
static void np_title(const struct NowPlaying *n){
 unsigned w=text_width(n->meta.title);
 text_draw_bg(80,18+NP_DY,80,232,n->meta.title,w>152?(unsigned)mq_off:0,C_BLACK,C_WHITE);
}
/* Artist / album line with its whole [x, x+w) x 16 background painted in the
 * same pass (a track change draws over the previous song's text). */
static void text_fit_draw_bg(int x,int y,int w,const char *s,uint16_t c){
 static char buf[160];unsigned whole;size_t n=text_fit(s,(unsigned)w,&whole);
 if(!whole){if(n>sizeof(buf)-4)n=sizeof(buf)-4;memcpy(buf,s,n);memcpy(buf+n,"...",4);s=buf;}
 text_draw_bg(x,y,x,x+w,s,0,c,C_WHITE);
}
static unsigned np_stage,np_blank;
static void np_step(const struct NowPlaying *n){
 switch(np_stage++){
   case 1:np_art(n);break;
   case 2:np_title(n);break;
   case 3:text_fit_draw_bg(80,40+NP_DY,152,library_artist(n->meta.artist),C_BLACK);break;
   case 4:text_fit_draw_bg(80,62+NP_DY,152,library_album(n->meta.album),C_BLACK);break;
   default:np_time(n,1);np_tech(n);np_sec=player_elapsed();np_playing=player_is_playing();np_mode=player_mode();np_stereo=player_stereo();np_boost=player_boost_level();if(np_error)fill(0,128,240,12,C_WHITE);np_error=0;np_stage=0;break;   /* v1.5: no screen clear left an old error line */
  }
}
static void draw_now(int full){
 const struct NowPlaying *n=player_now();
 unsigned has=player_loaded()&&n->valid;
 if(full||n->key!=np_key){
  /* v1.5: a track change (next / previous / song end) keeps the old song on
   * screen and replaces cover, title, artist, album and time in place, one
   * per frame as before; only entering the screen, or a screen that showed
   * "No song selected", clears first. Clearing on every track change showed
   * a blank screen for a few frames (flicker on real hardware). */
  if(full||!has||np_blank){header("Now Playing");fill(0,ROW_Y,240,160-ROW_Y,C_WHITE);}
  np_key=n->key;marquee_reset();
  if(!has){center_text(72,"No song selected",C_BLACK);np_sec=~0u;np_stage=0;np_blank=1;return;}
  np_blank=0;np_stage=1;
  /* v1.8: entering the screen draws every section now, the music topped up
   * between them (v2.1: on the hidden page; the screen appears complete). */
  if(full)while(np_stage){np_step(n);player_service_floor();}
  return;
 }
 if(!has)return;
 /* One section per UI frame: the full screen is too slow for a single frame
  * while MP3 decoding takes most of the CPU. v2.1: shown when all are drawn
  * (ui_frame): the new song appears whole. */
 if(np_stage){np_step(n);return;} if(player_is_playing()!=np_playing||player_mode()!=np_mode){np_sec=player_elapsed();np_playing=player_is_playing();np_mode=player_mode();np_time(n,1);}
 else if(player_elapsed()!=np_sec){np_sec=player_elapsed();np_time(n,0);}
 if(player_stereo()!=np_stereo||player_boost_level()!=np_boost){np_stereo=player_stereo();np_boost=player_boost_level();np_tech(n);}
 if(player_fatal()!=np_error){np_error=player_fatal();fill(0,128,240,12,C_WHITE);if(np_error){const char *e=player_error_text();small_draw((240-(int)small_width(e))/2,130,e,C_BLACK);}}
 {int step=marquee_step(n->meta.title,152);if(step)marquee_apply(step,n->meta.title,80,232,18+NP_DY,C_BLACK,C_WHITE);}
}

/* ---------- Song Info / About / naming ---------- */
static void draw_coll_info(struct Screen *s);
static void draw_info(struct Screen *s){
 if(s->aux){draw_coll_info(s);return;}   /* v1.9 */
 header("Song Info");fill(0,ROW_Y,240,144,C_WHITE);
 uint32_t id;struct SongMeta *m=0;static struct NowPlaying np;
 if(!library_find(s->path,&id)&&!library_song(id,&song)){
  np.meta.format=song.format;np.meta.rate=song.rate;np.meta.channels=song.channels;np.meta.bitrate=song.bitrate;np.meta.vbr=song.vbr;
  memcpy(np.meta.title,song.title,META_TITLE);memcpy(np.meta.artist,song.artist,META_NAME);memcpy(np.meta.album,song.album,META_NAME);memcpy(np.meta.genre,song.genre,META_GENRE);m=&np.meta;
 }else{const struct NowPlaying *n=player_now();if(n->valid&&!strcmp(n->path,s->path)){np=*n;m=&np.meta;}}
 static const char *const labels[]={"Title","Artist","Album","Genre","File","Format","Rate","Channels"};
 const char *base=s->path;for(const char *p=s->path;*p;p++)if(*p=='/')base=p+1;
 char rate[16]="?";if(m&&m->rate){unsigned r=m->rate;rate_text(rate,r);}
 /* v0.9.8: MP3 bitrate, "MP3, 320 kbps" or "MP3, VBR". */
 static char format[24];strcpy(format,m?(m->format==FORMAT_GAC?"GAC":"MP3"):"?");
 if(m&&m->format==FORMAT_MP3&&(m->vbr||m->bitrate)){
  if(m->vbr)strcpy(format+3,", VBR");
  else{char d[8];unsigned k=0,b=m->bitrate;do{d[k++]=(char)('0'+b%10);b/=10;}while(b);unsigned o=3;format[o++]=',';format[o++]=' ';while(k)format[o++]=d[--k];strcpy(format+o," kbps");}
 }
 const char *v[8]={m?m->title:base,m?library_artist(m->artist):"?",m?library_album(m->album):"?",m?library_genre(m->genre):"?",base,
  format,rate,m?(m->channels==1?"Mono":"Stereo"):"?"};
 for(unsigned i=0;i<8;i++){int y=ROW_Y+(int)i*ROW_H;small_draw(6,y+5,labels[i],C_GREY);text_fit_draw(64,y+1,170,v[i],C_BLACK);player_service();}
}
/* v1.8: one topic per page, plain sentences; the last page opens Secret
 * Settings (A, music stops). */
#define ABOUT_PAGES 8
static const char *const about_titles[ABOUT_PAGES]={"About","Controls","License","MP3 decoder","SD card and files","Text and fonts","More credits","Secret Settings"};
static const char *const about_pages[ABOUT_PAGES][8]={
 {"gbamp3 11.0","(c) 2026 Halim Jarrar","","GPL-3.0-or-later with","Helix linking permission","No warranty.","Source:","github.com/Xinon232/gbamp3"},
 {"Start: Options   Select: Settings","A: Open / Play   B: Back","L+R: Lock   Start: Unlock","Now Playing: Left / Right","tap: prev / next, hold: seek","Up / Down: Volume Boost level","Playlist: hold A + Up / Down","Search: Start + A B L R Up Down"},
 {"gbamp3 is free software under","the GNU GPL version 3 or later,","with an additional permission","for linking with the Helix MP3","decoder (see NOTICES.md).","Contact: halimj.itch.io","gba@halim-jarrar.de","Credits: next pages."},
 {"Helix MP3 decoder","Helix DNA Client technology","included. (c) 1995-2002","RealNetworks, Inc.","All rights reserved.","License: RPSL 1.0","By Jon Recker and Ken Cooke;","Arduino version: Phil Schatzmann"},
 {"FatFs by ChaN (BSD style)","SD card driver from SuperFW","by David Guillen Fandos","License: GPL 3.0 or later","Omega and EverDrive SD drivers","by Halim Jarrar (GPL), after the","Omega kernel (Apache 2.0) and","edio-gba by krikzz (MIT)"},
 {"Text renderer from SuperFW","by David Guillen Fandos","License: GPL 3.0 or later","Fonts: UNSCII and GNU Unifont","","Text input from GBAWriter,","accent folding from GBAVocab","by Halim Jarrar (GPL 3.0+)"},
 {"MP3 encoder: shine","by Gabriel Bouvigne, Pete","Everett, Romain Beauxis and","contributors","License: LGPL 2.0","","Built with devkitPro /","devkitARM."},
 {"Press A to open","Secret Settings.","","Music stops when you press A.","","B: back","",""}};
static void draw_about(struct Screen *s){
 header(about_titles[s->p0]);fill(0,ROW_Y,240,144,C_WHITE);
 for(unsigned i=0;i<8;i++)if(about_pages[s->p0][i][0]){text_fit_draw(6,ROW_Y+1+(int)i*ROW_H,228,about_pages[s->p0][i],C_BLACK);player_service();}
 char p[8]={(char)('1'+s->p0),'/',(char)('0'+ABOUT_PAGES),0};small_draw(234-(int)small_width(p),5,p,C_GREY);
}
static void draw_restart(void){
 header("Restart required");fill(0,ROW_Y,240,160-ROW_Y,C_WHITE);
 center_text(65,restart_failed?"Deletion failed.":"Deletion succeeded.",C_BLACK);
 center_text(88,"Restart system now.",C_BLACK);
}
static void draw_field(int y,const char *t,unsigned caret){
 fill(0,y,240,1,C_LIGHT);fill(0,y+1,6,ROW_H-1,C_LIGHT);fill(234,y+1,6,ROW_H-1,C_LIGHT);
 static char before[64];unsigned n=caret<63?caret:63;memcpy(before,t,n);before[n]=0;
 unsigned cx=text_width(before);int x=6,skip=0;
 if(cx>220){skip=(int)(cx-220);}
 text_draw_bg(x,y+1,6,234,t,(unsigned)skip,C_BLACK,C_LIGHT);
 fill(x+(int)cx-skip,y+1,1,15,C_BLACK);
}
static char status_shown[24];   /* last drawn status; "" + 0xff forces a redraw */
static void draw_writer_status(void){
 /* As in GBAWriter: the letter group in the case the next letter will get
 * ("abc", or "ABC" with Shift/Caps), and the Shift/Caps state. Redrawn only
 * when it changes: clearing and repainting it every few frames flickered. */
 const char *g=writer_group();const char *m=writer_caps()?"Caps":writer_shift()?"Shift":"";
 char now[24];unsigned k=0;
 for(const char *p=g?g:"";*p&&k<11;)now[k++]=*p++;
 now[k++]='|';for(const char *p=m;*p&&k<23;)now[k++]=*p++;now[k]=0;
 if(!strcmp(now,status_shown))return;
 memcpy(status_shown,now,k+1);
 fill(160,0,80,ROW_Y,C_WHITE);
 /* v1.2: Shift/Caps keeps one place: the group slot (every group is three
  * letters of the fixed-advance small font) stays reserved when empty. */
 int x=234-(int)small_width("abc");if(g&&g[0])small_draw(234-(int)small_width(g),5,g,C_GREY);x-=6;
 if(m[0])small_draw(x-(int)small_width(m),5,m,C_GREY);
}
static void draw_name(struct Screen *s,int full){
 if(full){status_shown[0]=(char)0xff;status_shown[1]=0;header(s->aux==NAME_RENAME||s->aux==NAME_FILE||s->aux==NAME_TAG?"Rename":"New Playlist");fill(0,ROW_Y,240,144,C_WHITE);
  small_draw(6,120,"Start+A: Save   Start+B: Cancel",C_GREY);}
 draw_field(40,writer_text(),writer_caret());draw_writer_status();
 fill(0,70,240,20,C_WHITE);if(message&&message_frames)center_text(72,message,C_BLACK);
}
static void draw_search(struct Screen *s,int full){
 /* The field is redrawn when the text or caret changes (writer_screen_frame);
  * repainting it every frame made it flicker. */
 if(full){search_clear=0;header(s->aux?"Add Songs":"Search");status_shown[0]=(char)0xff;status_shown[1]=0;
  draw_field(ROW_Y,writer_text(),writer_caret());draw_writer_status();}
 if(full){fill(0,ROW_Y+ROW_H,240,160-ROW_Y-ROW_H,C_WHITE);cache_page(s);
  if(!s->count){row_dirty=0;center_text(80,empty_text(s),C_BLACK);return;}
  unsigned used=s->count-s->top<rows_visible(s)?s->count-s->top:rows_visible(s);
  row_dirty=(1u<<used)-1;return;}
 if(search_clear){fill(0,ROW_Y+ROW_H,240,160-ROW_Y-ROW_H,C_WHITE);search_clear=0;
  if(!s->count){row_dirty=0;center_text(80,empty_text(s),C_BLACK);return;}}
 if(s->count)draw_rows(s);else row_dirty=0;
}

/* ---------- navigation helpers ---------- */
static void redraw(void){full_dirty=1;invalidate_rows();marquee_reset();box_off();}
static struct Screen *push(unsigned type,const char *title){
 if(depth==DEPTH){for(unsigned i=1;i<DEPTH-1;i++)stack[i]=stack[i+1];depth--;}
 struct Screen *s=&stack[depth++];memset(s,0,sizeof(*s));s->type=(uint8_t)type;
 if(title){unsigned n=strlen(title);if(n>=PLAYLIST_NAME)n=PLAYLIST_NAME-1;memcpy(s->title,title,n);}
 redraw();return s;
}
static void enter(struct Screen *s);
static void pop(void){if(depth>1)depth--;enter(top());redraw();}
static void ensure_visible(struct Screen *s){unsigned v=rows_visible(s);if(s->sel<s->top)s->top=s->sel;if(s->sel>=s->top+v)s->top=s->sel-v+1;}
/* One-row scroll: move the painted rows and the row cache by one row (DMA,
 * VRAM to VRAM) and fetch/paint only the row that came into view. */
static int scroll_one(struct Screen *s,uint32_t oldtop){
 unsigned v=rows_visible(s);
 if(!rc.valid||rc.owner!=s||rc.top!=oldtop||s->count<v||row_dirty)return 0;
 int y0=row_y0(s),down=s->top==oldtop+1;
 if(!down&&s->top+1!=oldtop)return 0;
 unsigned in=down?v-1:0;
 if(down){
  gfx_copy_lines(y0,y0+ROW_H,(int)(v-1)*ROW_H);   /* v1.7: onto the hidden page, shown whole */
  memmove(rc.text[0],rc.text[1],(v-1)*sizeof(rc.text[0]));memmove(rc.flag,rc.flag+1,v-1);memmove(rc.key,rc.key+1,(v-1)*sizeof(rc.key[0]));
 }else{
  gfx_copy_lines(y0+ROW_H,y0,(int)(v-1)*ROW_H);
  memmove(rc.text[1],rc.text[0],(v-1)*sizeof(rc.text[0]));memmove(rc.flag+1,rc.flag,v-1);memmove(rc.key+1,rc.key,(v-1)*sizeof(rc.key[0]));
 }
 rc.top=s->top;row(s,s->top+in,rc.text[in],META_TITLE,&rc.flag[in],&rc.key[in]);
 row_dirty|=1u<<in;return 1;
}
static void move_sel(struct Screen *s,int d){
 if(!s->count)return;
 uint32_t old=s->sel,oldtop=s->top;
 if(d<0&&s->sel)s->sel--;else if(d>0&&s->sel+1<s->count)s->sel++;
 if(old==s->sel)return;
 ensure_visible(s);marquee_reset();
 if(s->top!=oldtop){
  if(scroll_one(s,oldtop))row_dirty|=(1u<<(old-s->top))|(1u<<(s->sel-s->top));
  else{full_dirty=s->type!=T_SEARCH?1:0;if(s->type==T_SEARCH){invalidate_rows();cache_page(s);row_dirty=0xff;}}
 }
 else row_dirty|=(1u<<(old-s->top))|(1u<<(s->sel-s->top));
}
static void page_sel(struct Screen *s,int d){
 if(!s->count)return;unsigned v=rows_visible(s);
 if(d<0)s->sel=s->sel>v?s->sel-v:0;else s->sel=s->sel+v<s->count?s->sel+v:s->count-1;
 ensure_visible(s);marquee_reset();invalidate_rows();cache_page(s);row_dirty=0xff;
}
/* Up on the first row goes to the last, Down on the last to the first. */
static void jump_sel(struct Screen *s,uint32_t to){
 uint32_t old=s->sel,oldtop=s->top;if(!s->count||old==to)return;
 s->sel=to;ensure_visible(s);marquee_reset();
 if(s->top==oldtop)row_dirty|=(1u<<(old-s->top))|(1u<<(s->sel-s->top));
 else{invalidate_rows();cache_page(s);row_dirty=0xff;}
}
/* ---------- v1.5 long lists: speed-up, pages, letter mode, letter box ----------
 * Long lists (Songs, group lists, Folders, playlists) get the hold speed-up
 * and Left/Right pages; alphabetical ones also get L + Up/Down letter steps
 * and the letter box. Short menus keep the v1.3 behaviour. */
static int long_list(const struct Screen *s){
 return s->type==T_SONGS||s->type==T_GROUPS||s->type==T_FOLDER||s->type==T_PLAYLISTS||s->type==T_PLAYLIST||s->type==T_ADDTO;
}
static int alpha_list(struct Screen *s){
 /* By title (a genre's songs too); v1.6 with File Names: the Songs list in file-name order. */
 if(s->type==T_SONGS)return player_song_files()?s->p0==PERM_FILE:s->p0==PERM_TITLE||s->p0==PERM_GENRE;
 if(s->type==T_GROUPS)return 1;
 if(s->type==T_FOLDER)return folder_sorted(&dirs)&&folder_sorted(&songs);
 return 0;
}
/* Letter group of a sort field: the first folded letter. Digits and
 * symbols before "a" form one group "#"; an empty field (Unknown ...,
 * sorted last) is "?". The key never decreases along a sorted list. */
#define GROUP_UNKNOWN 0xffffffffu
static uint32_t fold_group(const char *t,char *label){
 struct Fold f;fold_init(&f,t);uint32_t c=fold_next(&f),u=0;
 if(!c){if(label)memcpy(label,"?",2);return GROUP_UNKNOWN;}
 if(c<'a'){if(label)memcpy(label,"#",2);return 1;}
 if(c<='z')u=c-32;
 else if(c>=0x3b1&&c<=0x3c9)u=c==0x3c2?0x3a3:c-32;   /* Greek (final sigma: Sigma) */
 else if(c>=0x430&&c<=0x44f)u=c-32;                   /* Cyrillic */
 else if(c>=0x450&&c<=0x45f)u=c-80;
 if(label){if(u){size_t n=utf8_put(label,7,u);label[n]=0;}else memcpy(label,"#",2);}
 return c;
}
/* Group key of list entry i (and its label). */
static int entry_key(struct Screen *s,uint32_t i,uint32_t *key,char *label){
 const char *t;uint32_t run=0;
 if(s->type==T_SONGS){uint32_t id;if(list_song_id(s,i,&id)||library_song(id,&song))return -1;t=song_label(&song);}
 else if(s->type==T_GROUPS){if(library_group(s->p0,s->p1+i,&group)||library_song(group.rep,&song))return -1;
  t=s->p0==GROUP_ARTISTS?song.artist:s->p0==GROUP_GENRES?song.genre:song.album;}
 else if(s->type==T_FOLDER){unsigned d=dir_count();
  if(i<d?folder_name(&dirs,i,name_buf,sizeof(name_buf)):folder_name(&songs,i-d,name_buf,sizeof(name_buf)))return -1;
  t=name_buf;run=i>=d;}   /* folders first, then songs: each run alphabetical */
 else return -1;
 uint32_t k=fold_group(t,label);*key=run?(k==GROUP_UNKNOWN?k:0x80000000u|k):k;return 0;
}
/* First index in [lo, hi) whose group key is > k (after) or >= k. */
static int group_bound(struct Screen *s,uint32_t lo,uint32_t hi,uint32_t k,int after,uint32_t *out){
 while(lo<hi){uint32_t m=lo+(hi-lo)/2,km;if(entry_key(s,m,&km,0))return -1;
  if(after?km<=k:km<k)lo=m+1;else hi=m;player_service();}
 *out=lo;return 0;
}
/* Sprite pixel (x, y) of the box, colour index c (0 transparent, 1 box, 2 white). */
static void box_pixels(void){
 uint16_t cols[32];unsigned w=text_width(box_label);if(w>BOX_W/2)w=BOX_W/2;
 struct TextCursor c;text_cursor(&c,box_label,0);for(unsigned i=0;i<w;i++)cols[i]=text_cursor_next(&c);
 int lx=(BOX_W-2*(int)w)/2,ly=(BOX_H-32)/2;
 for(int y=0;y<BOX_H;y++){int in=y==0||y==BOX_H-1?2:y==1||y==BOX_H-2?1:0;   /* rounded corners */
  for(int x=0;x<BOX_W;x+=4){uint16_t h=0;
   for(int q=0;q<4;q++){int px=x+q;unsigned v=0;
    if(px>=in&&px<BOX_W-in){v=1;int gx=px-lx,gy=y-ly;
     if(gx>=0&&gx<2*(int)w&&gy>=0&&gy<32&&(cols[gx/2]>>(gy/2)&1))v=2;}
    h|=(uint16_t)(v<<(4*q));}
   BOX_TILES[((y>>3)*8+(x>>3))*16+(y&7)*2+((x&7)>>2)]=h;}}
}
static void box_oam(unsigned show){OAM[0]=(uint16_t)(BOX_Y|0x0400|(show?0:0x0200));OAM[1]=(uint16_t)(BOX_X|0xC000);OAM[2]=960;}
static void box_alpha(unsigned eva){REG_BLDALPHA=(uint16_t)(eva|(16u-eva)<<8);}
/* Gone at once (screen changes). */
static void box_off(void){if(box_on)box_oam(0);box_on=0;box_frames=0;box_fade=0;}
/* v1.6: fade out over BOX_FADE frames (the list under it stays as it is). */
static void box_hide(void){
 if(!box_on||box_fade)return;box_frames=0;box_fade=BOX_FADE;marquee_reset();
}
static void box_fade_step(unsigned elapsed){
 if(!box_fade)return;
 box_fade=box_fade>elapsed?box_fade-elapsed:0;
 if(!box_fade){box_on=0;box_oam(0);}else box_alpha(16u*box_fade/BOX_FADE);
}
/* Show (or update) the box for the selected entry. frames: time left after
 * the movement stops; 0 keeps it while L is held. */
static void box_show(struct Screen *s,unsigned frames){
 char l[8];uint32_t k;unsigned lb=player_letter_box();
 if(!lb||(lb==2&&frames)||!s->count||entry_key(s,s->sel,&k,l))return;   /* Off; Only L+DPAD: letter mode only (frames 0) */
 if(!box_on||strcmp(l,box_label)){memcpy(box_label,l,sizeof(l));box_pixels();}
 if(!box_on||box_fade){box_on=1;box_fade=0;box_alpha(16);box_oam(1);}
 box_frames=frames;
}
/* v1.6: input only changes s->sel / s->top; present() moves the display. */
static void set_sel(struct Screen *s,uint32_t t){
 if(!s->count)return;if(t>=s->count)t=s->count-1;
 if(s->sel==t)return;s->sel=t;ensure_visible(s);marquee_reset();
}
/* Entry t selected and shown on the top row (as far as the list allows). */
static void sel_on_top(struct Screen *s,uint32_t t){
 unsigned v=rows_visible(s);uint32_t max=s->count>v?s->count-v:0;
 s->sel=t;s->top=t<max?t:max;marquee_reset();
}
/* L + Up/Down: the first entry of the previous / next letter group. A fresh
 * press past the first / last group wraps; holding stops there. */
static int letter_step(struct Screen *s,int dir,int fresh){
 uint32_t k,t,n=s->count;if(!n||entry_key(s,s->sel,&k,0))return 0;
 if(dir>0){
  if(group_bound(s,s->sel+1,n,k,1,&t))return 0;
  if(t>=n){if(!fresh)return 0;t=0;}
 }else{
  uint32_t start,kp;if(group_bound(s,0,s->sel+1,k,0,&start))return 0;
  if(!start){if(!fresh||entry_key(s,n-1,&kp,0))return 0;}
  else if(entry_key(s,start-1,&kp,0))return 0;
  if(group_bound(s,0,start?start:n,kp,0,&t))return 0;
 }
 sel_on_top(s,t);return 1;
}
#define BOX_LINGER 36   /* 0.6 s at 60 Hz */
#define BOX_HOLD 90     /* Up / Down held 1.5 s: the box shows */
static __attribute__((noinline)) void long_list_input(struct Screen *s,const struct InputFrame *in){   /* own frame: keeps ui_frame within its budget */
 int alpha=alpha_list(s);
 if(in->held&KEY_L){   /* letter mode; L + Left/Right and L alone do nothing */
  for(int j=0;j<2;j++)for(unsigned n=0;n<in->letters[j];n++){
   int fresh=!n&&(in->pressed&(j?KEY_DOWN:KEY_UP));
   if(!alpha||!letter_step(s,j?1:-1,fresh))break;
   box_show(s,0);
  }
  return;
 }
 if(box_on&&!box_frames)box_hide();   /* L released: the letter-mode box fades */
 unsigned v=rows_visible(s);int moved=0,wrapped=0;
 for(int j=0;j<2;j++){unsigned n=in->rows[j];if(!n||!s->count)continue;
  int down=j==1;
  if((in->pressed&(down?KEY_DOWN:KEY_UP))&&(down?s->sel+1==s->count:s->sel==0)){   /* fresh press at an end wraps */
   set_sel(s,down?0:s->count-1);wrapped=s->count>1;continue;}
  uint32_t t=down?(s->sel+n<s->count?s->sel+n:s->count-1):(s->sel>n?s->sel-n:0);   /* holding stops at the ends */
  if(t!=s->sel){set_sel(s,t);moved=1;}
 }
 /* v1.6: holding Up / Down keeps the list at most 2 rows ahead of the
  * screen, so the screen always moves by staged steps; with a busy CPU the
  * list goes slower instead of jumping. */
 if(moved&&rc.valid&&rc.owner==s){uint32_t t=s->top;
  if(t>rc.top+2)t=rc.top+2;else if(t+2<rc.top)t=rc.top-2;
  if(t!=s->top){uint32_t row=s->sel-s->top;s->top=t;s->sel=t+row;}}
 /* v1.6: Left / Right move the list by pages and the cursor keeps its screen
  * row; at the first / last page they do nothing. pages[0] is Left. */
 if(s->count>v)for(int j=0;j<2;j++){unsigned n=in->pages[j];if(!n)continue;
  uint32_t max=s->count-v,d=n*v,row=s->sel-s->top;
  uint32_t nt=j?(s->top+d<max?s->top+d:max):(s->top>d?s->top-d:0);
  if(nt!=s->top){s->top=nt;s->sel=nt+row;marquee_reset();moved=1;}
 }
 if(!alpha)return;
 int fast=((in->held&KEY_UP)&&in->hold_frames[6]>=BOX_HOLD)||((in->held&KEY_DOWN)&&in->hold_frames[7]>=BOX_HOLD)
        ||((in->held&KEY_LEFT)&&in->hold_frames[5]>INPUT_LIST_DELAY)||((in->held&KEY_RIGHT)&&in->hold_frames[4]>INPUT_LIST_DELAY);
 if(wrapped)box_show(s,BOX_LINGER);
 else if(fast){if(moved||!box_on||box_fade)box_show(s,BOX_LINGER);else box_frames=BOX_LINGER;}
 else if(moved&&box_on)box_hide();   /* a single step starts the fade at once */
 else if(box_on&&box_frames){box_frames=box_frames>in->elapsed?box_frames-in->elapsed:0;if(!box_frames)box_hide();}
}
/* Bring the display of a long list to s->top / s->sel (v1.7, mode 4). All
 * of it is drawn on the hidden page and shown at once when complete
 * (gfx_commit at the end of the UI frame, held while rows are unpainted):
 * - a move keeps the rows still in view (copied from the shown page, shifted),
 *   then paints the rows that come in and the old and new selection rows;
 * - a jump (pages, letters, wraps) paints the whole page, 3 rows per frame,
 *   each row read from the card when it is painted;
 * - moves wait while less than 1/8 s of audio is buffered. */
static void present(struct Screen *s){
 unsigned v=rows_visible(s),mask=(1u<<v)-1;
 if(!rc.valid||rc.owner!=s){cache_page(s);hl_sel=s->sel;row_dirty|=mask;}
 if(row_dirty&mask){draw_rows(s);return;}
 if(rc.top==s->top&&hl_sel==s->sel)return;
 int32_t lag=(int32_t)(s->top-rc.top);unsigned m=lag<0?(unsigned)-lag:(unsigned)lag;
 if(m>=v){rc.owner=s;rc.top=s->top;rc.valid=1;rc_need=mask;rc_unchecked=0;hl_sel=s->sel;row_dirty|=mask;draw_rows(s);return;}
 if(!player_scroll_ok())return;   /* audio first; input keeps the lag at 2 rows */
 uint32_t old_hl=hl_sel;int dir=lag>0?1:lag<0?-1:0;unsigned keep=v-m;int y0=row_y0(s);
 if(dir>0){memmove(rc.text[0],rc.text[m],keep*sizeof(rc.text[0]));memmove(rc.flag,rc.flag+m,keep);memmove(rc.key,rc.key+m,keep*sizeof(rc.key[0]));
  gfx_copy_lines(y0,y0+(int)m*ROW_H,(int)keep*ROW_H);rc_unchecked>>=m;}
 else if(dir<0){memmove(rc.text[m],rc.text[0],keep*sizeof(rc.text[0]));memmove(rc.flag+m,rc.flag,keep);memmove(rc.key+m,rc.key,keep*sizeof(rc.key[0]));
  gfx_copy_lines(y0+(int)m*ROW_H,y0,(int)keep*ROW_H);rc_unchecked=(rc_unchecked<<m)&mask;}
 rc.top=s->top;hl_sel=s->sel;
 for(unsigned n=0;n<m;n++){unsigned r=dir>0?keep+n:n;rc_need|=1u<<r;row_dirty|=1u<<r;}
 if(old_hl!=s->sel){
  if(old_hl>=rc.top&&old_hl<rc.top+v)row_dirty|=1u<<(old_hl-rc.top);
  if(s->sel>=rc.top&&s->sel<rc.top+v)row_dirty|=1u<<(s->sel-rc.top);
 }
 draw_rows(s);
}
static void search_update(struct Screen *s){
 unsigned old_count=s->count,old_sel=s->sel,old_top=s->top;
 unsigned reuse=rc.valid&&rc.owner==s&&rc.top==old_top;
 unsigned perm;uint32_t lo=0,hi=0;const char *q=writer_text();
 while(*q==' ')q++;   /* A types a space; leading spaces never narrow the list */
 if(player_search_field()==FIELD_TITLE&&q[0]&&library_words()){
  /* v1.2: Song Title matches the start of any word (p2 = 1: word mode,
   * p0 = folded query bytes, [p1, p3) = word entries, count = songs). */
  unsigned qb=0;uint32_t n=0;if(library_word_range(q,&lo,&hi,&qb,&n)){lo=hi=n=0;}
  s->p0=qb;s->p1=lo;s->p2=1;s->p3=hi;s->count=n;
 }else{
  if(library_prefix(player_search_field(),q,&perm,&lo,&hi)){lo=hi=0;perm=PERM_TITLE;}
  s->p0=perm;s->p1=lo;s->p2=0;s->count=hi-lo;
 }
 s->sel=0;s->top=0;
 unsigned n=strlen(writer_text());if(n>63)n=63;memcpy(search_query,writer_text(),n);search_query[n]=0;
 if(!!old_count!=!!s->count)search_clear=1;
 if(!reuse||old_top){marquee_reset();invalidate_rows();cache_page(s);row_dirty=0xff;return;}
 /* Compare the actual displayed identity, text and flags, not just the range:
  * a longer query often resolves to exactly the same visible songs. */
 unsigned selected_changed=0;
 for(unsigned r=0;r<rows_visible(s);r++){
  char t[META_TITLE];uint8_t f=0;unsigned key=0;
  if(r<s->count)row(s,r,t,sizeof(t),&f,&key);else t[0]=0;
  if(r==0&&(strcmp(t,rc.text[r])||(r<s->count&&key!=rc.key[r])))selected_changed=1;
  if(strcmp(t,rc.text[r])||f!=rc.flag[r]||(r<s->count&&key!=rc.key[r])){
   memcpy(rc.text[r],t,strlen(t)+1);rc.flag[r]=f;rc.key[r]=key;row_dirty|=1u<<r;
  }
 }
 if(old_sel&&old_sel>=old_top&&old_sel-old_top<rows_visible(s))row_dirty|=1u<<(old_sel-old_top);
 if(old_sel&&s->count)row_dirty|=1;
 rc.top=0;
 if(old_sel||old_top||selected_changed)marquee_reset();
}
static void enter(struct Screen *s){
 switch(s->type){
  case T_HOME:s->count=8;break;
  case T_SETTINGS:s->count=7+conv_all_shown();break;   /* v2.0: Convert all */
  case T_SONGS:
   /* v1.6: the Songs list (aux 1) follows Show songs as: file-name order
    * with File Names. A change while it was under Settings starts at the top. */
   if(s->aux==1){unsigned want=player_song_files()?PERM_FILE:PERM_TITLE;if(s->p0!=want){s->p0=want;s->sel=s->top=0;}}
   break;
  case T_ARTIST:s->count=2;break;
  case T_OPTIONS:s->p2=options_current(s);s->count=option_count(s);break;
  case T_PLOPTIONS:s->p2=coll_convertible();s->count=6+s->p2;break;   /* v1.9; v2.0 p2: Convert shown; 11.0: no Add to Playlist */
  case T_SECRET:s->count=10;break;
  case T_COPTIONS:coll_conv=coll_convertible();s->count=coll_count();break;
  case T_CONFIRM:s->count=2;break;
  case T_FOLDER:folder_bind(&dirs,s->path,FOLDER_DIRS);folder_bind(&songs,s->path,FOLDER_SONGS);{int a=folder_count(&dirs),b=folder_count(&songs);s->count=(a>0?(unsigned)a:0)+(b>0?(unsigned)b:0);}break;
  case T_PLAYLISTS:s->count=playlist_count()+1;break;
  case T_ADDTO:s->count=playlist_count()+1;s->p0=0;   /* 11.0: from a song in a playlist (p2), that playlist is left out */
   if(s->p2)for(unsigned k=0;k<playlist_count();k++){uint32_t id;if(!playlist_info(k,&id,0)&&id==s->p1){s->p0=k+1;s->count--;break;}}
   break;
  case T_PLAYLIST:{uint32_t n=playlist_length(s->p0);if(s->p0!=pl_hide_id)pl_hide_reset(s->p0);   /* 11.0: + Add Songs, hidden missing songs */
   s->count=(n>pl_hide_n?n-pl_hide_n:0)+1;break;}
  case T_SEARCH:writer_begin(search_query);{uint32_t keep_sel=s->sel,keep_top=s->top;search_update(s);if(keep_sel<s->count){s->sel=keep_sel;s->top=keep_top;}}break;
  default:break;
 }
 if(s->sel>=s->count)s->sel=s->count?s->count-1:0;ensure_visible(s);
}
static void open_screen(struct Screen *s){enter(s);redraw();}
/* Playback may advance behind Song Options. Reconcile labels and selected
 * stable action before another A release can activate it. */
static int refresh_options(struct Screen *s){
 if(s->type!=T_OPTIONS||s->p2==(unsigned)options_current(s))return 0;
 unsigned selected=menu_id(s,s->sel),old_sel=s->sel;
 s->p2=options_current(s);s->count=option_count(s);
 if(old_sel>=s->count)s->sel=s->count-1;
 for(unsigned i=0;i<s->count;i++)if(menu_id(s,i)==selected){s->sel=i;break;}
 ensure_visible(s);redraw();return 1;
}
static struct QueueSource qtmp;   /* one scratch source keeps these off the stack */
static struct QueueSource *new_source(void){memset(&qtmp,0,sizeof(qtmp));return &qtmp;}
static void open_now(void){if(top()->type!=T_NOW)push(T_NOW,"Now Playing");}
static int row_song(struct Screen *s,uint32_t i,char *path);
static void play_from(struct Screen *s,uint32_t row){
 /* The song that is loaded (marked with the play symbol) is not
  * restarted: it resumes if paused and Now Playing opens. A song that ended
  * or was only restored at startup plays from the list as usual; the queue
  * it plays from stays (as 10.0). 11.0: `row` is the list row (a folder's
  * songs come after its folders): in a folder with subfolders the song
  * index was taken as a row, comparing the wrong song. A playlist row is
  * its entry (11.0: + Add Songs and hidden missing songs). */
 static char path[STORAGE_PATH];const struct NowPlaying *np=player_now();
 if(player_active()&&!player_fatal()&&np->valid&&!row_song(s,row,path)&&!strcmp(np->path,path)){
  if(!player_is_playing())player_toggle();
  open_now();return;
 }
 uint32_t index=s->type==T_FOLDER?row-dir_count():s->type==T_PLAYLIST?pl_entry(s,row):row;
 struct QueueSource *q=new_source();
 if(s->type==T_SONGS||s->type==T_SEARCH)list_source(q,s);
 else if(s->type==T_FOLDER){q->kind=QUEUE_FOLDER;memcpy(q->text,s->path,STORAGE_PATH);int n=folder_count(&songs);q->count=n>0?(unsigned)n:0;}
 else if(s->type==T_PLAYLIST){q->kind=QUEUE_PLAYLIST;q->playlist=s->p0;q->count=playlist_length(s->p0);}
 else return;
 player_play(q,index);open_now();
}
/* Song path of list row i (song lists only). */
static int row_song(struct Screen *s,uint32_t i,char *path){
 if(s->type==T_SONGS||s->type==T_SEARCH){uint32_t id;if(list_song_id(s,i,&id)||library_song(id,&song))return -1;memcpy(path,song.path,STORAGE_PATH);return 0;}
 if(s->type==T_FOLDER){unsigned d=dir_count();if(i<d||folder_name(&songs,i-d,name_buf,sizeof(name_buf)))return -1;return folder_join(path,s->path,name_buf)?0:-1;}
 if(s->type==T_PLAYLIST){if(i==add_row(s)||playlist_entry(s->p0,pl_entry(s,i),&entry)||!entry.path[0])return -1;memcpy(path,entry.path,STORAGE_PATH);return 0;}
 return -1;
}
static void open_options(struct Screen *from,uint32_t i){
 static char p[STORAGE_PATH];if(row_song(from,i,p))return;
 unsigned in_playlist=from->type==T_PLAYLIST;uint32_t pl=from->p0;
 struct QueueSource *q=new_source();int queue_ok=0;
 if(from->type==T_SONGS||from->type==T_SEARCH){list_source(q,from);queue_ok=1;}
 else if(from->type==T_FOLDER){q->kind=QUEUE_FOLDER;memcpy(q->text,from->path,STORAGE_PATH);int n=folder_count(&songs);q->count=n>0?(unsigned)n:0;queue_ok=1;}
 else if(from->type==T_PLAYLIST){q->kind=QUEUE_PLAYLIST;q->playlist=pl;q->count=playlist_length(pl);queue_ok=1;}
 uint32_t qi=from->type==T_FOLDER?i-dir_count():from->type==T_PLAYLIST?pl_entry(from,i):i;
 struct Screen *s=push(T_OPTIONS,"Song Options");memcpy(s->path,p,STORAGE_PATH);
 s->aux=(uint8_t)in_playlist;s->p0=pl;s->p1=in_playlist?qi:i;if(queue_ok){opt_src=*q;opt_src_index=qi;s->has_src=1;}
 open_screen(s);
}
static void open_now_options(void){
 const struct NowPlaying *n=player_now();if(!player_loaded()||!n->valid)return;
 struct Screen *s=push(T_OPTIONS,"Song Options");memcpy(s->path,n->path,STORAGE_PATH);open_screen(s);
}
static void song_meta_for(const char *path,char *title,char *artist,uint32_t *ms){
 uint32_t id;title[0]=artist[0]=0;*ms=0;
 if(!library_find(path,&id)&&!library_song(id,&song)){memcpy(title,song.title,META_TITLE);memcpy(artist,song.artist,META_NAME);*ms=song.duration_ms;return;}
 const struct NowPlaying *n=player_now();
 if(n->valid&&!strcmp(n->path,path)){memcpy(title,n->meta.title,META_TITLE);memcpy(artist,n->meta.artist,META_NAME);*ms=n->meta.duration_ms;return;}
 const char *b=path;for(const char *p=path;*p;p++)if(*p=='/')b=p+1;unsigned k=strlen(b);if(k>4)k-=4;if(k>=META_TITLE)k=META_TITLE-1;memcpy(title,b,k);title[k]=0;
}
static void flash(const char *m){message=m;message_frames=90;}
static int add_to_playlist(uint32_t id,const char *path){
 static char t[META_TITLE],a[META_NAME];uint32_t ms;song_meta_for(path,t,a,&ms);
 player_fill();if(playlist_add(id,path,t,a,ms)){flash("Save failed");return -1;}
 return 0;
}
/* 11.0 "+ Add Songs": Search (all songs on an empty text) adds the chosen
 * song to playlist add_target; aux 1 marks this Search. */
static uint32_t add_target;static unsigned add_b_empty;
static void open_add_songs(struct Screen *pl){
 add_target=pl->p0;search_query[0]=0;add_b_empty=0;
 struct Screen *n=push(T_SEARCH,"Add Songs");n->aux=1;open_screen(n);
}

/* ---------- Update Library ---------- */
static unsigned update_last;
static int update_progress(unsigned phase,unsigned done){
 if(phase==0?frame_count-update_last<6&&done%64:phase==7&&done!=1&&done%16)return 0;update_last=frame_count;
 fill(0,96,240,12,C_WHITE);
 char t[40];static const char *const ph[]={"","Sorting songs","Sorting artists","Sorting albums","Sorting genres","Sorting files","Finishing","Reading covers","Indexing titles","Checking playlists"};
 if(phase==0){unsigned k=0;char d[12];unsigned n=0;unsigned v=done;do{d[n++]=(char)('0'+v%10);v/=10;}while(v);while(n)t[k++]=d[--n];memcpy(t+k," songs found",13);}
 else memcpy(t,ph[phase<10?phase:6],strlen(ph[phase<10?phase:6])+1);
 small_draw((240-(int)small_width(t))/2,98,t,C_GREY);gfx_commit_now();frame_count++;   /* v1.7: no sound plays; shown at once */
 return 0;
}
static void run_update(void){
 gfx_ready();
 header(top()->type==T_HOME?"gbamp3":top()->type==T_SECRET?"Secret Settings":"Settings");fill(0,ROW_Y,240,144,C_WHITE);center_text(70,"Updating library...",C_BLACK);gfx_commit_now();
 update_last=0;int e=player_update_library(update_progress);pl_hide_reset(0);   /* 11.0: missing playlist songs were removed */
 fill(0,ROW_Y,240,144,C_WHITE);center_text(70,e?"Update failed":"Library updated",C_BLACK);gfx_commit_now();
 message=0;message_frames=60;invalidate_rows();
 /* Library screens refer to the old index: drop them, keep Settings on top. */
 for(unsigned i=1;i<depth;i++)if(stack[i].type==T_SONGS||stack[i].type==T_GROUPS||stack[i].type==T_SEARCH||stack[i].type==T_ARTIST){
  depth=i;struct Screen *n=push(T_SETTINGS,"Settings");enter(n);n->sel=menu_position(n,1);break;}
 full_dirty=2;
}


/* v1.9: after a file change: back `levels` screens (options and its dialog),
 * update the library, show the lists again. */
static int run_update_and_back(unsigned levels){
 depth=depth>levels?depth-levels:1;
 gfx_ready();
 header(top()->type==T_HOME?"gbamp3":top()->title);fill(0,ROW_Y,240,144,C_WHITE);center_text(70,"Updating library...",C_BLACK);gfx_commit_now();
 update_last=0;int e=player_update_library(update_progress);pl_hide_reset(0);
 invalidate_rows();
 /* The whole lists (Songs, Artists, Albums, Genres from Home) take their new
  * length; a list of one artist, album or genre, or search results, refer to
  * the old index: the stack ends below the first of them. */
 for(unsigned i=1;i<depth;i++){struct Screen *x=&stack[i];
  if(x->type==T_SONGS&&x->aux==1){x->count=library_count();continue;}
  if(x->type==T_GROUPS&&x->p0!=GROUP_ARTIST_ALBUMS&&x->p1==0){x->count=library_groups(x->p0);continue;}
  if(x->type==T_SONGS||x->type==T_GROUPS||x->type==T_SEARCH||x->type==T_ARTIST){depth=i;break;}
 }
 struct Screen *t=top();enter(t);
 if(t->sel>=t->count)t->sel=t->count?t->count-1:0;ensure_visible(t);
 redraw();return e;
}
/* ---------- v1.9 collections: albums, artists, genres, folders, playlists ---------- */
static char cpath[STORAGE_PATH];   /* one path buffer for the collection actions */
static char coll_tag[META_NAME];   /* v1.9.1: the album / artist / genre tag of the collection */
static void progress_line(const char *t){
 fill(0,144,240,16,C_WHITE);small_draw((240-(int)small_width(t))/2,147,t,C_GREY);gfx_commit_now();
}
static void num_text(char *o,unsigned v){char d[12];unsigned n=0;do{d[n++]=(char)('0'+v%10);v/=10;}while(v);while(n)*o++=d[--n];*o=0;}
static struct Screen *coll_open(const char *title){struct Screen *n=push(T_COPTIONS,title);open_screen(n);return n;}
/* START on an album / artist / genre row. */
static void open_group_options(struct Screen *s,uint32_t i){
 if(!s->count||library_group(s->p0,s->p1+i,&group)||library_song(group.rep,&song))return;
 struct QueueSource *q=&coll_src;memset(q,0,sizeof(*q));q->kind=QUEUE_LIBRARY;q->start=group.start;q->count=group.count;q->generation=library_generation();
 coll_albums=0;
 if(s->p0==GROUP_ARTISTS){coll_kind=CK_ARTIST;q->perm=PERM_ARTIST;coll_albums=group.sub_count;}
 else if(s->p0==GROUP_GENRES){coll_kind=CK_GENRE;q->perm=PERM_GENRE;}
 else{coll_kind=CK_ALBUM;q->perm=s->p0==GROUP_ALBUMS?PERM_ALBUM:PERM_ARTIST;}
 {const char *v=coll_kind==CK_ARTIST?song.artist:coll_kind==CK_GENRE?song.genre:song.album;unsigned k=strlen(v);if(k>=sizeof(coll_tag))k=sizeof(coll_tag)-1;memcpy(coll_tag,v,k);coll_tag[k]=0;}   /* v1.9.1: the tag as stored */
 char t[PLAYLIST_NAME];const char *nm=group_name(s->p0,&song);unsigned k=strlen(nm);if(k>=sizeof(t))k=sizeof(t)-1;memcpy(t,nm,k);t[k]=0;
 coll_open(t);
}
/* START on a folder row. */
static void open_folder_options(struct Screen *s,uint32_t i){
 if(folder_name(&dirs,i,name_buf,sizeof(name_buf))||!folder_join(coll_path,s->path,name_buf))return;
 struct QueueSource *q=&coll_src;memset(q,0,sizeof(*q));q->kind=QUEUE_FOLDER;memcpy(q->text,coll_path,STORAGE_PATH);
 q->count=player_folder_count(coll_path);coll_kind=CK_FOLDER;coll_albums=0;
 char t[PLAYLIST_NAME];unsigned k=strlen(name_buf);if(k>=sizeof(t))k=sizeof(t)-1;memcpy(t,name_buf,k);t[k]=0;
 coll_open(t);
}
static void coll_play(int shuffle){
 if(!coll_src.count){flash("No songs");return;}
 uint32_t at=0;
 if(shuffle){player_set_mode(MODE_SHUFFLE);at=(*(volatile uint16_t*)0x4000108)%coll_src.count;}   /* Playback Mode: Shuffle */
 player_play(&coll_src,at);depth=depth>1?depth-1:1;open_now();
}
/* The songs after the current one, in their order (Play Next puts the newest
 * first, so they are added from the last); at most the free Play Next places. */
static void coll_play_next(void){
 if(!coll_src.count){flash("No songs");return;}
 if(!player_active()){coll_play(0);return;}
 unsigned free=player_play_next_free(),k=coll_src.count<free?coll_src.count:free;char *p=cpath;
 for(unsigned j=k;j-->0;){if(!player_resolve(&coll_src,j,p))player_play_next(p);player_service();}
 pop();flash(k<coll_src.count?"Queue full":"Added to Play Next");
}
/* All songs of the collection to playlist `id`; B stops. */
static void coll_add_to(uint32_t id){
 static char t[40];char *p=cpath;unsigned n=coll_src.count,added=0;
 for(unsigned i=0;i<n;i++){
  if(!(*(volatile uint16_t*)0x4000130&2))break;   /* B held */
  if(!(i&3)){memcpy(t,"Adding ",7);num_text(t+7,i+1);unsigned k=strlen(t);memcpy(t+k," / ",3);num_text(t+k+3,n);progress_line(t);}
  if(player_resolve(&coll_src,i,p))continue;
  static char ti[META_TITLE],ar[META_NAME];uint32_t ms;song_meta_for(p,ti,ar,&ms);
  player_fill();if(playlist_add(id,p,ti,ar,ms)){flash("Save failed");return;}
  added++;
 }
 static char m[32];num_text(m,added);unsigned k=strlen(m);memcpy(m+k,added==1?" song added":" songs added",added==1?12:13);flash(m);
}
static int run_update_and_back(unsigned levels);
/* Delete the collection's song files (folder: its songs, then the folder if
 * it is empty). Playback stops first; the library is updated afterwards. */
static void coll_delete_run(void){
 char *p=cpath;unsigned n=0,failed=0;
 if(coll_kind==CK_FOLDER){unsigned kept=0;failed=player_delete_folder(coll_path,&n,&kept)!=0;}
 else for(unsigned i=0;i<coll_src.count;i++){
  if(player_resolve(&coll_src,i,p))continue;
  if(player_delete_file(p))failed++;else n++;
  if(!(i&3)){static char t[32];memcpy(t,"Deleted ",8);num_text(t+8,n);progress_line(t);}
 }
 run_update_and_back(2);
 static char m[40];if(failed)strcpy(m,"Some files not deleted");else{num_text(m,n);unsigned k=strlen(m);memcpy(m+k,n==1?" song deleted":" songs deleted",n==1?14:15);}
 flash(m);
}
static void coll_confirm_delete(void){
 static char t[PLAYLIST_NAME];
 if(coll_kind==CK_SONG)strcpy(t,"Delete song?");
 else if(coll_kind==CK_FOLDER)strcpy(t,"Delete folder's songs?");
 else{memcpy(t,"Delete ",7);num_text(t+7,coll_src.count);unsigned k=strlen(t);memcpy(t+k,coll_src.count==1?" song?":" songs?",coll_src.count==1?7:8);}
 struct Screen *n=push(T_CONFIRM,t);n->aux=1;open_screen(n);
}
/* Rename a song file (dir 0, the extension kept) or a folder (dir 1). */
static void open_rename(const char *path,unsigned dir){
 const char *b=path;for(const char *q=path;*q;q++)if(*q=='/')b=q+1;
 char *base=cpath;unsigned k=strlen(b);memcpy(base,b,k+1);
 if(!dir&&k>4&&base[k-4]=='.')base[k-4]=0;
 struct Screen *n=push(T_NAME,"Rename");n->aux=NAME_FILE;n->p0=dir;memcpy(n->path,path,STORAGE_PATH);writer_begin(base);message_frames=0;
}
static void file_rename_commit(struct Screen *s){
 const char *t=writer_text();
 if(!t[0]){flash("Name cannot be empty");return;}
 for(const char *q=t;*q;q++)if(*q=='\\'||*q=='/'||*q==':'||*q=='*'||*q=='?'||*q=='"'||*q=='<'||*q=='>'||*q=='|'){flash("Not allowed: \\ / : * ? \" < > |");return;}
 char *np=cpath;const char *old=s->path,*slash=0;for(const char *q=old;*q;q++)if(*q=='/')slash=q;if(!slash)return;
 unsigned a=(unsigned)(slash-old)+1,k=strlen(t),e=0;const char *ext="";
 if(!s->p0){unsigned n=strlen(old);if(n>4&&old[n-4]=='.'){ext=old+n-4;e=4;}}
 if(a+k+e>=STORAGE_PATH){flash("Name too long");return;}
 memcpy(np,old,a);memcpy(np+a,t,k);memcpy(np+a+k,ext,e+1);
 if(!strcmp(np,old)){pop();return;}
 if(playlist_exists(np)){flash("Name already exists");return;}
 int r=player_rename(old,np,s->p0);
 if(r==-1){flash("Rename failed");return;}
 run_update_and_back(2);flash(r?"Renamed; playlists not updated":"Renamed");
}
/* v1.9.1: rename an album, artist or genre: its tag in every song file
 * (tags.c); music stops, the library is updated afterwards. */
static void open_tag_rename(void){
 struct Screen *n=push(T_NAME,"Rename");n->aux=NAME_TAG;n->p0=coll_kind==CK_ARTIST?TAG_ARTIST:coll_kind==CK_GENRE?TAG_GENRE:TAG_ALBUM;
 writer_begin(coll_tag);message_frames=0;
}
static void tag_rename_commit(struct Screen *s){
 const char *t=writer_text();
 while(*t==' ')t++;
 if(!t[0]){flash("Name cannot be empty");return;}
 if(strlen(t)>(s->p0==TAG_GENRE?TAG_MAX_GENRE:TAG_MAX_ALBUM)){flash("Name too long");return;}
 if(!strcmp(t,coll_tag)){pop();return;}
 static char v[META_NAME];strcpy(v,t);
 player_stop_all();
 char *p=cpath;unsigned n=coll_src.count,done=0,skipped=0;static char m[40];
 for(unsigned i=0;i<n;i++){
  if(!(i&3)){memcpy(m,"Renaming ",9);num_text(m+9,i+1);unsigned k=strlen(m);memcpy(m+k," / ",3);num_text(m+k+3,n);progress_line(m);}
  if(player_resolve(&coll_src,i,p)){skipped++;continue;}
  if(player_tag_set(p,s->p0,v))skipped++;else done++;
 }
 run_update_and_back(2);
 if(!skipped)strcpy(m,"Renamed");
 else{memcpy(m,"Renamed ",8);num_text(m+8,done);unsigned k=strlen(m);memcpy(m+k," of ",4);num_text(m+k+4,n);k=strlen(m);memcpy(m+k," songs",7);}
 flash(m);
}
/* Info of the collection: kind, name, songs (albums / folder), total length. */
static void draw_coll_info(struct Screen *s){
 header("Info");fill(0,ROW_Y,240,144,C_WHITE);
 static const char *const kinds[]={"Album","Artist","Genre","Folder","Playlist","Song"};
 static char songs[16],extra[16],len[24];num_text(songs,coll_src.count);num_text(extra,coll_albums);
 uint32_t sec=0;unsigned known=coll_src.count<=3000;
 for(uint32_t i=0;known&&i<coll_src.count;i++){uint32_t id;
  if(coll_src.kind==QUEUE_LIBRARY){if(!library_perm(coll_src.perm,coll_src.start+i,&id)&&!library_song(id,&song))sec+=(song.duration_ms+500)/1000;}
  else if(coll_src.kind==QUEUE_PLAYLIST){if(!playlist_entry_quick(coll_src.playlist,i,&entry))sec+=(entry.duration_ms+500)/1000;}
  else{char *p=cpath;if(!player_resolve(&coll_src,i,p)&&!library_find(p,&id)&&!library_song(id,&song))sec+=(song.duration_ms+500)/1000;}
  if(!(i&7))player_service();}
 if(known){unsigned h=sec/3600,m=sec/60%60;char *o=len;if(h){num_text(o,h);o+=strlen(o);*o++=':';*o++=(char)('0'+m/10);*o++=(char)('0'+m%10);}else{num_text(o,m);o+=strlen(o);}
  *o++=':';*o++=(char)('0'+sec%60/10);*o++=(char)('0'+sec%10);*o=0;}
 else strcpy(len,"?");
 const char *lab[5]={"Type","Name","Songs","Length",coll_kind==CK_ARTIST?"Albums":coll_kind==CK_FOLDER?"Path":""};
 const char *val[5]={kinds[coll_kind],s->title,songs,len,coll_kind==CK_ARTIST?extra:coll_kind==CK_FOLDER?coll_path+9:""};
 for(unsigned i=0;i<5;i++)if(lab[i][0]){int y=ROW_Y+(int)i*ROW_H;small_draw(6,y+5,lab[i],C_GREY);text_fit_draw(64,y+1,170,val[i],C_BLACK);player_service();}
}
static void coll_activate(struct Screen *s,unsigned id){
 switch(id){
  case CO_PLAY:coll_play(0);break;
  case CO_SHUFFLE:coll_play(1);break;
  case CO_NEXT:coll_play_next();break;
  case CO_ADD:{struct Screen *n=push(T_ADDTO,"Add to Playlist");n->aux=1;open_screen(n);break;}   /* aux 1: the collection */
  case CO_INFO:{struct Screen *n=push(T_INFO,"Info");n->aux=1;memcpy(n->title,s->title,PLAYLIST_NAME);break;}
  case CO_RENAME:if(coll_kind==CK_FOLDER)open_rename(coll_path,1);else open_tag_rename();break;
  case CO_DELETE:coll_confirm_delete();break;
  case CO_CONVERT:coll_batch(&cb);conv_confirm(2);break;   /* v2.0 */
 }
}
/* ---------- v2.0 conversion (Secret Settings > Conversion) ----------
 * Convert rows appear where Delete does (Song Options, album / artist /
 * genre / folder / playlist options) and as Convert all in Settings, only
 * while the Conversion switch is on and something of the source type is
 * there. A folder includes its subfolders (the library's songs under it). */
static int ieq_ext(const char *path,const char *ext){unsigned k=strlen(path);if(k<4||path[k-4]!='.')return 0;for(unsigned j=1;j<4;j++){char c=path[k-4+j];if(c>='A'&&c<='Z')c=(char)(c+32);if(c!=ext[j])return 0;}return 1;}
static int conv_source(const char *path){unsigned m=player_conversion();return m&&ieq_ext(path,m==1?".mp3":".gac");}
static uint32_t cb_items(const struct ConvBatch *b){return b->kind==CB_QUEUE?b->src->count:library_count();}
/* Item i of a batch: its path in out when it is a song to convert (0). */
static int cb_item(const struct ConvBatch *b,uint32_t i,char *out,uint32_t *ms){
 *ms=0;
 if(b->kind==CB_QUEUE){if(player_resolve(b->src,i,out)||!conv_source(out))return -1;uint32_t id;if(!library_find(out,&id)&&!library_song(id,&song))*ms=song.duration_ms;return 0;}
 if(library_song(i,&song)||!conv_source(song.path))return -1;
 if(b->kind==CB_FOLDER){unsigned a=strlen(b->folder);if(memcmp(song.path,b->folder,a)||song.path[a]!='/')return -1;}
 memcpy(out,song.path,STORAGE_PATH);*ms=song.duration_ms;return 0;
}
/* Count (all: n and total length; first: stop at the first song found). */
static uint32_t cb_count(struct ConvBatch *b,int first){
 b->n=0;b->ms=0;b->mode=(uint8_t)player_conversion();if(!b->mode)return 0;char *p=cpath;uint32_t ms;
 for(uint32_t i=0,n=cb_items(b);i<n;i++){if(!cb_item(b,i,p,&ms)){b->n++;b->ms+=ms;if(first)break;}if(!(i&15))player_service();}
 return b->n;
}
/* The open collection (coll_kind, coll_src, coll_path) as a batch: a folder
 * with its subfolders, the others their songs. */
static struct ConvBatch *coll_batch(struct ConvBatch *b){
 if(coll_kind==CK_FOLDER){b->kind=CB_FOLDER;b->folder=coll_path;}
 else{b->kind=CB_QUEUE;b->src=&coll_src;}
 return b;
}
/* Cached per collection, library and switch setting: entering its options
 * again (back from Info) does not search again. */
static uint32_t cc_key[6];static unsigned cc_have;
static unsigned coll_convertible(void){
 unsigned m=player_conversion();if(!m)return 0;
 uint32_t h=2166136261u;for(const char *q=coll_kind==CK_FOLDER?coll_path:coll_src.text;*q;q++)h=(h^(uint8_t)*q)*16777619u;
 uint32_t k[6]={coll_kind|m<<8|(uint32_t)coll_src.kind<<16,coll_src.start,coll_src.count,coll_src.playlist,library_generation(),h};
 if(memcmp(k,cc_key,sizeof(k))){memcpy(cc_key,k,sizeof(k));cc_have=cb_count(coll_batch(&cbt),1)!=0;}
 return cc_have;
}
/* Settings > Convert all: shown when the library holds a song to convert
 * (checked once per library and switch setting). */
static uint32_t conv_all_gen=~0u;static unsigned conv_all_mode,conv_all_have;
static unsigned conv_all_shown(void){
 unsigned m=player_conversion();if(!m)return 0;
 if(conv_all_gen!=library_generation()||conv_all_mode!=m){conv_all_gen=library_generation();conv_all_mode=m;cbt.kind=CB_ALL;conv_all_have=cb_count(&cbt,1)!=0;}
 return conv_all_have;
}
/* "About 2 h 15 min" for the batch: MP3 -> GAC about 9x the length,
 * GAC -> MP3 about 10.5x (measured on the GBA). */
static void dur_text(char *o,uint32_t sec){
 uint32_t m=(sec+59)/60;if(!m)m=1;
 if(m>=60){num_text(o,m/60);o+=strlen(o);memcpy(o," h ",3);o+=3;}num_text(o,m%60?m%60:m>=60?0:m);o+=strlen(o);memcpy(o," min",5);
}
static void conv_confirm(unsigned levels){
 if(!cb_count(&cb,0)){flash("Nothing to convert");return;}
 struct Screen *n=push(T_CONFIRM,conv_label());n->aux=2;n->p2=levels;open_screen(n);
}
static uint32_t cb_estimate(void){uint64_t s=(uint64_t)cb.ms*(cb.mode==2?21u:18u)/2000u;return (uint32_t)s;}
void draw_conv_confirm(void){
 static char a[32],b[40];num_text(a,cb.n);unsigned k=strlen(a);memcpy(a+k,cb.n==1?" song":" songs",cb.n==1?6:7);
 memcpy(b,"About ",6);dur_text(b+6,cb_estimate());
 fill(0,ROW_Y,240,3*ROW_H+4,C_WHITE);
 center_text(ROW_Y+2,a,C_BLACK);center_text(ROW_Y+2+ROW_H,b,C_BLACK);center_text(ROW_Y+2+2*ROW_H,"Replaces the originals. OK?",C_BLACK);
}
/* The run: one song after another; the screen shows the song, its progress
 * and the time left. B cancels (the song being converted is dropped, the
 * ones done stay); L+R turns the screen off, START on again. */
static struct {uint32_t i,n,done_ms,cur_ms,t_last;uint64_t ticks,t_draw;unsigned bar,dark,cancel,keys;char title[META_TITLE];} cr;
#define KEYS (*(volatile uint16_t*)0x4000130)
static void conv_screen(void){
 gfx_screen(1);gfx_ready();header(conv_label());fill(0,ROW_Y,240,144,C_WHITE);
 static char t[40];memcpy(t,"Song ",5);num_text(t+5,cr.i+1);unsigned k=strlen(t);memcpy(t+k," of ",4);num_text(t+k+4,cr.n);center_text(ROW_Y+6,t,C_BLACK);
 text_fit_draw(8,ROW_Y+6+ROW_H+4,224,cr.title,C_BLACK);
 fill(8,ROW_Y+6+2*ROW_H+12,224,6,C_LIGHT);cr.bar=0;
 small_draw((240-(int)small_width("B: Cancel   L+R: Screen off"))/2,146,"B: Cancel   L+R: Screen off",C_GREY);
 gfx_commit_now();cr.t_draw=0;
}
static void conv_time_left(void){
 /* Measured speed once 20 s have passed, else the estimate's. */
 uint64_t done=(uint64_t)cr.done_ms;uint32_t left_ms=cb.ms>cr.done_ms?cb.ms-cr.done_ms:0;uint32_t sec;
 if(cr.ticks>(uint64_t)20*16777216u&&done>1000)sec=(uint32_t)((uint64_t)left_ms*(cr.ticks/16777u)/done/1000u);
 else sec=(uint32_t)((uint64_t)left_ms*(cb.mode==2?21u:18u)/2000u);
 static char t[40];memcpy(t,"About ",6);dur_text(t+6,sec);{unsigned k=strlen(t);memcpy(t+k," left",6);}
 fill(0,ROW_Y+6+3*ROW_H+16,240,ROW_H,C_WHITE);center_text(ROW_Y+6+3*ROW_H+16,t,C_GREY);
}
static int conv_poll(void *ctx,uint32_t done,uint32_t total){
 (void)ctx;unsigned now=player_ticks();cr.ticks+=(uint32_t)(now-cr.t_last);cr.t_last=now;
 uint32_t ms=total?(uint32_t)((uint64_t)cr.cur_ms*(done<total?done:total)/total):0;
 unsigned latched=player_keys_take(),keys=~KEYS&0x3ff,fresh=latched&~(cr.keys&keys);cr.keys=keys;   /* a press since the last update counts */
 if(cr.dark){if(fresh&KEY_START){cr.dark=0;conv_screen();}return 0;}
 if(((latched|keys)&(KEY_L|KEY_R))==(KEY_L|KEY_R)&&(fresh&(KEY_L|KEY_R))){cr.dark=1;gfx_screen(0);return 0;}
 if(fresh&KEY_B){cr.cancel=1;return 1;}
 unsigned w=total?(unsigned)((uint64_t)224*(done<total?done:total)/total):0;
 if(w>cr.bar){fill(8+(int)cr.bar,ROW_Y+6+2*ROW_H+12,(int)(w-cr.bar),6,C_BLACK);cr.bar=w;gfx_commit_now();}
 if(!cr.t_draw||cr.ticks-cr.t_draw>(uint64_t)2*16777216u){uint32_t keep=cr.done_ms;cr.done_ms+=ms;conv_time_left();cr.done_ms=keep;cr.t_draw=cr.ticks|1;gfx_commit_now();}
 return 0;
}
static void conv_run(unsigned levels){
 player_stop_all();
 char *p=cpath;static char dst[STORAGE_PATH];uint32_t ms,ok=0,failed=0,skipped=0;
 memset(&cr,0,sizeof(cr));cr.n=cb.n;cr.t_last=player_ticks();(void)player_keys_take();cr.keys=~KEYS&0x3ff;
 for(uint32_t i=0,n=cb_items(&cb);i<n&&!cr.cancel;i++){
  if(cb_item(&cb,i,p,&ms))continue;
  unsigned k=strlen(p);if(k+1>=STORAGE_PATH)continue;memcpy(dst,p,k+1);memcpy(dst+k-3,cb.mode==1?"gac":"mp3",3);
  {const char *b=p;for(const char *q=p;*q;q++)if(*q=='/')b=q+1;uint32_t id;
   const char *t=!library_find(p,&id)&&!library_song(id,&song)&&!song.title_from_name?song.title:b;unsigned l=strlen(t);if(l>=META_TITLE)l=META_TITLE-1;memcpy(cr.title,t,l);cr.title[l]=0;}
  cr.cur_ms=ms;if(!cr.dark)conv_screen();
  int r=player_convert(p,dst,conv_poll,0);
  if(r==0){if(player_replace_original(p,dst)==-1)failed++;else ok++;}
  else if(r==-2)break;            /* CONVERT_CANCELLED */
  else if(r==-6)skipped++;        /* CONVERT_EXISTS: a file of that name is already there */
  else if(r==-1&&!playlist_exists(p)&&playlist_exists(dst)){}   /* listed twice: converted already */
  else failed++;
  cr.done_ms+=ms;cr.i++;
 }
 player_keys_release();
 if(cr.dark)gfx_screen(1);
 input_consume(KEY_A|KEY_B|KEY_L|KEY_R|KEY_START);
 run_update_and_back(levels);
 static char m[48];char *o=m;
 if(cr.cancel){memcpy(o,"Cancelled: ",11);o+=11;}
 num_text(o,ok);o+=strlen(o);memcpy(o," converted",11);o+=10;
 if(failed){memcpy(o,", ",2);num_text(o+2,failed);o+=strlen(o);memcpy(o," failed",8);o+=7;}
 if(skipped){memcpy(o,", ",2);num_text(o+2,skipped);o+=strlen(o);memcpy(o," skipped",9);}
 flash(m);
}
/* ---------- input per screen ---------- */
/* SELECT (outside Search and naming): Settings from any screen. */
static void open_settings(void){
 for(unsigned i=depth;i-->1;)if(stack[i].type==T_SETTINGS){if(i+1==depth)return;depth=i+1;enter(top());redraw();return;}
 struct Screen *n=push(T_SETTINGS,"Settings");open_screen(n);
}
/* Secret Settings rows: 0 Reorder menus, 1 Auto-update library (v1.5),
 * 2 Show Songs, 3 Letter Box (v1.8), 4 Rename files, 5 Delete files
 * (v1.9), 6 Conversion (v2.0), 7 Hold START to unlock (v2.2), 8 Delete
 * configuration, 9 Delete library index.
 * p0 = the row asking "Sure?", SECRET_NONE when none; p1 =
 * Show Songs when the screen opened (a change runs Update Library on B). */
#define SECRET_NONE 15
static void secret_activate(struct Screen *s,uint32_t i){
 if(i<8){if(i==0)player_menu_toggle();else if(i==1)player_auto_update_toggle();else if(i==2)player_song_types_next();else if(i==3)player_letter_box_toggle();else if(i==6)player_conversion_toggle();else if(i==7)player_hold_unlock_toggle();else player_file_ops_toggle(i==4?1u:2u);
  invalidate_rows();cache_page(s);row_dirty|=1u<<(s->sel-s->top);}
 else if(s->p0!=i){s->p0=i;invalidate_rows();cache_page(s);row_dirty|=1u<<(s->sel-s->top);}
 else{
  int e=i==8?player_delete_configuration():player_delete_library();
  /* An unlink may partly succeed even when the operation reports failure.
   * Discard every old screen and accept no more input until power cycling. */
  restart_failed=e!=0;message=0;message_frames=0;depth=0;push(T_RESTART,0);
 }
}
static void mark_menu_row(struct Screen *s){
 invalidate_rows();cache_page(s);row_dirty|=1u<<(s->sel-s->top);
}
/* START: Song Options for the song under the cursor (Now Playing: the song
 * that plays); Playlist Options on the playlist list. */
static void on_start(struct Screen *s){
 switch(s->type){
  case T_NOW:open_now_options();break;
  case T_FOLDER:if(s->count){if(s->sel<dir_count())open_folder_options(s,s->sel);else open_options(s,s->sel);}break;   /* v1.9: folder rows */
  case T_SONGS:case T_PLAYLIST:if(s->count)open_options(s,s->sel);break;
  case T_GROUPS:open_group_options(s,s->sel);break;   /* v1.9: albums, artists, genres */
  case T_PLAYLISTS:if(s->count&&s->sel!=new_row(s)){uint32_t id,idx=pl_index(s->sel);if(playlist_info(idx,&id,name_buf))break;struct Screen *n=push(T_PLOPTIONS,"Playlist Options");n->p0=id;n->p1=idx;
   memset(&coll_src,0,sizeof(coll_src));coll_src.kind=QUEUE_PLAYLIST;coll_src.playlist=id;coll_src.count=playlist_length(id);coll_kind=CK_PLAYLIST;coll_albums=0;open_screen(n);}break;
  default:break;
 }
}
static void activate(struct Screen *s){
 uint32_t i=s->sel;
 if(menu_kind(s)>=0)i=menu_id(s,i); /* stable action, never displayed position */
 switch(s->type){
  case T_HOME:
   switch(i){
    case 0:open_now();break;
    case 1:{struct Screen *n=push(T_PLAYLISTS,"Playlists");open_screen(n);}break;
    case 2:{struct Screen *n=push(T_FOLDER,"Folders");memcpy(n->path,"0:/gbamp3",10);open_screen(n);}break;
    case 3:{search_query[0]=0;struct Screen *n=push(T_SEARCH,"Search");open_screen(n);}break;
    case 4:case 5:case 6:{static const unsigned kinds[]={GROUP_ARTISTS,GROUP_ALBUMS,GROUP_GENRES};static const char *const t[]={"Artists","Albums","Genres"};
     struct Screen *n=push(T_GROUPS,t[i-4]);n->p0=kinds[i-4];n->p1=0;n->count=library_groups(kinds[i-4]);open_screen(n);}break;
    case 7:{struct Screen *n=push(T_SONGS,"Songs");n->aux=1;n->p0=PERM_TITLE;n->p1=0;n->count=library_count();open_screen(n);}break;
   }
   break;
  case T_SETTINGS:
   if(i==0){player_set_mode((player_mode()+1)%MODE_COUNT);mark_menu_row(s);}
   else if(i==1)run_update();
   else if(i==2){player_set_stereo(!player_stereo());mark_menu_row(s);}
   else if(i==3){player_set_boost_on(!player_boost_on());mark_menu_row(s);}
   else if(i==4){player_set_search_field((player_search_field()+1)%4);mark_menu_row(s);}
   else if(i==6){player_song_files_toggle();mark_menu_row(s);}
   else if(i==SETTING_CONVERT_ALL){cb.kind=CB_ALL;conv_confirm(1);}   /* v2.0 */
   else{struct Screen *n=push(T_ABOUT,"About");n->p0=0;}
   break;
  case T_PLAYLIST:if(i==add_row(s)){open_add_songs(s);break;}   /* 11.0 */
   play_from(s,i);break;
  case T_SONGS:case T_SEARCH:if(s->count)play_from(s,i);break;
  case T_GROUPS:
   if(!s->count||library_group(s->p0,s->p1+i,&group)||library_song(group.rep,&song))break;
   {const char *nm=group_name(s->p0,&song);
    if(s->p0==GROUP_ARTISTS){struct Screen *n=push(T_ARTIST,nm);n->p0=group.start;n->p1=group.count;n->p2=group.sub_start;n->p3=group.sub_count;open_screen(n);}
    else{struct Screen *n=push(T_SONGS,nm);n->p0=s->p0==GROUP_ALBUMS?PERM_ALBUM:s->p0==GROUP_GENRES?PERM_GENRE:PERM_ARTIST;n->p1=group.start;n->count=group.count;open_screen(n);}}
   break;
  case T_ARTIST:{char t[PLAYLIST_NAME];memcpy(t,s->title,PLAYLIST_NAME);
   if(i==0){struct Screen *n=push(T_SONGS,t);n->p0=PERM_ARTIST;n->p1=s->p0;n->count=s->p1;open_screen(n);}
   else{struct Screen *n=push(T_GROUPS,t);n->p0=GROUP_ARTIST_ALBUMS;n->p1=s->p2;n->count=s->p3;open_screen(n);}}
   break;
  case T_FOLDER:{unsigned d=dir_count();
   if(i<d){if(folder_name(&dirs,i,name_buf,sizeof(name_buf)))break;static char child[STORAGE_PATH];if(!folder_join(child,s->path,name_buf))break;
    struct Screen *n=push(T_FOLDER,name_buf);memcpy(n->path,child,STORAGE_PATH);open_screen(n);}
   else if(s->count)play_from(s,i);}
   break;
  case T_PLAYLISTS:
   if(i==new_row(s)){struct Screen *n=push(T_NAME,"New Playlist");n->aux=NAME_CREATE;writer_begin("");message_frames=0;}
   else{uint32_t id;if(playlist_info(pl_index(i),&id,name_buf))break;struct Screen *n=push(T_PLAYLIST,name_buf);n->p0=id;open_screen(n);}
   break;
  case T_ADDTO:{static char p[STORAGE_PATH];memcpy(p,s->path,STORAGE_PATH);
   if(i==new_row(s)){struct Screen *n=push(T_NAME,"New Playlist");n->aux=NAME_CREATE_ADD;n->p3=s->aux;memcpy(n->path,p,STORAGE_PATH);writer_begin("");message_frames=0;}
   else if(s->aux){uint32_t id;if(!playlist_info(pl_index(i),&id,0))coll_add_to(id);depth=depth>=3?depth-2:1;enter(top());redraw();}   /* v1.9: a collection's songs */
   else{uint32_t id;if(!playlist_info(pl_index(i),&id,0))add_to_playlist(id,p);depth=depth>=3?depth-2:1;enter(top());redraw();}}
   break;
  case T_OPTIONS:{switch(i){
   case OPTION_PLAY:if(options_current(s)){player_toggle();pop();}
    else{if(s->has_src)player_play(&opt_src,opt_src_index);else{struct QueueSource *q=new_source();q->kind=QUEUE_SINGLE;memcpy(q->text,s->path,STORAGE_PATH);player_play(q,0);}depth--;open_now();}break;
   case OPTION_NEXT:if(player_play_next(s->path))flash("Queue full");pop();break;
   case OPTION_ADD:{static char p[STORAGE_PATH];memcpy(p,s->path,STORAGE_PATH);unsigned inpl=s->aux;uint32_t pl=s->p0;struct Screen *n=push(T_ADDTO,"Add to Playlist");memcpy(n->path,p,STORAGE_PATH);
    if(inpl){n->p2=1;n->p1=pl;}   /* 11.0: not the playlist the song is in */
    open_screen(n);break;}
   case OPTION_INFO:{static char p[STORAGE_PATH];memcpy(p,s->path,STORAGE_PATH);struct Screen *n=push(T_INFO,"Song Info");memcpy(n->path,p,STORAGE_PATH);break;}
   case OPTION_REMOVE:{uint32_t id=s->p0,at=s->p1;player_fill();if(!playlist_remove(id,at)){player_playlist_removed(id,at);pl_hide_removed(id,at);}else flash("Save failed");pop();break;}
   case OPTION_RENAME:{static char p[STORAGE_PATH];memcpy(p,s->path,STORAGE_PATH);open_rename(p,0);break;}   /* v1.9 */
   case OPTION_DELETE:memset(&coll_src,0,sizeof(coll_src));coll_src.kind=QUEUE_SINGLE;memcpy(coll_src.text,s->path,STORAGE_PATH);coll_src.count=1;coll_kind=CK_SONG;coll_confirm_delete();break;
   case OPTION_CONVERT:memset(&coll_src,0,sizeof(coll_src));coll_src.kind=QUEUE_SINGLE;memcpy(coll_src.text,s->path,STORAGE_PATH);coll_src.count=1;coll_kind=CK_SONG;coll_batch(&cb);conv_confirm(2);break;   /* v2.0 */
   }}
   break;
  case T_PLOPTIONS:
   if(i==PLOPT_PLAY){if(playlist_length(s->p0)){struct QueueSource *q=new_source();q->kind=QUEUE_PLAYLIST;q->playlist=s->p0;q->count=playlist_length(s->p0);player_play(q,0);depth--;open_now();}}
   else if(i==1){uint32_t id=s->p0;unsigned idx=s->p1;playlist_info(idx,0,name_buf);struct Screen *n=push(T_NAME,"Rename");n->aux=NAME_RENAME;n->p0=id;n->p1=idx;writer_begin(name_buf);message_frames=0;}
   else if(i==2){uint32_t idx=s->p1;struct Screen *n=push(T_CONFIRM,"Delete Playlist?");n->p1=idx;open_screen(n);}
   else coll_activate(s,i==PLOPT_SHUFFLE?CO_SHUFFLE:i==PLOPT_NEXT?CO_NEXT:i==PLOPT_ADD?CO_ADD:i==PLOPT_CONVERT?CO_CONVERT:CO_INFO);   /* v1.9; v2.0 Convert */
   break;
  case T_CONFIRM:
   if(i==1&&s->aux==2)conv_run(s->p2);     /* v2.0: conversion */
   else if(i==1&&s->aux==1)coll_delete_run();   /* v1.9: song files */
   else if(i==1){player_fill();if(playlist_delete(s->p1))flash("Delete failed");depth=depth>=3?depth-2:1;enter(top());redraw();}
   else pop();
   break;
  case T_COPTIONS:coll_activate(s,coll_id(i));break;
  case T_SECRET:secret_activate(s,i);break;
  default:break;
 }
}
static void reorder(struct Screen *s,int d){
 if(!s->count)return;uint32_t a=s->sel,b;
 if(d<0){if(!a)return;b=a-1;}else{if(a+1>=s->count)return;b=a+1;}
 if(s->type==T_PLAYLIST){uint32_t ar=add_row(s);
  /* 11.0: "+ Add Songs" goes to the very top / bottom like + New Playlist
   * (Reorder menus on); songs move past each other, never past it. */
  if(a==ar){if(!(player_menus()->enabled&MENU_ORDER_ON))return;
   if(d<0&&!add_top()){player_set_add_songs_top(1);b=0;}
   else if(d>0&&add_top()){player_set_add_songs_top(0);b=s->count-1;}
   else return;}
  else{if(b==ar)return;uint32_t ea=pl_entry(s,a),eb=pl_entry(s,b);player_fill();if(playlist_swap(s->p0,ea,eb)){flash("Save failed");return;}player_playlist_moved(s->p0,ea,eb);}}
 else if(s->type==T_PLAYLISTS){uint32_t nr=new_row(s);
  /* v1.8: with Reorder menus on, "+ New Playlist" goes to the very top (Up)
   * or the very bottom (Down), never between playlists. */
  if(a==nr){if(!(player_menus()->enabled&MENU_ORDER_ON))return;
   if(d<0&&!player_new_playlist_top()){player_set_new_playlist_top(1);b=0;}
   else if(d>0&&player_new_playlist_top()){player_set_new_playlist_top(0);b=s->count-1;}
   else return;}
  else{if(b==nr)return;player_fill();if(playlist_move(pl_index(a),pl_index(b))){flash("Save failed");return;}}}
 else if(menu_kind(s)>=0){if(player_menu_exchange((unsigned)menu_kind(s),menu_id(s,a),menu_id(s,b)))return;}
 else return;
 s->sel=b;ensure_visible(s);invalidate_rows();cache_page(s);full_dirty=1;
}
static void name_commit(struct Screen *s){
 if(s->aux==NAME_FILE){file_rename_commit(s);return;}   /* v1.9 */
 if(s->aux==NAME_TAG){tag_rename_commit(s);return;}    /* v1.9.1 */
 const char *t=writer_text();int e;uint32_t id;
 player_fill();
 if(s->aux==NAME_RENAME)e=playlist_rename(s->p1,t);
 else e=playlist_create(t,&id);
 if(e==-3){flash("Name cannot be empty");return;}
 if(e==-2){flash("Name already exists");return;}
 if(e){flash("Save failed");return;}
 if(s->aux==NAME_CREATE_ADD&&s->p3){coll_add_to(id);depth=depth>=4?depth-3:1;enter(top());redraw();return;}   /* v1.9: a collection */
 if(s->aux==NAME_CREATE_ADD){static char p[STORAGE_PATH];memcpy(p,s->path,STORAGE_PATH);add_to_playlist(id,p);
  /* Back to the screen that opened Song Options. */
  depth=depth>=4?depth-3:1;enter(top());redraw();return;}
 pop();
}
static void secret_action_input(struct Screen *s,const struct InputFrame *in){
 if((in->tap&KEY_B)||((in->tap&KEY_A)&&(in->held&KEY_B))){
  /* B cancels even if A releases first. Consume the companion's release
   * tail so About page 5 cannot reopen Secret Settings. */
  input_consume(KEY_A|KEY_B);
  /* B on "Sure?" only takes the question back; the screen stays. */
  if(s->p0!=SECRET_NONE){unsigned prior=s->p0;s->p0=SECRET_NONE;invalidate_rows();cache_page(s);if(prior>=s->top)row_dirty|=1u<<(prior-s->top);}
  /* v1.8: a new Show Songs choice rebuilds the library now (music is off). */
  else if(player_song_types()!=s->p1){run_update();if(top()->type==T_SECRET)depth--;}
  else if(depth>1)pop();
 }else if(in->tap&KEY_A)secret_activate(s,s->sel);
}
static void lock(unsigned on){
 locked=on;stats.locked=on;
 if(on){box_off();gfx_screen(0);}   /* the picture off: black backdrop */
 else{gfx_screen(1);redraw();unlock_guard=1;unlock_quiet=0;input_consume(0x3ff);}
}
void ui_init(void){
 {unsigned n;uint8_t *z=library_scratch(&n);folder_sort_area(&dirs,z,n/4);folder_sort_area(&songs,z+n/4,n/4+n/8);}   /* 16 + 24 KB */
/* v1.6: sprites for the letter box: all others off, the box's colours,
 * alpha blending of the box over the bitmap (and backdrop) for the fade. */
 for(unsigned i=0;i<128;i++)OAM[i*4]=0x0200;
 {volatile uint32_t zero=0;dma_words(BOX_TILES,&zero,512,1);}
 OBJ_PALETTE[1]=C_BOX;OBJ_PALETTE[2]=C_WHITE;REG_BLDCNT=0x2400;box_alpha(16);
 gfx_init();depth=0;push(T_HOME,0);enter(top());}   /* v1.7: mode 4 */
/* v1.5: Secret Settings > Auto-update library: Update Library at every start
 * (the card must be readable; the library screens show the result). */
void ui_boot(void){if(player_auto_update()&&!player_fatal())run_update();else redraw();}
static void writer_screen_frame(struct Screen *s,const struct InputFrame *in){
 if(s->type==T_SEARCH&&(in->pressed&KEY_B))add_b_empty=!writer_text()[0];   /* 11.0: B pressed with nothing typed */
 unsigned f=writer_frame(in->held);
 /* A START command (START+A/B/SELECT) used the keys still held: as in
  * GBAWriter, releasing them afterwards does nothing, also on the screen the
  * command opened (START would open Song Options, A toggle play/pause). */
 if(f&(WRITER_START_A|WRITER_START_B|WRITER_START_SELECT))input_consume(KEY_START|KEY_A|KEY_B|KEY_SELECT);
 if(s->type==T_NAME){
  if(f&WRITER_START_B){pop();return;}
  if(f&WRITER_START_A){name_commit(s);if(top()==s)draw_name(s,0);return;}
  if(f&(WRITER_TEXT|WRITER_FULL)){if(f&WRITER_FULL)flash("Name too long");draw_name(s,0);}
  else if(frame_count%8==0)draw_writer_status();
  return;
 }
 if(s->aux){   /* 11.0 + Add Songs: START+B (or B with nothing typed) back to the playlist, START+A adds */
  if((f&WRITER_START_B)||((in->tap&KEY_B)&&add_b_empty&&!writer_text()[0]&&!(in->held&~KEY_B))){add_b_empty=0;input_consume(KEY_B);pop();return;}
  if(f&WRITER_START_A){static char p[STORAGE_PATH];if(s->count&&!row_song(s,s->sel,p)&&!add_to_playlist(add_target,p))flash("Added");return;}
 }
 if(f&WRITER_START_B){depth=1;enter(top());redraw();return;}
 if(f&WRITER_START_A){if(s->count)play_from(s,s->sel);return;}
 if(f&(WRITER_START_SELECT|WRITER_START_ALONE)){if(s->count)open_options(s,s->sel);return;}
 if(f&WRITER_UP)move_sel(s,-1);
 if(f&WRITER_DOWN)move_sel(s,1);
 if(f&WRITER_PAGE_PREV)page_sel(s,-1);
 if(f&WRITER_PAGE_NEXT)page_sel(s,1);
 if(f&WRITER_TEXT){if(strcmp(search_query,writer_text()))search_update(s);
  draw_field(ROW_Y,writer_text(),writer_caret());draw_writer_status();}
 else if(frame_count%8==0)draw_writer_status();
}
static void ui_frame_body(const struct InputFrame *in);
/* v1.5 test telemetry (fields unused before): screen type | letter box << 8,
 * selection, top row, first byte of the box label. */
void ui_frame(const struct InputFrame *in){
 gfx_ready();   /* v1.7: the hidden page is free to draw on */
 static struct InputFrame quiet;   /* static: ui_frame's stack frame stays small */
 if(unlock_guard){   /* buttons do nothing until all are up for a moment */
  quiet=*in;quiet.pressed=quiet.tap=quiet.repeat=0;quiet.lr_chord=quiet.a_chord=0;
  for(int j=0;j<2;j++)quiet.rows[j]=quiet.letters[j]=quiet.pages[j]=0;
  input_consume(0x3ff);
  if(in->held||in->tap)unlock_quiet=0;else if(++unlock_quiet>=UNLOCK_QUIET)unlock_guard=0;
  in=&quiet;
 }
 ui_frame_body(in);
 const struct Screen *s=top();
 if(!(is_list(s)&&row_dirty)&&!(s->type==T_NOW&&np_stage))gfx_commit();   /* a list is shown only once all its rows are painted; Now Playing once all its sections are */
 stats.browser_open=s->type|(box_on?(box_fade?0x200u:0x100u):0u);stats.browser_selection=s->sel;stats.browser_error=s->top;stats.info_page=box_on?(uint8_t)box_label[0]:s->type==T_ABOUT||s->type==T_SECRET?s->p0:0;   /* About: page; Secret Settings: the row asking Sure? (15: none) */
}
static void ui_frame_body(const struct InputFrame *in){
 frame_count++;
 if(top()->type==T_RESTART){if(full_dirty){full_dirty=0;draw_restart();}return;}
 if(message_frames){message_frames--;if(!message_frames){message=0;if(top()->type!=T_NAME)full_dirty=1;}}
 struct Screen *s=top();
 int options_changed=refresh_options(s);
 int writer=s->type==T_SEARCH||s->type==T_NAME;
 /* L+R locks (not while typing: L and R type letters); one START press
  * unlocks and does nothing else (v2.2 Hold START to unlock On: START held
  * 1 s; a shorter press does nothing). */
 if(locked){
  /* v1.9 Sleep: locked with nothing playing (or the music has ended):
   * the GBA stops until START, which also unlocks (Hold START to unlock:
   * player_sleep returns once START has been held 1 s). */
  if(!player_is_playing()){player_sleep();input_consume(KEY_START);lock(0);return;}
  if(player_hold_unlock()?in->hold_frames[3]>=UNLOCK_HOLD:(in->tap&KEY_START)!=0)lock(0);return;}
 if(in->lr_chord&&!writer){lock(1);return;}
 box_fade_step(in->elapsed);   /* v1.6 */
 if(full_dirty==2){if(message_frames)return;full_dirty=1;}
 if(writer)writer_screen_frame(s,in);
 else if(in->tap&KEY_SELECT)open_settings();
 else if(s->type==T_NOW){
  if(in->tap&KEY_A)player_toggle();
  if(in->tap&KEY_B)pop();
  if(in->tap&KEY_LEFT)player_prev();
  if(in->tap&KEY_RIGHT)player_next();
  if(in->repeat&KEY_LEFT)player_seek(-10);   /* v2.1.1: 10 s steps (5 s before) */
  if(in->repeat&KEY_RIGHT)player_seek(10);
  if(in->tap&KEY_START)on_start(s);
  /* Volume Boost: one step per press (no auto-repeat), only while on. */
  if(in->pressed&KEY_UP)player_boost_step(1);
  if(in->pressed&KEY_DOWN)player_boost_step(-1);
 }else if(s->type==T_ABOUT){
  if(in->tap&KEY_B)pop();
  else if(in->tap&KEY_RIGHT){if(s->p0+1<ABOUT_PAGES){s->p0++;full_dirty=1;}}   /* no wrap: Secret Settings stays 7 pages away */
  else if(in->tap&KEY_LEFT){if(s->p0){s->p0--;full_dirty=1;}}
  else if((in->tap&KEY_A)&&s->p0==ABOUT_PAGES-1){   /* v1.8: the music stops first */
   player_pause();struct Screen *n=push(T_SECRET,"Secret Settings");n->p0=SECRET_NONE;n->p1=player_song_types();enter(n);}
 }else if(s->type==T_INFO){
  if(in->tap&KEY_B)pop();
 }else{
  uint32_t previous_sel=s->sel;
  if(in->a_chord&&(in->repeat&KEY_UP))reorder(s,-1);
  else if(in->a_chord&&(in->repeat&KEY_DOWN))reorder(s,1);
  else if(long_list(s))long_list_input(s,in);   /* v1.5 */
  else if(in->repeat&KEY_UP){if(!s->sel&&(in->pressed&KEY_UP))jump_sel(s,s->count-1);else move_sel(s,-1);}
  else if(in->repeat&KEY_DOWN){if(s->count&&s->sel+1==s->count&&(in->pressed&KEY_DOWN))jump_sel(s,0);else move_sel(s,1);}
  if(s->type==T_SECRET&&s->sel!=previous_sel&&s->p0!=SECRET_NONE){unsigned prior=s->p0;s->p0=SECRET_NONE;
   invalidate_rows();cache_page(s);if(prior>=s->top)row_dirty|=1u<<(prior-s->top);}
  if(s->type==T_SECRET)secret_action_input(s,in);
  else if((in->tap&KEY_A)&&!options_changed)activate(s);
  else if(in->tap&KEY_B){if(depth>1)pop();}
  else if(in->tap&KEY_START)on_start(s);
 }
 s=top();
 if(full_dirty){full_dirty=0;
  back_from_now=last_full==T_NOW;last_full=s->type;
  switch(s->type){case T_NOW:draw_now(1);break;case T_ABOUT:draw_about(s);break;case T_INFO:draw_info(s);break;
   case T_RESTART:draw_restart();break;
   case T_NAME:draw_name(s,1);break;case T_SEARCH:draw_search(s,1);break;default:draw_list(s,1);}
  return;
 }
 if(s->type==T_NOW){draw_now(0);return;}
 if(s->type==T_SEARCH){refresh_play_flags();draw_search(s,0);if(s->count&&s->sel>=s->top)marquee_row(s,s->sel-s->top);
  if(message&&message_frames){fill(0,140,240,16,C_WHITE);small_draw((240-(int)small_width(message))/2,145,message,C_BLACK);}   /* 11.0: "Added" */
  return;}
 if(is_list(s)){
  if(frame_count%30==0)refresh_play_flags();
  if(long_list(s)){present(s);   /* v1.6 */
   if(s->count&&!row_dirty&&rc.top==s->top&&hl_sel==s->sel)marquee_row(s,s->sel-s->top);
   check_rows(s);}
  else{
   if(row_dirty)draw_list(s,0);
   if(s->count&&s->sel>=s->top)marquee_row(s,s->sel-s->top);}
  if(message&&message_frames){fill(0,140,240,16,C_WHITE);small_draw((240-(int)small_width(message))/2,145,message,C_BLACK);}
 }
}
