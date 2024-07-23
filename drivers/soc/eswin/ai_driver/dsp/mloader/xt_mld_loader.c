/*
 * Copyright (c) 2019 by Tensilica Inc. ALL RIGHTS RESERVED.

 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */
#include <linux/string.h>
#include <linux/kernel.h>

#if __xtensa__
#include <xtensa/config/core.h>
#include <xtensa/config/defs.h>
#else
#define xthal_memcpy memcpy
#endif

#include "xt_mld_loader.h"
#include "xt_elf.h"

#if defined(XCHAL_DCACHE_LINESIZE)
#define DCACHE_LINESIZE XCHAL_DCACHE_LINESIZE
#else
#define DCACHE_LINESIZE 1
#endif

#define ADDR64_HI 0xffffffff00000000

xtmld_state_t *xtmld_loaded_lib_info = 0;
/*
 * This function exists only for the debugger to detect when a library
 * has been unloaded (by setting a breakpoint on it.
 *
 * Therefore, if any changes are made to the signature, then the
 * debug module needs a corresponding update.
 *
 * Also, it is critical that this function (and calls to it) not be
 * optimized away.
 */
static void xtmld_unloaded(xtmld_state_t *state)
{
	/* this is not necessary for the operation of the library but it defeats
     * optimization that might break the debug flow.*/
	memset(state, 0, sizeof(xtmld_state_t));
}

typedef struct {
	xtmld_result_code_t status;
	uint32_t number_sections;
	int32_t scratch_section_found;
	uint32_t scratch_section_index;
	Elf32_Phdr *section[XTMLD_MAX_SECTIONS];
} xtmld_validate_t;

#ifndef DISABLE_EXPENSIVE_CHECKS
/*
 These functions are used in checking that sections that require local memory
 are placed correctly.  Impossible to reliably check on architectures with
 address translation so we just return true if there is any local memory.
 */
static int maybe_in_iram(void *addr, uint32_t size)
{
#if XCHAL_NUM_INSTRAM > 0
#if !XCHAL_HAVE_IDENTITY_MAP
	return 1;
#else
	uint32_t x = (uint32_t)addr;
	if ((x >= XCHAL_INSTRAM0_VADDR) &&
	    ((x + size) < (XCHAL_INSTRAM0_VADDR + XCHAL_INSTRAM0_SIZE)))
		return 1;
#if XCHAL_NUM_INSTRAM > 1
	if ((x >= XCHAL_INSTRAM1_VADDR) &&
	    ((x + size) < (XCHAL_INSTRAM1_VADDR + XCHAL_INSTRAM1_SIZE)))
		return 1;
#endif
#endif
#endif
	return 0;
}

#if XCHAL_NUM_DATARAM > 0
static int maybe_in_dram(void *addr, uint32_t size)
{
#if XCHAL_NUM_DATARAM > 0
#if !XCHAL_HAVE_IDENTITY_MAP
	return 1;
#else
	uint32_t x = (uint32_t)addr;
	if ((x >= XCHAL_DATARAM0_VADDR) &&
	    ((x + size) < (XCHAL_DATARAM0_VADDR + XCHAL_DATARAM0_SIZE)))
		return 1;
#if XCHAL_NUM_DATARAM > 1
	if ((x >= XCHAL_DATARAM1_VADDR) &&
	    ((x + size) < (XCHAL_DATARAM1_VADDR + XCHAL_DATARAM1_SIZE)))
		return 1;
#endif
#endif
#endif
	return 0;
}

static int maybe_in_dram0(void *addr, uint32_t size)
{
#if XCHAL_HAVE_DATARAM0
#if !XCHAL_HAVE_IDENTITY_MAP
	return 1;
#else
	uint32_t x = (uint32_t)addr;

	if ((x >= XCHAL_DATARAM0_VADDR) &&
	    ((x + size) < (XCHAL_DATARAM0_VADDR + XCHAL_DATARAM0_SIZE))) {
		return 1;
	}
#endif
#endif
	return 0;
}

static int maybe_in_dram1(void *addr, uint32_t size)
{
#if XCHAL_HAVE_DATARAM1
#if !XCHAL_HAVE_IDENTITY_MAP
	return 1;
#else
	uint32_t x = (uint32_t)addr;

	if ((x >= XCHAL_DATARAM1_VADDR) &&
	    ((x + size) < (XCHAL_DATARAM1_VADDR + XCHAL_DATARAM1_SIZE))) {
		return 1;
	}
#endif
#endif
	return 0;
}
#endif
#endif

/*
 * host_half and host_word are intended to isolate any endian differences
 * between the host processor (the one doing the load) and the target
 * processor (the one executing the loaded library code).
 *
 * Note that host loading is not currently supported or tested.
 */
static Elf32_Half host_half(Elf32_Half v)
{
	return (XTENSA_HOST_BYTE_SWAP) ? (v >> 8) | (v << 8) : v;
}

static Elf32_Word host_word(Elf32_Word v)
{
	if (XTENSA_HOST_BYTE_SWAP) {
		v = ((v & 0x00FF00FF) << 8) | ((v & 0xFF00FF00) >> 8);
		v = (v >> 16) | (v << 16);
	}
	return v;
}

static int verify_magic(Elf32_Ehdr *header)
{
	Elf32_Byte magic_no;

	magic_no = header->e_ident[EI_MAG0];
	if (magic_no != 0x7f) {
		return -1;
	}

	magic_no = header->e_ident[EI_MAG1];
	if (magic_no != 'E') {
		return -1;
	}

	magic_no = header->e_ident[EI_MAG2];
	if (magic_no != 'L') {
		return -1;
	}

	magic_no = header->e_ident[EI_MAG3];
	if (magic_no != 'F') {
		return -1;
	}

	if (header->e_ident[EI_CLASS] != ELFCLASS32)
		return -1;

	return 0;
}

static xtmld_ptr align_ptr(xtmld_ptr ptr, uint32_t t_align, uint32_t t_paddr)
{
	uint32_t align = host_word(t_align);

	// A segment may not start at desired alignment boundary, so calculate the
	// offset from the alignment boundary.
	uint32_t offset = host_word(t_paddr) & (align - 1);

	// To align an address to the boundary, the starting address needs some
	// adjustment before it can be aligned to the desired boundary.
	uint32_t align_adj = align - offset - 1;
	xtmld_ptr ret = ((((uint32_t)ptr + align_adj) & ~(align - 1)) + offset);
#ifndef __XTENSA__
	xtmld_ptr ret_hi = (uint64_t)ptr & ADDR64_HI;
	ret = (xtmld_ptr)((uint64_t)ret_hi | (uint64_t)ret);
#endif
	return ret;
}

static xtmld_result_code_t validate_dynamic(Elf32_Ehdr *header)
{
	if (verify_magic(header) != 0) {
		return xtmld_invalid_library;
	}

	if (host_half(header->e_type) != ET_DYN) {
		return xtmld_invalid_library;
	}

	return xtmld_success;
}

static xtmld_ptr xt_ptr_offs(xtmld_ptr base, Elf32_Word offs)
{
	// return (xtmld_ptr) host_word((uint32_t) base + host_word(offs));
	return (xtmld_ptr)((uint64_t)base + offs);
}

static int32_t is_data_section(Elf32_Phdr *phi)
{
	return (host_word(phi->p_flags) & (PF_X | PF_R | PF_W)) ==
	       (PF_W | PF_R | PF_X);
}

static int32_t is_code_section(Elf32_Phdr *phi)
{
	return ((host_word(phi->p_flags) & (PF_W | PF_R | PF_X)) ==
		(PF_R | PF_X));
}

static int32_t is_scratch_section(Elf32_Phdr *phi)
{
	return ((host_word(phi->p_flags) & (PF_W | PF_R | PF_X)) ==
		(PF_W | PF_X));
}

/* .rodata section is the ELF image we want in a packed library */
xtmld_result_code_t xt_mld_get_packaged_lib(void *packaged_img,
					    xtmld_packaged_lib_t **lib)
{
	Elf32_Ehdr *eheader = (Elf32_Ehdr *)packaged_img;
	Elf32_Shdr *sheader;
	Elf32_Shdr *shi;
	int i;
	char *string_table;

	if (!packaged_img || !lib)
		return xtmld_null_ptr;

	*lib = NULL;

	/* We don't want to deal with unaligned access; ELF image must be 4-byte aligned */
	if ((uint32_t)eheader & 0x3) {
		return xtmld_image_not_aligned;
	}

	/* Is this an ELF? */
	if (verify_magic(eheader) != 0) {
		return xtmld_invalid_library;
	}

	/* Check size of section header entry */
	if (host_half(eheader->e_shentsize) != sizeof(Elf32_Shdr)) {
		return xtmld_invalid_library;
	}

	/* Locate section headers */
	sheader = (Elf32_Shdr *)((char *)eheader + host_word(eheader->e_shoff));

	/* Locate section containing section names */
	shi = sheader + host_half(eheader->e_shstrndx);

	/* Locate section name string table */
	string_table = (char *)eheader + host_word(shi->sh_offset);

	/* Looking for .rodata section */
	for (i = 0; i < host_half(eheader->e_shnum); i++) {
		shi = &sheader[i];

		if (0 ==
		    strcmp(string_table + host_word(shi->sh_name), ".rodata")) {
			*lib = (xtmld_packaged_lib_t *)((char *)eheader +
							host_word(
								shi->sh_offset));
			if (host_word(shi->sh_addralign) != 0 &&
			    (uint32_t)*lib % host_word(shi->sh_addralign) !=
				    0) {
				*lib = NULL;
				return xtmld_image_not_aligned;
			}

			return xtmld_success;
		}
	}

	/* Can't find .rodata */
	return xtmld_invalid_library;
}

static xtmld_validate_t validate_dynamic_multload(Elf32_Ehdr *header)
{
	Elf32_Phdr *pheader;
	xtmld_validate_t rv;
	memset(&rv, 0, sizeof(rv));

	rv.status = validate_dynamic(header);

	if (rv.status != xtmld_success)
		return rv;

	/* make sure it's split load pi library, expecting three headers,
     code, data and dynamic, for example:

     LOAD off    0x00000094 vaddr 0x00000000 paddr 0x00000000 align 2**0
     filesz 0x00000081 memsz 0x00000081 flags r-x
     LOAD off    0x00000124 vaddr 0x00000084 paddr 0x00000084 align 2**0
     filesz 0x000001ab memsz 0x000011bc flags rwx
     DYNAMIC off    0x00000124 vaddr 0x00000084 paddr 0x00000084 align 2**2
     filesz 0x000000a0 memsz 0x000000a0 flags rw-

     */

	int dynamic = 0;

	/* minimum segment count is 3, Segments for .text section, dynamic data and
    reloaction data.  Segments with PT_NULL type don't count.  */
	if (host_half(header->e_phnum) < 3) {
		rv.status = xtmld_invalid_library;
		return rv;
	}

	pheader = (Elf32_Phdr *)((char *)header + host_word(header->e_phoff));

	int i;
	uint32_t number_code_sections = 0;
	uint32_t number_data_sections = 0;
	for (i = 0; i < header->e_phnum; i++) {
		Elf32_Phdr *phi = &pheader[i];
		/* LOAD R-X */
		Dprintf("%d %x %x\n", i, phi->p_flags, phi->p_memsz);

		if (host_word(phi->p_type) == PT_LOAD && (phi->p_memsz > 0) &&
		    (host_word(phi->p_flags) & (PF_R | PF_W | PF_X)) ==
			    (PF_R | PF_X)) {
			number_code_sections++;
			if (number_code_sections <= XTMLD_MAX_CODE_SECTIONS) {
				rv.section[rv.number_sections++] = phi;
			} else {
				break;
			}
		}

		/* LOAD RWX */
		if (host_word(phi->p_type) == PT_LOAD && (phi->p_memsz > 0) &&
		    (host_word(phi->p_flags) & (PF_R | PF_W | PF_X)) ==
			    (PF_R | PF_W | PF_X)) {
			number_data_sections++;
			if (number_data_sections <= XTMLD_MAX_DATA_SECTIONS) {
				rv.section[rv.number_sections++] = phi;
			} else {
				break;
			}
		}

		/* LOAD RWX */
		if (host_word(phi->p_type) == PT_LOAD && (phi->p_memsz > 0) &&
		    is_scratch_section(phi)) {
			if (!rv.scratch_section_found) {
				rv.scratch_section_found = 1;
				rv.scratch_section_index = rv.number_sections;
				Dprintf("scratch section found\n");
				rv.section[rv.number_sections++] = phi;
			} else {
				break;
			}
		}

		/* DYNAMIC RW- */
		if (host_word(phi->p_type) == PT_DYNAMIC &&
		    (host_word(phi->p_flags) & (PF_R | PF_W | PF_X)) ==
			    (PF_R | PF_W)) {
			dynamic++;
		}
	}
	Dprintf("number of data sections %d\n", number_data_sections);
	if (dynamic && (number_data_sections > 0) &&
	    (number_data_sections <= XTMLD_MAX_DATA_SECTIONS) &&
	    (number_code_sections > 0) &&
	    (number_code_sections <= XTMLD_MAX_CODE_SECTIONS))
		rv.status = xtmld_success;
	else
		rv.status = xtmld_invalid_library;
	return rv;
}

static Elf32_Dyn *find_dynamic_info(Elf32_Ehdr *eheader, uint32_t *size)
{
	char *base_addr = (char *)eheader;

	Elf32_Phdr *pheader =
		(Elf32_Phdr *)(base_addr + host_word(eheader->e_phoff));

	int seg = 0;
	int num = host_half(eheader->e_phnum);

	while (seg < num) {
		if (host_word(pheader[seg].p_type) == PT_DYNAMIC) {
			*size = host_word(pheader[seg].p_memsz);
			return (Elf32_Dyn *)(base_addr +
					     host_word(pheader[seg].p_offset));
		}
		seg++;
	}
	return 0;
}

/*
 * This function finds the memory requirements data for the code
 * and data sections which is embedded into the dynamic section
 * of the packaged library elf file.
 */
static xtmld_result_code_t get_mem_perf(Elf32_Ehdr *eheader,
					xtmld_mem_require_t *data_performance,
					xtmld_mem_require_t *code_performance)
{
	int i;
	int count = 0;
	int data_found = 0;
	int code_found = 0;
	uint32_t *ptr;
	uint32_t size;
	Elf32_Dyn *dyn_entry = find_dynamic_info(eheader, &size);

	if (dyn_entry == 0) {
		return xtmld_invalid_library;
	}

	while (dyn_entry->d_tag != DT_NULL) {
		dyn_entry++;
	}

	ptr = (uint32_t *)dyn_entry;

	while (count++ < size / sizeof(uint32_t)) {
		if (*(ptr++) == 0x70000002) {
			int dcount;
			for (dcount = 0;
			     dcount + count < size / sizeof(uint32_t); dcount++)
				if (*(ptr + dcount) == XTMLD_DATA_MARKER) {
					for (i = 0; i < XTMLD_MAX_DATA_SECTIONS;
					     i++)
						data_performance[i] =
							*(ptr + dcount + i + 1);
					data_found = 1;
					break;
				}
			for (dcount = 0;
			     dcount + count < size / sizeof(uint32_t); dcount++)
				if (*(ptr + dcount) == XTMLD_CODE_MARKER) {
					for (i = 0; i < XTMLD_MAX_CODE_SECTIONS;
					     i++)
						code_performance[i] =
							*(ptr + dcount + i + 1);
					code_found = 1;
					break;
				}
			break;
		}
	}
	{
		/* If either .dram0.data or .dram1.data sections exist, force the corresponding
           requirements to require dataram0 or dataram1.
         */
		/* Locate section headers */
		Elf32_Shdr *shdr = (Elf32_Shdr *)((char *)eheader +
						  host_word(eheader->e_shoff));
		/* Locate section containing section names */
		Elf32_Shdr *sstr = shdr + host_half(eheader->e_shstrndx);
		/* Locate name string table */
		char *name_table = (char *)eheader + host_word(sstr->sh_offset);
		/* Locate segment headers */
		Elf32_Phdr *phdr = (Elf32_Phdr *)((char *)eheader +
						  host_word(eheader->e_phoff));
		uint32_t ndx = 0;

		/* Iterate over all the program segment headers */
		for (i = 0; i < eheader->e_phnum; i++) {
			if ((host_word(phdr[i].p_type) == PT_LOAD) &&
			    (phdr[i].p_memsz > 0)) {
				if ((host_word(phdr[i].p_flags) &
				     (PF_R | PF_W | PF_X)) ==
				    (PF_R | PF_W | PF_X)) {
					/* Found a data segment, now find first section in segment */
					/* .dram0.data and .dram1.data will be first and only section in segment */
					int32_t j;

					for (j = 0;
					     j < host_half(eheader->e_shnum);
					     j++) {
						/* First section's vaddr should match program segment vaddr */
						if (shdr[j].sh_addr ==
						    phdr[i].p_vaddr) {
							if (strcmp(name_table +
									   host_word(
										   shdr[j].sh_name),
								   ".dram0.data") ==
							    0) {
								data_performance[ndx] =
									xtmld_load_require_dram0;
							} else if (
								strcmp(name_table +
									       host_word(
										       shdr[j].sh_name),
								       ".dram1.data") ==
								0) {
								data_performance[ndx] =
									xtmld_load_require_dram1;
							}
						}
					}
					ndx++;
				}
			}
		}

		return xtmld_success;
	}

	return xtmld_mem_require_not_found;
}

/*
    This function searches the dynamic info section for the library name
    (used for debugging purposes).  If no library name is found then a default
    name is created.
 */
static void get_libname(Elf32_Ehdr *eheader, xtmld_state_t *state)
{
	const char *default_name = "loadable_lib_";
	static int libcount = 0;
	int count = 0;
	char *ptr;
	uint32_t size;
	const char *prefix = "xtensalibname:";
	uint32_t prefix_len = strlen(prefix);
	Elf32_Dyn *dyn_entry = find_dynamic_info(eheader, &size);

	while (dyn_entry->d_tag != DT_NULL) {
		dyn_entry++;
	}

	ptr = (char *)dyn_entry;

	while (count++ < size) {
		if (!strncmp(ptr, prefix, prefix_len)) {
			char *str = ptr + prefix_len;
			strncpy(state->libname, str,
				sizeof(state->libname) - 1);
			return;
		}
		ptr++;
	}
	snprintf(state->libname, sizeof(state->libname) - 1, "%s",
		 default_name);
	state->libname[strlen(default_name)] = 'a' + libcount++;
}

static xtmld_result_code_t get_dyn_info(Elf32_Ehdr *eheader,
					xtmld_validate_t *val,
					xtmld_ptr *dest_addr,
					xtmld_state_t *state)
{
	unsigned int jmprel = 0;
	unsigned int pltrelsz = 0;
	uint32_t size;
	Elf32_Dyn *dyn_entry = find_dynamic_info(eheader, &size);

	if (dyn_entry == 0) {
		return xtmld_invalid_library;
	}

	int i;
	int j;
	state->scratch_section_found = val->scratch_section_found;
	// Initialize the state structure and find the _start symbol in the
	// library
	for (i = 0; i < val->number_sections; i++) {
		state->dst_addr[i] = dest_addr[i];
		state->src_offs[i] = host_word(val->section[i]->p_paddr);
		state->size[i] = val->section[i]->p_memsz;
		if (is_code_section(val->section[i]) &&
		    (eheader->e_entry >= val->section[i]->p_paddr) &&
		    (eheader->e_entry <
		     val->section[i]->p_paddr + val->section[i]->p_memsz)) {
			state->start_sym =
				(xtmld_start_fn_ptr)(eheader->e_entry +
						     (uint32_t)state
							     ->dst_addr[i] -
						     val->section[i]->p_paddr);
		}
	}

	state->num_sections = val->number_sections;

	Dprintf("get_dyn_info: start address = %p\n", state->start_sym);

	while (dyn_entry->d_tag != DT_NULL) {
		switch ((Elf32_Sword)host_word((Elf32_Word)dyn_entry->d_tag)) {
		case DT_RELA:
			Dprintf("in case DT_RELA with offset = %d and ptr = %0x\n",
				val->section[0]->p_offset,
				dyn_entry->d_un.d_ptr);
			if (val->scratch_section_found) {
				int32_t si = val->scratch_section_index;
				state->rel = xt_ptr_offs(
					eheader,
					val->section[si]->p_offset +
						dyn_entry->d_un.d_ptr -
						val->section[si]->p_vaddr);
			} else {
				/* d_un.ptr is offset from the the first section (always section[0] */
				state->rel = xt_ptr_offs(
					eheader, val->section[0]->p_offset +
							 dyn_entry->d_un.d_ptr);
			}
			break;
		case DT_RELASZ:
			state->rela_count =
				host_word(host_word(dyn_entry->d_un.d_val) /
					  sizeof(Elf32_Rela));
			break;
		case DT_INIT:
			for (i = 0; i < val->number_sections; i++)
				if (is_code_section(val->section[i]) &&
				    (val->section[i]->p_paddr <=
				     dyn_entry->d_un.d_ptr) &&
				    (dyn_entry->d_un.d_ptr <
				     val->section[i]->p_paddr +
					     val->section[i]->p_memsz)) {
					state->init = xt_ptr_offs(
						state->dst_addr[i] -
							val->section[i]->p_paddr,
						dyn_entry->d_un.d_ptr);
					Dprintf("init function found = %p\n",
						state->init);
					break;
				}
			break;
		case DT_FINI:
			if (is_code_section(val->section[i]) &&
			    (val->section[i]->p_paddr <=
			     dyn_entry->d_un.d_ptr) &&
			    (dyn_entry->d_un.d_ptr <
			     val->section[i]->p_paddr +
				     val->section[i]->p_memsz)) {
				state->fini = xt_ptr_offs(
					state->dst_addr[i] -
						val->section[i]->p_paddr,
					dyn_entry->d_un.d_ptr);
				Dprintf("fini function found = %p\n",
					state->fini);
				break;
			}
			break;
		case DT_HASH:
			for (i = 0, j = 0; i < val->number_sections; i++)
				if (is_data_section(val->section[i]))
					state->hash[j++] = xt_ptr_offs(
						state->dst_addr[i] -
							val->section[i]->p_paddr,
						dyn_entry->d_un.d_ptr);
			break;
		case DT_SYMTAB:
			break;
		case DT_STRTAB:
			break;
		case DT_JMPREL:
			jmprel = dyn_entry->d_un.d_val;
			break;
		case DT_PLTRELSZ:
			pltrelsz = dyn_entry->d_un.d_val;
			break;
		case DT_LOPROC + 2:
			for (i = 0, j = 0; i < val->number_sections; i++) {
				state->text_addr[j++] = xt_ptr_offs(
					state->dst_addr[i] -
						val->section[i]->p_paddr,
					dyn_entry->d_un.d_ptr);
				pr_debug("DT_LOPROC dyn text_addr:%0x",
					 (uint32_t)state->text_addr[j]);
			}
			break;

		default:
			/* do nothing */
			break;
		}
		dyn_entry++;
	}
	return xtmld_success;
}

static void xtmld_sync(void)
{
	/* Synchronize caches and memory. We've just loaded code and possibly
     patched some of it. All changes need to be flushed out of dcache
     and the corresponding sections need to be invalidated in icache.
     */
#ifdef __XTENSA__
	/* we don't know exactly how much to writeback and/or invalidate
     so do all. Possible optimization later.
     */
	xthal_dcache_all_writeback();
	xthal_icache_all_invalidate();
	xthal_clear_regcached_code();
#endif
}

static xtmld_result_code_t xtmld_target_init_library(xtmld_state_t *lib_info)
{
	xtmld_result_code_t rv = xtmld_relocate_library(lib_info);
	if (rv != xtmld_success) {
		return rv;
	}

	xtmld_sync();
#ifdef __XTENSA__
	((void (*)(void))(lib_info->init))();
#endif
	return xtmld_success;
}

/*
 * load_section does the work of actually loading each section into memory.
 */
static void load_section(Elf32_Phdr *pheader, xtmld_ptr src_addr,
			 xtmld_ptr dst_addr, memcpy_func_ex mcpy,
			 memset_func_ex mset, void *user)
{
	Elf32_Word bytes_to_copy = host_word(pheader->p_filesz);
	Elf32_Word bytes_to_zero = host_word(pheader->p_memsz) - bytes_to_copy;

	xtmld_ptr zero_addr = dst_addr + bytes_to_copy;
	if (bytes_to_copy > 0) {
		memcpy(dst_addr, src_addr, bytes_to_copy);
	}
	/* this zeros the bss area */
	if (bytes_to_zero > 0) {
		memset(zero_addr, 0, bytes_to_zero);
	}
	/* The dcache needs to be written back and the icache needs to
     be invalidated, but that happens in xtmld_sync() so we don't
     need to do it here.
     */
}

/*
 * xtmld_load_sections() does the loading of a library into memory.
 */
static xtmld_result_code_t
xtmld_load_sections(xtmld_packaged_lib_t *library, xtmld_state_t *lib_info,
		    int32_t num, Elf32_Phdr **ph, xtmld_ptr *dest_addr,
		    memcpy_func_ex mcpy_fn, memset_func_ex mset_fn, void *user)
{
	int32_t i;
	for (i = 0; i < num; i++) {
		Elf32_Phdr *phi = ph[i];
		if (is_code_section(phi) /* || is_data_section(phi)*/)
			load_section(phi,
				     (uint8_t *)library +
					     host_word(phi->p_offset),
				     dest_addr[i], mcpy_fn, mset_fn, user);
	}
	return xtmld_success;
}

/*
 *
 * This function is used by the debug component, so if you change its signature
 * then corresponding changes need to be made to the debug module.
 */
static xtmld_result_code_t
xtmld_load_mlib_break(xtmld_packaged_lib_t *library, xtmld_state_t *lib_info,
		      int32_t num, Elf32_Phdr **ph, xtmld_ptr *dest_addr,
		      memcpy_func_ex mcpy_fn, memset_func_ex mset_fn,
		      void *user)
{
	return xtmld_success;
}

/*
 * xtmld_load_common is the common root between xtmld_load and
 * xtmld_load_custom_fn.
 *
 * 1) It aligns the section addresses,
 * 2) checks for
 *              a) overlapping sections
 *              b) code sections out of the 1-GB range
 *              c) sections that require local memory but are being loaded into
 *                      system memory.
 * 3) loads the library sections into memory
 */

static xtmld_result_code_t
xtmld_load_common(xtmld_packaged_lib_t *library,
		  xtmld_code_ptr *destination_code_address,
		  xtmld_data_ptr *destination_data_address,
		  xtmld_state_t *state, memcpy_func_ex mcpy_fn,
		  memset_func_ex mset_fn, void *user, xtmld_start_fn_ptr *start)
{
	if (!library || !destination_code_address ||
	    !destination_data_address || !state || !mcpy_fn || !mset_fn ||
	    !start) {
		return xtmld_null_ptr;
	}

	{
		int32_t i;
		int32_t j = 0;
		Elf32_Ehdr *header = (Elf32_Ehdr *)library;
		xtmld_library_info_t info;

		xtmld_result_code_t status;
		xtmld_ptr addr[XTMLD_MAX_SECTIONS];

		xtmld_validate_t val = validate_dynamic_multload(header);
		if (val.status != xtmld_success)
			return val.status;
		status = xtmld_library_info(library, &info);
		if (status != xtmld_success)
			return status;
		for (i = 0; i < info.number_of_code_sections; i++)
			addr[j++] = align_ptr(destination_code_address[i],
					      val.section[i]->p_align,
					      val.section[i]->p_paddr);
		for (i = 0; i < info.number_of_data_sections; i++)
			addr[j++] = align_ptr(
				destination_data_address[i],
				val.section[i + info.number_of_code_sections]
					->p_align,
				val.section[i + info.number_of_code_sections]
					->p_paddr);
/*
 * Doubtful that these checks are very expensive in practice, but
 * they are set up to be easily disabled.
 */
#ifndef DISABLE_EXPENSIVE_CHECKS
		{
			int j;
			uint32_t size[XTMLD_MAX_CODE_SECTIONS +
				      XTMLD_MAX_DATA_SECTIONS];

			/* Check that any section requiring local memory is actually in
             * local memory
             */
			for (i = 0; i < info.number_of_code_sections; i++) {
				size[i] = info.code_section_size[i];
				if ((info.code_section_mem_req[i] ==
				     xtmld_load_require_localmem) &&
				    !(maybe_in_iram(destination_code_address[i],
						    info.code_section_size[i])))
					return xtmld_local_memory_required;
			}
			for (i = 0; i < info.number_of_data_sections; i++) {
				size[i + info.number_of_code_sections] =
					info.data_section_size[i];
#if (XCHAL_NUM_DATARAM > 0)
				if ((info.data_section_mem_req[i] ==
				     xtmld_load_require_localmem) &&
				    !(maybe_in_dram(
					    destination_data_address[i],
					    info.data_section_size[i]))) {
					return xtmld_local_memory_required;
				} else if ((info.data_section_mem_req[i] ==
					    xtmld_load_require_dram0) &&
					   !(maybe_in_dram0(
						   destination_data_address[i],
						   info.data_section_size[i]))) {
					return xtmld_local_memory_required;
				} else if ((info.data_section_mem_req[i] ==
					    xtmld_load_require_dram1) &&
					   !(maybe_in_dram1(
						   destination_data_address[i],
						   info.data_section_size[i]))) {
					return xtmld_local_memory_required;
				}
#endif
			}
#if !(XCHAL_HAVE_XEA3 || defined(__XTENSA_CALL0_ABI__))
			/*
             * Check that code sections all start in the same 1-GB range
             */
			/*           {
                           void* base = &xtmld_load_common;
                           uint32_t gig_addr_start = ((uint32_t) base) & 0xc0000000;
                           for (i = 0; i < info.number_of_code_sections; i++)
                               if (((((uint32_t) destination_code_address[i]) & 0xc0000000)
                                       != gig_addr_start) && info.code_section_size[i])
                                   return xtmld_code_must_be_in_1gig_region;
                       }*/
#endif
			/* Check that none of the sections overlap */
			for (i = 1; i < info.number_of_code_sections +
						info.number_of_data_sections;
			     i++)
				for (j = 0; j < 1; j++)
					if (((addr[i] <= addr[j]) &&
					     (addr[i] +
						      size[i] /
							      sizeof(xtmld_ptr) >=
					      addr[j])) ||
					    ((addr[j] <= addr[i]) &&
					     (addr[j] +
						      size[j] /
							      sizeof(xtmld_ptr) >=
					      addr[i])))
						return xtmld_sections_overlap;
		}
#endif

		status = xtmld_load_sections(library, state,
					     val.number_sections, val.section,
					     addr, mcpy_fn, mset_fn, user);
		if (status != xtmld_success)
			return status;

		status = get_dyn_info(header, &val, addr, state);
		if (status != xtmld_success)
			return status;

		state->num_text_sections = info.number_of_code_sections;

		get_libname(header, state);

		state->next = xtmld_loaded_lib_info;
		xtmld_loaded_lib_info = state;

		if (status != xtmld_success)
			return status;

		*start = (xtmld_start_fn_ptr)host_word(
			(Elf32_Word)state->start_sym);
		Dprintf("code loaded ... start = %p\n", *start);

		status = xtmld_load_mlib_break(library, state,
					       val.number_sections, val.section,
					       addr, mcpy_fn, mset_fn, user);

		return xtmld_success;
	}
}

/*
 * The function xtmld_library_info() populates the info argument with the
 * numbers, sizes, and load requirements for the library
 *
 */
xtmld_result_code_t xtmld_library_info(xtmld_packaged_lib_t *library,
				       xtmld_library_info_t *info)
{
	Elf32_Phdr *pheader;
	Elf32_Ehdr *header = (Elf32_Ehdr *)library;
	xtmld_result_code_t status;
	xtmld_validate_t val;

	int align;
	int i;

	if ((library == 0) || (info == 0))
		return xtmld_null_ptr;

	val = validate_dynamic_multload(header);

	if (val.status != xtmld_success) {
		return val.status;
	}

	pheader =
		(Elf32_Phdr *)((uint8_t *)library + host_word(header->e_phoff));

	memset(info, 0, sizeof(*info));

	status = get_mem_perf(header, info->data_section_mem_req,
			      info->code_section_mem_req);

	if (status != xtmld_success)
		return status;
	/*
     the size calculation assumes the starting address of destination buffer may
     not be aligned to the required alignment, so extra space is needed in order to
     maintain the same alignment as the source data.  Besides this, the source data
     may not start at the alignment boundary, so another extra space is needed.  At the
     end of the buffer, some padding may also necessary if the buffer is not ended at
     the alignment boundary, in order to safe guard the write operation.
     So, the total size is in multiple of alignment.
     */
	for (i = 0; i < val.number_sections; i++) {
		align = host_word(val.section[i]->p_align);
		if (is_code_section(val.section[i])) {
			info->code_section_size[info->number_of_code_sections] =
				(align + host_word(val.section[i]->p_memsz) +
				 align - 1) &
				(~(align - 1));
			//      info->code_section_alignment[info->number_of_code_sections] =
			//             align;

			info->number_of_code_sections++;
		} else if (is_data_section(val.section[i])) {
			align = DCACHE_LINESIZE > align ? DCACHE_LINESIZE :
								align;
			info->data_section_size[info->number_of_data_sections] =
				(align + host_word(val.section[i]->p_memsz) +
				 align - 1) &
				(~(align - 1));
			//         info->data_section_alignment[info->number_of_data_sections] =
			//            align;
			info->number_of_data_sections++;
		}
	}
	return xtmld_success;
}

/*
 * The function xtmld_load() loads the library into the sections
 * supplied, call the library init function (C++ constructors
 * for static objects), and sets the start function point to point
 * to the _start function() of the supplied library.
 */
xtmld_result_code_t xtmld_load(xtmld_packaged_lib_t *lib,
			       xtmld_code_ptr *code_section_la,
			       xtmld_data_ptr *data_section_la,
			       xtmld_state_t *state, xtmld_start_fn_ptr *start)
{
	return xtmld_load_custom_fn(lib, code_section_la, data_section_la,
				    state, (memcpy_func_ex)xthal_memcpy,
				    (memset_func_ex)memset, 0, start);
}

/*
 * xtmld_load_custom_fn is the same as xtmld_load except that it has
 * additional arguments for custom memory copy and set functions. The
 * parameter user, is passed to the custom functions.
 */
xtmld_result_code_t xtmld_load_custom_fn(
	xtmld_packaged_lib_t *library, xtmld_code_ptr *destination_code_address,
	xtmld_data_ptr *destination_data_address, xtmld_state_t *lib_info,
	memcpy_func_ex mcpy_fn, memset_func_ex mset_fn, void *user,
	xtmld_start_fn_ptr *start)
{
	xtmld_result_code_t result;

	result = xtmld_load_common(library, destination_code_address,
				   destination_data_address, lib_info, mcpy_fn,
				   mset_fn, user, start);

	if (result == xtmld_success)
		return xtmld_target_init_library(lib_info);
	else
		return result;
}

/*
 * This function unloads a library.  The state argument must be the
 * same as passed to the load function.
 */
xtmld_result_code_t xtmld_unload(xtmld_state_t *state)
{
	int found = 0;
	if (!state)
		return xtmld_null_ptr;
	if (state == xtmld_loaded_lib_info) {
		xtmld_loaded_lib_info = state->next;
		found = 1;
	} else {
		xtmld_state_t *pptr = xtmld_loaded_lib_info;
		xtmld_state_t *ptr = pptr->next;
		while (ptr) {
			if (state == ptr) {
				pptr->next = ptr->next;
				found = 1;
				break;
			} else {
				pptr = ptr;
				ptr = pptr->next;
			}
		}
	}
	if (!found)
		return xtmld_invalid_library;
#ifdef __XTENSA__
	((void (*)(void))(state->fini))();
#endif
	xtmld_unloaded(state);
	return xtmld_success;
}

/*
 * This function writes information about the packaged library into
 * the info structure.
 */
const char *xtmld_error_string(xtmld_result_code_t x)
{
	switch (x) {
	case xtmld_success:
		return "No Error.";
	case xtmld_null_ptr:
		return "Function improperly called with NULL pointer.";
	case xtmld_invalid_library:
		return "Invalid library format.";
	case xtmld_internal_error:
		return "Internal error.";
	case xtmld_local_memory_required:
		return "Local memory was required for some library sections, but it was not provided.";
	case xtmld_mem_require_not_found:
		return "Memory requirements not found. Use XTMLD_CODE_MEM_REQ and XTMLD_DATA_MEM_REQ macros to specify "
		       "memory requirements for library.";
	case xtmld_code_must_be_in_1gig_region:
		return "Attempt to load code outside 1-gig range.";
	case xtmld_sections_overlap:
		return "Section addresses overlap.";
	case xtmld_image_not_aligned:
		return "Library is not properly aligned.";
	default:
		return "Unknown error.";
	}
}
