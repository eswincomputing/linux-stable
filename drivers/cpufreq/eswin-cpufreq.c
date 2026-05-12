// SPDX-License-Identifier: GPL-2.0
/*
 * eswin-cpufreq.c - ESWIN EIC772x cpufreq driver with shared regulator support
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
 */

#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/cpufreq.h>
#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/cpufreq.h>
#include <linux/regulator/consumer.h>
#include <linux/pm_opp.h>
#include <linux/of.h>
#include <linux/cpu.h>
#include <linux/cpumask.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/workqueue.h>

#define DRV_NAME            "eswin-cpufreq"
#define MAX_DOMAINS         8
#define TRANSITION_LATENCY  300000
#define VOLTAGE_DOWN_DELAY_MS 1

struct es_cpufreq_domain {
    int id;
    int leader_cpu;
    struct device *cpu_dev;
    struct clk **clks;
    int num_clks;
    struct regulator *reg;
    struct cpufreq_policy *policy;
    cpumask_t cpus;
    unsigned long cur_freq;      /* Hz */
    unsigned long cur_uV;
    unsigned long target_freq;
    unsigned long target_uV;
};

struct soc_es_cpufreq {
    int nr_domains;
    struct mutex lock;
    struct es_cpufreq_domain domains[MAX_DOMAINS];
    bool reg_shared;
    struct regulator *shared_reg;
    unsigned long global_target_uV;
    unsigned long global_cur_uV;
    struct delayed_work voltage_down_work;
    bool voltage_down_pending;
};

static struct soc_es_cpufreq *g_es_cpufreq;

static struct freq_attr *cpufreq_dt_attr[] = {
	&cpufreq_freq_attr_scaling_available_freqs,
	NULL,   /* Extra space for boost-attr if required */
	NULL,
};

/* =========================================================
 * OPP helpers
 * ========================================================= */
static unsigned long es_cpufreq_get_opp_uV(struct device *dev, unsigned long freq)
{
    struct dev_pm_opp *opp;
    unsigned long uV;

    opp = dev_pm_opp_find_freq_ceil(dev, &freq);
    if (IS_ERR(opp))
        return 0;
    uV = dev_pm_opp_get_voltage(opp);
    dev_pm_opp_put(opp);
    return uV;
}

static int es_cpufreq_set_voltage(struct regulator *reg, int min_uV, int max_uV)
{
    int ret = 0;
    if (reg && !IS_ERR(reg)) {
        ret = regulator_set_voltage(reg, min_uV, max_uV);
        if(!ret)
            pr_info("es_cpufreq set voltage to %u uV\n", min_uV);
    }

    return ret;
}

/* =========================================================
 * Shared regulator voltage commit (voting)
 * ========================================================= */
static int es_cpufreq_commit_voltage(void)
{
    unsigned long new_vmax = 0;
    int i, ret = 0;

    if (!g_es_cpufreq->reg_shared)
        return 0;

    for (i = 0; i < g_es_cpufreq->nr_domains; i++) {
        new_vmax = max(new_vmax, g_es_cpufreq->domains[i].target_uV);
    }

    if (new_vmax == g_es_cpufreq->global_target_uV)
        return 0;

    if (new_vmax < g_es_cpufreq->global_target_uV) {
        if (!g_es_cpufreq->voltage_down_pending) {
            g_es_cpufreq->voltage_down_pending = true;
            schedule_delayed_work(&g_es_cpufreq->voltage_down_work,
                                  msecs_to_jiffies(VOLTAGE_DOWN_DELAY_MS));
        }
        g_es_cpufreq->global_target_uV = new_vmax;
        return 0;
    }

    ret = regulator_set_voltage(g_es_cpufreq->shared_reg, new_vmax, new_vmax);
    if (ret) {
        dev_err(g_es_cpufreq->domains[0].cpu_dev,
                "Failed to raise voltage to %lu uV: %d\n", new_vmax, ret);
        return ret;
    }
    dev_info(g_es_cpufreq->domains[0].cpu_dev, "raise voltage to %lu uV\n", new_vmax);

    g_es_cpufreq->global_cur_uV = new_vmax;
    g_es_cpufreq->global_target_uV = new_vmax;
    return 0;
}

static void es_cpufreq_voltage_down_work(struct work_struct *work)
{
    struct soc_es_cpufreq *es = container_of(work, struct soc_es_cpufreq, voltage_down_work.work);
    unsigned long new_vmax = 0;
    int i, ret;

    mutex_lock(&es->lock);
    es->voltage_down_pending = false;

    if (!es->reg_shared) {
        mutex_unlock(&es->lock);
        return;
    }

    for (i = 0; i < es->nr_domains; i++)
        new_vmax = max(new_vmax, es->domains[i].target_uV);

    if (new_vmax < es->global_cur_uV) {
        ret = regulator_set_voltage(es->shared_reg, new_vmax, new_vmax);
        if (!ret) {
            es->global_cur_uV = new_vmax;
            es->global_target_uV = new_vmax;
            dev_info(es->domains[0].cpu_dev, "Voltage lowered to %lu uV\n", new_vmax);
        } else {
            dev_err(es->domains[0].cpu_dev, "Failed to lower voltage to %lu uV: %d\n", new_vmax, ret);
        }
    }
    mutex_unlock(&es->lock);
}

static void es_cpufreq_update_target_uv(struct es_cpufreq_domain *domain, unsigned long uV)
{
    domain->target_uV = uV;
}
static int es_cpufreq_clk_set_rate(struct es_cpufreq_domain *domain, unsigned long rate, unsigned long old_rate)
{
    int ret = 0;
    for (int i = 0; i < domain->num_clks; i++) {
        ret = clk_set_rate(domain->clks[i], rate);
        if (ret) {
            dev_err(domain->cpu_dev, "clk_set_rate(%lu) failed: %d\n", rate, ret);
            while(--i >= 0)
                clk_set_rate(domain->clks[i], old_rate);
            break;
        }
    }
    return ret;
}

/* =========================================================
 * Core frequency/voltage transition for one domain
 * ========================================================= */
static int es_cpufreq_set_rate(struct es_cpufreq_domain *domain,
                         unsigned long freq,
                         unsigned long uV)
{
    unsigned long old_freq = domain->cur_freq;
    unsigned long old_uV = domain->cur_uV;
    unsigned long rounded_freq;
    int ret = 0;

    domain->target_freq = freq;
    es_cpufreq_update_target_uv(domain, uV);

    rounded_freq = clk_round_rate(domain->clks[0], freq);
    if (rounded_freq != freq) {
        dev_dbg(domain->cpu_dev, "Requested freq %lu Hz, rounded to %lu Hz\n", freq, rounded_freq);
        freq = rounded_freq;
        uV = es_cpufreq_get_opp_uV(domain->cpu_dev, freq);
        if (!uV) {
            dev_err(domain->cpu_dev, "No OPP for rounded freq %lu Hz\n", freq);
            goto restore;
        }
        es_cpufreq_update_target_uv(domain, uV);
    }

    if (freq > old_freq) {
        if (g_es_cpufreq->reg_shared) {
            ret = es_cpufreq_commit_voltage();
            if (ret)
                goto restore;
        } else {
            if (uV != old_uV) {
                ret = es_cpufreq_set_voltage(domain->reg, uV, uV);
                if (ret)
                    goto restore;
            }
        }

        ret = es_cpufreq_clk_set_rate(domain, freq, old_freq);
        if (ret) {
            if (g_es_cpufreq->reg_shared) {
                es_cpufreq_update_target_uv(domain, old_uV);
                es_cpufreq_commit_voltage();
            } else if (uV != old_uV) {
                es_cpufreq_set_voltage(domain->reg, old_uV, old_uV);
            }
            goto restore_novote;
        }
    } else if (freq < old_freq) {
        ret = es_cpufreq_clk_set_rate(domain, freq, old_freq);
        if (ret) {
            dev_err(domain->cpu_dev, "es_cpufreq_clk_set_rate(%lu) failed: %d\n", freq, ret);
            goto restore;
        }

        if (g_es_cpufreq->reg_shared) {
            es_cpufreq_commit_voltage();
        } else {
            if (uV != old_uV) {
                ret = es_cpufreq_set_voltage(domain->reg, uV, uV);
                if (ret) {
                    es_cpufreq_clk_set_rate(domain, old_freq, old_freq);
                    goto restore;
                }
            }
        }
    } else {
        if (uV != old_uV) {
            if (g_es_cpufreq->reg_shared) {
                es_cpufreq_update_target_uv(domain, uV);
                ret = es_cpufreq_commit_voltage();
                if (ret)
                    goto restore;
            } else{
                ret = es_cpufreq_set_voltage(domain->reg, uV, uV);
                if (ret)
                    goto restore;
            }
        }
    }

    domain->cur_freq = freq;
    domain->cur_uV = uV;
    dev_info(domain->cpu_dev, "Set freq=%lu Hz\n", freq);
    return 0;

restore:
    es_cpufreq_update_target_uv(domain, old_uV);
    domain->target_freq = old_freq;
restore_novote:
    return ret;
}

/* =========================================================
 * cpufreq target callback
 * ========================================================= */
static int es_cpufreq_target_index(struct cpufreq_policy *policy, unsigned int index)
{
    struct es_cpufreq_domain *domain = policy->driver_data;
    unsigned long freq, uV;
    int ret = 0;

    if (!policy->freq_table || policy->freq_table[index].frequency == CPUFREQ_TABLE_END) {
        dev_err(domain->cpu_dev, "Invalid freq index %u\n", index);
        return -EINVAL;
    }

    freq = policy->freq_table[index].frequency * 1000UL;
    uV = es_cpufreq_get_opp_uV(domain->cpu_dev, freq);
    if (!uV) {
        dev_err(domain->cpu_dev, "No voltage for freq %lu Hz\n", freq);
        return -EINVAL;
    }

    mutex_lock(&g_es_cpufreq->lock);
    ret = es_cpufreq_set_rate(domain, freq, uV);
    mutex_unlock(&g_es_cpufreq->lock);
    return ret;
}

/* =========================================================
 * fast switch (allowed only when regulator not shared)
 * ========================================================= */
static unsigned int es_cpufreq_fast_switch(struct cpufreq_policy *policy,
                                     unsigned int target_freq)
{
    struct es_cpufreq_domain *domain = policy->driver_data;
    unsigned long freq_hz = target_freq * 1000UL;
    unsigned long uV;

    if (g_es_cpufreq->reg_shared)
        return 0;

    uV = es_cpufreq_get_opp_uV(domain->cpu_dev, freq_hz);
    if (!uV || uV != domain->cur_uV)
        return 0;

    if (es_cpufreq_clk_set_rate(domain, freq_hz, freq_hz))
        return 0;

    domain->cur_freq = freq_hz;
    return target_freq;
}

/* =========================================================
 * Topology discovery and OPP table initialization
 * ========================================================= */
static int es_cpufreq_build_topology(void)
{
    cpumask_var_t share_mask;
    cpumask_t visited;

    struct clk *clk;
    int clk_cnt = 0;
    struct clk **clk_list = NULL;
    int cpu_in_mask;
    unsigned int phandle;
    unsigned int *phandle_list;

    int cpu, domain_id = 0;
    int ret;

    cpumask_clear(&visited);
    if (!zalloc_cpumask_var(&share_mask, GFP_KERNEL))
        return -ENOMEM;

    for_each_possible_cpu(cpu) {
        struct es_cpufreq_domain *domain;
        struct device *cpu_dev;
        int leader;

        if (cpumask_test_cpu(cpu, &visited))
            continue;

        cpu_dev = get_cpu_device(cpu);
        if (!cpu_dev)
            continue;

        cpumask_clear(share_mask);
        ret = dev_pm_opp_of_get_sharing_cpus(cpu_dev, share_mask);
        if (ret)
            cpumask_set_cpu(cpu, share_mask);

        cpumask_or(&visited, &visited, share_mask);
        leader = cpumask_first(share_mask);
        cpu_dev = get_cpu_device(leader);
        if (!cpu_dev)
            continue;

        ret = dev_pm_opp_of_cpumask_add_table(share_mask);
        if (ret) {
            dev_err(cpu_dev, "Failed to add OPP table for mask %*pbl: %d\n",
                    cpumask_pr_args(share_mask), ret);
            continue;
        }

        if (domain_id >= MAX_DOMAINS) {
            dev_err(cpu_dev, "Too many cpufreq domains (max %d)\n", MAX_DOMAINS);
            ret = -ENOSPC;
            goto err_free_tables;
        }

        domain = &g_es_cpufreq->domains[domain_id];
        domain->id = domain_id;
        domain->leader_cpu = leader;
        domain->cpu_dev = cpu_dev;
        cpumask_copy(&domain->cpus, share_mask);

        clk_cnt = 0;
        clk_list = kcalloc(cpumask_weight(share_mask), sizeof(struct clk *), GFP_KERNEL);
        if(!clk_list) {
            ret = -ENOMEM;
            goto err_free_tables;
        }
        phandle_list = kcalloc(cpumask_weight(share_mask), sizeof(int), GFP_KERNEL);
        if (!phandle_list) {
            ret = -ENOMEM;
            kfree(clk_list);
            goto err_free_tables;
        }
        for_each_cpu(cpu_in_mask, share_mask) {
            struct device *cpu_dev_tmp = get_cpu_device(cpu_in_mask);
            if (!cpu_dev_tmp)
                continue;
            clk = devm_clk_get(cpu_dev_tmp, NULL);
            if (IS_ERR(clk)) {
                ret = PTR_ERR(clk);
                dev_err(cpu_dev, "Failed to get clk for CPU%d: %d\n", cpu_in_mask, ret);
                kfree(phandle_list);
                kfree(clk_list);
                goto err_free_tables;
            }
            of_property_read_u32_index(cpu_dev_tmp->of_node, "clocks", 0, &phandle);

            int already = 0;
            for (int i = 0; i < clk_cnt; i++) {
                if (phandle == phandle_list[i]) {
                    clk_put(clk);
                    already = 1;
                    break;
                }
            }
            if (!already) {
                phandle_list[clk_cnt] = phandle;
                clk_list[clk_cnt++] = clk;
            }
        }

        domain->num_clks = clk_cnt;
        domain->clks = kmemdup(clk_list, clk_cnt * sizeof(struct clk *), GFP_KERNEL);
        if (!domain->clks) {
            ret = -ENOMEM;
            kfree(phandle_list);
            kfree(clk_list);
            goto err_free_tables;
        }
        kfree(clk_list);
        kfree(phandle_list);

        domain->reg = devm_regulator_get_optional(cpu_dev, "cpu");
        if (IS_ERR(domain->reg)) {
            ret = PTR_ERR(domain->reg);
            dev_err(cpu_dev, "Failed to get regulator: %d\n", ret);
            domain->reg = NULL;
            if(ret == -EPROBE_DEFER)
                goto err_free_tables;
        }

        domain->cur_freq = clk_get_rate(domain->clks[0]);
        if (domain->cur_freq == 0) {
            /* Fallback: try to read from OPP table lowest frequency */
            struct dev_pm_opp *opp = dev_pm_opp_find_freq_ceil(domain->cpu_dev, &domain->cur_freq);
            if (!IS_ERR(opp)) {
                domain->cur_freq = dev_pm_opp_get_freq(opp);
                dev_pm_opp_put(opp);
                dev_warn(cpu_dev, "clk_get_rate returned 0, using OPP lowest freq %lu Hz\n", domain->cur_freq);
            } else {
                dev_warn(cpu_dev, "Cannot determine current frequency, using 0\n");
            }
        }
        if (domain->reg && !IS_ERR(domain->reg))
            domain->cur_uV = regulator_get_voltage(domain->reg);
        else
            domain->cur_uV = es_cpufreq_get_opp_uV(cpu_dev, domain->cur_freq);

        if (!domain->cur_uV) {
            domain->cur_uV = 0;
            dev_warn(cpu_dev, "Using fallback voltage %lu uV\n", domain->cur_uV);
        }

        domain->target_freq = domain->cur_freq;
        domain->target_uV = domain->cur_uV;

        dev_info(cpu_dev, "Domain%d: leader=%d, cur_freq=%lu Hz, cur_uV=%lu uV\n",
                 domain_id, leader, domain->cur_freq, domain->cur_uV);

        domain_id++;
    }

    free_cpumask_var(share_mask);
    g_es_cpufreq->nr_domains = domain_id;

    if (domain_id == 0) {
        dev_err(g_es_cpufreq->domains[0].cpu_dev, "No cpufreq domain found\n");
        return -ENODEV;
    }

    return 0;

err_free_tables:
    for (--domain_id; domain_id >= 0; domain_id--) {
        kfree(g_es_cpufreq->domains[domain_id].clks);
        dev_pm_opp_of_cpumask_remove_table(&g_es_cpufreq->domains[domain_id].cpus);
    }
    free_cpumask_var(share_mask);
    return ret;
}

static int es_cpufreq_check_regulator_sharing(void)
{
    struct regulator *first_reg = NULL;
    int i;

    for (i = 0; i < g_es_cpufreq->nr_domains; i++) {
        struct regulator *reg = g_es_cpufreq->domains[i].reg;
        if(reg == NULL) {
            g_es_cpufreq->reg_shared = false;
            return 0;
        }

        if (!first_reg)
            first_reg = reg;
        else if (!regulator_is_equal(reg, first_reg)) {
            g_es_cpufreq->reg_shared = false;
            return 0;
        }
    }
    g_es_cpufreq->reg_shared = true;
    g_es_cpufreq->shared_reg = g_es_cpufreq->domains[0].reg;
    dev_info(g_es_cpufreq->domains[0].cpu_dev, "All domains share the same regulator\n");
    return 0;
}

/* =========================================================
 * cpufreq driver callbacks
 * ========================================================= */
static int es_cpufreq_cpufreq_init(struct cpufreq_policy *policy)
{
    struct es_cpufreq_domain *domain = NULL;
    int i, ret, table_entries = 0;

    for (i = 0; i < g_es_cpufreq->nr_domains; i++) {
        if (cpumask_test_cpu(policy->cpu, &g_es_cpufreq->domains[i].cpus)) {
            domain = &g_es_cpufreq->domains[i];
            break;
        }
    }
    if (!domain)
        return -ENODEV;

    policy->driver_data = domain;
    policy->clk = domain->clks[0];
    cpumask_copy(policy->cpus, &domain->cpus);
    policy->cpuinfo.transition_latency = TRANSITION_LATENCY;
    policy->fast_switch_possible = !g_es_cpufreq->reg_shared;

    /* Set current frequency in kHz (required by cpufreq framework) */
    policy->cur = domain->cur_freq / 1000;

    ret = dev_pm_opp_init_cpufreq_table(domain->cpu_dev, &policy->freq_table);
    if (ret) {
        dev_err(domain->cpu_dev, "Failed to init cpufreq table: %d\n", ret);
        return ret;
    }

    while (policy->freq_table[table_entries].frequency != CPUFREQ_TABLE_END)
        table_entries++;
    if (table_entries == 0) {
        dev_err(domain->cpu_dev, "Freq table has 0 entries!\n");
        dev_pm_opp_free_cpufreq_table(domain->cpu_dev, &policy->freq_table);
        return -ENOENT;
    }
    dev_info(domain->cpu_dev, "Freq table has %d entries (kHz):", table_entries);
    for (i = 0; i < table_entries; i++)
        pr_cont(" %u", policy->freq_table[i].frequency);
    pr_cont("\n");

    domain->policy = policy;
    return 0;
}

static int es_cpufreq_cpufreq_exit(struct cpufreq_policy *policy)
{
    struct es_cpufreq_domain *domain = policy->driver_data;
    dev_pm_opp_free_cpufreq_table(domain->cpu_dev, &policy->freq_table);
    return 0;
}

static struct cpufreq_driver es_cpufreq_driver = {
    .name       = DRV_NAME,
    .flags      = CPUFREQ_IS_COOLING_DEV | CPUFREQ_NEED_INITIAL_FREQ_CHECK,
    .verify     = cpufreq_generic_frequency_table_verify,
    .target_index = es_cpufreq_target_index,
    .fast_switch  = es_cpufreq_fast_switch,
    .get        = cpufreq_generic_get,
    .init       = es_cpufreq_cpufreq_init,
    .exit       = es_cpufreq_cpufreq_exit,
	.register_em = cpufreq_register_em_with_opp,
	.attr = cpufreq_dt_attr,
};

static int es_cpufreq_probe(struct platform_device *pdev)
{
    int ret, i;

    g_es_cpufreq = devm_kzalloc(&pdev->dev, sizeof(*g_es_cpufreq), GFP_KERNEL);
    if (!g_es_cpufreq)
        return -ENOMEM;

    mutex_init(&g_es_cpufreq->lock);
    INIT_DELAYED_WORK(&g_es_cpufreq->voltage_down_work, es_cpufreq_voltage_down_work);
    g_es_cpufreq->voltage_down_pending = false;

    ret = es_cpufreq_build_topology();
    if (ret)
        return ret;

    ret = es_cpufreq_check_regulator_sharing();
    if (ret)
        goto err_free_opp;

    if (g_es_cpufreq->reg_shared) {
        unsigned long initial_vmax = 0;
        for (i = 0; i < g_es_cpufreq->nr_domains; i++)
            initial_vmax = max(initial_vmax, g_es_cpufreq->domains[i].cur_uV);
        g_es_cpufreq->global_cur_uV = initial_vmax;
        g_es_cpufreq->global_target_uV = initial_vmax;
        ret = regulator_set_voltage(g_es_cpufreq->shared_reg, initial_vmax, initial_vmax);
        if (ret)
            dev_warn(&pdev->dev, "Failed to set initial voltage %lu: %d\n", initial_vmax, ret);
        else
            dev_info(&pdev->dev, "Initial shared voltage set to %lu uV\n", initial_vmax);
    }

    ret = cpufreq_register_driver(&es_cpufreq_driver);
    if (ret) {
        dev_err(&pdev->dev, "Failed to register cpufreq driver: %d\n", ret);
        goto err_free_opp;
    }

    dev_info(&pdev->dev, "ESWIN EIC772x cpufreq loaded, reg_shared=%d, domains=%d\n",
             g_es_cpufreq->reg_shared, g_es_cpufreq->nr_domains);
    return 0;

err_free_opp:
    for (i = 0; i < g_es_cpufreq->nr_domains; i++) {
        kfree(g_es_cpufreq->domains[i].clks);
        dev_pm_opp_of_cpumask_remove_table(&g_es_cpufreq->domains[i].cpus);
    }
    return ret;
}

static int es_cpufreq_remove(struct platform_device *pdev)
{
    int i;

    cpufreq_unregister_driver(&es_cpufreq_driver);
    cancel_delayed_work_sync(&g_es_cpufreq->voltage_down_work);

    for (i = 0; i < g_es_cpufreq->nr_domains; i++) {
        kfree(g_es_cpufreq->domains[i].clks);
        dev_pm_opp_of_cpumask_remove_table(&g_es_cpufreq->domains[i].cpus);
    }

    return 0;
}

static const struct of_device_id es_of_match[] = {
    { .compatible = "eswin,eic772x-cpufreq" },
    {}
};
MODULE_DEVICE_TABLE(of, es_of_match);

static struct platform_driver eic772x_cpufreq_platdrv = {
    .probe  = es_cpufreq_probe,
    .remove = es_cpufreq_remove,
    .driver = {
        .name = DRV_NAME,
        .of_match_table = es_of_match,
    },
};
module_platform_driver(eic772x_cpufreq_platdrv);


MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("ESWIN EIC772x cpufreq driver with shared regulator support");
MODULE_AUTHOR("XuXiang <xuxiang@eswincomputing.com>");
