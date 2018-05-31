// SPDX-License-Identifier: GPL-2.0 OR MIT
/* Copyright 2017-2018 Qiang Yu <yuq825@gmail.com> */

#include <linux/slab.h>
#include <linux/dma-mapping.h>
#include <linux/interval_tree_generic.h>

#include "lima_device.h"
#include "lima_vm.h"
#include "lima_object.h"
#include "lima_regs.h"

struct lima_bo_va_mapping {
	struct list_head list;
	struct rb_node rb;
	uint32_t start;
	uint32_t last;
	uint32_t __subtree_last;
};

struct lima_bo_va {
	struct list_head list;
	unsigned ref_count;

	struct list_head mapping;

	struct lima_vm *vm;
};

#define LIMA_VM_PD_SHIFT 22
#define LIMA_VM_PT_SHIFT 12
#define LIMA_VM_PB_SHIFT (LIMA_VM_PD_SHIFT + LIMA_VM_NUM_PT_PER_BT_SHIFT)
#define LIMA_VM_BT_SHIFT LIMA_VM_PT_SHIFT

#define LIMA_VM_PT_MASK ((1 << LIMA_VM_PD_SHIFT) - 1)
#define LIMA_VM_BT_MASK ((1 << LIMA_VM_PB_SHIFT) - 1)

#define LIMA_PDE(va) (va >> LIMA_VM_PD_SHIFT)
#define LIMA_PTE(va) ((va & LIMA_VM_PT_MASK) >> LIMA_VM_PT_SHIFT)
#define LIMA_PBE(va) (va >> LIMA_VM_PB_SHIFT)
#define LIMA_BTE(va) ((va & LIMA_VM_BT_MASK) >> LIMA_VM_BT_SHIFT)

#define START(node) ((node)->start)
#define LAST(node) ((node)->last)

INTERVAL_TREE_DEFINE(struct lima_bo_va_mapping, rb, uint32_t, __subtree_last,
		     START, LAST, static, lima_vm_it)

#undef START
#undef LAST

static void lima_vm_unmap_page_table(struct lima_vm *vm, u32 start, u32 end)
{
	u32 addr;

	for (addr = start; addr <= end; addr += LIMA_PAGE_SIZE) {
		u32 pbe = LIMA_PBE(addr);
		u32 bte = LIMA_BTE(addr);
		u32 *bt;

		bt = lima_bo_kmap(vm->bts[pbe]);
		bt[bte] = 0;
	}
}

static int lima_vm_map_page_table(struct lima_vm *vm, dma_addr_t *dma,
				  u32 start, u32 end)
{
	u64 addr;
	int err, i = 0;

	for (addr = start; addr <= end; addr += LIMA_PAGE_SIZE) {
		u32 pbe = LIMA_PBE(addr);
		u32 bte = LIMA_BTE(addr);
		u32 *bt;

		if (vm->bts[pbe])
			bt = lima_bo_kmap(vm->bts[pbe]);
		else {
			struct lima_bo *bt_bo;
			dma_addr_t *pts;
			u32 *pd;
			int j;

			bt_bo = lima_bo_create(
				vm->dev, LIMA_PAGE_SIZE << LIMA_VM_NUM_PT_PER_BT_SHIFT,
				0, ttm_bo_type_kernel,
				NULL, vm->pd->tbo.resv);
			if (IS_ERR(bt_bo)) {
				err = PTR_ERR(bt_bo);
				goto err_out;
			}

			bt = lima_bo_kmap(bt_bo);
			if (IS_ERR(bt)) {
				lima_bo_unref(bt_bo);
				err = PTR_ERR(bt);
				goto err_out;
			}
			memset(bt, 0, LIMA_PAGE_SIZE << LIMA_VM_NUM_PT_PER_BT_SHIFT);

			vm->bts[pbe] = bt_bo;
			pd = lima_bo_kmap(vm->pd);
			pd += pbe << LIMA_VM_NUM_PT_PER_BT_SHIFT;
			pts = lima_bo_get_pages(bt_bo);
			for (j = 0; j < LIMA_VM_NUM_PT_PER_BT; j++)
				*pd++ = *pts++ | LIMA_VM_FLAG_PRESENT;
		}

		bt[bte] = dma[i++] | LIMA_VM_FLAGS_CACHE;
	}

	return 0;

err_out:
	if (addr != start)
		lima_vm_unmap_page_table(vm, start, addr - 1);
	return err;
}

static struct lima_bo_va *
lima_vm_bo_find(struct lima_vm *vm, struct lima_bo *bo)
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

int lima_vm_bo_map(struct lima_vm *vm, struct lima_bo *bo, u32 start)
{
	int err;
	struct lima_bo_va_mapping *it, *mapping;
	u32 end = start + bo->gem.size - 1;
	dma_addr_t *pages_dma = lima_bo_get_pages(bo);
	struct lima_bo_va *bo_va;

	it = lima_vm_it_iter_first(&vm->va, start, end);
	if (it) {
		dev_dbg(bo->gem.dev->dev, "lima vm map va overlap %x-%x %x-%x\n",
			start, end, it->start, it->last);
		return -EINVAL;
	}

	mapping = kmalloc(sizeof(*mapping), GFP_KERNEL);
	if (!mapping)
		return -ENOMEM;
	mapping->start = start;
	mapping->last = end;

	err = lima_vm_map_page_table(vm, pages_dma, start, end);
	if (err) {
		kfree(mapping);
		return err;
	}

	lima_vm_it_insert(mapping, &vm->va);

	bo_va = lima_vm_bo_find(vm, bo);
	list_add_tail(&mapping->list, &bo_va->mapping);

	return 0;
}

static void lima_vm_unmap(struct lima_vm *vm,
			  struct lima_bo_va_mapping *mapping)
{
	lima_vm_it_remove(mapping, &vm->va);

	lima_vm_unmap_page_table(vm, mapping->start, mapping->last);

	list_del(&mapping->list);
	kfree(mapping);
}

int lima_vm_bo_unmap(struct lima_vm *vm, struct lima_bo *bo, u32 start)
{
	struct lima_bo_va *bo_va;
	struct lima_bo_va_mapping *mapping;

	bo_va = lima_vm_bo_find(vm, bo);
	list_for_each_entry(mapping, &bo_va->mapping, list) {
		if (mapping->start == start) {
		        lima_vm_unmap(vm, mapping);
			break;
		}
	}

	return 0;
}

int lima_vm_bo_add(struct lima_vm *vm, struct lima_bo *bo)
{
	struct lima_bo_va *bo_va;

	bo_va = lima_vm_bo_find(vm, bo);
	if (bo_va) {
		bo_va->ref_count++;
		return 0;
	}

	bo_va = kmalloc(sizeof(*bo_va), GFP_KERNEL);
	if (!bo_va)
		return -ENOMEM;

	bo_va->vm = vm;
	bo_va->ref_count = 1;
	INIT_LIST_HEAD(&bo_va->mapping);
	list_add_tail(&bo_va->list, &bo->va);
	return 0;
}

/* wait only fence of resv from task using vm */
static int lima_vm_wait_resv(struct lima_vm *vm,
			     struct reservation_object *resv)
{
	unsigned nr_fences;
	struct dma_fence **fences;
	int i;
	long err;

	err = reservation_object_get_fences_rcu(resv, NULL, &nr_fences, &fences);
	if (err || !nr_fences)
		return err;

	for (i = 0; i < nr_fences; i++) {
		struct drm_sched_fence *sf = to_drm_sched_fence(fences[i]);
		if (sf && sf->owner == vm)
			err |= dma_fence_wait(fences[i], false);
		dma_fence_put(fences[i]);
	}

	kfree(fences);
	return err;
}

int lima_vm_bo_del(struct lima_vm *vm, struct lima_bo *bo)
{
	struct lima_bo_va *bo_va;
	struct lima_bo_va_mapping *mapping, *tmp;
	int err;

	bo_va = lima_vm_bo_find(vm, bo);
	if (--bo_va->ref_count > 0)
		return 0;

	/* wait bo idle before unmap it from vm in case user
	 * space application is terminated when bo is busy.
	 */
	err = lima_vm_wait_resv(vm, bo->tbo.resv);
	if (err)
		dev_err(vm->dev->dev, "bo del fail to wait (%d)\n", err);

	list_for_each_entry_safe(mapping, tmp, &bo_va->mapping, list) {
	        lima_vm_unmap(vm, mapping);
	}
	list_del(&bo_va->list);
	kfree(bo_va);
	return 0;
}

struct lima_vm *lima_vm_create(struct lima_device *dev)
{
	struct lima_vm *vm;
	void *pd;

	vm = kzalloc(sizeof(*vm), GFP_KERNEL);
	if (!vm)
		return NULL;

	vm->dev = dev;
	vm->va = RB_ROOT_CACHED;
	kref_init(&vm->refcount);

	vm->pd = lima_bo_create(dev, LIMA_PAGE_SIZE, 0,
				ttm_bo_type_kernel, NULL, NULL);
	if (IS_ERR(vm->pd))
		goto err_out0;

	pd = lima_bo_kmap(vm->pd);
	if (IS_ERR(pd))
		goto err_out1;
	memset(pd, 0, LIMA_PAGE_SIZE);

	if (dev->dlbu_cpu) {
		int err = lima_vm_map_page_table(
			vm, &dev->dlbu_dma, LIMA_VA_RESERVE_DLBU,
			LIMA_VA_RESERVE_DLBU + LIMA_PAGE_SIZE - 1);
		if (err)
			goto err_out1;
	}

	return vm;

err_out1:
	lima_bo_unref(vm->pd);
err_out0:
	kfree(vm);
	return NULL;
}

void lima_vm_release(struct kref *kref)
{
	struct lima_vm *vm = container_of(kref, struct lima_vm, refcount);
	struct lima_device *dev = vm->dev;
	int i;

	if (!RB_EMPTY_ROOT(&vm->va.rb_root)) {
		dev_err(dev->dev, "still active bo inside vm\n");
	}

	for (i = 0; i < LIMA_VM_NUM_BT; i++) {
		if (vm->bts[i])
			lima_bo_unref(vm->bts[i]);
	}

	if (vm->pd)
	        lima_bo_unref(vm->pd);

	kfree(vm);
}

void lima_vm_print(struct lima_vm *vm)
{
	int i, j, k;
	u32 *pd, *pt;

	/* to avoid the defined by not used warning */
	(void)&lima_vm_it_iter_next;

	pd = lima_bo_kmap(vm->pd);
	for (i = 0; i < LIMA_VM_NUM_BT; i++) {
		if (!vm->bts[i])
			continue;

		pt = lima_bo_kmap(vm->bts[i]);
		for (j = 0; j < LIMA_VM_NUM_PT_PER_BT; j++) {
			int idx = (i << LIMA_VM_NUM_PT_PER_BT_SHIFT) + j;
			printk(KERN_INFO "lima vm pd %03x:%08x\n", idx, pd[idx]);

			for (k = 0; k < LIMA_PAGE_ENT_NUM; k++) {
				u32 pte = *pt++;
				if (pte)
					printk(KERN_INFO "  pt %03x:%08x\n", k, pte);
			}
		}
	}
}
