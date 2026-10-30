/* SuperFW font glyphs (GPL-3.0-or-later; UNSCII/Unifont sources), subset by
 * tools/font_subset.py: Latin-1 Supplement, Latin Extended-A/B, Greek, Cyrillic. */
    .section .rodata.font_pack_latin,"a"
    .align 2
    .global font_pack_latin
font_pack_latin:
    .incbin "fonts-latin-greek-cyrillic.pack"
