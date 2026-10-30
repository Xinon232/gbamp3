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
#pragma once
/* 2026-09-12: selected mono averages reconstructed hybrid samples, floor
 * rounding, without overflowing even for arbitrary signed32 inputs. */
#define RT_MONO_AVERAGE(a,b) (((a)>>1)+((b)>>1)+((a)&(b)&1))
#define RT_SHIFT 3
#define RT_FACTOR (1 << RT_SHIFT)
/* 2026-09-27: stereo synthesis runs at the lowest rate that carries the
 * retained bands (32 >> shift == bands): 32 kHz now 1/8 (was 1/4) and
 * 16-24 kHz 1/4 (was 1/2), like 44.1/48 kHz. Retained bandwidth unchanged;
 * halves the polyphase cost of these rates for the 32768 Hz output. */
#define RT_SHIFT_FOR(rate) ((rate)>=32000?3:((rate)>=16000?2:1))
#define RT_BANDS(rate) ((rate)>=32000?4:((rate)>=16000?8:16))

#define RT_RATIONAL_RATE 1
/* 2026-10-02: output mode 3 = every band of every encoded channel at the
 * native rate (original Helix synthesis), for converting songs on the GBA. */
#define RT_FULL(p) ((p)->outputMode==3)
#define RT_MONO(p) ((p)->outputMode==1 || (p)->outputMode==2)
#define RT_OUTPUT_CHANNELS(p) (RT_MONO(p) ? 1 : (p)->nChans)
#define RT_OUTPUT_SHIFT(p) (RT_FULL(p) ? 0 : RT_MONO(p) ? ((p)->samprate>=32000?2:((p)->samprate>=16000?1:0)) : RT_SHIFT_FOR((p)->samprate))
/* 2026-09-26: output mode 2 = mono with 8 bands (7 at 48 kHz) at >= 32 kHz,
 * the most the 1/4-rate synthesis carries; the player picks it per song. */
#define RT_MONO_BANDS(p) ((p)->outputMode==2 ? ((p)->samprate>=48000?7:8) : 6)
#define RT_OUTPUT_BANDS(p) (RT_FULL(p) ? 32 : RT_MONO(p) ? ((p)->samprate>=32000?RT_MONO_BANDS(p):((p)->samprate>=16000?12:24)) : RT_BANDS((p)->samprate))
