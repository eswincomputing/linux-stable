/*************************************************************************/ /*!
@File           rgxtransfer_shader.h
@Title          TQ binary shader file info
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    This header holds info about TQ binary shader file generated
                by the TQ shader factory. This header is need by shader factory
                when generating the file; by services KM when reading and
                loading the file into memory; and by services UM when
                constructing blits using the shaders.
@License        Dual MIT/GPLv2

The contents of this file are subject to the MIT license as set out below.

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

Alternatively, the contents of this file may be used under the terms of
the GNU General Public License Version 2 ("GPL") in which case the provisions
of GPL are applicable instead of those above.

If you wish to allow use of your version of this file only under the terms of
GPL, and not to allow others to use your version of this file under the terms
of the MIT license, indicate your decision by deleting the provisions above
and replace them with the notice and other provisions required by GPL as set
out in the file called "GPL-COPYING" included in this distribution. If you do
not delete the provisions above, a recipient may use your version of this file
under the terms of either the MIT license or GPL.

This License is also included in this distribution in the file called
"MIT-COPYING".

EXCEPT AS OTHERWISE STATED IN A NEGOTIATED AGREEMENT: (A) THE SOFTWARE IS
PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING
BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR
PURPOSE AND NONINFRINGEMENT; AND (B) IN NO EVENT SHALL THE AUTHORS OR
COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/ /**************************************************************************/

#if !defined(RGXSHADERHEADER_H)
#define RGXSHADERHEADER_H

#include "pvrversion.h"

typedef struct _RGX_SHADER_HEADER_
{
	IMG_UINT32 ui32Version;
	IMG_UINT32 ui32NumFragment;
	IMG_UINT32 ui32SizeFragment;
	IMG_UINT32 ui32SizeClientMem;
} RGX_SHADER_HEADER;

/* TQ shaders version is used to check compatibility between the
   binary TQ shaders file and the DDK. This number should be incremented
   if a change to the TQ shader factory breaks compatibility. */
#define RGX_TQ_SHADERS_VERSION 2U

#define RGX_TQ_SHADERS_VERSION_PACK \
	(((RGX_TQ_SHADERS_VERSION & 0xFFU) << 16) | ((PVRVERSION_MAJ & 0xFFU) << 8) | ((PVRVERSION_MIN & 0xFFU) << 0))

#endif /* RGXSHADERHEADER_H */
