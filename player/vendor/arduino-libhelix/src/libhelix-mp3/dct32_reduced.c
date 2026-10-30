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
/* Approximate reduced FDCT via independently generated basis responses.
 * Derived from Helix equations; inherited RPSL/RCSL applies.
 * int64 accumulation bound tested; no float, no waveform-fitted gain. */
#include "coder.h"
#include <stdint.h>
static const int coeff[17][16] __attribute__((section(".iwram.coeff"),aligned(4)))={{131072,131072,131072,131072,131072,131072,131072,131072,131072,131072,131072,131072,131072,131072,131072,131072},{92682,-92682,-92682,92682,92682,-92682,-92682,92682,92682,-92682,-92682,92682,92682,-92682,-92682,92682},{83151,-115595,-38048,130441,-12847,-125428,61787,101320,-101320,-61787,125428,12847,-130441,38048,115595,-83151},{72820,-128553,25571,108982,-108982,-25571,128553,-72820,-72820,128553,-25571,-108982,108982,25571,-128553,72820},{61787,-130441,83151,38048,-125428,101320,12847,-115595,115595,-12847,-101320,125428,-38048,-83151,130441,-61787},{50159,-121095,121095,-50159,-50159,121095,-121095,50159,50159,-121095,121095,-50159,-50159,121095,-121095,50159},{38048,-101320,130441,-115595,61787,12847,-83151,125428,-125428,83151,-12847,-61787,115595,-130441,101320,-38048},{25571,-72820,108982,-128553,128554,-108982,72820,-25571,-25571,72820,-108982,128554,-128553,108982,-72820,25571},{12847,-38048,61787,-83151,101320,-115595,125428,-130441,130441,-125428,115595,-101320,83151,-61787,38048,-12847},{92682,-92682,-92682,92682,92682,-92682,-92682,92682,92682,-92682,-92682,92682,92682,-92682,-92682,92682},{101320,-61787,-125428,12847,130441,38048,-115595,-83151,83151,115595,-38048,-130441,-12847,125428,61787,-101320},{108982,-25571,-128553,-72820,72820,128553,25571,-108982,-108982,25571,128553,72820,-72820,-128553,-25571,108982},{115595,12847,-101320,-125428,-38048,83151,130441,61787,-61787,-130441,-83151,38048,125428,101320,-12847,-115595},{121095,50159,-50159,-121095,-121095,-50159,50159,121095,121095,50159,-50159,-121095,-121095,-50159,50159,121095},{125428,83151,12847,-61787,-115595,-130441,-101320,-38048,38048,101320,130441,115595,61787,-12847,-83151,-125428},{128553,108982,72820,25571,-25571,-72820,-108982,-128554,-128554,-108982,-72820,-25571,25571,72820,108982,128553},{130441,125428,115595,101320,83151,61787,38048,12847,-12847,-38048,-61787,-83151,-101320,-115595,-125428,-130441}};
/* Exact common-factor normalization: all basis coefficients divisible by 64.
 * 16 * 2^31 * 2^17 / 2^24 = 2^28: postshift fits int32. */
void __attribute__((section(".iwram"),noinline,optimize("O3"))) FDCT32Reduced(int *buf,int *dest,int offset,int odd,int gb,int shift,int bands) {
 (void)gb; int step=1<<shift;
 for(int row=0;row<17;row++) {
  int phase=row==0?16:2*((row-1)&7);
  if(phase & (step-1))continue;
  int64_t sum=INT64_C(1)<<23;
  for(int j=0;j<bands;j++)sum+=(int64_t)buf[j]*coeff[row][j];
  int32_t q=(int32_t)(sum>>24);if(q>16777216)q=16777216;if(q< -16777216)q= -16777216;
  int *p;
  if(row==0)p=dest+1024+2*((offset-odd)&7)+(odd?0:VBUF_LENGTH);
  else if(row<=8)p=dest+64*phase+2*offset+(odd?VBUF_LENGTH:0);
  else p=dest+64*phase+32+2*((offset-odd)&7)+(odd?0:VBUF_LENGTH);
  p[0]=p[16]=(int)q;
 }
}
