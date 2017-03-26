#include "lima.h"

int lima_device_init(struct lima_device *ldev, struct drm_device *dev)
{
	int err;
	struct device_node *np;

	ldev->pdev = dev->platformdev;
	ldev->dev = &dev->platformdev->dev;
	ldev->ddev = dev;

	ldev->gpu_type = GPU_MALI400;

	np = ldev->dev->of_node;
	err = of_property_read_u32(np, "num-pp", &ldev->num_pp);
	if (err) {
		dev_err(ldev->dev, "no num-pp property defined\n");
		return err;
	}

	return 0;
}

void lima_device_fini(struct lima_device *ldev)
{

}
