/* Halim Jarrar, 2026. GPL-3.0-or-later with the additional permission described in NOTICES.md (linking with the Helix MP3 decoder). GAC v4 order-8 LPC synthesis for ARM7TDMI.
 * void gac4_synth8(int32_t *x, const int32_t *c, const int32_t *r)
 * For i = 0..31: x[i] = clamp16(((2048 + sum_k c[k] * x[i-1-k]) mod 2^32 >> 12) + r[i]),
 * with x[-8..-1] the history and r[i] = q[i] * step. Same result as synth()
 * in gac4.c. Coefficients stay in r4-r11; the 32 samples are unrolled. */
.syntax unified
.arm
.section .iwram.gac4_synth8,"ax",%progbits
.global gac4_synth8
.type gac4_synth8,%function
gac4_synth8:
 push {r4-r11,lr}
 ldmia r1,{r4-r11}
 .rept 32
 ldmdb r0,{r12,lr}           @ x[i-2], x[i-1]
 mul r3,lr,r4
 mla r3,r12,r5,r3
 ldr r12,[r0,#-12]
 ldr lr,[r0,#-16]
 mla r3,r12,r6,r3
 mla r3,lr,r7,r3
 ldr r12,[r0,#-20]
 ldr lr,[r0,#-24]
 mla r3,r12,r8,r3
 mla r3,lr,r9,r3
 ldr r12,[r0,#-28]
 ldr lr,[r0,#-32]
 mla r3,r12,r10,r3
 mla r3,lr,r11,r3
 ldr r12,[r2],#4
 add r3,r3,#2048
 add r3,r12,r3,asr #12
 mov r12,r3,asr #15          @ saturate to signed 16 bits
 teq r12,r3,asr #31
 movne r3,r3,asr #31
 eorne r3,r3,#0x7f00
 eorne r3,r3,#0xff
 str r3,[r0],#4
 .endr
 pop {r4-r11,lr}
 bx lr
.size gac4_synth8,.-gac4_synth8

/* v0.9.5 Volume Boost: one full stereo unit (64 frames) with gain + soft
 * limiter + noise-shaped 8-bit output; same result as emit_boost() in gac4.c.
 * void gac4_boost64(const int32_t *m, const int32_t *sd, int8_t *l, struct Gac4Boost *b)
 * b = {el, er, prev, gain, r - l}; the 64 ring slots from l do not wrap (the
 * caller checks) and the caller writes the tags. Registers: r0 m, r1 sd,
 * r2 l, r3 r - l, r4 el, r5 er, r6 gain, r7 BOOST_KNEE, r8 boost_curve,
 * r9 prev (side sample of the previous odd frame). Samples above the knee
 * take the out-of-line limiter path (subsection 1). */
#include "boost_table.h"
.section .iwram.gac4_boost64,"ax",%progbits
.global gac4_boost64
.type gac4_boost64,%function
@ One output sample: V = boost(V); shape with error E; store at [r2, OFF].
@ Temps r10, r11; the limiter path also borrows r9 (saved at [sp, #8]).
.macro SAMPLE V, E, OFF
 mul r10,\V,r6
 mov \V,r10,asr #8
 add r10,\V,r7
 cmp r10,r7,lsl #1
 bhi 8f
9:
 add \V,\V,\E
 sub r10,\V,#0x7f00            @ clamp to -32768..32639
 cmp r10,#0x80
 movge \V,#0x7f00
 orrge \V,\V,#0x7f
 cmn \V,#0x8000
 mvnlt \V,#0x7f00
 biclt \V,\V,#0xff
 add r10,\V,#128
 mov r10,r10,asr #8
 sub \E,\V,r10,lsl #8
 strb r10,[r2,\OFF]
 .subsection 1
8:                            @ |v| > knee: KNEE + curve(|v| - KNEE), sign kept
 movs r10,\V
 rsbmi r10,\V,#0
 sub r10,r10,r7
 mov r11,r10,lsr #10
 cmp r11,#256
 movhs r11,#256
 movhs r10,#0
 add r11,r8,r11,lsl #1
 mov r10,r10,lsl #22
 str r9,[sp,#8]
 ldrsh r9,[r11,#2]
 ldrsh r11,[r11]
 sub r9,r9,r11
 mov r10,r10,lsr #22
 mul r10,r9,r10
 add r11,r11,r10,asr #10
 add r11,r11,r7
 cmp \V,#0
 rsblt \V,r11,#0
 movge \V,r11
 ldr r9,[sp,#8]
 b 9b
 .subsection 0
.endm
gac4_boost64:
 push {r4-r11,lr}
 sub sp,sp,#12
 str r3,[sp]
 add r10,r1,#128
 str r10,[sp,#4]
 ldmia r3,{r4,r5,r9}
 ldr r6,[r3,#12]
 ldr r3,[r3,#16]
 ldr r7,=BOOST_KNEE
 ldr r8,=boost_curve
1:
 ldr r10,[r1],#4              @ s1 (odd frame); even frame: (prev + s1) >> 1
 add r11,r9,r10
 mov r9,r10
 ldr r10,[r0],#4              @ m0
 add lr,r10,r11,asr #1
 sub r12,r10,r11,asr #1
 SAMPLE lr,r4,#0
 SAMPLE r12,r5,r3
 add r2,r2,#1
 ldr r10,[r0],#4              @ m1
 add lr,r10,r9
 sub r12,r10,r9
 SAMPLE lr,r4,#0
 SAMPLE r12,r5,r3
 add r2,r2,#1
 ldr r10,[sp,#4]
 cmp r1,r10
 bne 1b
 ldr r3,[sp]
 stmia r3,{r4,r5,r9}
 add sp,sp,#12
 pop {r4-r11,lr}
 bx lr
.ltorg
.size gac4_boost64,.-gac4_boost64
.section .note.GNU-stack,"",%progbits
