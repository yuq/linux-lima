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
#ifndef __LIMA_GEM_H__
#define __LIMA_GEM_H__

#include <drm/drm_gem.h>
#include <linux/reservation.h>

struct lima_bo;
struct lima_submit;
struct lima_vm;

struct lima_bo_va {
	struct list_head list;
	unsigned ref_count;

	struct list_head mapping;

	struct lima_vm *vm;
};

struct lima_bo_ops {
	void (*release)(struct lima_bo *);
	int (*mmap)(struct lima_bo *, struct vm_area_struct *);
};

struct lima_bo {
	struct drm_gem_object gem;

	enum lima_bo_type {
		lima_bo_type_shmem,
		lima_bo_type_dma_buf,
	} type;

	struct page **pages;
	dma_addr_t *pages_dma_addr;	
	struct sg_table *sgt;

	struct lima_bo_ops *ops;

	struct mutex lock;
	struct list_head va;

	/* normally (resv == &_resv) except for imported bo's */
	struct reservation_object *resv;
	struct reservation_object _resv;
};

static inline struct lima_bo *
to_lima_bo(struct drm_gem_object *obj)
{
	return container_of(obj, struct lima_bo, gem);
}

struct lima_bo *lima_gem_create_bo(struct drm_device *dev, u32 size, u32 flags);
int lima_gem_create_handle(struct drm_device *dev, struct drm_file *file,
			   u32 size, u32 flags, u32 *handle);
void lima_gem_free_object(struct drm_gem_object *obj);
int lima_gem_object_open(struct drm_gem_object *obj, struct drm_file *file);
void lima_gem_object_close(struct drm_gem_object *obj, struct drm_file *file);
int lima_gem_mmap_offset(struct drm_file *file, u32 handle, u64 *offset);
int lima_gem_mmap(struct file *filp, struct vm_area_struct *vma);
int lima_gem_va_map(struct drm_file *file, u32 handle, u32 flags, u32 va);
int lima_gem_va_unmap(struct drm_file *file, u32 handle, u32 va);
int lima_gem_submit(struct drm_file *file, struct lima_submit *submit);
int lima_gem_wait(struct drm_file *file, u32 handle, u32 op, u64 timeout_ns);

#endif
