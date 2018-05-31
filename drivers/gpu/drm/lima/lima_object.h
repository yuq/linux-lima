/* SPDX-License-Identifier: GPL-2.0 OR MIT */
/* Copyright 2018 Qiang Yu <yuq825@gmail.com> */

#ifndef __LIMA_OBJECT_H__
#define __LIMA_OBJECT_H__

#include <drm/drm_gem.h>
#include <drm/ttm/ttm_placement.h>
#include <drm/ttm/ttm_bo_api.h>

#include "lima_device.h"

struct lima_bo {
	struct drm_gem_object gem;

	struct ttm_place place;
	struct ttm_placement placement;
	struct ttm_buffer_object tbo;
	struct ttm_bo_kmap_obj kmap;

	struct list_head va;
};

static inline struct lima_bo *
to_lima_bo(struct drm_gem_object *obj)
{
	return container_of(obj, struct lima_bo, gem);
}

static inline struct lima_bo *
ttm_to_lima_bo(struct ttm_buffer_object *tbo)
{
	return container_of(tbo, struct lima_bo, tbo);
}

static inline int lima_bo_reserve(struct lima_bo *bo, bool intr)
{
	struct lima_device *dev = ttm_to_lima_dev(bo->tbo.bdev);
	int r;

	r = ttm_bo_reserve(&bo->tbo, intr, false, NULL);
	if (unlikely(r != 0)) {
		if (r != -ERESTARTSYS)
			dev_err(dev->dev, "%p reserve failed\n", bo);
		return r;
	}
	return 0;
}

static inline void lima_bo_unreserve(struct lima_bo *bo)
{
	ttm_bo_unreserve(&bo->tbo);
}

struct lima_bo *lima_bo_create(struct lima_device *dev, u64 size,
			       u32 flags, enum ttm_bo_type type,
			       struct sg_table *sg,
			       struct reservation_object *resv);

static inline void lima_bo_unref(struct lima_bo *bo)
{
	struct ttm_buffer_object *tbo = &bo->tbo;
	ttm_bo_unref(&tbo);
}

dma_addr_t *lima_bo_get_pages(struct lima_bo *bo);
void *lima_bo_kmap(struct lima_bo *bo);

#endif
