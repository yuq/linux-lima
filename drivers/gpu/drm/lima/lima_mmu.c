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

#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/device.h>

#include "lima_device.h"
#include "lima_mmu.h"
#include "lima_vm.h"

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
	mmu_write(DTE_ADDR, dev->empty_vm->pd.dma);
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
		mmu_write(DTE_ADDR, vm->pd.dma);

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
		mmu_write(DTE_ADDR, dev->empty_vm->pd.dma);
		lima_mmu_send_command(LIMA_MMU_COMMAND_ENABLE_PAGING,
				      mmu_read(STATUS) & LIMA_MMU_STATUS_PAGING_ENABLED);
	}
}
