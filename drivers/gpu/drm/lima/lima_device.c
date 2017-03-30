#include <linux/reset.h>
#include <linux/clk.h>
#include <linux/dma-mapping.h>

#include "lima.h"

static int lima_clk_init(struct lima_device *dev)
{
	int err;
	unsigned long bus_rate, gpu_rate;

	dev->clk_bus = devm_clk_get(dev->dev, "bus");
	if (IS_ERR(dev->clk_bus)) {
		dev_err(dev->dev, "get bus clk fail %ld\n", PTR_ERR(dev->clk_bus));
		return PTR_ERR(dev->clk_bus);
	}

	dev->clk_gpu = devm_clk_get(dev->dev, "gpu");
	if (IS_ERR(dev->clk_gpu)) {
		dev_err(dev->dev, "get gpu clk fail %ld\n", PTR_ERR(dev->clk_gpu));
		return PTR_ERR(dev->clk_gpu);
	}

	bus_rate = clk_get_rate(dev->clk_bus);
	dev_info(dev->dev, "bus rate = %lu\n", bus_rate);

	gpu_rate = clk_get_rate(dev->clk_gpu);
	dev_info(dev->dev, "mod rate = %lu", gpu_rate);

	if ((err = clk_prepare_enable(dev->clk_bus)))
		return err;
	if ((err = clk_prepare_enable(dev->clk_gpu)))
		goto error_out0;

	dev->reset = devm_reset_control_get(dev->dev, "ahb");
	if (IS_ERR(dev->reset)) {
		err = PTR_ERR(dev->reset);
		goto error_out1;
	}

	if ((err = reset_control_deassert(dev->reset)))
		goto error_out1;

	return 0;

error_out1:
	clk_disable_unprepare(dev->clk_gpu);
error_out0:
	clk_disable_unprepare(dev->clk_bus);
	return err;
}

static void lima_clk_fini(struct lima_device *dev)
{
	reset_control_assert(dev->reset);
	clk_disable_unprepare(dev->clk_gpu);
	clk_disable_unprepare(dev->clk_bus);
}

static int lima_init_ip(struct lima_device *dev, const char *name,
			struct lima_ip *ip)
{
	struct resource *res;

	if (ip->irq == 0) {
		ip->irq = platform_get_irq_byname(dev->pdev, name);
		if (ip->irq < 0) {
			dev_err(dev->dev, "fail to get irq %s\n", name);
			return ip->irq;
		}
	}

	res = platform_get_resource_byname(dev->pdev, IORESOURCE_MEM, name);
	if (!res) {
		dev_err(dev->dev, "fail to get iomem %s\n", name);
		return -EINVAL;
	}

	ip->iomem = devm_ioremap_resource(dev->dev, res);
	if (IS_ERR(ip->iomem)) {
		dev_err(dev->dev, "fail to ioremap iomem %s\n", name);
		return PTR_ERR(ip->iomem);
	}

	ip->dev = dev;
	strncpy(ip->name, name, LIMA_IP_MAX_NAME_LEN);
	ip->name[LIMA_IP_MAX_NAME_LEN - 1] = '\0';

	return 0;
}

int lima_device_init(struct lima_device *ldev, struct drm_device *dev)
{
	int err, i;
	struct device_node *np;
	u32 num_pp;
	struct lima_mmu *mmu;

	ldev->pdev = dev->platformdev;
	ldev->dev = &dev->platformdev->dev;
	ldev->ddev = dev;

	dma_set_coherent_mask(ldev->dev, DMA_BIT_MASK(32));

	ldev->gpu_type = GPU_MALI400;

	np = ldev->dev->of_node;
	err = of_property_read_u32(np, "num-pp", &num_pp);
	if (err) {
		dev_err(ldev->dev, "no num-pp property defined\n");
		return err;
	}
	if (num_pp > LIMA_MAX_PP) {
		dev_err(ldev->dev, "too many pp %u\n", num_pp);
		return -EINVAL;
	}

	err = lima_clk_init(ldev);
	if (err) {
		dev_err(ldev->dev, "clk init fail %d\n", err);
		return err;
	}

	ldev->empty_mmu_pda = dma_alloc_wc(ldev->dev, PAGE_SIZE,
					   &ldev->empty_mmu_pda_dma, GFP_KERNEL);
	if (!ldev->empty_mmu_pda) {
		err = -ENOMEM;
		goto err_out;
	}
	memset(ldev->empty_mmu_pda, 0, PAGE_SIZE);

	ldev->pmu = kzalloc(sizeof(*ldev->pmu), GFP_KERNEL);
	if (!ldev->pmu) {
		err = -ENOMEM;
		goto err_out;
	}
	if ((err = lima_init_ip(ldev, "pmu", &ldev->pmu->ip)) ||
	    (err = lima_pmu_init(ldev->pmu))) {
		kfree(ldev->pmu);
		ldev->pmu = NULL;
		goto err_out;
	}

	ldev->l2_cache = kzalloc(sizeof(*ldev->l2_cache), GFP_KERNEL);
	if (!ldev->l2_cache) {
		err = -ENOMEM;
		goto err_out;
	}
	ldev->l2_cache->ip.irq = -1;
	if ((err = lima_init_ip(ldev, "l2-cache", &ldev->l2_cache->ip)) ||
	    (err = lima_l2_cache_init(ldev->l2_cache))) {
		kfree(ldev->l2_cache);
		ldev->l2_cache = NULL;
		goto err_out;
	}

	mmu = kzalloc(sizeof(*ldev->mmu[0]), GFP_KERNEL);
	if (!mmu) {
		err = -ENOMEM;
		goto err_out;
	}
	if ((err = lima_init_ip(ldev, "gp-mmu", &mmu->ip)) ||
	    (err = lima_mmu_init(mmu))) {
		kfree(mmu);
		goto err_out;
	}
	ldev->mmu[ldev->num_mmu++] = mmu;

	ldev->gp = kzalloc(sizeof(*ldev->gp), GFP_KERNEL);
	if (!ldev->gp) {
		err = -ENOMEM;
		goto err_out;
	}
	ldev->gp->mmu = mmu;
	if ((err = lima_init_ip(ldev, "gp", &ldev->gp->ip)) ||
	    (err = lima_gp_init(ldev->gp))) {
		kfree(ldev->gp);
		ldev->gp = NULL;
		goto err_out;
	}

	for (i = 0; i < num_pp; i++) {
		struct lima_pp *pp;
		char *pp_name = "pp0", *pp_mmu_name = "pp0-mmu";

		pp_name[2] += i; pp_mmu_name[2] += i;

		mmu = kzalloc(sizeof(*mmu), GFP_KERNEL);
		if (!mmu) {
			err = -ENOMEM;
			goto err_out;
		}
		if ((err = lima_init_ip(ldev, pp_mmu_name, &mmu->ip)) ||
		    (err = lima_mmu_init(mmu))) {
			kfree(mmu);
			goto err_out;
		}
		ldev->mmu[ldev->num_mmu++] = mmu;

		pp = kzalloc(sizeof(*pp), GFP_KERNEL);
		if (!pp) {
			err = -ENOMEM;
			goto err_out;
		}
		pp->mmu = mmu;
		if ((err = lima_init_ip(ldev, pp_name, &pp->ip)) ||
		    (err = lima_pp_init(pp))) {
			kfree(pp);
			goto err_out;
		}
		ldev->pp[ldev->num_pp++] = pp;
	}

	return 0;

err_out:
	lima_device_fini(ldev);
	return err;
}

void lima_device_fini(struct lima_device *ldev)
{
	int i;

	for (i = 0; i < ldev->num_pp; i++) {
		lima_pp_fini(ldev->pp[i]);
		kfree(ldev->pp[i]);
	}

	if (ldev->gp) {
		lima_gp_fini(ldev->gp);
		kfree(ldev->gp);
	}

	for (i = 0; i < ldev->num_mmu; i++) {
		lima_mmu_fini(ldev->mmu[i]);
		kfree(ldev->mmu[i]);
	}

	if (ldev->l2_cache) {
		lima_l2_cache_fini(ldev->l2_cache);
		kfree(ldev->l2_cache);
	}

	if (ldev->pmu) {
		lima_pmu_fini(ldev->pmu);
		kfree(ldev->pmu);
	}

	if (ldev->empty_mmu_pda) {
		dma_free_wc(ldev->dev, PAGE_SIZE, ldev->empty_mmu_pda,
			    ldev->empty_mmu_pda_dma);
	}

	lima_clk_fini(ldev);
}
