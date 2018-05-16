/*
 * Copyright (C) 2017-2018 Lima Project
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
