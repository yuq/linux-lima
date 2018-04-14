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

#include <linux/io.h>
#include <linux/device.h>

#include "lima_device.h"
#include "lima_dlbu.h"

#define LIMA_DLBU_MASTER_TLLIST_PHYS_ADDR  0x0000
#define	LIMA_DLBU_MASTER_TLLIST_VADDR      0x0004
#define	LIMA_DLBU_TLLIST_VBASEADDR         0x0008
#define	LIMA_DLBU_FB_DIM                   0x000C
#define	LIMA_DLBU_TLLIST_CONF              0x0010
#define	LIMA_DLBU_START_TILE_POS           0x0014
#define	LIMA_DLBU_PP_ENABLE_MASK           0x0018

#define dlbu_write(reg, data) writel(data, ip->iomem + LIMA_DLBU_##reg)
#define dlbu_read(reg) readl(ip->iomem + LIMA_DLBU_##reg)

int lima_dlbu_init(struct lima_ip *ip)
{
	struct lima_device *dev = ip->dev;

	dev_info(dev->dev, "dlbu %x %x\n",
		 dlbu_read(MASTER_TLLIST_PHYS_ADDR),
		 dlbu_read(PP_ENABLE_MASK));

	return 0;
}

void lima_dlbu_fini(struct lima_ip *ip)
{
	
}
