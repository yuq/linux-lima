#include <linux/interrupt.h>

#include "lima.h"

#define LIMA_MMU_DTE_ADDR		  0x0000
#define LIMA_MMU_STATUS			  0x0004
#define   LIMA_MMU_STATUS_PAGING_ENABLED      (1 << 0)
#define   LIMA_MMU_STATUS_PAGE_FAULT_ACTIVE   (1 << 1)
#define   LIMA_MMU_STATUS_STALL_ACTIVE        (1 << 2)
#define   LIMA_MMU_STATUS_IDLE                (1 << 3)
#define   LIMA_MMU_STATUS_REPLAY_BUFFER_EMPTY (1 << 4)
#define   LIMA_MMU_STATUS_PAGE_FAULT_IS_WRITE (1 << 5)
#define   LIMA_MMU_STATUS_BUS_ID(x)           ((x >> 6) & 0x1F)
#define LIMA_MMU_COMMAND		  0x0008
#define   LIMA_MMU_COMMAND_ENABLE_PAGING    0x00
#define   LIMA_MMU_COMMAND_DISABLE_PAGING   0x01
#define   LIMA_MMU_COMMAND_ENABLE_STALL     0x02
#define   LIMA_MMU_COMMAND_DISABLE_STALL    0x03
#define   LIMA_MMU_COMMAND_ZAP_CACHE        0x04
#define   LIMA_MMU_COMMAND_PAGE_FAULT_DONE  0x05
#define   LIMA_MMU_COMMAND_HARD_RESET       0x06
#define LIMA_MMU_PAGE_FAULT_ADDR          0x000C
#define LIMA_MMU_ZAP_ONE_LINE	          0x0010
#define LIMA_MMU_INT_RAWSTAT	          0x0014
#define LIMA_MMU_INT_CLEAR		  0x0018
#define LIMA_MMU_INT_MASK		  0x001C
#define   LIMA_MMU_INT_PAGE_FAULT           0x01
#define   LIMA_MMU_INT_READ_BUS_ERROR       0x02
#define LIMA_MMU_INT_STATUS		  0x0020

#define mmu_write(reg, data) writel(data, mmu->ip.iomem + LIMA_MMU_##reg)
#define mmu_read(reg) readl(mmu->ip.iomem + LIMA_MMU_##reg)

#define lima_mmu_send_command(command, condition)	     \
({							     \
	int __timeout, __ret = 0;			     \
							     \
	mmu_write(COMMAND, command);			     \
	for (__timeout = 1000; __timeout > 0; __timeout--) { \
		if (condition)				     \
			break;				     \
	}						     \
	if (!__timeout)	{				     \
		dev_err(dev->dev, "mmu command %x timeout\n", command); \
		__ret = -ETIMEDOUT;			     \
	}						     \
	__ret;						     \
})

static irqreturn_t lima_mmu_irq_handler(int irq, void *data)
{
	struct lima_mmu *mmu = data;
	struct lima_device *dev = mmu->ip.dev;
	u32 status = mmu_read(INT_STATUS);

	if (status & LIMA_MMU_INT_PAGE_FAULT) {
		u32 fault = mmu_read(PAGE_FAULT_ADDR);
		dev_info(dev->dev, "mmu page fault at 0x%x from bus id %d of type %s on %s\n",
			 fault, LIMA_MMU_STATUS_BUS_ID(status),
			 status & LIMA_MMU_STATUS_PAGE_FAULT_IS_WRITE ? "write" : "read",
			 mmu->ip.name);
		//lima_vm_print(mmu->vm);
	}

	if (status & LIMA_MMU_INT_READ_BUS_ERROR) {
		dev_info(dev->dev, "mmu %s irq bus error\n", mmu->ip.name);
	}

	lima_sched_pipe_task_done(mmu->pipe, true);

	mmu_write(INT_CLEAR, status);
	return IRQ_NONE;
}

int lima_mmu_init(struct lima_mmu *mmu)
{
	struct lima_device *dev = mmu->ip.dev;
	int err;

	mmu_write(DTE_ADDR, 0xCAFEBABE);
	if (mmu_read(DTE_ADDR) != 0xCAFEB000) {
		dev_err(dev->dev, "mmu %s dte write test fail\n", mmu->ip.name);
		return -EIO;
	}

	err = lima_mmu_send_command(LIMA_MMU_COMMAND_HARD_RESET, mmu_read(DTE_ADDR) == 0);
	if (err)
		return err;

	err = devm_request_irq(dev->dev, mmu->ip.irq, lima_mmu_irq_handler, 0,
			       mmu->ip.name, mmu);
	if (err) {
		dev_err(dev->dev, "mmu %s fail to request irq\n", mmu->ip.name);
		return err;
	}

	mmu_write(INT_MASK, LIMA_MMU_INT_PAGE_FAULT | LIMA_MMU_INT_READ_BUS_ERROR);
	mmu_write(DTE_ADDR, dev->empty_vm->pd.dma);
	err = lima_mmu_send_command(LIMA_MMU_COMMAND_ENABLE_PAGING,
				    mmu_read(STATUS) & LIMA_MMU_STATUS_PAGING_ENABLED);
	if (err)
		return err;

	mmu->vm = dev->empty_vm;
	spin_lock_init(&mmu->lock);
	mmu->zap_all = false;

	return 0;
}

void lima_mmu_fini(struct lima_mmu *mmu)
{

}

void lima_mmu_switch_vm(struct lima_mmu *mmu, struct lima_vm *vm, bool reset)
{
	struct lima_device *dev = mmu->ip.dev;

	spin_lock(&mmu->lock);

	if (mmu->vm == vm) {
		if (reset)
			vm = dev->empty_vm;
		else if (!mmu->zap_all)
			goto out;
	}

	lima_mmu_send_command(LIMA_MMU_COMMAND_ENABLE_STALL,
			      mmu_read(STATUS) & LIMA_MMU_STATUS_STALL_ACTIVE);

	if (mmu->vm != vm)
		mmu_write(DTE_ADDR, vm->pd.dma);

	/* flush the TLB */
	mmu_write(COMMAND, LIMA_MMU_COMMAND_ZAP_CACHE);

	lima_mmu_send_command(LIMA_MMU_COMMAND_DISABLE_STALL,
			      !(mmu_read(STATUS) & LIMA_MMU_STATUS_STALL_ACTIVE));

	mmu->vm = vm;
	mmu->zap_all = false;

out:
	spin_unlock(&mmu->lock);
}

void lima_mmu_zap_vm(struct lima_mmu *mmu, struct lima_vm *vm, u32 va, u32 size)
{
	/* TODO: use LIMA_MMU_ZAP_ONE_LINE to just zap a PDE
         * needs to investigate:
         * 1. if LIMA_MMU_ZAP_ONE_LINE need stall mmu, otherwise we can zap it here
         * 2. how many PDE when LIMA_MMU_ZAP_ONE_LINE is better than zap all,
         *    otherwise we can use zap all when exceeds that limit
         */

	spin_lock(&mmu->lock);
	if (mmu->vm == vm)
	        mmu->zap_all = true;
	spin_unlock(&mmu->lock);
}

void lima_mmu_page_fault_resume(struct lima_mmu *mmu)
{
	struct lima_device *dev = mmu->ip.dev;
	u32 status = mmu_read(STATUS);

	if (status & LIMA_MMU_STATUS_PAGE_FAULT_ACTIVE) {
		dev_info(dev->dev, "mmu resume\n");

	        spin_lock(&mmu->lock);
		mmu->vm = dev->empty_vm;
		spin_unlock(&mmu->lock);

		mmu_write(INT_MASK, 0);
		mmu_write(DTE_ADDR, 0xCAFEBABE);
		lima_mmu_send_command(LIMA_MMU_COMMAND_HARD_RESET, mmu_read(DTE_ADDR) == 0);
	        mmu_write(INT_MASK, LIMA_MMU_INT_PAGE_FAULT | LIMA_MMU_INT_READ_BUS_ERROR);
		mmu_write(DTE_ADDR, dev->empty_vm->pd.dma);
		lima_mmu_send_command(LIMA_MMU_COMMAND_ENABLE_PAGING,
				      mmu_read(STATUS) & LIMA_MMU_STATUS_PAGING_ENABLED);
	}
}
