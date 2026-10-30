/* Halim Jarrar, 2026. GPL-3.0-or-later with the additional permission described in NOTICES.md (linking with the Helix MP3 decoder). Song metadata for MP3 (ID3v2.2-2.4, ID3v1) and GAC.
 * Reads only the tag and one or two audio frame headers; embedded pictures and
 * other large frames are skipped by seeking. */
#include "meta.h"
unsigned player_div64(uint64_t n,unsigned d);
#include "mp3_header.h"
#include "gac4.h"
#include "text.h"
#include "jpeg_dc.h"
#include <string.h>

static const char *const genres[]={
 "Blues","Classic Rock","Country","Dance","Disco","Funk","Grunge","Hip-Hop","Jazz","Metal","New Age","Oldies",
 "Other","Pop","R&B","Rap","Reggae","Rock","Techno","Industrial","Alternative","Ska","Death Metal","Pranks",
 "Soundtrack","Euro-Techno","Ambient","Trip-Hop","Vocal","Jazz+Funk","Fusion","Trance","Classical",
 "Instrumental","Acid","House","Game","Sound Clip","Gospel","Noise","Alternative Rock","Bass","Soul","Punk",
 "Space","Meditative","Instrumental Pop","Instrumental Rock","Ethnic","Gothic","Darkwave","Techno-Industrial",
 "Electronic","Pop-Folk","Eurodance","Dream","Southern Rock","Comedy","Cult","Gangsta","Top 40",
 "Christian Rap","Pop/Funk","Jungle","Native American","Cabaret","New Wave","Psychedelic","Rave","Showtunes",
 "Trailer","Lo-Fi","Tribal","Acid Punk","Acid Jazz","Polka","Retro","Musical","Rock & Roll","Hard Rock",
 "Folk","Folk-Rock","National Folk","Swing","Fast Fusion","Bebop","Latin","Revival","Celtic","Bluegrass",
 "Avantgarde","Gothic Rock","Progressive Rock","Psychedelic Rock","Symphonic Rock","Slow Rock","Big Band",
 "Chorus","Easy Listening","Acoustic","Humour","Speech","Chanson","Opera","Chamber Music","Sonata","Symphony",
 "Booty Bass","Primus","Porn Groove","Satire","Slow Jam","Club","Tango","Samba","Folklore","Ballad",
 "Power Ballad","Rhythmic Soul","Freestyle","Duet","Punk Rock","Drum Solo","A Cappella","Euro-House",
 "Dance Hall","Goa","Drum & Bass","Club-House","Hardcore Techno","Terror","Indie","BritPop","Negerpunk",
 "Polsk Punk","Beat","Christian Gangsta Rap","Heavy Metal","Black Metal","Crossover","Contemporary Christian",
 "Christian Rock","Merengue","Salsa","Thrash Metal","Anime","Jpop","Synthpop","Abstract","Art Rock","Baroque",
 "Bhangra","Big Beat","Breakbeat","Chillout","Downtempo","Dub","EBM","Eclectic","Electro","Electroclash",
 "Emo","Experimental","Garage","Global","IDM","Illbient","Industro-Goth","Jam Band","Krautrock","Leftfield",
 "Lounge","Math Rock","New Romantic","Nu-Breakz","Post-Punk","Post-Rock","Psytrance","Shoegaze","Space Rock",
 "Trop Rock","World Music","Neoclassical","Audiobook","Audio Theatre","Neue Deutsche Welle","Podcast",
 "Indie Rock","G-Funk","Dubstep","Garage Rock","Psybient"
};
const char *meta_genre_name(unsigned g){return g<sizeof(genres)/sizeof(genres[0])?genres[g]:"";}

static int rd(FIL *f,unsigned at,void *p,unsigned n,unsigned *got){
 UINT g=0;if(f_lseek(f,at)!=FR_OK||f_tell(f)!=at)return -1;if(f_read(f,p,n,&g)!=FR_OK)return -1;*got=g;return 0;
}
/* Copy UTF-8 into a bounded field: whole scalars only, controls become spaces,
 * surrounding spaces trimmed. */
static void put_utf8(char *out,unsigned cap,const char *s,unsigned n){
 unsigned o=0;const char *e=s+n;
 while(s<e&&*s){const char *q=s;uint32_t c=text_next(&q);if(q>e)break;s=q;
  if(c<0x20||c==0x7f)c=' ';if(c==' '&&!o)continue;
  char tmp[4];unsigned k=(unsigned)utf8_put(tmp,4,c);if(o+k>=cap)break;memcpy(out+o,tmp,k);o+=k;}
 while(o&&out[o-1]==' ')o--;out[o]=0;
}
void meta_id3_text(const unsigned char *p,unsigned n,char *out,unsigned cap){
 out[0]=0;if(!n||cap<2)return;
 unsigned enc=p[0];p++;n--;
 static char tmp[384];unsigned t=0;
 if(enc==0){for(unsigned i=0;i<n&&p[i];i++){unsigned k=(unsigned)utf8_put(tmp+t,sizeof(tmp)-t,p[i]);if(!k)break;t+=k;}}
 else if(enc==3){while(t<n&&t<sizeof(tmp)&&p[t])t++;memcpy(tmp,p,t);}
 else if(enc==1||enc==2){
  int be=enc==2;unsigned i=0;
  if(enc==1&&n>=2){if(p[0]==0xff&&p[1]==0xfe){be=0;i=2;}else if(p[0]==0xfe&&p[1]==0xff){be=1;i=2;}}
  for(;i+1<n;i+=2){uint32_t c=be?(unsigned)p[i]<<8|p[i+1]:(unsigned)p[i+1]<<8|p[i];if(!c)break;
   if(c>=0xd800&&c<=0xdbff&&i+3<n){uint32_t d=be?(unsigned)p[i+2]<<8|p[i+3]:(unsigned)p[i+3]<<8|p[i+2];
    if(d>=0xdc00&&d<=0xdfff){c=0x10000+((c-0xd800)<<10)+(d-0xdc00);i+=2;}else c=0xfffd;}
   else if(c>=0xd800&&c<=0xdfff)c=0xfffd;
   unsigned k=(unsigned)utf8_put(tmp+t,sizeof(tmp)-t,c);if(!k)break;t+=k;}
 }else return;
 put_utf8(out,cap,tmp,t);
}
static unsigned number(const char *s){unsigned v=0,any=0;while(*s==' ')s++;while(*s>='0'&&*s<='9'&&v<100000){v=v*10+(unsigned)(*s++-'0');any=1;}return any?v:0;}
/* TCON: "(17)", "17", "(17)Rock", "Rock". */
static void genre_text(char *g,unsigned cap){
 if(g[0]=='('&&g[1]>='0'&&g[1]<='9'){const char *p=g+1;unsigned v=number(p);while(*p>='0'&&*p<='9')p++;
  if(*p==')'){p++;if(*p){memmove(g,p,strlen(p)+1);return;}put_utf8(g,cap,meta_genre_name(v),strlen(meta_genre_name(v)));return;}}
 if(g[0]>='0'&&g[0]<='9'){const char *p=g;while(*p>='0'&&*p<='9')p++;if(!*p){unsigned v=number(g);put_utf8(g,cap,meta_genre_name(v),strlen(meta_genre_name(v)));}}
}
static int ieq(const unsigned char *a,const char *b,unsigned n){for(unsigned i=0;i<n;i++)if((a[i]|32)!=(b[i]|32))return 0;return 1;}
int meta_id3_picture(const unsigned char *p,unsigned n,unsigned size,unsigned v22,uint32_t at,uint32_t *off,uint32_t *len){
 if(n<4||n>size)return -1;
 unsigned enc=p[0],i=1,jpeg;
 if(v22){jpeg=ieq(p+1,"JPG",3);i=4;}
 else{unsigned k=1;while(k<n&&p[k])k++;if(k>=n)return -1;
  unsigned m=k-1;const unsigned char *t=p+1;
  jpeg=(m==10&&ieq(t,"image/jpeg",10))||(m==9&&ieq(t,"image/jpg",9))||(m==4&&ieq(t,"jpeg",4))||(m==3&&ieq(t,"jpg",3));
  i=k+1;}
 if(!jpeg||i>=n||enc>3)return -1;
 unsigned type=p[i++];
 if(enc==1||enc==2){while(i+1<n&&(p[i]|p[i+1]))i+=2;if(i+1>=n)return -1;i+=2;}   /* UTF-16 description */
 else{while(i<n&&p[i])i++;if(i>=n)return -1;i++;}
 if(i>=size||size-i>JPEG_MAX_BYTES)return -1;
 *off=at+i;*len=size-i;return (int)type;
}
static unsigned synchsafe(const unsigned char *p){return (unsigned)(p[0]&127)<<21|(unsigned)(p[1]&127)<<14|(unsigned)(p[2]&127)<<7|(p[3]&127);}
static __attribute__((noinline)) int id3v2(FIL *f,struct SongMeta *m,unsigned *audio_start){
 unsigned char h[10];unsigned got,front=0;*audio_start=0;
 if(rd(f,0,h,10,&got)||got<10)return got<10?0:-1;
 if(memcmp(h,"ID3",3)||h[3]<2||h[3]>4||h[4]==0xff||(h[6]|h[7]|h[8]|h[9])&0x80)return 0;
 unsigned ver=h[3],size=synchsafe(h+6),end=10+size+((ver==4&&(h[5]&0x10))?10:0);
 *audio_start=end;
 unsigned pos=10;
 if(ver>=3&&(h[5]&0x40)){unsigned char e[4];if(rd(f,10,e,4,&got)||got<4)return -1;
  unsigned ext=ver==4?synchsafe(e):((unsigned)e[0]<<24|e[1]<<16|e[2]<<8|e[3])+4;pos+=ext;}
 unsigned hs=ver==2?6:10;
 while(pos+hs<=10+size){
  unsigned char fh[10];if(rd(f,pos,fh,hs,&got)||got<hs)return -1;
  if(!fh[0])break;
  unsigned n,flags=0;char id[5]={0};
  if(ver==2){memcpy(id,fh,3);n=(unsigned)fh[3]<<16|fh[4]<<8|fh[5];}
  else{memcpy(id,fh,4);n=ver==4?synchsafe(fh+4):(unsigned)fh[4]<<24|fh[5]<<16|fh[6]<<8|fh[7];flags=(unsigned)fh[8]<<8|fh[9];}
  if(n>size)break;
  char *dst=0;unsigned cap=0,kind=0;
  if(!strcmp(id,"TIT2")||!strcmp(id,"TT2")){dst=m->title;cap=META_TITLE;}
  else if(!strcmp(id,"TPE1")||!strcmp(id,"TP1")){dst=m->artist;cap=META_NAME;}
  else if(!strcmp(id,"TALB")||!strcmp(id,"TAL")){dst=m->album;cap=META_NAME;}
  else if(!strcmp(id,"TPE2")||!strcmp(id,"TP2")){dst=m->album_artist;cap=META_NAME;}
  else if(!strcmp(id,"TCON")||!strcmp(id,"TCO")){dst=m->genre;cap=META_GENRE;kind=1;}
  else if(!strcmp(id,"TRCK")||!strcmp(id,"TRK"))kind=2;
  else if(!strcmp(id,"TPOS")||!strcmp(id,"TPA"))kind=3;
  else if(!strcmp(id,"APIC")||!strcmp(id,"PIC"))kind=4;
  /* Compressed/encrypted (2.3: 0x0080/0x0040; 2.4: 0x0008/0x0004) or unsynchronised 2.4 frames are skipped. */
  unsigned skip=ver==3?(flags&0xc0):ver==4?(flags&0x0e):0;
  /* Pictures: unsynchronised tags (header flag) would corrupt the JPEG. */
  if(kind==4){
   if(!skip&&!(h[5]&0x80)&&n>4){
    static unsigned char pb[256];unsigned off=ver==4&&(flags&1)?4:0,k=n-off<sizeof(pb)?n-off:sizeof(pb);
    if(off<n&&!rd(f,pos+hs+off,pb,k,&got)&&got==k){
     uint32_t po,pl;int type=meta_id3_picture(pb,k,n-off,ver==2,pos+hs+off,&po,&pl);
     /* The front cover (type 3) wins; otherwise the first JPEG. */
     if(type>=0&&(!m->pic_len||(type==3&&!front))){m->pic_off=po;m->pic_len=pl;front=type==3;}
    }
   }
  }
  else if((dst||kind)&&!skip&&n){
   static unsigned char buf[320];unsigned k=n<sizeof(buf)?n:sizeof(buf);unsigned off=0;
   if(ver==4&&(flags&1)){off=4;}
   if(rd(f,pos+hs,buf,k,&got)||got<k)return -1;
   if(off<k){
    char t[96];
    if(kind==2||kind==3){meta_id3_text(buf+off,k-off,t,sizeof(t));unsigned v=number(t);if(kind==2)m->track=(uint16_t)(v>65535?0:v);else m->disc=(uint16_t)(v>65535?0:v);}
    else{meta_id3_text(buf+off,k-off,dst,cap);if(kind==1)genre_text(dst,cap);}
   }
  }
  pos+=hs+n;
 }
 return 0;
}
static __attribute__((noinline)) int id3v1(FIL *f,struct SongMeta *m){
 FSIZE_t size=f_size(f);if(size<128)return 0;
 unsigned char t[128];unsigned got;if(rd(f,(unsigned)size-128,t,128,&got)||got<128)return -1;
 if(memcmp(t,"TAG",3))return 0;
 char s[32];
 unsigned char *fields[3]={t+3,t+33,t+63};char *dst[3]={m->title,m->artist,m->album};unsigned cap[3]={META_TITLE,META_NAME,META_NAME};
 for(int i=0;i<3;i++)if(!dst[i][0]){unsigned char e[31];e[0]=0;memcpy(e+1,fields[i],30);meta_id3_text(e,31,dst[i],cap[i]);}
 if(!m->track&&t[125]==0&&t[126])m->track=t[126];
 if(!m->genre[0]&&t[127]!=255){const char *g=meta_genre_name(t[127]);put_utf8(m->genre,META_GENRE,g,strlen(g));}
 (void)s;return 1;
}
/* First frame header at or after `at`, within 4 KiB. */
static int find_frame(FIL *f,unsigned at,unsigned end,struct MP3Header *h,unsigned *where,unsigned char frame[64]){
 static unsigned char buf[512+64];unsigned got;
 for(unsigned base=at;base<at+4096&&base+4<end;base+=512){
  if(rd(f,base,buf,sizeof(buf),&got))return -1;
  for(unsigned i=0;i+4<=got&&i<512;i++)if(buf[i]==0xff&&(buf[i+1]&0xe0)==0xe0&&mp3_header(buf+i,h)){
   *where=base+i;unsigned k=got-i<64?got-i:64;memset(frame,0,64);memcpy(frame,buf+i,k);return 1;}
 }
 return 0;
}
static uint32_t ms(uint64_t samples,unsigned rate){return rate?player_div64(samples*1000,rate):0;}
static __attribute__((noinline)) void mp3_duration(FIL *f,struct SongMeta *m,unsigned audio_start,unsigned has_v1){
 struct MP3Header h;unsigned where;unsigned char fr[64];
 unsigned end=(unsigned)f_size(f)-(has_v1?128:0);
 if(find_frame(f,audio_start,end,&h,&where,fr)!=1)return;
 m->rate=h.rate;m->channels=(unsigned char)h.channels;
 unsigned mpeg1=((fr[1]>>3)&3)==3,mono=(fr[3]>>6)==3;
 unsigned side=mpeg1?(mono?17:32):(mono?9:17);
 const unsigned char *x=fr+4+side;
 if(!memcmp(x,"Xing",4)||!memcmp(x,"Info",4)){
  unsigned flags=(unsigned)x[4]<<24|x[5]<<16|x[6]<<8|x[7];
  if(flags&1){unsigned frames=(unsigned)x[8]<<24|x[9]<<16|x[10]<<8|x[11];m->duration_ms=ms((uint64_t)frames*h.samples,h.rate);}
  /* LAME writes "Info" for constant bitrate files, "Xing" for VBR. */
  if(x[0]=='I')m->bitrate=(uint16_t)(h.bitrate/1000);else m->vbr=1;
  return;
 }
 if(!memcmp(fr+36,"VBRI",4)){unsigned frames=(unsigned)fr[50]<<24|fr[51]<<16|fr[52]<<8|fr[53];m->duration_ms=ms((uint64_t)frames*h.samples,h.rate);m->vbr=1;return;}
 /* No length header: trust CBR only if a frame half-way through agrees. */
 struct MP3Header h2;unsigned w2;unsigned char f2[64];
 unsigned mid=where+(end-where)/2;
 /* The next frame and one half-way through must agree for CBR. */
 unsigned char nx[4];unsigned got;struct MP3Header hn;
 if(!rd(f,where+h.bytes,nx,4,&got)&&got==4&&nx[0]==0xff&&(nx[1]&0xe0)==0xe0&&mp3_header(nx,&hn)&&hn.bitrate!=h.bitrate){m->vbr=1;return;}
 if(end>where+8192&&find_frame(f,mid,end,&h2,&w2,f2)==1&&h2.rate==h.rate&&h.bitrate){
  if(h2.bitrate==h.bitrate){m->duration_ms=player_div64((uint64_t)(end-where)*8000,h.bitrate);m->bitrate=(uint16_t)(h.bitrate/1000);}
  else m->vbr=1;   /* VBR without a length header: length unknown */
 }
}
int meta_gac_block(const unsigned char *b,struct SongMeta *m){
 if(!memcmp(b,"GACMETA3",8)){
  m->track=(uint16_t)(b[8]|b[9]<<8);m->disc=(uint16_t)(b[12]|b[13]<<8);
  put_utf8(m->title,META_TITLE,(const char*)b+16,128);put_utf8(m->artist,META_NAME,(const char*)b+144,96);
  put_utf8(m->album,META_NAME,(const char*)b+240,96);put_utf8(m->album_artist,META_NAME,(const char*)b+336,96);
  put_utf8(m->genre,META_GENRE,(const char*)b+432,64);return 1;
 }
 return 0;
}
static __attribute__((noinline)) int gac(FIL *f,struct SongMeta *m){
 static unsigned char h[64],b[512];unsigned got;struct GacInfo info;
 if(rd(f,0,h,64,&got))return -1;
 /* Not v4 (an old GAC too): listed by file name. */
 if(got<64||gac_header(h,64,(unsigned)f_size(f),&info)!=1)return 0;
 m->rate=info.rate;m->channels=(unsigned char)info.channels;m->duration_ms=ms(info.total,info.rate);
 m->has_art=info.art!=0;
 if(rd(f,64,b,512,&got))return -1;
 if(got==512&&gac_metadata_valid(h,b))meta_gac_block(b,m);
 return 0;
}
static int ends_with(const char *s,const char *ext){unsigned n=strlen(s);return n>=4&&s[n-4]=='.'&&(s[n-3]|32)==ext[0]&&(s[n-2]|32)==ext[1]&&(s[n-1]|32)==ext[2];}
int meta_read(FIL *f,const char *name,struct SongMeta *m){
 memset(m,0,sizeof(*m));
 int e=0;
 if(ends_with(name,"gac")){m->format=FORMAT_GAC;e=gac(f,m);}
 else{
  m->format=FORMAT_MP3;unsigned start=0;
  e=id3v2(f,m,&start);
  int v1=0;
  if(!e&&(!m->title[0]||!m->artist[0]||!m->album[0]||!m->genre[0])){v1=id3v1(f,m);if(v1<0)e=-1;}
  else if(!e){unsigned char t[3];unsigned got;FSIZE_t s=f_size(f);if(s>=128&&!rd(f,(unsigned)s-128,t,3,&got)&&got==3&&!memcmp(t,"TAG",3))v1=1;}
  if(!e)mp3_duration(f,m,start,v1>0);
 }
 if(!m->title[0]){unsigned n=strlen(name);if(n>4&&name[n-4]=='.')n-=4;put_utf8(m->title,META_TITLE,name,n);m->title_from_name=1;}
 return e<0?-1:0;
}
