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
/* ***** BEGIN LICENSE BLOCK ***** 
 * Version: RCSL 1.0/RPSL 1.0 
 *  
 * Portions Copyright (c) 1995-2002 RealNetworks, Inc. All Rights Reserved. 
 *      
 * The contents of this file, and the files included with this file, are 
 * subject to the current version of the RealNetworks Public Source License 
 * Version 1.0 (the "RPSL") available at 
 * http://www.helixcommunity.org/content/rpsl unless you have licensed 
 * the file under the RealNetworks Community Source License Version 1.0 
 * (the "RCSL") available at http://www.helixcommunity.org/content/rcsl, 
 * in which case the RCSL will apply. You may also obtain the license terms 
 * directly from RealNetworks.  You may not use this file except in 
 * compliance with the RPSL or, if you have a valid RCSL with RealNetworks 
 * applicable to this file, the RCSL.  Please see the applicable RPSL or 
 * RCSL for the rights, obligations and limitations governing use of the 
 * contents of the file.  
 *  
 * This file is part of the Helix DNA Technology. RealNetworks is the 
 * developer of the Original Code and owns the copyrights in the portions 
 * it created. 
 *  
 * This file, and the files included with this file, is distributed and made 
 * available on an 'AS IS' basis, WITHOUT WARRANTY OF ANY KIND, EITHER 
 * EXPRESS OR IMPLIED, AND REALNETWORKS HEREBY DISCLAIMS ALL SUCH WARRANTIES, 
 * INCLUDING WITHOUT LIMITATION, ANY WARRANTIES OF MERCHANTABILITY, FITNESS 
 * FOR A PARTICULAR PURPOSE, QUIET ENJOYMENT OR NON-INFRINGEMENT. 
 * 
 * Technology Compatibility Kit Test Suite(s) Location: 
 *    http://www.helixcommunity.org/content/tck 
 * 
 * Contributor(s): 
 *  
 * ***** END LICENSE BLOCK ***** */ 

/**************************************************************************************
 * Fixed-point MP3 decoder
 * Jon Recker (jrecker@real.com), Ken Cooke (kenc@real.com)
 * June 2003
 *
 * subband.c - subband transform (synthesis filterbank implemented via 32-point DCT
 *               followed by polyphase filter)
 **************************************************************************************/

#include "coder.h"
#include "assembly.h"
extern void FDCT32Reduced(int*,int*,int,int,int,int,int);

/**************************************************************************************
 * Function:    Subband
 *
 * Description: do subband transform on all the blocks in one granule, all channels
 *
 * Inputs:      filled MP3DecInfo structure, after calling IMDCT for all channels
 *              vbuf[ch] and vindex[ch] must be preserved between calls
 *
 * Outputs:     decoded PCM data, interleaved LRLRLR... if stereo
 *
 * Return:      0 on success,  -1 if null input pointers
 **************************************************************************************/
int __attribute__((optimize("O2"))) Subband(MP3DecInfo *mp3DecInfo, short *pcmBuf)
{
	int b;
 int shift,bands;
	IMDCTInfo *mi;
	SubbandInfo *sbi;

	/* validate pointers */
	if (!mp3DecInfo || !mp3DecInfo->HuffmanInfoPS || !mp3DecInfo->IMDCTInfoPS || !mp3DecInfo->SubbandInfoPS)
		return -1;

	shift = RT_OUTPUT_SHIFT(mp3DecInfo);bands=RT_OUTPUT_BANDS(mp3DecInfo);
	mi = (IMDCTInfo *)(mp3DecInfo->IMDCTInfoPS);
	sbi = (SubbandInfo*)(mp3DecInfo->SubbandInfoPS);

	/* 2026-09-12: mix AFTER independent IMDCT/window/overlap. This is safe
     * for differing block types and mixed-block cutoffs across L/R, and never
     * changes encoded nChans used by entropy, joint-stereo or reservoir code.
     * Only the FDCT/polyphase history becomes mono; native mono is not halved. */
    if (RT_MONO(mp3DecInfo) && mp3DecInfo->nChans == 2 && !mp3DecInfo->hybridMono) {   /* 2026-10-02: not mode 3 */
        for (b=0;b<BLOCK_SIZE;b++) for (int k=0;k<NBANDS;k++)
            mi->outBuf[0][b][k]=RT_MONO_AVERAGE(mi->outBuf[0][b][k],mi->outBuf[1][b][k]);
        mi->gb[0]=MIN(mi->gb[0],mi->gb[1]);
    }
	if (RT_OUTPUT_CHANNELS(mp3DecInfo) == 2) {
		/* stereo */
		for (b = 0; b < BLOCK_SIZE; b++) {
			if(shift) FDCT32Reduced(mi->outBuf[0][b], sbi->vbuf + 0*32, sbi->vindex, (b & 0x01), mi->gb[0],shift,bands); else FDCT32(mi->outBuf[0][b], sbi->vbuf + 0*32, sbi->vindex, (b & 0x01), mi->gb[0]);
			if(shift) FDCT32Reduced(mi->outBuf[1][b], sbi->vbuf + 1, sbi->vindex, (b & 0x01), mi->gb[1],shift,bands); else FDCT32(mi->outBuf[1][b], sbi->vbuf + 1, sbi->vindex, (b & 0x01), mi->gb[1]);
			PolyphaseStereo(pcmBuf, sbi->vbuf + 2*sbi->vindex + VBUF_LENGTH * (b & 0x01), polyCoef, shift);
			sbi->vindex = (sbi->vindex - (b & 0x01)) & 7;
			pcmBuf += (2 * NBANDS) >> shift;
		}
	} else {
		/* mono */
		for (b = 0; b < BLOCK_SIZE; b++) {
			if(shift) FDCT32Reduced(mi->outBuf[0][b], sbi->vbuf + 0*32, sbi->vindex, (b & 0x01), mi->gb[0],shift,bands); else FDCT32(mi->outBuf[0][b], sbi->vbuf + 0*32, sbi->vindex, (b & 0x01), mi->gb[0]);
			PolyphaseMono(pcmBuf, sbi->vbuf + 2*sbi->vindex + VBUF_LENGTH * (b & 0x01), polyCoef, shift);
			sbi->vindex = (sbi->vindex - (b & 0x01)) & 7;
			pcmBuf += NBANDS >> shift;
		}
	}

	return 0;
}

