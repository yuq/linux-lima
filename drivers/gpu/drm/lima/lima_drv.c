#include <linux/module.h>
#include <linux/of_platform.h>
#include <drm/lima_drm.h>

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
	default:
		return -ENODEV;
	}
	info->num_pp = ldev->num_pp;
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

static int lima_drm_driver_load(struct drm_device *dev, unsigned long flags)
{
	struct lima_device *ldev;
	int err;

	ldev = kzalloc(sizeof(*ldev), GFP_KERNEL);
	if (!ldev)
		return -ENOMEM;

	dev->dev_private = (void *)ldev;

	err = lima_device_init(ldev, dev);
	if (err) {
		dev_err(&dev->platformdev->dev, "Fatal error during GPU init\n");
		goto err0;
	}

	platform_set_drvdata(dev->platformdev, dev);

	return 0;

err0:
	kfree(ldev);
	return err;
}

static int lima_drm_driver_unload(struct drm_device *dev)
{
	struct lima_device *ldev = to_lima_dev(dev);

	lima_device_fini(ldev);
	kfree(ldev);
	dev->dev_private = NULL;
	return 0;
}

static const struct drm_ioctl_desc lima_drm_driver_ioctls[] = {
	DRM_IOCTL_DEF_DRV(LIMA_INFO, lima_ioctl_info, DRM_AUTH|DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(LIMA_GEM_CREATE, lima_ioctl_gem_create, DRM_AUTH|DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(LIMA_GEM_INFO, lima_ioctl_gem_info, DRM_AUTH|DRM_RENDER_ALLOW),
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
	.driver_features    = DRIVER_RENDER | DRIVER_GEM,
	.load		    = lima_drm_driver_load,
	.unload             = lima_drm_driver_unload,
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
};

static int lima_pdev_probe(struct platform_device *pdev)
{
	return drm_platform_init(&lima_drm_driver, pdev);
}

static int lima_pdev_remove(struct platform_device *pdev)
{
	drm_put_dev(platform_get_drvdata(pdev));
	return 0;
}

static const struct of_device_id dt_match[] = {
	{ .compatible = "arm,mali400" },
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
