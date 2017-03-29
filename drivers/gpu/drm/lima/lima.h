/*
 * Copyright (C) 2017 Lima Project
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
#ifndef __LIMA_H__

#include <drm/drmP.h>

enum lima_gpu_type {
	GPU_MALI400 = 0,
};

struct lima_device;

#define LIMA_IP_MAX_NAME_LEN 32

struct lima_ip {
	struct lima_device *dev;
	char name[LIMA_IP_MAX_NAME_LEN];
	void __iomem *iomem;
	int irq;
};

struct lima_pmu {
	struct lima_ip ip;
};

struct lima_l2_cache {
	struct lima_ip ip;
};

struct lima_mmu {
	struct lima_ip ip;
};

struct lima_gp {
	struct lima_ip ip;
	struct lima_mmu *mmu;
};

struct lima_pp {
	struct lima_ip ip;
	struct lima_mmu *mmu;
};

#define LIMA_MAX_PP 4

struct lima_device {
	struct device *dev;
	struct drm_device *ddev;
	struct platform_device *pdev;

	enum lima_gpu_type gpu_type;

	struct clk *clk_bus;
	struct clk *clk_gpu;
	struct reset_control *reset;

	struct lima_pmu *pmu;

	struct lima_l2_cache *l2_cache;

	struct lima_mmu *mmu[LIMA_MAX_PP + 1];
	int num_mmu;

	struct lima_gp *gp;

	struct lima_pp *pp[LIMA_MAX_PP];
	int num_pp;
};

int lima_device_init(struct lima_device *ldev, struct drm_device *dev);
void lima_device_fini(struct lima_device *ldev);

int lima_pmu_init(struct lima_pmu *pmu);
void lima_pmu_fini(struct lima_pmu *pmu);

int lima_l2_cache_init(struct lima_l2_cache *l2_cache);
void lima_l2_cache_fini(struct lima_l2_cache *l2_cache);

int lima_mmu_init(struct lima_mmu *mmu);
void lima_mmu_fini(struct lima_mmu *mmu);

int lima_gp_init(struct lima_gp *gp);
void lima_gp_fini(struct lima_gp *gp);

int lima_pp_init(struct lima_pp *pp);
void lima_pp_fini(struct lima_pp *pp);

#endif
