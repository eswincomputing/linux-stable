/****************************************************************************
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2020 VeriSilicon Holdings Co., Ltd.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 *****************************************************************************
 *
 * The GPL License (GPL)
 *
 * Copyright (c) 2020 VeriSilicon Holdings Co., Ltd.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program;
 *
 *****************************************************************************
 *
 * Note: This software is released under dual MIT and GPL licenses. A
 * recipient may use this file under the terms of either the MIT license or
 * GPL License. If you wish to use only one license not the other, you can
 * indicate your decision by deleting one of the above license notices in your
 * version of this file.
 *
 *****************************************************************************/
#ifndef _VIVDW200_QUEUE_H_
#define _VIVDW200_QUEUE_H_
#ifdef __KERNEL__
#include <linux/list.h>
#endif

typedef struct vivdw200_mis_s {
	unsigned int val;
#ifdef __KERNEL__
	struct list_head list;
#endif
} vivdw200_mis_t;
typedef struct vivdw200_mis_list_s {
	vivdw200_mis_t *pHead;
	vivdw200_mis_t *pRead;
	vivdw200_mis_t *pWrite;
} vivdw200_mis_list_t;

int vivdw200_enqueue(vivdw200_mis_t *data, vivdw200_mis_t *head);
int vivdw200_dequeue(vivdw200_mis_t *data, vivdw200_mis_t *head);
bool vivdw200_is_queue_empty(vivdw200_mis_t *head);

#define QUEUE_NODE_COUNT 256
int vivdw200_create_circle_queue(vivdw200_mis_list_t *pCList, int number);
int vivdw200_destroy_circle_queue(vivdw200_mis_list_t *pCList);

int vivdw200_read_circle_queue(vivdw200_mis_t *data,
			       vivdw200_mis_list_t *pCList);
int vivdw200_write_circle_queue(vivdw200_mis_t *data,
				vivdw200_mis_list_t *pCList);

#endif
