// SPDX-License-Identifier: GPL-2.0

#include "eswin-khandle.h"
#include <linux/vmalloc.h>
#include <asm/atomic.h>
#include <linux/xarray.h>

struct fd_pool_desc {
	spinlock_t lock;
	struct xarray fd_array;
};

static void init_fd_pool(void *fd_pool)
{
	struct fd_pool_desc *pool = (struct fd_pool_desc *)fd_pool;
	pr_debug("%s, %d, pool=0x%px", __func__, __LINE__, pool);
	xa_init_flags(&pool->fd_array, XA_FLAGS_ALLOC);
	spin_lock_init(&pool->lock);
}

static int alloc_fd(void *fd_pool, struct khandle *h)
{
	struct fd_pool_desc *pool = (struct fd_pool_desc *)fd_pool;
	int ret;
	u32 fd;

	pr_debug("%s, %d, pool=0x%px", __func__, __LINE__, pool);
	ret = xa_alloc(&pool->fd_array, &fd, h, xa_limit_32b, GFP_ATOMIC);
	if (ret < 0) {
		pr_err("%s, %d, ret=%d.\n", __func__, __LINE__, ret);
		return ret;
	}
	pr_debug("%s, %d, pool=0x%px, fd=%d.\n", __func__, __LINE__, pool, fd);
	return fd;
}

static void release_fd(void *fd_pool, int fd)
{
	unsigned long flags;
	struct khandle *h;
	struct fd_pool_desc *pool = (struct fd_pool_desc *)fd_pool;

	pr_debug("%s, %d, pool=0x%px, fd=%d.\n", __func__, __LINE__, pool, fd);

	spin_lock_irqsave(&pool->lock, flags);
	h = xa_load(&pool->fd_array, fd);
    if (!h) {
    	spin_unlock_irqrestore(&pool->lock, flags);
        return;
    }
	xa_erase(&pool->fd_array, fd);
    spin_unlock_irqrestore(&pool->lock, flags);
}

static struct khandle *find_khandle_by_fd(void *fd_pool, int fd)
{
	unsigned long flags;
	struct khandle *h;
	struct fd_pool_desc *pool = (struct fd_pool_desc *)fd_pool;

	spin_lock_irqsave(&pool->lock, flags);
	h = xa_load(&pool->fd_array, fd);
	if (h == NULL) {
    	spin_unlock_irqrestore(&pool->lock, flags);
		return NULL;
	}
	kref_get(&h->refcount);
	spin_unlock_irqrestore(&pool->lock, flags);
	return h;
}

static void kref_khandle_fn(struct kref *kref)
{
	unsigned long flags;
	struct khandle *h = container_of(kref, struct khandle, refcount);
	struct khandle *parent;
	struct fd_pool_desc *pool;

	pr_debug("%s, h address=0x%px.\n", __func__, h);
	BUG_ON(h == NULL);
	BUG_ON(h->fd != INVALID_HANDLE_VALUE);

	pr_debug("%s, k->fd=%d, refcount=%d.\n", __func__, h->fd,
		 kref_read(kref));

	parent = h->parent;

	if (parent == NULL) {
		pool = h->fd_pool;
		xa_destroy(&pool->fd_array);
		vfree(h->fd_pool);
	} else {
		spin_lock_irqsave(&parent->lock, flags);
		list_del_init(&h->entry);
		spin_unlock_irqrestore(&parent->lock, flags);
	}

	if (h->fn != NULL) {
		h->fn(h);
	}

	if (parent != NULL) {
		kref_put(&parent->refcount, kref_khandle_fn);
	}
}

void kernel_handle_addref(struct khandle *h)
{
	BUG_ON(h == NULL);

	kref_get(&h->refcount);
	pr_debug("%s, h addr=0x%px, fd=%d, refcount=%d.\n", __func__, h, h->fd,
		 kref_read(&h->refcount));
}
EXPORT_SYMBOL(kernel_handle_addref);

void kernel_handle_decref(struct khandle *h)
{
	BUG_ON(h == NULL);

	kref_put(&h->refcount, kref_khandle_fn);
	pr_debug("%s, done.\n", __func__);
}
EXPORT_SYMBOL(kernel_handle_decref);

static struct list_head *capture_next_khandle_node(struct list_head *head,
						   struct list_head *cur)
{
	struct khandle *h;

	while (true) {
		cur = cur->next;
		if (cur == head) {
			return cur;
		}

		/* Protect child not released until return of kernel_handle_release_family. */
		h = container_of(cur, struct khandle, entry);
		if (kref_get_unless_zero(&h->refcount) != 0) {
			return cur;
		}
	}
}

void kernel_handle_release_family(struct khandle *h)
{
	unsigned long flags;
	struct list_head *child;
	struct khandle *child_khandle;

	BUG_ON(h == NULL);
	spin_lock_irqsave(&h->lock, flags);
	if (h->fd == INVALID_HANDLE_VALUE) {
		spin_unlock_irqrestore(&h->lock, flags);
		return;
	}

	release_fd(h->fd_pool, h->fd);
	h->fd = INVALID_HANDLE_VALUE;
	child = capture_next_khandle_node(&h->head, &h->head);
	while (child != &h->head) {
		child_khandle = container_of(child, struct khandle, entry);
		child = capture_next_khandle_node(&h->head, child);
		spin_unlock_irqrestore(&h->lock, flags);
		kernel_handle_release_family(child_khandle);
		kernel_handle_decref(child_khandle);
		spin_lock_irqsave(&h->lock, flags);
	}

	spin_unlock_irqrestore(&h->lock, flags);
	kref_put(&h->refcount, kref_khandle_fn);
	pr_debug("%s, done.\n", __func__);
}
EXPORT_SYMBOL(kernel_handle_release_family);

int init_kernel_handle(struct khandle *h, release_khandle_fn fn, int magic,
		       struct khandle *parent)
{
	unsigned long flags;
	void *fd_pool;

	BUG_ON(h == NULL);
	kref_init(&h->refcount);
	kref_get(&h->refcount);

	if ((h->parent = parent) == NULL) {
		fd_pool = vmalloc(sizeof(struct fd_pool_desc));
		init_fd_pool(fd_pool);
		if (fd_pool == NULL) {
			return -ENOMEM;
		}
	} else {
		fd_pool = parent->fd_pool;
	}
	h->fd_pool = fd_pool;

	if ((h->fd = alloc_fd(fd_pool, h)) == INVALID_HANDLE_VALUE) {
		BUG_ON(parent == NULL);
		return -EINVAL;
	}

	pr_debug("%s, hfile addr=%u.\n", __func__, h->fd);
	h->fn = fn;
	h->magic = magic;
	spin_lock_init(&h->lock);

	INIT_LIST_HEAD(&h->head);
	INIT_LIST_HEAD(&h->entry);

	if (parent != NULL) {
		spin_lock_irqsave(&parent->lock, flags);
		if (parent->fd == INVALID_HANDLE_VALUE) {
			spin_unlock_irqrestore(&parent->lock, flags);
			release_fd(fd_pool, h->fd);
			return -EINVAL;
		}

		list_add_tail(&h->entry, &parent->head);
		kref_get(&parent->refcount);
		spin_unlock_irqrestore(&parent->lock, flags);
	}

	return 0;
}
EXPORT_SYMBOL(init_kernel_handle);

struct khandle *find_kernel_handle(struct khandle *ancestor, int fd, int magic)
{
	struct khandle *h;

	h = find_khandle_by_fd(ancestor->fd_pool, fd);
	if (h == NULL) {
		return NULL;
	}

	if (h->magic != magic) {
		kref_put(&h->refcount, kref_khandle_fn);
		return NULL;
	}
	return h;
}
EXPORT_SYMBOL(find_kernel_handle);
