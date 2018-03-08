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

#include <drm/drmP.h>
#include <linux/dma-buf.h>

#include "lima_gem.h"
#include "lima_gem_prime.h"

static void lima_bo_dma_buf_release(struct lima_bo *bo)
{
	if (bo->pages_dma_addr)
		kfree(bo->pages_dma_addr);

	if (bo->pages)
		kfree(bo->pages);

	drm_prime_gem_destroy(&bo->gem, bo->sgt);
}

static int lima_bo_dma_buf_mmap(struct lima_bo *bo, struct vm_area_struct *vma)
{
	return dma_buf_mmap(bo->gem.dma_buf, vma, 0);
}

static struct lima_bo_ops lima_bo_dma_buf_ops = {
	.release = lima_bo_dma_buf_release,
	.mmap = lima_bo_dma_buf_mmap,
};

struct drm_gem_object *lima_gem_prime_import_sg_table(
	struct drm_device *dev, struct dma_buf_attachment *attach,
	struct sg_table *sgt)
{
	struct lima_bo *bo;
	struct drm_gem_object *ret;

	bo = lima_gem_create_bo(dev, attach->dmabuf->size, 0);
	if (!bo)
		return ERR_PTR(-ENOMEM);

	bo->type = lima_bo_type_dma_buf;
	bo->ops = &lima_bo_dma_buf_ops;
	bo->sgt = sgt;

	if (sgt->nents == 1) {
		bo->cpu_addr = sg_virt(sgt->sgl);
		bo->dma_addr = sg_dma_address(sgt->sgl);
	}
	else {
		int err, npages = attach->dmabuf->size >> PAGE_SHIFT;

		bo->pages_dma_addr = kzalloc(npages * sizeof(dma_addr_t), GFP_KERNEL);
		if (!bo->pages_dma_addr) {
			ret = ERR_PTR(-ENOMEM);
			goto err_out;
		}

		bo->pages = kzalloc(npages * sizeof(*bo->pages), GFP_KERNEL);
		if (!bo->pages) {
			ret = ERR_PTR(-ENOMEM);
			goto err_out;
		}

		err = drm_prime_sg_to_page_addr_arrays(
			sgt, bo->pages, bo->pages_dma_addr, npages);
	        if (err) {
			ret = ERR_PTR(err);
			goto err_out;
		}
	}

	bo->resv = attach->dmabuf->resv;

	return &bo->gem;

err_out:
	lima_gem_free_object(&bo->gem);
	return ret;
}

struct reservation_object *lima_gem_prime_res_obj(struct drm_gem_object *obj)
{
        struct lima_bo *bo = to_lima_bo(obj);

	return bo->resv;
}

struct sg_table *lima_gem_prime_get_sg_table(struct drm_gem_object *obj)
{
	struct lima_bo *bo = to_lima_bo(obj);
	struct sg_table *sgt;
	int ret;

	if (bo->pages)
		return drm_prime_pages_to_sg(bo->pages, obj->size >> PAGE_SHIFT);

	sgt = kzalloc(sizeof(*sgt), GFP_KERNEL);
	if (!sgt)
		return NULL;

	ret = dma_get_sgtable(obj->dev->dev, sgt, bo->cpu_addr,
			      bo->dma_addr, obj->size);
	if (ret < 0) {
		kfree(sgt);
		return NULL;
	}

	return sgt;
}
