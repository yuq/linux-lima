/*
 * Copyright (C) 2018 Lima Project
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */

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
