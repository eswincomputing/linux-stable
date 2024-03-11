/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/debugfs.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/seq_file.h>
/*
	1.mount -t debugfs none /sys/kernel/debug
*/
#define CLKDBG_ARGS_MAX			3
#define CLKDBG_INSTR_MAX_LEN	128
#define CLKDBG_DEFAULT_SEP_STR	" \n"

enum {
	CLKDBG_CMD_ENABLE,
	CLKDBG_CMD_DISABLE,
	CLKDBG_CMD_PREPARE,
	CLKDBG_CMD_UNPREPARE,
	CLKDBG_CMD_PREPARE_ENABLE,
	CLKDBG_CMD_DISABLE_UNPREPARE,
	CLKDBG_CMD_SETRATE,
	CLKDBG_CMD_ROUNDRATE,
	CLKDBG_CMD_GETRATE,
	CLKDBG_CMD_SETPARENT,
	CLKDBG_CMD_GETPARENT,
	CLKDBG_CMD_NR,
};

enum {
	CLKDBG_ERR_NO = 0x0,
	CLKDBG_ERR_PARSE_CMD = 0x1,
	CLKDBG_ERR_PARSE_SELF = 0x2,
	CLKDBG_ERR_PARSE_ARGS = 0x4,
	CLKDBG_ERR_EXEC_CMD = 0x10,
};
#define CLKDBG_ERR_PARSE	(CLKDBG_ERR_PARSE_CMD |\
							CLKDBG_ERR_PARSE_SELF |\
							CLKDBG_ERR_PARSE_ARGS)
#define CLKDBG_ERR_EXEC		(CLKDBG_ERR_EXEC_CMD)

enum {
	CLKDBG_STS_COMPLETE,
	CLKDBG_STS_START,
};

struct cmd_packet {
	unsigned int cmd;
	struct clk *self;
	struct clk_hw *self_hw;
	struct clk *parent;
	struct clk_hw *parent_hw;
	unsigned long rate;
	int result;
	int status;
};

struct clkdbg {
	void *instr;
	struct dentry *clkdbg_dir;
	struct cmd_packet *cmdp;
};

static struct clkdbg *clkdbg = NULL;

static const char * const cmd_str[CLKDBG_CMD_NR] = {
	"enable",
	"disable",
	"prepare",
	"unprepare",
	"prepare_enable",
	"disable_unprepare",
	"set_rate",
	"round_rate",
	"get_rate",
	"set_parent",
	"get_parent",
};

static const char * const hlp_str[CLKDBG_CMD_NR] = {
	"enable\t\t\t: echo enable {clk} > clkdbg\n",
	"disable\t\t\t: echo disable {clk} > clkdbg\n",
	"prepare\t\t\t: echo prepare {clk} > clkdbg\n",
	"unprepare\t\t: echo unprepare {clk} > clkdbg\n",
	"prepare_enable\t\t: echo prepare {clk} > clkdbg\n",
	"disable_unprepare\t: echo unprepare {clk} > clkdbg\n",
	"set_rate\t\t: echo set_rate {clk} {rate(Hz)} > clkdbg\n",
	"round_rate\t\t: echo round_rate {clk} {rate(Hz)} > clkdbg\n",
	"get_rate\t\t: echo get_rate {clk} > clkdbg\n",
	"set_parent\t\t: echo set_parent {clk} {parent} > clkdbg\n",
	"get_parent\t\t: echo get_parent {clk} > clkdbg\n",
};

static int clkdbg_parse_cmd(char **str, char *sep,
							struct cmd_packet *cmdp)
{
	char *str_sec;
	int ret, cmd = 0;

	/* parse the first section string. */
	str_sec = strsep(str, sep);
	if (str_sec == NULL)
		return -EINVAL;

	/*
	 * search request keyword to find predefined keywords:
	 * enable, disable, prepare, unprepare, set_rate, set_parent.
	 */
	do {
		ret = strcmp(str_sec, cmd_str[cmd]);
	} while (ret && (++cmd < CLKDBG_CMD_NR));

	/* assign and record command value. */
	cmdp->cmd = cmd;

	pr_info("cmd: %d\n", cmdp->cmd);

	return ret;
}

static int clkdbg_parse_selfclk(char **str, char *sep,
								  struct cmd_packet *cmdp)
{
	char *str_sec;

	/* parse the self clock name. */
	str_sec = strsep(str, sep);
	if (str_sec == NULL)
		return -EINVAL;

	/*
	 * try to lookup self clock reference, and return directly
	 * if failed.
	 */
	cmdp->self = __clk_lookup(str_sec);
	if (!cmdp->self)
		return -EINVAL;

	cmdp->self_hw = __clk_get_hw(cmdp->self);

	pr_info("self: \"%s\"\n", __clk_get_name(cmdp->self));

	return 0;
}

static int clkdbg_parse_args(char **str, char *sep,
							 struct cmd_packet *cmdp)
{
	char *args[CLKDBG_ARGS_MAX];
	int idx = 0, ret = 0;

	/* parse argument list and store them. */
	do {
		args[idx] = strsep(str, sep);
	} while (args[idx] && (++idx < CLKDBG_ARGS_MAX));

	switch (cmdp->cmd) {
	case CLKDBG_CMD_SETRATE:
	case CLKDBG_CMD_ROUNDRATE:
		ret = kstrtoul(args[0], 0, &cmdp->rate);
		pr_info("rate: %ld\n", cmdp->rate);
		break;
	case CLKDBG_CMD_SETPARENT:
		cmdp->parent = __clk_lookup(args[0]);
		if (!cmdp->parent)
			ret = -EINVAL;
		pr_info("parent: \"%s\"\n", __clk_get_name(cmdp->parent));
		break;
	default:
		ret = 0;
		break;
	}

	return ret;
}

static int clkdbg_parse_str(char **str, char *sep,
							struct cmd_packet *cmdp)
{
	int ret = 0;

	ret = clkdbg_parse_cmd(str, sep, cmdp);
	if (ret) {
		cmdp->result = CLKDBG_ERR_PARSE_CMD;
		goto out;
	}

	ret = clkdbg_parse_selfclk(str, sep, cmdp);
	if (ret) {
		cmdp->result = CLKDBG_ERR_PARSE_SELF;
		goto out;
	}

	ret = clkdbg_parse_args(str, sep, cmdp);
	if (ret) {
		cmdp->result = CLKDBG_ERR_PARSE_ARGS;
		goto out;
	}

out:
	return ret;
}

static int clkdbg_execute_cmd(struct clkdbg *clkdbg)
{
	struct cmd_packet *cmdp = clkdbg->cmdp;
	int ret = 0;

	switch (cmdp->cmd) {
	case CLKDBG_CMD_PREPARE:
		ret = clk_prepare(cmdp->self);
		break;
	case CLKDBG_CMD_UNPREPARE:
		clk_unprepare(cmdp->self);
		break;
	case CLKDBG_CMD_ENABLE:
		ret = clk_enable(cmdp->self);
		break;
	case CLKDBG_CMD_DISABLE:
		clk_disable(cmdp->self);
		break;
	case CLKDBG_CMD_PREPARE_ENABLE:
		ret = clk_prepare_enable(cmdp->self);
		break;
	case CLKDBG_CMD_DISABLE_UNPREPARE:
		clk_disable_unprepare(cmdp->self);
		break;
	case CLKDBG_CMD_SETRATE:
		ret = clk_set_rate(cmdp->self, cmdp->rate);
		break;
	case CLKDBG_CMD_ROUNDRATE:
		ret = clk_round_rate(cmdp->self, cmdp->rate);
		if (ret >= 0) {
			cmdp->rate = ret;
			ret = 0;
		}
		break;
	case CLKDBG_CMD_GETRATE:
		cmdp->rate = clk_get_rate(cmdp->self);
		break;
	case CLKDBG_CMD_SETPARENT:
		ret = clk_set_parent(cmdp->self, cmdp->parent);
		break;
	case CLKDBG_CMD_GETPARENT:
		cmdp->parent = clk_get_parent(cmdp->self);
		cmdp->parent_hw = __clk_get_hw(cmdp->parent);
		break;
	default:
		break;
	}

	if (ret)
		cmdp->result = CLKDBG_ERR_EXEC_CMD;

	return ret;
}

static ssize_t clkdbg_write(struct file *filp,
							const char __user *userbuf,
							size_t count, loff_t *ppos)
{
	struct clkdbg *clkdbg =
			((struct seq_file *)filp->private_data)->private;
	struct cmd_packet *cmdp = clkdbg->cmdp;
	char *sep = CLKDBG_DEFAULT_SEP_STR;
	char *instr = clkdbg->instr;
	int ret;

	memset(instr, 0x0, CLKDBG_INSTR_MAX_LEN);
	memset(cmdp, 0x0, sizeof(*cmdp));
	clkdbg->cmdp->status = CLKDBG_STS_START;
	clkdbg->cmdp->result = CLKDBG_ERR_NO;

	ret = copy_from_user(instr, userbuf, count);
	if (ret) {
		ret = -EFAULT;
		goto out;
	}

	/*
	 * parse the input string and write cmd, argument list,
	 * self clock and parent(if exists) into cmd_packet struct.
	 */
	ret = clkdbg_parse_str(&instr, sep, cmdp);
	if (ret < 0)
		goto out;

	/* execute clock command. */
	ret = clkdbg_execute_cmd(clkdbg);

out:
	return count;
}

static int clkdbg_show(struct seq_file *s, void *data)
{
	struct clkdbg *clkdbg = (struct clkdbg *)s->private;
	struct cmd_packet *cmdp = clkdbg->cmdp;
	struct clk_hw *clk_hw;
	//const char *pname_c, *pname;
	int idx, num_parents;

	/* default helper string printed. */
	if (cmdp->status == CLKDBG_STS_COMPLETE) {
		for (idx = CLKDBG_CMD_ENABLE; idx < CLKDBG_CMD_NR; idx++)
			seq_printf(s, "%s", hlp_str[idx]);
		return 0;
	}

	if (cmdp->result & CLKDBG_ERR_PARSE) {
		seq_printf(s, "parse cmd string failed(%d)!\n", cmdp->result);
		goto out;
	}

	seq_printf(s, "%s \"%s\"",
			   cmd_str[cmdp->cmd], __clk_get_name(cmdp->self));

	switch (cmdp->cmd) {
	case CLKDBG_CMD_SETRATE:
	case CLKDBG_CMD_ROUNDRATE:
		seq_printf(s, " as %ld ", cmdp->rate);
		break;
	case CLKDBG_CMD_GETRATE:
		seq_printf(s, ":\n\t%ld Hz\n", cmdp->rate);
		break;
	case CLKDBG_CMD_SETPARENT:
		seq_printf(s, " as \"%s\" ", __clk_get_name(cmdp->parent));
		break;
	case CLKDBG_CMD_GETPARENT:
		seq_puts(s, ":\n");
		num_parents = clk_hw_get_num_parents(cmdp->self_hw);
		for (idx = 0; idx < num_parents; idx++) {
			clk_hw = clk_hw_get_parent_by_index(cmdp->self_hw, idx);
			if (clk_hw == cmdp->parent_hw)
				seq_puts(s, "\t[*]");
			else
				seq_puts(s, "\t[ ]");

			seq_printf(s, "%s\n", __clk_get_name(clk_hw->clk));
		}
		break;
	default:
		break;
	}

	seq_printf(s, "%s\n", cmdp->result ? "failed" : "success");

out:
	cmdp->status = CLKDBG_STS_COMPLETE;

	return 0;
}

static int clkdbg_open(struct inode *inode, struct file *file)
{
	return single_open(file, clkdbg_show, inode->i_private);
}

static const struct file_operations clkdbg_ops = {
	.owner		= THIS_MODULE,
	.open		= clkdbg_open,
	.release	= single_release,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.write		= clkdbg_write,
};

static int clkdbg_add_debugfs(void)
{
	if (!debugfs_initialized())
		return 0;

	clkdbg->clkdbg_dir = debugfs_create_file("clkdbg", 0600, NULL, clkdbg, &clkdbg_ops);

	return 0;
}

static int __init clkdbg_init(void)
{
	clkdbg = kzalloc(sizeof(*clkdbg), GFP_KERNEL);
	if (!clkdbg)
		return -ENOMEM;

	clkdbg->instr = kzalloc(CLKDBG_INSTR_MAX_LEN, GFP_KERNEL);
	if (!clkdbg->instr)
		return -ENOMEM;

	clkdbg->cmdp = kzalloc(sizeof(struct cmd_packet), GFP_KERNEL);
	if (!clkdbg->instr)
		return -ENOMEM;

	return clkdbg_add_debugfs();
}

static void __exit clkdbg_exit(void)
{
	if (clkdbg && clkdbg->clkdbg_dir)
		debugfs_remove_recursive(clkdbg->clkdbg_dir);

	if (clkdbg)
		kfree(clkdbg);
}

module_init(clkdbg_init);
module_exit(clkdbg_exit);

MODULE_DESCRIPTION("CCF Debug Driver");
MODULE_AUTHOR("Shunli Wang <wangshunli@cambricon.com>");
MODULE_LICENSE("GPL v2");
