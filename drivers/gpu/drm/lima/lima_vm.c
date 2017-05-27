#include <linux/slab.h>
#include <linux/interval_tree.h>
#include <linux/dma-mapping.h>

#include "lima.h"

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
		dev_err(vm->dev->dev, "lima vm map va overlap %x-%x %lx-%lx\n",
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
				vm->dev->dev, LIMA_PAGE_SIZE,
				&vm->pts[pde].dma, GFP_KERNEL);
			if (!vm->pts[pde].cpu) {
				err = -ENOMEM;
				goto err_out1;
			}
			memset(vm->pts[pde].cpu, 0, LIMA_PAGE_SIZE);
			vm->pd.cpu[pde] = vm->pts[pde].dma | LIMA_VM_FLAG_PRESENT;
		}

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
	int err, i, j;
	struct interval_tree_node *it;
	u32 addr;
	struct lima_device *dev = vm->dev;

	mutex_lock(&vm->lock);

	it = interval_tree_iter_first(&vm->va, va, va + size - 1);
	if (it) {
		if (it->start != va || it->last != va + size - 1) {
			dev_err(dev->dev, "lima vm unmap va not match %x-%x %lx-%lx\n",
				va, va + size -1, it->start, it->last);
			err = -EINVAL;
			goto err_out;
		}
		interval_tree_remove(it, &vm->va);
		kfree(it);
	}
	else
		goto err_out;

	for (addr = va; addr < va + size; addr += LIMA_PAGE_SIZE) {
		u32 pde = LIMA_PDE(addr);
		u32 pte = LIMA_PTE(addr);
		vm->pts[pde].cpu[pte] = 0;
	}

	mutex_unlock(&vm->lock);

	for (i = 0; i < ARRAY_SIZE(dev->pipe); i++) {
		for (j = 0; j < dev->pipe[i]->num_mmu; j++)
			lima_mmu_zap_vm(dev->pipe[i]->mmu[j], vm, va, size);
	}

	return 0;

err_out:
	mutex_unlock(&vm->lock);
	return err;
}

struct lima_vm *lima_vm_create(struct lima_device *dev)
{
	struct lima_vm *vm;

	vm = drm_calloc_large(1, sizeof(*vm));
	if (!vm)
		return NULL;

	vm->dev = dev;
	vm->va = RB_ROOT;
	mutex_init(&vm->lock);
	kref_init(&vm->refcount);

	vm->pd.cpu = dma_alloc_coherent(dev->dev, LIMA_PAGE_SIZE, &vm->pd.dma, GFP_KERNEL);
	if (!vm->pd.cpu)
		goto err_out;
	memset(vm->pd.cpu, 0, LIMA_PAGE_SIZE);

	return vm;

err_out:
	drm_free_large(vm);
	return NULL;
}

void lima_vm_release(struct kref *kref)
{
	struct lima_vm *vm = container_of(kref, struct lima_vm, refcount);
	struct interval_tree_node *it, *tmp;
	struct lima_device *dev = vm->dev;
	int i, j;

	/* switch mmu vm to empty vm if this vm is used by it */
	if (vm != dev->empty_vm) {
		for (i = 0; i < ARRAY_SIZE(dev->pipe); i++) {
			for (j = 0; j < dev->pipe[i]->num_mmu; j++)
				lima_mmu_switch_vm(dev->pipe[i]->mmu[j], vm, true);
		}
	}

	if (!RB_EMPTY_ROOT(&vm->va)) {
		dev_err(vm->dev->dev, "still active bo inside vm\n");
	}

	rbtree_postorder_for_each_entry_safe(it, tmp, &vm->va, rb) {
		interval_tree_remove(it, &vm->va);
		kfree(it);
	}

	if (vm->pd.cpu)
		dma_free_coherent(vm->dev->dev, LIMA_PAGE_SIZE, vm->pd.cpu, vm->pd.dma);

	drm_free_large(vm);
}

void lima_vm_print(struct lima_vm *vm)
{
	int i, j;

	if (!vm->pd.cpu)
		return;

	for (i = 0; i < LIMA_PAGE_ENT_NUM; i++) {
		if (vm->pd.cpu[i]) {
			printk(KERN_INFO "lima vm pd %03x:%08x\n", i, vm->pd.cpu[i]);
			if ((vm->pd.cpu[i] & ~LIMA_VM_FLAG_MASK) != vm->pts[i].dma)
				printk(KERN_INFO "pd %x not match pt %x\n", i, vm->pts[i].dma);
			for (j = 0; j < LIMA_PAGE_ENT_NUM; j++) {
				if (vm->pts[i].cpu[j])
					printk(KERN_INFO "  pt %03x:%08x\n", j, vm->pts[i].cpu[j]);
			}
		}
	}
}
