// Copyright © 2023 ESWIN. All rights reserved.
//
// Beijing ESWIN Computing Technology Co., Ltd and its affiliated companies ("ESWIN") retain
// all intellectual property and proprietary rights in and to this software. Except as expressly
// authorized by ESWIN, no part of the software may be released, copied, distributed, reproduced,
// modified, adapted, translated, or created derivative work of, in whole or in part.

#ifndef __HETERO_HOST_H__
#define __HETERO_HOST_H__

#include "conv_regs.h"
#include "hetero_arch.h"
#include "hetero_ipc.h"
#include "hetero_types.h"
#include "npu_base_regs.h"
#ifndef __KERNEL__
#include "hetero_log.h"
#endif

#ifdef __cplusplus
extern "C" {
#endif

static const u32 legal_token = 0x6D696E2E;  // the value is the asm of ".nim"
static const u32 nim_version = 0x0001;
static const u32 nim_signature = 0x2030;

typedef union pp_status_t {
    struct {
        u32 status_0 : 2;
        u32 : 6;
        u32 status_1 : 2;
        u32 : 6;
        u32 pp_status : 1;
        u32 : 15;
    };
    u32 value;
} pp_status_t;

typedef struct {
    /**
     * @brief Specifies the start of elf session relative to the header of file.
     *
     */
    u32 elf_start;

    /**
     * @brief Specifies the length in bytes of each elf session.
     *
     */
    u32 elf_len;

    /**
     * @brief Each bit in this bitmap corresponds to a E31 node. If it is 1, then this elf maps to the target node.
     *        If the bitmap is 0, then this elf maps to ddr as the bootloader for each E31 node.
     */
    u32 node_bitmap;
} elf_descriptor_t;

typedef struct {
    /**
     * @brief .nim file should start with .nim, token corresponds to this value.
     *
     */
    u32 token;

    /**
     * @brief Specifies file version.
     *
     */
    u32 version;

    /**
     * @brief Specifies total length of file in bytes.
     *
     */
    u32 total_len;

    /**
     * @brief Specifies a signature of the whole file.
     *
     */
    u32 signature;

    /**
     * @brief Specifies number of descriptors in .nim file.
     *
     */
    u32 num_descriptors;

    /**
     * @brief This descriptor list describes how each elf session should be loaded.
     *
     */
    elf_descriptor_t descriptors[0];
} nim_header_t;

#if defined(__KERNEL__) && NPU_DEV_SIM == NPU_REAL_ENV
#include <asm/cacheflush.h>
#include <linux/delay.h>
/* ITIM 32k */
#define E31_CORE_ITIM_BASE_ADDR 0x1800000
#define E31_CORE_ITIM_MAX_ADDR 0x1808000
/* DTIM 32k */
#define E31_CORE_DTIM_BASE_ADDR 0x30000000
#define E31_CORE_DTIM_MAX_ADDR 0x30008000

/**
 * According to HW best practice, NPU configuration reset should be treated
 * specially.
 */
#define SW_NPU_CFG_RSTN 1

/**
 * @brief Activate E31 core with E31 interrupt.
 *
 * @param npu_cfg_base_addr     NPU control base address.
 * @param core_idx              E31 core index.
 */
static inline void activate_system(u8 *npu_cfg_base_addr, u32 core_idx)
{
    io_write(npu_cfg_base_addr + (NPU_CTRL_OFFSET + INT_SET_BITS(core_idx)), 1U << IRQ_E31_INTER_CORE_COMM);
}

/**
 * @brief Reset NPU subsystem.
 *
 * @param npu_cfg_base_addr NPU configuration base address.
 */
static inline void npu_clk_reset(u8 *npu_cfg_base_addr)
{
#ifndef __KERNEL__
    /* E31's clk&reset already configured in e31_clk_reset_init(). */

    /* Reset SW_NPU_CFG_RSTN first. */
    io_write(npu_cfg_base_addr + NPU_RST_CTRL, ~(1U << SW_NPU_CFG_RSTN));
    io_write(npu_cfg_base_addr + NPU_RST_CTRL, 0);
    io_write(npu_cfg_base_addr + NPU_ACLK_CTRL, 0xC0000020);  // aclk:800M
    io_write(npu_cfg_base_addr + NPU_LLC_CTRL, 0x80000220);   // llc:800M
    // 0xC0001212--npu&&e31:1040M   0xC0002012--npu:1040M e31:750M
    io_write(npu_cfg_base_addr + NPU_CORE_CTRL, 0xC0001212);
    /* Release SW_NPU_CFG_RSTN last. */
    io_write(npu_cfg_base_addr + NPU_RST_CTRL, ~(1U << SW_NPU_CFG_RSTN));
    io_write(npu_cfg_base_addr + NPU_RST_CTRL, 0xFFFFFFFF);
    /* PMC */
    io_write(npu_cfg_base_addr + NPU_CLAMP_CTRL, io_read(npu_cfg_base_addr + NPU_CLAMP_CTRL) & (~1U));
    /* Configure the JTAG chain. */
    io_write(npu_cfg_base_addr + JTAG_CHAIN_CTRL, 0x39);
#endif
    /* Configure scie hardware pattern */
    io_write(npu_cfg_base_addr + NPU_SCIE_CMD_OFFSET, 1);
}

/**
 * @brief Activate E31 bootloader with E31 core num and bootloader entry address.
 *
 * @param npu_cfg_base_addr     NPU control base address.
 * @param core_idx              E31 core index.
 * @param entry_addr            E31 bootloader entry address.
 */
static inline void activate_bootloader(u8 *npu_cfg_base_addr, u32 core_idx, u32 boot_dma_addr)
{
    u32 value;

    value = io_read(npu_cfg_base_addr + (NPU_CTRL_OFFSET + NPU_E31_CORE_REST));
    io_write(npu_cfg_base_addr + (NPU_CTRL_OFFSET + NPU_E31_CORE_REST), set_bit_pos(value, core_idx));

    value = io_read(npu_cfg_base_addr + (NPU_CTRL_OFFSET + NPU_E31_BUS_REST));
    io_write(npu_cfg_base_addr + (NPU_CTRL_OFFSET + NPU_E31_BUS_REST), clear_bit_pos(value, core_idx));

    value = io_read(npu_cfg_base_addr + (NPU_CTRL_OFFSET + NPU_E31_GPR_REST));
    io_write(npu_cfg_base_addr + (NPU_CTRL_OFFSET + NPU_E31_GPR_REST), clear_bit_pos(value, core_idx));

    io_write(npu_cfg_base_addr + (NPU_CTRL_OFFSET + RESET_VECTOR_OFFSET(core_idx)), boot_dma_addr);
    io_write(npu_cfg_base_addr + (NPU_CTRL_OFFSET + JTAG_ID_CODE_OFFSET(core_idx)), (core_idx + 1));

    value = io_read(npu_cfg_base_addr + (NPU_CTRL_OFFSET + NPU_E31_CLOCK_GATE));
    io_write(npu_cfg_base_addr + (NPU_CTRL_OFFSET + NPU_E31_CLOCK_GATE), set_bit_pos(value, core_idx));

    value = io_read(npu_cfg_base_addr + (NPU_CTRL_OFFSET + NPU_E31_CORE_CLOCK_GATE));
    io_write(npu_cfg_base_addr + (NPU_CTRL_OFFSET + NPU_E31_CORE_CLOCK_GATE), set_bit_pos(value, core_idx));

    value = io_read(npu_cfg_base_addr + (NPU_CTRL_OFFSET + NPU_E31_CORE_REST));
    io_write(npu_cfg_base_addr + (NPU_CTRL_OFFSET + NPU_E31_CORE_REST), clear_bit_pos(value, core_idx));
}

/* elf e_ident */
#define IS_ELF(ehdr)                                                                                                   \
    ((ehdr).e_ident[EI_MAG0] == ELFMAG0 && (ehdr).e_ident[EI_MAG1] == ELFMAG1 && (ehdr).e_ident[EI_MAG2] == ELFMAG2 && \
     (ehdr).e_ident[EI_MAG3] == ELFMAG3)

/**
 * @brief Check if a valid ELF image exists at the given memory location.
 * @param addr    Address of the ELF image.
 * @return        True is valid, false is invalid.
 */
static bool validate_elf_image(Elf32_Ehdr *ehdr)
{
    if (!IS_ELF(*ehdr)) {
        printf("No elf image at address %px\n", ehdr);
        return false;
    }

    if (ehdr->e_ident[EI_CLASS] == ELFCLASS64) {
        printf("Not a 32-bit elf image at address %px\n", ehdr);
        return false;
    }

    if (ehdr->e_type != ET_EXEC) {
        printf("Not an execute image at address %px, e_type=0x%x\n", ehdr, ehdr->e_type);
        return false;
    }

    return true;
}

/**
 * @brief Get ELF text section data.
 * @param img_addr          Address of the ELF image.
 * @param entry_addr        Points to the start of text section in ELF file.
 * @param bootloader_len    Points to the bootloader length variable.
 * @return                  True if ELF section retrieves successfully.
 */
static inline bool get_elf_shdr(u8 *img_addr, u8 **entry_addr, u32 *bootloader_len)
{
    Elf32_Ehdr *ehdr;  // elf header structure pointer
    Elf32_Shdr *shdr;  // section header structure pointer
    int i;             // loop counter

    ehdr = (Elf32_Ehdr *)img_addr;
    if (!validate_elf_image(ehdr)) {
        return false;
    }

    /* Find the section header string table for output info */
    shdr = (Elf32_Shdr *)(img_addr + ehdr->e_shoff + (ehdr->e_shstrndx * sizeof(Elf32_Shdr)));

    /* Load each appropriate section */
    for (i = 0; i < ehdr->e_shnum; ++i) {
        shdr = (Elf32_Shdr *)(img_addr + ehdr->e_shoff + (i * sizeof(Elf32_Shdr)));

        if (!(shdr->sh_flags & SHF_ALLOC) || shdr->sh_addr == 0 || shdr->sh_size == 0) {
            continue;
        }
        *entry_addr = (u8 *)img_addr + shdr->sh_offset;
        *bootloader_len = shdr->sh_size;
        break;
    }

    return true;
}

/**
 * @brief Parse E31 bootloader entry address in ddr.
 *
 * @param nim_header        Points to the header of .nim data.
 * @param entry_addr        Points to the start of text section in ELF file.
 * @param bootloader_len    Points to the bootloader length variable.
 * @return                  True means loading bootloader successfully.
 */
static inline bool load_bootloader(nim_header_t *nim_header, u8 **entry_addr, u32 *bootloader_len)
{
    u8 *img_addr;
    u32 elf_idx;

    printf("Parse bootloader entry from memory address:%px\n", nim_header);
    *entry_addr = NULL;
    *bootloader_len = 0;

    /* Get the bootloader entry address */
    for (elf_idx = 0; elf_idx != nim_header->num_descriptors; ++elf_idx) {
        if (nim_header->descriptors[elf_idx].node_bitmap == 0) {
            // Calculate elf image address
            img_addr = (u8 *)nim_header + nim_header->descriptors[elf_idx].elf_start;
            if (!get_elf_shdr(img_addr, entry_addr, bootloader_len)) {
                printf(".nim elf[%d] load from %px failed\n", elf_idx, img_addr);
            }
            break;
        }
    }

    printf("E31 bootloader entry_addr=%px, bootloader length=%u.\n", *entry_addr, *bootloader_len);
    return *entry_addr != NULL;
}

/**
 * @brief Verify whether the nim file header is correct.
 * @param nim_header    Points to the header of .nim data.
 * @return              True is valid, false is invalid.
 */
static inline bool validate_nim_header(nim_header_t *nim_header)
{
    if (nim_header->token != legal_token) {
        printf("Invalid token:0x%x legal_token:0x%x\n", nim_header->token, legal_token);
        return false;
    }

    if (nim_header->signature != nim_signature) {
        printf("Signature:%x (%x) verification failed!\n", nim_header->signature, nim_signature);
        return false;
    }

    printf("nim file total_len:%u, num_descriptors:%u\n", nim_header->total_len, nim_header->num_descriptors);

    return true;
}

/**
 * @brief Find the boot firmware image inside .nim file.
 *
 * @param nim_image         Points to the .nim file image in DDR.
 * @param bootloader_len    Points to the bootloader length variable.
 * @return                  The boot firmware image pointer in DDR or NULL if the given part can not be found.
 */
static inline u8 *find_boot_firmware(char *nim_image, u32 *bootloader_len)
{
    u8 *entry_addr = NULL;
    nim_header_t *nim_header = NULL;

    /* Let all E31 cores become active. */
    if (nim_image == NULL) {
        printf("Invalid input parameters %px\n", nim_image);
        return NULL;
    }

    nim_header = (nim_header_t *)nim_image;
    if (!validate_nim_header(nim_header)) {
        return NULL;
    }

    if (!load_bootloader(nim_header, &entry_addr, bootloader_len)) {
        return NULL;
    }

    flush_cache_all();

    return entry_addr;
}

/**
 * @brief Check if all E31s are activated.
 *
 * @param npu_cfg_base_addr     NPU control base address.
 * @return                      Returns true to indicate system activation
 *                              status.
 */
static inline bool check_system_activated(u8 *npu_cfg_base_addr)
{
    /* We must ensure all E31s are activated. */
    u8 *tim_base_addr = npu_cfg_base_addr + NPU_CPU_OFFSET;
    u32 remain_bitmap = BITMASK(NUM_NPU_CORES);
    u32 times = 3;
    u32 i = 0;
    u32 index;
    u32 *signauture_addr;

    while (remain_bitmap != 0) {
        if (i > times) {
            printf("check mcu node_sigature failed, remain_bitmap=0x%x.\n", remain_bitmap);
            return false;
        }
        index = ffs(remain_bitmap);
        signauture_addr = (u32 *)(tim_base_addr + NPU_CPU_SIZE * index + NPU_DTIM_OFFSET);
        if (*signauture_addr == node_signature) {
            clear_bit_pos(remain_bitmap, index);
            printf("E31(%u) validation passed\n", index);
            i = 0;
        } else {
            printf("Wait for E31(%u) validation.\n", index);
            mdelay(10);
            i++;
        }
    }
#ifdef LOG_DEBUG
    printf("All nodes activated.\n");
#endif
    return true;
}

/**
 * @brief Calculate ITIM or DTIM load address.
 * @param shdr        ELF section.
 * @param tim_addr    Address of the ELF destination address.
 * @return            The section load address.
 */
static void *get_tim_load_addr(Elf32_Shdr *shdr, u8 *tim_addr)
{
    void *load_addr = NULL;

    if ((E31_CORE_ITIM_BASE_ADDR <= shdr->sh_addr) && ((shdr->sh_addr + shdr->sh_size) <= E31_CORE_ITIM_MAX_ADDR)) {
        load_addr = (tim_addr + NPU_ITIM_OFFSET) + (shdr->sh_addr - E31_CORE_ITIM_BASE_ADDR);
    } else if ((E31_CORE_DTIM_BASE_ADDR <= shdr->sh_addr) &&
               ((shdr->sh_addr + shdr->sh_size) <= E31_CORE_DTIM_MAX_ADDR)) {
        load_addr = (tim_addr + NPU_DTIM_OFFSET) + (shdr->sh_addr - E31_CORE_DTIM_BASE_ADDR);
    }

    return load_addr;
}

/**
 * @brief Load ELF from DDR to E31 ITIM/ DTIM
 * @param img_addr    Address of the ELF image.
 * @param tim_addr    Address of the ELF destination address.
 * @return            True if ELF data loads successfully.
 */
static inline bool load_elf_shdr(u8 *img_addr, u8 *tim_addr)
{
    Elf32_Ehdr *ehdr;  // elf header structure pointer
    Elf32_Shdr *shdr;  // section header structure pointer
    char *strtab;      // string table pointer
    char *image;       // binary image pointer
    int i;             // loop counter
    void *load_addr;   // load addr

    ehdr = (Elf32_Ehdr *)img_addr;
    if (!validate_elf_image(ehdr)) {
        return false;
    }

    /* Find the section header string table for output info */
    shdr = (Elf32_Shdr *)(img_addr + ehdr->e_shoff + (ehdr->e_shstrndx * sizeof(Elf32_Shdr)));

    if (shdr->sh_type == SHT_STRTAB) {
        strtab = (char *)(img_addr + shdr->sh_offset);
        UNUSED(strtab);
    }

    /* Load each appropriate section */
    for (i = 0; i < ehdr->e_shnum; ++i) {
        shdr = (Elf32_Shdr *)(img_addr + ehdr->e_shoff + (i * sizeof(Elf32_Shdr)));

        // get section name from .shstrtab
        if (!(shdr->sh_flags & SHF_ALLOC) || shdr->sh_addr == 0 || shdr->sh_size == 0) {
            continue;
        }

        load_addr = get_tim_load_addr(shdr, tim_addr);
        if (load_addr == NULL) {
            continue;
        }

        /* load image to ITIM/ DTIM */
        if (shdr->sh_type == SHT_NOBITS) {
            // TODO(someone) Temporarily disable memory clean process.
            // memset(load_addr, 0, shdr->sh_size);
        } else {
            image = (char *)img_addr + shdr->sh_offset;
            memcpy(load_addr, image, shdr->sh_size);
        }
    }

    return true;
}

/**
 * @brief Load ELF from given memory address in .nim format to ITim and DTim addresses.
 *
 * @param nim_header        Points to the header of .nim data.
 * @param tim_base_addr     Points to the base memory of E31 clusters.
 * @return                  true means ELF loading completes.
 */
static inline bool load_system(nim_header_t *nim_header, u8 *tim_base_addr)
{
    u8 *img_addr;
    u8 *tim_addr;
    u32 elf_idx;
    u32 bitmap;
    u32 core_idx;

    if (nim_header == NULL || tim_base_addr == NULL) {
        printf("Invalid params!\n");
        return false;
    }

    printf("Load ELF from memory address:%px to cpu address:%px\n", nim_header, tim_base_addr);

    /* Load elf to destination address */
    for (elf_idx = 0; elf_idx < nim_header->num_descriptors; ++elf_idx) {
        // calculate elf image address
        img_addr = (u8 *)nim_header + nim_header->descriptors[elf_idx].elf_start;

        bitmap = nim_header->descriptors[elf_idx].node_bitmap;
        // get position of the only set bit in 'bitmap', and then calculate e31 node tim base address
        while (bitmap != 0) {
            core_idx = ffs(bitmap);
            tim_addr = tim_base_addr + NPU_CPU_SIZE * core_idx;
            printf("Start to load E31(%u) elf to address:%px\n", core_idx, img_addr);
            if (!load_elf_shdr(img_addr, tim_addr)) {
                printf(".nim elf[%d] load from %px failed\n", elf_idx, img_addr);
                return false;
            }

            bitmap &= ~(1U << core_idx);
        }
    }

    printf("Load image done.\n");
    return true;
}

/**
 * @brief Load firmware to convolution E31 cores. This function can be called in
 * baremetal or linux environment.
 *
 * @param nim_image         Points to the .nim file image in DDR.
 * @param boot_dma_addr     Provides the DMA address from E31 pespective.
 * @param npu_ctrl_addr     The NPU ctrl base address.
 * @param tim_addr          The TIM base address.
 * @return                  Returns true means loading completes successfully.
 */
static inline bool load_firmware_to_conv_cpus(char *nim_image, u32 boot_dma_addr, u8 *npu_cfg_base_addr)
{
    u32 core_idx = 0;
    u8 *tim_base_addr = NULL;
    nim_header_t *nim_header = NULL;

    if (nim_image == NULL || npu_cfg_base_addr == NULL) {
        printf("Invalid nim_image:%px or npu_cfg_base_addr:%px\n", nim_image, npu_cfg_base_addr);
        return false;
    }

    /* NPU reset and clock initialization. */
    npu_clk_reset(npu_cfg_base_addr);

    /* Active all e31 bootloaders */
    for (core_idx = 0; core_idx != NUM_NPU_CORES; ++core_idx) {
        activate_bootloader(npu_cfg_base_addr, core_idx, boot_dma_addr);
    }

    nim_header = (nim_header_t *)nim_image;
    if (!validate_nim_header(nim_header)) {
        return false;
    }

    /* Load .nim data. */
    tim_base_addr = npu_cfg_base_addr + NPU_CPU_OFFSET;

    if (!load_system(nim_header, tim_base_addr)) {
        return false;
    }

    return true;
}
#endif

static void host_ipc_initialize(u64 host_node_addr, u32 host_node_iova, u64 emission_addr, u64 program_addr)
{
    emission_node_t *pemission_node;
    host_node_t *phost_node = (host_node_t *)host_node_addr;
    phost_node->emission_base_addr = emission_addr;
    phost_node->program_base_addr = program_addr;

    pemission_node = (emission_node_t *)phost_node->emission_base_addr;
    pemission_node->host_base_addr = host_node_iova;
    hetero_init(phost_node->chn_to_emission, pemission_node->chn_from_host);
    phost_node->chn_to_emission.channel.peer = ADDR_OFFSET(emission_node_t, chn_from_host);
}

static inline void *get_npu_ctrl_addr(void *npu_base)
{
    return (void *)((u8 *)npu_base + NPU_CTRL_OFFSET);  // NPU_CTRL_BASE_ADDR: 0x51D80000
}

static void send_frame_ready(u8 tiktok, bool enable_stat, hetero_ipc_frame_t *frame_desc, npu_io_tensor_t *io_tensor,
                             void *npu_base, host_node_t *host_node)
{
    emission_node_t *pemission_node = (emission_node_t *)host_node->emission_base_addr;
    program_node_t *program_node = (program_node_t *)host_node->program_base_addr;
    u8 param = tiktok | (enable_stat ? 0x02 : 0x0);
    msg_payload_t payload = {FRAME_READY, param};
    memcpy(&pemission_node->frame_desc[tiktok], frame_desc, sizeof(hetero_ipc_frame_t));
    memcpy(program_node->io_addr_list[tiktok].tensor_addr, io_tensor->tensor_addr, sizeof(io_tensor->tensor_addr));
    host_hetero_send(&host_node->chn_to_emission, (void *)host_node->emission_base_addr, get_npu_ctrl_addr(npu_base),
                     payload);
#ifdef __KERNEL__
    dla_debug("send msg:{%u,%u} to emission\n", payload.type, payload.param);
#else
    printf("send msg:{%u,%u} to emission\n", payload.type, payload.param);
#endif
}

static void send_op_resume(u8 tiktok, u16 op_index, void *npu_base, host_node_t *host_node)
{
    msg_payload_t payload;
    payload.type = NOTIFY_OP_RESUME;
    payload.param = tiktok;
    payload.lparam = op_index;
    host_hetero_send(&host_node->chn_to_emission, (void *)host_node->emission_base_addr, get_npu_ctrl_addr(npu_base),
                     payload);
#ifdef __KERNEL__
    dla_debug("send msg:{%u,%u,%u} to emission\n", payload.type, payload.param, payload.lparam);
#endif
}

static void send_event_source_req(u8 tiktok, u16 op_index, void *npu_base, host_node_t *host_node)
{
    msg_payload_t payload;
    payload.type = DEC_OP_REF;
    payload.param = tiktok;
    payload.param |= IDX_EVENT_SOURCE << 4;
    payload.lparam = op_index;
    host_hetero_send(&host_node->chn_to_emission, (void *)host_node->emission_base_addr, get_npu_ctrl_addr(npu_base),
                     payload);
#ifdef __KERNEL__
    dla_debug("send event op msg:{%u,%u,%u} to emission\n", payload.type, payload.param, payload.lparam);
#else
    NPU_LOG_DBG("send event op tiktok=%u op_index=%u to emission\n", tiktok, op_index);
#endif
}

#ifdef __cplusplus
}
#endif

#endif  // __HETERO_HOST_H__
