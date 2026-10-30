/* Halim Jarrar, 2026. Freestanding support. GPL-3.0-or-later with the additional permission described in NOTICES.md (linking with the Helix MP3 decoder). */
#include <stdint.h>
#include <stddef.h>
unsigned player_div64(uint64_t n,unsigned d){uint64_t rem=0;unsigned q=0;if(!d)return 0;for(unsigned i=0;i<64;i++){rem=(rem<<1)|(n>>63);n<<=1;q<<=1;if(rem>=d){rem-=d;q|=1;}}return q;}
char *strchr(const char*s,int c){do{if(*s==(char)c)return(char*)s;}while(*s++);return 0;}
void *memcpy(void *dst,const void *src,size_t n){unsigned char*d=dst;const unsigned char*s=src;while(n--)*d++=*s++;return dst;}
void *memmove(void *dst,const void *src,size_t n){unsigned char*d=dst;const unsigned char*s=src;if(d<s){while(n--)*d++=*s++;}else{d+=n;s+=n;while(n--)*--d=*--s;}return dst;}
void *memset(void *dst,int c,size_t n){unsigned char*d=dst;while(n--)*d++=(unsigned char)c;return dst;}
int memcmp(const void*a,const void*b,size_t n){
 const unsigned char*x=a,*y=b;
 /* Aligned inputs (sort keys, records) skip equal words four bytes at a time. */
 if(!(((uintptr_t)x|(uintptr_t)y)&3))while(n>=4&&*(const uint32_t*)x==*(const uint32_t*)y){x+=4;y+=4;n-=4;}
 while(n--){if(*x!=*y)return *x-*y;x++;y++;}return 0;}
size_t strlen(const char*s){size_t n=0;while(s[n])n++;return n;}
unsigned rt_udiv(unsigned n,unsigned d){if(!d)return 0;unsigned q=0,bit=1;while(d<=n/2 && !(d&0x80000000u)){d<<=1;bit<<=1;}while(bit){if(n>=d){n-=d;q|=bit;}d>>=1;bit>>=1;}return q;}
void __aeabi_memcpy(void*d,const void*s,size_t n){memcpy(d,s,n);}
void __aeabi_memcpy4(void*d,const void*s,size_t n){memcpy(d,s,n);}
void __aeabi_memclr(void*d,size_t n){memset(d,0,n);}
void __aeabi_memclr4(void*d,size_t n){memset(d,0,n);}
void __aeabi_memset(void*d,size_t n,int c){memset(d,c,n);}
void __aeabi_memset4(void*d,size_t n,int c){memset(d,c,n);}
int strcmp(const char*a,const char*b){while(*a&&*a==*b){a++;b++;}return (unsigned char)*a-(unsigned char)*b;}
char *strcpy(char*d,const char*s){char*r=d;while((*d++=*s++));return r;}
/* Built as ARM: the 64-bit product below compiles to umull/mla, not a call. */
uint64_t __aeabi_lmul(uint64_t a,uint64_t b){uint32_t al=(uint32_t)a,ah=(uint32_t)(a>>32),bl=(uint32_t)b,bh=(uint32_t)(b>>32);return (uint64_t)al*bl+((uint64_t)(al*bh+ah*bl)<<32);}
