/*
 Copyright (c) 2019 Cadence Design Systems Inc.

 Permission is hereby granted, free of charge, to any person obtaining
 a copy of this software and associated documentation files (the
 "Software"), to deal in the Software without restriction, including
 without limitation the rights to use, copy, modify, merge, publish,
 distribute, sublicense, and/or sell copies of the Software, and to
 permit persons to whom the Software is furnished to do so, subject to
 the following conditions:

 The above copyright notice and this permission notice shall be included
 in all copies or substantial portions of the Software.

 THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
 CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#include <linux/kernel.h>
#include "xt_elf.h"
#include "xt_mld_loader.h"
#include "core-const16w.inc"

#define BPW (8 * sizeof(Elf32_Word))
#define DIV_ROUNDUP(a, b) (((a) + (b)-1) / (b))

/* Define the minimum separation between segments.  This must be > than
 * the largest indirect with L32I.
 */
#define MIN_SEPARATION 0x400

static unsigned int XT_SSA8L(unsigned int s)
{
	return ((((s) << 3) & 0x18U));
}

static unsigned int XT_SSA8B(unsigned int s)
{
	return ((0x20U) - ((((s) << 3) & 0x18U))) & 0x3fU;
}

static unsigned int XT_SRC(unsigned int s, unsigned int t, unsigned int SAR)
{
	unsigned int r__out = 0;

	unsigned int tn0__o0;
	unsigned int tn4__o0[2];
	unsigned int tn4__i0[2];
	tn0__o0 = SAR;
	tn4__i0[0] = ((t));
	tn4__i0[1] = ((s));
	{
		unsigned bit__shift = tn0__o0 & 0x1fU;
		unsigned word__shift = tn0__o0 >> 5;
		if (bit__shift == 0) {
			tn4__o0[0] = (tn4__i0[word__shift]);
		} else {
			unsigned int rshift__ = 32 - bit__shift;
			unsigned int tmp__;
			unsigned int prev__;
			prev__ = (word__shift > 0) ? 0 : tn4__i0[1];
			tmp__ = tn4__i0[word__shift];
			tn4__o0[0] =
				((tmp__ >> bit__shift) | (prev__ << rshift__));
		}
	}
	r__out = ((tn4__o0[0]));
	return r__out;
}
static inline uint32_t load_32(xtmld_ptr addr)
{
	uint32_t *paddr = (uint32_t *)addr;
	return (uint32_t)*paddr;
}

static inline void store_32(xtmld_ptr addr, uint32_t value)
{
	uint32_t *paddr = (uint32_t *)addr;
	*paddr = value;
}

/* Extract word from {lo, hi} pair at byte offset 'ofs'. This is equivalent
 of unaligned load from aligned memory region {lo, hi}. */

static inline uint32_t extract(uint32_t lo, uint32_t hi, int32_t ofs)
{
#ifdef __XTENSA_EB__
	unsigned int SAR = XT_SSA8B(ofs);
	return XT_SRC(lo, hi, SAR);
#else
	unsigned int SAR = XT_SSA8L(ofs);
	return XT_SRC(hi, lo, SAR);
#endif
}

/* Sets word at byte offset 'ofs' in the pair {lo, hi} to 'value'. This is equivalent
 of unaligned store into aligned memory region {lo, hi}. */

static inline void combine(uint32_t *lo, uint32_t *hi, uint32_t value,
			   int32_t ofs)
{
	uint32_t x0, x1;
#ifdef __XTENSA_EB__
	unsigned int SAR = XT_SSA8B(ofs);
	x0 = XT_SRC(*lo, *lo, SAR);
	x1 = XT_SRC(*hi, *hi, SAR);
	SAR = XT_SSA8L(ofs);
	*lo = XT_SRC(x0, value, SAR);
	*hi = XT_SRC(value, x1, SAR);
#else
	unsigned int SAR = XT_SSA8L(ofs);
	x0 = XT_SRC(*lo, *lo, SAR);
	x1 = XT_SRC(*hi, *hi, SAR);
	SAR = XT_SSA8B(ofs);
	*lo = XT_SRC(value, x0, SAR);
	*hi = XT_SRC(x1, value, SAR);
#endif
}

/* Helper functions for copying instructions between IRAM and word aligned buffer.
 */

/* copy_in copies 'bits' number of bits starting from word aligned pointer 'src' at
 byte offset 'src_ofs' into word-aligned buffer 'buf'.
 The actual number of bytes copied is higher, this helps to write this buffer back
 without need to read data from IRAM again. The actual 'src':'src_pos' data starts
 from word dst[1]. Th word dst[0] holds data from the start of 'src' up to 'src_pos'
 byte.
 */
static inline void copy_in(Elf32_Word *dst, Elf32_Word *src, int src_ofs,
			   int32_t bits)
{
	Elf32_Word copied;
	Elf32_Word count = DIV_ROUNDUP(bits + 8 * src_ofs, BPW);
	unsigned int SAR = 0;

	if (src_ofs == 0) { /* special case - source is word-aligned */
		dst++;
		for (copied = 0; copied < count; copied++)
			*dst++ = load_32(src++);
	} else {
		Elf32_Word last = 0;
		Elf32_Word curr;
#ifdef __XTENSA_EB__
		SAR = XT_SSA8B(src_ofs);
		for (copied = 0; copied < count; copied++) {
			*dst++ = XT_SRC(last, (curr = load_32(src++), SAR));
			last = curr;
		}
#else
		SAR = XT_SSA8L(src_ofs);
		for (copied = 0; copied < count; copied++) {
			*dst++ = XT_SRC((curr = load_32(src++)), last, SAR);
			last = curr;
		}
#endif
		*dst = XT_SRC(last, last, SAR);
	}
}

/* copy_out writes data buffered using copy_in back to IRAM.
 'src_ofs' and 'bits' has to be the same as in copy_in call
 but 'dst' now is the 'src' from copy_in and 'src' in copy_out
 is the 'dst' from copy_in. */

static inline void copy_out(Elf32_Word *dst, Elf32_Word *src, int32_t src_ofs,
			    int32_t bits)
{
	Elf32_Word copied;
	Elf32_Word count = DIV_ROUNDUP(bits + 8 * src_ofs, BPW);
	unsigned int SAR = 0;

	if (src_ofs == 0) { /* special case - desctionation is word-aligned */
		src++;
		for (copied = 0; copied < count; copied++)
			store_32(dst++, *src++);
	} else {
		Elf32_Word last = *src++;
		Elf32_Word curr;
#ifdef __XTENSA_EB__
		SAR = XT_SSA8L(src_ofs);
		for (copied = 0; copied < count; copied++) {
			store_32(dst++, XT_SRC(last, (curr = *src++), SAR));
			last = curr;
		}
#else
		SAR = XT_SSA8B(src_ofs);
		for (copied = 0; copied < count; copied++) {
			store_32(dst++, XT_SRC((curr = *src++), last, SAR));
			last = curr;
		}
#endif
	}
}

static xtmld_result_code_t relocate_op(char *ptr, int slot, Elf32_Word value)
{
	Elf32_Word format_length, a_ofs, mem_lo, mem_hi, insn;
	uint32_t *a_ptr;

	// To decode the instruction length we need to read up to 4 bytes
	// of the instruction from memory. We cannot pass "ptr" as is to
	// the decode function because it may be unaligned, and iram will
	// not allow unaligned access. So we will read two words aligned
	// and then merge them to get the first 4 bytes (if the insn is
	// less than 4 bytes we get some garbage at the end but it does
	// not matter.

	a_ofs = (uint32_t)ptr % sizeof(uint32_t);
#ifdef __XTENSA__
	a_ptr = (uint32_t *)((uint32_t)ptr - a_ofs);
#else
	a_ptr = (uint32_t *)((uint64_t)ptr - a_ofs);
#endif

	mem_lo = load_32(a_ptr);
	mem_hi = load_32(a_ptr + 1);
#ifdef __XTENSA_EB__
	switch (a_ofs) {
	case 0:
		insn = mem_lo;
		break;
	case 1:
		insn = (mem_lo << 8) | (mem_hi >> 24);
		break;
	case 2:
		insn = (mem_lo << 16) | (mem_hi >> 16);
		break;
	case 3:
		insn = (mem_lo << 24) | (mem_hi >> 8);
		break;
	default:
		return xtmld_internal_error;
	}
#else
	switch (a_ofs) {
	case 0:
		insn = mem_lo;
		break;
	case 1:
		insn = (mem_lo >> 8) | (mem_hi << 24);
		break;
	case 2:
		insn = (mem_lo >> 16) | (mem_hi << 16);
		break;
	case 3:
		insn = (mem_lo >> 24) | (mem_hi << 8);
		break;
	default:
		return xtmld_internal_error;
	}
#endif

	format_length = xtimm_get_format_length(insn);
	if (format_length == 0)
		return xtmld_internal_error;

	if (format_length <= 32) {
		if (format_length + 8 * a_ofs <= 32) {
			if (!xtimm_fmt_small_immediate_field_set(&insn, slot,
								 value))
				return xtmld_internal_error;
			combine(&mem_lo, &mem_hi, insn, a_ofs);
			store_32(a_ptr, mem_lo);
		} else {
			mem_hi = load_32(a_ptr + 1);
			insn = extract(mem_lo, mem_hi, a_ofs);
			if (!xtimm_fmt_small_immediate_field_set(&insn, slot,
								 value))
				return xtmld_internal_error;
			combine(&mem_lo, &mem_hi, insn, a_ofs);
			store_32(a_ptr, mem_lo);
			store_32(a_ptr + 1, mem_hi);
		}
	} else {
		Elf32_Word buf[DIV_ROUNDUP(XTIMM_MAX_FORMAT_LENGTH, BPW) + 2];
		copy_in(buf, a_ptr, a_ofs, format_length);
		if (!xtimm_fmt_large_immediate_field_set(&buf[1], slot, value))
			return xtmld_internal_error;
		copy_out(a_ptr, buf, a_ofs, format_length);
	}
	return xtmld_success;
}

static xtmld_result_code_t reloc_addr(const xtmld_state_t *lib_info,
				      Elf32_Addr addr, xtmld_ptr *relocation)
{
	int i;
	uint32_t sep = lib_info->scratch_section_found ? MIN_SEPARATION : 0;
	for (i = 0; i < lib_info->num_sections; i++) {
		if ((addr + sep >= lib_info->src_offs[i]) &&
		    (addr < lib_info->src_offs[i] + lib_info->size[i] + sep)) {
			*relocation =
				(xtmld_ptr)((Elf_Addr)lib_info->dst_addr[i]) -
				lib_info->src_offs[i] + addr;
			return xtmld_success;
		}
	}
	Dprintf("Failed to find relocation\n");
	return xtmld_internal_error;
}

static xtmld_result_code_t reloc_addr_value(const xtmld_state_t *lib_info,
					    Elf32_Addr addr,
					    Elf32_Addr *relocation)
{
	int i;
	uint32_t sep = lib_info->scratch_section_found ? MIN_SEPARATION : 0;
	for (i = 0; i < lib_info->num_sections; i++) {
		if ((addr + sep >= lib_info->src_offs[i]) &&
		    (addr < lib_info->src_offs[i] + lib_info->size[i] + sep)) {
			if (i <
			    lib_info->num_text_sections) {  // addr located in text segment
				*relocation =
					((Elf32_Addr)lib_info->text_addr[i]) -
					lib_info->src_offs[i] + addr;
			} else {
				*relocation =
					((Elf32_Addr)lib_info->dst_addr[i]) -
					lib_info->src_offs[i] + addr;
			}
			return xtmld_success;
		}
	}
	Dprintf("Failed to find relocation\n");
	return xtmld_internal_error;
}

static xtmld_result_code_t relocate_relative(xtmld_state_t *lib_info,
					     Elf32_Rela *rela)
{
	xtmld_ptr addr;
	xtmld_result_code_t status;
	status = reloc_addr(lib_info, rela->r_offset, &addr);
	if (status != xtmld_success)
		return status;
	Elf32_Word a_ofs = (Elf32_Word)addr % 4;
	// check if it's 4 byte aligned, C++ exception tables may have
	// unaligned relocations
	if (a_ofs == 0) {
		xtmld_ptr raddr;
		status = reloc_addr(lib_info, load_32(addr) + rela->r_addend,
				    &raddr);
		if (status != xtmld_success)
			return status;
		store_32(addr, (uint32_t)raddr);
	} else {
		Elf32_Word hi, lo, val;
		xtmld_ptr a_ptr = (addr - a_ofs);
		lo = load_32(a_ptr);
		hi = load_32((uint32_t *)a_ptr + 1);
		val = extract(lo, hi, a_ofs);
		status = reloc_addr(lib_info, val + rela->r_addend,
				    ((xtmld_ptr *)&val));
		if (status != xtmld_success)
			return status;
		combine(&lo, &hi, val, a_ofs);
		store_32(a_ptr, lo);
		store_32(((uint32_t *)a_ptr + 1), hi);
	}
	return xtmld_success;
}

static xtmld_result_code_t relocate_32_pcrel(xtmld_state_t *lib_info,
					     Elf32_Rela *rela)
{
	xtmld_ptr addr;
	xtmld_ptr raddr;
	xtmld_result_code_t status;
	// r_offset is the location of source in PIL, which has data to be relocated
	// get loaded address of source
	status = reloc_addr(lib_info, rela->r_offset, &addr);
	if (status != xtmld_success)
		return status;
	Elf32_Word a_ofs = (Elf32_Word)addr % 4;
	// r_addend is the location of target in PIL
	// get loaded address of target
	status = reloc_addr(lib_info, rela->r_addend, &raddr);
	if (status != xtmld_success)
		return status;
	// check if it's 4 byte aligned, C++ exception tables may have
	// unaligned relocations
	if (a_ofs == 0) {
		// write offset between target and source to source address
		store_32(addr, (uint32_t)raddr - (uint32_t)addr);
	} else {
		Elf32_Word hi, lo;
		xtmld_ptr a_ptr = (addr - a_ofs);

		// unaligned access; read the original to keep partial data
		lo = load_32(a_ptr);
		hi = load_32((uint32_t *)a_ptr + 1);
		// split the offset between source and target into 2 words
		combine(&lo, &hi, (uint32_t)raddr - (uint32_t)addr, a_ofs);

		// write offset between target and source to source address
		store_32(a_ptr, lo);
		store_32((uint32_t *)a_ptr + 1, hi);
	}
	return xtmld_success;
}

int is_data_segment(xtmld_state_t *lib_info, Elf32_Addr addr)
{
	int i;
	uint32_t sep = lib_info->scratch_section_found ? MIN_SEPARATION : 0;
	for (i = 0; i < lib_info->num_sections; i++) {
		if ((addr + sep >= lib_info->src_offs[i]) &&
		    (addr < lib_info->src_offs[i] + lib_info->size[i] + sep)) {
			if (i <
			    lib_info->num_text_sections) {  // addr located in text segment
				return 0;
			} else {
				return 1;
			}
		}
	}
	return 0;
}

xtmld_result_code_t xtmld_relocate_library(xtmld_state_t *lib_info)
{
	int i;
	xtmld_result_code_t status;
	Elf32_Rela *relocations = (Elf32_Rela *)lib_info->rel;
	for (i = 0; i < lib_info->rela_count; i++) {
		Elf32_Rela *rela = &relocations[i];
		Elf32_Word r_type;

		if (is_data_segment(lib_info, rela->r_offset)) {
			continue;  // data segment cannot relocate
		}

		if (ELF32_R_SYM(rela->r_info) != STN_UNDEF)
			return xtmld_internal_error;

		r_type = ELF32_R_TYPE(rela->r_info);
		/* instruction specific relocation, at the moment only const16 expected  */
		if (r_type >= R_XTENSA_SLOT0_OP &&
		    r_type <= R_XTENSA_SLOT14_OP) {
			Elf_Addr r_addr;
			Elf32_Addr r_addr_value;
			status = reloc_addr(lib_info, rela->r_offset,
					    (xtmld_ptr *)&r_addr);
			if (status != xtmld_success)
				return status;
			status = reloc_addr_value(lib_info, rela->r_addend,
						  &r_addr_value);
			if (status != xtmld_success)
				return status;

			status = relocate_op((xtmld_ptr)r_addr,
					     (r_type - R_XTENSA_SLOT0_OP),
					     r_addr_value);
			if (status != xtmld_success)
				return status;
		} else if (r_type >= R_XTENSA_SLOT0_ALT &&
			   r_type <= R_XTENSA_SLOT14_ALT) {
			Elf_Addr r_addr;
			Elf32_Addr r_addr_value;
			status = reloc_addr(lib_info, rela->r_offset,
					    (xtmld_ptr *)&r_addr);
			if (status != xtmld_success)
				return status;
			status = reloc_addr_value(lib_info, rela->r_addend,
						  &r_addr_value);
			if (status != xtmld_success)
				return status;

			status = relocate_op((xtmld_ptr)r_addr,
					     (r_type - R_XTENSA_SLOT0_ALT),
					     r_addr_value >> 16);
			if (status != xtmld_success) {
				return status;
			}
		} else if (r_type == R_XTENSA_RELATIVE) {
			status = relocate_relative(lib_info, rela);
			if (status != xtmld_success)
				return status;
		} else if (r_type == R_XTENSA_32_PCREL) {
			/* this is used with symbol@pcrel relocation for PIC code and
               std non-PIC code in .eh_frame sections*/
			status = relocate_32_pcrel(lib_info, rela);
			if (status != xtmld_success)
				return status;
		} else if (r_type != R_XTENSA_NONE) {
			return xtmld_internal_error;
		}
	}
	return xtmld_success;
}

/* end relocate code */
