/*******************************************************************************
@File
@Title          Server bridge for mmextmem
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    Implements the server side of the bridge for mmextmem
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
*******************************************************************************/

#include <linux/uaccess.h>

#include "img_defs.h"

#include "devicemem_server.h"
#include "pmr.h"
#include "devicemem_heapcfg.h"
#include "physmem.h"
#include "physmem_extmem.h"

#include "common_mmextmem_bridge.h"

#include "allocmem.h"
#include "pvr_debug.h"
#include "connection_server.h"
#include "pvr_bridge.h"
#if defined(SUPPORT_RGX)
#include "rgx_bridge.h"
#endif
#include "srvcore.h"
#include "handle.h"

#include <linux/slab.h>

/* ***************************************************************************
 * Server-side bridge entry points
 */

static PVRSRV_ERROR _PhysmemWrapExtMempsPMRPtrIntRelease(void *pvData)
{
	PVRSRV_ERROR eError;
	eError = PMRUnrefPMR((PMR *) pvData);
	return eError;
}

static IMG_INT
PVRSRVBridgePhysmemWrapExtMem(IMG_UINT32 ui32DispatchTableEntry,
			      IMG_UINT8 * psPhysmemWrapExtMemIN_UI8,
			      IMG_UINT8 * psPhysmemWrapExtMemOUT_UI8,
			      CONNECTION_DATA * psConnection)
{
	PVRSRV_BRIDGE_IN_PHYSMEMWRAPEXTMEM *psPhysmemWrapExtMemIN =
	    (PVRSRV_BRIDGE_IN_PHYSMEMWRAPEXTMEM *) IMG_OFFSET_ADDR(psPhysmemWrapExtMemIN_UI8, 0);
	PVRSRV_BRIDGE_OUT_PHYSMEMWRAPEXTMEM *psPhysmemWrapExtMemOUT =
	    (PVRSRV_BRIDGE_OUT_PHYSMEMWRAPEXTMEM *) IMG_OFFSET_ADDR(psPhysmemWrapExtMemOUT_UI8, 0);

	PMR *psPMRPtrInt = NULL;

	psPhysmemWrapExtMemOUT->eError =
	    PhysmemWrapExtMem(psConnection, OSGetDevNode(psConnection),
			      psPhysmemWrapExtMemIN->uiSize,
			      psPhysmemWrapExtMemIN->ui64CpuVAddr,
			      psPhysmemWrapExtMemIN->uiFlags, &psPMRPtrInt);
	/* Exit early if bridged call fails */
	if (unlikely(psPhysmemWrapExtMemOUT->eError != PVRSRV_OK))
	{
		goto PhysmemWrapExtMem_exit;
	}

	/* Lock over handle creation. */
	LockHandle(psConnection->psHandleBase);

	psPhysmemWrapExtMemOUT->eError = PVRSRVAllocHandleUnlocked(psConnection->psHandleBase,
								   &psPhysmemWrapExtMemOUT->hPMRPtr,
								   (void *)psPMRPtrInt,
								   PVRSRV_HANDLE_TYPE_PHYSMEM_PMR,
								   PVRSRV_HANDLE_ALLOC_FLAG_MULTI,
								   (PFN_HANDLE_RELEASE) &
								   _PhysmemWrapExtMempsPMRPtrIntRelease);
	if (unlikely(psPhysmemWrapExtMemOUT->eError != PVRSRV_OK))
	{
		UnlockHandle(psConnection->psHandleBase);
		goto PhysmemWrapExtMem_exit;
	}

	/* Release now we have created handles. */
	UnlockHandle(psConnection->psHandleBase);

PhysmemWrapExtMem_exit:

	if (psPhysmemWrapExtMemOUT->eError != PVRSRV_OK)
	{
		if (psPMRPtrInt)
		{
			LockHandle(KERNEL_HANDLE_BASE);
			PMRUnrefPMR(psPMRPtrInt);
			UnlockHandle(KERNEL_HANDLE_BASE);
		}
	}

	return 0;
}

/* ***************************************************************************
 * Server bridge dispatch related glue
 */

PVRSRV_ERROR InitMMEXTMEMBridge(void);
void DeinitMMEXTMEMBridge(void);

/*
 * Register all MMEXTMEM functions with services
 */
PVRSRV_ERROR InitMMEXTMEMBridge(void)
{

	SetDispatchTableEntry(PVRSRV_BRIDGE_MMEXTMEM, PVRSRV_BRIDGE_MMEXTMEM_PHYSMEMWRAPEXTMEM,
			      PVRSRVBridgePhysmemWrapExtMem, NULL,
			      sizeof(PVRSRV_BRIDGE_IN_PHYSMEMWRAPEXTMEM),
			      sizeof(PVRSRV_BRIDGE_OUT_PHYSMEMWRAPEXTMEM));

	return PVRSRV_OK;
}

/*
 * Unregister all mmextmem functions with services
 */
void DeinitMMEXTMEMBridge(void)
{

	UnsetDispatchTableEntry(PVRSRV_BRIDGE_MMEXTMEM, PVRSRV_BRIDGE_MMEXTMEM_PHYSMEMWRAPEXTMEM);

}
