/* SPDX-License-Identifier: GPL-2.0 OR MIT */
/* Copyright 2018 Qiang Yu <yuq825@gmail.com> */

#ifndef __LIMA_TTM_H__
#define __LIMA_TTM_H__

#include <drm/ttm/ttm_bo_driver.h>

struct lima_mman {
	struct ttm_bo_global_ref bo_global_ref;
	struct drm_global_reference mem_global_ref;
	struct ttm_bo_device bdev;
	bool mem_global_referenced;
};

struct lima_ttm_tt {
	struct ttm_dma_tt ttm;
};

struct lima_device;
struct lima_bo;

int lima_ttm_init(struct lima_device *dev);
void lima_ttm_fini(struct lima_device *dev);

#endif
