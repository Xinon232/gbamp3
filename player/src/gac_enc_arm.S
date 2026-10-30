/* Halim Jarrar, 2026. GPL-3.0-or-later with the additional permission described in NOTICES.md (linking with the Helix MP3 decoder). GAC encoder: one step-size candidate of a
 * 32-sample subblock (gac_enc.c ge_candidate(), same arithmetic), ARM7TDMI.
 * Runs from the conversion overlay (IWRAM).
 *
 * int ge_candidate_arm(struct GeCand *p): p->t[32] targets, p->w[40] the
 * reconstruction window (w[0..7] history oldest first; w[8+j] receives x),
 * p->q[32] residuals, p->c[8] Q12 coefficients (0 past the order), p->step,
 * p->inv = round(2^24 / step), p->shift (distortion weight 1 << shift),
 * p->best (64-bit, the best cost so far). Writes p->d (64-bit distortion)
 * and p->live (OR of |q|). Returns 32, or 0 when the weighted distortion
 * passed best (checked after every 4th sample: the candidate loses). */
 .section .iwram.gecand,"ax",%progbits
 .arm
 .align 2
 .global ge_candidate_arm
 .type ge_candidate_arm,%function
/* struct GeCand offsets */
 .equ P_T,0
 .equ P_W,4
 .equ P_Q,8
 .equ P_C,12
 .equ P_STEP,16
 .equ P_INV,20
 .equ P_SHIFT,24
 .equ P_BESTLO,28
 .equ P_BESTHI,32
 .equ P_DLO,36
 .equ P_DHI,40
 .equ P_LIVE,44
ge_candidate_arm:
 push {r4-r11,lr}
 mov r11,r0                 /* p */
 ldr r0,[r11,#P_T]          /* t */
 ldr r1,[r11,#P_W]          /* w: w[j] .. w[j+7] are the 8 newest */
 ldr r2,[r11,#P_Q]          /* q */
 ldr r3,[r11,#P_C]          /* c */
 mov r4,#0                  /* j */
 mov r8,#0                  /* d lo */
 mov r9,#0                  /* d hi */
 mov r10,#0                 /* live */
1:
 /* acc = 2048 + sum c[k] * w[j+7-k] */
 mov r5,#2048
 ldr r6,[r3,#0]
 ldr r7,[r1,#28]
 mla r5,r6,r7,r5
 ldr r6,[r3,#4]
 ldr r7,[r1,#24]
 mla r5,r6,r7,r5
 ldr r6,[r3,#8]
 ldr r7,[r1,#20]
 mla r5,r6,r7,r5
 ldr r6,[r3,#12]
 ldr r7,[r1,#16]
 mla r5,r6,r7,r5
 ldr r6,[r3,#16]
 ldr r7,[r1,#12]
 mla r5,r6,r7,r5
 ldr r6,[r3,#20]
 ldr r7,[r1,#8]
 mla r5,r6,r7,r5
 ldr r6,[r3,#24]
 ldr r7,[r1,#4]
 mla r5,r6,r7,r5
 ldr r6,[r3,#28]
 ldr r7,[r1,#0]
 mla r5,r6,r7,r5
 mov r5,r5,asr #12          /* pj */
 ldrsh r6,[r0],#2           /* t[j] */
 sub r7,r6,r5               /* dv */
 cmp r7,#0
 rsblt r7,r7,#0             /* a = |dv| */
 ldr lr,[r11,#P_INV]
 umull r12,lr,r7,lr         /* a * inv: lo r12, hi lr (at most 2^42; RdLo, RdHi, Rm distinct on ARM7) */
 ldr r7,=0x4ccccc           /* DZ = 0.3 * 2^24 */
 adds r12,r12,r7
 adc lr,lr,#0
 mov r7,r12,lsr #24
 orr r7,r7,lr,lsl #8        /* qq */
 ldr lr,=2047
 cmp r7,lr
 movhi r7,lr
 orr r10,r10,r7             /* live */
 cmp r6,r5                  /* dv < 0 */
 rsblt r7,r7,#0             /* qv */
 strh r7,[r2],#2            /* q[j] */
 ldr lr,[r11,#P_STEP]
 mla r12,r7,lr,r5           /* x = pj + qv * step */
 ldr lr,=32767
 cmp r12,lr
 movgt r12,lr
 ldr lr,=-32768
 cmp r12,lr
 movlt r12,lr
 str r12,[r1,#32]           /* w[j+8] = x */
 sub r12,r12,r6             /* er */
 smlal r8,r9,r12,r12        /* d += er^2 */
 add r1,r1,#4
 add r4,r4,#1
 tst r4,#3
 bne 2f
 /* weighted distortion > best ? stop */
 ldr r12,[r11,#P_SHIFT]
 mov r6,r8,lsl r12
 rsb r7,r12,#32
 mov lr,r9,lsl r12
 cmp r12,#0
 orrne lr,lr,r8,lsr r7      /* (d << shift) hi */
 ldr r7,[r11,#P_BESTHI]
 cmp lr,r7
 bhi 3f
 bne 2f
 ldr r7,[r11,#P_BESTLO]
 cmp r6,r7
 bhi 3f
2:
 cmp r4,#32
 blo 1b
 mov r0,#32                 /* coded */
 b 4f
3:
 mov r0,#0                  /* stopped: costs more than the best */
4:
 str r8,[r11,#P_DLO]
 str r9,[r11,#P_DHI]
 str r10,[r11,#P_LIVE]
 pop {r4-r11,lr}
 bx lr
 .ltorg
 .size ge_candidate_arm,.-ge_candidate_arm

/* void ge_lags(const int32_t *x, unsigned n, int64_t r[9]):
 * r[k] = sum over i < n of x[i] * x[i-k], k = 0..8 (x[-8..-1] are read).
 * Three lags per pass over x; |x| < 2^20 (16-bit samples, or Q4 windowed). */
 .section .iwram.gelags,"ax",%progbits
 .arm
 .align 2
 .global ge_lags
 .type ge_lags,%function
ge_lags:
 push {r4-r11,lr}
 add r1,r0,r1,lsl #2        /* end */
 sub sp,sp,#12
 str r0,[sp]                /* x */
 str r2,[sp,#4]             /* r */
 mov lr,#0                  /* k0 */
5:
 str lr,[sp,#8]
 ldr r0,[sp]                /* p = x */
 sub r2,r0,lr,lsl #2        /* q = x - k0 */
 ldr r5,[r2,#-4]            /* v1 = x[-k0-1] */
 ldr r6,[r2,#-8]            /* v2 = x[-k0-2] */
 mov r7,#0
 mov r8,#0
 mov r9,#0
 mov r10,#0
 mov r11,#0
 mov r12,#0
6:
 ldr r3,[r0],#4             /* a = x[i] */
 ldr r4,[r2],#4             /* x[i-k0] */
 smlal r7,r8,r4,r3
 smlal r9,r10,r5,r3
 smlal r11,r12,r6,r3
 cmp r0,r1
 beq 7f
 ldr r3,[r0],#4
 ldr r6,[r2],#4             /* lag k0: r6, k0+1: r4, k0+2: r5 */
 smlal r7,r8,r6,r3
 smlal r9,r10,r4,r3
 smlal r11,r12,r5,r3
 cmp r0,r1
 beq 7f
 ldr r3,[r0],#4
 ldr r5,[r2],#4             /* lag k0: r5, k0+1: r6, k0+2: r4 */
 smlal r7,r8,r5,r3
 smlal r9,r10,r6,r3
 smlal r11,r12,r4,r3
                            /* r5, r6: x[i-k0], x[i-k0-1] = the next v1, v2 */
 cmp r0,r1
 bne 6b
7:
 ldr lr,[sp,#8]
 ldr r3,[sp,#4]
 add r3,r3,lr,lsl #3
 stmia r3,{r7-r12}
 add lr,lr,#3
 cmp lr,#9
 blo 5b
 add sp,sp,#12
 pop {r4-r11,lr}
 bx lr
 .size ge_lags,.-ge_lags

