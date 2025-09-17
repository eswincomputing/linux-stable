/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright 2024, Beijing ESWIN Computing Technology Co., Ltd.. All rights reserved.
 *
 * Authors: huangyifeng<huangyifeng@eswincomputing.com>
 */

#ifndef __ESWIN_CLK_H__
#define __ESWIN_CLK_H__

#include <linux/clk-provider.h>
#include <linux/io.h>
#include <linux/spinlock.h>

#define CLK_FREQ_1800M 1800000000
#define CLK_FREQ_1700M 1700000000
#define CLK_FREQ_1600M 1600000000
#define CLK_FREQ_1500M 1500000000
#define CLK_FREQ_1400M 1400000000
#define CLK_FREQ_1300M 1300000000
#define CLK_FREQ_1200M 1200000000
#define CLK_FREQ_1100M 1100000000
#define CLK_FREQ_1000M 1000000000
#define CLK_FREQ_900M 900000000
#define CLK_FREQ_800M 800000000
#define CLK_FREQ_700M 700000000
#define CLK_FREQ_600M 600000000
#define CLK_FREQ_500M 500000000
#define CLK_FREQ_400M 400000000
#define CLK_FREQ_200M 200000000
#define CLK_FREQ_100M 100000000
#define CLK_FREQ_24M 24000000

#define APLL_HIGH_FREQ 983040000
#define APLL_LOW_FREQ 225792000

struct eswin_clock_data {
	struct clk_onecell_data clk_data;
	void __iomem *base;
	int numa_id;
	spinlock_t lock;
};

struct eswin_clock {
	unsigned int id;
	char *name;
	const char *parent_name;
	unsigned long flags;
	struct clk_hw hw;
	const char *alias;
};

struct eswin_fixed_rate_clock {
	unsigned int id;
	char *name;
	const char *parent_name;
	unsigned long flags;
	unsigned long fixed_rate;
};

struct eswin_fixed_factor_clock {
	unsigned int id;
	char *name;
	const char *parent_name;
	unsigned long mult;
	unsigned long div;
	unsigned long flags;
};

struct eswin_mux_clock {
	unsigned int id;
	const char *name;
	const char *const *parent_names;
	u8 num_parents;
	unsigned long flags;
	unsigned long offset;
	u8 shift;
	u32 mask;
	u8 mux_flags;
	u32 *table;
	const char *alias;
};

struct eswin_divider_clock {
	unsigned int id;
	const char *name;
	const char *parent_name;
	unsigned long flags;
	unsigned long offset;
	u8 shift;
	u8 width;
	u8 div_flags;
	struct clk_div_table *table;
	const char *alias;
};

struct eswin_gate_clock {
	unsigned int id;
	const char *name;
	const char *parent_name;
	unsigned long flags;
	unsigned long offset;
	u8 bit_idx;
	u8 gate_flags;
	const char *alias;
};

struct eswin_pll_clock {
	u32 id;
	const char *name;
	const char *parent_name;
	const u32 ctrl_reg0;
	const u8 pllen_shift;
	const u8 pllen_width;
	const u8 refdiv_shift;
	const u8 refdiv_width;
	const u8 fbdiv_shift;
	const u8 fbdiv_width;

	const u32 ctrl_reg1;
	const u8 frac_shift;
	const u8 frac_width;

	const u32 ctrl_reg2;
	const u8 postdiv1_shift;
	const u8 postdiv1_width;
	const u8 postdiv2_shift;
	const u8 postdiv2_width;

	const u32 status_reg;
	const u8 lock_shift;
	const u8 lock_width;
};

enum voltage_level {
	VOLTAGE_0_9V = 900000, // Represents 0.9V in microvolt‌
	VOLTAGE_0_8V = 800000 // Represents 0.8V in microvolt‌
};

struct eswin_clk_pll {
	struct clk_hw hw;
	struct device *dev;
	u32 id;
	int numa_id;
	void __iomem *ctrl_reg0;
	u8 pllen_shift;
	u8 pllen_width;
	u8 refdiv_shift;
	u8 refdiv_width;
	u8 fbdiv_shift;
	u8 fbdiv_width;

	void __iomem *ctrl_reg1;
	u8 frac_shift;
	u8 frac_width;

	void __iomem *ctrl_reg2;
	u8 postdiv1_shift;
	u8 postdiv1_width;
	u8 postdiv2_shift;
	u8 postdiv2_width;

	void __iomem *status_reg;
	u8 lock_shift;
	u8 lock_width;
	struct gpio_desc *cpu_voltage_gpio;
	enum voltage_level cpu_current_voltage;
};

struct eswin_clock_data *eswin_clk_init(struct platform_device *pdev, int nr_clks);
int eswin_clk_register_fixed_rate(const struct eswin_fixed_rate_clock *clks, int nums,
				  struct eswin_clock_data *data);

void eswin_clk_register_pll(struct eswin_pll_clock *clks, int nums,
			    struct eswin_clock_data *data, struct device *dev);

int eswin_clk_register_fixed_factor(const struct eswin_fixed_factor_clock *clks,
				    int nums, struct eswin_clock_data *data);
int eswin_clk_register_mux(const struct eswin_mux_clock *clks, int nums,
			   struct eswin_clock_data *data);

int eswin_clk_register_divider(const struct eswin_divider_clock *clks, int nums,
			       struct eswin_clock_data *data);
int eswin_clk_register_gate(const struct eswin_gate_clock *clks, int nums,
			    struct eswin_clock_data *data);

int eswin_clk_register_clk(const struct eswin_clock *clks, int nums,
			   struct eswin_clock_data *data);

#define eswin_clk_unregister(type)                                 \
	static inline void eswin_clk_unregister_##type(            \
		const struct eswin_##type##_clock *clks, int nums, \
		struct eswin_clock_data *data)                     \
	{                                                          \
		struct clk **clocks = data->clk_data.clks;         \
		int i;                                             \
		for (i = 0; i < nums; i++) {                       \
			int id = clks[i].id;                       \
			if (clocks[id])                            \
				clk_unregister_##type(clocks[id]); \
		}                                                  \
	}

eswin_clk_unregister(fixed_rate) eswin_clk_unregister(fixed_factor)
	eswin_clk_unregister(mux) eswin_clk_unregister(divider)
		eswin_clk_unregister(gate)

#endif /* __ESWIN_CLK_H__ */
