#include <drm/drmP.h>
#include <linux/interval_tree.h>
#include <linux/dma-mapping.h>

#include "lima_vm.h"

#define LIMA_PAGE_SIZE    4096
#define LIMA_PAGE_ENT_NUM (LIMA_PAGE_SIZE / sizeof(u32))

#define LIMA_PDE(va) (va >> 22)
#define LIMA_PTE(va) ((va << 10) >> 22)

#define LIMA_VM_FLAG_PRESENT          (1 << 0)
#define LIMA_VM_FLAG_READ_PERMISSION  (1 << 1)
#define LIMA_VM_FLAG_WRITE_PERMISSION (1 << 2)
#define LIMA_VM_FLAG_OVERRIDE_CACHE   (1 << 3)
#define LIMA_VM_FLAG_WRITE_CACHEABLE  (1 << 4)
#define LIMA_VM_FLAG_WRITE_ALLOCATE   (1 << 5)
#define LIMA_VM_FLAG_WRITE_BUFFERABLE (1 << 6)
#define LIMA_VM_FLAG_READ_CACHEABLE   (1 << 7)
#define LIMA_VM_FLAG_READ_ALLOCATE    (1 << 8)
#define LIMA_VM_FLAG_MASK             0x1FF

#define LIMA_VM_FLAGS_CACHE (			 \
		LIMA_VM_FLAG_PRESENT |		 \
		LIMA_VM_FLAG_READ_PERMISSION |	 \
		LIMA_VM_FLAG_WRITE_PERMISSION |	 \
		LIMA_VM_FLAG_OVERRIDE_CACHE |	 \
		LIMA_VM_FLAG_WRITE_CACHEABLE |	 \
		LIMA_VM_FLAG_WRITE_BUFFERABLE |	 \
		LIMA_VM_FLAG_READ_CACHEABLE |	 \
		LIMA_VM_FLAG_READ_ALLOCATE )

#define LIMA_VM_FLAGS_UNCACHE (			\
		LIMA_VM_FLAG_PRESENT |		\
		LIMA_VM_FLAG_READ_PERMISSION |	\
		LIMA_VM_FLAG_WRITE_PERMISSION )


int lima_vm_map(struct lima_vm *vm, dma_addr_t dma, u32 va, u32 size)
{
	int err;
	struct interval_tree_node *it;
	u32 addr, faddr;

	mutex_lock(&vm->lock);

	it = interval_tree_iter_first(&vm->va, va, va + size - 1);
	if (it) {
		dev_err(vm->dev, "lima vm map va overlap %x-%x %lx-%lx\n",
			va, va + size -1, it->start, it->last);
		err = -EINVAL;
		goto err_out0;
	}

	it = kmalloc(sizeof(*it), GFP_KERNEL);
	if (!it) {
		err = -ENOMEM;
		goto err_out0;
	}

	it->start = va;
	it->last = va + size - 1;
	interval_tree_insert(it, &vm->va);

	for (addr = va; addr < va + size; addr += LIMA_PAGE_SIZE, dma += LIMA_PAGE_SIZE) {
		u32 pde = LIMA_PDE(addr);
		u32 pte = LIMA_PTE(addr);

		if (!vm->pts[pde].cpu) {
			vm->pts[pde].cpu = dma_alloc_coherent(
				vm->dev, LIMA_PAGE_SIZE,
				&vm->pts[pde].dma, GFP_KERNEL);
			if (!vm->pts[pde].cpu) {
				err = -ENOMEM;
				goto err_out1;
			}
			memset(vm->pts[pde].cpu, 0, LIMA_PAGE_SIZE);
		}

		vm->pd.cpu[pde] = vm->pts[pde].dma | LIMA_VM_FLAG_PRESENT;
		vm->pts[pde].cpu[pte] = dma | LIMA_VM_FLAGS_CACHE;
	}

	mutex_unlock(&vm->lock);
	return 0;

err_out1:
	for (faddr = va; faddr < addr; faddr += LIMA_PAGE_SIZE) {
		u32 pde = LIMA_PDE(faddr);
		u32 pte = LIMA_PTE(faddr);
		vm->pts[pde].cpu[pte] = 0;
	}
	interval_tree_remove(it, &vm->va);
	kfree(it);
err_out0:
	mutex_unlock(&vm->lock);
	return err;
}

int lima_vm_unmap(struct lima_vm *vm, u32 va, u32 size)
{
	int err = 0;
	struct interval_tree_node *it;
	u32 addr;

	mutex_lock(&vm->lock);

	it = interval_tree_iter_first(&vm->va, va, va + size - 1);
	if (it) {
		if (it->start != va || it->last != va + size - 1) {
			dev_err(vm->dev, "lima vm unmap va not match %x-%x %lx-%lx\n",
				va, va + size -1, it->start, it->last);
			err = -EINVAL;
			goto out;
		}
		interval_tree_remove(it, &vm->va);
		kfree(it);
	}
	else
		goto out;

	for (addr = va; addr < va + size; addr += LIMA_PAGE_SIZE) {
		u32 pde = LIMA_PDE(addr);
		u32 pte = LIMA_PTE(addr);
		vm->pts[pde].cpu[pte] = 0;
	}

out:
	mutex_unlock(&vm->lock);
	return err;
}

int lima_vm_init(struct lima_vm *vm, struct device *dev, bool empty)
{
	int err;

	vm->dev = dev;
	vm->va = RB_ROOT;
	mutex_init(&vm->lock);

	vm->pd.cpu = dma_alloc_coherent(dev, LIMA_PAGE_SIZE, &vm->pd.dma, GFP_KERNEL);
	if (!vm->pd.cpu)
		return -ENOMEM;
	memset(vm->pd.cpu, 0, LIMA_PAGE_SIZE);

	if (!empty) {
		vm->pts = drm_calloc_large(LIMA_PAGE_ENT_NUM, sizeof(vm->pts[0]));
		if (!vm->pts) {
			err = -ENOMEM;
			goto err_out;
		}
	}

	return 0;

err_out:
	dma_free_coherent(dev, LIMA_PAGE_SIZE, vm->pd.cpu, vm->pd.dma);
	return err;
}

void lima_vm_fini(struct lima_vm *vm)
{
	struct interval_tree_node *it, *tmp;

	if (!RB_EMPTY_ROOT(&vm->va)) {
		dev_err(vm->dev, "still active bo inside vm\n");
	}

	rbtree_postorder_for_each_entry_safe(it, tmp, &vm->va, rb) {
		interval_tree_remove(it, &vm->va);
		kfree(it);
	}

	if (vm->pts)
		drm_free_large(vm->pts);

	if (vm->pd.cpu)
		dma_free_coherent(vm->dev, LIMA_PAGE_SIZE, vm->pd.cpu, vm->pd.dma);
}
