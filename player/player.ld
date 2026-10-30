/* Halim Jarrar, 2026. GPL-3.0-or-later with the additional permission described in NOTICES.md (linking with the Helix MP3 decoder). Freestanding layout: no SDK startup or runtime libraries. */
OUTPUT_ARCH(arm)
ENTRY(_start)
MEMORY {
 ROM (rx) : ORIGIN = 0x08000000, LENGTH = 32M
 EWRAM (rw) : ORIGIN = 0x02000000, LENGTH = 256K
 IWRAM (rwx) : ORIGIN = 0x03000000, LENGTH = 32K
}
SECTIONS {
 .text : {
  KEEP(*(.text.start))
  *(EXCLUDE_FILE (*.iwram.o) .text)
  *(EXCLUDE_FILE (*.iwram.o) .text.*)
  *(.rodata .rodata.*)
  . = ALIGN(4);
 } > ROM
 /* Common IWRAM: always resident, copied by start.S. */
 .iwram : {
  __iwram_start = .;
  *(.iwram .iwram.*)
  *.iwram.o(.text .text.*)
  . = ALIGN(4); __iwram_common_end = .;
 } > IWRAM AT> ROM
 __iwram_load = LOADADDR(.iwram);
 .data : {
  __data_start = .; *(.data .data.* .ewram .ewram.*) . = ALIGN(4); __data_end = .;
 } > EWRAM AT> ROM
 __data_load = LOADADDR(.data);
 /* Cart overlay: the SD driver code that must run from RAM, for the cart
  * found at boot (diskio.c copies one in): the SuperCard read code or the
  * EZ-Flash Omega per-command code. ld provides __load_start_cart_sc etc. */
 OVERLAY __iwram_common_end : NOCROSSREFS AT(__data_load + SIZEOF(.data)) {
  .cart_sc { *(.cart_sc .cart_sc.*) . = ALIGN(4); }
  .cart_ezfo { *(.cart_ezfo .cart_ezfo.*) . = ALIGN(4); }
 }
 __cart_start = ADDR(.cart_sc);
 __cart_end = MAX(__cart_start + SIZEOF(.cart_sc), __cart_start + SIZEOF(.cart_ezfo));
 /* Decoder overlay region: MP3 (Helix) or GAC code and constant tables share
  * one IWRAM range. Only overlay.c copies them in; NOCROSSREFS fails the link
  * if either overlay references the other. ld provides __load_start_ov_mp3,
  * __load_stop_ov_mp3 and the GAC pair. */
 OVERLAY __cart_end : NOCROSSREFS AT(__load_stop_cart_ezfo) {
  .ov_mp3 { *(.ov_mp3 .ov_mp3.*) . = ALIGN(4); }
  .ov_gac { *(.ov_gac .ov_gac.*) . = ALIGN(4); }
  .ov_jpeg { *(.ov_jpeg .ov_jpeg.*) . = ALIGN(4); }
  .ov_conv { *(.ov_conv .ov_conv.*) . = ALIGN(4); }   /* conversion: resampler + GAC encoder */
  .ov_menc { *(.ov_menc .ov_menc.*) . = ALIGN(4); }   /* v2.0 GAC -> MP3: resampler copy + shine's hot code and tables */
 }
 __overlay_start = ADDR(.ov_mp3);
 /* GAC decoder state: zeroed by overlay.c when the GAC overlay is loaded;
  * shares the region with the MP3 overlay like the GAC code does. */
 .gac_scratch __overlay_start + SIZEOF(.ov_gac) (NOLOAD) : {
  __gac_scratch_start = .; *(.gac_scratch) . = ALIGN(4); __gac_scratch_end = .;
 }
 /* v0.9.8 cover decoder (Update Library only): Huffman tables after its code. */
 .jpeg_scratch __overlay_start + SIZEOF(.ov_jpeg) (NOLOAD) : {
  __jpeg_scratch_start = .; *(.jpeg_scratch) . = ALIGN(4); __jpeg_scratch_end = .;
 }
 /* v2.0 MP3 encoder scratch (the granule being quantized) after its code. */
 .menc_scratch __overlay_start + SIZEOF(.ov_menc) (NOLOAD) : {
  __menc_scratch_start = .; *(.menc_scratch) . = ALIGN(4); __menc_scratch_end = .;
 }
 /* Conversion scratch (encoder look-ahead, subblock search) after its code. */
 .conv_scratch __overlay_start + SIZEOF(.ov_conv) (NOLOAD) : {
  __conv_scratch_start = .; *(.conv_scratch) . = ALIGN(4); __conv_scratch_end = .;
 }
 __overlay_end = MAX(MAX(MAX(MAX(__overlay_start + SIZEOF(.ov_mp3), __gac_scratch_end), __jpeg_scratch_end), __conv_scratch_end), __menc_scratch_end);
 __iwram_end = __overlay_end;
 .bss (NOLOAD) : {
  __bss_start = .; *(.bss .bss.* .sbss .sbss.* COMMON) . = ALIGN(4); __bss_end = .;
 } > EWRAM
 /DISCARD/ : { *(.ARM.exidx* .ARM.extab* .eh_frame* .comment .note*) }
 ASSERT(__iwram_end <= 0x03006e00, "Insufficient conservative player main-stack reserve")
 ASSERT(SIZEOF(.ov_mp3) > 0 && SIZEOF(.ov_gac) > 0 && SIZEOF(.ov_jpeg) > 0, "Empty decoder overlay")
 ASSERT(SIZEOF(.cart_sc) > 0 && SIZEOF(.cart_ezfo) > 0, "Empty cart overlay")   /* .ov_conv: empty in builds without conversion */
 ASSERT(__conv_scratch_end <= __overlay_start + SIZEOF(.ov_mp3), "Conversion overlay larger than the MP3 one")
 ASSERT(__menc_scratch_end <= __gac_scratch_start, "MP3 encoder overlay reaches the GAC decoder state (GAC -> MP3 alternates them)")
 ASSERT(__bss_end <= 0x0203f000, "EWRAM overflow")
}
