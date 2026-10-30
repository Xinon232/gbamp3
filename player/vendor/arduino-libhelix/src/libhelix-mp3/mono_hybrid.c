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
/* 2026-09-12: window-aware mono transform sharing.
 * Equal current window/transform and previous overlap-window metadata make
 * alias + IMDCT + window/overlap linear in L/R, apart from fixed-point rounding.
 * Average both spectra and unwindowed history only under those equalities.
 * Duplicate resulting averaged history into both channels: either later
 * independent transform sees the same prior contribution, whose average is
 * unchanged. If current OR previous metadata differ, perform both complete
 * transforms and average afterward. No reinterpretation of short/mixed history.
 */
#include "coder.h"
#include <string.h>
static __attribute__((section(".iwram"),noinline,noclone,optimize("O2"))) void mix(int *a,const int*b,int n,int average){
 if(average)for(int i=0;i<n;i++)a[i]=RT_MONO_AVERAGE(a[i],b[i]);
 else for(int i=0;i<n;i++)a[i]=b[i];
}
int IMDCTMono(MP3DecInfo *p,int gr){
 SideInfo *si=p->SideInfoPS;HuffmanInfo *hi=p->HuffmanInfoPS;IMDCTInfo *mi=p->IMDCTInfoPS;
 p->hybridMono=0;
 if(p->nChans==1)return IMDCT(p,gr,0);
 if(si->sis[gr][0].blockType==si->sis[gr][1].blockType &&
    si->sis[gr][0].mixedBlock==si->sis[gr][1].mixedBlock &&
    mi->prevType[0]==mi->prevType[1] && mi->prevWinSwitch[0]==mi->prevWinSwitch[1]){
  int bands=RT_OUTPUT_BANDS(p);
  mix(hi->huffDecBuf[0],hi->huffDecBuf[1],MIN(576,bands*18+8),1);
  mix(mi->overBuf[0],mi->overBuf[1],bands*9,1);
  hi->nonZeroBound[0]=MAX(hi->nonZeroBound[0],hi->nonZeroBound[1]);
  hi->gb[0]=MIN(hi->gb[0],hi->gb[1]);
  mi->numPrevIMDCT[0]=MAX(mi->numPrevIMDCT[0],mi->numPrevIMDCT[1]);
  int e=IMDCT(p,gr,0);if(e)return e;
  mix(mi->overBuf[1],mi->overBuf[0],bands*9,0);
  mi->prevType[1]=mi->prevType[0];mi->prevWinSwitch[1]=mi->prevWinSwitch[0];
  mi->numPrevIMDCT[1]=mi->numPrevIMDCT[0];p->hybridMono=1;return 0;
 }
 int e=IMDCT(p,gr,0);return e?e:IMDCT(p,gr,1);
}
