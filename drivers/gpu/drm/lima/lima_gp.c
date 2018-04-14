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
#include <linux/slab.h>

#include <drm/lima_drm.h>

#include "lima_device.h"
#include "lima_gp.h"

#define LIMA_GP_VSCL_START_ADDR                0x00
#define LIMA_GP_VSCL_END_ADDR                  0x04
#define LIMA_GP_PLBUCL_START_ADDR              0x08
#define LIMA_GP_PLBUCL_END_ADDR                0x0c
#define LIMA_GP_PLBU_ALLOC_START_ADDR          0x10
#define LIMA_GP_PLBU_ALLOC_END_ADDR            0x14
#define LIMA_GP_CMD                            0x20
#define   LIMA_GP_CMD_START_VS                 (1 << 0)
#define   LIMA_GP_CMD_START_PLBU               (1 << 1)
#define   LIMA_GP_CMD_UPDATE_PLBU_ALLOC        (1 << 4)
#define   LIMA_GP_CMD_RESET                    (1 << 5)
#define   LIMA_GP_CMD_FORCE_HANG               (1 << 6)
#define   LIMA_GP_CMD_STOP_BUS                 (1 << 9)
#define   LIMA_GP_CMD_SOFT_RESET               (1 << 10)
#define LIMA_GP_INT_RAWSTAT                    0x24
#define LIMA_GP_INT_CLEAR                      0x28
#define LIMA_GP_INT_MASK                       0x2C
#define LIMA_GP_INT_STAT                       0x30
#define   LIMA_GP_IRQ_VS_END_CMD_LST           (1 << 0)
#define   LIMA_GP_IRQ_PLBU_END_CMD_LST         (1 << 1)
#define   LIMA_GP_IRQ_PLBU_OUT_OF_MEM          (1 << 2)
#define   LIMA_GP_IRQ_VS_SEM_IRQ               (1 << 3)
#define   LIMA_GP_IRQ_PLBU_SEM_IRQ             (1 << 4)
#define   LIMA_GP_IRQ_HANG                     (1 << 5)
#define   LIMA_GP_IRQ_FORCE_HANG               (1 << 6)
#define   LIMA_GP_IRQ_PERF_CNT_0_LIMIT         (1 << 7)
#define   LIMA_GP_IRQ_PERF_CNT_1_LIMIT         (1 << 8)
#define   LIMA_GP_IRQ_WRITE_BOUND_ERR          (1 << 9)
#define   LIMA_GP_IRQ_SYNC_ERROR               (1 << 10)
#define   LIMA_GP_IRQ_AXI_BUS_ERROR            (1 << 11)
#define   LIMA_GP_IRQ_AXI_BUS_STOPPED          (1 << 12)
#define   LIMA_GP_IRQ_VS_INVALID_CMD           (1 << 13)
#define   LIMA_GP_IRQ_PLB_INVALID_CMD          (1 << 14)
#define   LIMA_GP_IRQ_RESET_COMPLETED          (1 << 19)
#define   LIMA_GP_IRQ_SEMAPHORE_UNDERFLOW      (1 << 20)
#define   LIMA_GP_IRQ_SEMAPHORE_OVERFLOW       (1 << 21)
#define   LIMA_GP_IRQ_PTR_ARRAY_OUT_OF_BOUNDS  (1 << 22)
#define LIMA_GP_WRITE_BOUND_LOW                0x34
#define LIMA_GP_PERF_CNT_0_ENABLE              0x3C
#define LIMA_GP_PERF_CNT_1_ENABLE              0x40
#define LIMA_GP_PERF_CNT_0_SRC                 0x44
#define LIMA_GP_PERF_CNT_1_SRC                 0x48
#define LIMA_GP_PERF_CNT_0_VALUE               0x4C
#define LIMA_GP_PERF_CNT_1_VALUE               0x50
#define LIMA_GP_PERF_CNT_0_LIMIT               0x54
#define LIMA_GP_STATUS                         0x68
#define   LIMA_GP_STATUS_VS_ACTIVE             (1 << 1)
#define   LIMA_GP_STATUS_BUS_STOPPED	       (1 << 2)
#define	  LIMA_GP_STATUS_PLBU_ACTIVE	       (1 << 3)
#define	  LIMA_GP_STATUS_BUS_ERROR	       (1 << 6)
#define	  LIMA_GP_STATUS_WRITE_BOUND_ERR       (1 << 8)
#define LIMA_GP_VERSION                        0x6C
#define LIMA_GP_VSCL_START_ADDR_READ           0x80
#define LIMA_GP_PLBCL_START_ADDR_READ          0x84
#define LIMA_GP_CONTR_AXI_BUS_ERROR_STAT       0x94

#define LIMA_GP_IRQ_MASK_ALL		   \
	(				   \
	 LIMA_GP_IRQ_VS_END_CMD_LST      | \
	 LIMA_GP_IRQ_PLBU_END_CMD_LST    | \
	 LIMA_GP_IRQ_PLBU_OUT_OF_MEM     | \
	 LIMA_GP_IRQ_VS_SEM_IRQ          | \
	 LIMA_GP_IRQ_PLBU_SEM_IRQ        | \
	 LIMA_GP_IRQ_HANG                | \
	 LIMA_GP_IRQ_FORCE_HANG          | \
	 LIMA_GP_IRQ_PERF_CNT_0_LIMIT    | \
	 LIMA_GP_IRQ_PERF_CNT_1_LIMIT    | \
	 LIMA_GP_IRQ_WRITE_BOUND_ERR     | \
	 LIMA_GP_IRQ_SYNC_ERROR          | \
	 LIMA_GP_IRQ_AXI_BUS_ERROR       | \
	 LIMA_GP_IRQ_AXI_BUS_STOPPED     | \
	 LIMA_GP_IRQ_VS_INVALID_CMD      | \
	 LIMA_GP_IRQ_PLB_INVALID_CMD     | \
	 LIMA_GP_IRQ_RESET_COMPLETED     | \
	 LIMA_GP_IRQ_SEMAPHORE_UNDERFLOW | \
	 LIMA_GP_IRQ_SEMAPHORE_OVERFLOW  | \
	 LIMA_GP_IRQ_PTR_ARRAY_OUT_OF_BOUNDS)

#define LIMA_GP_IRQ_MASK_ERROR             \
	(                                  \
	 LIMA_GP_IRQ_PLBU_OUT_OF_MEM     | \
	 LIMA_GP_IRQ_FORCE_HANG          | \
	 LIMA_GP_IRQ_WRITE_BOUND_ERR     | \
	 LIMA_GP_IRQ_SYNC_ERROR          | \
	 LIMA_GP_IRQ_AXI_BUS_ERROR       | \
	 LIMA_GP_IRQ_VS_INVALID_CMD      | \
	 LIMA_GP_IRQ_PLB_INVALID_CMD     | \
	 LIMA_GP_IRQ_SEMAPHORE_UNDERFLOW | \
	 LIMA_GP_IRQ_SEMAPHORE_OVERFLOW  | \
	 LIMA_GP_IRQ_PTR_ARRAY_OUT_OF_BOUNDS)

#define LIMA_GP_IRQ_MASK_USED		   \
	(				   \
	 LIMA_GP_IRQ_VS_END_CMD_LST      | \
	 LIMA_GP_IRQ_PLBU_END_CMD_LST    | \
	 LIMA_GP_IRQ_MASK_ERROR)


#define gp_write(reg, data) writel(data, ip->iomem + LIMA_GP_##reg)
#define gp_read(reg) readl(ip->iomem + LIMA_GP_##reg)

static irqreturn_t lima_gp_irq_handler(int irq, void *data)
{
	struct lima_ip *ip = data;
	struct lima_device *dev = ip->dev;
	struct lima_sched_pipe *pipe = dev->pipe + lima_pipe_gp;
	u32 state = gp_read(INT_STAT);
	u32 status = gp_read(STATUS);
	bool done = false;

	/* for shared irq case */
	if (!state)
		return IRQ_NONE;

	if (state & LIMA_GP_IRQ_MASK_ERROR) {
		dev_err(dev->dev, "gp error irq state=%x status=%x\n",
			state, status);

		/* mask all interrupts before hard reset */
		gp_write(INT_MASK, 0);

		pipe->error = true;
		done = true;
	}
	else {
		bool valid = state & (LIMA_GP_IRQ_VS_END_CMD_LST |
				      LIMA_GP_IRQ_PLBU_END_CMD_LST);
		bool active = status & (LIMA_GP_STATUS_VS_ACTIVE |
					LIMA_GP_STATUS_PLBU_ACTIVE);
		done = valid && !active;
	}

	gp_write(INT_CLEAR, state);

	if (done)
		lima_sched_pipe_task_done(pipe);

	return IRQ_HANDLED;
}

static void lima_gp_soft_reset_async(struct lima_ip *ip)
{
	if (ip->data.async_reset)
		return;

	gp_write(INT_MASK, 0);
	gp_write(INT_CLEAR, LIMA_GP_IRQ_RESET_COMPLETED);
	gp_write(CMD, LIMA_GP_CMD_SOFT_RESET);
	ip->data.async_reset = true;
}

static int lima_gp_soft_reset_async_wait(struct lima_ip *ip)
{
	struct lima_device *dev = ip->dev;
	int timeout;

	if (!ip->data.async_reset)
		return 0;

	for (timeout = 1000; timeout > 0; timeout--) {
		if (gp_read(INT_RAWSTAT) & LIMA_GP_IRQ_RESET_COMPLETED)
			break;
	}
	if (!timeout) {
		dev_err(dev->dev, "gp soft reset time out\n");
		return -ETIMEDOUT;
	}

	gp_write(INT_CLEAR, LIMA_GP_IRQ_MASK_ALL);
	gp_write(INT_MASK, LIMA_GP_IRQ_MASK_USED);

	ip->data.async_reset = false;
	return 0;
}

static int lima_gp_task_validate(struct lima_sched_pipe *pipe,
				 struct lima_sched_task *task)
{
	struct drm_lima_m400_gp_frame *f = task->frame;
	(void)pipe;

	if (f->vs_cmd_start > f->vs_cmd_end ||
	    f->plbu_cmd_start > f->plbu_cmd_end ||
	    f->tile_heap_start > f->tile_heap_end)
		return -EINVAL;

	if (f->vs_cmd_start == f->vs_cmd_end &&
	    f->plbu_cmd_start == f->plbu_cmd_end)
		return -EINVAL;

	return 0;
}

static void lima_gp_task_run(struct lima_sched_pipe *pipe,
			     struct lima_sched_task *task)
{
	struct lima_ip *ip = pipe->processor[0];
	struct drm_lima_m400_gp_frame *frame = task->frame;
	u32 cmd = 0;

	if (frame->vs_cmd_start != frame->vs_cmd_end)
		cmd |= LIMA_GP_CMD_START_VS;
	if (frame->plbu_cmd_start != frame->plbu_cmd_end)
		cmd |= LIMA_GP_CMD_START_PLBU;

	/* before any hw ops, wait last success task async soft reset */
	lima_gp_soft_reset_async_wait(ip);

	gp_write(VSCL_START_ADDR, frame->vs_cmd_start);
	gp_write(VSCL_END_ADDR, frame->vs_cmd_end);
	gp_write(PLBUCL_START_ADDR, frame->plbu_cmd_start);
	gp_write(PLBUCL_END_ADDR, frame->plbu_cmd_end);
	gp_write(PLBU_ALLOC_START_ADDR, frame->tile_heap_start);
	gp_write(PLBU_ALLOC_END_ADDR, frame->tile_heap_end);

	gp_write(CMD, LIMA_GP_CMD_UPDATE_PLBU_ALLOC);
	gp_write(CMD, cmd);
}

static int lima_gp_hard_reset(struct lima_ip *ip)
{
	struct lima_device *dev = ip->dev;
	int timeout;

	gp_write(PERF_CNT_0_LIMIT, 0xC0FFE000);
	gp_write(INT_MASK, 0);
	gp_write(CMD, LIMA_GP_CMD_RESET);
	for (timeout = 1000; timeout > 0; timeout--) {
		gp_write(PERF_CNT_0_LIMIT, 0xC01A0000);
		if (gp_read(PERF_CNT_0_LIMIT) == 0xC01A0000)
			break;
	}
	if (!timeout) {
		dev_err(dev->dev, "gp hard reset timeout\n");
		return -ETIMEDOUT;
	}

	gp_write(PERF_CNT_0_LIMIT, 0);
	gp_write(INT_CLEAR, LIMA_GP_IRQ_MASK_ALL);
	gp_write(INT_MASK, LIMA_GP_IRQ_MASK_USED);
	return 0;
}

static void lima_gp_task_fini(struct lima_sched_pipe *pipe)
{
	lima_gp_soft_reset_async(pipe->processor[0]);
}

static void lima_gp_task_error(struct lima_sched_pipe *pipe)
{
	lima_gp_hard_reset(pipe->processor[0]);
}

static void lima_gp_task_mmu_error(struct lima_sched_pipe *pipe)
{
	lima_sched_pipe_task_done(pipe);
}

static void lima_gp_print_version(struct lima_ip *ip)
{
	u32 version, major, minor;
	char *name;

	version = gp_read(VERSION);
	major = (version >> 8) & 0xFF;
	minor = version & 0xFF;
	switch (version >> 16) {
	case 0xA07:
	    name = "mali200";
		break;
	case 0xC07:
		name = "mali300";
		break;
	case 0xB07:
		name = "mali400";
		break;
	case 0xD07:
		name = "mali450";
		break;
	default:
		name = "unknow";
		break;
	}
	dev_info(ip->dev->dev, "%s - %s version major %d minor %d\n",
		 lima_ip_name(ip), name, major, minor);
}

static struct kmem_cache *lima_gp_task_slab = NULL;
static int lima_gp_task_slab_refcnt = 0;

int lima_gp_init(struct lima_ip *ip)
{
	struct lima_device *dev = ip->dev;
	int err;

	lima_gp_print_version(ip);

	ip->data.async_reset = false;
	lima_gp_soft_reset_async(ip);
	err = lima_gp_soft_reset_async_wait(ip);
	if (err)
		return err;

	err = devm_request_irq(dev->dev, ip->irq, lima_gp_irq_handler, 0,
			       lima_ip_name(ip), ip);
	if (err) {
		dev_err(dev->dev, "gp %s fail to request irq\n",
			lima_ip_name(ip));
		return err;
	}

	return 0;
}

void lima_gp_fini(struct lima_ip *ip)
{

}

int lima_gp_pipe_init(struct lima_device *dev)
{
	int frame_size = sizeof(struct drm_lima_m400_gp_frame);
	struct lima_sched_pipe *pipe = dev->pipe + lima_pipe_gp;

	if (!lima_gp_task_slab) {
		lima_gp_task_slab = kmem_cache_create(
			"lima_gp_task", sizeof(struct lima_sched_task) + frame_size,
			0, SLAB_HWCACHE_ALIGN, NULL);
		if (!lima_gp_task_slab)
			return -ENOMEM;
	}
	lima_gp_task_slab_refcnt++;

	pipe->frame_size = frame_size;
	pipe->task_slab = lima_gp_task_slab;

	pipe->task_validate = lima_gp_task_validate;
	pipe->task_run = lima_gp_task_run;
	pipe->task_fini = lima_gp_task_fini;
	pipe->task_error = lima_gp_task_error;
	pipe->task_mmu_error = lima_gp_task_mmu_error;

	return 0;
}

void lima_gp_pipe_fini(struct lima_device *dev)
{
	if (!--lima_gp_task_slab_refcnt) {
		kmem_cache_destroy(lima_gp_task_slab);
		lima_gp_task_slab = NULL;
	}
}
