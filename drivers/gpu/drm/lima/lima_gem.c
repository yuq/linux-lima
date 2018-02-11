#include <drm/drmP.h>
#include <drm/drm_gem.h>
#include <linux/dma-buf.h>
#include <linux/dma-mapping.h>
#include <linux/reservation.h>

#include "lima.h"

struct lima_bo_va {
	struct list_head list;
	unsigned ref_count;

	struct list_head mapping;

	struct lima_vm *vm;
};

struct lima_bo {
	struct drm_gem_object gem;
	dma_addr_t dma_addr;
	void *cpu_addr;

	struct mutex lock;
	struct list_head va;

	/* normally (resv == &_resv) except for imported bo's */
	struct reservation_object *resv;
	struct reservation_object _resv;
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
	reservation_object_init(&bo->_resv);

	err = drm_gem_object_init(dev, &bo->gem, size);
	if (err)
		goto err_out0;

	return bo;

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

	bo->cpu_addr = dma_alloc_coherent(dev->dev, size, &bo->dma_addr, GFP_USER);
	if (!bo->cpu_addr) {
		err = -ENOMEM;
		goto err_out;
	}

	bo->resv = &bo->_resv;

	err = drm_gem_handle_create(file, &bo->gem, handle);

	/* drop reference from allocate - handle holds it now */
	drm_gem_object_unreference_unlocked(&bo->gem);

	return err;

err_out:
	lima_gem_free_object(&bo->gem);
	return err;
}

void lima_gem_free_object(struct drm_gem_object *obj)
{
	struct lima_bo *bo = to_lima_bo(obj);

	if (!list_empty(&bo->va))
		dev_err(obj->dev->dev, "lima gem free bo still has va\n");

	if (!obj->import_attach)
		dma_free_coherent(obj->dev->dev, obj->size, bo->cpu_addr, bo->dma_addr);

	reservation_object_fini(&bo->_resv);
	drm_gem_object_release(obj);
	kfree(bo);
}

static struct lima_bo_va *lima_gem_find_bo_va(struct lima_bo *bo, struct lima_vm *vm)
{
	struct lima_bo_va *bo_va, *ret = NULL;

	list_for_each_entry(bo_va, &bo->va, list) {
		if (bo_va->vm == vm) {
			ret = bo_va;
			break;
		}
	}

	return ret;
}

int lima_gem_object_open(struct drm_gem_object *obj, struct drm_file *file)
{
	struct lima_bo *bo = to_lima_bo(obj);
	struct lima_drm_priv *priv = to_lima_drm_priv(file);
	struct lima_vm *vm = priv->vm;
	struct lima_bo_va *bo_va;
	int err = 0;

	mutex_lock(&bo->lock);

	bo_va = lima_gem_find_bo_va(bo, vm);
	if (bo_va)
		bo_va->ref_count++;
	else {
		bo_va = kmalloc(sizeof(*bo_va), GFP_KERNEL);
		if (!bo_va) {
			err = -ENOMEM;
			goto out;
		}

		bo_va->vm = vm;
		bo_va->ref_count = 1;
		INIT_LIST_HEAD(&bo_va->mapping);
		list_add_tail(&bo_va->list, &bo->va);
	}

out:
	mutex_unlock(&bo->lock);
	return err;
}

void lima_gem_object_close(struct drm_gem_object *obj, struct drm_file *file)
{
	struct lima_bo *bo = to_lima_bo(obj);
	struct lima_drm_priv *priv = to_lima_drm_priv(file);
	struct lima_vm *vm = priv->vm;
	struct lima_bo_va *bo_va;

	mutex_lock(&bo->lock);

	bo_va = lima_gem_find_bo_va(bo, vm);
	BUG_ON(!bo_va);

	if (--bo_va->ref_count == 0) {
		struct lima_bo_va_mapping *mapping, *tmp;
		list_for_each_entry_safe(mapping, tmp, &bo_va->mapping, list) {
			lima_vm_unmap(vm, mapping);
			list_del(&mapping->list);
			kfree(mapping);
		}
		list_del(&bo_va->list);
		kfree(bo_va);
	}

	mutex_unlock(&bo->lock);
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

static int lima_gem_fault(struct vm_fault *vmf)
{
	struct vm_area_struct *vma = vmf->vma;
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
	struct lima_vm *vm = priv->vm;
	struct drm_gem_object *obj;
	struct lima_bo *bo;
	struct lima_bo_va *bo_va;
	struct lima_bo_va_mapping *mapping;
	int err;

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

	mapping = kmalloc(sizeof(*mapping), GFP_KERNEL);
	if (!mapping) {
		err = -ENOMEM;
		goto err_out0;
	}

	mapping->start = va;
	mapping->last = va + obj->size - 1;

	mutex_lock(&bo->lock);

	bo_va = lima_gem_find_bo_va(bo, vm);
	BUG_ON(!bo_va);

	err = lima_vm_map(vm, bo->dma_addr, mapping);
	if (err) {
		mutex_unlock(&bo->lock);
		goto err_out1;
	}

	list_add_tail(&mapping->list, &bo_va->mapping);

	mutex_unlock(&bo->lock);

	drm_gem_object_unreference_unlocked(obj);
	return 0;

err_out1:
	kfree(mapping);
err_out0:
	drm_gem_object_unreference_unlocked(obj);
	return err;
}

int lima_gem_va_unmap(struct drm_file *file, u32 handle, u32 va)
{
	struct lima_drm_priv *priv = to_lima_drm_priv(file);
	struct lima_vm *vm = priv->vm;
	struct drm_gem_object *obj;
	struct lima_bo *bo;
	struct lima_bo_va *bo_va;
	struct lima_bo_va_mapping *mapping;

	obj = drm_gem_object_lookup(file, handle);
	if (!obj)
		return -ENOENT;

	bo = to_lima_bo(obj);

	mutex_lock(&bo->lock);

	bo_va = lima_gem_find_bo_va(bo, vm);
	BUG_ON(!bo_va);

	list_for_each_entry(mapping, &bo_va->mapping, list) {
		if (mapping->start == va) {
			lima_vm_unmap(vm, mapping);
			list_del(&mapping->list);
			kfree(mapping);
			break;
		}
	}

	mutex_unlock(&bo->lock);

	drm_gem_object_unreference_unlocked(obj);
	return 0;
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

		ret = ww_mutex_lock_interruptible(&bos[i]->resv->lock, ctx);
		if (ret < 0) {
			contended = i;
			goto err;
		}
	}

	ww_acquire_done(ctx);
	return 0;

err:
	for (i--; i >= 0; i--)
		ww_mutex_unlock(&bos[i]->resv->lock);

	if (slow_locked >= 0)
		ww_mutex_unlock(&bos[slow_locked]->resv->lock);

	if (ret == -EDEADLK) {
		/* we lost out in a seqno race, lock and retry.. */
		ret = ww_mutex_lock_slow_interruptible(&bos[contended]->resv->lock, ctx);
		if (!ret) {
			slow_locked = contended;
			goto retry;
		}
	}
	ww_acquire_fini(ctx);

	return ret;
}

static int lima_gem_sync_bo(struct lima_sched_task *task, struct lima_bo *bo, bool write)
{
	int i, err;
	struct dma_fence *f;
	u64 context = task->base.s_fence->finished.context;

	if (write) {
		struct reservation_object_list *fobj =
			reservation_object_get_list(bo->resv);

		if (fobj && fobj->shared_count > 0) {
			for (i = 0; i < fobj->shared_count; i++) {
				f = rcu_dereference_protected(
					fobj->shared[i], reservation_object_held(bo->resv));
				if (f->context != context) {
					err = lima_sched_task_add_dep(task, f);
					if (err)
						return err;
				}
			}
		}
	}

	f = reservation_object_get_excl(bo->resv);
	if (f) {
		err = lima_sched_task_add_dep(task, f);
		if (err)
			return err;
	}

	if (!write) {
		err = reservation_object_reserve_shared(bo->resv);
		if (err)
			return err;
	}

	return 0;
}

int lima_gem_submit(struct drm_file *file, struct lima_submit *submit, u32 *fence)
{
	int i, err = 0;
	struct ww_acquire_ctx ctx;
	struct lima_drm_priv *priv = to_lima_drm_priv(file);

	for (i = 0; i < submit->nr_bos; i++) {
		struct drm_gem_object *obj;

		obj = drm_gem_object_lookup(file, submit->bos[i].handle);
		if (!obj) {
			err = -ENOENT;
			goto out0;
		}
		submit->lbos[i] = to_lima_bo(obj);
	}

	err = lima_gem_lock_bos(submit->lbos, submit->nr_bos, &ctx);
	if (err)
		goto out0;

	err = lima_sched_task_init(
		submit->task, submit->ctx->context + submit->pipe, priv->vm);
	if (err)
		goto out1;

	for (i = 0; i < submit->nr_bos; i++) {
		err = lima_gem_sync_bo(submit->task, submit->lbos[i],
				       submit->bos[i].flags & LIMA_SUBMIT_BO_WRITE);
		if (err)
			goto out2;
	}

	for (i = 0; i < submit->nr_bos; i++) {
		if (submit->bos[i].flags & LIMA_SUBMIT_BO_WRITE)
			reservation_object_add_excl_fence(
				submit->lbos[i]->resv, &submit->task->base.s_fence->finished);
		else
			reservation_object_add_shared_fence(
				submit->lbos[i]->resv, &submit->task->base.s_fence->finished);
	}

	*fence = lima_sched_context_queue_task(
		submit->ctx->context + submit->pipe, submit->task);

out2:
	if (err)
		lima_sched_task_fini(submit->task);
out1:
	for (i = 0; i < submit->nr_bos; i++)
		ww_mutex_unlock(&submit->lbos[i]->resv->lock);
	ww_acquire_fini(&ctx);
out0:
	for (i = 0; i < submit->nr_bos && submit->lbos[i]; i++)
		drm_gem_object_unreference_unlocked(&submit->lbos[i]->gem);
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
	ret = reservation_object_wait_timeout_rcu(bo->resv, write, true, timeout);
	if (ret == 0)
		ret = timeout ? -ETIMEDOUT : -EBUSY;
	else if (ret > 0)
		ret = 0;

	drm_gem_object_unreference_unlocked(obj);
	return ret;
}

struct drm_gem_object *lima_gem_prime_import_sg_table(struct drm_device *dev,
						      struct dma_buf_attachment *attach,
						      struct sg_table *sgt)
{
	struct lima_bo *bo;

	bo = lima_gem_create_bo(dev, attach->dmabuf->size, 0);
	if (!bo)
		return ERR_PTR(-ENOMEM);

	bo->cpu_addr = sg_virt(sgt->sgl);
	bo->dma_addr = sg_dma_address(sgt->sgl);
	bo->resv = attach->dmabuf->resv;

	return &bo->gem;
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

	sgt = kzalloc(sizeof(*sgt), GFP_KERNEL);
	if (!sgt)
		return NULL;

	ret = dma_get_sgtable(obj->dev->dev, sgt, bo->cpu_addr,
			      bo->dma_addr, obj->size);
	if (ret < 0)
		goto out;

	return sgt;

out:
	kfree(sgt);
	return NULL;
}
