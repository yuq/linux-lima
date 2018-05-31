// SPDX-License-Identifier: GPL-2.0 OR MIT
/* Copyright 2018 Qiang Yu <yuq825@gmail.com> */

#include <linux/dma-buf.h>
#include <drm/drm_prime.h>

#include "lima_device.h"
#include "lima_object.h"
#include "lima_gem_prime.h"

struct drm_gem_object *lima_gem_prime_import_sg_table(
	struct drm_device *dev, struct dma_buf_attachment *attach,
	struct sg_table *sgt)
{
	struct reservation_object *resv = attach->dmabuf->resv;
	struct lima_device *ldev = to_lima_dev(dev);
	struct lima_bo *bo;

	ww_mutex_lock(&resv->lock, NULL);

	bo = lima_bo_create(ldev, attach->dmabuf->size, 0,
			    ttm_bo_type_sg, sgt, resv);
	if (IS_ERR(bo))
		goto err_out;

	ww_mutex_unlock(&resv->lock);
	return &bo->gem;

err_out:
	ww_mutex_unlock(&resv->lock);
	return (void *)bo;
}

struct reservation_object *lima_gem_prime_res_obj(struct drm_gem_object *obj)
{
        struct lima_bo *bo = to_lima_bo(obj);

	return bo->tbo.resv;
}

struct sg_table *lima_gem_prime_get_sg_table(struct drm_gem_object *obj)
{
	struct lima_bo *bo = to_lima_bo(obj);
	int npages = bo->tbo.num_pages;

	return drm_prime_pages_to_sg(bo->tbo.ttm->pages, npages);
}
