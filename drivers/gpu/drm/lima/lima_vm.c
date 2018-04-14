#include <linux/slab.h>
#include <linux/dma-mapping.h>
#include <linux/interval_tree_generic.h>

#include "lima_device.h"
#include "lima_vm.h"

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

#define START(node) ((node)->start)
#define LAST(node) ((node)->last)

INTERVAL_TREE_DEFINE(struct lima_bo_va_mapping, rb, uint32_t, __subtree_last,
		     START, LAST, static, lima_vm_it)

#undef START
#undef LAST

static void lima_vm_unmap_page_table(struct lima_vm *vm, u32 start, u32 end)
{
	u32 addr;

	for (addr = start; addr < end; addr += LIMA_PAGE_SIZE) {
		u32 pde = LIMA_PDE(addr);
		u32 pte = LIMA_PTE(addr);

		vm->pts[pde].cpu[pte] = 0;
		vm->pts[pde].dma--;
		if (!(vm->pts[pde].dma & LIMA_PAGE_MASK)) {
			vm->pd.cpu[pde] = 0;
			vm->pd.dma--;

			dma_free_wc(vm->dev->dev, LIMA_PAGE_SIZE,
				    vm->pts[pde].cpu, vm->pts[pde].dma);
			vm->pts[pde].cpu = 0;
		}
	}
}

int lima_vm_map(struct lima_vm *vm, dma_addr_t *pages_dma,
		struct lima_bo_va_mapping *mapping)
{
	int err, i = 0;
	struct lima_bo_va_mapping *it;
	u32 addr;

	mutex_lock(&vm->lock);

	it = lima_vm_it_iter_first(&vm->va, mapping->start, mapping->last);
	if (it) {
		dev_err(vm->dev->dev, "lima vm map va overlap %x-%x %x-%x\n",
			mapping->start, mapping->last, it->start, it->last);
		err = -EINVAL;
		goto err_out0;
	}

	lima_vm_it_insert(mapping, &vm->va);

	for (addr = mapping->start; addr <= mapping->last; addr += LIMA_PAGE_SIZE) {
		u32 pde = LIMA_PDE(addr);
		u32 pte = LIMA_PTE(addr);

		if (!vm->pts[pde].cpu) {
			vm->pts[pde].cpu =
				dma_alloc_wc(vm->dev->dev, LIMA_PAGE_SIZE,
					     &vm->pts[pde].dma, GFP_KERNEL);
			if (!vm->pts[pde].cpu) {
				err = -ENOMEM;
				goto err_out1;
			}
			memset(vm->pts[pde].cpu, 0, LIMA_PAGE_SIZE);
			vm->pd.cpu[pde] = vm->pts[pde].dma | LIMA_VM_FLAG_PRESENT;
			vm->pd.dma++;
		}

		/* dma address should be 4K aligned, so use the lower 12 bit
		 * as a reference count, 12bit is enough for 1024 max count
		 */
		vm->pts[pde].dma++;
		vm->pts[pde].cpu[pte] = pages_dma[i++] | LIMA_VM_FLAGS_CACHE;
	}

	mutex_unlock(&vm->lock);
	return 0;

err_out1:
	lima_vm_unmap_page_table(vm, mapping->start, addr);
	lima_vm_it_remove(mapping, &vm->va);
err_out0:
	mutex_unlock(&vm->lock);
	return err;
}

int lima_vm_unmap(struct lima_vm *vm, struct lima_bo_va_mapping *mapping)
{
	mutex_lock(&vm->lock);

	lima_vm_it_remove(mapping, &vm->va);

	lima_vm_unmap_page_table(vm, mapping->start, mapping->last + 1);

	mutex_unlock(&vm->lock);

	/* TODO: zap MMU using this vm in case buggy user app
	 * free bo during GP/PP running which may corrupt kernel
	 * reusing this memory. */

	return 0;
}

struct lima_vm *lima_vm_create(struct lima_device *dev)
{
	struct lima_vm *vm;

	vm = kvzalloc(sizeof(*vm), GFP_KERNEL);
	if (!vm)
		return NULL;

	vm->dev = dev;
	vm->va = RB_ROOT_CACHED;
	mutex_init(&vm->lock);
	kref_init(&vm->refcount);

	vm->pd.cpu = dma_alloc_wc(dev->dev, LIMA_PAGE_SIZE, &vm->pd.dma, GFP_KERNEL);
	if (!vm->pd.cpu)
		goto err_out;
	memset(vm->pd.cpu, 0, LIMA_PAGE_SIZE);

	return vm;

err_out:
	kvfree(vm);
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

	for (i = 0; (vm->pd.dma & LIMA_PAGE_MASK) && i < LIMA_PAGE_ENT_NUM; i++) {
		if (vm->pts[i].cpu) {
			dma_free_wc(vm->dev->dev, LIMA_PAGE_SIZE,
				    vm->pts[i].cpu, vm->pts[i].dma & ~LIMA_PAGE_MASK);
			vm->pd.dma--;
		}
	}

	if (vm->pd.cpu)
		dma_free_wc(vm->dev->dev, LIMA_PAGE_SIZE, vm->pd.cpu,
			    vm->pd.dma & ~LIMA_PAGE_MASK);

	kvfree(vm);
}

void lima_vm_print(struct lima_vm *vm)
{
	int i, j;

	/* to avoid the defined by not used warning */
	(void)&lima_vm_it_iter_next;

	if (!vm->pd.cpu)
		return;

	for (i = 0; i < LIMA_PAGE_ENT_NUM; i++) {
		if (vm->pd.cpu[i]) {
			printk(KERN_INFO "lima vm pd %03x:%08x\n", i, vm->pd.cpu[i]);
			if ((vm->pd.cpu[i] & ~LIMA_VM_FLAG_MASK) != vm->pts[i].dma)
				printk(KERN_INFO "pd %x not match pt %x\n",
				       i, (u32)vm->pts[i].dma);
			for (j = 0; j < LIMA_PAGE_ENT_NUM; j++) {
				if (vm->pts[i].cpu[j])
					printk(KERN_INFO "  pt %03x:%08x\n",
					       j, vm->pts[i].cpu[j]);
			}
		}
	}
}
