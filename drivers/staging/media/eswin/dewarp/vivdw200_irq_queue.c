
#ifdef __KERNEL__
#include <asm/io.h>

#include <linux/cdev.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/delay.h>
#include <linux/clk.h>
#include <linux/io.h>
#include <linux/mm.h>

#include <linux/timer.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/workqueue.h>
#include <linux/slab.h>
#include <linux/proc_fs.h>
#include <linux/debugfs.h>
#include <linux/miscdevice.h>
#include <linux/uaccess.h>
#include <linux/interrupt.h>
#else
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#endif
#include "vivdw200_irq_queue.h"

//enqueue
int vivdw200_enqueue(vivdw200_mis_t *data, vivdw200_mis_t *head)
{
#ifdef __KERNEL__
	vivdw200_mis_t *new_node = (vivdw200_mis_t *)kmalloc(
		sizeof(vivdw200_mis_t), GFP_KERNEL); //create new node

	if (data == NULL || head == NULL) {
		//printk("%s: input wrong parameter\n", __func__);
		return -1;
	}
	new_node->val = data->val;

	printk("%s: new_node %px", __func__, new_node);
	INIT_LIST_HEAD(&new_node->list);
	list_add_tail(&new_node->list, &head->list); //append to tail
#endif
	return 0;
}

//dequeue && release memory
int vivdw200_dequeue(vivdw200_mis_t *data, vivdw200_mis_t *head)
{
#ifdef __KERNEL__
	vivdw200_mis_t *entry;
	if (data == NULL || head == NULL) {
		//printk("%s: input wrong parameter\n", __func__);
		return -1;
	}
	if (list_empty(&head->list)) {
		//printk("%s: There is no node\n", __func__);
		return -1;
	}

	entry = list_first_entry(&head->list, vivdw200_mis_t, list);
	printk("%s: entry %px", __func__, entry);
	data->val = entry->val;

	list_del_init(&entry->list);

	kfree(entry);
#endif
	return 0;
}

bool vivdw200_is_queue_empty(vivdw200_mis_t *head)
{
#ifdef __KERNEL__
	return list_empty(&head->list);
#else
	return 0;
#endif
}

int vivdw200_create_circle_queue(vivdw200_mis_list_t *pCList, int number)
{
#ifdef __KERNEL__
	int i;
	vivdw200_mis_t *pMisNode;
	if (pCList == NULL || number <= 0) {
		printk("%s: create circle queue failed\n", __func__);
		return -1;
	}

	if (pCList->pHead == NULL) {
		pCList->pHead = (vivdw200_mis_t *)kmalloc(
			sizeof(vivdw200_mis_t), GFP_KERNEL);
		INIT_LIST_HEAD(&pCList->pHead->list);
		pCList->pRead = pCList->pHead;
		pCList->pWrite = pCList->pHead;
	}
	// printk("%s:pHead %px\n", __func__, pCList->pHead);
	for (i = 0; i < number - 1; i++) {
		pMisNode = (vivdw200_mis_t *)kmalloc(sizeof(vivdw200_mis_t),
						     GFP_KERNEL);
		INIT_LIST_HEAD(&pMisNode->list);
		list_add_tail(&pMisNode->list, &pCList->pHead->list);
		// printk("%s:pMisNode %px\n", __func__, pMisNode);
	}

#endif
	return 0;
}

int vivdw200_destroy_circle_queue(vivdw200_mis_list_t *pCList)
{
#ifdef __KERNEL__
	vivdw200_mis_t *pMisNode;
	if (pCList == NULL) {
		printk("%s: destroy circle queue failed. pClist %px\n",
		       __func__, pCList);
		return -1;
	}

	while (!list_empty(&pCList->pHead->list)) {
		pMisNode = list_first_entry(&pCList->pHead->list,
					    vivdw200_mis_t, list);
		// printk("%s:pMisNode %px\n", __func__, pMisNode);
		list_del(&pMisNode->list);
		kfree(pMisNode);
		pMisNode = NULL;
	}
	// printk("%s:pHead %px\n", __func__, pCList->pHead);
	kfree(pCList->pHead);
	pCList->pHead = NULL;
	pCList->pRead = NULL;
	pCList->pWrite = NULL;
#endif
	return 0;
}

int vivdw200_read_circle_queue(vivdw200_mis_t *data,
			       vivdw200_mis_list_t *pCList)
{
#ifdef __KERNEL__
	//vivdw200_mis_t* pReadEntry;
	if (pCList == NULL) {
		printk("%s: can not read circle queue\n", __func__);
		return -1;
	}

	if (pCList->pRead == pCList->pWrite) {
		printk("%s: There is no irq mis data\n", __func__);
		return -1;
	}
	data->val = pCList->pRead->val;

	// printk("%s: entry %px, msi %08x\n", __func__, pCList->pRead, data->val);
	/*Get the next entry that link with read entry list*/
	/*Update read pointer to next entry*/
	pCList->pRead =
		list_first_entry(&pCList->pRead->list, vivdw200_mis_t, list);

	//pCList->pRead = pReadEntry;

#endif
	return 0;
}

int vivdw200_write_circle_queue(vivdw200_mis_t *data,
				vivdw200_mis_list_t *pCList)
{
#ifdef __KERNEL__
	vivdw200_mis_t *pWriteEntry;
	if (pCList == NULL) {
		printk("%s: can not write circle queue\n", __func__);
		return -1;
	}

	pr_info("%s enqueue mis val 0x%x\n", __func__, data->val);

	if (!pCList->pWrite) {
		printk("%s, irq queue is full\n", __func__);
		return -1;
	}

	pCList->pWrite->val = data->val;
	// printk("%s: entry %px, msi %08x\n", __func__,  pCList->pWrite,  data->val);
	/*get the next write entry pointer that link with the write entry list*/
	pWriteEntry =
		list_first_entry(&pCList->pWrite->list, vivdw200_mis_t, list);

	/*Update write pointer to point next entry*/
	pCList->pWrite = pWriteEntry;
#endif

	return 0;
}