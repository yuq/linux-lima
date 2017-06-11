#include <drm/drmP.h>
#include <drm/drm_gem.h>
#include <linux/dma-mapping.h>
#include <linux/reservation.h>

#include "lima.h"

struct lima_bo_va {
	struct list_head list;
	struct lima_vm *vm;
	u32 va;
};

struct lima_bo {
	struct drm_gem_object gem;
	dma_addr_t dma_addr;
	void *cpu_addr;

	struct mutex lock;
	struct list_head va;

	struct reservation_object resv;
};

static inline
struct lima_bo *to_lima_bo(struct drm_gem_object *obj)
{
	return container_of(obj, struct lima_bo, gem);
}

static inline
struct lima_drm_priv *to_lima_drm_priv(struct drm_file *file)
{
	return file->driver_priv;
}

static struct lima_bo *lima_gem_create_bo(struct drm_device *dev, u32 size, u32 flags)
{
	int err;
	struct lima_bo *bo;

	size = PAGE_ALIGN(size);

	bo = kzalloc(sizeof(*bo), GFP_KERNEL);
	if (!bo)
		return ERR_PTR(-ENOMEM);

	mutex_init(&bo->lock);
	INIT_LIST_HEAD(&bo->va);
	reservation_object_init(&bo->resv);

	err = drm_gem_object_init(dev, &bo->gem, size);
	if (err)
		goto err_out0;

	bo->cpu_addr = dma_alloc_coherent(dev->dev, size, &bo->dma_addr, GFP_USER);
	if (!bo->cpu_addr) {
		err = -ENOMEM;
		goto err_out1;
	}

	return bo;

err_out1:
	drm_gem_object_release(&bo->gem);
err_out0:
	kfree(bo);
	return ERR_PTR(err);
}

int lima_gem_create_handle(struct drm_device *dev, struct drm_file *file,
			   u32 size, u32 flags, u32 *handle)
{
	int err;
	struct lima_bo *bo;

	bo = lima_gem_create_bo(dev, size, flags);
	if (IS_ERR(bo))
		return PTR_ERR(bo);

	err = drm_gem_handle_create(file, &bo->gem, handle);

	/* drop reference from allocate - handle holds it now */
	drm_gem_object_unreference_unlocked(&bo->gem);

	return err;
}

void lima_gem_free_object(struct drm_gem_object *obj)
{
	struct lima_bo *bo = to_lima_bo(obj);
	struct lima_bo_va *bo_va, *tmp;

	list_for_each_entry_safe(bo_va, tmp, &bo->va, list) {
		lima_vm_unmap(bo_va->vm, bo_va->va, obj->size);
		list_del(&bo_va->list);
		kfree(bo_va);
	}

	dma_free_coherent(obj->dev->dev, obj->size, bo->cpu_addr, bo->dma_addr);
	reservation_object_fini(&bo->resv);
	drm_gem_object_release(obj);
	kfree(bo);
}

int lima_gem_mmap_offset(struct drm_file *file, u32 handle, u64 *offset)
{
	int err;
	struct drm_gem_object *obj;

	obj = drm_gem_object_lookup(file, handle);
	if (!obj)
		return -ENOENT;

	err = drm_gem_create_mmap_offset(obj);
	if (!err)
		*offset = drm_vma_node_offset_addr(&obj->vma_node);

	drm_gem_object_unreference_unlocked(obj);
	return err;
}

int lima_gem_mmap(struct file *filp, struct vm_area_struct *vma)
{
	int err;
	struct lima_bo *bo;

	err = drm_gem_mmap(filp, vma);
	if (err)
		return err;

	vma->vm_pgoff = 0;

	bo = to_lima_bo(vma->vm_private_data);
	err = dma_mmap_coherent(bo->gem.dev->dev, vma, bo->cpu_addr,
				bo->dma_addr, bo->gem.size);
	if (err) {
		drm_gem_vm_close(vma);
		return err;
	}

	return 0;
}

static int lima_gem_fault(struct vm_area_struct *vma, struct vm_fault *vmf)
{
	struct lima_bo *bo = to_lima_bo(vma->vm_private_data);

	dev_err(bo->gem.dev->dev, "unexpected vm fault %lx\n", vmf->address);
	return 0;
}

const struct vm_operations_struct lima_gem_vm_ops = {
	.fault = lima_gem_fault,
	.open = drm_gem_vm_open,
	.close = drm_gem_vm_close,
};

int lima_gem_va_map(struct drm_file *file, u32 handle, u32 flags, u32 va)
{
	struct lima_drm_priv *priv = to_lima_drm_priv(file);
	struct drm_gem_object *obj;
	struct lima_bo *bo;
	int err;
	struct lima_bo_va *bo_va;

	if (!PAGE_ALIGNED(va))
		return -EINVAL;

	obj = drm_gem_object_lookup(file, handle);
	if (!obj)
		return -ENOENT;

	bo = to_lima_bo(obj);

	/* overflow */
	if (va + obj->size < va) {
		err = -EINVAL;
		goto err_out0;
	}

	err = lima_vm_map(priv->vm, bo->dma_addr, va, obj->size);
	if (err)
		goto err_out0;

	bo_va = kmalloc(sizeof(*bo_va), GFP_KERNEL);
	if (!bo_va) {
		err = -ENOMEM;
		goto err_out1;
	}
	INIT_LIST_HEAD(&bo_va->list);
	bo_va->vm = priv->vm;
	bo_va->va = va;

	mutex_lock(&bo->lock);

	list_add(&bo_va->list, &bo->va);

	mutex_unlock(&bo->lock);

	drm_gem_object_unreference_unlocked(obj);
	return 0;

err_out1:
	lima_vm_unmap(priv->vm, va, obj->size);
err_out0:
	drm_gem_object_unreference_unlocked(obj);
	return err;
}

int lima_gem_va_unmap(struct drm_file *file, u32 handle, u32 va)
{
	struct lima_drm_priv *priv = to_lima_drm_priv(file);
	struct drm_gem_object *obj;
	struct lima_bo *bo;
	int err = 0;
	struct lima_bo_va *bo_va, *tmp;
	bool found = false;

	obj = drm_gem_object_lookup(file, handle);
	if (!obj)
		return -ENOENT;

	bo = to_lima_bo(obj);

	mutex_lock(&bo->lock);

	list_for_each_entry_safe(bo_va, tmp, &bo->va, list) {
		if (bo_va->vm == priv->vm && bo_va->va == va) {
			list_del(&bo_va->list);
			kfree(bo_va);
			found = true;
			break;
		}
	}

	mutex_unlock(&bo->lock);

	if (!found) {
		err = -ENOENT;
		goto out;
	}

	err = lima_vm_unmap(priv->vm, va, obj->size);

out:
	drm_gem_object_unreference_unlocked(obj);
	return err;
}

static int lima_gem_lock_bos(struct lima_bo **bos, u32 nr_bos,
			     struct ww_acquire_ctx *ctx)
{
        int i, ret = 0, contended, slow_locked = -1;

	ww_acquire_init(ctx, &reservation_ww_class);

retry:
	for (i = 0; i < nr_bos; i++) {
		if (i == slow_locked) {
			slow_locked = -1;
			continue;
		}

		ret = ww_mutex_lock_interruptible(&bos[i]->resv.lock, ctx);
		if (ret < 0) {
			contended = i;
			goto err;
		}
	}

	ww_acquire_done(ctx);
	return 0;

err:
	for (i--; i >= 0; i--)
		ww_mutex_unlock(&bos[i]->resv.lock);

	if (slow_locked >= 0)
		ww_mutex_unlock(&bos[slow_locked]->resv.lock);

	if (ret == -EDEADLK) {
		/* we lost out in a seqno race, lock and retry.. */
		ret = ww_mutex_lock_slow_interruptible(&bos[contended]->resv.lock, ctx);
		if (!ret) {
			slow_locked = contended;
			goto retry;
		}
	}
	ww_acquire_fini(ctx);

	return ret;
}

static int lima_gem_sync_bo(struct lima_sched_task *task, u64 context,
			    struct lima_bo *bo, bool write)
{
	int i, err;
	struct dma_fence *f;

	if (write) {
		struct reservation_object_list *fobj =
			reservation_object_get_list(&bo->resv);

		if (fobj && fobj->shared_count > 0) {
			for (i = 0; i < fobj->shared_count; i++) {
				f = rcu_dereference_protected(
					fobj->shared[i], reservation_object_held(&bo->resv));
				if (f->context != context) {
					err = lima_sched_task_add_dep(task, f);
					if (err)
						return err;
				}
			}
		}
	}

	f = reservation_object_get_excl(&bo->resv);
	if (f) {
		err = lima_sched_task_add_dep(task, f);
		if (err)
			return err;
	}

	if (!write) {
		err = reservation_object_reserve_shared(&bo->resv);
		if (err)
			return err;
	}

	return 0;
}

int lima_gem_submit(struct drm_file *file, struct lima_sched_pipe *pipe,
		    struct drm_lima_gem_submit_bo *bos, u32 nr_bos,
		    void *frame, u32 *fence)
{
	struct lima_bo **lbos;
	int i, err = 0;
	struct ww_acquire_ctx ctx;
	struct lima_sched_task *task;
	struct lima_drm_priv *priv = to_lima_drm_priv(file);

	lbos = kzalloc(sizeof(*lbos) * nr_bos, GFP_KERNEL);
	if (!lbos)
		return -ENOMEM;

	for (i = 0; i < nr_bos; i++) {
		struct drm_gem_object *obj;

		obj = drm_gem_object_lookup(file, bos[i].handle);
		if (!obj) {
			err = -ENOENT;
			goto out0;
		}
		lbos[i] = to_lima_bo(obj);
	}

	err = lima_gem_lock_bos(lbos, nr_bos, &ctx);
	if (err)
		goto out0;

	task = lima_sched_task_create(priv->vm, frame);
	if (IS_ERR(task)) {
		err = PTR_ERR(task);
		goto out1;
	}

	for (i = 0; i < nr_bos; i++) {
		err = lima_gem_sync_bo(task, pipe->fence_context, lbos[i],
				       bos[i].flags & LIMA_SUBMIT_BO_WRITE);
		if (err)
			goto out2;
	}

	err = lima_sched_task_queue(pipe, task);
	if (err)
		goto out2;

	for (i = 0; i < nr_bos; i++) {
		if (bos[i].flags & LIMA_SUBMIT_BO_WRITE)
			reservation_object_add_excl_fence(&lbos[i]->resv, task->fence);
		else
			reservation_object_add_shared_fence(&lbos[i]->resv, task->fence);
	}
	dma_fence_put(task->fence);

	*fence = task->fence->seqno;

out2:
	if (err)
		lima_sched_task_delete(task);
out1:
	for (i = 0; i < nr_bos; i++)
		ww_mutex_unlock(&lbos[i]->resv.lock);
	ww_acquire_fini(&ctx);
out0:
	for (i = 0; i < nr_bos && lbos[i]; i++)
		drm_gem_object_unreference_unlocked(&lbos[i]->gem);
	kfree(lbos);
	return err;
}

int lima_gem_wait(struct drm_file *file, u32 handle, u32 op, u64 timeout_ns)
{
	bool write = op & LIMA_GEM_WAIT_WRITE;
	struct drm_gem_object *obj;
	struct lima_bo *bo;
	int ret;
	unsigned long timeout;

	obj = drm_gem_object_lookup(file, handle);
	if (!obj)
		return -ENOENT;

	bo = to_lima_bo(obj);

	timeout = timeout_ns ? lima_timeout_to_jiffies(timeout_ns) : 0;
	ret = reservation_object_wait_timeout_rcu(&bo->resv, write, true, timeout);
	if (ret == 0)
		ret = timeout ? -ETIMEDOUT : -EBUSY;
	else if (ret > 0)
		ret = 0;

	drm_gem_object_unreference_unlocked(obj);
	return ret;
}
