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
/* Exact Helix FDCT32 arithmetic core, ARM7TDMI. Retains original radix
 * operation order, signed-high products and modulo-32 additions/shifts.
 * Scaling, clipping and history shuffle remain in dct32.c.
 * ABI: r0 aligned 32-word buffer; preserves r4-r11, SP and interworking LR.
 * 64-byte aligned frame. No ARMv5/DSP instructions. */
.syntax unified
.cpu arm7tdmi
.arm
.section .text.dct32_core,"ax",%progbits
.align 2
.global dct32_core
.type dct32_core,%function
.macro FIRST i,s1,s2
 ldr r2,[r0,#(4*\i)]
 ldr r3,[r0,#(4*(31-\i))]
 ldr r4,[r0,#(4*(15-\i))]
 ldr r5,[r0,#(4*(16+\i))]
 add r6,r2,r3
 sub r2,r2,r3
 add r7,r4,r5
 sub r4,r4,r5
 ldmia r1!,{r8-r10}
 smull r11,r2,r8,r2
 smull r11,r4,r9,r4
 mov r2,r2,lsl #1
 mov r4,r4,lsl #\s1
 add r8,r6,r7
 str r8,[r0,#(4*\i)]
 sub r6,r6,r7
 smull r11,r6,r10,r6
 mov r6,r6,lsl #\s2
 str r6,[r0,#(4*(15-\i))]
 add r8,r2,r4
 str r8,[r0,#(4*(16+\i))]
 sub r2,r2,r4
 smull r11,r2,r10,r2
 mov r2,r2,lsl #\s2
 str r2,[r0,#(4*(31-\i))]
.endm
/* Four-input folded butterfly; output r2,r3,r4,r5. Coefficients Rm,
 * changing signal is Rs for signed early termination, no illegal Rd/Rm overlap. */
.macro FOLD a,b,c,d,shift,post
 ldr r2,[r0,#(4*\a)]
 ldr r3,[r0,#(4*\b)]
 ldr r4,[r0,#(4*\c)]
 ldr r5,[r0,#(4*\d)]
 add r6,r2,r3
 sub r2,r2,r3
 add r7,r4,r5
 sub r4,r4,r5
 ldmia r1!,{r8-r10}
 smull r11,r2,r8,r2
 smull r11,r4,r9,r4
 mov r2,r2,lsl #1
 mov r4,r4,lsl #\shift
 sub r3,r6,r7
 smull r11,r3,r10,r3
 mov r3,r3,lsl #\post
 sub r5,r2,r4
 smull r11,r5,r10,r5
 mov r5,r5,lsl #\post
 add r4,r4,r2
 add r2,r6,r7
.endm
dct32_core:
 stmdb sp!,{r4-r11,lr}
 sub sp,sp,#28
 adr r1,.Ltable
 FIRST 0,5,1
 FIRST 1,3,1
 FIRST 2,3,1
 FIRST 3,2,1
 FIRST 4,2,1
 FIRST 5,1,2
 FIRST 6,1,2
 FIRST 7,1,4
 mov r2,#4
 str r2,[sp,#16]
 ldr r12,.Lcos4
.Lsecond:
 FOLD 0,7,3,4,3,1
 stmia sp,{r2-r5}
 FOLD 1,6,2,5,1,2
 ldmia sp,{r6-r9}
 add r10,r6,r2
 str r10,[r0]
 sub r6,r6,r2
 smull r11,r6,r12,r6
 mov r6,r6,lsl #1
 str r6,[r0,#4]
 add r10,r3,r7
 sub r7,r7,r3
 smull r11,r7,r12,r7
 mov r7,r7,lsl #1
 add r10,r10,r7
 str r10,[r0,#8]
 str r7,[r0,#12]
 add r2,r8,r4
 sub r8,r8,r4
 smull r11,r8,r12,r8
 mov r8,r8,lsl #1
 add r3,r5,r9
 sub r9,r9,r5
 smull r11,r9,r12,r9
 mov r9,r9,lsl #1
 add r3,r3,r9
 add r2,r2,r3
 str r2,[r0,#16]
 add r4,r8,r9
 str r4,[r0,#20]
 add r3,r8,r3
 str r3,[r0,#24]
 str r9,[r0,#28]
 add r0,r0,#32
 ldr r2,[sp,#16]
 subs r2,r2,#1
 str r2,[sp,#16]
 bne .Lsecond
 add sp,sp,#28
 ldmia sp!,{r4-r11,lr}
 bx lr
.Lcos4: .word 0x5a82799a
.Ltable:
 .word 0x4013c251,0x518522fb,0x404f4672
 .word 0x40b345bd,0x6d0b20cf,0x42e13c10
 .word 0x41fa2d6d,0x41d95790,0x48919f44
 .word 0x43f93421,0x5efc8d96,0x52cb0e63
 .word 0x46cc1bc4,0x4ad81a97,0x64e2402e
 .word 0x4a9d9cf0,0x7c7d1db3,0x43e224a9
 .word 0x4fae3711,0x6b6fcf26,0x6e3c92c1
 .word 0x56601ea7,0x5f4cf6eb,0x519e4e04
 .word 0x4140fb46,0x52036742,0x4545e9ef,0x4cf8de88,0x73326bbf,0x539eba45
 .word -0x4140fb46,-0x52036742,0x4545e9ef,-0x4cf8de88,-0x73326bbf,0x539eba45
 .word 0x4140fb46,0x52036742,0x4545e9ef,0x4cf8de88,0x73326bbf,0x539eba45
 .word -0x4140fb46,-0x52036742,0x4545e9ef,-0x4cf8de88,-0x73326bbf,0x539eba45
.size dct32_core,.-dct32_core
.section .note.GNU-stack,"",%progbits
