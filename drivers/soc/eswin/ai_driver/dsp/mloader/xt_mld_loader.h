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

#ifndef XT_MLD_LOADER_H_
#define XT_MLD_LOADER_H_

#include <linux/string.h>

typedef void *xtmld_code_ptr;
typedef void *xtmld_data_ptr;
typedef void *xtmld_ptr;
typedef void *xtmld_start_fn_ptr;

typedef int int32_t;
typedef unsigned int uint32_t;
typedef unsigned char uint8_t;

#define xthal_memcpy memcpy

#ifdef __cplusplus
extern "C" {
#endif

#if defined(XTMLIB_DEBUG) && XTMLIB_DEBUG
#include <stdio.h>
#define Dprintf(args...) printf("%s", args)
#else
#define Dprintf(args...)
#endif

/* this is the type for the memory copy function that is passed to
 * xtmld_load_library_custom_fn().  Note that the signature is the
 * same as the standard library function memcpy() except for the user pointer.
 */
typedef xtmld_ptr (*memcpy_func_ex)(xtmld_ptr dest, const xtmld_ptr src,
				    unsigned int n, void *user);

/* this is the type for the memset function that is passed to
 * xtmld_load_library_custom_fn().  Note that the signature is the
 * same as the standard library function memset() except for the user pointer.
 */
typedef xtmld_ptr (*memset_func_ex)(xtmld_ptr s, int c, unsigned int n,
				    void *user);

/* Constants for the maximum number of supported code and data segments.  If
 * these values are changed, then the linker script needs to be updated
 * as well.
 */
#define XTMLD_MAX_CODE_SECTIONS 4
#define XTMLD_MAX_DATA_SECTIONS 4
#define XTMLD_MAX_SECTIONS \
	(XTMLD_MAX_CODE_SECTIONS + XTMLD_MAX_DATA_SECTIONS + 1)

/*
 * XTENSA_HOST_BYTE_SWAP should be defined to 1 only if the libraries are
 * being loaded on a core with a different endianness than the target where
 * the library code will be executed.
 */
#ifndef XTENSA_HOST_BYTE_SWAP
#define XTENSA_HOST_BYTE_SWAP 0
#endif

#define XTMLD_DATA_MARKER ((xtmld_mem_require_t)0x70000003)
#define XTMLD_CODE_MARKER ((xtmld_mem_require_t)0x70000004)
#define XTMLD_MAX_LIB_NAME_LENGTH 32  // default name

#define XTMLD_DATARAM0_ADDR 0x28100000
/* IMPORTANT: Do not change the numerical values of these enums. Doing so will
 * impact compatibility with libraries built with earlier versions of the SDK.
 */
typedef enum xtmld_mem_require {
	xtmld_load_normal = 1,
	xtmld_load_prefer_localmem = 2,
	xtmld_load_require_localmem = 3,
	xtmld_load_require_dram0 = 4,
	xtmld_load_require_dram1 = 5,
	xtmld_load_prefer_l2mem = 6,
} xtmld_mem_require_t;

/* The XTMLD_CODE_MEM_REQ() and XTMLD_DATA_MEM_REQ() macros must be included
 * in each multiple section loadable library.  The macros specify the memory
 * needed for each section.  Typical usage:
 *
 * Suppose, .data contains bulk data that isn't performance critical
 *          .1.data and .2.data contain performance critical data.
 *          .3.data contains an iDMA buffer which MUST be in local memory
 *
 * XTMLD_DATA_MEM_REQ(xtmld_load_normal, xtmld_load_prefer_localmem,
 *                    xtmld_prefer_local_memory, and xtmld_require_local_mem)
 */
#define XTMLD_CODE_MEM_REQ(sec1, sec2, sec3, sec4)                          \
	__attribute__((section(".dyninfo")))                                \
	xtmld_mem_require_t xtmld_text[] = { XTMLD_CODE_MARKER, sec1, sec2, \
					     sec3, sec4 };

#define XTMLD_DATA_MEM_REQ(sec1, sec2, sec3, sec4)                          \
	__attribute__((section(".dyninfo")))                                \
	xtmld_mem_require_t xtmld_data[] = { XTMLD_DATA_MARKER, sec1, sec2, \
					     sec3, sec4 };

#define XTMLD_LIBRARY_NAME(lname)                                   \
	__attribute__((section(".dyninfo"))) char xtmld_libname[] = \
		"xtensalibname:" #lname;

/*
 * A structure of type xtmld_library_info_t is by xtmld_library_info to
 * return the number and sizes of the sections.
 */
typedef struct {
	uint32_t number_of_code_sections;
	uint32_t number_of_data_sections;
	uint32_t code_section_size[XTMLD_MAX_CODE_SECTIONS];
	uint32_t data_section_size[XTMLD_MAX_DATA_SECTIONS];
	xtmld_mem_require_t code_section_mem_req[XTMLD_MAX_CODE_SECTIONS];
	xtmld_mem_require_t data_section_mem_req[XTMLD_MAX_DATA_SECTIONS];
} xtmld_library_info_t;

/*
 * All functions return a result code of type xtmld_result_code.
 */
typedef enum {
	/* Function executed as expected */
	xtmld_success = 0,
	/* a NULL ptr was passed to function */
	xtmld_null_ptr,
	/* The in memory library format was invalid. Verify that the correct
     * linker script was used to build the library.  Also verify the
     * correct command was used to package the library.
     */
	xtmld_invalid_library,
	/* An error internal to implementation was found.*/
	xtmld_internal_error,
	/* Make sure that XTMLD_DATA_MEM_REQ and XTMLD_CODE_MEM_REQ macros are
     * in library.
     */
	xtmld_mem_require_not_found,
	/*
     * A section that requires local memory was placed in a non-local
     * memory region.
     */
	xtmld_local_memory_required,
	/*
     * One or more code sections were not placed within the same 1GB range.
     */
	xtmld_code_must_be_in_1gig_region,
	/*
     * One or more of the section load areas (supplied to xtmld_load)
     * overlapped.
     */
	xtmld_sections_overlap,
	/*
     * Packed library is not properly aligned.
     */
	xtmld_image_not_aligned
} xtmld_result_code_t;

/*
 * xtmld_state_t is an internal structure used during library loading
 * and unloading.
 */
typedef struct xtmld_state_t {
	uint32_t num_sections;
	uint32_t num_text_sections;
	xtmld_ptr dst_addr[XTMLD_MAX_SECTIONS];
	uint32_t src_offs[XTMLD_MAX_SECTIONS];
	uint32_t size[XTMLD_MAX_SECTIONS];
	xtmld_start_fn_ptr start_sym;
	xtmld_ptr text_addr[XTMLD_MAX_CODE_SECTIONS];
	xtmld_ptr init;
	xtmld_ptr fini;
	xtmld_ptr rel;
	uint32_t rela_count;
	xtmld_ptr hash[XTMLD_MAX_DATA_SECTIONS];
	struct xtmld_state_t *next;
	char libname[XTMLD_MAX_LIB_NAME_LENGTH];
	int32_t scratch_section_found;
} xtmld_state_t;

typedef void *xtmld_packaged_lib_t;

extern xtmld_state_t *xtmld_loaded_lib_info;

/*
 * This function locates the .rodata section of the packaged library.
 * So, the packaged library has no need to be linked with main module
 * and it can be loaded on the fly.  The returned result in lib can
 * be used in xtmld_library_info() and xtmld_load().
 */
xtmld_result_code_t xt_mld_get_packaged_lib(void *packaged_img,
					    xtmld_packaged_lib_t **lib);

/*
 * This function converts the result code from one of the
 * other xtmld functions into a descriptive string.
 */
extern const char *xtmld_error_string(xtmld_result_code_t);

/*
 * This function writes information about the packaged library into
 * the info structure.
 */
extern xtmld_result_code_t xtmld_library_info(xtmld_packaged_lib_t *lib,
					      xtmld_library_info_t *info);

/*
 * The function xtmld_load() loads the library into the sections
 * supplied, call the library init function (C++ constructors
 * for static objects), and sets the start function point to point
 * to the _start function() of the supplied library.
 */
extern xtmld_result_code_t xtmld_load(xtmld_packaged_lib_t *lib,
				      xtmld_code_ptr *code_section_la,
				      xtmld_data_ptr *data_section_la,
				      xtmld_state_t *state,
				      xtmld_start_fn_ptr *start);

/*
 * xtmld_load_custom_fn is the same as xtmld_load except that it has
 * additional arguments for custom memory copy and set functions. The
 * parameter user, is passed to the custom functions.
 */
extern xtmld_result_code_t xtmld_load_custom_fn(
	xtmld_packaged_lib_t *library, xtmld_code_ptr *destination_code_address,
	xtmld_data_ptr *destination_data_address, xtmld_state_t *lib_info,
	memcpy_func_ex mcpy_fn, memset_func_ex mset_fn, void *user,
	xtmld_start_fn_ptr *start);

/*
 * This function unloads a library.  The state argument must be the
 * same as passed to the load function.
 */
extern xtmld_result_code_t xtmld_unload(xtmld_state_t *state);

extern xtmld_result_code_t xtmld_relocate_library(xtmld_state_t *state);

#ifdef __cplusplus
}
#endif

#endif
