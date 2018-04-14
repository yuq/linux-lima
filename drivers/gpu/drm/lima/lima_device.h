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
#ifndef __LIMA_DEVICE_H__
#define __LIMA_DEVICE_H__

#include "lima_sched.h"

enum lima_gpu_id {
        lima_gpu_mali400 = 0,
	lima_gpu_mali450,
	lima_gpu_num,
};

enum lima_ip_id {
	lima_ip_pmu,
	lima_ip_gpmmu,
	lima_ip_ppmmu0,
	lima_ip_ppmmu1,
	lima_ip_ppmmu2,
	lima_ip_ppmmu3,
	lima_ip_ppmmu4,
	lima_ip_ppmmu5,
	lima_ip_ppmmu6,
	lima_ip_ppmmu7,
	lima_ip_gp,
	lima_ip_pp0,
	lima_ip_pp1,
	lima_ip_pp2,
	lima_ip_pp3,
	lima_ip_pp4,
	lima_ip_pp5,
	lima_ip_pp6,
	lima_ip_pp7,
	lima_ip_l2_cache0,
	lima_ip_l2_cache1,
	lima_ip_l2_cache2,
	lima_ip_dlbu,
	lima_ip_bcast,
	lima_ip_pp_bcast,
	lima_ip_ppmmu_bcast,
	lima_ip_num,
};

struct lima_device;

struct lima_ip {
	struct lima_device *dev;
	enum lima_ip_id id;
	bool present;

	void __iomem *iomem;
	int irq;

	union {
		/* pmu */
		unsigned switch_delay;
		/* gp/pp */
		bool async_reset;
	} data;
};

enum lima_pipe_id {
	lima_pipe_gp,
	lima_pipe_pp,
	lima_pipe_num,
};

struct lima_device {
	struct device *dev;
	struct drm_device *ddev;
	struct platform_device *pdev;

	enum lima_gpu_id id;
	int num_pp;

	void __iomem *iomem;
	struct clk *clk_bus;
	struct clk *clk_gpu;
	struct reset_control *reset;
	struct regulator *regulator;

	struct lima_ip ip[lima_ip_num];
	struct lima_sched_pipe pipe[lima_pipe_num];

	struct lima_vm *empty_vm;
	uint64_t va_start;
	uint64_t va_end;
};

int lima_device_init(struct lima_device *ldev);
void lima_device_fini(struct lima_device *ldev);

const char *lima_ip_name(struct lima_ip *ip);

#endif
