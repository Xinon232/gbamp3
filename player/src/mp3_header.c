/* Halim Jarrar, 2026. GPL-3.0-or-later with the additional permission described in NOTICES.md (linking with the Helix MP3 decoder). MPEG Layer III header only, not payload validation. */
#include "mp3_header.h"
int mp3_header(const unsigned char*p,struct MP3Header*h){
 static const unsigned br1[16]={0,32,40,48,56,64,80,96,112,128,160,192,224,256,320,0};
 static const unsigned br2[16]={0,8,16,24,32,40,48,56,64,80,96,112,128,144,160,0};
 static const unsigned sr[3]={44100,48000,32000};
 unsigned v=(p[1]>>3)&3,b=p[2]>>4,s=(p[2]>>2)&3;
 if(p[0]!=255||(p[1]&224)!=224||v==1||((p[1]>>1)&3)!=1||!b||b==15||s==3||(p[3]&3)==2)return 0;
 h->rate=sr[s]>>(v==3?0:v==2?1:2);h->bitrate=(v==3?br1[b]:br2[b])*1000;
 h->samples=v==3?1152:576;h->channels=(p[3]>>6)==3?1:2;
 h->bytes=(v==3?144:72)*h->bitrate/h->rate+((p[2]>>1)&1);
 return h->bytes>=24&&h->bytes<=2048;
}
