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
/* WINNER E1 SSO: Q23 history rounded to Q16, Q14 window, modulo32 MAC; changed quality. */
/* Exact native mono synthesis, ARM7TDMI. Derived from licensed Helix
 * equations; see polyphase.c RPSL/RCSL attribution. Full 32 PCM outputs.
 * 64-bit wrapped sums, signed32 truncation after >>20 then >>6, clip16.
 * 48-byte aligned frame; r4-r11/SP preserved; BX returns to ARM/Thumb.
 * No speculative input reads; addresses are identical to reference C.
 */
.syntax unified
.cpu arm7tdmi
.arm
.section .text.xmp3_PolyphaseMono,"ax",%progbits
.align 2
.global xmp3_PolyphaseMono
.macro clip lo,hi,dst,off
 mov r10,\lo,asr #15
 mov r11,r10,asr #31
 cmp r11,r10,asr #15
 eorne r10,r11,lr
 strh r10,[\dst,#\off]
.endm
.macro phase0 j
 ldmia r2!,{r8,r9}
 ldr r10,[r1,#(8*\j)]
 ldr r11,[r1,#(8*(23-\j))]
 mla r4,r10,r8,r4
 rsb r9,r9,#0
 mla r4,r11,r9,r4
.endm
.macro phase16 j
 ldr r8,[r2],#4
 ldr r10,[r1,#(8*\j)]
 mla r4,r10,r8,r4
.endm
.macro pair j
 ldmia r2!,{r8,r9}
 ldr r10,[r1,#(8*\j)]
 ldr r11,[r1,#(8*(23-\j))]
 mla r4,r10,r8,r4
 mla r6,r10,r9,r6
 rsb r9,r9,#0
 mla r4,r11,r9,r4
 mla r6,r11,r8,r6
.endm
.type xmp3_PolyphaseMono,%function
xmp3_PolyphaseMono:
 push {r0-r12,lr}          /* 48 bytes, no stack traffic inside main loop */
 mov lr,#0x7f00
 orr lr,lr,#0xff
 mov r4,#0x00004000
 phase0 0
 phase0 1
 phase0 2
 phase0 3
 phase0 4
 phase0 5
 phase0 6
 phase0 7
 clip r4,r5,r0,0
 add r2,r2,#960
 add r1,r1,#4096
 mov r4,#0x00004000
 mov r12,#8
.Lsso16:
 ldr r8,[r2],#4
 ldr r10,[r1],#8
 mla r4,r10,r8,r4
 subs r12,r12,#1
 bne .Lsso16
 ldr r12,[sp,#12]
 mov r3,#32
 mov r3,r3,lsr r12
 add r3,r0,r3
 clip r4,r5,r3,0
 ldmia sp,{r0-r2}
 ldr r12,[sp,#12]
 mov r3,#64
 mov r3,r3,lsr r12
 sub r3,r3,#2
 add r3,r0,r3
 add r0,r0,#2
 mov r10,#256
 add r1,r1,r10,lsl r12
 mov r10,#64
 add r2,r2,r10,lsl r12
 mov r10,#16
 mov r12,r10,lsr r12
 sub r12,r12,#1
.Lpairs:
 mov r4,#0x00004000
 mov r6,#0x00004000
 pair 0
 pair 1
 pair 2
 pair 3
 pair 4
 pair 5
 pair 6
 pair 7
 clip r4,r5,r0,0
 clip r6,r7,r3,0
 add r0,r0,#2
 sub r3,r3,#2
 ldr r10,[sp,#12]
 mov r11,#256
 mov r11,r11,lsl r10
 sub r11,r11,#0
 add r1,r1,r11
 mov r11,#64
 mov r11,r11,lsl r10
 sub r11,r11,#64
 add r2,r2,r11
 subs r12,r12,#1
 bne .Lpairs
 pop {r0-r12,lr}
 bx lr
.size xmp3_PolyphaseMono,.-xmp3_PolyphaseMono
.section .note.GNU-stack,"",%progbits
