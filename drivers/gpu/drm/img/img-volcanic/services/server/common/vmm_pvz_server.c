/*************************************************************************/ /*!
@File			vmm_pvz_server.c
@Title          VM manager server para-virtualization handlers
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    Header provides VMM server para-virtz handler APIs
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

#include "pvrsrv.h"
#include "img_types.h"
#include "img_defs.h"
#include "pvrsrv_error.h"
#include "rgxfwutils.h"

#include "vz_vm.h"
#include "vmm_impl.h"
#include "vz_vmm_pvz.h"
#include "vmm_pvz_server.h"

static inline void
PvzServerLockAcquire(void)
{
#if !defined(FPGA)
	PVRSRV_DATA *psPVRSRVData = PVRSRVGetPVRSRVData();
	OSLockAcquire(psPVRSRVData->hPvzConnectionLock);
#endif
}

static inline void
PvzServerLockRelease(void)
{
#if !defined(FPGA)
	PVRSRV_DATA *psPVRSRVData = PVRSRVGetPVRSRVData();
	OSLockRelease(psPVRSRVData->hPvzConnectionLock);
#endif
}

#define VALIDATE_DRVID_DEVID(ui32DriverID, ui32DevID) do {							\
	if ((ui32DriverID >= RGX_NUM_DRIVERS_SUPPORTED) ||								\
		(ui32DriverID < RGXFW_GUEST_DRIVER_ID_START))								\
	{																				\
		PVR_DPF((PVR_DBG_ERROR,														\
					"%s: Invalid OSID %u. Supported Guest OSID range: %u - %u",		\
					__func__,														\
					ui32DriverID,													\
					RGXFW_GUEST_DRIVER_ID_START,									\
					RGX_NUM_DRIVERS_SUPPORTED-1));									\
		return PVRSRV_ERROR_INVALID_PARAMS;											\
	}																				\
	if (PVRSRVGetDeviceInstance(ui32DevID) == NULL)									\
	{																				\
		PVR_DPF((PVR_DBG_ERROR,														\
					"%s: Invalid Device ID %u.",									\
					__func__,														\
					ui32DevID));													\
		return PVRSRV_ERROR_INVALID_PARAMS;											\
	}																				\
} while (false);

/*
 * ===========================================================
 *  The following server para-virtualization (pvz) functions
 *  are exclusively called by the VM manager (hypervisor) on
 *  behalf of guests to complete guest pvz calls
 *  (guest -> vm manager -> host)
 * ===========================================================
 */

PVRSRV_ERROR
PvzServerMapDevPhysHeap(IMG_UINT32 ui32DriverID,
						IMG_UINT32 ui32DevID,
						IMG_UINT64 ui64Size,
						IMG_UINT64 ui64PAddr)
{
#if defined(RGX_VZ_STATIC_CARVEOUT_FW_HEAPS) && !defined(FPGA)
		/*
		 * Reject hypercall if called on a system configured at build time to
		 * preallocate the Guest's firmware heaps from static carveout memory.
		 */
		PVR_DPF((PVR_DBG_ERROR,
		         "%s: Host PVZ config: Does not match with Guest PVZ config."
		         " Host preallocates the Guest's FW physheap from static memory carveouts at startup.", __func__));
		return PVRSRV_ERROR_INVALID_PVZ_CONFIG;
#else
	PVRSRV_ERROR eError = PVRSRV_OK;

	VALIDATE_DRVID_DEVID(ui32DriverID, ui32DevID);

	PvzServerLockAcquire();

#if defined(SUPPORT_RGX)
	if (IsVmOnline(ui32DriverID, ui32DevID))
	{
		PVRSRV_DEVICE_NODE *psDeviceNode = PVRSRVGetDeviceInstance(ui32DevID);
		IMG_DEV_PHYADDR sDevPAddr = {ui64PAddr};
		IMG_UINT32 sync;

		eError = RGXFwRawHeapAllocMap(psDeviceNode, ui32DriverID, sDevPAddr, ui64Size);
		PVR_LOG_GOTO_IF_ERROR(eError, "RGXFwRawHeapAllocMap", e0);

		/* Invalidate MMU cache in preparation for a kick from this Guest */
		eError = psDeviceNode->pfnMMUCacheInvalidateKick(psDeviceNode, &sync);
		PVR_LOG_GOTO_IF_ERROR(eError, "MMUCacheInvalidateKick", e0);

		/* Everything is ready for the firmware to start interacting with this OS */
		eError = RGXFWSetFwOsState(psDeviceNode->pvDevice, ui32DriverID, RGXFWIF_OS_ONLINE);
	}
	else
	{
		PVR_DPF((PVR_DBG_ERROR, "%s: Paravirtualized request to map Guest%u's Firmware heap"
								" rejected: Guest not allowed to run.", __func__, ui32DriverID));
		eError = PVRSRV_ERROR_PVZ_OSID_IS_OFFLINE;
	}
e0:
#endif /* defined(SUPPORT_RGX) */
	PvzServerLockRelease();

	return eError;
#endif
}

PVRSRV_ERROR
PvzServerUnmapDevPhysHeap(IMG_UINT32 ui32DriverID,
						  IMG_UINT32 ui32DevID)
{
#if defined(RGX_VZ_STATIC_CARVEOUT_FW_HEAPS) && !defined(FPGA)
		/*
		 * Reject hypercall if called on a system configured at built time to
		 * preallocate the Guest's firmware heaps from static carveout memory.
		 */
		PVR_DPF((PVR_DBG_ERROR,
		         "%s: Host PVZ config: Does not match with Guest PVZ config."
		         " Host preallocates the Guest's FW physheap from static memory carveouts at startup.", __func__));
		return PVRSRV_ERROR_INVALID_PVZ_CONFIG;
#else
	PVRSRV_ERROR eError = PVRSRV_OK;

	VALIDATE_DRVID_DEVID(ui32DriverID, ui32DevID);

	PvzServerLockAcquire();

#if defined(SUPPORT_RGX)
	if (IsVmOnline(ui32DriverID, ui32DevID))
	{
		PVRSRV_DEVICE_NODE *psDeviceNode = PVRSRVGetDeviceInstance(ui32DevID);

		/* Order firmware to offload this OS' data and stop accepting commands from it */
		eError = RGXFWSetFwOsState(psDeviceNode->pvDevice, ui32DriverID, RGXFWIF_OS_OFFLINE);
		PVR_LOG_GOTO_IF_ERROR(eError, "RGXFWSetFwOsState", e0);

		/* it is now safe to remove the Guest's memory mappings */
		RGXFwRawHeapUnmapFree(psDeviceNode, ui32DriverID);
	}
e0:
#endif

	PvzServerLockRelease();

	return eError;
#endif
}

/*
 * ============================================================
 *  The following server para-virtualization (pvz) functions
 *  are exclusively called by the VM manager (hypervisor) to
 *  pass side band information to the host (vm manager -> host)
 * ============================================================
 */

PVRSRV_ERROR
PvzServerOnVmOnline(IMG_UINT32 ui32DriverID,
					IMG_UINT32 ui32DevID)
{
	PVRSRV_ERROR eError;

	VALIDATE_DRVID_DEVID(ui32DriverID, ui32DevID);
	PvzServerLockAcquire();
	eError = PvzOnVmOnline(ui32DriverID, ui32DevID);
	PvzServerLockRelease();

	return eError;
}

PVRSRV_ERROR
PvzServerOnVmOffline(IMG_UINT32 ui32DriverID,
					 IMG_UINT32 ui32DevID)
{
	PVRSRV_ERROR eError;

	VALIDATE_DRVID_DEVID(ui32DriverID, ui32DevID);
	PvzServerLockAcquire();
	eError = PvzOnVmOffline(ui32DriverID, ui32DevID);
	PvzServerLockRelease();

	return eError;
}

PVRSRV_ERROR
PvzServerVMMConfigure(VMM_CONF_PARAM eVMMParamType,
					  IMG_UINT32 ui32ParamValue,
					  IMG_UINT32 ui32DevID)
{
	PVRSRV_ERROR eError;

	VALIDATE_DRVID_DEVID(RGXFW_GUEST_DRIVER_ID_START, ui32DevID);
	PvzServerLockAcquire();
	eError = PvzVMMConfigure(eVMMParamType, ui32ParamValue, ui32DevID);
	PvzServerLockRelease();

	return eError;
}

/******************************************************************************
 End of file (vmm_pvz_server.c)
******************************************************************************/
