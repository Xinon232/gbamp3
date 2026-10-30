/* Halim Jarrar, 2026. GPL-3.0-or-later with the additional permission described in NOTICES.md (linking with the Helix MP3 decoder). SuperFW driver adapter; EZ-Flash Omega driver (ezfo_driver.c);
 * EverDrive GBA driver (edgba_driver.c). */
#include "ff.h"
#include "diskio.h"
#include "supercard_driver.h"
#include "ezfo_driver.h"
#include "edgba_driver.h"
#include "overlay.h"
static DSTATUS state=STA_NOINIT;
static unsigned index_writes,write_failed;
extern void storage_discard_window(void);
/* Closing a write window after any refused/failed write drops FatFS's cached
 * dirty sector, so later reads never try to flush it (e.g. read-only card). */
void disk_index_writes(unsigned enabled){
 index_writes=enabled!=0;
 if(!enabled&&write_failed){write_failed=0;storage_discard_window();}
}
static t_card_info card;
extern void player_poll_io(void);
#define WAITCNT (*(volatile uint16_t*)0x04000204)
DSTATUS disk_status(BYTE d){return d?STA_NOINIT:state;}
/* EZ-Flash Omega and EverDrive GBA support: the cart is checked once per
 * boot. The EverDrive first (its configuration register is where the Omega
 * check writes its lock value; its driver runs from ROM). The Omega driver's
 * RAM code is loaded before its check (its probe needs it); without an Omega
 * the cart overlay gets the SuperCard read code. L+R at boot: SuperCard. */
static unsigned cart_kind;   /* 0 not checked, 1 SuperCard, 2 Omega, 3 Omega without card, 4 EverDrive, 5 EverDrive without card */
static unsigned cart_detect(void){
 if(cart_kind)return cart_kind;
 if(((~*(volatile uint16_t*)0x04000130)&0x300u)==0x300u){cart_use(0);return cart_kind=1;}
 unsigned e=edgba_detect();
 if(e!=EDGBA_NONE)return cart_kind=e==EDGBA_READY?4:5;
 cart_use(1);
 ezfo_irq_live=1;   /* the sound interrupt keeps running: its code is in RAM */
 unsigned r=ezfo_detect();
 cart_kind=r==EZFO_READY?2:r==EZFO_NO_CARD?3:1;
 if(cart_kind==1)cart_use(0);
 return cart_kind;
}
DSTATUS disk_initialize(BYTE d){
 if(d)return STA_NOINIT;
 unsigned kind=cart_detect();
 if(kind!=1){
  /* The Omega and EverDrive drivers report no card size: FatFS takes it from the partition. */
  if(kind==2||kind==4){card.block_cnt=0xffffffffu;card.sdhc=true;state=0;}else state=STA_NOINIT;
  return state;
 }
 unsigned old=WAITCNT;WAITCNT=0x40c0;
 /* SDRAM stays mapped read-only while register interface is enabled.
  * ROM is <16MiB, below the SD register window. IRQ/FIFO runs in IWRAM.
  * No DMA/timer borrowing, no interrupt masking, no fast mirror. */
 set_supercard_mode(MAPPED_SDRAM,false,true);
 state=sdcard_init(&card)?STA_NOINIT:0;
 /* 11.0: no size cap (10.0 refused cards over 32 GiB: "No SD card" on 64 GB
  * SDXC). Any SDHC/SDXC size up to FatFs's 32-bit LBA limit (2 TiB); only a
  * standard-capacity card over 2 GiB (an impossible CSD) is refused. */
 if(!card.block_cnt||(!card.sdhc&&card.block_cnt>0x400000u))state=STA_NOINIT;
 WAITCNT=old;return state;
}
/* v1.2: FatFS reads and writes whole clusters directly (up to 64 sectors
 * with 32 KB clusters); requests are split into commands of at most 16
 * sectors (8 KiB) with the audio poll between them, instead of rejected. */
#define DISK_CHUNK 16u
DRESULT disk_read(BYTE d,BYTE*b,LBA_t s,UINT n){
 if(d||state||!b||!n||s>=card.block_cnt||n>card.block_cnt-s)return RES_PARERR;
 unsigned old=WAITCNT;WAITCNT=0x40c0;
 /* Keep IRQ live. CMD18/CMD12 once per chunk avoids per-sector command overhead. */
 bool ok=true;
 while(n&&ok){UINT k=n<DISK_CHUNK?n:DISK_CHUNK;player_poll_io();ok=(cart_kind==2?ezfo_read(b,s,k):cart_kind==4?edgba_read(b,s,k):sdcard_read_blocks(b,s,k))==0;player_poll_io();b+=512*k;s+=k;n-=k;}
 WAITCNT=old;if(!ok){state=STA_NOINIT;return RES_ERROR;}return RES_OK;
}
/* Only the library transaction holds this gate; songs are opened FA_READ. */
DRESULT disk_write(BYTE d,const BYTE*b,LBA_t s,UINT n){
 if(!index_writes){write_failed=1;return RES_WRPRT;}
 if(d||state||!b||!n||s>=card.block_cnt||n>card.block_cnt-s)return RES_PARERR;
 unsigned old=WAITCNT;WAITCNT=0x40c0;
 bool ok=true;
 while(n&&ok){UINT k=n<DISK_CHUNK?n:DISK_CHUNK;player_poll_io();ok=(cart_kind==2?ezfo_write(b,s,k):cart_kind==4?edgba_write(b,s,k):sdcard_write_blocks(b,s,k))==0;player_poll_io();b+=512*k;s+=k;n-=k;}
 WAITCNT=old;
 /* A rejected index write (including card write protection) does not prove
  * reads are unavailable. Abort the transaction; let a real read determine
  * read health. Never retry this write or invalidate every live read handle. */
 if(!ok)write_failed=1;
 return ok?RES_OK:RES_ERROR;
}
DRESULT disk_ioctl(BYTE d,BYTE cmd,void*b){
 if(d||state)return RES_NOTRDY;
 if(cmd==CTRL_SYNC)return RES_OK;
 if(!b)return RES_PARERR;
 if(cmd==GET_SECTOR_COUNT){*(LBA_t*)b=card.block_cnt;return RES_OK;}
 if(cmd==GET_SECTOR_SIZE){*(WORD*)b=512;return RES_OK;}
 return RES_PARERR;
}
