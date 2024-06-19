// SPDX-License-Identifier: GPL-2.0-only
/*
 * Header file of es_proc.c
 *
 * Copyright 2024 Beijing ESWIN Computing Technology Co., Ltd.
 *   Authors:
 *    HuangYiFeng<huangyifeng@eswincomputing.com>
 *
 */

#ifndef __ES_PROC__
#define __ES_PROC__

#define PROC_ENTRY_VI "vi"
#define PROC_ENTRY_VO "vo"
#define PROC_ENTRY_VB "vb"
#define PROC_ENTRY_ISP "isp"

// proc
typedef struct es_proc_dir_entry {
	char name[50];
	void *proc_dir_entry;
	int (*open)(struct es_proc_dir_entry *entry);
	int (*read)(struct es_proc_dir_entry *entry);
	int (*write)(struct es_proc_dir_entry *entry, const char *buf,
		     int count, long long *);
	void *private;
	void *seqfile;
	struct list_head node;
} es_proc_entry_t;

extern es_proc_entry_t *es_create_proc_entry(const char *name,
					es_proc_entry_t *parent);
extern es_proc_entry_t *es_proc_mkdir(const char *name,
					es_proc_entry_t *parent);
extern void es_remove_proc_entry(const char *name, es_proc_entry_t *parent);
extern int es_seq_printf(es_proc_entry_t *entry, const char *fmt, ...)
	__attribute__((format(printf, 2, 3)));

#endif
