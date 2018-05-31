// SPDX-License-Identifier: GPL-2.0 OR MIT
/* Copyright 2017-2018 Qiang Yu <yuq825@gmail.com> */

#include <linux/of.h>
#include <linux/io.h>
#include <linux/device.h>

#include "lima_device.h"
#include "lima_pmu.h"
#include "lima_regs.h"

#define pmu_write(reg, data) writel(data, ip->iomem + LIMA_PMU_##reg)
#define pmu_read(reg) readl(ip->iomem + LIMA_PMU_##reg)

static int lima_pmu_wait_cmd(struct lima_ip *ip)
{
	struct lima_device *dev = ip->dev;
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

int lima_pmu_init(struct lima_ip *ip)
{
	int err;
	u32 stat;
	struct lima_device *dev = ip->dev;
	struct device_node *np = dev->dev->of_node;

	/* If this value is too low, when in high GPU clk freq,
	 * GPU will be in unstable state. */
	if (of_property_read_u32(np, "switch-delay", &ip->data.switch_delay))
		ip->data.switch_delay = 0xff;

	pmu_write(INT_MASK, 0);
	pmu_write(SW_DELAY, ip->data.switch_delay);

	/* status reg 1=off 0=on */
	stat = pmu_read(STATUS);

	/* power up all ip */
	if (stat) {
		pmu_write(POWER_UP, stat);
		err = lima_pmu_wait_cmd(ip);
		if (err)
			return err;
	}
	return 0;
}

void lima_pmu_fini(struct lima_ip *ip)
{

}
