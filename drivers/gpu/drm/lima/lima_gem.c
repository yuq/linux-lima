// SPDX-License-Identifier: GPL-2.0 OR MIT
/* Copyright 2017-2018 Qiang Yu <yuq825@gmail.com> */

#include <drm/drmP.h>
#include <linux/dma-mapping.h>
#include <linux/pagemap.h>
#include <linux/sync_file.h>

#include <drm/lima_drm.h>

#include "lima_drv.h"
#include "lima_gem.h"
#include "lima_vm.h"
#include "lima_object.h"

int lima_gem_create_handle(struct drm_device *dev, struct drm_file *file,
			   u32 size, u32 flags, u32 *handle)
{
	int err;
	struct lima_bo *bo;
	struct lima_device *ldev = to_lima_dev(dev);

	bo = lima_bo_create(ldev, size, flags, ttm_bo_type_device, NULL, NULL);
	if (IS_ERR(bo))
		return PTR_ERR(bo);

	err = drm_gem_handle_create(file, &bo->gem, handle);

	/* drop reference from allocate - handle holds it now */
	drm_gem_object_put_unlocked(&bo->gem);

	return err;
}

void lima_gem_free_object(struct drm_gem_object *obj)
{
	struct lima_bo *bo = to_lima_bo(obj);

	if (!list_empty(&bo->va))
		dev_err(obj->dev->dev, "lima gem free bo still has va\n");

	lima_bo_unref(bo);
}

int lima_gem_object_open(struct drm_gem_object *obj, struct drm_file *file)
{
	struct lima_bo *bo = to_lima_bo(obj);
	struct lima_drm_priv *priv = to_lima_drm_priv(file);
	struct lima_vm *vm = priv->vm;
	int err;

	err = lima_bo_reserve(bo, true);
	if (err)
		return err;

	err = lima_vm_bo_add(vm, bo);

	lima_bo_unreserve(bo);
	return err;
}

void lima_gem_object_close(struct drm_gem_object *obj, struct drm_file *file)
{
	struct lima_bo *bo = to_lima_bo(obj);
	struct lima_device *dev = to_lima_dev(obj->dev);
	struct lima_drm_priv *priv = to_lima_drm_priv(file);
	struct lima_vm *vm = priv->vm;

	LIST_HEAD(list);
	struct ttm_validate_buffer tv_bo, tv_pd;
	struct ww_acquire_ctx ticket;
	int r;

	tv_bo.bo = &bo->tbo;
	tv_bo.shared = true;
	list_add(&tv_bo.head, &list);

	tv_pd.bo = &vm->pd->tbo;
	tv_pd.shared = true;
	list_add(&tv_pd.head, &list);

	r = ttm_eu_reserve_buffers(&ticket, &list, false, NULL);
	if (r) {
		dev_err(dev->dev, "leeking bo va because we "
			"fail to reserve bo (%d)\n", r);
		return;
	}

	lima_vm_bo_del(vm, bo);

	ttm_eu_backoff_reservation(&ticket, &list);
}

int lima_gem_mmap_offset(struct drm_file *file, u32 handle, u64 *offset)
{
	struct drm_gem_object *obj;
	struct lima_bo *bo;

	obj = drm_gem_object_lookup(file, handle);
	if (!obj)
		return -ENOENT;

	bo = to_lima_bo(obj);
	*offset = drm_vma_node_offset_addr(&bo->tbo.vma_node);

	drm_gem_object_put_unlocked(obj);
	return 0;
}

int lima_gem_mmap(struct file *filp, struct vm_area_struct *vma)
{
	struct drm_file *file_priv;
	struct lima_device *dev;

	if (unlikely(vma->vm_pgoff < DRM_FILE_PAGE_OFFSET))
		return -EINVAL;

	file_priv = filp->private_data;
	dev = file_priv->minor->dev->dev_private;
	if (dev == NULL)
		return -EINVAL;

	return ttm_bo_mmap(filp, vma, &dev->mman.bdev);
}

int lima_gem_va_map(struct drm_file *file, u32 handle, u32 flags, u32 va)
{
	struct lima_drm_priv *priv = to_lima_drm_priv(file);
	struct lima_vm *vm = priv->vm;
	struct drm_gem_object *obj;
	struct lima_bo *bo;
	struct lima_device *dev;
	int err;

	LIST_HEAD(list);
	struct ttm_validate_buffer tv_bo, tv_pd;
	struct ww_acquire_ctx ticket;

	if (!PAGE_ALIGNED(va))
		return -EINVAL;

	obj = drm_gem_object_lookup(file, handle);
	if (!obj)
		return -ENOENT;

	bo = to_lima_bo(obj);
	dev = to_lima_dev(obj->dev);

	/* carefully handle overflow when calculate range */
	if (va < dev->va_start || dev->va_end - obj->size < va) {
		err = -EINVAL;
		goto out;
	}

	tv_bo.bo = &bo->tbo;
	tv_bo.shared = true;
	list_add(&tv_bo.head, &list);

	tv_pd.bo = &vm->pd->tbo;
	tv_pd.shared = true;
	list_add(&tv_pd.head, &list);

	err = ttm_eu_reserve_buffers(&ticket, &list, false, NULL);
	if (err)
		goto out;

	err = lima_vm_bo_map(vm, bo, va);

	ttm_eu_backoff_reservation(&ticket, &list);
out:
	drm_gem_object_put_unlocked(obj);
	return err;
}

int lima_gem_va_unmap(struct drm_file *file, u32 handle, u32 va)
{
	struct lima_drm_priv *priv = to_lima_drm_priv(file);
	struct lima_vm *vm = priv->vm;
	struct drm_gem_object *obj;
	struct lima_bo *bo;
	int err;

	LIST_HEAD(list);
	struct ttm_validate_buffer tv_bo, tv_pd;
	struct ww_acquire_ctx ticket;

	if (!PAGE_ALIGNED(va))
		return -EINVAL;

	obj = drm_gem_object_lookup(file, handle);
	if (!obj)
		return -ENOENT;

	bo = to_lima_bo(obj);

	tv_bo.bo = &bo->tbo;
	tv_bo.shared = true;
	list_add(&tv_bo.head, &list);

	tv_pd.bo = &vm->pd->tbo;
	tv_pd.shared = true;
	list_add(&tv_pd.head, &list);

	err = ttm_eu_reserve_buffers(&ticket, &list, false, NULL);
	if (err)
		goto out;

	err = lima_vm_bo_unmap(vm, bo, va);

	ttm_eu_backoff_reservation(&ticket, &list);
out:
	drm_gem_object_put_unlocked(obj);
	return err;
}

static int lima_gem_sync_bo(struct lima_sched_task *task, struct lima_bo *bo,
			    bool write, bool explicit)
{
	int err = 0;

	if (!write) {
		err = reservation_object_reserve_shared(bo->tbo.resv);
		if (err)
			return err;
	}

	/* explicit sync use user passed dep fence */
	if (explicit)
		return 0;

	/* implicit sync use bo fence in resv obj */
	if (write) {
		unsigned nr_fences;
		struct dma_fence **fences;
		int i;

		err = reservation_object_get_fences_rcu(
			bo->tbo.resv, NULL, &nr_fences, &fences);
		if (err || !nr_fences)
			return err;

		for (i = 0; i < nr_fences; i++) {
			err = lima_sched_task_add_dep(task, fences[i]);
			if (err)
				break;
		}

		/* for error case free remaining fences */
		for ( ; i < nr_fences; i++)
			dma_fence_put(fences[i]);

		kfree(fences);
	}
	else {
		struct dma_fence *fence;
		fence = reservation_object_get_excl_rcu(bo->tbo.resv);
		if (fence) {
			err = lima_sched_task_add_dep(task, fence);
			if (err)
				dma_fence_put(fence);
		}
	}

	return err;
}

static int lima_gem_add_deps(struct lima_ctx_mgr *mgr, struct lima_submit *submit)
{
	int i, err = 0;

	for (i = 0; i < submit->nr_deps; i++) {
		union drm_lima_gem_submit_dep *dep = submit->deps + i;
		struct dma_fence *fence;

		if (dep->type == LIMA_SUBMIT_DEP_FENCE) {
			fence = lima_ctx_get_native_fence(
				mgr, dep->fence.ctx, dep->fence.pipe,
				dep->fence.seq);
			if (IS_ERR(fence)) {
				err = PTR_ERR(fence);
				break;
			}
		}
		else if (dep->type == LIMA_SUBMIT_DEP_SYNC_FD) {
			fence = sync_file_get_fence(dep->sync_fd.fd);
			if (!fence) {
				err = -EINVAL;
				break;
			}
		}
		else {
			err = -EINVAL;
			break;
		}

		if (fence) {
			err = lima_sched_task_add_dep(submit->task, fence);
			if (err) {
				dma_fence_put(fence);
				break;
			}
		}
	}

	return err;
}

static int lima_gem_get_sync_fd(struct dma_fence *fence)
{
	struct sync_file *sync_file;
	int fd;

	fd = get_unused_fd_flags(O_CLOEXEC);
	if (fd < 0)
		return fd;

	sync_file = sync_file_create(fence);
	if (!sync_file) {
		put_unused_fd(fd);
		return -ENOMEM;
	}

	fd_install(fd, sync_file->file);
	return fd;
}

int lima_gem_submit(struct drm_file *file, struct lima_submit *submit)
{
	int i, err = 0;
	struct lima_drm_priv *priv = to_lima_drm_priv(file);
	struct lima_vm *vm = priv->vm;

	INIT_LIST_HEAD(&submit->validated);
	INIT_LIST_HEAD(&submit->duplicates);

	for (i = 0; i < submit->nr_bos; i++) {
		struct drm_gem_object *obj;
		struct drm_lima_gem_submit_bo *bo = submit->bos + i;
		struct ttm_validate_buffer *vb = submit->vbs + i;

		obj = drm_gem_object_lookup(file, bo->handle);
		if (!obj) {
			err = -ENOENT;
			goto out0;
		}

		vb->bo = &to_lima_bo(obj)->tbo;
		vb->shared = !(bo->flags & LIMA_SUBMIT_BO_WRITE);
		list_add_tail(&vb->head, &submit->validated);
	}

	submit->vm_pd_vb.bo = &vm->pd->tbo;
	submit->vm_pd_vb.shared = true;
	list_add(&submit->vm_pd_vb.head, &submit->validated);

	err = ttm_eu_reserve_buffers(&submit->ticket, &submit->validated,
				     true, &submit->duplicates);
	if (err)
		goto out0;

	err = lima_sched_task_init(
		submit->task, submit->ctx->context + submit->pipe, vm);
	if (err)
		goto out1;

	err = lima_gem_add_deps(&priv->ctx_mgr, submit);
	if (err)
		goto out2;

	for (i = 0; i < submit->nr_bos; i++) {
		struct ttm_validate_buffer *vb = submit->vbs + i;
		struct lima_bo *bo = ttm_to_lima_bo(vb->bo);
		err = lima_gem_sync_bo(
			submit->task, bo, !vb->shared,
			submit->flags & LIMA_SUBMIT_FLAG_EXPLICIT_FENCE);
		if (err)
			goto out2;
	}

	if (submit->flags & LIMA_SUBMIT_FLAG_SYNC_FD_OUT) {
		int fd = lima_gem_get_sync_fd(
			&submit->task->base.s_fence->finished);
		if (fd < 0) {
			err = fd;
			goto out2;
		}
		submit->sync_fd = fd;
	}

	submit->fence = lima_sched_context_queue_task(
		submit->ctx->context + submit->pipe, submit->task,
		&submit->done);

	ttm_eu_fence_buffer_objects(&submit->ticket, &submit->validated,
				    &submit->task->base.s_fence->finished);

out2:
	if (err)
		lima_sched_task_fini(submit->task);
out1:
        if (err)
		ttm_eu_backoff_reservation(&submit->ticket, &submit->validated);
out0:
	for (i = 0; i < submit->nr_bos; i++) {
		struct ttm_validate_buffer *vb = submit->vbs + i;
		if (!vb->bo)
			break;
		drm_gem_object_put_unlocked(&ttm_to_lima_bo(vb->bo)->gem);
	}
	return err;
}

int lima_gem_wait(struct drm_file *file, u32 handle, u32 op, u64 timeout_ns)
{
	bool write = op & LIMA_GEM_WAIT_WRITE;
	struct drm_gem_object *obj;
	struct lima_bo *bo;
	signed long ret;
	unsigned long timeout;

	obj = drm_gem_object_lookup(file, handle);
	if (!obj)
		return -ENOENT;

	bo = to_lima_bo(obj);

	timeout = timeout_ns ? lima_timeout_to_jiffies(timeout_ns) : 0;

	ret = lima_bo_reserve(bo, true);
	if (ret)
		goto out;

	/* must use long for result check because in 64bit arch int
	 * will overflow if timeout is too large and get <0 result
	 */
	ret = reservation_object_wait_timeout_rcu(bo->tbo.resv, write, true, timeout);
	if (ret == 0)
		ret = timeout ? -ETIMEDOUT : -EBUSY;
	else if (ret > 0)
		ret = 0;

	lima_bo_unreserve(bo);
out:
	drm_gem_object_put_unlocked(obj);
	return ret;
}
