// SPDX-License-Identifier: GPL-2.0 OR MIT
/* Copyright 2017-2018 Qiang Yu <yuq825@gmail.com> */

#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/device.h>

#include "lima_device.h"
#include "lima_mmu.h"
#include "lima_vm.h"
#include "lima_object.h"
#include "lima_regs.h"

#define mmu_write(reg, data) writel(data, ip->iomem + LIMA_MMU_##reg)
#define mmu_read(reg) readl(ip->iomem + LIMA_MMU_##reg)

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
	struct lima_ip *ip = data;
	struct lima_device *dev = ip->dev;
	u32 status = mmu_read(INT_STATUS);
	struct lima_sched_pipe *pipe;

	/* for shared irq case */
	if (!status)
		return IRQ_NONE;

	if (status & LIMA_MMU_INT_PAGE_FAULT) {
		u32 fault = mmu_read(PAGE_FAULT_ADDR);
		dev_err(dev->dev, "mmu page fault at 0x%x from bus id %d of type %s on %s\n",
			fault, LIMA_MMU_STATUS_BUS_ID(status),
			status & LIMA_MMU_STATUS_PAGE_FAULT_IS_WRITE ? "write" : "read",
			lima_ip_name(ip));
	}

	if (status & LIMA_MMU_INT_READ_BUS_ERROR) {
		dev_err(dev->dev, "mmu %s irq bus error\n", lima_ip_name(ip));
	}

	/* mask all interrupts before resume */
	mmu_write(INT_MASK, 0);
	mmu_write(INT_CLEAR, status);

	pipe = dev->pipe + (ip->id == lima_ip_gpmmu ? lima_pipe_gp : lima_pipe_pp);
	lima_sched_pipe_mmu_error(pipe);

	return IRQ_HANDLED;
}

int lima_mmu_init(struct lima_ip *ip)
{
	struct lima_device *dev = ip->dev;
	int err;

	if (ip->id == lima_ip_ppmmu_bcast)
		return 0;

	mmu_write(DTE_ADDR, 0xCAFEBABE);
	if (mmu_read(DTE_ADDR) != 0xCAFEB000) {
		dev_err(dev->dev, "mmu %s dte write test fail\n", lima_ip_name(ip));
		return -EIO;
	}

	err = lima_mmu_send_command(LIMA_MMU_COMMAND_HARD_RESET, mmu_read(DTE_ADDR) == 0);
	if (err)
		return err;

	err = devm_request_irq(dev->dev, ip->irq, lima_mmu_irq_handler,
			       IRQF_SHARED, lima_ip_name(ip), ip);
	if (err) {
		dev_err(dev->dev, "mmu %s fail to request irq\n", lima_ip_name(ip));
		return err;
	}

	mmu_write(INT_MASK, LIMA_MMU_INT_PAGE_FAULT | LIMA_MMU_INT_READ_BUS_ERROR);
	mmu_write(DTE_ADDR, *lima_bo_get_pages(dev->empty_vm->pd));
	return lima_mmu_send_command(LIMA_MMU_COMMAND_ENABLE_PAGING,
				     mmu_read(STATUS) & LIMA_MMU_STATUS_PAGING_ENABLED);
}

void lima_mmu_fini(struct lima_ip *ip)
{

}

void lima_mmu_switch_vm(struct lima_ip *ip, struct lima_vm *vm)
{
	struct lima_device *dev = ip->dev;

	lima_mmu_send_command(LIMA_MMU_COMMAND_ENABLE_STALL,
			      mmu_read(STATUS) & LIMA_MMU_STATUS_STALL_ACTIVE);

	if (vm)
		mmu_write(DTE_ADDR, *lima_bo_get_pages(vm->pd));

	/* flush the TLB */
	mmu_write(COMMAND, LIMA_MMU_COMMAND_ZAP_CACHE);

	lima_mmu_send_command(LIMA_MMU_COMMAND_DISABLE_STALL,
			      !(mmu_read(STATUS) & LIMA_MMU_STATUS_STALL_ACTIVE));
}

void lima_mmu_page_fault_resume(struct lima_ip *ip)
{
	struct lima_device *dev = ip->dev;
	u32 status = mmu_read(STATUS);

	if (status & LIMA_MMU_STATUS_PAGE_FAULT_ACTIVE) {
		dev_info(dev->dev, "mmu resume\n");

		mmu_write(INT_MASK, 0);
		mmu_write(DTE_ADDR, 0xCAFEBABE);
		lima_mmu_send_command(LIMA_MMU_COMMAND_HARD_RESET, mmu_read(DTE_ADDR) == 0);
	        mmu_write(INT_MASK, LIMA_MMU_INT_PAGE_FAULT | LIMA_MMU_INT_READ_BUS_ERROR);
		mmu_write(DTE_ADDR, *lima_bo_get_pages(dev->empty_vm->pd));
		lima_mmu_send_command(LIMA_MMU_COMMAND_ENABLE_PAGING,
				      mmu_read(STATUS) & LIMA_MMU_STATUS_PAGING_ENABLED);
	}
}
