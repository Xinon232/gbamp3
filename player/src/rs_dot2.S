/* Halim Jarrar, 2026. GPL-3.0-or-later with the additional permission described in NOTICES.md (linking with the Helix MP3 decoder). resample.c's two-channel dot product (ARM7TDMI).
 * build.py assembles it twice: rs_dot2 (conversion overlay) and, with
 * -Drs_dot2=rs_dot2_m, for the MP3 encoder overlay. */
/* uint64_t rs_dot2(const int16_t h[16], const int16_t *l, const int16_t *r):
 * 8192 + sum h[k] l[k] (low word) and the same for r (high word): the 16
 * taps of one output frame of resample.c, both channels. h word aligned. */
 .section .iwram.rsdot,"ax",%progbits   /* in the conversion overlay, and (rs_dot2_m) the MP3 encoder one */
 .arm
 .align 2
 .global rs_dot2
 .type rs_dot2,%function
rs_dot2:
 push {r4-r8,lr}
 mov r3,#8192
 mov r12,#8192
 ldmia r0!,{r4,r5}         /* h[0..3] */
 ldrsh r6,[r1],#2
 ldrsh r7,[r2],#2
 mov r8,r4,lsl #16
 mov r8,r8,asr #16
 mla r3,r8,r6,r3
 mla r12,r8,r7,r12
 ldrsh r6,[r1],#2
 ldrsh r7,[r2],#2
 mov r8,r4,asr #16
 mla r3,r8,r6,r3
 mla r12,r8,r7,r12
 ldrsh r6,[r1],#2
 ldrsh r7,[r2],#2
 mov r8,r5,lsl #16
 mov r8,r8,asr #16
 mla r3,r8,r6,r3
 mla r12,r8,r7,r12
 ldrsh r6,[r1],#2
 ldrsh r7,[r2],#2
 mov r8,r5,asr #16
 mla r3,r8,r6,r3
 mla r12,r8,r7,r12
 ldmia r0!,{r4,r5}         /* h[4..7] */
 ldrsh r6,[r1],#2
 ldrsh r7,[r2],#2
 mov r8,r4,lsl #16
 mov r8,r8,asr #16
 mla r3,r8,r6,r3
 mla r12,r8,r7,r12
 ldrsh r6,[r1],#2
 ldrsh r7,[r2],#2
 mov r8,r4,asr #16
 mla r3,r8,r6,r3
 mla r12,r8,r7,r12
 ldrsh r6,[r1],#2
 ldrsh r7,[r2],#2
 mov r8,r5,lsl #16
 mov r8,r8,asr #16
 mla r3,r8,r6,r3
 mla r12,r8,r7,r12
 ldrsh r6,[r1],#2
 ldrsh r7,[r2],#2
 mov r8,r5,asr #16
 mla r3,r8,r6,r3
 mla r12,r8,r7,r12
 ldmia r0!,{r4,r5}         /* h[8..11] */
 ldrsh r6,[r1],#2
 ldrsh r7,[r2],#2
 mov r8,r4,lsl #16
 mov r8,r8,asr #16
 mla r3,r8,r6,r3
 mla r12,r8,r7,r12
 ldrsh r6,[r1],#2
 ldrsh r7,[r2],#2
 mov r8,r4,asr #16
 mla r3,r8,r6,r3
 mla r12,r8,r7,r12
 ldrsh r6,[r1],#2
 ldrsh r7,[r2],#2
 mov r8,r5,lsl #16
 mov r8,r8,asr #16
 mla r3,r8,r6,r3
 mla r12,r8,r7,r12
 ldrsh r6,[r1],#2
 ldrsh r7,[r2],#2
 mov r8,r5,asr #16
 mla r3,r8,r6,r3
 mla r12,r8,r7,r12
 ldmia r0!,{r4,r5}         /* h[12..15] */
 ldrsh r6,[r1],#2
 ldrsh r7,[r2],#2
 mov r8,r4,lsl #16
 mov r8,r8,asr #16
 mla r3,r8,r6,r3
 mla r12,r8,r7,r12
 ldrsh r6,[r1],#2
 ldrsh r7,[r2],#2
 mov r8,r4,asr #16
 mla r3,r8,r6,r3
 mla r12,r8,r7,r12
 ldrsh r6,[r1],#2
 ldrsh r7,[r2],#2
 mov r8,r5,lsl #16
 mov r8,r8,asr #16
 mla r3,r8,r6,r3
 mla r12,r8,r7,r12
 ldrsh r6,[r1],#2
 ldrsh r7,[r2],#2
 mov r8,r5,asr #16
 mla r3,r8,r6,r3
 mla r12,r8,r7,r12
 mov r0,r3
 mov r1,r12
 pop {r4-r8,lr}
 bx lr
 .size rs_dot2,.-rs_dot2
