// SPDX-License-Identifier: GPL-2.0
/*
 * ESWIN cipher serivce driver
 *
 * Copyright 2024, Beijing ESWIN Computing Technology Co., Ltd.. All rights reserved.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, version 2.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 *
 * Authors: Min Lin <linmin@eswincomputing.com>
 */

#include <linux/types.h>
#include <linux/err.h>
#include <linux/kernel.h>
#include <linux/mailbox_client.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/random.h>
#include <uapi/linux/dma-heap.h>
#include <linux/dma-buf.h>
#include <linux/scatterlist.h>
#include <linux/slab.h>
#include <linux/list.h>
#include <linux/mem_perf_api.h>
#include <linux/delay.h>
#include <linux/miscdevice.h>
#include <linux/dma-mapping.h>
#include <linux/debugfs.h>
#include <linux/err.h>
#include <linux/fs.h>
#include <linux/io.h>
#include <linux/poll.h>
#include <linux/uaccess.h>
#include <linux/sched/signal.h>
#include <linux/wait.h>
#include <linux/idr.h>
#include <linux/dma-map-ops.h>
#include <asm/smp.h>

#include <linux/eswin-win2030-sid-cfg.h>
#include <linux/dmabuf-heap-import-helper.h>
#include <linux/eswin_cpuid_hartid_convert.h>
#include <uapi/linux/eswin-ipc-scpu.h>

MODULE_IMPORT_NS(DMA_BUF);

#define LOG_PRINT_DATA_DEBUG_EN	(0)

#ifndef __UNION32
#define __UNION32(MSB, SEC, THD, LSB)                                          \
	((((MSB)&0xff) << 24) | (((SEC)&0xff) << 16) | (((THD)&0xff) << 8) |   \
	 (((LSB)&0xff)))
#endif

#define MAX_RX_TIMEOUT (msecs_to_jiffies(30000))
#define MAX(a, b) (((a) > (b)) ? (a) : (b))

#define IPC_SERVICE_REQ_MAX_LEN sizeof(req_service_t)
#define IPC_SERVICE_RES_MAX_LEN sizeof(res_service_t)


#define IPC_K_HANDLE_MAX_ID	INT_MAX



typedef struct usermem_iova_info {
	struct sg_table table;
	struct list_head node;
}usermem_iova_info_t;

typedef struct ipc_private_data {
	struct heap_root hp_root;
	struct list_head usermem_iova_list;
	struct mutex usermem_iova_lock;
	struct idr cipher_mem_resource_idr; // protected by idr_cipher_mem_lock
	struct mutex idr_cipher_mem_lock;
}ipc_private_data_t;

struct ipc_session {
	struct miscdevice miscdev;
	struct mutex lock;
	struct mbox_client client;
	struct mbox_chan *mbox_channel;
	struct dma_allocation_data send_buff;
	wait_queue_head_t waitq;
	u32 num;
	u8 *req_msg;
	u32 res_size;
	__u32 kinfo_cnt;
	int kinfo_id[MAX_NUM_K_DMA_ALLOC_INFO];

	struct mutex idr_lock;
	struct idr handle_idr;

	struct workqueue_struct *work_q;
	struct work_struct session_work;
	#ifdef ES_CIPHER_QEMU_DEBUG
	struct delayed_work d_session_work;
	#endif

	ipc_private_data_t *ipc_priv;

	res_service_t res_srvc;
	atomic_t receive_data_ready;
	atomic_t ipc_service_ready;
};

typedef struct process_data {
	struct list_head node;
	int id;
}process_data_t;

typedef struct process_data_list {
	struct list_head list; // protected by lock
	struct mutex lock;
	struct ipc_session *session;
}process_data_list_t;

typedef struct cipher_mem_resource_info {
	struct dma_buf *dma_buf;
	khandle_dma_allocation_data_t k_dma_info;
	struct ipc_session *session;
	struct kref refcount;
}cipher_mem_resource_info_t;

// static res_service_t res_srvc;
static DEFINE_MUTEX(dmabuf_bank_lock);

static u32 req_size[SRVC_TYPE_MAX + 2] = {
	sizeof(signature_validation_check_req_t),
	sizeof(image_decryption_req_t),
	sizeof(firmware_download_req_t),
	sizeof(pubkey_download_req_t),
	sizeof(rsa_encdec_req_t),
	sizeof(ecdh_key_req_t),
	sizeof(aes_sm4_encdec_req_t),
	sizeof(sha1_sha256_sm3_digest_req_t),
	sizeof(hmac_req_t),
	sizeof(otp_req_t),
	sizeof(trng_req_t),
	sizeof(addr_region_protection_req_t),
	sizeof(downloadable_init_req_t),
	sizeof(basicio_req_t),
	sizeof(axprot_req_t),
	sizeof(downloadable_destory_req_t),
	sizeof(downloadable_ioctl_req_t),
};

/* Declaration of local functions */
static void ipc_release_mem_khandle_fn(struct kref *ref);

static int ipc_add_cipher_mem_rsc_info_to_idr(struct ipc_session *session, cipher_mem_resource_info_t *pstCipher_mem_rsc_info);
static void ipc_remove_cipher_mem_rsc_info_from_idr(struct ipc_session *session, u32 id);
static cipher_mem_resource_info_t *ipc_find_cipher_mem_rsc_info(struct ipc_session *session, u32 id);

static int ipc_session_mem_info_get(struct ipc_session *session, cipher_create_handle_req_t *pstCreate_handle_req);
static void ipc_session_mem_info_put(struct ipc_session *session);


/*
static struct page *dma_common_vaddr_to_page(void *cpu_addr)
{
	if (is_vmalloc_addr(cpu_addr))
		return vmalloc_to_page(cpu_addr);
	return virt_to_page(cpu_addr);
}
*/
static int ipc_usermem_iova_alloc(struct ipc_session *session, u64 addr, size_t len, dma_addr_t *dma_addr_p)
{
	struct device *dev = session->miscdev.parent;
	ipc_private_data_t *ipc_priv =session->ipc_priv;
	struct page **pages;
	u32 offset, nr_pages, i;
	u64 first, last;
	usermem_iova_info_t *req_data_addr_i;
	struct sg_table *table;
	int ret = -ENOMEM;

	if (!len) {
		dev_err(dev, "invalid userptr size.\n");
		return -EINVAL;
	}
	/* offset into first page */
	offset = offset_in_page(addr);

	/* Calculate number of pages */
	first = (addr & PAGE_MASK) >> PAGE_SHIFT;
	last  = ((addr + len - 1) & PAGE_MASK) >> PAGE_SHIFT;
	nr_pages = last - first + 1;
	dev_dbg(dev, "%s, addr=0x%llx, nr_pages=%d(fist:0x%llx,last:0x%llx), len=0x%lx\n",
		__func__, addr, nr_pages, first, last, len);

	/* alloc array to storing the pages */
	pages = kvmalloc_array(nr_pages,
				   sizeof(struct page *),
				   GFP_KERNEL);
	if (!pages)
		return -ENOMEM;

	/* alloc usermem iova node for storing the iova info(sgt) */
	req_data_addr_i = kzalloc(sizeof(*req_data_addr_i), GFP_KERNEL);
	if (!req_data_addr_i) {
		ret = -ENOMEM;
		goto free_pages_list;
	}

	/* Get user pages into memory */
	ret = get_user_pages_fast(addr & PAGE_MASK,
				nr_pages,
				FOLL_FORCE | FOLL_WRITE,
				pages);
	if (ret != nr_pages) {
		nr_pages = (ret >= 0) ? ret : 0;
		dev_err(dev, "get_user_pages_fast, err=%d [%d]\n",
			ret, nr_pages);
		ret = ret < 0 ? ret : -EINVAL;
		goto free_usermem_iova_i;
	}

	table = &req_data_addr_i->table;
	ret = sg_alloc_table_from_pages(table,
					pages,
					nr_pages, offset,
					len,
					GFP_KERNEL);
	if (ret) {
		dev_err(dev, "sg_alloc_table_from_pages, err=%d\n", ret);
		goto free_user_pages;
	}

	ret = dma_map_sgtable(dev, table, DMA_BIDIRECTIONAL, 0);
	if (ret) {
		dev_err(dev, "dma_map_sgtable, err=%d\n", ret);
		ret = -ENOMEM;
		goto free_sg_table;
	}
	*dma_addr_p = sg_dma_address(table->sgl);
	mutex_lock(&ipc_priv->usermem_iova_lock);
	list_add(&req_data_addr_i->node, &ipc_priv->usermem_iova_list);
	mutex_unlock(&ipc_priv->usermem_iova_lock);

	dev_dbg(dev, "%s, dmaaddr=0x%llx(phys:0x%llx)\n",
		__func__, sg_dma_address(table->sgl), sg_phys(table->sgl));

	kfree(pages);

	return 0;

free_sg_table:
	sg_free_table(table);
free_user_pages:
	for (i = 0; i < nr_pages; i++)
		put_page(pages[i]);
free_usermem_iova_i:
	kfree(req_data_addr_i);
free_pages_list:
	kvfree(pages);

	return ret;
}

static int do_ipc_usermem_iova_free(struct ipc_session *session, struct sg_table *table)
{
	struct device *dev = session->miscdev.parent;
	struct sg_page_iter piter;

	dev_dbg(dev, "%s, dmaAddr=0x%llx, phys=0x%llx\n", __func__, sg_dma_address(table->sgl), sg_phys(table->sgl));

	dma_unmap_sgtable(dev, table, DMA_TO_DEVICE, 0);

	for_each_sgtable_page(table, &piter, 0) {
		struct page *page = sg_page_iter_page(&piter);
		put_page(page);
	}
	sg_free_table(table);

	return 0;
}

static int ipc_usermem_iova_free(struct ipc_session *session, dma_addr_t dma_addr)
{
	ipc_private_data_t *ipc_priv = session->ipc_priv;
	usermem_iova_info_t *req_data_addr_i, *tmp;
	int ret = -EINVAL;

	mutex_lock(&ipc_priv->usermem_iova_lock);
	list_for_each_entry_safe(req_data_addr_i, tmp, &ipc_priv->usermem_iova_list, node) {
		struct sg_table *table = &req_data_addr_i->table;
		if (dma_addr == sg_dma_address(table->sgl)) {
			do_ipc_usermem_iova_free(session, table);
			list_del(&req_data_addr_i->node);
			kfree(req_data_addr_i);
			ret = 0;
			pr_debug("%s, free dma_addr=0x%llx\n", __func__, dma_addr);
			break;
		}
	}
	mutex_unlock(&ipc_priv->usermem_iova_lock);

	return ret;
}

static int ipc_dmabuf_iova_alloc(struct ipc_session *session, struct dma_buf *dma_buf, u64 offset, size_t len, dma_addr_t *dma_addr_p)
{
	struct device *dev = session->miscdev.parent;
	ipc_private_data_t *ipc_priv = session->ipc_priv;
	struct heap_mem *hmem = NULL;
	struct heap_root *hp_root = &ipc_priv->hp_root;

	/* a:hold ,hold the dmabuf untill this function exit */
	get_dma_buf(dma_buf);

	/* b: add refcount since a new dmabuf_fd was created  and associated with the user process that is calling this function.
	 * user program must call close(dmabuf_fd) explicitly to decrease file_count(dambuf->file), so that the dmabuf can be finally released
	*/
	// get_dma_buf(dmabuf);

	/* the refcount will be increased by common_dmabuf_heap_import_from_user, and decrease when ipc_dmabuf_iova_free is called */
	hmem = common_dmabuf_heap_import_from_user_with_dma_buf_st(hp_root, dma_buf);
	if(IS_ERR(hmem)) {
		dev_err(dev, "dmabuf-heap alloc from userspace failed\n");
		dma_buf_put(dma_buf); // a:put, put the dmabuf back
		return -ENOMEM;
	}

	*dma_addr_p = sg_dma_address(hmem->sgt->sgl) + offset;

	dma_buf_put(dma_buf); // a:put, put the dmabuf back

	dev_dbg(dev, "%s, dma_addr=0x%llx(phys:0x%llx)\n", __func__, *dma_addr_p, sg_phys(hmem->sgt->sgl));

	return 0;
}

static int ipc_dmabuf_iova_free(struct ipc_session *session, struct dma_buf *dma_buf)
{
	struct heap_mem *hmem = NULL;
	struct heap_root *hp_root = &session->ipc_priv->hp_root;

	pr_debug("---%s:%d\n", __func__, __LINE__);
	hmem = common_dmabuf_lookup_heapobj_by_dma_buf_st(hp_root, dma_buf);
	if (IS_ERR(hmem)) {
		pr_err("cannot find dmabuf-heap for dma_buf 0x%px\n", dma_buf);
		return -EINVAL;
	}

	common_dmabuf_heap_release(hmem);

	return 0;
}
static int ipc_req_data_iova_alloc(struct ipc_session *session, cipher_mem_resource_info_t *pstCipher_mem_rsc_info)
{
	struct device *dev = session->miscdev.parent;
	struct vm_area_struct *vma = NULL;
	vm_flags_t vm_flags;
	struct mm_struct *mm = current->mm;
	u64 offset = 0;
	bool is_dmabuf = false;
	struct dma_buf *dma_buf = NULL;
	int ret = 0;
	struct dma_allocation_data *pstDma_alloc_info = &pstCipher_mem_rsc_info->k_dma_info.dma_alloc_info;
	u64 addr = (u64)pstDma_alloc_info->cpu_vaddr;
	size_t len = pstDma_alloc_info->size;
	dma_addr_t dma_addr;

	/* */
	mmap_read_lock(mm);
	vma = vma_lookup(mm, addr & PAGE_MASK);
	if (!vma) {
		dev_err(dev, "%s, vma_lookup failed!\n", __func__);
		return -EFAULT;
	}
	vm_flags = vma->vm_flags;
	mmap_read_unlock(mm);

	if (vm_flags & (VM_IO | VM_PFNMAP)) {
		dev_dbg(dev, "%s, vm_flags=0x%lx, Page-ranges managed without struct page, just pure PFN!\n", __func__, vm_flags);
		is_dmabuf = true;
	}
	dev_dbg(dev, "%s, is_dmabuf = %s. vm_start=0x%lx, vm_end=0x%lx, size=0x%lx\n", __func__,
		is_dmabuf==0?"false":"true", vma->vm_start, vma->vm_end, (vma->vm_end - vma->vm_start));

	/* check the validation of addr and len */
	if (addr + len > vma->vm_end) {
		dev_err(dev, "%s, Err,addr +len exceed the end_addr of the buffer!\n", __func__);
		return -EINVAL;
	}

	if(is_dmabuf == false) {
		ret = ipc_usermem_iova_alloc(session, addr, len, &dma_addr);
		if (ret) {
			dev_dbg(dev, "ipc_usermem_iova_alloc, failed!, ErrCode:%d\n", ret);
			return ret;
		}
	}
	else {
		dma_buf = vma->vm_private_data;
		if (NULL == dma_buf) {
			dev_err(dev, "%s, Err, Can't find dmabuf!\n", __func__);
			return -EFAULT;
		}

		offset = addr - vma->vm_start;
		ret = ipc_dmabuf_iova_alloc(session, dma_buf, offset, len, &dma_addr);
		if (ret) {
			dev_dbg(dev, "ipc_dmabuf_iova_alloc, failed!, ErrCode:%d\n", ret);
			return ret;
		}
	}

	pstCipher_mem_rsc_info->dma_buf = dma_buf;
	pstDma_alloc_info->dma_addr = dma_addr;
	return 0;
}

static int ipc_req_data_iova_free(struct ipc_session *session, cipher_mem_resource_info_t *pstCipher_mem_rsc_info)
{
	struct device *dev = session->miscdev.parent;
	dma_addr_t dma_addr = pstCipher_mem_rsc_info->k_dma_info.dma_alloc_info.dma_addr;
	struct dma_buf *dma_buf = pstCipher_mem_rsc_info->dma_buf;
	int ret = -EINVAL;

	if (!dma_buf) {
		ret = ipc_usermem_iova_free(session, dma_addr);
		if (ret) {
			dev_err(dev, "faild to free usermem iova:0x%llx, ErrCode:%d!\n", dma_addr, ret);
		}
	}
	else {
		ret = ipc_dmabuf_iova_free(session, dma_buf);
		if (ret) {
			dev_err(dev, "faild to free dmabuf iova:0x%llx, ErrCode:%d!\n", dma_addr, ret);
		}
	}

	return ret;
}

static int ipc_assign_handle_id(struct ipc_session *session, req_service_t *service_req, u32 *handle_id)
{
	int ret = 0;

	mutex_lock(&session->idr_lock);
	ret = idr_alloc(&session->handle_idr, service_req, 0, IPC_K_HANDLE_MAX_ID,
			GFP_KERNEL);
	if (ret >= 0) {
		*handle_id = ret;
	}
	mutex_unlock(&session->idr_lock);

	return ret < 0 ? ret : 0;
}

static int ipc_create_handle(struct ipc_session *session, req_service_t *in_service_req, u32 *handle_id)
{
	int ret = 0;
	struct device *dev =  session->miscdev.parent;
	req_service_t *pService_req = NULL;

	if (in_service_req == NULL)
		return -EINVAL;

	pService_req = kzalloc(sizeof(*pService_req), GFP_KERNEL);
	if (!pService_req) {
		dev_err(dev, "failed to create handle, err(%d)\n", ret);
		return -ENOMEM;
	}
	memcpy(pService_req, in_service_req, sizeof(*pService_req));

	ret = ipc_assign_handle_id(session, pService_req, handle_id);
	if (ret < 0) {
		dev_err(dev, "failed to assign handle id, err(%d)\n", ret);
		goto  free_service_req;
	}

	return 0;

free_service_req:
	kfree(pService_req);
	return ret;
}

static int ipc_find_handle(struct ipc_session *session, u32 handle_id, req_service_t **ppService_req)
{
	int ret = 0;
	struct device *dev =  session->miscdev.parent;
	req_service_t *pService_req;

	mutex_lock(&session->idr_lock);
	pService_req = idr_find(&session->handle_idr, handle_id);
	if (!pService_req) {
		dev_err(dev, "failed to find handle %d\n", handle_id);
		ret = -EINVAL;
	}
	else {
		*ppService_req = pService_req;
	}
	mutex_unlock(&session->idr_lock);

	return ret;
}

static int ipc_destroy_handle(struct ipc_session *session, u32 handle_id)
{
	int ret = 0;
	struct device *dev =  session->miscdev.parent;
	req_service_t *pService_req;
	aes_sm4_encdec_req_t *pSymm_req;

	mutex_lock(&session->idr_lock);
	pService_req = idr_find(&session->handle_idr, handle_id);
	if (!pService_req) {
		dev_err(dev, "failed to find handle %d\n", handle_id);
		ret = -EINVAL;
		goto out;
	}

	pSymm_req = &pService_req->data.symm_crypto_req;
	dev_dbg(dev, "pSymm_req->crypto_mode=%d\n", pSymm_req->flag.crypto_mode);
	dev_dbg(dev, "pSymm_req->keySrc=%d\n", pSymm_req->flag.keyid);
	dev_dbg(dev, "pSymm_req->key_size=%d\n", pSymm_req->key_size);
	dev_dbg(dev, "pSymm_req->iv_size=%d\n", pSymm_req->iv_size);

	idr_remove(&session->handle_idr, handle_id);
	mutex_unlock(&session->idr_lock);

	kfree(pService_req);
	return 0;

out:
	mutex_unlock(&session->idr_lock);

	return ret;
}

static int ipc_ioctl_create_handle(process_data_list_t *pstProc_data_list, unsigned int cmd, void __user *user_arg)
{
	struct ipc_session *session = pstProc_data_list->session;
	struct device *dev = session->miscdev.parent;
	unsigned int cmd_size = 0;
	// cipher_create_handle_req_t create_handle_req;
	cipher_create_handle_req_t *pstCreate_handle_req;
	req_service_t *pService_req;
	u32 handle_id;
	int ret = 0;

	// pr_debug("---%s:%d---\n", __func__, __LINE__);
	cmd_size = _IOC_SIZE(cmd);
	if (cmd_size != sizeof(cipher_create_handle_req_t)) {
		return -EFAULT;
	}

	pstCreate_handle_req = kzalloc(sizeof(*pstCreate_handle_req), GFP_ATOMIC); // GFP_KERNEL
	if (!pstCreate_handle_req) {
		return -ENOMEM;
	}

	if (copy_from_user(pstCreate_handle_req, user_arg, sizeof(cipher_create_handle_req_t))) {
		ret = -EFAULT;
		goto OUT_FREE;
	}

	pService_req = &pstCreate_handle_req->service_req;
	ret = ipc_create_handle(session, pService_req, &handle_id);
	if (ret < 0) {
		goto OUT_FREE;
	}

	pstCreate_handle_req->handle_id = handle_id;

	if (copy_to_user(user_arg, pstCreate_handle_req, sizeof(cipher_create_handle_req_t))) {
		ipc_destroy_handle(session, handle_id);
		ret = -EFAULT;
	}

OUT_FREE:
	kfree(pstCreate_handle_req);

	dev_dbg(dev, "%s, ret=%d!\n", __func__, ret);
	return ret;
}

static int ipc_ioc_destroy_handle(process_data_list_t *pstProc_data_list, unsigned int cmd, void __user *user_arg)
{
	struct ipc_session *session = pstProc_data_list->session;
	struct device *dev = session->miscdev.parent;
	unsigned int cmd_size = 0;
	cipher_create_handle_req_t *pstCreate_handle_req;
	int ret = 0;

	// pr_debug("---%s:%d---\n", __func__, __LINE__);
	cmd_size = _IOC_SIZE(cmd);
	if (cmd_size != sizeof(cipher_create_handle_req_t)) {
		return -EFAULT;
	}

	pstCreate_handle_req = kzalloc(sizeof(*pstCreate_handle_req), GFP_ATOMIC); // GFP_KERNEL
	if (!pstCreate_handle_req) {
		return -ENOMEM;
	}

	if (copy_from_user(pstCreate_handle_req, user_arg, sizeof(cipher_create_handle_req_t))) {
		ret = -EFAULT;
		goto OUT_FREE;
	}

	ret = ipc_destroy_handle(session, pstCreate_handle_req->handle_id);
	if (ret < 0) {
		goto OUT_FREE;
	}

OUT_FREE:
	kfree(pstCreate_handle_req);
	dev_dbg(dev, "%s, ret=%d!\n", __func__, ret);

	return ret;
}

static int ipc_ioc_get_handle_config(process_data_list_t *pstProc_data_list, unsigned int cmd, void __user *user_arg)
{
	struct ipc_session *session = pstProc_data_list->session;
	struct device *dev = session->miscdev.parent;
	unsigned int cmd_size = 0;
	cipher_create_handle_req_t *pstCreate_handle_req;
	req_service_t *pService_req;
	int ret = 0;

	// pr_debug("---%s:%d---\n", __func__, __LINE__);
	cmd_size = _IOC_SIZE(cmd);
	if (cmd_size != sizeof(cipher_create_handle_req_t)) {
		return -EFAULT;
	}

	pstCreate_handle_req = kzalloc(sizeof(*pstCreate_handle_req), GFP_ATOMIC); // GFP_KERNEL
	if (!pstCreate_handle_req) {
		return -ENOMEM;
	}

	if (copy_from_user(pstCreate_handle_req, user_arg, sizeof(cipher_create_handle_req_t))) {
		ret = -EFAULT;
		goto OUT_FREE;
	}

	ret = ipc_find_handle(session, pstCreate_handle_req->handle_id, &pService_req);
	if (ret < 0) {
		return ret;
	}
	memcpy(&pstCreate_handle_req->service_req, pService_req, sizeof(*pService_req));
	if (copy_to_user(user_arg, pstCreate_handle_req, sizeof(cipher_create_handle_req_t))) {
		ret = -EFAULT;
		goto OUT_FREE;
	}

OUT_FREE:
	kfree(pstCreate_handle_req);
	dev_dbg(dev, "%s, ret=%d!\n", __func__, ret);

	return ret;
}

static int ipc_ioc_update_handle_config(process_data_list_t *pstProc_data_list, unsigned int cmd, void __user *user_arg)
{
	struct ipc_session *session = pstProc_data_list->session;
	struct device *dev = session->miscdev.parent;
	unsigned int cmd_size = 0;
	cipher_create_handle_req_t *pstCreate_handle_req;
	req_service_t *pService_req;
	int ret = 0;

	// pr_debug("---%s:%d---\n", __func__, __LINE__);
	cmd_size = _IOC_SIZE(cmd);
	if (cmd_size != sizeof(cipher_create_handle_req_t)) {
		return -EFAULT;
	}

	pstCreate_handle_req = kzalloc(sizeof(*pstCreate_handle_req), GFP_ATOMIC); // GFP_KERNEL
	if (!pstCreate_handle_req) {
		return -ENOMEM;
	}

	if (copy_from_user(pstCreate_handle_req, user_arg, sizeof(cipher_create_handle_req_t))) {
		ret = -EFAULT;
		goto OUT_FREE;
	}

	ret = ipc_find_handle(session, pstCreate_handle_req->handle_id, &pService_req);
	if (ret < 0) {
		goto OUT_FREE;
	}
	/* update this idr with the user configuration */
	memcpy(pService_req, &pstCreate_handle_req->service_req, sizeof(*pService_req));

OUT_FREE:
	kfree(pstCreate_handle_req);
	dev_dbg(dev, "%s, ret=%d!\n", __func__, ret);

	return ret;
}

static char check_xor(char *buf, int size)
{
    char tmp = 0;
    int i = 0;

    for (i = 0; i < size; i++)
        tmp ^= buf[i];

    return tmp;
}


static int encode_ecdh_key_req(ecdh_key_req_t *in, u8 *out)
{
	int len = 0;
	u32 keylen = 0;

	if (in->flag.opt == OPT_GEN) {
		len = sizeof(in->flag) + sizeof(ecdh_key_gen_t);
		memcpy(out, (u8 *)in, len);
	} else if (in->flag.opt == OPT_AGR) {
		len = sizeof(in->flag) + sizeof(u32);
		memcpy(out, (u8 *)in, len);

		keylen = in->data.agr.pubk_remote.keylen;
		memcpy(out + len, (u8 *)&in->data.agr.pubk_remote.keylen,
		       sizeof(u32));
		len += sizeof(u32);

		memcpy(out + len, (u8 *)in->data.agr.pubk_remote.x, keylen);
		len += keylen;

		memcpy(out + len, (u8 *)in->data.agr.pubk_remote.y, keylen);
		len += keylen;

		keylen = in->data.agr.privk_local.keylen;
		memcpy(out + len, (u8 *)&in->data.agr.privk_local.keylen,
		       sizeof(u32));
		len += sizeof(u32);

		memcpy(out + len, (u8 *)in->data.agr.privk_local.z, keylen);
		len += keylen;
	} else if (in->flag.opt == OPT_DRIV) {
		len = sizeof(in->flag);
		memcpy(out, (u8 *)in, len);

		keylen = in->data.driv.seck.keylen;
		memcpy(out + len, (u8 *)&in->data.driv.seck.keylen,
		       sizeof(u32));
		len += sizeof(u32);

		memcpy(out + len, (u8 *)in->data.driv.seck.x, keylen);
		len += keylen;

		memcpy(out + len, (u8 *)in->data.driv.seck.y, keylen);
		len += keylen;

		memcpy(out + len, (u8 *)&in->data.driv.rand, 4);
		len += 4;
	} else {
		len = 0;
	}

	return len;
}

static int encode_rsa_req(rsa_encdec_req_t *in, u8 *out)
{
	int len;
	int key_len = 0;

	if (in->flag.keyid < EXT_PRIV_KEY)
		key_len = 0;
	else if (in->flag.keyid == EXT_PRIV_KEY) {
		key_len = sizeof(in->t_key.rsa_prvkey.keylen) +
			  2 * in->t_key.rsa_prvkey.keylen;
	} else {
		key_len = sizeof(in->t_key.rsa2048_pubkey.keylen) +
			  sizeof(in->t_key.rsa2048_pubkey.exponent) +
			  in->t_key.rsa2048_pubkey.keylen;
	}

	len = 20 + key_len;
	memcpy(out, (u8 *)in, len);

	return len;
}

static int encode_hmac_req(hmac_req_t *in, u8 *out)
{
	int len;
	len = 16 + in->key_size;
	memcpy(out, (u8 *)in, len);

	return len;
}

static int encode_symm_req(aes_sm4_encdec_req_t *in, u8 *out)
{
	int len;

	len = 20;
	memcpy(out, (u8 *)in, len);
	memcpy(out + len, in->key, in->key_size);
	len += in->key_size;
	memcpy(out + len, &in->iv_size, 4);
	len += 4;
	memcpy(out + len, in->iv, in->iv_size);
	len += in->iv_size;

	return len;
}

static int encode_loadable_req(downloadable_init_req_t *in, u8 *out)
{
	int len;
	flag14_t *flag = (flag14_t *)in;
	if (flag->service_type == 0) {
		len = sizeof(downloadable_init_req_t);
	} else if (flag->service_type == 1) {
		len = sizeof(downloadable_destory_req_t);
	} else if (flag->service_type == 2) {
		downloadable_ioctl_req_t *ioctl_req =
			(downloadable_ioctl_req_t *)in;
		len = sizeof(downloadable_ioctl_req_t) + ioctl_req->param_size;
	}

	memcpy((void *)out, (void *)in, len);
	return len;
}

static void u32_to_u8(u8 *u8_data, u32 *u32_data)
{
	u32 tmp;
	tmp = *u32_data;
	*u8_data = (u8)(0x000000ff & tmp);
	*(u8_data + 1) = (u8)((0x0000ff00 & tmp) >> 8);
	*(u8_data + 2) = (u8)((0x00ff0000 & tmp) >> 16);
	*(u8_data + 3) = (u8)((0xff000000 & tmp) >> 24);
}

static int encode_req_data(u8 *out, SRVC_TYPE service_id, req_data_domain_t *in)
{
	int len = 0;
	if (NULL == out || NULL == in)
		return len;

	switch (service_id) {
	case SRVC_TYPE_SIGN_CHECK:
	case SRVC_TYPE_IMG_DECRPT:
	case SRVC_TYPE_FIRMWARE_DOWNLOAD:
	case SRVC_TYPE_TRNG:
	case SRVC_TYPE_DIGEST:
	case SRVC_TYPE_ADDR_REGION_PROTECTION:
	case SRVC_TYPE_BASIC_IO:
	case SRVC_TYPE_AXPROT:
	case SRVC_TYPE_PUBKEY_DOWNLOAD:
	case SRVC_TYPE_OTP_READ_PROGRAM:
		len = req_size[service_id];
		memcpy((void *)out, (void *)in, len);
		break;
	case SRVC_TYPE_ECDH_KEY:
		len = encode_ecdh_key_req(&(in->ecdh_key_req), out);
		break;
	case SRVC_TYPE_SYM_CRYPT_DECRYPT:
		len = encode_symm_req(&(in->symm_crypto_req), out);
		break;
	case SRVC_TYPE_RSA_CRYPT_DECRYPT:
		len = encode_rsa_req(&(in->rsa_crypto_req), out);
		break;
	case SRVC_TYPE_HMAC:
		len = encode_hmac_req(&(in->hmac_req), out);
		break;
	case SRVC_TYPE_DOWNLOADABLE:
		len = encode_loadable_req(&(in->dl_init_req), out);
		break;
	default:
		break;
	}
	return len;
}

static int get_req_data_size(u8 service_id, u8 *req_data)
{
	int size = 0;
	unsigned int keylen = 0;
	switch (service_id) {
	case SRVC_TYPE_SIGN_CHECK:
	case SRVC_TYPE_IMG_DECRPT:
	case SRVC_TYPE_FIRMWARE_DOWNLOAD:
	case SRVC_TYPE_TRNG:
	case SRVC_TYPE_ADDR_REGION_PROTECTION:
	case SRVC_TYPE_BASIC_IO:
	case SRVC_TYPE_AXPROT:
	case SRVC_TYPE_PUBKEY_DOWNLOAD:
	case SRVC_TYPE_DIGEST:
	case SRVC_TYPE_OTP_READ_PROGRAM:
		size = req_size[service_id];
		break;
	case SRVC_TYPE_DOWNLOADABLE: {
		flag14_t *flag = (flag14_t *)req_data;
		if (flag->service_type == 0) {
			size = req_size[service_id];
		} else if (flag->service_type == 1) {
			size = sizeof(downloadable_destory_req_t);
		} else if (flag->service_type == 2) {
			downloadable_ioctl_req_t *ioctl_req =
				(downloadable_ioctl_req_t *)req_data;
			size = sizeof(downloadable_ioctl_req_t) +
			       ioctl_req->param_size;
		}
	} break;

	case SRVC_TYPE_HMAC: {
		hmac_req_t *h_req;
		h_req = (hmac_req_t *)req_data;
		size = 16 + h_req->key_size;
	} break;

	case SRVC_TYPE_RSA_CRYPT_DECRYPT: {
		rsa_encdec_req_t *rsa_req;
		int key_len = 0;
		rsa_req = (rsa_encdec_req_t *)req_data;
		if (rsa_req->flag.keyid < EXT_PRIV_KEY) {
			key_len = 0;
		} else if (rsa_req->flag.keyid == EXT_PRIV_KEY) {
			key_len = sizeof(rsa_req->t_key.rsa_prvkey.keylen) +
				  2 * rsa_req->t_key.rsa_prvkey.keylen;
		} else {
			key_len = sizeof(rsa_req->t_key.rsa2048_pubkey.keylen) +
				  sizeof(rsa_req->t_key.rsa2048_pubkey.exponent) +
				  rsa_req->t_key.rsa2048_pubkey.keylen;
		}
		size = 20 + key_len;
	} break;

	case SRVC_TYPE_ECDH_KEY: {
		ecdh_key_req_t *in = (ecdh_key_req_t *)req_data;
		if (in->flag.opt == OPT_GEN) {
			size = sizeof(in->flag) + sizeof(ecdh_key_gen_t);
		} else if (in->flag.opt == OPT_AGR) {
			size = sizeof(in->flag) + sizeof(u32);
			keylen = in->data.agr.pubk_remote.keylen;
			size += 4 + 2 * keylen;
			keylen = in->data.agr.privk_local.keylen;
			size += 4 + keylen;
		} else if (in->flag.opt == OPT_DRIV) {
			size = sizeof(in->flag);
			keylen = in->data.driv.seck.keylen;
			size += sizeof(u32) + 2 * keylen;
			size += 4;
		} else {
			size = 0;
		}
	} break;

	case SRVC_TYPE_SYM_CRYPT_DECRYPT: {
		aes_sm4_encdec_req_t *in = (aes_sm4_encdec_req_t *)req_data;
		size = 24 + in->key_size + in->iv_size;
	} break;

	default:
		break;
	}

	pr_debug("get_req_data_size: SRVC_TYPE[%d] size = [0x%x]!\r\n",
	       service_id, size);
	if (0 != (size % 4)) {
		if ((SRVC_TYPE_OTP_READ_PROGRAM != service_id) && (SRVC_TYPE_ECDH_KEY != service_id)) {
			pr_err("get_req_data_size: SRVC_TYPE[%d] size = [0x%x], not 4 "
			       "times, error!\r\n",
			       service_id, size);
			return 0;
		}
	}

	return size;
}

static inline u32 get_union32_data(u8 *in, u32 offset)
{
	u32 ret = 0;
	ret = __UNION32(*(in + offset + 3), *(in + offset + 2),
			*(in + offset + 1), *(in + offset));
	return ret;
}

static int get_receive_message_size(struct ipc_session *session, u8 *res_msg)
{
	int size = 0;
	res_service_t *tmp;
	tmp = (res_service_t *)res_msg;

	#if LOG_PRINT_DATA_DEBUG_EN
	int i = 0;

	pr_info("response data is : \r\n");
	for (i = 0; i < (tmp->size + 20); i++) {
		pr_info("0x%02x ! \r\n", res_msg[i]);
	}
	#endif

	switch (tmp->service_id) {
	case SRVC_TYPE_SIGN_CHECK:
		size = sizeof(signature_validation_check_res_t);
		break;

	case SRVC_TYPE_IMG_DECRPT:
		size = sizeof(image_decryption_res_t);
		break;

	case SRVC_TYPE_FIRMWARE_DOWNLOAD:
		size = sizeof(firmware_download_res_t);
		break;

	case SRVC_TYPE_TRNG:
		size = MAX_TRNG_DATA_LEN;
		break;

	case SRVC_TYPE_DIGEST:
		size = sizeof(sha1_sha256_sm3_digest_res_t);
		break;

	case SRVC_TYPE_ADDR_REGION_PROTECTION:
		size = sizeof(addr_region_protection_res_t);
		break;

	case SRVC_TYPE_DOWNLOADABLE:
		size = sizeof(downloadable_srvc_res);
		break;

	case SRVC_TYPE_BASIC_IO:
		size = sizeof(basicio_res_t);
		break;

	case SRVC_TYPE_AXPROT:
		size = sizeof(axprot_res_t);
		break;

	case SRVC_TYPE_SYM_CRYPT_DECRYPT:
		size = sizeof(aes_sm4_encdec_res_t);
		break;

	case SRVC_TYPE_OTP_READ_PROGRAM:
		size = sizeof(otp_res_t);
		break;

	case SRVC_TYPE_RSA_CRYPT_DECRYPT:
		size = sizeof(rsa_encdec_res_t);
		break;

	case SRVC_TYPE_HMAC:
		size = sizeof(hmac_res_t);
		break;

	case SRVC_TYPE_ECDH_KEY:
		size = sizeof(ecdh_key_res_t);
		break;

	default:
		break;
	}
	size = size + RES_DATA_OFFSET;
	memcpy(&session->res_srvc, res_msg, size);
	return size;
}

#if LOG_PRINT_DATA_DEBUG_EN
static void msg_print(msg_send_t *msg)
{
	int i = 0;
	pr_info("req num is: %02x.\r\n", msg->num);
	pr_info("req service_id is: %02x.\r\n", msg->service_id);
	pr_info("req size is: %02x.\r\n", msg->size);
	pr_info("packet data is:\r\n");
	for (i = 0; i < msg->size; i++) {
		pr_info("0x%02X ", *((char *)msg->data + i));
		if (((i + 1) & 0xf) == 0)
			pr_info("\r\n");
	}
}
#else
#define msg_print(msg)
#endif

static int encode_send_buffer(u8 *buffer_addr, msg_send_t *msg)
{
	int ret = 0;
	u8 tmp;
	int i, len;

	if (NULL == msg->data)
		return -EAGAIN;

	memcpy(buffer_addr + 0, (void *)&msg->fream_head, 4);

	u32_to_u8(buffer_addr + 4, &msg->num);
	u32_to_u8(buffer_addr + 8, &msg->service_id);
	u32_to_u8(buffer_addr + 12, &msg->size);
	len = msg->size;

	memcpy(buffer_addr + 16, msg->data, len);
	tmp = *buffer_addr;
	len = 16 + len;

	for (i = 1; i < len; i++)
		tmp = tmp ^ (*(buffer_addr + i));

	*(buffer_addr + i) = tmp;

	return ret;
}

static void fill_and_slice_regdata(mbox_register_data *reg_data,
				   dma_addr_t shm_dma_addr, u8 *out)
{
	u8 xor ;
	int i;
	int len;
	u8 *tmp;

	memset(reg_data, 0, sizeof(mbox_register_data));
	memset(out, 0, 8);

	reg_data->fream_head = FRAME_HEAD_U84;
	reg_data->fream_len = (u8)0x04;
	reg_data->ipc_addr[0] = (u8)(0x000000ff & shm_dma_addr);
	reg_data->ipc_addr[1] = (u8)((0x0000ff00 & shm_dma_addr) >> 8);
	reg_data->ipc_addr[2] = (u8)((0x00ff0000 & shm_dma_addr) >> 16);
	reg_data->ipc_addr[3] = (u8)((0xff000000 & shm_dma_addr) >> 24);

	len = sizeof(mbox_register_data) - 1;
	tmp = (u8 *)reg_data;
	xor = *(tmp + 0);
	for (i = 1; i < len; i++)
		xor = xor^(*(tmp + i));
	*(tmp + i) = xor;

	for (i = 0; i < 7; i++)
		*(out + i) = *(tmp + i);
	*(out + i) = 0x80;

	return;
}

static int get_shm_addr(struct ipc_session *session)
{
	int ret = 0;
	struct device *dev = session->miscdev.parent;
	session->send_buff.size = sizeof(req_data_domain_t);
	session->send_buff.cpu_vaddr =
		dma_alloc_coherent(dev, session->send_buff.size,
				   &session->send_buff.dma_addr, GFP_KERNEL);
	if (NULL == session->send_buff.cpu_vaddr) {
		dev_err(dev, "dma_alloc_coherent failed\n");
		ret = -ENOMEM;
	}

	return ret;
}

static void free_shm_addr(struct ipc_session *session)
{
	struct device *dev = session->miscdev.parent;

	dma_free_coherent(dev, session->send_buff.size,
			  session->send_buff.cpu_vaddr,
			  session->send_buff.dma_addr);
}

static int get_send_reg_data(u8 *in, u8 *out, struct ipc_session *session)
{
	int ret, len;
	#if LOG_PRINT_DATA_DEBUG_EN
	int k;
	#endif
	struct dma_allocation_data tmp;
	msg_send_t msg_send;
	req_service_t *service_req = (req_service_t *)in;
	mbox_register_data reg_data;
	struct device *dev = session->miscdev.parent;

	session->num++;
	msg_send.fream_head = PREAM_REQ;
	msg_send.num = session->num;
	msg_send.service_id = service_req->serivce_type;
	msg_send.size = get_req_data_size(msg_send.service_id,
					  (u8 *)(in + SERVICE_ID_OFFSET));

	dev_dbg(dev, "msg_send.size:0x%d.\r\n", msg_send.size);
	if (0 >= msg_send.size) {
		dev_err(dev, "msg_send.size is error.\r\n");
		return -EINVAL;
	}

	tmp.cpu_vaddr = kzalloc(msg_send.size, GFP_KERNEL);
	if (NULL == tmp.cpu_vaddr) {
		dev_err(dev, "kzalloc failed\n");
		return -ENOMEM;
	}

	msg_send.data = (u8 *)tmp.cpu_vaddr;
	len = encode_req_data(msg_send.data, msg_send.service_id,
			      (req_data_domain_t *)(in + SERVICE_ID_OFFSET));
	if (len != msg_send.size) {
		dev_err(dev, "encode_req_data error!\r\n");
		ret = -EAGAIN;
		goto out;
	}

	msg_print(&msg_send);

	ret = encode_send_buffer(session->send_buff.cpu_vaddr, &msg_send);
	if (ret < 0) {
		dev_err(dev, "encode_send_buffer err!\r\n");
		ret = -EAGAIN;
		goto out;
	}

	fill_and_slice_regdata(&reg_data, session->send_buff.dma_addr, out);

	/*TODO: delete when release*/
	#if LOG_PRINT_DATA_DEBUG_EN
	len = 16 + msg_send.size + 1; // 1-xor byte
	pr_debug("win2030_umbox_send: umbox_send_data is:\r\n");
	for (k = 0; k < len; k++) {
		pr_debug("0x%02x ",
		       *((u8 *)(session->send_buff.cpu_vaddr) + k));
		if ((k + 1) % 8 == 0) {
			pr_debug("\r\n");
		}
	}
	#endif

out:
	kfree(tmp.cpu_vaddr);

	return ret;
}

static void eswin_ipc_tx_done(struct mbox_client *client, void *msg, int r)
{
	if (r)
		dev_warn(client->dev, "Client: Message could not be sent:%d\n",
			 r);
	else
		dev_dbg(client->dev, "Client: Message sent\n");
}

static void eswin_ipc_rx_callback(struct mbox_client *client, void *msg)
{
	struct ipc_session *session = container_of(client, struct ipc_session, client);
	struct device *dev = session->miscdev.parent;
	res_service_t *pstRes_srvc = &session->res_srvc;
	u8 reg_data[8];
	u8 *res_msg = (u8 *)session->send_buff.cpu_vaddr;
	u32 dma_addr;
	u32 fream_head;
	u32 i = 0;
	int frame_len=0;

	for (i = 0; i < 8; i++)
		reg_data[i] = *((u8 *)msg + i);

	dma_addr =
		__UNION32(reg_data[1], reg_data[2], reg_data[3], reg_data[4]);
	if (dma_addr != session->send_buff.dma_addr) {
		dev_err(dev, "The received dma_addr is bad \n");
		goto out;
	}

	fream_head = get_union32_data(res_msg, 0);
	if (PREAM_RSP == fream_head) {
		frame_len = IPC_RES_FIXED_SIZE + \
			*((u32 *)res_msg + IPC_RES_DATA_LEN_OFFSET);
		if (check_xor(res_msg,frame_len))
		{
			pstRes_srvc->num = IPC_RES_XOR_ERR;
			pstRes_srvc->service_id = IPC_RES_XOR_ERR;
			pstRes_srvc->ipc_status = IPC_RES_XOR_ERR;
			pstRes_srvc->service_status = IPC_RES_XOR_ERR;
			pstRes_srvc->size = IPC_RES_XOR_ERR;
			session->res_size = RES_DATA_OFFSET;
			dev_err(dev, "check_xor err \n");
			goto out;
		}
		session->res_size =
			get_receive_message_size(session, res_msg + PREAM_RSP_LEN);
	} else {
		pstRes_srvc->num = IPC_RES_HEADER_ERR;
		pstRes_srvc->service_id = IPC_RES_HEADER_ERR;
		pstRes_srvc->ipc_status = IPC_RES_HEADER_ERR;
		pstRes_srvc->service_status = IPC_RES_HEADER_ERR;
		pstRes_srvc->size = IPC_RES_HEADER_ERR;
		session->res_size = RES_DATA_OFFSET;
		dev_err(dev, "The response header is invalid \n");
		goto out;
	}

out:
	/* put the cipher_mem_rsc_ifo */
	pr_debug("%s:session->num=%d\n", __func__, session->res_srvc.num);
	queue_work(session->work_q, &session->session_work);

	return;
}

static int ipc_add_proc_priv_data_to_list(process_data_list_t *pstProc_data_list, int id)
{
	process_data_t *proc_data = NULL;

	proc_data = kzalloc(sizeof(*proc_data), GFP_ATOMIC);
	if (!proc_data)
		return -ENOMEM;

	proc_data->id = id;

	mutex_lock(&pstProc_data_list->lock);
	list_add_tail(&proc_data->node, &pstProc_data_list->list);
	mutex_unlock(&pstProc_data_list->lock);

	return 0;
}

static void ipc_del_proc_priv_data_from_list(process_data_list_t *pstProc_data_list, int id)
{
	process_data_t *proc_data, *tmp_proc_data;

	mutex_lock(&pstProc_data_list->lock);
	list_for_each_entry_safe(proc_data, tmp_proc_data, &pstProc_data_list->list, node) {
		if (id == proc_data->id) {
			list_del(&proc_data->node);
			kfree(proc_data);
		}
	}
	mutex_unlock(&pstProc_data_list->lock);
}

static process_data_list_t *ipc_proc_data_list_init(void)
{
	process_data_list_t *pstProc_data_list = NULL;

	pstProc_data_list = kzalloc(sizeof(*pstProc_data_list), GFP_ATOMIC);
	if (!pstProc_data_list)
		return NULL;

	mutex_init(&pstProc_data_list->lock);
	INIT_LIST_HEAD(&pstProc_data_list->list);

	return pstProc_data_list;
}

static void ipc_proc_data_list_destroy(process_data_list_t *pstProc_data_list)
{
	struct ipc_session *session = pstProc_data_list->session;
	cipher_mem_resource_info_t *pstCipher_mem_rsc_info = NULL;
	process_data_t *proc_data, *tmp_proc_data;

	mutex_lock(&pstProc_data_list->lock);
	list_for_each_entry_safe(proc_data, tmp_proc_data, &pstProc_data_list->list, node) {
		pr_debug("%s:id=%d\n", __func__, proc_data->id);
		pstCipher_mem_rsc_info = ipc_find_cipher_mem_rsc_info(session, proc_data->id);
		if (pstCipher_mem_rsc_info) {
			kref_put(&pstCipher_mem_rsc_info->refcount, ipc_release_mem_khandle_fn);
		}
		list_del(&proc_data->node);
		kfree(proc_data);
	}
	mutex_unlock(&pstProc_data_list->lock);

	kfree(pstProc_data_list);
}

static int eswin_ipc_open(struct inode *inode, struct file *filp)
{
	int ret = 0;
	struct miscdevice *mdev = filp->private_data;
	struct ipc_session *session = container_of(mdev, struct ipc_session, miscdev);
	process_data_list_t *pstProc_data_list = NULL;

	pstProc_data_list = ipc_proc_data_list_init();
	if (!pstProc_data_list)
		return -ENOMEM;

	pstProc_data_list->session = session;
	filp->private_data = pstProc_data_list;

	return ret;
}

static int eswin_receive_data_ready(struct ipc_session *session)
{
	int data_ready;
	data_ready = atomic_read(&session->receive_data_ready);
	return data_ready;
}

static int eswin_ipc_service_ready(struct ipc_session *session)
{
	int data_ready;
	data_ready = atomic_read(&session->ipc_service_ready);
	return data_ready;
}

static int eswin_ipc_release(struct inode *inode, struct file *filp)
{
	process_data_list_t *pstProc_data_list = filp->private_data;

	pr_debug("----%s:pid[%d] called!\n", __func__, task_pid_nr(current));

	ipc_proc_data_list_destroy(pstProc_data_list);
	filp->private_data = NULL;

	return 0;
}

static int do_ipc_msg_tx(struct ipc_session *session, req_service_t *pService_req)
{
	struct device *dev = session->miscdev.parent;
	u8 reg_data[8];
	int ret;

	WARN_ON(!mutex_is_locked(&session->lock));

	#ifndef ES_CIPHER_QEMU_DEBUG
	if (!session->mbox_channel) {
		dev_err(dev, "Channel cannot do Tx\n");
		return -EINVAL;
	}
	#endif

	ret = get_send_reg_data((u8 *)pService_req, reg_data, session);
	if (ret < 0) {
		ret = -EAGAIN;
		goto OUT;
	}
	dev_dbg(dev,"pid[%d]:session->num:%d\n", task_pid_nr(current), session->num);
	atomic_set(&session->receive_data_ready, false);
	#ifndef ES_CIPHER_QEMU_DEBUG
	ret = mbox_send_message(session->mbox_channel, reg_data);
	if (ret < 0){
		ret = -EAGAIN;
		dev_err(dev, "Failed to send message via mailbox\r\n");
		atomic_set(&session->receive_data_ready, true);
		goto OUT;
	}

	#else
	session->res_srvc.service_id = pService_req->serivce_type;
	session->res_srvc.service_status = 1; // For QEMU simulation ,alwasy be false
	session->res_size = sizeof(res_data_domain_t) + RES_DATA_OFFSET;
	session->res_srvc.data_t.ecdh_key_res.ecdh_key.keylen = PKA_CURVE_BRAINPOOL_P256R1_BYTE;
	queue_delayed_work(session->work_q, &session->d_session_work, msecs_to_jiffies(5000));
	#endif

	ret = wait_event_interruptible_timeout(session->waitq,
					 eswin_receive_data_ready(session),
					 MAX_RX_TIMEOUT);

OUT:

	return ret;
}

static int ipc_session_mem_info_get(struct ipc_session *session, cipher_create_handle_req_t *pstCreate_handle_req)
{
	struct device *dev = session->miscdev.parent;
	cipher_mem_resource_info_t *pstCipher_mem_rsc_infos[MAX_NUM_K_DMA_ALLOC_INFO] = {NULL};
	khandle_dma_allocation_data_t *pstK_dma_info = NULL;
	__u32 kinfo_cnt = pstCreate_handle_req->kinfo_cnt;
	int i, j;

	WARN_ON(!mutex_is_locked(&session->lock));
	/* clear session khandle info */
	session->kinfo_cnt = 0;
	for (i = 0; i < MAX_NUM_K_DMA_ALLOC_INFO; i++)
		session->kinfo_id[i] = -1;

	pr_debug("%s:kinfo_cnt=%d\n", __func__, kinfo_cnt);
	/* find the pstCipher_mem_rsc_infos by id */
	for (i = 0; i < kinfo_cnt; i++) {
		pstK_dma_info = &pstCreate_handle_req->k_dma_infos[i];
		pstCipher_mem_rsc_infos[i] = ipc_find_cipher_mem_rsc_info(session, pstK_dma_info->id);
		if (pstCipher_mem_rsc_infos[i] == NULL) 
		{
			dev_err(dev, "Failed to find cipher_mem_rsc_info by id(%d)\n", pstK_dma_info->id);
			for (j = 0; j < i; j++) {
				session->kinfo_id[j] = -1;
				kref_put(&pstCipher_mem_rsc_infos[j]->refcount, ipc_release_mem_khandle_fn);
			}
			return -EFAULT;
		}
		session->kinfo_id[i] = pstK_dma_info->id;;
		kref_get(&pstCipher_mem_rsc_infos[i]->refcount);
		pr_debug("%s:id[%d]=%d\n", __func__, i, session->kinfo_id[i]);
	}

	session->kinfo_cnt = pstCreate_handle_req->kinfo_cnt;

	return 0;
}

static void ipc_session_mem_info_put(struct ipc_session *session)
{
	cipher_mem_resource_info_t *pstCipher_mem_rsc_info = NULL;
	__u32 kinfo_cnt = session->kinfo_cnt;
	int i;

	pr_debug("%s:kinfo_cnt=%d\n", __func__, kinfo_cnt);
	for (i = 0; i < kinfo_cnt; i++) {
		pr_debug("%s:id[%d]=%d\n", __func__, i, session->kinfo_id[i]);
		pstCipher_mem_rsc_info = ipc_find_cipher_mem_rsc_info(session, session->kinfo_id[i]);
		if (pstCipher_mem_rsc_info) {
			kref_put(&pstCipher_mem_rsc_info->refcount, ipc_release_mem_khandle_fn);
		}
		session->kinfo_id[i] = -1;
	}
	session->kinfo_cnt = 0;
}

static int ipc_ioctl_msg_commu(process_data_list_t *pstProc_data_list, unsigned int cmd, void __user *user_arg)
{
	struct ipc_session *session = pstProc_data_list->session;
	struct device *dev = session->miscdev.parent;
	int ret = 0;
	unsigned int cmd_size = 0;
	// cipher_create_handle_req_t stCreate_handle_req;
	cipher_create_handle_req_t *pstCreate_handle_req;// = &stCreate_handle_req;
	req_service_t *pService_req = NULL;
	unsigned long time;

	pr_debug("---%s:%d,pid[%d]---\n",
		__func__, __LINE__, task_pid_nr(current));
	cmd_size = _IOC_SIZE(cmd);
	if (cmd_size != sizeof(cipher_create_handle_req_t)) {
		return -EFAULT;
	}

	pstCreate_handle_req = kzalloc(sizeof(*pstCreate_handle_req), GFP_ATOMIC); // GFP_KERNEL
	if (!pstCreate_handle_req) {
		return -ENOMEM;
	}

	if (copy_from_user(pstCreate_handle_req, user_arg, sizeof(cipher_create_handle_req_t))) {
		ret = -EFAULT;
		goto OUT_FREE;
	}
	pService_req = &pstCreate_handle_req->service_req;

	/* wait for the service to be ready */
	time = jiffies;
	while (mutex_trylock(&session->lock) == 0) {
		if (msleep_interruptible(10)) {
			ret = -ERESTARTSYS; // interrupted by a signal from user-space
			goto OUT_FREE;
		}
		if (time_after(jiffies, time + MAX_RX_TIMEOUT)) {
			pr_err("Time out waiting for mutex be released!\n");
			ret = -ETIMEDOUT;
			goto OUT_FREE;
		}
	}
	/* In the case that previous session was interrupted by signal from user-space,
	   the mutex was unlocked by previous process, but service is still runnning for
	   the previous process. So, current process needs to wait again.
	*/
	while (false == eswin_receive_data_ready(session)) {
		if (msleep_interruptible(10)) {
			ret = -ERESTARTSYS; // interrupted by a signal from user-space
			mutex_unlock(&session->lock);
			pr_debug("%s,pid[%d] was cancled by user!\n", __func__, task_pid_nr(current));
			goto OUT_FREE;
		}
		if (time_after(jiffies, time + MAX_RX_TIMEOUT)) {
			pr_err("%s, pid[%d] Time out waiting for Cipher Service be ready!\n",
				__func__, task_pid_nr(current));
			ret = -ETIMEDOUT;
			mutex_unlock(&session->lock);
			goto OUT_FREE;
		}
	}

	pr_debug("---%s:pid[%d],SRVC_TYPE=%d get the session!\n",
		__func__, task_pid_nr(current), pService_req->serivce_type);
	/* find the mem src info by id, and hold the infos by adding the krefcount */
	ret = ipc_session_mem_info_get(session, pstCreate_handle_req);
	if (ret) {
		pr_err("Failed to hold the cipher_mem_src_info\n");
		mutex_unlock(&session->lock);
		goto OUT_FREE;
	}

	ret = do_ipc_msg_tx(session, pService_req);
	if (ret == 0) {
		ret = -ETIMEDOUT;
		mutex_unlock(&session->lock);
		dev_err(dev,"pid[%d],session->num:%d,timeout!!!\n",
			task_pid_nr(current), session->num);
		goto OUT_FREE;
	}
	else if(ret < 0) {
		mutex_unlock(&session->lock);
		dev_dbg(dev,"pid[%d],session->num:%d,cancled!!!\n",
			task_pid_nr(current), session->num);
		goto OUT_FREE;
	}
	else
		ret = 0;

	/* copy response data to user */
	pr_debug("---%s:%d,(%d), sizeof(res_srvc)=0x%lx, res_size=0x%x\n",
		__func__, __LINE__, task_pid_nr(current), sizeof(session->res_srvc), session->res_size);
	pr_debug("---%s:pid[%d],SRVC_TYPE=%d release the session!\n",
		__func__, task_pid_nr(current), pService_req->serivce_type);
	memcpy(&pstCreate_handle_req->service_resp, &session->res_srvc, session->res_size);
	mutex_unlock(&session->lock);

	if (copy_to_user(user_arg, pstCreate_handle_req, sizeof(cipher_create_handle_req_t)))
		ret = -EFAULT;

OUT_FREE:
	kfree(pstCreate_handle_req);

	return ret;
}

static ssize_t eswin_ipc_message_write(struct file *filp,
				       const char __user *userbuf, size_t count,
				       loff_t *ppos)
{
	process_data_list_t *pstProc_data_list = filp->private_data;
	struct ipc_session *session = pstProc_data_list->session;
	ipc_private_data_t *ipc_priv = session->ipc_priv;
	struct heap_root *hp_root = &ipc_priv->hp_root;
	struct device *dev = hp_root->dev;
	u8 reg_data[8];
	int ret;

	if (!session->mbox_channel) {
		dev_err(dev, "Channel cannot do Tx\n");
		return -EINVAL;
	}

	if (count > IPC_SERVICE_REQ_MAX_LEN) {
		dev_err(dev,
			"Message length %ld greater than ipc allowed %ld\n",
			count, IPC_SERVICE_REQ_MAX_LEN);
		return -EINVAL;
	}

	mutex_lock(&session->lock);

	atomic_set(&session->ipc_service_ready, false);
	session->req_msg = kzalloc(IPC_SERVICE_REQ_MAX_LEN, GFP_KERNEL);
	if (!session->req_msg) {
		ret = -ENOMEM;
		goto out1;
	}

	ret = copy_from_user(session->req_msg, userbuf, count);
	if (ret) {
		ret = -EFAULT;
		goto out2;
	}

	ret = get_send_reg_data(session->req_msg, reg_data, session);
	if (ret < 0) {
		ret = -EAGAIN;
		goto out3;
	}

	#ifndef ES_CIPHER_QEMU_DEBUG
	ret = mbox_send_message(session->mbox_channel, reg_data);
	if (ret < 0){
		ret = -EAGAIN;
		dev_err(dev, "Failed to send message via mailbox\r\n");
		goto out3;
	}

	atomic_set(&session->receive_data_ready, false);
	#else
	session->res_srvc.service_status = 0; // For QEMU simulation ,alwasy be true
	session->res_size = sizeof(res_data_domain_t) + RES_DATA_OFFSET;
	session->res_srvc.data_t.ecdh_key_res.ecdh_key.keylen = PKA_CURVE_BRAINPOOL_P256R1_BYTE;
	atomic_set(&session->receive_data_ready, true);
	#endif

	ret = wait_event_interruptible_timeout(session->waitq,
					 eswin_receive_data_ready(session),
					 MAX_RX_TIMEOUT);
	if (ret == 0)
		dev_err(dev, "Timeout waiting for scpu response\r\n");

out3:
	dma_free_coherent(dev, session->send_buff.size,
			  session->send_buff.cpu_vaddr,
			  session->send_buff.dma_addr);
out2:
	kfree(session->req_msg);

out1:
	atomic_set(&session->receive_data_ready, true);
	mutex_unlock(&session->lock);
	return ret;
}

static ssize_t eswin_ipc_message_read(struct file *filp, char __user *userbuf,
				      size_t count, loff_t *ppos)
{
	process_data_list_t *pstProc_data_list = filp->private_data;
	struct ipc_session *session = pstProc_data_list->session;
	ipc_private_data_t *ipc_priv = session->ipc_priv;
	struct heap_root *hp_root = &ipc_priv->hp_root;
	struct device *dev = hp_root->dev;
	int ret = 0;

	if (copy_to_user(userbuf, &session->res_srvc, session->res_size) != 0) {
		dev_err(dev, "copy to user failed\n");
		ret = -EFAULT;
		goto out;
	}

out:
	atomic_set(&session->ipc_service_ready, true);
	return ret;
}

static __poll_t eswin_service_status_poll(struct file *filp,
					  struct poll_table_struct *wait)
{
	process_data_list_t *pstProc_data_list = filp->private_data;
	struct ipc_session *session = pstProc_data_list->session;

	poll_wait(filp, &session->waitq, wait);

	if (eswin_ipc_service_ready(session))
		return EPOLLIN | EPOLLRDNORM;
	return 0;
}

static int ipc_ioctl_alloc_iova(process_data_list_t *pstProc_data_list, unsigned int cmd, void __user *user_arg)
{
	struct ipc_session *session = pstProc_data_list->session;
	struct device *dev = session->miscdev.parent;
	cipher_mem_resource_info_t *pstCipher_mem_rsc_info = NULL;
	khandle_dma_allocation_data_t *pstK_dma_info = NULL;
	struct dma_allocation_data *pstDma_alloc_info = NULL;
	unsigned int cmd_size = 0;
	u64 addr;
	u64 len;
	int ret = 0;

	// pr_debug("---%s:%d---\n", __func__, __LINE__);

	cmd_size = _IOC_SIZE(cmd);
	if (cmd_size != sizeof(khandle_dma_allocation_data_t)) {
		return -EFAULT;
	}

	pstCipher_mem_rsc_info = kzalloc(sizeof(*pstCipher_mem_rsc_info), GFP_KERNEL);
	if (!pstCipher_mem_rsc_info)
		return -ENOMEM;

	pstCipher_mem_rsc_info->session = session;
	pstK_dma_info = &pstCipher_mem_rsc_info->k_dma_info;
	if (copy_from_user(pstK_dma_info, user_arg, sizeof(khandle_dma_allocation_data_t))) {
		ret = -EFAULT;
		goto OUT_FREE_CIPHER_MEM;
	}

	pstDma_alloc_info = &pstK_dma_info->dma_alloc_info;
	addr = (u64)pstDma_alloc_info->cpu_vaddr;
	len = pstDma_alloc_info->size;
	dev_dbg(dev, "%s, addr=0x%llx,len=0x%llx\n", __func__, addr, len);
	ret = ipc_req_data_iova_alloc(session, pstCipher_mem_rsc_info);
	if (ret) {
		dev_err(dev, "failed to alloc iova, err=%d\n", ret);
		goto OUT_FREE_CIPHER_MEM;
	}

	/* init refcount of this mem resource info */
	kref_init(&pstCipher_mem_rsc_info->refcount);

	/* add this mem resource info into IDR(assign id simultaneously) */
	ret = ipc_add_cipher_mem_rsc_info_to_idr(session, pstCipher_mem_rsc_info);
	if (ret) {
		dev_err(dev, "Failed to add cipher_mem_rsc_info into IDR\n");
		goto OUT_UNIT_KERNEL_HANDLE;
	}

	/* alloc process priv data(node) and assign the ID that was allocated by ipc_add_cipher_mem_rsc_info_to_idr,
	   then add this node to the pstProc_data_list. This node is used for searching and delete the mem_rsc_info
	   with ID when eswin_ipc_release is called(i.e, the eswin_ipc file is closed bu user)*/
	ret = ipc_add_proc_priv_data_to_list(pstProc_data_list, pstCipher_mem_rsc_info->k_dma_info.id);
	if (ret) {
		dev_err(dev, "Failed to add proc data!\n");
		goto OUT_REMOVE_FROM_IDR;
	}

	if (copy_to_user(user_arg, pstK_dma_info, sizeof(khandle_dma_allocation_data_t))) {
		ret = -EFAULT;
		goto OUT_PROC_DATA_DEL;
	}
	pr_debug("%s:pid[%d]:id=%d, iova=0x%llx, dmabuf=0x%px, len=0x%llx\n",
		__func__, task_pid_nr(current), pstK_dma_info->id,
		pstDma_alloc_info->dma_addr, pstCipher_mem_rsc_info->dma_buf, len);

	/* increase refcount of this module to prevent module from being removed when mem resource is still in use */
	if (!try_module_get(THIS_MODULE)) {
		ret = -ENODEV;
		dev_err(dev, "Failed to get module!\n");
		goto OUT_PROC_DATA_DEL;
	}

	return 0;

OUT_PROC_DATA_DEL:
	ipc_del_proc_priv_data_from_list(pstProc_data_list, pstCipher_mem_rsc_info->k_dma_info.id);
OUT_REMOVE_FROM_IDR:
	ipc_remove_cipher_mem_rsc_info_from_idr(session, pstCipher_mem_rsc_info->k_dma_info.id);
OUT_UNIT_KERNEL_HANDLE:
	/* decrease h->refcount, then the refcount is 0 here, so that the iova will be freed. */
	kref_put(&pstCipher_mem_rsc_info->refcount, ipc_release_mem_khandle_fn);
OUT_FREE_CIPHER_MEM:
	kfree(pstCipher_mem_rsc_info);
	return ret;
}

static int ipc_ioctl_free_iova(process_data_list_t *pstProc_data_list, unsigned int cmd, void __user *user_arg)
{
	struct ipc_session *session = pstProc_data_list->session;
	khandle_dma_allocation_data_t k_dma_info;
	cipher_mem_resource_info_t *pstCipher_mem_rsc_info = NULL;
	unsigned int cmd_size = 0;
	int ret = 0;

	// pr_debug("---%s:%d---\n", __func__, __LINE__);

	cmd_size = _IOC_SIZE(cmd);
	if (cmd_size != sizeof(khandle_dma_allocation_data_t)) {
		return -EFAULT;
	}

	if (copy_from_user(&k_dma_info, user_arg, sizeof(khandle_dma_allocation_data_t))) {
		return -EFAULT;
	}

	pstCipher_mem_rsc_info = ipc_find_cipher_mem_rsc_info(session, k_dma_info.id);
	if (NULL == pstCipher_mem_rsc_info) {
		ret = -EFAULT;
		goto OUT;
	}
	pr_debug("%s:free id=%d\n", __func__, pstCipher_mem_rsc_info->k_dma_info.id);
	ipc_del_proc_priv_data_from_list(pstProc_data_list, k_dma_info.id);
	kref_put(&pstCipher_mem_rsc_info->refcount, ipc_release_mem_khandle_fn);
OUT:
	return ret;
}

static int ipc_ioctl_cpu_nid(process_data_list_t *pstProc_data_list, unsigned int cmd, void __user *user_arg)
{
	struct ipc_session *session = pstProc_data_list->session;
	struct device *dev = session->miscdev.parent;
	unsigned int cmd_size = 0;
	cipher_get_nid_req_t arg;
	int hartid, harts = fls(cpu_online_mask->bits[0]);
	int cpu;

	cmd_size = _IOC_SIZE(cmd);
	if (cmd_size != sizeof(arg)) {
		return -EFAULT;
	}

	if (copy_from_user(&arg, user_arg, sizeof(arg))) {
		return -EFAULT;
	}

	cpu = get_cpu();
	// hartid = cpuid_to_hartid_map(arg.cpuid);
	hartid = eswin_cpuid_to_hartid_map(cpu);
	put_cpu();

	arg.nid = (hartid < 4 ? 0 : 1);

	dev_dbg(dev, "%s, oneline_harts:%d, cpu%d,cpuid:%d, hartid:%d, nid:%d\n",
		__func__, harts, cpu, arg.cpuid, hartid, arg.nid);

	if (copy_to_user(user_arg, &arg, sizeof(arg))) {
		return -EFAULT;
	}

	return 0;
}

static long eswin_ipc_ioctl(struct file *filp, unsigned int cmd,
			    unsigned long arg)
{
	process_data_list_t *pstProc_data_list = filp->private_data;
	struct ipc_session *session = pstProc_data_list->session;
	struct heap_root *hp_root = &session->ipc_priv->hp_root;
	struct device *dev = hp_root->dev;
	void *cpu_vaddr = NULL;
	struct dmabuf_bank_info *info, *info_free;
	unsigned int cmd_size = 0;
	size_t buf_size = 0;
	int ret = 0;
	void __user *argp = (void __user *)arg;

	switch (cmd) {
		/* alloc memory by driver using dmabuf heap helper API  */
		case SCPU_IOC_ALLOCATE_MEM_BY_DRIVER_WITH_DMA_HEAP: {
			mutex_lock(&dmabuf_bank_lock);
			cmd_size = _IOC_SIZE(cmd);
			if (cmd_size != sizeof(struct dmabuf_bank_info)) {
				return -EFAULT;
			}

			info = kzalloc(sizeof(*info), GFP_KERNEL);
			if (!info) {
				return -ENOMEM;
			}

			if (copy_from_user(info, (void __user *)arg, cmd_size) != 0) {
				kfree(info);
				return -EFAULT;
			}

			/* alloc dmabuf via dma heap, map the pha to dma addr(iova), get dmabuf_fd*/
			info->hmem = common_dmabuf_heap_import_from_kernel(
				hp_root, "mmz_nid_0_part_0", info->dma_alloc_info.size, 0);
			if (IS_ERR(info->hmem)) {
				dev_err(dev, "dmabuf-heap alloc from kernel fail\n");
				kfree(info);
				return -ENOMEM;
			}
			info->dma_alloc_info.dmabuf_fd = info->hmem->dbuf_fd;

			/* map the pha to cpu vaddr */
			cpu_vaddr = common_dmabuf_heap_map_vaddr(info->hmem);
			if (cpu_vaddr == NULL) {
				dev_err(dev, "map to cpu_vaddr failed\n");
				common_dmabuf_heap_release(info->hmem);
				kfree(info);
				return -ENOMEM;
			}
			/* get the actual buf_size (page aligned size)of the dmabuf allocated by dmabuf_heap */
			buf_size = common_dmabuf_heap_get_size(info->hmem);

			pr_debug("dmabuf info: CPU KVaddr:0x%lx, PA:0x%lx, DMA addr(iova):0x%lx, allocated size=0x%lx\n",
				(unsigned long)info->hmem->vaddr,
				(unsigned long)sg_phys(info->hmem->sgt->sgl),
				(unsigned long)sg_dma_address(info->hmem->sgt->sgl),
				(unsigned long)buf_size);

			info->hmem->dir = DMA_BIDIRECTIONAL;
			info->dma_alloc_info.phy_addr =
				(unsigned long)sg_phys(info->hmem->sgt->sgl);
			info->dma_alloc_info.dma_addr =
				(unsigned long)sg_dma_address(info->hmem->sgt->sgl);

			pr_debug("%s:%d, alloc size=0x%llx\n", __func__, __LINE__,
				info->dma_alloc_info.size);
			/*  return the dmabuf_fd to user space, so it can mmap the dmabuf to tje vaddr of the user space*/
			if (copy_to_user((void __user *)arg, info, cmd_size) != 0) {
				dev_err(dev, "copy to user failed\n");
				ret = -EFAULT;
			}
			kfree(info);
			mutex_unlock(&dmabuf_bank_lock);
			break;
		}
		case SCPU_IOC_FREE_MEM_BY_DRIVER_WITH_DMA_HEAP: {
			mutex_lock(&dmabuf_bank_lock);

			cmd_size = _IOC_SIZE(cmd);
			if (cmd_size != sizeof(struct dmabuf_bank_info)) {
				return -EFAULT;
			}

			info_free = kzalloc(sizeof(*info_free), GFP_KERNEL);
			if (!info_free) {
				return -ENOMEM;
			}

			if (copy_from_user(info_free, (void __user *)arg, cmd_size) !=
			0) {
				kfree(info_free);
				return -EFAULT;
			}

			common_dmabuf_heap_umap_vaddr(info_free->hmem);
			common_dmabuf_heap_release(info_free->hmem);
			kfree(info_free);

			mutex_unlock(&dmabuf_bank_lock);
			break;
		}
		case SCPU_IOC_ALLOC_IOVA: {
			return ipc_ioctl_alloc_iova(pstProc_data_list, cmd, argp);
		}
		case SCPU_IOC_FREE_IOVA: {
			return ipc_ioctl_free_iova(pstProc_data_list, cmd, argp);
		}
		case SCPU_IOC_CPU_NID: {
			return ipc_ioctl_cpu_nid(pstProc_data_list, cmd, argp);
		}
		case SCPU_IOC_CREATE_HANDLE: {
			return ipc_ioctl_create_handle(pstProc_data_list, cmd, argp);
		}
		case SCPU_IOC_DESTROY_HANDLE: {
			return ipc_ioc_destroy_handle(pstProc_data_list, cmd, argp);
		}
		case SCPU_IOC_GET_HANDLE_CONFIG: {
			return ipc_ioc_get_handle_config(pstProc_data_list, cmd, argp);
		}
		case SCPU_IOC_UPDATE_HANDLE_CONFIG: {
			return ipc_ioc_update_handle_config(pstProc_data_list, cmd, argp);
		}
		case SCPU_IOC_MESSAGE_COMMUNICATION: {
			return ipc_ioctl_msg_commu(pstProc_data_list, cmd, argp);
		}
		default: {
			dev_err(dev, "Invalid IOCTL command %u\n", cmd);
			return -ENOTTY;
		}
	}

	return ret;
}

static const struct file_operations eswin_ipc_message_ops = {
	.owner = THIS_MODULE,
	.write = eswin_ipc_message_write,
	.read = eswin_ipc_message_read,
	.open = eswin_ipc_open,
	.poll = eswin_service_status_poll,
	.release = eswin_ipc_release,
	.unlocked_ioctl = eswin_ipc_ioctl,
};

static int eswin_ipc_request_channel(struct platform_device *pdev,
						   const char *name)
{
	struct ipc_session *session = platform_get_drvdata(pdev);
	struct mbox_client *client = &session->client;
	struct mbox_chan *channel = NULL;

	client->dev = &pdev->dev;
	client->rx_callback = eswin_ipc_rx_callback;
	client->tx_prepare = NULL;
	client->tx_done = eswin_ipc_tx_done;
	client->tx_block = false;
	client->knows_txdone = false;

	#ifndef ES_CIPHER_QEMU_DEBUG
	channel = mbox_request_channel_byname(client, name);
	if (IS_ERR(channel)) {
		dev_warn(&pdev->dev, "Failed to request %s channel\n", name);
		session->mbox_channel = NULL;
		return -EFAULT;
	}
	#endif
	session->mbox_channel = channel;
	dev_dbg(&pdev->dev, "request mbox chan %s\n", name);

	return 0;
}

static int eswin_ipc_dev_nid(struct device* dev, int *pNid)
{
	int nid;

	#ifdef CONFIG_NUMA
	nid = dev_to_node(dev);
	if (nid == NUMA_NO_NODE) {
		dev_err(dev, "%s:%d, numa-node-id was not defined!\n", __func__, __LINE__);
		return -EFAULT;
	}
	#else
	if (of_property_read_s32(dev->of_node, "numa-node-id", &nid)) {
		dev_err(dev, "%s:%d, numa-node-id was not defined!\n", __func__, __LINE__);
		return -EFAULT;
	}
	#endif

	*pNid = nid;

	return 0;
}

static int ipc_add_cipher_mem_rsc_info_to_idr(struct ipc_session *session, cipher_mem_resource_info_t *pstCipher_mem_rsc_info)
{
	int ret = 0;
	ipc_private_data_t *ipc_priv = session->ipc_priv;

	mutex_lock(&ipc_priv->idr_cipher_mem_lock);
	ret = idr_alloc(&ipc_priv->cipher_mem_resource_idr, pstCipher_mem_rsc_info, 0, INT_MAX, GFP_KERNEL);
	if (ret >= 0) {
		pstCipher_mem_rsc_info->k_dma_info.id = ret;
	}
	mutex_unlock(&ipc_priv->idr_cipher_mem_lock);

	return ret < 0 ? ret : 0;
}

static cipher_mem_resource_info_t *ipc_find_cipher_mem_rsc_info(struct ipc_session *session, u32 id)
{
	ipc_private_data_t *ipc_priv = session->ipc_priv;
	cipher_mem_resource_info_t *pstCipher_mem_rsc_info = NULL;

	mutex_lock(&ipc_priv->idr_cipher_mem_lock);
	pstCipher_mem_rsc_info = idr_find(&ipc_priv->cipher_mem_resource_idr, id);
	if (!pstCipher_mem_rsc_info) {
		pr_err("Failed to find cipher_mem_src_info of id:%d\n", id);
	}
	mutex_unlock(&ipc_priv->idr_cipher_mem_lock);

	return pstCipher_mem_rsc_info;
}

static void ipc_remove_cipher_mem_rsc_info_from_idr(struct ipc_session *session, u32 id)
{
	ipc_private_data_t *ipc_priv = session->ipc_priv;

	mutex_lock(&ipc_priv->idr_cipher_mem_lock);
	if (NULL == idr_remove(&ipc_priv->cipher_mem_resource_idr, id)) {
		pr_err("Failed to remove cipher_mem_src_info of id:%d(Not in previously in the IDR)\n", id);
	}
	mutex_unlock(&ipc_priv->idr_cipher_mem_lock);
}

static void ipc_release_mem_khandle_fn(struct kref *ref)
{
	cipher_mem_resource_info_t *pstCipher_mem_rsc_info = container_of(ref, cipher_mem_resource_info_t, refcount);
	struct ipc_session *session = pstCipher_mem_rsc_info->session;

	pr_debug("%s, id=%d, iova=0x%llx\n",
		__func__, pstCipher_mem_rsc_info->k_dma_info.id,
		pstCipher_mem_rsc_info->k_dma_info.dma_alloc_info.dma_addr);
	ipc_req_data_iova_free(session, pstCipher_mem_rsc_info);

	ipc_remove_cipher_mem_rsc_info_from_idr(session, pstCipher_mem_rsc_info->k_dma_info.id);

	kfree(pstCipher_mem_rsc_info);

	/* decrease refcount of this module */
	module_put(THIS_MODULE);
}

static void ipc_session_work(struct work_struct *work)
{
	#ifndef ES_CIPHER_QEMU_DEBUG
	struct ipc_session *session = container_of(work, struct ipc_session, session_work);
	#else
	struct ipc_session *session = container_of(work, struct ipc_session, d_session_work.work);
	#endif

	pr_debug("======%s, session->num:%d,service_id:%d done!\n",
		__func__, session->num, session->res_srvc.service_id);
	ipc_session_mem_info_put(session);
	atomic_set(&session->receive_data_ready, 1);
	wake_up_interruptible(&session->waitq);
}

static int ipc_cipher_mem_rsc_info_mgt_init(struct ipc_session *session)
{
	struct device *dev = session->miscdev.parent;
	ipc_private_data_t *ipc_priv = NULL;
	int ret = 0;

	ipc_priv = kzalloc(sizeof(ipc_private_data_t), GFP_KERNEL);
	if (!ipc_priv)
		return -ENOMEM;
	/* Init ipc_private_data_t for each process who opened this device*/
	common_dmabuf_heap_import_init(&ipc_priv->hp_root, dev);
	INIT_LIST_HEAD(&ipc_priv->usermem_iova_list);
	mutex_init(&ipc_priv->usermem_iova_lock);

	mutex_init(&ipc_priv->idr_cipher_mem_lock);
	idr_init(&ipc_priv->cipher_mem_resource_idr);

	session->ipc_priv = ipc_priv;

	/* init work queue for putting back mem_rsc_info which was hold by session */
	session->work_q = create_singlethread_workqueue("eswin-ipc-wq");
	if (!session->work_q) {
		dev_err(dev, "failed to init work queue\n");
		ret = -ENOMEM;
		goto OUT;
	}
	#ifndef ES_CIPHER_QEMU_DEBUG
	INIT_WORK(&session->session_work, ipc_session_work);
	#else
	INIT_DELAYED_WORK(&session->d_session_work, ipc_session_work);
	#endif

	return 0;
OUT:
	idr_destroy(&ipc_priv->cipher_mem_resource_idr);
	common_dmabuf_heap_import_uninit(&ipc_priv->hp_root);
	kfree(ipc_priv);
	return ret;
}

static int ipc_cipher_mem_rsc_mgt_unit(struct ipc_session *session)
{
	ipc_private_data_t *ipc_priv = session->ipc_priv;
	struct heap_root *hp_root = &ipc_priv->hp_root;
	struct device *dev = session->miscdev.parent;
	int ret = 0;

	mutex_lock(&ipc_priv->idr_cipher_mem_lock);
	if (!idr_is_empty(&ipc_priv->cipher_mem_resource_idr)) {
		dev_err(dev, "Can't uninstall driver, some of the resource are still in use!!!\n");
		ret = -EBUSY;
		mutex_unlock(&ipc_priv->idr_cipher_mem_lock);
		goto err;
	}
	idr_destroy(&ipc_priv->cipher_mem_resource_idr);
	mutex_unlock(&ipc_priv->idr_cipher_mem_lock);

	common_dmabuf_heap_import_uninit(hp_root);
	kfree(ipc_priv);

	destroy_workqueue(session->work_q);

err:
	return ret;
}

static int ipc_cipher_handle_free(int id, void *p, void *data)
{
	req_service_t *service_req = (req_service_t *)p;

	kfree(service_req);

	pr_debug("---%s: id=%d\n", __func__, id);
	return 0;
}

static int ipc_cipher_destroy_all_handles(struct ipc_session *session)
{
	int ret = 0;

	mutex_lock(&session->idr_lock);
	ret = idr_for_each(&session->handle_idr, ipc_cipher_handle_free, NULL);
	idr_destroy(&session->handle_idr);
	mutex_unlock(&session->idr_lock);

	return ret;
}

static int eswin_ipc_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	const char *mbox_channel_name;
	struct ipc_session *session = NULL;
	int nid = 0;
	int ret = 0;

	ret = eswin_ipc_dev_nid(dev, &nid);
	if (ret) {
		dev_err(dev, "failed to ipc's nodeID\n");
		return ret;
	}
	#ifndef ES_CIPHER_QEMU_DEBUG
	ret = win2030_aon_sid_cfg(dev);
	if (ret) {
		dev_err(dev, "failed to init_streamID: %d\n", ret);
		return -EFAULT;
	}
	win2030_tbu_power(dev, true);
	#endif
	ret = dma_set_mask_and_coherent(dev, DMA_BIT_MASK(32));
	if (ret)
		dev_warn(dev, "Unable to set coherent mask\n");

	session = devm_kzalloc(dev, sizeof(*session), GFP_KERNEL);
	if (!session) {
		return -ENOMEM;
	}
	platform_set_drvdata(pdev, session);

	session->miscdev.minor = MISC_DYNAMIC_MINOR;
	session->miscdev.name = devm_kasprintf(dev, GFP_KERNEL, "%s%d", "eswin_ipc_misc_dev", nid);
	session->miscdev.fops = &eswin_ipc_message_ops;
	session->miscdev.parent = dev;

	/* dma_alloc_coherent memory for memory sharing between scpu and u84 */
	ret = get_shm_addr(session);
	if (ret < 0) {
		devm_kfree(dev, session);
		ret = -ENOMEM;
		goto OUT_FREE_SESSION;
	}

	ret =  ipc_cipher_mem_rsc_info_mgt_init(session);
	if (0 != ret) {
		dev_err(dev, "failed to init mem resource mgt: %d\n", ret);
		goto OUT_FREE_SESSION;
	}

	ret = misc_register(&session->miscdev);
	if (ret) {
		dev_err(dev, "failed to register misc device: %d\n", ret);
		ret = -EBUSY;
		goto OUT_UNIT_MEM_MGT;
	}

	/*use eswin mailbox0 to send msg and mailbox1 receive msg*/
	ret = device_property_read_string(&pdev->dev, "mbox-names",
					  &mbox_channel_name);
	if (ret == 0) {
		ret = eswin_ipc_request_channel(pdev, mbox_channel_name);
		if (ret) {
			dev_err(dev, "failed to request mbox channel\n");
			goto OUT_MISC_DEREGISTER;
		}
	}else{
		dev_err(dev, "given arguments are not valid: %d\n", ret);
		ret = -EINVAL;
		goto OUT_MISC_DEREGISTER;
	}

	mutex_init(&session->lock);
	init_waitqueue_head(&session->waitq);
	dev_info(dev, "initialized successfully\n");

	mutex_init(&session->idr_lock);
	idr_init(&session->handle_idr);

	atomic_set(&session->receive_data_ready, true);
	atomic_set(&session->ipc_service_ready, true);
	pr_debug("sizeof(cipher_create_handle_req_t)=%ld, sizeof(req_service_t)=%ld, sizeof(res_service_t)=%ld, sizeof(req_data_domain_t)=%ld\n",
		sizeof(cipher_create_handle_req_t), sizeof(req_service_t), sizeof(res_service_t), sizeof(req_data_domain_t));

	return 0;

OUT_MISC_DEREGISTER:
	misc_deregister(&session->miscdev);
OUT_UNIT_MEM_MGT:
	ipc_cipher_mem_rsc_mgt_unit(session);
OUT_FREE_SESSION:
	free_shm_addr(session);
	devm_kfree(dev, session);

	return ret;
}

static int eswin_ipc_remove(struct platform_device *pdev)
{
	struct ipc_session *session = platform_get_drvdata(pdev);
	struct device *dev = session->miscdev.parent;
	int ret = 0;

	ret =ipc_cipher_mem_rsc_mgt_unit(session);
	if (ret)
		return ret;

	ipc_cipher_destroy_all_handles(session);

	#ifndef QEMU_DEBU
	mbox_free_channel(session->mbox_channel);
	#endif

	misc_deregister(&session->miscdev);

	free_shm_addr(session);
	win2030_tbu_power(dev, false);
	dev_info(&pdev->dev, "%s removed!\n", pdev->name);
	return 0;
}

static const struct of_device_id eswin_ipc_match[] = {
	{
		.compatible = "eswin,win2030-ipc",
	},
	{ /* Sentinel */ }
};

static struct platform_driver eswin_ipc_driver = {
	.driver = {
		.name = "win2030-ipc",
		.of_match_table = eswin_ipc_match,
		.suppress_bind_attrs	= true,
	},
	.probe = eswin_ipc_probe,
	.remove = eswin_ipc_remove,
};
// builtin_platform_driver(eswin_ipc_driver);
module_platform_driver(eswin_ipc_driver);

MODULE_DESCRIPTION("ESWIN WIN2030 firmware protocol driver");
MODULE_LICENSE("GPL v2");