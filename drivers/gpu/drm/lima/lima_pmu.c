#include "lima.h"

#define LIMA_PMU_POWER_UP                  0x00
#define LIMA_PMU_POWER_DOWN                0x04
#define   LIMA_PMU_POWER_GP0_MASK          (1 << 0)
#define   LIMA_PMU_POWER_L2_MASK           (1 << 1)
#define   LIMA_PMU_POWER_PP_MASK(i)        (1 << (2 + i))

/*
 * On Mali450 each block automatically starts up its corresponding L2
 * and the PPs are not fully independent controllable.
 * Instead PP0, PP1-3 and PP4-7 can be turned on or off.
 */
#define   LIMA450_PMU_POWER_PP0_MASK       BIT(1)
#define   LIMA450_PMU_POWER_PP13_MASK      BIT(2)
#define   LIMA450_PMU_POWER_PP47_MASK      BIT(3)

#define LIMA_PMU_STATUS                    0x08
#define LIMA_PMU_INT_MASK                  0x0C
#define LIMA_PMU_INT_RAWSTAT               0x10
#define LIMA_PMU_INT_CLEAR                 0x18
#define   LIMA_PMU_INT_CMD_MASK            (1 << 0)
#define LIMA_PMU_SW_DELAY                  0x1C

#define pmu_write(reg, data) writel(data, pmu->ip.iomem + LIMA_PMU_##reg)
#define pmu_read(reg) readl(pmu->ip.iomem + LIMA_PMU_##reg)

static int lima_pmu_wait_cmd(struct lima_pmu *pmu)
{
	struct lima_device *dev = pmu->ip.dev;
	u32 stat, timeout;

	for (timeout = 1000000; timeout > 0; timeout--) {
		stat = pmu_read(INT_RAWSTAT);
		if (stat & LIMA_PMU_INT_CMD_MASK)
			break;
	}

	if (!timeout) {
		dev_err(dev->dev, "timeout wait pmd cmd\n");
		return -ETIMEDOUT;
	}

	pmu_write(INT_CLEAR, LIMA_PMU_INT_CMD_MASK);
	return 0;
}

int lima_pmu_init(struct lima_pmu *pmu)
{
	int i, err;
	u32 stat, mask;
	struct lima_device *dev = pmu->ip.dev;

	pmu_write(INT_MASK, 0);
	pmu_write(SW_DELAY, 0xff);

	/* status reg 1=off 0=on */
	stat = pmu_read(STATUS);

	/* power up all ip */
	switch (dev->gpu_type) {
	case GPU_MALI400:
		mask = LIMA_PMU_POWER_GP0_MASK | LIMA_PMU_POWER_L2_MASK;
		for (i = 0; i < dev->num_pp; i++)
			mask |= LIMA_PMU_POWER_PP_MASK(i);
		break;
	case GPU_MALI450:
		mask = LIMA_PMU_POWER_GP0_MASK | LIMA450_PMU_POWER_PP0_MASK;
		if (dev->num_pp > 1)
			mask |= LIMA450_PMU_POWER_PP13_MASK;
		if (dev->num_pp > 4)
			mask |= LIMA450_PMU_POWER_PP47_MASK;
		break;
	default:
		return -ENODEV;
	}

	if (stat & mask) {
		pmu_write(POWER_UP, stat & mask);
		err = lima_pmu_wait_cmd(pmu);
		if (err)
			return err;
	}
	return 0;
}

void lima_pmu_fini(struct lima_pmu *pmu)
{

}
