/* Halim Jarrar, 2026. GPL-3.0-or-later with the additional permission described in NOTICES.md (linking with the Helix MP3 decoder). IWRAM decoder overlay swap (v0.9.0).
 * MP3 (Helix) and GAC code share one IWRAM region (player.ld). Callers switch
 * only while audio is stopped (begin_track); the copy is a plain CPU loop, so
 * the sound IRQ is never blocked even if it were running. */
#include <stdint.h>
#include "overlay.h"
#include "player.h"
extern const uint32_t __load_start_ov_mp3[],__load_stop_ov_mp3[],__load_start_ov_gac[],__load_stop_ov_gac[],__load_start_ov_jpeg[],__load_stop_ov_jpeg[],__load_start_ov_conv[],__load_stop_ov_conv[],__load_start_ov_menc[],__load_stop_ov_menc[];
extern uint32_t __overlay_start[],__gac_scratch_start[],__gac_scratch_end[];
extern const uint32_t __load_start_cart_sc[],__load_stop_cart_sc[],__load_start_cart_ezfo[],__load_stop_cart_ezfo[];
extern uint32_t __cart_start[];
static __attribute__((section(".iwram"),target("arm"),noinline,optimize("Os"))) void copy_words(uint32_t *d,const uint32_t *s,const uint32_t *e){
 /* 32 bytes per ldmia/stmia pair; IRQs stay enabled. Sizes are multiples of 4. */
 while(e-s>=8){
  __asm__ volatile("ldmia %1!,{r2-r9}\n\tstmia %0!,{r2-r9}":"+r"(d),"+r"(s)::"r2","r3","r4","r5","r6","r7","r8","r9","memory");
 }
 while(s<e)*d++=*s++;
}
static void use(unsigned which,unsigned keep){
 if(stats.decoder==which)return;
 stats.decoder=DECODER_NONE;__asm__ volatile("":::"memory");
 if(which==DECODER_MP3)copy_words(__overlay_start,__load_start_ov_mp3,__load_stop_ov_mp3);
 else if(which==DECODER_GAC){
  copy_words(__overlay_start,__load_start_ov_gac,__load_stop_ov_gac);
  if(!keep)for(uint32_t*p=__gac_scratch_start;p<__gac_scratch_end;p++)*p=0;
 }
 else if(which==DECODER_JPEG)copy_words(__overlay_start,__load_start_ov_jpeg,__load_stop_ov_jpeg);
 else if(which==DECODER_CONV)copy_words(__overlay_start,__load_start_ov_conv,__load_stop_ov_conv);
 else if(which==DECODER_MENC)copy_words(__overlay_start,__load_start_ov_menc,__load_stop_ov_menc);
 else return;
 __asm__ volatile("":::"memory");stats.decoder=which;stats.decoder_swaps++;
}
void decoder_use(unsigned which){use(which,0);}
/* v2.0 GAC -> MP3: the GAC decoder back with its state (the conversion
 * overlay in between leaves the GAC scratch alone: player.ld). */
void decoder_use_keep(unsigned which){use(which,1);}
/* EZ-Flash Omega support: the SD driver's RAM code for the cart found at boot
 * (player.ld cart overlay). Called before the card is used. */
void cart_use(unsigned ezfo){
 if(ezfo)copy_words(__cart_start,__load_start_cart_ezfo,__load_stop_cart_ezfo);
 else copy_words(__cart_start,__load_start_cart_sc,__load_stop_cart_sc);
}
