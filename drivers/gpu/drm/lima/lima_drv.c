#include <linux/module.h>
#include <linux/of_platform.h>
#include <drm/drm_prime.h>

#include "lima.h"


static inline struct lima_device *to_lima_dev(struct drm_device *dev)
{
	return dev->dev_private;
}

static int lima_ioctl_info(struct drm_device *dev, void *data, struct drm_file *file)
{
	struct drm_lima_info *info = data;
	struct lima_device *ldev = to_lima_dev(dev);

	switch (ldev->gpu_type) {
	case GPU_MALI400:
		info->gpu_id = LIMA_INFO_GPU_MALI400;
		break;
	case GPU_MALI450:
		info->gpu_id = LIMA_INFO_GPU_MALI450;
		break;
	default:
		return -ENODEV;
	}
	info->num_pp = ldev->pp->num_core;
	return 0;
}

static int lima_ioctl_gem_create(struct drm_device *dev, void *data, struct drm_file *file)
{
	struct drm_lima_gem_create *args = data;

	if (args->flags || args->size == 0)
		return -EINVAL;

	return lima_gem_create_handle(dev, file, args->size, args->flags, &args->handle);
}

static int lima_ioctl_gem_info(struct drm_device *dev, void *data, struct drm_file *file)
{
	struct drm_lima_gem_info *args = data;

	return lima_gem_mmap_offset(file, args->handle, &args->offset);
}

static int lima_ioctl_gem_va(struct drm_device *dev, void *data, struct drm_file *file)
{
	struct drm_lima_gem_va *args = data;

	switch (args->op) {
	case LIMA_VA_OP_MAP:
		return lima_gem_va_map(file, args->handle, args->flags, args->va);
	case LIMA_VA_OP_UNMAP:
		return lima_gem_va_unmap(file, args->handle, args->va);
	default:
		return -EINVAL;
	}
}

static int lima_ioctl_gem_submit(struct drm_device *dev, void *data, struct drm_file *file)
{
	struct drm_lima_gem_submit *args = data;
	struct drm_lima_gem_submit_bo *bos;
	int err = 0;
	void *frame;
	struct lima_device *ldev = to_lima_dev(dev);

	if (args->pipe >= ARRAY_SIZE(ldev->pipe) || args->nr_bos == 0)
		return -EINVAL;

	switch (ldev->gpu_type) {
	case GPU_MALI400:
	case GPU_MALI450:
		if (args->pipe == LIMA_PIPE_GP) {
			if (args->frame_size != sizeof(struct drm_lima_m400_gp_frame))
				return -EINVAL;
		}
		else {
			if (args->frame_size != sizeof(struct drm_lima_m400_pp_frame))
				return -EINVAL;
		}
		break;
	default:
		return -EINVAL;
	}

	bos = kmalloc(args->nr_bos * sizeof(*bos), GFP_KERNEL);
	if (!bos)
		return -ENOMEM;
	if (copy_from_user(bos, u64_to_user_ptr(args->bos), args->nr_bos * sizeof(*bos))) {
		err = -EFAULT;
		goto out0;
	}

	frame = kmalloc(args->frame_size, GFP_KERNEL);
	if (!frame) {
		err = -ENOMEM;
		goto out0;
	}
	if (copy_from_user(frame, u64_to_user_ptr(args->frame), args->frame_size)) {
		err = -EFAULT;
		goto out1;
	}

	switch (ldev->gpu_type) {
	case GPU_MALI400:
	case GPU_MALI450:
		if (args->pipe == LIMA_PIPE_PP) {
			struct drm_lima_m400_pp_frame *f = frame;
			if (f->num_pp > ldev->pp->num_core) {
				err = -EINVAL;
				goto out1;
			}
		}
		break;
	}

	err = lima_gem_submit(file, ldev->pipe[args->pipe], bos, args->nr_bos,
			      frame, &args->fence);

out1:
	if (err)
		kfree(frame);
out0:
	kfree(bos);
	return err;
}

static int lima_ioctl_wait_fence(struct drm_device *dev, void *data, struct drm_file *file)
{
	struct drm_lima_wait_fence *args = data;
	struct lima_device *ldev = to_lima_dev(dev);

	if (args->pipe >= ARRAY_SIZE(ldev->pipe))
		return -EINVAL;

	return lima_sched_pipe_wait_fence(ldev->pipe[args->pipe], args->fence, args->timeout_ns);
}

static int lima_ioctl_gem_wait(struct drm_device *dev, void *data, struct drm_file *file)
{
	struct drm_lima_gem_wait *args = data;

	if (!(args->op & (LIMA_GEM_WAIT_READ|LIMA_GEM_WAIT_WRITE)))
	    return -EINVAL;

	return lima_gem_wait(file, args->handle, args->op, args->timeout_ns);
}

static int lima_drm_driver_open(struct drm_device *dev, struct drm_file *file)
{
	int err;
	struct lima_drm_priv *priv;
	struct lima_device *ldev = to_lima_dev(dev);

	priv = kzalloc(sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->vm = lima_vm_create(ldev);
	if (!priv->vm) {
		err = -ENOMEM;
		goto err_out0;
	}

	file->driver_priv = priv;
	return 0;

err_out0:
	kfree(priv);
	return err;
}

static void lima_drm_driver_postclose(struct drm_device *dev, struct drm_file *file)
{
	struct lima_drm_priv *priv = file->driver_priv;

	lima_vm_put(priv->vm);
	kfree(priv);
}

static const struct drm_ioctl_desc lima_drm_driver_ioctls[] = {
	DRM_IOCTL_DEF_DRV(LIMA_INFO, lima_ioctl_info, DRM_AUTH|DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(LIMA_GEM_CREATE, lima_ioctl_gem_create, DRM_AUTH|DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(LIMA_GEM_INFO, lima_ioctl_gem_info, DRM_AUTH|DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(LIMA_GEM_VA, lima_ioctl_gem_va, DRM_AUTH|DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(LIMA_GEM_SUBMIT, lima_ioctl_gem_submit, DRM_AUTH|DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(LIMA_WAIT_FENCE, lima_ioctl_wait_fence, DRM_AUTH|DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(LIMA_GEM_WAIT, lima_ioctl_gem_wait, DRM_AUTH|DRM_RENDER_ALLOW),
};

extern const struct vm_operations_struct lima_gem_vm_ops;

static const struct file_operations lima_drm_driver_fops = {
	.owner              = THIS_MODULE,
	.open               = drm_open,
	.release            = drm_release,
	.unlocked_ioctl     = drm_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl       = drm_compat_ioctl,
#endif
	.mmap               = lima_gem_mmap,
};

static struct drm_driver lima_drm_driver = {
	.driver_features    = DRIVER_RENDER | DRIVER_GEM | DRIVER_PRIME,
	.open               = lima_drm_driver_open,
	.postclose          = lima_drm_driver_postclose,
	.ioctls             = lima_drm_driver_ioctls,
	.num_ioctls         = ARRAY_SIZE(lima_drm_driver_ioctls),
	.fops               = &lima_drm_driver_fops,
	.gem_free_object_unlocked = lima_gem_free_object,
	.gem_vm_ops         = &lima_gem_vm_ops,
	.name               = "lima",
	.desc               = "lima DRM",
	.date               = "20170325",
	.major              = 1,
	.minor              = 0,

	.prime_fd_to_handle = drm_gem_prime_fd_to_handle,
	.gem_prime_import   = drm_gem_prime_import,
	.gem_prime_import_sg_table = lima_gem_prime_import_sg_table,
	.prime_handle_to_fd = drm_gem_prime_handle_to_fd,
	.gem_prime_export   = drm_gem_prime_export,
	.gem_prime_get_sg_table = lima_gem_prime_get_sg_table,
};

static int lima_pdev_probe(struct platform_device *pdev)
{
	struct lima_device *ldev;
	struct drm_device *ddev;
	int err;

	ldev = devm_kzalloc(&pdev->dev, sizeof(*ldev), GFP_KERNEL);
	if (!ldev)
		return -ENOMEM;

	ldev->pdev = pdev;
	ldev->dev = &pdev->dev;
	ldev->gpu_type = (enum lima_gpu_type)of_device_get_match_data(&pdev->dev);

	platform_set_drvdata(pdev, ldev);

	/* Allocate and initialize the DRM device. */
	ddev = drm_dev_alloc(&lima_drm_driver, &pdev->dev);
	if (IS_ERR(ddev))
		return PTR_ERR(ddev);

	ddev->dev_private = ldev;
	ldev->ddev = ddev;

	err = lima_device_init(ldev);
	if (err) {
		dev_err(&pdev->dev, "Fatal error during GPU init\n");
		goto err_out0;
	}

	/*
	 * Register the DRM device with the core and the connectors with
	 * sysfs.
	 */
	err = drm_dev_register(ddev, 0);
	if (err < 0)
		goto err_out1;

	return 0;

err_out1:
	lima_device_fini(ldev);
err_out0:
	drm_dev_unref(ddev);
	return err;
}

static int lima_pdev_remove(struct platform_device *pdev)
{
	struct lima_device *ldev = platform_get_drvdata(pdev);
	struct drm_device *ddev = ldev->ddev;

	drm_dev_unregister(ddev);
	lima_device_fini(ldev);
	drm_dev_unref(ddev);
	return 0;
}

static const struct of_device_id dt_match[] = {
	{ .compatible = "arm,mali-400", .data = (void *)GPU_MALI400 },
	{ .compatible = "arm,mali-450", .data = (void *)GPU_MALI450 },
	{}
};
MODULE_DEVICE_TABLE(of, dt_match);

static struct platform_driver lima_platform_driver = {
	.probe      = lima_pdev_probe,
	.remove     = lima_pdev_remove,
	.driver     = {
		.name   = "lima",
		.of_match_table = dt_match,
	},
};

static int __init lima_init(void)
{
	int ret;

	ret = platform_driver_register(&lima_platform_driver);

	return ret;
}
module_init(lima_init);

static void __exit lima_exit(void)
{
	platform_driver_unregister(&lima_platform_driver);
}
module_exit(lima_exit);

MODULE_AUTHOR("Qiang Yu <yuq825@gmail.com>");
MODULE_DESCRIPTION("Lima DRM Driver");
MODULE_LICENSE("GPL v2");
