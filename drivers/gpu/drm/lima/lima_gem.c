#include <drm/drmP.h>
#include <drm/drm_gem.h>
#include <linux/dma-mapping.h>

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

	err = lima_vm_map(&priv->vm, bo->dma_addr, va, obj->size);
	if (err)
		goto err_out0;

	bo_va = kmalloc(sizeof(*bo_va), GFP_KERNEL);
	if (!bo_va) {
		err = -ENOMEM;
		goto err_out1;
	}
	INIT_LIST_HEAD(&bo_va->list);
	bo_va->vm = &priv->vm;
	bo_va->va = va;

	mutex_lock(&bo->lock);

	list_add(&bo_va->list, &bo->va);

	mutex_unlock(&bo->lock);

	drm_gem_object_unreference_unlocked(obj);
	return 0;

err_out1:
	lima_vm_unmap(&priv->vm, va, obj->size);
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
		if (bo_va->vm == &priv->vm && bo_va->va == va) {
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

	err = lima_vm_unmap(&priv->vm, va, obj->size);

out:
	drm_gem_object_unreference_unlocked(obj);
	return err;
}
