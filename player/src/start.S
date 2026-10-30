/* Halim Jarrar, 2026. Freestanding GBA startup. GPL-3.0-or-later with the additional permission described in NOTICES.md (linking with the Helix MP3 decoder). */
.syntax unified
.cpu arm7tdmi
.arm
.section .text.start,"ax"
.global _start
_start:
 b reset
 .space 188
reset:
 mov r0,#0xd2
 msr cpsr_c,r0
 ldr r0,=0x04000208
 mov r1,#0
 strh r1,[r0]
 ldr sp,=0x03007fa0
 mov r0,#0xdf
 msr cpsr_c,r0
 ldr sp,=0x03007e00
 ldr r0,=__iwram_load
 ldr r1,=__iwram_start
 ldr r2,=__iwram_common_end   @ decoder overlays are copied by overlay.c
1: cmp r1,r2
 ldrlo r3,[r0],#4
 strlo r3,[r1],#4
 blo 1b
 ldr r0,=__data_load
 ldr r1,=__data_start
 ldr r2,=__data_end
2: cmp r1,r2
 ldrlo r3,[r0],#4
 strlo r3,[r1],#4
 blo 2b
 ldr r1,=__bss_start
 ldr r2,=__bss_end
 mov r3,#0
3: cmp r1,r2
 strlo r3,[r1],#4
 blo 3b
 ldr r0,=main
 mov lr,pc
 bx r0
4: b 4b
.global __aeabi_uidiv
__aeabi_uidiv:
 b rt_udiv
.global __aeabi_idiv
__aeabi_idiv:
 push {r4,lr}
 eor r4,r0,r1
 cmp r0,#0
 rsblt r0,r0,#0
 cmp r1,#0
 rsblt r1,r1,#0
 bl rt_udiv
 cmp r4,#0
 rsblt r0,r0,#0
 pop {r4,lr}
 bx lr
.global __aeabi_uidivmod
__aeabi_uidivmod:
 push {r0,r1,r4,lr}
 bl rt_udiv
 ldmia sp,{r2,r3}
 mul r1,r0,r3
 rsb r1,r1,r2
 add sp,sp,#8
 pop {r4,lr}
 bx lr
.global __aeabi_idivmod
__aeabi_idivmod:
 push {r0,r1,r4,lr}
 bl __aeabi_idiv
 ldmia sp,{r2,r3}
 mul r1,r0,r3
 rsb r1,r1,r2
 add sp,sp,#8
 pop {r4,lr}
 bx lr
.section .note.GNU-stack,"",%progbits
