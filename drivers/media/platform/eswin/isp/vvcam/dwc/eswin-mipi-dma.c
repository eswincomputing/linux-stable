#include <linux/list.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_graph.h>
#include <linux/platform_device.h>

#include <media/v4l2-device.h>
#include <media/v4l2-dev.h>
#include <media/videobuf2-v4l2.h>
#include <media/v4l2-ioctl.h>
#include <media/videobuf2-dma-contig.h>
#include <media/v4l2-fwnode.h>
#include <media/v4l2-async.h>
#include <media/v4l2-subdev.h>
#include <linux/dev_printk.h>

struct csi_mipi_dma
{
	struct device *dev;

	struct v4l2_device v4l2_dev;
	struct media_device media_dev;

	struct v4l2_async_notifier notifier;

	struct video_device video;
	struct media_pad pad;

	struct vb2_queue queue;

	struct mutex lock;

	struct v4l2_subdev sd;
	struct media_pad pads[8];
	struct v4l2_subdev *src_sd;
	struct v4l2_mbus_config_mipi_csi2 bus;
};

struct csi_dma_buffer
{
	struct vb2_v4l2_buffer buf;
	struct list_head queue;
	struct csi_mipi_dma *dma;
};

// static int csi_dma_fwnode_parse(struct device *dev,
// 				struct v4l2_fwnode_endpoint *vep,
// 				struct v4l2_async_subdev *asd)
// {
// 	switch (vep->bus_type) {
// 	case V4L2_MBUS_CSI2_DPHY:
// 		return 0;
// 	default:
// 		dev_err(dev, "Unsupported media bus type\n");
// 		return -ENOTCONN;
// 	}
// }

static int csi_dma_graph_notify_complete(struct v4l2_async_notifier *notifier)
{
	int ret;
	struct csi_mipi_dma *dma =
	    container_of(notifier, struct csi_mipi_dma, notifier);

	ret = v4l2_device_register_subdev_nodes(&dma->v4l2_dev); /*create all subdev node*/
	if (ret < 0)
		dev_err(dma->dev, "failed to register subdev nodes\n");

	return media_device_register(&dma->media_dev);
}

static int csi_dma_graph_notify_bound(struct v4l2_async_notifier *notifier,
				      struct v4l2_subdev *sd,
				      struct v4l2_async_connection *asd)
{
	printk("%s %d\n", __func__, __LINE__);
	return 0;
}

static const struct v4l2_async_notifier_operations csi_dma_graph_notify_ops = {
    .bound = csi_dma_graph_notify_bound,
    .complete = csi_dma_graph_notify_complete,
};

static int csi_dma_queue_setup(struct vb2_queue *vq, unsigned int *nbuffers,
			       unsigned int *nplanes, unsigned int sizes[],
			       struct device *alloc_devs[])
{
	return 0;
}

static void csi_dma_buffer_queue(struct vb2_buffer *vb)
{
}

static const struct vb2_ops csi_dma_queue_qops = {
    .queue_setup = csi_dma_queue_setup,
    .buf_queue = csi_dma_buffer_queue,
};

static const struct v4l2_ioctl_ops csi_dma_ioctl_ops = {
    .vidioc_reqbufs = vb2_ioctl_reqbufs,
    .vidioc_querybuf = vb2_ioctl_querybuf,
    .vidioc_qbuf = vb2_ioctl_qbuf,
    .vidioc_dqbuf = vb2_ioctl_dqbuf,
    .vidioc_create_bufs = vb2_ioctl_create_bufs,
    .vidioc_expbuf = vb2_ioctl_expbuf,
    .vidioc_streamon = vb2_ioctl_streamon,
    .vidioc_streamoff = vb2_ioctl_streamoff,
};

static const struct v4l2_file_operations csi_dma_fops = {
    .owner = THIS_MODULE,
    .unlocked_ioctl = video_ioctl2,
    .open = v4l2_fh_open,
    .release = vb2_fop_release,
    .poll = vb2_fop_poll,
    .mmap = vb2_fop_mmap,
};

static int cio2_parse_firmware(struct csi_mipi_dma *cio2)
{
	struct device *dev = cio2->dev;
	// unsigned int i;
	int ret;

	struct v4l2_fwnode_endpoint vep = {
	    .bus_type = V4L2_MBUS_CSI2_DPHY};
	struct v4l2_async_connection *asd;
	struct fwnode_handle *ep;

	ep = fwnode_graph_get_endpoint_by_id(dev_fwnode(dev), 0, 0,
					     FWNODE_GRAPH_ENDPOINT_NEXT);
	if (!ep)
	{
		dev_err(dev, "failed to fwnode_graph_get_endpoint_by_id\n");
		return -ENOTCONN;
	}

	ret = v4l2_fwnode_endpoint_parse(ep, &vep);
	if (ret)
		dev_err(dev, "failed to v4l2_fwnode_endpoint_parse : %d\n", ret);

	asd = v4l2_async_nf_add_fwnode_remote(&cio2->notifier, ep,
					      struct v4l2_async_connection);
	if (IS_ERR(asd))
	{
		ret = PTR_ERR(asd);
	}

	fwnode_handle_put(ep);

	/*
	 * Proceed even without sensors connected to allow the device to
	 * suspend.
	 */
	cio2->notifier.ops = &csi_dma_graph_notify_ops;
	ret = v4l2_async_nf_register(&cio2->notifier);
	if (ret)
		dev_err(dev, "failed to register async notifier : %d\n", ret);

	return ret;
}

static int csi_composite_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct csi_mipi_dma *csi_dma;
	// struct v4l2_async_subdev *asd;

	int ret;

	dev_info(dev, "Probing started\n");

	csi_dma = devm_kzalloc(dev, sizeof(*csi_dma), GFP_KERNEL);
	if (!dev)
		return -ENOMEM;

	csi_dma->dev = dev;
	mutex_init(&csi_dma->lock);

	/* media device init */
	csi_dma->media_dev.dev = dev;
	strscpy(csi_dma->media_dev.model, "eswin MIPI CSI DMA", sizeof(csi_dma->media_dev.model));
	csi_dma->media_dev.hw_revision = 0;
	media_device_init(&csi_dma->media_dev);

	/* v4l2 device register */
	csi_dma->v4l2_dev.mdev = &csi_dma->media_dev;
	ret = v4l2_device_register(dev, &csi_dma->v4l2_dev);

	if (ret < 0)
	{
		dev_err(dev, "V4L2 device registration failed (%d)\n", ret);
		media_device_cleanup(&csi_dma->media_dev);
		return ret;
	}

	/* video device init */
	csi_dma->video.fops = &csi_dma_fops;
	csi_dma->video.v4l2_dev = &csi_dma->v4l2_dev;
	csi_dma->video.queue = &csi_dma->queue;
	snprintf(csi_dma->video.name, sizeof(csi_dma->video.name), "eswin mipi csi dma");

	csi_dma->video.vfl_type = VFL_TYPE_VIDEO;
	csi_dma->video.vfl_dir = VFL_DIR_RX;
	csi_dma->video.release = video_device_release_empty;
	csi_dma->video.ioctl_ops = &csi_dma_ioctl_ops;
	csi_dma->video.lock = &csi_dma->lock;
	csi_dma->video.device_caps = V4L2_CAP_STREAMING | V4L2_CAP_VIDEO_CAPTURE;

	video_set_drvdata(&csi_dma->video, csi_dma);

	/* init vb queue */
	csi_dma->queue.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	csi_dma->queue.io_modes = VB2_MMAP | VB2_USERPTR | VB2_DMABUF;
	csi_dma->queue.lock = &csi_dma->lock;
	csi_dma->queue.drv_priv = csi_dma;
	csi_dma->queue.buf_struct_size = sizeof(struct csi_dma_buffer);
	csi_dma->queue.ops = &csi_dma_queue_qops;
	csi_dma->queue.mem_ops = &vb2_dma_contig_memops;
	csi_dma->queue.timestamp_flags = V4L2_BUF_FLAG_TIMESTAMP_MONOTONIC |
					 V4L2_BUF_FLAG_TSTAMP_SRC_EOF;
	csi_dma->queue.dev = dev;

	ret = vb2_queue_init(&csi_dma->queue);
	if (ret < 0)
	{
		dev_err(dev, "failed to initialize VB2 queue\n");
		goto clean_out;
	}

	/* video device register */
	ret = video_register_device(&csi_dma->video, VFL_TYPE_VIDEO, -1);
	if (ret < 0)
	{
		dev_err(dev, "failed to register video device\n");
		goto clean_out;
	}

	// 	/* Register the subdevices notifier. */
	v4l2_async_nf_init(&csi_dma->notifier, &csi_dma->v4l2_dev);

	ret = cio2_parse_firmware(csi_dma);
	if (ret < 0)
	{
		dev_err(dev, "notifier registration failed\n");
		goto clean_out;
	}

	platform_set_drvdata(pdev, csi_dma);

	dev_info(dev, "Probing End\n");

	return 0;

clean_out:
	v4l2_device_unregister(&csi_dma->v4l2_dev);
	v4l2_async_nf_cleanup(&csi_dma->notifier);
	media_device_unregister(&csi_dma->media_dev);
	media_device_cleanup(&csi_dma->media_dev);

	return ret;
}

static int csi_composite_remove(struct platform_device *pdev)
{
	struct csi_mipi_dma *csi_dma = platform_get_drvdata(pdev);

	video_unregister_device(&csi_dma->video);
	v4l2_async_nf_unregister(&csi_dma->notifier);
	v4l2_async_nf_cleanup(&csi_dma->notifier);
	v4l2_device_unregister(&csi_dma->v4l2_dev);
	media_device_unregister(&csi_dma->media_dev);
	media_device_cleanup(&csi_dma->media_dev);

	return 0;
}

static const struct of_device_id eswin_composite_of_id_table[] = {
    {.compatible = "eswin,csi-video"},
    {}};
MODULE_DEVICE_TABLE(of, eswin_composite_of_id_table);

static struct platform_driver eswin_composite_driver = {
    .driver = {
	.name = "eswin-csi-video",
	.of_match_table = eswin_composite_of_id_table,
    },
    .probe = csi_composite_probe,
    .remove = csi_composite_remove,
};

module_platform_driver(eswin_composite_driver);

MODULE_AUTHOR("Y o n g Y a n g, <yangyong@eswin.com>");
MODULE_DESCRIPTION("A MIPI DMA CONTROLLER");
MODULE_LICENSE("GPL v2");
