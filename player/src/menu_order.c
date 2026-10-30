/* Halim Jarrar, 2026. GPL-3.0-or-later with the additional permission described in NOTICES.md (linking with the Helix MP3 decoder). Stable menu identities and per-menu permutations. */
#include "menu_order.h"
static const uint8_t count[MENU_COUNT]={8,8,8,8};   /* v1.9: Song Options +2, Playlist Options +4; v2.0: Convert in Settings, Song Options, Playlist Options */
unsigned menu_order_files,menu_order_convert;
/* v1.6: Settings ID 6 "Show songs as" is shown before About (ID 5). The
 * unused last Settings slot marks the v1.6 layout (it was 7 before). */
#define SETTINGS_V16 0x80
/* v1.9: Playlist Options holds 7 entries; its spare last slot marks it. */
#define PLOPT_V19 0x80
static void playlist_options_v19(uint8_t *o){
 /* Play, Shuffle Play, Play Next, Add to Playlist, Info follow Play;
  * Rename and Delete keep their order after it. */
 uint8_t old[3]={o[0],o[1],o[2]},n=0;
 for(unsigned i=0;i<3;i++){o[n++]=old[i];if(old[i]==PLOPT_PLAY){o[n++]=PLOPT_SHUFFLE;o[n++]=PLOPT_NEXT;o[n++]=PLOPT_ADD;o[n++]=PLOPT_INFO;}}
 o[7]=PLOPT_V19;
}
/* v2.0: the seven entries of a menu get ID 7 right after `after`. */
static void insert_after(uint8_t *o,uint8_t after){
 uint8_t old[7];for(unsigned i=0;i<7;i++)old[i]=o[i];unsigned n=0;
 for(unsigned i=0;i<7;i++){o[n++]=old[i];if(old[i]==after)o[n++]=7;}
 if(n==7)o[n++]=7;   /* not found (cannot happen for a valid order) */
}
static void v20(struct MenuOrder *m){
 insert_after(m->order[MENU_SETTINGS],1);                 /* Convert all after Update Library */
 insert_after(m->order[MENU_OPTIONS],OPTION_INFO);        /* Convert after Song Info */
 insert_after(m->order[MENU_PLAYLIST_OPTIONS],PLOPT_INFO);/* Convert after Info */
 m->enabled|=MENU_ORDER_V20;
}
static void v19_reset(struct MenuOrder *m){
 for(unsigned k=0;k<MENU_COUNT;k++)for(unsigned i=0;i<8;i++)m->order[k][i]=(uint8_t)i;
 m->order[MENU_SETTINGS][5]=6;m->order[MENU_SETTINGS][6]=5;m->order[MENU_SETTINGS][7]=SETTINGS_V16;
 playlist_options_v19(m->order[MENU_PLAYLIST_OPTIONS]);
}
void menu_order_reset(struct MenuOrder *m){
 m->enabled=0;v19_reset(m);v20(m);
}
void menu_order_upgrade(struct MenuOrder *m){
 if(m->enabled&MENU_ORDER_V20)return;   /* v2.0 layout: the old markers' slots hold entries now */
 {uint8_t *p=m->order[MENU_PLAYLIST_OPTIONS];
  if(p[7]!=PLOPT_V19){unsigned ok=1,seen=0;for(unsigned i=0;i<3;i++){if(p[i]>2||(seen>>p[i]&1))ok=0;seen|=1u<<(p[i]&7);}
   if(ok)playlist_options_v19(p);else{for(unsigned i=0;i<8;i++)p[i]=(uint8_t)i;playlist_options_v19(p);}}}
 uint8_t *o=m->order[MENU_SETTINGS];
 if(o[7]!=SETTINGS_V16){
  /* Saved before v1.6: ID 6 sits in the former spare slot after the six
   * items; put it before About when About was last. */
  if(o[5]==5&&o[6]==6){o[5]=6;o[6]=5;}
  o[7]=SETTINGS_V16;
 }
 v20(m);
}
int menu_order_validate(const struct MenuOrder *m){
 if(m->enabled&~(MENU_ORDER_ON|MENU_ORDER_V20))return 0;
 for(unsigned k=0;k<MENU_COUNT;k++){
  /* before v2.0, Settings, Song Options and Playlist Options had 7 entries */
  unsigned c=(m->enabled&MENU_ORDER_V20)||k==MENU_HOME?count[k]:7u,seen=0;
  for(unsigned i=0;i<c;i++){unsigned id=m->order[k][i];if(id>=c||(seen&(1u<<id)))return 0;seen|=1u<<id;}
 }
 return 1;
}
static int visible(unsigned k,unsigned id,unsigned current,unsigned playlist){
 if(k==MENU_SETTINGS)return id!=SETTING_CONVERT_ALL||(menu_order_convert&1);
 /* 11.0: Add to Playlist is no longer offered for a playlist (it made a
  * playlist inside a playlist); its ID keeps its slot in the saved order. */
 if(k==MENU_PLAYLIST_OPTIONS)return id!=PLOPT_ADD&&(id!=PLOPT_CONVERT||(menu_order_convert&4));
 return k!=MENU_OPTIONS||((id!=OPTION_NEXT||!current)&&(id!=OPTION_REMOVE||playlist)
  &&(id!=OPTION_RENAME||(menu_order_files&1))&&(id!=OPTION_DELETE||(menu_order_files&2))&&(id!=OPTION_CONVERT||(menu_order_convert&2)));
}
unsigned menu_order_visible(const struct MenuOrder *m,unsigned k,unsigned current,unsigned playlist,uint8_t ids[8]){
 if(k>=MENU_COUNT)return 0;
 unsigned n=0;for(unsigned i=0;i<count[k];i++){uint8_t id=m->order[k][i];if(visible(k,id,current,playlist))ids[n++]=id;}
 return n;
}
int menu_order_exchange(struct MenuOrder *m,unsigned k,unsigned id_a,unsigned id_b){
 if(k>=MENU_COUNT||id_a>=count[k]||id_b>=count[k]||id_a==id_b)return -1;
 unsigned a=0,b=0;while(m->order[k][a]!=id_a)a++;while(m->order[k][b]!=id_b)b++;
 m->order[k][a]=(uint8_t)id_b;m->order[k][b]=(uint8_t)id_a;return 0;
}
int menu_order_swap(struct MenuOrder *m,unsigned k,unsigned from,unsigned to){
 uint8_t ids[8];unsigned n=menu_order_visible(m,k,0,1,ids);
 if(from>=n||to>=n||from==to)return -1;
 return menu_order_exchange(m,k,ids[from],ids[to]);
}
