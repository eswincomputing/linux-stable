/*************************************************************************/ /*!
@Title          Kernel reservation object compatibility header
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    Per-version macros to allow code to seamlessly use older kernel
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

#ifndef __PVR_DMA_RESV_H__
#define __PVR_DMA_RESV_H__

#include <linux/version.h>

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 4, 0))
#include <linux/dma-resv.h>
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 17, 0))
#define dma_resv_get_excl		dma_resv_excl_fence
#define dma_resv_get_list		dma_resv_shared_list
#define dma_resv_test_signaled_rcu		dma_resv_test_signaled
#define dma_resv_wait_timeout_rcu	dma_resv_wait_timeout
#endif
#else
#include <linux/reservation.h>

/* Reservation object types */
#define dma_resv			reservation_object
#define dma_resv_list			reservation_object_list

/* Reservation object functions */
#define dma_resv_add_excl_fence		reservation_object_add_excl_fence
#define dma_resv_add_shared_fence	reservation_object_add_shared_fence
#define dma_resv_fini			reservation_object_fini
#define dma_resv_get_excl		reservation_object_get_excl
#define dma_resv_get_list		reservation_object_get_list
#define dma_resv_held			reservation_object_held
#define dma_resv_init			reservation_object_init
#define dma_resv_reserve_shared		reservation_object_reserve_shared
#define dma_resv_test_signaled_rcu	reservation_object_test_signaled_rcu
#define dma_resv_wait_timeout_rcu	reservation_object_wait_timeout_rcu
#endif

#if (LINUX_VERSION_CODE < KERNEL_VERSION(5, 14, 0))

#define dma_resv_shared_list   dma_resv_get_list
#define dma_resv_excl_fence    dma_resv_get_excl
#define dma_resv_wait_timeout  dma_resv_wait_timeout_rcu
#define dma_resv_test_signaled dma_resv_test_signaled_rcu
#define dma_resv_get_fences    dma_resv_get_fences_rcu

#endif /* (LINUX_VERSION_CODE < KERNEL_VERSION(5, 14, 0)) */

#endif /* __PVR_DMA_RESV_H__ */
