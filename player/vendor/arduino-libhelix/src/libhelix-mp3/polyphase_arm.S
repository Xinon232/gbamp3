/*
2026-09-12: This source notice was added during GBA release packaging.
Existing code, copyright notices and earlier modifications are preserved.
This packaging step changes no decoder algorithm. It does not reconstruct
unknown earlier change dates; see the accompanying modification provenance.

EXHIBIT A.  

"Copyright &copy; 1995-2002
RealNetworks, Inc. and/or its licensors. All Rights Reserved.

The contents of this file, and the files included with this file, are subject to
the current version of the RealNetworks Public Source License Version 1.0 (the
"RPSL") available at https://www.helixcommunity.org/content/rpsl unless you have
licensed the file under the RealNetworks Community Source License Version 1.0
(the "RCSL") available at https://www.helixcommunity.org/content/rcsl, in which
case the RCSL will apply. You may also obtain the license terms directly from
RealNetworks. You may not use this file except in compliance with the RPSL or,
if you have a valid RCSL with RealNetworks applicable to this file, the RCSL.
Please see the applicable RPSL or RCSL for the rights, obligations and
limitations governing use of the contents of the file.

This file is part of the Helix DNA Technology. RealNetworks is the developer of
the Original code and owns the copyrights in the portions it created.

This file, and the files included with this file, is distributed and made
available on an 'AS IS' basis, WITHOUT WARRANTY OF ANY KIND, EITHER EXPRESS OR
IMPLIED, AND REALNETWORKS HEREBY DISCLAIMS ALL SUCH WARRANTIES, INCLUDING
WITHOUT LIMITATION, ANY WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR
PURPOSE, QUIET ENJOYMENT OR NON-INFRINGEMENT.

Contributor(s): ____________________________________ 

Technology Compatibility Kit Test
Suite(s) Location (if licensed under the RCSL): ______________________________ 

Object Code Notice: Helix DNA Client technology included. Copyright (c)
RealNetworks, Inc., 1995-2002. All rights reserved.
*/
/* Overnight layout: LR adjacent, paired LDM; no precision change. */
/* winner_fused_stereo: E3 independently adapted from local Helix equations.
 * RPSL/RCSL provenance: see inherited polyphase.c and PROVENANCE.md.
 * Input persistent Q16 history and Q14 coefficients (E1/E2 SSO).
 * Four independent modulo32 accumulators; identical output to E2.
 * Right-channel product order reverses within each pair; modulo32 addition
 * is associative, with coefficient negation still modulo32.
 * No input/history writes, no new bounds or alignment assumptions.
 */
.syntax unified
.cpu arm7tdmi
.arm
.section .text.xmp3_PolyphaseStereo,"ax",%progbits
.align 2
.global xmp3_PolyphaseStereo
.type xmp3_PolyphaseStereo,%function
.macro clip acc,dst,off
 mov r10,\acc,asr #15
 mov r11,r10,asr #31
 cmp r11,r10,asr #15
 eorne r10,r11,lr
 strh r10,[\dst,#\off]
.endm
.macro zero j
 ldmia r2!,{r8,r9}
 ldmia r1!,{r10,r11}
 mla r4,r10,r8,r4
 mla r5,r11,r8,r5
 rsb r9,r9,#0
 ldr r10,[r1,#(176-16*\j)]
 ldr r11,[r1,#(180-16*\j)]
 mla r4,r10,r9,r4
 mla r5,r11,r9,r5
.endm
.macro pair j
 ldmia r2!,{r8,r9}
 ldmia r1!,{r10,r11}
 mla r4,r10,r8,r4
 mla r6,r10,r9,r6
 mla r5,r11,r8,r5
 mla r7,r11,r9,r7
 rsb r9,r9,#0
 ldr r10,[r1,#(176-16*\j)]
 ldr r11,[r1,#(180-16*\j)]
 mla r4,r10,r9,r4
 mla r6,r10,r8,r6
 mla r5,r11,r9,r5
 mla r7,r11,r8,r7
.endm
xmp3_PolyphaseStereo:
 push {r0-r12,lr}
 mov lr,#0x7f00
 orr lr,lr,#0xff
 mov r4,#16384
 mov r5,#16384
 zero 0
 zero 1
 zero 2
 zero 3
 zero 4
 zero 5
 zero 6
 zero 7
 clip r4,r0,0
 clip r5,r0,2
 add r2,r2,#960
 add r1,r1,#4032
 mov r4,#16384
 mov r5,#16384
 mov r12,#8
.Lsixteen:
 ldr r8,[r2],#4
 ldmia r1!,{r10,r11}
 mla r4,r10,r8,r4
 mla r5,r11,r8,r5
 subs r12,r12,#1
 bne .Lsixteen
 ldr r12,[sp,#12]
 mov r3,#64
 mov r3,r3,lsr r12
 add r3,r0,r3
 clip r4,r3,0
 clip r5,r3,2
 ldmia sp,{r0-r2}
 ldr r12,[sp,#12]
 mov r3,#128
 mov r3,r3,lsr r12
 sub r3,r3,#4
 add r3,r0,r3
 add r0,r0,#4
 mov r10,#256
 add r1,r1,r10,lsl r12
 mov r10,#64
 add r2,r2,r10,lsl r12
 mov r10,#16
 mov r12,r10,lsr r12
 sub r12,r12,#1
.Lpairs:
 mov r4,#16384
 mov r5,#16384
 mov r6,#16384
 mov r7,#16384
 pair 0
 pair 1
 pair 2
 pair 3
 pair 4
 pair 5
 pair 6
 pair 7
 clip r4,r0,0
 clip r5,r0,2
 clip r6,r3,0
 clip r7,r3,2
 add r0,r0,#4
 sub r3,r3,#4
 ldr r10,[sp,#12]
 mov r11,#256
 mov r11,r11,lsl r10
 sub r11,r11,#64
 add r1,r1,r11
 mov r11,#64
 mov r11,r11,lsl r10
 sub r11,r11,#64
 add r2,r2,r11
 subs r12,r12,#1
 bne .Lpairs
 pop {r0-r12,lr}
 bx lr
.size xmp3_PolyphaseStereo,.-xmp3_PolyphaseStereo
.section .note.GNU-stack,"",%progbits
