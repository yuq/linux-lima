// SPDX-License-Identifier: GPL-2.0 OR MIT
/* Copyright 2017-2018 Qiang Yu <yuq825@gmail.com> */

#include <linux/io.h>
#include <linux/device.h>

#include "lima_device.h"
#include "lima_l2_cache.h"
#include "lima_regs.h"

#define l2_cache_write(reg, data) writel(data, ip->iomem + LIMA_L2_CACHE_##reg)
#define l2_cache_read(reg) readl(ip->iomem + LIMA_L2_CACHE_##reg)

static int lima_l2_cache_wait_idle(struct lima_ip *ip)
{
	int timeout;
	struct lima_device *dev = ip->dev;

	for (timeout = 100000; timeout > 0; timeout--) {
	    if (!(l2_cache_read(STATUS) & LIMA_L2_CACHE_STATUS_COMMAND_BUSY))
		break;
	}
	if (!timeout) {
	    dev_err(dev->dev, "l2 cache wait command timeout\n");
	    return -ETIMEDOUT;
	}
	return 0;
}

int lima_l2_cache_flush(struct lima_ip *ip)
{
	int ret;

	spin_lock(&ip->data.lock);
	l2_cache_write(COMMAND, LIMA_L2_CACHE_COMMAND_CLEAR_ALL);
	ret = lima_l2_cache_wait_idle(ip);
	spin_unlock(&ip->data.lock);
	return ret;
}

int lima_l2_cache_init(struct lima_ip *ip)
{
	int i, err;
	u32 size;
	struct lima_device *dev = ip->dev;

	/* l2_cache2 only exists when one of PP4-7 present */
	if (ip->id == lima_ip_l2_cache2) {
		for (i = lima_ip_pp4; i <= lima_ip_pp7; i++) {
			if (dev->ip[i].present)
				break;
		}
		if (i > lima_ip_pp7)
			return -ENODEV;
	}

	spin_lock_init(&ip->data.lock);

	size = l2_cache_read(SIZE);
	dev_info(dev->dev, "l2 cache %uK, %u-way, %ubyte cache line, %ubit external bus\n",
		 1 << (((size >> 16) & 0xff) - 10),
		 1 << ((size >> 8) & 0xff),
		 1 << (size & 0xff),
		 1 << ((size >> 24) & 0xff));

	err = lima_l2_cache_flush(ip);
	if (err)
		return err;

	l2_cache_write(ENABLE, LIMA_L2_CACHE_ENABLE_ACCESS | LIMA_L2_CACHE_ENABLE_READ_ALLOCATE);
	l2_cache_write(MAX_READS, 0x1c);

	return 0;
}

void lima_l2_cache_fini(struct lima_ip *ip)
{

}
