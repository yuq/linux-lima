#include <linux/reset.h>
#include <linux/clk.h>
#include <linux/dma-mapping.h>

#include "lima.h"

#define LIMA_GP_BASE           0x0000
#define LIMA_L2_BASE           0x1000
#define LIMA_PMU_BASE          0x2000
#define LIMA_GPMMU_BASE        0x3000
#define LIMA_PPMMU_BASE(i)     ((i < 4) ? 0x4000 + 0x1000 * (i) : \
					  0x1C000 + 0x1000 * (i - 4))
#define LIMA_PP_BASE(i)        ((i < 4) ? 0x8000 + 0x2000 * (i) : \
					  0x28000 + 0x2000 * (i - 4))

/* Separate L2-caches per group on Mali450 */
#define LIMA450_GPL2_BASE      0x10000
#define LIMA450_PP03L2_BASE    0x01000
#define LIMA450_PP47L2_BASE    0x11000

#define LIMA_BCAST_BASE        0x13000
#define LIMA_PPBCAST_BASE      0x16000
#define LIMA_PPBCASTMMU_BASE   0x15000
#define LIMA_DLBU_BASE         0x14000
#define LIMA_DMA_BASE          0x12000

static int lima_clk_init(struct lima_device *dev)
{
	int err;
	unsigned long bus_rate, gpu_rate;

	dev->clk_bus = devm_clk_get(dev->dev, "bus");
	if (IS_ERR(dev->clk_bus)) {
		dev_err(dev->dev, "get bus clk failed %ld\n", PTR_ERR(dev->clk_bus));
		return PTR_ERR(dev->clk_bus);
	}

	dev->clk_gpu = devm_clk_get(dev->dev, "core");
	if (IS_ERR(dev->clk_gpu)) {
		dev_err(dev->dev, "get core clk failed %ld\n", PTR_ERR(dev->clk_gpu));
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

	dev->reset = devm_reset_control_get_optional(dev->dev, NULL);
	if (IS_ERR(dev->reset)) {
		err = PTR_ERR(dev->reset);
		goto error_out1;
	} else if (dev->reset != NULL) {
		if ((err = reset_control_deassert(dev->reset)))
			goto error_out1;
	}

	return 0;

error_out1:
	clk_disable_unprepare(dev->clk_gpu);
error_out0:
	clk_disable_unprepare(dev->clk_bus);
	return err;
}

static void lima_clk_fini(struct lima_device *dev)
{
	if (dev->reset != NULL)
		reset_control_assert(dev->reset);
	clk_disable_unprepare(dev->clk_gpu);
	clk_disable_unprepare(dev->clk_bus);
}

static int lima_init_ip(struct lima_device *dev, const char *name,
			struct lima_ip *ip, u32 offset)
{
	ip->iomem = dev->iomem + offset;
	ip->dev = dev;

	strncpy(ip->name, name, LIMA_IP_MAX_NAME_LEN);
	ip->name[LIMA_IP_MAX_NAME_LEN - 1] = '\0';

	if (ip->irq == 0) {
		ip->irq = platform_get_irq_byname(dev->pdev, name);
		if (ip->irq < 0) {
			dev_err(dev->dev, "fail to get irq %s\n", name);
			return ip->irq;
		}
	}

	return 0;
}

static int lima_gp_group_init(struct lima_device *dev)
{
	int err;
	struct lima_gp *gp;

	gp = kzalloc(sizeof(*gp), GFP_KERNEL);
	if (!gp)
		return -ENOMEM;

	/* Init GP-group L2 cache on Mali450 */
	if (dev->gpu_type == GPU_MALI450) {
		gp->l2_cache = kzalloc(sizeof(*gp->l2_cache), GFP_KERNEL);
		if (!gp->l2_cache) {
			err = -ENOMEM;
			goto err_out0;
		}
		gp->l2_cache->ip.irq = -1;
		if ((err = lima_init_ip(dev, "gp-l2-cache", &gp->l2_cache->ip, LIMA450_GPL2_BASE)) ||
		    (err = lima_l2_cache_init(gp->l2_cache))) {
			goto err_out1;
		}
	}

	if ((err = lima_init_ip(dev, "gpmmu", &gp->mmu.ip, LIMA_GPMMU_BASE)) ||
	    (err = lima_mmu_init(&gp->mmu)))
		goto err_out1;

	if ((err = lima_init_ip(dev, "gp", &gp->ip, LIMA_GP_BASE)) ||
	    (err = lima_gp_init(gp)))
		goto err_out2;

	if ((err = lima_sched_pipe_init(&gp->pipe, gp->ip.name)))
		goto err_out3;

	dev->pipe[LIMA_PIPE_GP] = &gp->pipe;
	gp->mmu.pipe = &gp->pipe;
	dev->gp = gp;
	return 0;

err_out3:
	lima_gp_fini(gp);
err_out2:
	lima_mmu_fini(&gp->mmu);
err_out1:
	if (gp->l2_cache)
		lima_l2_cache_fini(gp->l2_cache);
err_out0:
	if (gp->l2_cache)
		kfree(gp->l2_cache);
	kfree(gp);
	return err;
}

static int lima_pp_group_init(struct lima_device *dev)
{
	int err, i;
	struct lima_pp *pp;
	char pp_name[] = "pp0", pp_mmu_name[] = "ppmmu0";

	pp = kzalloc(sizeof(*pp), GFP_KERNEL);
	if (!pp)
		return -ENOMEM;
	dev->pp = pp;

	/* Init PP-group L2 cache on Mali450 */
	if (dev->gpu_type == GPU_MALI450) {
		pp->l2_cache = kzalloc(sizeof(*pp->l2_cache), GFP_KERNEL);
		if (!pp->l2_cache)
			return -ENOMEM;
		pp->l2_cache->ip.irq = -1;
		if ((err = lima_init_ip(dev, "pp-l2-cache", &pp->l2_cache->ip, LIMA450_PP03L2_BASE)) ||
		    (err = lima_l2_cache_init(pp->l2_cache)))
			return err;
	}

	for (i = 0; i < dev->num_pp; i++) {
		struct lima_pp_core *core = pp->core + pp->num_core;

		pp_name[2] = '0' + i; pp_mmu_name[5] = '0' + i;

		if ((err = lima_init_ip(dev, pp_mmu_name, &core->mmu.ip, LIMA_PPMMU_BASE(i))) ||
		    (err = lima_mmu_init(&core->mmu))) {
			memset(core, 0, sizeof(*core));
			continue;
		}

		if ((err = lima_init_ip(dev, pp_name, &core->ip, LIMA_PP_BASE(i))) ||
		    (err = lima_pp_core_init(core))) {
			lima_mmu_fini(&core->mmu);
			memset(core, 0, sizeof(*core));
			continue;
		}

		pp->num_core++;
	}

	if (pp->num_core != dev->num_pp)
		dev_warn(dev->dev, "bringup pp %d/%d\n", pp->num_core, dev->num_pp);

	if (pp->num_core == 0)
		return -ENODEV;

	if ((err = lima_sched_pipe_init(&pp->pipe, "pp")))
		return err;

	dev->pipe[LIMA_PIPE_PP] = &pp->pipe;
	for (i = 0; i < pp->num_core; i++)
		pp->core[i].mmu.pipe = &pp->pipe;
	lima_pp_init(pp);
	return 0;
}

int lima_device_init(struct lima_device *ldev)
{
	int err, i;
	struct device_node *np;
	struct resource *res;

	dma_set_coherent_mask(ldev->dev, DMA_BIT_MASK(32));

	np = ldev->dev->of_node;

	err = lima_clk_init(ldev);
	if (err) {
		dev_err(ldev->dev, "clk init fail %d\n", err);
		return err;
	}

	ldev->empty_vm = lima_vm_create(ldev);
	if (!ldev->empty_vm) {
		err = -ENOMEM;
		goto err_out;
	}

	res = platform_get_resource(ldev->pdev, IORESOURCE_MEM, 0);
	ldev->iomem = devm_ioremap_resource(ldev->dev, res);
	if (IS_ERR(ldev->iomem)) {
		dev_err(ldev->dev, "fail to ioremap iomem\n");
	        err = PTR_ERR(ldev->iomem);
		goto err_out;
	}

	/* Get the number of PPs */
	for (i = 0; i < LIMA_MAX_PP; i++) {
		char pp_name[] = "pp0";
		pp_name[2] = '0' + i;
		if (platform_get_irq_byname(ldev->pdev, pp_name) < 0)
			break;
	}
	dev_info(ldev->dev, "found %d PPs\n", i);
	ldev->num_pp = i;

	ldev->pmu = kzalloc(sizeof(*ldev->pmu), GFP_KERNEL);
	if (!ldev->pmu) {
		err = -ENOMEM;
		goto err_out;
	}

	/* pmu is optional and not always present */
	if ((err = lima_init_ip(ldev, "pmu", &ldev->pmu->ip, LIMA_PMU_BASE)) ||
	    (err = lima_pmu_init(ldev->pmu))) {
		dev_info(ldev->dev, "no PMU present\n");
		kfree(ldev->pmu);
		ldev->pmu = NULL;
	}

	if (ldev->gpu_type != GPU_MALI450) {
		ldev->l2_cache = kzalloc(sizeof(*ldev->l2_cache), GFP_KERNEL);
		if (!ldev->l2_cache) {
			err = -ENOMEM;
			goto err_out;
		}
		ldev->l2_cache->ip.irq = -1;
		if ((err = lima_init_ip(ldev, "l2-cache", &ldev->l2_cache->ip, LIMA_L2_BASE)) ||
		    (err = lima_l2_cache_init(ldev->l2_cache))) {
			kfree(ldev->l2_cache);
			ldev->l2_cache = NULL;
			goto err_out;
		}
	}

	if ((err = lima_gp_group_init(ldev)))
		goto err_out;

	if ((err = lima_pp_group_init(ldev)))
		goto err_out;

	return 0;

err_out:
	lima_device_fini(ldev);
	return err;
}

void lima_device_fini(struct lima_device *ldev)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(ldev->pipe); i++) {
		if (ldev->pipe[i])
			lima_sched_pipe_fini(ldev->pipe[i]);
	}

	if (ldev->pp) {
		for (i = 0; i < ldev->pp->num_core; i++) {
			lima_pp_core_fini(ldev->pp->core + i);
			lima_mmu_fini(&ldev->pp->core[i].mmu);
		}

		if (ldev->pp->l2_cache) {
			lima_l2_cache_fini(ldev->pp->l2_cache);
			kfree(ldev->pp->l2_cache);
		}

		kfree(ldev->pp);
	}

	if (ldev->gp) {
		lima_gp_fini(ldev->gp);
		lima_mmu_fini(&ldev->gp->mmu);

		if (ldev->gp->l2_cache) {
			lima_l2_cache_fini(ldev->gp->l2_cache);
			kfree(ldev->gp->l2_cache);
		}

		kfree(ldev->gp);
	}

	if (ldev->l2_cache) {
		lima_l2_cache_fini(ldev->l2_cache);
		kfree(ldev->l2_cache);
	}

	if (ldev->pmu) {
		lima_pmu_fini(ldev->pmu);
		kfree(ldev->pmu);
	}

	lima_vm_put(ldev->empty_vm);

	lima_clk_fini(ldev);
}
