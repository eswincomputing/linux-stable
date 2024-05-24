/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __ESWIN_KHANDLE_H_
#define __ESWIN_KHANDLE_H_

#include <linux/kref.h>
#include <linux/types.h>
#include <linux/list.h>
#include <linux/mutex.h>

#define INVALID_HANDLE_VALUE (-1)

struct khandle;
typedef void (*release_khandle_fn)(struct khandle *h);
struct khandle {
	int fd;
	int magic;
	spinlock_t lock;
	release_khandle_fn fn;
	struct kref refcount;
	struct list_head entry;
	struct list_head head;
	struct khandle *parent;
	void *fd_pool;
};

/**
 * @brief Remove the family relations hierachy. This function also actively
 * free the fd of this kernel object and its descendants.
 *
 * @param o: This is the kernel object.
 */
void kernel_handle_release_family(struct khandle *o);

/**
 * @brief Decrease the reference of kernel object `o`. If reference reaches 0,
 * the release delegation function is called.
 *
 * @param o: This is the kernel object.
 */
void kernel_handle_decref(struct khandle *o);


/**
 * @brief Increase the reference of kernel object `o`.
 *
 * @param o: This is the kernel object.
 */
void kernel_handle_addref(struct khandle *o);


/**
 * @brief This function intialize an kernel object in the memory specified by
 * `o`. It returns zero on success or a Linux error code. Note this function
 * should only be called in IOCtl context. The initial reference is set to 1.
 *
 * @param o: This specifies an memory for holding kernel object.
 * @param fn: This points to a callback delegation function. When the
 * reference of `o` reaches 0, this callback function is called. It
 * is intended for releasing resources associated with this kernel
 * object.
 * @param magic: This is a magic number for determining the type of kernel
 * object.
 * @param parent: Points to the parent of this kernel object.
 * @return It returns zero on success or a Linux error code.
 *
 * when use khandle, host structure release must use kernel_handle_decref function.
 */
int init_kernel_handle(struct khandle *o, release_khandle_fn fn, int magic,
					   struct khandle *parent);


/**
 * @brief This function is used to find the kernel object associated with fd.
 * Note the khandle object has one additional reference so user should dereference
 * it if not needed.
 *
 * @param ancestor: This is one ancestor of kernel object that matches fd.
 * @param fd: This is the fd associated with a specific kernel object.
 * @param magic: This is the magic associated with a specific kernel object.
 * @return It returns the kernel object on success or NULL if the given fd
 * is invalid.
 */
struct khandle *find_kernel_handle(struct khandle *ancestor, int fd, int magic);
#endif
