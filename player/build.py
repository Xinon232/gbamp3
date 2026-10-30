# Halim Jarrar, 2026. GPL-3.0-or-later with the additional permission described in NOTICES.md (linking with the Helix MP3 decoder).
from pathlib import Path
import subprocess,json,os,sys
D=Path(__file__).resolve().parent;R=D.parent
(R/'tmp').mkdir(exist_ok=True);(R/'cache').mkdir(exist_ok=True);(D/'evidence').mkdir(exist_ok=True)
os.environ.update(TMPDIR=str(R/'tmp'),XDG_CACHE_HOME=str(R/'cache'),PYTHONDONTWRITEBYTECODE='1')
P=str(Path(os.environ.get('DEVKITARM','/opt/devkitpro/devkitARM'))/'bin/arm-none-eabi-');B=D/'build';B.mkdir(exist_ok=True)
# Shared telemetry header and source are maintained explicitly, not generated.
RATE=int(next(x.split()[-1] for x in (D/"src/player.h").read_text().splitlines() if x.startswith("#define PLAYER_RATE")))
# FatFs from gbaSD (gbasd/fatfs, docs/fatfs.md there): ff.c, ffunicode.c and
# exfat_repair.c by name (gbamp3 keeps its own src/diskio.c, which polls the
# audio, and its ffconf.h in vendor/fatfs, found first on the include path).
# exfat_repair.c keeps its place among src/*.c in the link order.
# 11.0: alloc_hint.c (free-space hint after the mount, Thumb in ROM).
FATFS=R/'gbasd/fatfs';FATFS_SRC=[FATFS/'ff.c',FATFS/'ffunicode.c',FATFS/'alloc_hint.c']
v=D/'vendor/arduino-libhelix/src/libhelix-mp3';flags=['-O3','-g','-std=c11','-mcpu=arm7tdmi','-marm','-mthumb-interwork','-mlong-calls','-ffreestanding','-fno-builtin','-fno-unwind-tables','-fno-asynchronous-unwind-tables','-fwrapv','-fno-strict-aliasing','-ffunction-sections','-fdata-sections','-fstack-usage','-DGBA_FIXED_C','-DUSE_MONO_ARM','-DUSE_POLYPHASE_ARM','-DUSE_DCT32_ARM','-I'+str(v),'-I'+str(v.parent),'-I'+str(B),'-I'+str(D/'src'),'-I'+str(D/'vendor/fatfs'),'-I'+str(FATFS),'-I'+str(R/'gbasd/include')]
def cmd(xs):print(' '.join(map(str,xs)),flush=True);subprocess.run(list(map(str,xs)),check=True,timeout=120)
objs=[]
sys.path.insert(0,str(D/'tools'));from placement import apply,overlay
flags+=['-I'+str(D/'vendor/superfw_font'),'-I'+str(D/'vendor/gbawriter'),'-Wa,-I'+str(D/'vendor/superfw_font')]
# v0.8.0: user-interface/library modules run from ROM as Thumb (16-bit bus, prefetch).
THUMB={'text','exfat_repair','alloc_hint','gfx','input','meta','library','queue','state','playlist','ui','screens','writer_glue','writer_core','ffunicode','folder','jpeg_dc',
       'scalfact',   # v0.9.8 B1-pre: Helix scale factors (~2% of MP3 decoding) leave IWRAM for the ROM
       'bitstream','stproc','mp3dec','dequant','mono_hybrid'}   # v0.9.8 B1c: Helix code already in ROM runs faster as Thumb (16-bit bus)
# v0.9.0 decoder overlays: all Helix IWRAM code and tables go to .ov_mp3.*
# (whole .text of the FAST objects, plus their .iwram* sections); the GAC
# decoder's .iwram* sections go to .ov_gac.*. Only blit stays common.
FAST={'huffman','imdct','polyphase','polyphase_arm','polyphase_mono_arm','alias_arm','resample','dct32_core_arm',
      'resample_m','shine_l3subband','shine_l3mdct','shine_l3loop'}   # v2.0 GAC -> MP3: their whole code in the MP3 encoder overlay
OVERLAY={**{q.stem:'ov_mp3' for q in list(v.glob('*.c'))+list(v.glob('*.S'))},'gac4':'ov_gac','gac4_arm':'ov_gac','jpeg_dc':'ov_jpeg','mp3_copy':'ov_mp3','gac_enc':'ov_conv','gac_enc_arm':'ov_conv','resample':'ov_conv','rs_dot2':'ov_conv',
         'crc':'cart_sc','supercard_driver':'cart_sc','supercard_io':'cart_sc',   # SuperCard read code: cart overlay (see player.ld)
         'resample_m':'ov_menc','rs_dot2_m':'ov_menc','shine_l3subband':'ov_menc','shine_l3mdct':'ov_menc','shine_l3loop':'ov_menc','shine_tables':'ov_menc','shine_huffman':'ov_menc','mp3enc_arm':'ov_menc'}
# gbaSD sources in the link order of the old vendor copies (SuperCard, then
# Omega, then EverDrive), which keeps the EWRAM alignment padding the same.
GBASD_SRC=[R/'gbasd/src'/n for n in ('crc.c','supercard_driver.c','supercard_env.c','supercard_io.s','supercard_write.s','ezfo_driver.c','edgba_driver.c')]
GBASD=['-DGBASD_SC_IDLE_TIMEOUT=0x8000','-DGBASD_SC_DATA_TIMEOUT=0x4000','-DGBASD_SC_WRITE_RETRIES=0','-DGBASD_SC_MAX_WRITE_BLOCKS=16',
       '-DGBASD_SC_FAST_FN=__attribute__((section(".iwram"),noinline,noclone))','-DGBASD_SC_CRC7_FN=__attribute__((section(".iwram"),noinline,noclone))',
       '-DGBASD_SC_CRC16_FN=__attribute__((section(".ewram"),target("arm"),noinline))','-DGBASD_SC_CMD_IN_IWRAM','-DGBASD_SC_WRITE_IN_EWRAM']
cxx=['-std=gnu++17','-fno-exceptions','-fno-rtti','-fno-threadsafe-statics','-fno-use-cxa-atexit']
for p in sorted(v.glob('*.c'))+sorted(v.glob('*.S'))+sorted(list((D/'src').glob('*.c'))+[FATFS/'exfat_repair.c'],key=lambda q:q.name)+sorted((D/'src').glob('*.cpp'))+sorted((D/'src').glob('*.S'))+FATFS_SRC+GBASD_SRC+sorted((D/'vendor/gbawriter').glob('*.cpp'))+sorted((D/'vendor/shine').glob('*.c')):
 n=('shine_' if p.parent.name=='shine' else '')+p.stem   # v2.0: shine (MP3 encoder, LGPL v2) has a huffman.c too
 o=B/(n+('.iwram.o' if p.stem=='blit' else '.o'))
 f=[x for x in flags if not(p.suffix=='.cpp' and x=='-std=c11')]+(cxx if p.suffix=='.cpp' else [])
 if p.stem in THUMB:f=[x for x in f if x!='-marm']+['-mthumb','-Os','-fno-jump-tables']
 if p.stem=='blit':f=[x for x in f if x!='-O3']+['-Os']   # IWRAM is nearly full: keep it tiny
 if p.stem=='gac_enc':f=[x for x in f if x!='-O3']+['-O2']+(['-DGE_NO_ASM'] if os.environ.get('GE_NO_ASM') else [])   # conversion overlay: fits beside its scratch
 if p.stem=='mp3dec':f=f+['-Dmemcpy=mp3_memcpy','-Dmemmove=mp3_memmove']   # v0.9.8 B1a: word copies (mp3_copy.c), build level
 if p.stem=='mp3dec' and os.environ.get('PROFILE_MP3'):f=f+['-DPROFILE']   # test builds only (Part B0)
 if p.stem=='main' and os.environ.get('CONVERT_BENCH'):f=f+['-DCONVERT_BENCH='+os.environ['CONVERT_BENCH']]   # test builds only: time one conversion
 if n.startswith('shine_'):f=[x for x in f if x!='-O3']+['-O2','-I'+str(D/'vendor/shine')]   # GAC -> MP3 (ROM)
 if p.stem=='convert_gac':f=f+['-I'+str(D/'vendor/shine')]
 # EZ-Flash Omega driver: per-command code in the cart overlay (IWRAM, shared
 # with the SuperCard read code: only one cart is used per boot), the boot
 # probe in EWRAM (Thumb), the outer loops and detection in ROM (Thumb).
 if p.stem=='ezfo_driver':f=[x for x in f if x not in('-marm','-O3')]+['-mthumb','-Os','-fno-jump-tables','-DEZFO_SECTION=".cart_ezfo"','-DEZFO_ISA="arm"','-DEZFO_OUTER_SECTION=".text.ezfo"','-DEZFO_PROBE_SECTION=".ewram.ezfo_probe"','-DEZFO_PROBE_ISA="thumb"']
 # gbaSD (SD card drivers, git submodule): gbamp3's settings. Short SuperCard
 # waits (playback must not stall), no write retry (the library transaction
 # decides), at most 16 sectors per write; command/read code and crc7 in IWRAM
 # (cart overlay, see OVERLAY), the sector writer and its CRC in EWRAM.
 # gbaSD's own detection (cart_io.c) is not used: diskio.c has its own.
 if p.parent==R/'gbasd/src':f=f+GBASD
 if p.suffix=='.s':f=f+['-x','assembler-with-cpp']   # gbaSD assembly uses the C preprocessor
 # EverDrive GBA driver: all in ROM (Thumb); the ROM stays mapped while it runs.
 if p.stem=='edgba_driver':f=[x for x in f if x not in('-marm','-O3')]+['-mthumb','-Os','-fno-jump-tables']
 cmd([P+('g++' if p.suffix=='.cpp' else 'gcc'),*f,'-c',p,'-o',o]);objs.append(o)
 if n in OVERLAY:overlay(o,OVERLAY[n],n in FAST,P,cmd)
# v2.0: the resampler a second time for the MP3 encoder overlay (one object
# lives in one overlay): resample_m.o, rs_dot2_m.o with renamed symbols.
for p,n,extra in [(D/'src/resample.c','resample_m',['-Dresample_run=resample_run_m','-Dresample_begin=resample_begin_m','-Dresample_begin_down=resample_begin_down_m','-Drs_dot2=rs_dot2_m']),
                  (D/'src/rs_dot2.S','rs_dot2_m',['-Drs_dot2=rs_dot2_m'])]:
 o=B/(n+'.o');cmd([P+'gcc',*flags,*extra,'-c',p,'-o',o]);objs.append(o);overlay(o,OVERLAY[n],n in FAST,P,cmd)
layout=json.loads((D/'LAYOUT.json').read_text());layout['handles']=False;layout['allocator']=False
apply(B,objs,P,layout,cmd)
cmd([P+'gcc','-nostdlib','-mcpu=arm7tdmi','-marm','-Wl,--gc-sections','-Wl,-Map,'+str(B/'player.map'),'-T',D/'player.ld',*objs,'-lgcc','-o',B/'player.elf'])   # libgcc: soft-float and 64-bit division (conversion's LPC analysis)
cmd([P+'objcopy','-O','binary',B/'player.elf',B/'player.gba']);cmd(['/opt/devkitpro/tools/bin/gbafix',B/'player.gba','-tRTSTEREOMP3','-cRMP3','-m00'])
for n,args in [('symbols',['nm','-S','-n']),('sections',['objdump','-h']),('disassembly',['objdump','-d']),('attributes',['readelf','-A'])]:(D/'evidence'/(n+'.txt')).write_text(subprocess.check_output([P+args[0],*args[1:],str(B/'player.elf')],text=True))
print(subprocess.check_output([P+'size',str(B/'player.elf')],text=True))
