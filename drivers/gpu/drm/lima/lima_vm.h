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
#ifndef __LIMA_VM_H__
#define __LIMA_VM_H__

#include <linux/rbtree.h>
#include <linux/kref.h>

#define LIMA_PAGE_SIZE    4096
#define LIMA_PAGE_MASK    (LIMA_PAGE_SIZE - 1)
#define LIMA_PAGE_ENT_NUM (LIMA_PAGE_SIZE / sizeof(u32))

#define LIMA_VA_RESERVE_START  0xFFF00000
#define LIMA_VA_RESERVE_DLBU   LIMA_VA_RESERVE_START
#define LIMA_VA_RESERVE_END    0x100000000

struct lima_bo;
struct lima_device;

struct lima_vm {
	struct kref refcount;

	/* tree of virtual addresses mapped */
	struct rb_root_cached va;

	struct lima_device *dev;

	struct lima_bo *pd;
	struct lima_bo *pt[LIMA_PAGE_ENT_NUM];
};

int lima_vm_bo_map(struct lima_vm *vm, struct lima_bo *bo, u32 start);
int lima_vm_bo_unmap(struct lima_vm *vm, struct lima_bo *bo, u32 start);

int lima_vm_bo_add(struct lima_vm *vm, struct lima_bo *bo);
int lima_vm_bo_del(struct lima_vm *vm, struct lima_bo *bo);

struct lima_vm *lima_vm_create(struct lima_device *dev);
void lima_vm_release(struct kref *kref);

static inline struct lima_vm *lima_vm_get(struct lima_vm *vm)
{
	kref_get(&vm->refcount);
	return vm;
}

static inline void lima_vm_put(struct lima_vm *vm)
{
	kref_put(&vm->refcount, lima_vm_release);
}

void lima_vm_print(struct lima_vm *vm);

#endif
