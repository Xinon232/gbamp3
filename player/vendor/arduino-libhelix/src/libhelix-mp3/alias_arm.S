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
/* Exact Helix alias butterflies: truncate each product before add/sub.
 * ARM7TDMI, signals in Rs for early termination. 40-byte aligned frame.
 * r4-r11, SP/LR preserved; BX return supports Thumb callers.
 */
.syntax unified
.cpu arm7tdmi
.arm
.section .text.alias_kernel,"ax",%progbits
.align 2
.global alias_kernel
.type alias_kernel,%function
alias_kernel:
 cmp r1,#0
 bxle lr
 push {r4-r11,lr}
 sub sp,sp,#4
1:
 add r0,r0,#72
 sub lr,r0,#4
 adr r2,alias_coefficients
 mov r3,#8
2:
 ldmia r2!,{r4,r5}
 ldr r6,[lr],#-4
 ldr r7,[r0],#4
 smull r12,r8,r4,r6
 smull r12,r9,r5,r7
 smull r12,r10,r4,r7
 smull r12,r11,r5,r6
 sub r8,r8,r9
 add r10,r10,r11
 mov r8,r8,lsl #1
 mov r10,r10,lsl #1
 str r8,[lr,#4]
 str r10,[r0,#-4]
 subs r3,r3,#1
 bne 2b
 sub r0,r0,#32
 subs r1,r1,#1
 bne 1b
 add sp,sp,#4
 pop {r4-r11,lr}
 bx lr
.align 2
alias_coefficients:
.word 0x6dc253f0,0xbe2500aa,0x70dcebe4,0xc39e4949
.word 0x798d6e73,0xd7e33f4a,0x7ddd40a7,0xe8b71176
.word 0x7f6d20b7,0xf3e4fe2f,0x7fe47e40,0xfac1a3c7
.word 0x7ffcb263,0xfe2ebdc6,0x7fffc694,0xff86c25d
.size alias_kernel,.-alias_kernel
.section .note.GNU-stack,"",%progbits
