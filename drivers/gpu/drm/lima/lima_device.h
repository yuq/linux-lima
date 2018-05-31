/* SPDX-License-Identifier: GPL-2.0 OR MIT */
/* Copyright 2018 Qiang Yu <yuq825@gmail.com> */

#ifndef __LIMA_DEVICE_H__
#define __LIMA_DEVICE_H__

#include <drm/drm_device.h>

#include "lima_sched.h"
#include "lima_ttm.h"

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
		/* l2 cache */
		spinlock_t lock;
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

	struct lima_mman mman;

	struct lima_vm *empty_vm;
	uint64_t va_start;
	uint64_t va_end;

	u32 *dlbu_cpu;
	dma_addr_t dlbu_dma;
};

static inline struct lima_device *
to_lima_dev(struct drm_device *dev)
{
	return dev->dev_private;
}

static inline struct lima_device *
ttm_to_lima_dev(struct ttm_bo_device *dev)
{
	return container_of(dev, struct lima_device, mman.bdev);
}

int lima_device_init(struct lima_device *ldev);
void lima_device_fini(struct lima_device *ldev);

const char *lima_ip_name(struct lima_ip *ip);

#endif
