// Copyright © 2023 ESWIN. All rights reserved.
//
// Beijing ESWIN Computing Technology Co., Ltd and its affiliated companies ("ESWIN") retain
// all intellectual property and proprietary rights in and to this software. Except as expressly
// authorized by ESWIN, no part of the software may be released, copied, distributed, reproduced,
// modified, adapted, translated, or created derivative work of, in whole or in part.

#include <linux/fs.h>
#include <asm/uaccess.h>
#include <linux/namei.h>
#include "dsp_main.h"
#include "xt_mld_loader.h"
#include "xt_elf.h"

#define DSP_CACHE_LINE_SIZE 64

static int load_op_file(char *file_path, char *buff, int size)
{
	struct file *file = NULL;
	loff_t pos = 0;
	int ret = 0;

	file = filp_open(file_path, O_RDONLY, 0664);

	if (IS_ERR(file)) {
		dsp_err("\nfile_read: filp_open fail!\n");
		return -EEXIST;
	}

	ret = kernel_read(file, buff, size, &pos);
	filp_close(file, NULL);
	return 0;
}

static int get_file_size(const char *file_path)
{
	struct path p;
	struct kstat ks;
	if (kern_path(file_path, 0, &p) == 0 &&
	    vfs_getattr(&p, &ks, 0, 0) == 0) {
		return ks.size;
	} else {
		dsp_err("invalid file:%s\n", file_path);
	}
	return 0;
}

static int load_op_funcs(const void *buf, xtmld_state_t *lib_info,
			 struct dsp_op_desc *op)
{
	Elf32_Ehdr *elf = (Elf32_Ehdr *)buf;
	void *shoff = (void *)elf + elf->e_shoff;
	Elf32_Shdr *shstrtab =
		(Elf32_Shdr *)(shoff + elf->e_shstrndx * elf->e_shentsize);
	void *shoff0 = (void *)elf + elf->e_shoff;
	char *symstr = NULL;

	int ret = 0;
	int i, k, t;
	uint32_t real_addr;
	char *shstr = vmalloc(shstrtab->sh_size);
	if (shstr == NULL) {
		return -ENOMEM;
	}

	memcpy(shstr, ((void *)elf) + shstrtab->sh_offset, shstrtab->sh_size);

	for (k = 0; k < elf->e_shnum; k++, shoff0 += elf->e_shentsize) {
		Elf32_Shdr *sh = (Elf32_Shdr *)shoff0;
		if (SHT_STRTAB == sh->sh_type) {
			if (strcmp(".strtab", shstr + sh->sh_name))
				continue;
			symstr = vmalloc(sh->sh_size);
			if (symstr == NULL) {
				ret = -ENOMEM;
				goto failed;
			}
			memcpy(symstr, ((void *)elf) + sh->sh_offset,
			       sh->sh_size);
			break;
		}
	}

	for (k = 0; k < elf->e_shnum; k++, shoff += elf->e_shentsize) {
		Elf32_Shdr *sh = (Elf32_Shdr *)shoff;
		if (SHT_STRTAB == sh->sh_type) {
		} else if (SHT_SYMTAB == sh->sh_type) {
			Elf32_Sym *symtab =
				(Elf32_Sym *)(((void *)elf) + sh->sh_offset);
			int sym_num = sh->sh_size / sizeof(Elf32_Sym);
			for (i = 0; i < sym_num; i++) {
				if (ELF32_ST_TYPE(symtab->st_info) !=
				    STT_FUNC) {
					symtab++;
					continue;
				}

				for (t = 0; t < lib_info->num_sections; t++) {
					if (symtab->st_value >=
						    lib_info->src_offs[t] &&
					    symtab->st_value <
						    (lib_info->src_offs[t] +
						     lib_info->size[t])) {
						real_addr =
							op->iova_base +
							(symtab->st_value -
							 lib_info->src_offs[t]);
						break;
					}
				}

				if (strcmp("_start",
					   symstr + symtab->st_name) == 0) {
					// nothing
				} else if (strcmp("prepare",
						  symstr + symtab->st_name) ==
					   0) {
					op->funcs.dsp_prepare_fn = real_addr;
				} else if (strcmp("eval",
						  symstr + symtab->st_name) ==
					   0) {
					op->funcs.dsp_eval_fn = real_addr;
				}
				symtab++;
			}
		}
	}
	if (!op->funcs.dsp_eval_fn) {
		dsp_err("func eval not found,[%0x,%0x] ", op->funcs.dsp_eval_fn);
		ret = -1;
	}

	if (symstr) {
		vfree(symstr);
	}
failed:
	if (shstr) {
		vfree(shstr);
	}
	return ret;
}

int mloader_load_operator(char *buff, struct es_dsp *dsp,
			  struct dsp_op_desc *op)
{
	xtmld_library_info_t info;
	xtmld_result_code_t ret_code;
	xtmld_state_t loader_state = { 0 };
	void *p_start;
	int i;
	int code_size = 0;
	int dram_size = 0;
	int data_size = 0;
	void *current_addr;
	void *code_memory[XTMLD_MAX_CODE_SECTIONS];
	void *data_memory[XTMLD_MAX_DATA_SECTIONS];
	void *data_addr = (void *)DSP_IDDR_IOVA;
	void *dram_addr = (void *)XTMLD_DATARAM0_ADDR;

	xtmld_packaged_lib_t *p_mld_lib = (xtmld_packaged_lib_t *)buff;
	if ((ret_code = xtmld_library_info(p_mld_lib, &info)) !=
	    xtmld_success) {
		dsp_err("xtmld_library_info failed\n");
		return -1;
	}
	for (i = 0; i < info.number_of_code_sections; i++) {
		code_size += info.code_section_size[i];
	}
	code_size += DSP_CACHE_LINE_SIZE * 2;
	dsp_debug("op library text size:0x%x\n", code_size);
	op->op_shared_seg_size = round_up(code_size, DSP_2M_SIZE);

	op->op_shared_seg_ptr = dma_alloc_coherent(
		dsp->dev, op->op_shared_seg_size, &op->iova_base, GFP_KERNEL);
	if (!op->op_shared_seg_ptr) {
		dsp_err("dma map iova failed.\n");
		goto map_failed;
	}
	current_addr = op->op_shared_seg_ptr;
	dsp_debug("op->iova_base=0x%x size:0x%x\n", op->iova_base,
		  op->op_shared_seg_size);
	for (i = 0; i < info.number_of_code_sections; i++) {
		if (info.code_section_size[i]) {
			code_memory[i] = current_addr;
			current_addr += info.code_section_size[i];
			loader_state.text_addr[i] =
				(void *)(op->iova_base +
					 (code_memory[i] -
					  op->op_shared_seg_ptr));
		} else {
			code_memory[i] = 0;
			loader_state.text_addr[i] = NULL;
		}
	}

	for (i = 0; i < info.number_of_data_sections; i++) {
		if (info.data_section_mem_req[i] == xtmld_load_require_dram0 ||
		    info.data_section_mem_req[i] == xtmld_load_require_dram1) {
			dram_size += info.data_section_size[i];
			data_memory[i] = dram_addr;
			dram_addr += info.data_section_size[i];
		} else {
			data_size += info.data_section_size[i];
			data_memory[i] = data_addr;
			data_addr += info.data_section_size[i];
		}
	}

	ret_code = xtmld_load(p_mld_lib, code_memory, data_memory,
			      &loader_state, &p_start);
	if ((ret_code == xtmld_success) && (p_start != NULL)) {
		dsp_debug("xtmld_load() succeeded, start_fn = 0x%px\n",
			  p_start);
	} else {
		dsp_err("xtmld_load() failed: %s\n",
			xtmld_error_string(ret_code));
		goto load_failed;
	}

	if (load_op_funcs(buff, &loader_state, op) == 0) {
		return 0;
	}

load_failed:
	dma_free_coherent(dsp->dev, op->op_shared_seg_size,
			  op->op_shared_seg_ptr, op->iova_base);
	op->iova_base = 0;
map_failed:
	op->op_shared_seg_addr = 0;
	op->op_shared_seg_ptr = NULL;
	return -1;
}

int dsp_load_op_file(struct es_dsp *dsp, struct dsp_op_desc *op)
{
	char op_path[256] = { 0 };
	char *lib_buff;
	int file_size;
	int ret = 0;
	char *op_dir = NULL;

	if (strlen(op->name) > 200) {
		dsp_err("dsp operator name %s length is over 200, error.\n",
			op->name);
		return -EINVAL;
	}
	if (op->op_dir) {
		op_dir = op->op_dir;
	} else {
		op_dir = DSP_OP_LIB_DIR;
	}

	sprintf(op_path, "%s%s%s%s", op_dir, "/es_", op->name, ".pkg.lib");
	dsp_debug("start load operator path:%s\n", op_path);
	file_size = get_file_size(op_path);
	if (file_size <= 0) {
		return -EEXIST;
	}
	lib_buff = vmalloc(file_size);
	if (lib_buff == NULL) {
		return -ENOMEM;
	}
	ret = load_op_file(op_path, lib_buff, file_size);
	if (ret == 0) {
		ret = mloader_load_operator(lib_buff, dsp, op);
	}
	vfree(lib_buff);
	return ret;
}
