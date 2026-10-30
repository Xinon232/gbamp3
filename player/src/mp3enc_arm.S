/* Halim Jarrar, 2026. GPL-3.0-or-later with the additional permission described in NOTICES.md (linking with the Helix MP3 decoder). The MP3 encoder's analysis filterbank on the
 * ARM7TDMI (GAC -> MP3, conversion overlay): shine's window_filter_subband
 * (player/vendor/shine/l3subband.c), the same integer results.
 * On the ARM7, SMULL/SMLAL need RdLo, RdHi and Rm distinct. */
 .section .iwram.mp3enc,"ax",%progbits
 .arm
 .align 2

/* void shine_window_arm(const int32_t x[512], unsigned off, int32_t y[64],
 *                       const int32_t w[512]):
 * y[i] = (sum over k < 8 of x[(off+i+64k) & 511] * w[i+64k]) >> 32, with
 * x = sample << 16 (so: the samples times w, >> 16; 16-bit multiplier). */
 .global shine_window_arm
 .type shine_window_arm,%function
shine_window_arm:
 push {r4-r11,lr}
 mov r4,r3                  @ w[i]
 mov r11,#0                 @ i
1:
 add r5,r1,r11
 bic r5,r5,#0xfe00          @ (off + i) & 511 (off < 512, i < 64)
 mov r6,#0
 mov r7,#0
 mov r8,r5
 add r8,r0,r8,lsl #2
 ldrsh r9,[r8,#2]         @ the sample (x = sample << 16)
 ldr r10,[r4,#0]
 smlal r6,r7,r10,r9
 add r8,r5,#64
 bic r8,r8,#512
 add r8,r0,r8,lsl #2
 ldrsh r9,[r8,#2]         @ the sample (x = sample << 16)
 ldr r10,[r4,#256]
 smlal r6,r7,r10,r9
 add r8,r5,#128
 bic r8,r8,#512
 add r8,r0,r8,lsl #2
 ldrsh r9,[r8,#2]         @ the sample (x = sample << 16)
 ldr r10,[r4,#512]
 smlal r6,r7,r10,r9
 add r8,r5,#192
 bic r8,r8,#512
 add r8,r0,r8,lsl #2
 ldrsh r9,[r8,#2]         @ the sample (x = sample << 16)
 ldr r10,[r4,#768]
 smlal r6,r7,r10,r9
 add r8,r5,#256
 bic r8,r8,#512
 add r8,r0,r8,lsl #2
 ldrsh r9,[r8,#2]         @ the sample (x = sample << 16)
 ldr r10,[r4,#1024]
 smlal r6,r7,r10,r9
 add r8,r5,#320
 bic r8,r8,#512
 add r8,r0,r8,lsl #2
 ldrsh r9,[r8,#2]         @ the sample (x = sample << 16)
 ldr r10,[r4,#1280]
 smlal r6,r7,r10,r9
 add r8,r5,#384
 bic r8,r8,#512
 add r8,r0,r8,lsl #2
 ldrsh r9,[r8,#2]         @ the sample (x = sample << 16)
 ldr r10,[r4,#1536]
 smlal r6,r7,r10,r9
 add r8,r5,#448
 bic r8,r8,#512
 add r8,r0,r8,lsl #2
 ldrsh r9,[r8,#2]         @ the sample (x = sample << 16)
 ldr r10,[r4,#1792]
 smlal r6,r7,r10,r9
 mov r6,r6,lsr #16
 orr r6,r6,r7,lsl #16
 str r6,[r2],#4
 add r4,r4,#4
 add r11,r11,#1
 cmp r11,#64
 blo 1b
 pop {r4-r11,lr}
 bx lr
 .ltorg
 .size shine_window_arm,.-shine_window_arm

/* void shine_matrix_arm(const int32_t u[32], int32_t s[32], const int32_t f[32][32]):
 * s[i] = (sum over c of f[i][c] u[c]) >> 32 (f: shine_fl32). */
 .global shine_matrix_arm
 .type shine_matrix_arm,%function
shine_matrix_arm:
 push {r4-r11,lr}
 add r3,r1,#128
 push {r3}                @ end of s
2:
 mov r3,#0
 mov r4,#0
 mov r5,r0
 ldmia r2!,{r6-r9}
 ldmia r5!,{r10-r12,lr}
 smlal r3,r4,r6,r10
 smlal r3,r4,r7,r11
 smlal r3,r4,r8,r12
 smlal r3,r4,r9,lr
 ldmia r2!,{r6-r9}
 ldmia r5!,{r10-r12,lr}
 smlal r3,r4,r6,r10
 smlal r3,r4,r7,r11
 smlal r3,r4,r8,r12
 smlal r3,r4,r9,lr
 ldmia r2!,{r6-r9}
 ldmia r5!,{r10-r12,lr}
 smlal r3,r4,r6,r10
 smlal r3,r4,r7,r11
 smlal r3,r4,r8,r12
 smlal r3,r4,r9,lr
 ldmia r2!,{r6-r9}
 ldmia r5!,{r10-r12,lr}
 smlal r3,r4,r6,r10
 smlal r3,r4,r7,r11
 smlal r3,r4,r8,r12
 smlal r3,r4,r9,lr
 ldmia r2!,{r6-r9}
 ldmia r5!,{r10-r12,lr}
 smlal r3,r4,r6,r10
 smlal r3,r4,r7,r11
 smlal r3,r4,r8,r12
 smlal r3,r4,r9,lr
 ldmia r2!,{r6-r9}
 ldmia r5!,{r10-r12,lr}
 smlal r3,r4,r6,r10
 smlal r3,r4,r7,r11
 smlal r3,r4,r8,r12
 smlal r3,r4,r9,lr
 ldmia r2!,{r6-r9}
 ldmia r5!,{r10-r12,lr}
 smlal r3,r4,r6,r10
 smlal r3,r4,r7,r11
 smlal r3,r4,r8,r12
 smlal r3,r4,r9,lr
 ldmia r2!,{r6-r9}
 ldmia r5!,{r10-r12,lr}
 smlal r3,r4,r6,r10
 smlal r3,r4,r7,r11
 smlal r3,r4,r8,r12
 smlal r3,r4,r9,lr
 str r4,[r1],#4
 ldr r3,[sp]
 cmp r1,r3
 blo 2b
 add sp,sp,#4
 pop {r4-r11,lr}
 bx lr
 .ltorg
 .size shine_matrix_arm,.-shine_matrix_arm
