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
#include "lima_bcast.h"

#define LIMA_BCAST_BROADCAST_MASK    0x0
#define LIMA_BCAST_INTERRUPT_MASK    0x4

#define bcast_write(reg, data) writel(data, ip->iomem + LIMA_BCAST_##reg)
#define bcast_read(reg) readl(ip->iomem + LIMA_BCAST_##reg)

void lima_bcast_enable(struct lima_device *dev)
{
	struct lima_sched_pipe *pipe = dev->pipe + lima_pipe_pp;
	struct lima_ip *ip = dev->ip + lima_ip_bcast;
	int i, mask = 0;

	for (i = 0; i < pipe->num_processor; i++) {
		struct lima_ip *pp = pipe->processor[i];
		mask |= 1 << (pp->id - lima_ip_pp0);
	}

	bcast_write(BROADCAST_MASK, (mask << 16) | mask);
	bcast_write(INTERRUPT_MASK, mask);
}

void lima_bcast_disable(struct lima_device *dev)
{
	struct lima_ip *ip = dev->ip + lima_ip_bcast;

	bcast_write(BROADCAST_MASK, 0);
	bcast_write(INTERRUPT_MASK, 0);
}

int lima_bcast_init(struct lima_ip *ip)
{
	return 0;
}

void lima_bcast_fini(struct lima_ip *ip)
{
	
}

