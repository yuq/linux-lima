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
#define   LIMA_MMU_STATUS_STALL_NOT_ACTIVE    (1 << 31)
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

#define LIMA_MMU_FLAG_PRESENT          (1 << 0)
#define LIMA_MMU_FLAG_READ_PERMISSION  (1 << 1)
#define LIMA_MMU_FLAG_WRITE_PERMISSION (1 << 2)
#define LIMA_MMU_FLAG_OVERRIDE_CACHE   (1 << 3)
#define LIMA_MMU_FLAG_WRITE_CACHEABLE  (1 << 4)
#define LIMA_MMU_FLAG_WRITE_ALLOCATE   (1 << 5)
#define LIMA_MMU_FLAG_WRITE_BUFFERABLE (1 << 6)
#define LIMA_MMU_FLAG_READ_CACHEABLE   (1 << 7)
#define LIMA_MMU_FLAG_READ_ALLOCATE    (1 << 8)
#define LIMA_MMU_FLAG_MASK             0x1FF

#define LIMA_MMU_FLAGS_FORCE_GP_READ_ALLOCATE (	 \
		LIMA_MMU_FLAG_PRESENT |		 \
		LIMA_MMU_FLAG_READ_PERMISSION |  \
		LIMA_MMU_FLAG_WRITE_PERMISSION | \
		LIMA_MMU_FLAG_OVERRIDE_CACHE |	 \
		LIMA_MMU_FLAG_WRITE_CACHEABLE |  \
		LIMA_MMU_FLAG_WRITE_BUFFERABLE | \
		LIMA_MMU_FLAG_READ_CACHEABLE |	 \
		LIMA_MMU_FLAG_READ_ALLOCATE )

#define LIMA_MMU_FLAGS_DEFAULT (			   \
		LIMA_MMU_FLAG_PRESENT |			   \
		LIMA_MMU_FLAG_READ_PERMISSION |		   \
		LIMA_MMU_FLAG_WRITE_PERMISSION )

#define mmu_write(reg, data) writel(data, mmu->ip.iomem + LIMA_MMU_##reg)
#define mmu_read(reg) readl(mmu->ip.iomem + LIMA_MMU_##reg)


static irqreturn_t lima_mmu_irq_handler(int irq, void *data)
{
	struct lima_mmu *mmu = data;
	struct lima_device *dev = mmu->ip.dev;

	dev_info(dev->dev, "mmu %s irq\n", mmu->ip.name);
	return IRQ_NONE;
}

int lima_mmu_init(struct lima_mmu *mmu)
{
	struct lima_device *dev = mmu->ip.dev;
	int err, timeout;

	mmu_write(DTE_ADDR, 0xCAFEBABE);
	if (mmu_read(DTE_ADDR) != 0xCAFEB000) {
		dev_err(dev->dev, "mmu %s dte write test fail\n", mmu->ip.name);
		return -EIO;
	}

	mmu_write(COMMAND, LIMA_MMU_COMMAND_HARD_RESET);
	for (timeout = 1000; timeout > 0; timeout--) {
		if (mmu_read(DTE_ADDR) == 0)
			break;
	}
	if (!timeout) {
		dev_err(dev->dev, "mmu %s reset timeout\n", mmu->ip.name);
		return -ETIMEDOUT;
	}

	err = devm_request_irq(dev->dev, mmu->ip.irq, lima_mmu_irq_handler, 0,
			       mmu->ip.name, mmu);
	if (err) {
		dev_err(dev->dev, "mmu %s fail to request irq\n", mmu->ip.name);
		return err;
	}

	mmu_write(INT_MASK, LIMA_MMU_INT_PAGE_FAULT | LIMA_MMU_INT_READ_BUS_ERROR);
	mmu_write(DTE_ADDR, dev->empty_mmu_pda_dma);
	mmu_write(COMMAND, LIMA_MMU_COMMAND_ENABLE_PAGING);
	for (timeout = 1000; timeout > 0; timeout--) {
		if (mmu_read(STATUS) & LIMA_MMU_STATUS_PAGING_ENABLED)
			break;
	}
	if (!timeout) {
		dev_err(dev->dev, "mmu %s enable paging time out\n", mmu->ip.name);
		return -ETIMEDOUT;
	}

	return 0;
}

void lima_mmu_fini(struct lima_mmu *mmu)
{

}
