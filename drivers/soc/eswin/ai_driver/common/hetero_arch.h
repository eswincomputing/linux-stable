// SPDX-License-Identifier: GPL-2.0
/*
 * ESWIN AI driver
 *
 * Copyright 2024, Beijing ESWIN Computing Technology Co., Ltd.. All rights reserved.
 * SPDX-License-Identifier: GPL-2.0
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, version 2.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 *
 * Authors: Lu XiangFeng <luxiangfeng@eswincomputing.com>
 */

#ifndef __HETERO_UTILS_H__
#define __HETERO_UTILS_H__

#include "hetero_types.h"
#include "hetero_common.h"
#include "npu_base_regs.h"
#include "sys_regs.h"

#if defined(__KERNEL__)
#else
#include <strings.h>
#include <assert.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define set_bit_pos(bitmap, pos) ((bitmap) |= (1UL << (pos)))
#define clear_bit_pos(bitmap, pos) ((bitmap) &= ~(1UL << pos))

#if defined(__KERNEL__)
#define hmb() smp_mb()
#define printf printk
#define io_read(addr) readl(addr)
#define io_write(addr, value) writel(value, addr)
#else  // (__KERNEL__)
/**
 * @brief Add a compiler time memory barrier honored by GCC. It is not honored by CPU.
 *
 */
#define cmb() asm volatile("" : : : "memory")

/**
 * @brief Add a hardware memory barrier honored by GCC and CPU.
 *
 */
#define hmb() __sync_synchronize()
#endif  // (__KERNEL__)

#if NPU_DEV_SIM != NPU_REAL_ENV

#define ffs(value) ((value) == 0 ? sizeof(u32) * 8 : __builtin_ctz(value))
#define popcnt __builtin_popcount

extern void reg_write(u32 addr, u32 value);
extern u32 reg_read(u32 addr);
#ifndef __KERNEL__
extern void get_scie_data(u32 &ret, u32 rs1, u32 func7);
#endif
extern void send_scie_data(u32 op_code, u32 rs1, u32 rs2);

extern void sim_release_ref_in_context(u32 core_id, u32 pingpong);
extern void prepare_conv_context(u32 pingpong, const conv_program_data_t *conv_prog, u64 extra_data_addr);
extern u64 read_clock_cycles(void);
static inline u32 es_sys_getcurcnt(void) { return 0; }
/**
 * @brief The high precision clock frequency.
 *
 */
static const u64 clock_frequency = 1000000000ULL;

#ifdef __cplusplus
#define TLS thread_local
#else  // __cplusplus
#define TLS __thread
#endif  // __cplusplus

#define NO_RETURN

extern bool all_frames_completed(void);

#else  // NPU_DEV_SIM != NPU_REAL_ENV

#define TLS
#define NO_RETURN __attribute__((noreturn))

#define get_scie_data(ret, rs1, func7) \
    asm volatile(".insn r 0xB, 0x0, %3, %0, %1, %2" : "=r"(ret) : "r"(rs1), "r"(rs1), "I"(func7));
/* Need to add a nop instruction in front of SCIe instruction. This is used temporarily to avoid an SCIe bug. */
#define send_scie_data(op_code, rs1, rs2)                            \
    asm volatile("ori tp, %2, %0\n"                                  \
                 ".insn r 0xB, 0x0, 0x4, x0, %1, tp" ::"i"(op_code), \
                 "r"(rs1), "r"(rs2));

#ifndef __KERNEL__
#define ffs(value) _ffs(value)
#else
#define ffs(value) ((value) == 0 ? sizeof(u32) * 8 : __builtin_ctz(value))
#endif

/**
 * @brief Scan binary representation of r0 from LSB to MSB. Calculate the first occasion of 1 and return this value.
 * Returns 32 if r0 equals 0.
 *
 * @param r0
 * @return u32
 */
static inline u32 _ffs(u32 r0)
{
    u32 r1;
    get_scie_data(r1, r0, 0x0);
    return r1;
}

#define TIMER0_BASE 0x51840000
#define PTS_CHAN 7
#define PTS_END_CYCLE (*(u32 *)(TIMER0_BASE + 0x0 + PTS_CHAN * 0x14))
#define PTS_START_CYCLE (*(u32 *)(TIMER0_BASE + 0x4 + PTS_CHAN * 0x14))

static inline u32 es_sys_getcurcnt(void) { return (PTS_END_CYCLE - PTS_START_CYCLE); }

/**
 * @brief Calculate number of 1s in binary representation of r0 and return this value.
 *
 * @param r0
 * @return u32
 */
static inline u32 popcnt(u32 r0)
{
    u32 r1;
    get_scie_data(r1, r0, 0x1);
    return r1;
}

/**
 * @brief Write a DW data to a given register. Note no memory barrier is set. Caller is responsible for coordinating
 * register order by utilizing memory barriers.
 *
 * @param addr      The address of register.
 * @param value     The DW value to be written.
 */
static inline void reg_write(size_t addr, u32 value)
{
    ASSERT_REG_ADDR(addr);
    *(volatile u32 *)(addr) = value;
}

/**
 * @brief Read a DW data from a given register.
 *
 * @param addr      The address of register.
 * @return u32      The DW value read from register.
 */
static inline u32 reg_read(size_t addr)
{
    ASSERT_REG_ADDR(addr);
    return *(volatile u32 *)addr;
}

/**
 * @brief Clear interrupt enable bitmap.
 *
 * @return The old mie value.
 */
static inline u32 clear_interrupt(void)
{
    u32 mie;
    asm volatile("csrrw %0, mie, zero" : "=r"(mie));
    return mie;
}

/**
 * @brief Restore interrupt enable bitmap.
 *
 */
static inline void restore_interrupt(u32 mie) { asm volatile("csrw mie, %0" ::"r"(mie)); }

/**
 * @brief Write the 15th bit of the test_reg_0 register to "1" for setting zebu breakpoint.
 *
 */
static inline void set_zebu_breakpoint(void) { reg_write(SYS_CON_TEST_REG_0, 0x8000); }

/**
 * @brief The high precision clock frequency.
 *
 */
static const u64 clock_frequency = 5000000L;

/**
 * @brief Read CPU clock cycles
 *
 * @return         The CPU clock cycles
 */
static inline unsigned long read_clock_cycles(void)
{
    unsigned long cycles;
    asm volatile("rdcycle %0" : "=r"(cycles));
    return cycles;
}

#endif  // NPU_DEV_SIM != NPU_REAL_ENV

#ifdef __cplusplus
}
#endif

#endif  // __HETERO_UTILS_H__
