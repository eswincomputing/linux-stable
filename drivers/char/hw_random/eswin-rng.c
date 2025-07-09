// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright 2025, Beijing ESWIN Computing Technology Co., Ltd.. All rights reserved.
 *
 * RNG driver for ESWIN EIC770X SoC
 *
 * Authors:
 *	Hang Cao <caohang@eswincomputing.com>
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/err.h>
#include <linux/kernel.h>
#include <linux/hw_random.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/random.h>
#include <linux/platform_device.h>
#include <uapi/linux/eswin-ipc-scpu.h>

struct eswin_rng {
    struct platform_device *parent;
    struct hwrng rng;
    cipher_create_handle_req_t *req_handle;
    struct device *dev;
    res_service_t *res_srv;
};

static int eswin_rng_init(struct hwrng *rng)
{
    struct eswin_rng *es_rng = (struct eswin_rng *)rng->priv;

    es_rng->req_handle = kzalloc(sizeof(cipher_create_handle_req_t), GFP_KERNEL);
    if (!es_rng->req_handle) {
        pr_err("Failed to allocate memory for req_handle\n");
        return -ENOMEM;
    }

    es_rng->res_srv = kzalloc(sizeof(res_service_t), GFP_KERNEL);
    if (!es_rng->req_handle) {
        pr_err("Failed to allocate memory for res_srv\n");
        return -ENOMEM;
    }

    return 0;
}

static void eswin_rng_cleanup(struct hwrng *rng)
{
    struct eswin_rng *es_rng = (struct eswin_rng *)rng->priv;

    if (es_rng->req_handle)
        kfree(es_rng->req_handle);

    if (es_rng->res_srv)
        kfree(es_rng->res_srv);
}

static int eswin_rng_read(struct hwrng *rng, void *buf, size_t max_len, bool wait)
{
    int ret = 0;
    struct eswin_rng *es_rng = (struct eswin_rng *)rng->priv;
	int got_data_size = 0;

    dev_dbg(es_rng->dev, "Eswin RNG read called, max_len = %ld, wait = %d\n", max_len, wait);

    if (!wait) {
        return 0;
    }

    es_rng->req_handle->service_req.serivce_type = SRVC_TYPE_TRNG;
    es_rng->req_handle->service_req.data.trng_req.flag = max_len;
    ret = eswin_ipc_session_kernel_request((void *)es_rng->parent, es_rng->req_handle, es_rng->res_srv);
    if (ret >= 0) {
        got_data_size = es_rng->res_srv->size;
        ret = max_len < got_data_size ? max_len : got_data_size;
        memcpy(buf, es_rng->res_srv->data_t.trng_res.data, ret);
    }

    return ret;
}

static int eswin_rng_dev_nid(struct device *dev, int *p_nid)
{
    int nid;

#ifdef CONFIG_NUMA
    nid = dev_to_node(dev);
    if (nid == NUMA_NO_NODE) {
        pr_err("%s:%d, numa-node-id was not defined!\n", __func__, __LINE__);
        return -EFAULT;
    }
#else
    if (of_property_read_s32(dev->of_node, "numa-node-id", &nid)) {
        pr_err("%s:%d, numa-node-id was not defined!\n", __func__, __LINE__);
        return -EFAULT;
    }
#endif

    *p_nid = nid;

    return 0;
}

static int eswin_rng_probe(struct platform_device *pdev)
{
    int ret;
    struct eswin_rng *es_rng;
    struct device_node *np;
    struct platform_device *platform_dev;
    int nid;

    np = of_get_parent(pdev->dev.of_node);
    platform_dev = of_find_device_by_node(np);
    of_node_put(np);

    // dev_info(&pdev->dev, "%s Found parent platform device: %s\n", pdev->name, platform_dev->name);

    ret = eswin_rng_dev_nid(&platform_dev->dev, &nid);
    if (ret) {
        dev_err(&pdev->dev, "failed to ipc's nodeID\n");
        return ret;
    }

    if (!eswin_ipc_session_service_ready((void *)platform_dev)) {
        dev_info(&pdev->dev, "ipc service not ready, probe deferred.");
        return -EPROBE_DEFER;
    }

    es_rng = devm_kzalloc(&pdev->dev, sizeof(*es_rng), GFP_KERNEL);
    if (!es_rng)
        return -ENOMEM;

    es_rng->rng.priv = (unsigned long)es_rng;
    es_rng->dev = &pdev->dev;
    es_rng->parent = platform_dev;
    es_rng->rng.init = eswin_rng_init;
    es_rng->rng.cleanup = eswin_rng_cleanup;
    es_rng->rng.read = eswin_rng_read;
    es_rng->rng.name = devm_kasprintf(&pdev->dev, GFP_KERNEL, "%s%d", pdev->name, nid);

    ret = devm_hwrng_register(&pdev->dev, &es_rng->rng);
    if (ret) {
        dev_err(&pdev->dev, "failed to register eswin-hwrng\n");
        goto REGISTER_ERROR;
    }

    platform_set_drvdata(pdev, es_rng);

    return ret;

REGISTER_ERROR:
    if (es_rng)
        kfree(es_rng);

    return ret;
}

static int eswin_rng_remove(struct platform_device *pdev)
{
    struct eswin_rng *es_rng = platform_get_drvdata(pdev);

    devm_hwrng_unregister(&pdev->dev, &es_rng->rng);

    if (es_rng)
        kfree(es_rng);

    dev_info(&pdev->dev, "Eswin RNG driver removed\n");
    return 0;
}

static const struct of_device_id eswin_rng_dt_ids[] = {
    {.compatible = "eswin,ex77xx-trng"},
    {},
};

static struct platform_driver eswin_rng_driver = {
    .driver = {
        .name = "eswin-rng",
        .of_match_table = eswin_rng_dt_ids,
    },
    .probe = eswin_rng_probe,
    .remove = eswin_rng_remove,
};

module_platform_driver(eswin_rng_driver);
MODULE_LICENSE("GPL v2");