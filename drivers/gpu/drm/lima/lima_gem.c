#include <drm/drmP.h>
#include <drm/drm_gem.h>


struct lima_bo {
	struct drm_gem_object gem;
};

static inline
struct lima_bo *to_lima_bo(struct drm_gem_object *obj)
{
	return container_of(obj, struct lima_bo, gem);
}

static struct lima_bo *lima_gem_create_bo(struct drm_device *dev, u32 size, u32 flags)
{
	int err;
	struct lima_bo *bo;

	size = PAGE_ALIGN(size);

	bo = kzalloc(sizeof(*bo), GFP_KERNEL);
	if (!bo)
		return ERR_PTR(-ENOMEM);

	err = drm_gem_object_init(dev, &bo->gem, size);
	if (err) {
		kfree(bo);
		return ERR_PTR(err);
	}

	return bo;
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

	drm_gem_object_release(obj);
	kfree(bo);
}
