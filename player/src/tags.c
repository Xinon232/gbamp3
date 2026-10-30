/* Halim Jarrar, 2026. GPL-3.0-or-later with the additional permission described in NOTICES.md (linking with the Helix MP3 decoder). v1.9.1: change the artist, album or genre tag of a
 * song file. See tags.h. Only the one frame / field changes; other frames
 * (pictures included) are kept byte for byte. */
#include "tags.h"
#include "ff.h"
#include "text.h"
#include <string.h>

static FIL *f,*t;   /* in the work memory (EWRAM is full) */
static int rd(FIL *x,uint32_t at,void *p,unsigned n){UINT g=0;return f_lseek(x,at)!=FR_OK||f_read(x,p,n,&g)!=FR_OK||g!=n?-1:0;}
static int wr(FIL *x,uint32_t at,const void *p,unsigned n){UINT g=0;return f_lseek(x,at)!=FR_OK||f_write(x,p,n,&g)!=FR_OK||g!=n?-1:0;}
static uint32_t fnv(const uint8_t *p,unsigned n){uint32_t h=2166136261u;while(n--)h=(h^*p++)*16777619u;return h;}
static void put_le32(uint8_t *p,uint32_t v){p[0]=(uint8_t)v;p[1]=(uint8_t)(v>>8);p[2]=(uint8_t)(v>>16);p[3]=(uint8_t)(v>>24);}
static unsigned synchsafe(const uint8_t *p){return (unsigned)(p[0]&127)<<21|(unsigned)(p[1]&127)<<14|(unsigned)(p[2]&127)<<7|(p[3]&127);}
static void put_synchsafe(uint8_t *p,uint32_t v){p[0]=(uint8_t)(v>>21&127);p[1]=(uint8_t)(v>>14&127);p[2]=(uint8_t)(v>>7&127);p[3]=(uint8_t)(v&127);}

/* GAC: the 576 bytes of header + GACMETA3, one field replaced, checksum renewed. */
static int gac_set(unsigned field,const char *text,uint8_t *b){
 if(rd(f,0,b,576))return -1;
 if(memcmp(b,"GBCACHE",8)||b[8]!=4||b[9]||memcmp(b+64,"GACMETA3",8))return -2;
 uint32_t sum=(uint32_t)b[52]|(uint32_t)b[53]<<8|(uint32_t)b[54]<<16|(uint32_t)b[55]<<24;
 if(fnv(b+64,512)!=sum)return -2;
 static const unsigned short at[3]={144,240,432},len[3]={96,96,64};
 uint8_t *p=b+64+at[field];unsigned n=strlen(text);if(n>=len[field])return -2;
 memset(p,0,len[field]);memcpy(p,text,n);
 put_le32(b+52,fnv(b+64,512));
 return wr(f,0,b,576)||f_sync(f)!=FR_OK?-1:0;
}

/* The new ID3v2 text frame. Returns its length, 0 if it cannot be written
 * (an unsynchronised tag needs a frame without 0xFF bytes). */
static unsigned build_frame(uint8_t *o,unsigned ver,unsigned field,const char *text,unsigned unsync){
 static const char *const id3[3]={"TP1","TAL","TCO"},*const id4[3]={"TPE1","TALB","TCON"};
 unsigned hs=ver==2?6:10,n=hs;
 unsigned latin=1;for(const char *s=text;*s;){uint32_t c=text_next(&s);if(c>0xff)latin=0;}
 if(ver==4){o[n++]=3;unsigned k=strlen(text);memcpy(o+n,text,k);n+=k;}
 else if(latin){o[n++]=0;for(const char *s=text;*s;)o[n++]=(uint8_t)text_next(&s);}
 else{o[n++]=1;o[n++]=0xff;o[n++]=0xfe;
  for(const char *s=text;*s;){uint32_t c=text_next(&s);
   if(c>=0x10000){c-=0x10000;uint32_t hi=0xd800+(c>>10),lo=0xdc00+(c&0x3ff);o[n++]=(uint8_t)hi;o[n++]=(uint8_t)(hi>>8);c=lo;}
   o[n++]=(uint8_t)c;o[n++]=(uint8_t)(c>>8);}}
 unsigned size=n-hs;
 if(ver==2){memcpy(o,id3[field],3);o[3]=(uint8_t)(size>>16);o[4]=(uint8_t)(size>>8);o[5]=(uint8_t)size;}
 else{memcpy(o,id4[field],4);if(ver==4)put_synchsafe(o+4,size);else{o[4]=(uint8_t)(size>>24);o[5]=(uint8_t)(size>>16);o[6]=(uint8_t)(size>>8);o[7]=(uint8_t)size;}o[8]=o[9]=0;}
 if(unsync)for(unsigned i=0;i<n;i++)if(o[i]==0xff)return 0;
 return n;
}
static int is_id(const uint8_t *p,unsigned k){for(unsigned i=0;i<k;i++)if(!((p[i]>='A'&&p[i]<='Z')||(p[i]>='0'&&p[i]<='9')))return 0;return 1;}

/* Copy len bytes from src to dst inside f (regions may overlap). */
static int move(uint32_t src,uint32_t dst,uint32_t len,uint8_t *b,unsigned cap){
 if(src==dst||!len)return 0;
 if(dst<src){for(uint32_t d=0;d<len;){unsigned k=len-d<cap?len-d:cap;if(rd(f,src+d,b,k)||wr(f,dst+d,b,k))return -1;d+=k;}}
 else{for(uint32_t d=len;d;){unsigned k=d<cap?d:cap;d-=k;if(rd(f,src+d,b,k)||wr(f,dst+d,b,k))return -1;}}
 return 0;
}
/* Append len bytes of f from src to t. */
static int append(uint32_t src,uint32_t len,uint8_t *b,unsigned cap){
 if(f_lseek(f,src)!=FR_OK)return -1;
 while(len){unsigned k=len<cap?len:cap;UINT g=0;
  if(f_read(f,b,k,&g)!=FR_OK||g!=k)return -1;if(f_write(t,b,k,&g)!=FR_OK||g!=k)return -1;len-=k;}
 return 0;
}
/* The finished PATH.tmp replaces PATH; PATH.old is kept until then. */
static __attribute__((noinline)) int replace_file(const char *path,const char *tmp,const char *old){
 f_unlink(old);
 if(f_rename(path,old)!=FR_OK)return -1;
 if(f_rename(tmp,path)!=FR_OK){f_rename(old,path);return -1;}
 f_unlink(old);return 0;
}

#define MAXT 8
static int mp3_set(const char *path,unsigned field,const char *text,uint8_t *b,unsigned cap){
 uint8_t h[10],fh[10],*nf=b;static uint32_t ts[MAXT],te[MAXT];b+=512;cap-=512;   /* nf: the new frame */
 uint32_t fsize=(uint32_t)f_size(f);
 unsigned ver=3,flags=0,size=0,tag=0;uint32_t audio=0,used=10;
 if(fsize>=10&&rd(f,0,h,10))return -1;
 if(fsize>=10&&!memcmp(h,"ID3",3)){
  if(h[3]<2||h[3]>4||h[4]==0xff||(h[6]|h[7]|h[8]|h[9])&0x80)return -2;
  ver=h[3];flags=h[5];size=synchsafe(h+6);tag=1;
  if(ver==4&&(flags&0x80))return -2;          /* unsynchronised frames: not handled */
  if(ver>=3&&(flags&0x40))return -2;          /* extended header (CRC, restrictions) */
  if(ver==2&&(flags&0x40))return -2;          /* compressed v2.2 tag */
  audio=10+size+((ver==4&&(flags&0x10))?10:0);
  if(audio>fsize)return -2;
 }
 static const char *const id3[3]={"TP1","TAL","TCO"},*const id4[3]={"TPE1","TALB","TCON"};
 unsigned hs=ver==2?6:10,nt=0;
 if(tag){
  uint32_t pos=10,end=10+size;
  while(pos+hs<=end){
   if(rd(f,pos,fh,hs))return -1;
   if(!is_id(fh,ver==2?3:4))break;   /* padding (zeros) or the end of the frames */
   uint32_t n=ver==2?(uint32_t)fh[3]<<16|fh[4]<<8|fh[5]:ver==4?synchsafe(fh+4):(uint32_t)fh[4]<<24|fh[5]<<16|fh[6]<<8|fh[7];
   if(n>end-pos-hs)return -2;          /* a frame runs past the tag */
   if(!memcmp(fh,ver==2?id3[field]:id4[field],ver==2?3:4)){if(nt==MAXT)return -2;ts[nt]=pos;te[nt]=pos+hs+n;nt++;}
   pos+=hs+n;
  }
  used=pos;
 }
 unsigned nl=build_frame(nf,ver,field,text,tag&&(flags&0x80));if(!nl)return -2;
 uint32_t removed=0;for(unsigned i=0;i<nt;i++)removed+=te[i]-ts[i];
 uint32_t need=used-removed+nl;
 /* In place: frames up to the first old frame stay; the rest close up behind
  * the new frame; the space after them becomes padding. */
 if(tag&&!(flags&0x10)&&need<=10+size){
  uint32_t at=nt?ts[0]:used;           /* where the new frame goes */
  static uint32_t ss[MAXT],sl[MAXT],sd[MAXT];unsigned ns=0;uint32_t d=at+nl;
  for(unsigned i=0;i<nt;i++){uint32_t s=te[i],e=i+1<nt?ts[i+1]:used;if(e>s){ss[ns]=s;sl[ns]=e-s;sd[ns]=d;d+=e-s;ns++;}}
  /* Segments moving left go first (front to back), then those moving right (back to front). */
  for(unsigned i=0;i<ns;i++)if(sd[i]<ss[i]&&move(ss[i],sd[i],sl[i],b,cap))return -1;
  for(unsigned i=ns;i-->0;)if(sd[i]>ss[i]&&move(ss[i],sd[i],sl[i],b,cap))return -1;
  if(wr(f,at,nf,nl))return -1;
  if(d<used){unsigned z=cap<512?cap:512;memset(b,0,z);for(uint32_t p=d;p<used;){unsigned k=used-p<z?used-p:z;if(wr(f,p,b,k))return -1;p+=k;}}
  return f_sync(f)!=FR_OK?-1:0;
 }
 /* Rewrite: PATH.tmp with the new tag (2 KB padding), then the audio. */
 char *tmp=(char*)b,*old=tmp+264;b+=528;cap-=528;unsigned pl=strlen(path);if(pl+5>264)return -2;
 memcpy(tmp,path,pl);memcpy(tmp+pl,".tmp",5);memcpy(old,path,pl);memcpy(old+pl,".old",5);
 if(f_open(t,tmp,FA_CREATE_ALWAYS|FA_WRITE)!=FR_OK)return -1;
 int e=0;
 uint32_t nsize=need-10+2048;uint8_t nh[10]={'I','D','3',(uint8_t)ver,0,(uint8_t)(flags&~0x10u),0,0,0,0};put_synchsafe(nh+6,nsize);
 UINT g;
 if(f_write(t,nh,10,&g)!=FR_OK||g!=10)e=-1;
 uint32_t at=nt?ts[0]:used;
 if(!e&&tag&&append(10,at-10,b,cap))e=-1;
 if(!e&&(f_write(t,nf,nl,&g)!=FR_OK||g!=nl))e=-1;
 for(unsigned i=0;!e&&i<nt;i++){uint32_t s=te[i],x=i+1<nt?ts[i+1]:used;if(x>s&&append(s,x-s,b,cap))e=-1;}
 if(!e){unsigned z=cap<512?cap:512;memset(b,0,z);for(uint32_t p=0;p<2048&&!e;p+=z)if(f_write(t,b,z,&g)!=FR_OK||g!=z)e=-1;}
 if(!e&&append(audio,fsize-audio,b,cap))e=-1;
 if(f_close(t)!=FR_OK)e=-1;
 f_close(f);
 if(e){f_unlink(tmp);return -1;}
 return replace_file(path,tmp,old);
}

int tag_set(const char *path,unsigned field,const char *text,uint8_t *buf,unsigned cap){
 if(field>TAG_GENRE||cap<8192)return -2;
 /* Work memory: the two files, then the data buffer (word aligned). */
 f=(FIL*)(void*)buf;t=f+1;FILINFO *st=(FILINFO*)(void*)(t+1);
 unsigned used=((unsigned)(sizeof(FIL)*2+sizeof(FILINFO))+3u)&~3u;buf+=used;cap-=used;
 if(f_stat(path,st)!=FR_OK)return -1;
 if(f_open(f,path,FA_READ|FA_WRITE)!=FR_OK)return -1;
 unsigned n=strlen(path);int gac=n>4&&path[n-4]=='.'&&(path[n-3]|32)=='g'&&(path[n-2]|32)=='a'&&(path[n-1]|32)=='c';
 int e=gac?gac_set(field,text,buf):mp3_set(path,field,text,buf,cap);
 f_close(f);   /* harmless when the rewrite already closed it */
 if(!e){st->ftime=(WORD)(st->ftime^1);f_utime(path,st);}   /* Update Library reads it again */
 return e;
}
