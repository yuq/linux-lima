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

#include "lima.h"
#include "lima_bcast.h"

#define LIMA_BCAST_BROADCAST_MASK    0x0
#define LIMA_BCAST_INTERRUPT_MASK    0x4

#define bcast_write(reg, data) writel(data, bcast->ip.iomem + LIMA_BCAST_##reg)
#define bcast_read(reg) readl(bcast->ip.iomem + LIMA_BCAST_##reg)

int lima_bcast_init(struct lima_bcast *bcast)
{
	struct lima_device *dev = bcast->ip.dev;

	dev_info(dev->dev, "bcast %x %x\n",
		 bcast_read(BROADCAST_MASK),
		 bcast_read(INTERRUPT_MASK));

	return 0;
}

void lima_bcast_fini(struct lima_bcast *bcast)
{
	
}

