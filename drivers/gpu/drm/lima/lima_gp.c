#include "lima.h"

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

#define LIMA_GP_IRQ_MASK_USED		   \
	(				   \
	 LIMA_GP_IRQ_VS_END_CMD_LST      | \
	 LIMA_GP_IRQ_PLBU_END_CMD_LST    | \
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

#define gp_write(reg, data) writel(data, gp->ip.iomem + LIMA_GP_##reg)
#define gp_read(reg) readl(gp->ip.iomem + LIMA_GP_##reg)

static irqreturn_t lima_gp_irq_handler(int irq, void *data)
{
	struct lima_gp *gp = data;
	struct lima_device *dev = gp->ip.dev;
	u32 state = gp_read(INT_STAT);
	u32 status = gp_read(STATUS);
	bool task_done = false;

	dev_info_ratelimited(dev->dev, "gp irq state=%x status=%x\n", state, status);

	if (state & LIMA_GP_IRQ_VS_END_CMD_LST) {
		gp->task &= ~LIMA_GP_TASK_VS;
		task_done = true;
	}

	if (state & LIMA_GP_IRQ_PLBU_END_CMD_LST) {
		gp->task &= ~LIMA_GP_TASK_PLBU;
		task_done = true;
	}

	if (task_done && !gp->task)
		lima_sched_pipe_task_done(&gp->pipe);

	gp_write(INT_CLEAR, state);
	return IRQ_NONE;
}

static int lima_gp_start_task(void *data, struct lima_sched_task *task)
{
	struct lima_gp *gp = data;
	struct lima_device *dev = gp->ip.dev;
	struct drm_lima_m400_gp_frame *frame = task->frame;
	u32 cmd = 0;

	dev_info(dev->dev, "lima start task gp status %08x\n", gp_read(STATUS));

	if (frame->vs_cmd_start > frame->vs_cmd_end ||
	    frame->plbu_cmd_start > frame->plbu_cmd_end ||
	    frame->tile_heap_start > frame->tile_heap_end)
		return -EINVAL;

	gp->task = 0;
	if (frame->vs_cmd_start != frame->vs_cmd_end) {
		cmd |= LIMA_GP_CMD_START_VS;
		gp->task |= LIMA_GP_TASK_VS;
	}
	if (frame->plbu_cmd_start != frame->plbu_cmd_end) {
		cmd |= LIMA_GP_CMD_START_PLBU;
		gp->task |= LIMA_GP_TASK_PLBU;
	}

	if (!cmd) {
		dev_err(dev->dev, "start gp task is empty\n");
		return -EINVAL;
	}

	gp_write(VSCL_START_ADDR, frame->vs_cmd_start);
	gp_write(VSCL_END_ADDR, frame->vs_cmd_end);
	gp_write(PLBUCL_START_ADDR, frame->plbu_cmd_start);
	gp_write(PLBUCL_END_ADDR, frame->plbu_cmd_end);
	gp_write(PLBU_ALLOC_START_ADDR, frame->tile_heap_start);
	gp_write(PLBU_ALLOC_END_ADDR, frame->tile_heap_end);

	gp_write(CMD, LIMA_GP_CMD_UPDATE_PLBU_ALLOC);
	gp_write(CMD, cmd);
	return 0;
}

static int lima_gp_reset(void *data)
{
	struct lima_gp *gp = data;
	struct lima_device *dev = gp->ip.dev;
	int timeout;

	gp_write(INT_MASK, 0);
	gp_write(INT_CLEAR, LIMA_GP_IRQ_RESET_COMPLETED);
	gp_write(CMD, LIMA_GP_CMD_SOFT_RESET);
	for (timeout = 1000; timeout > 0; timeout--) {
		if (gp_read(INT_RAWSTAT) & LIMA_GP_IRQ_RESET_COMPLETED)
			break;
	}
	if (!timeout) {
		dev_err(dev->dev, "gp reset time out\n");
		return -ETIMEDOUT;
	}

	gp_write(INT_CLEAR, LIMA_GP_IRQ_MASK_ALL);
	gp_write(INT_MASK, LIMA_GP_IRQ_MASK_USED);
	return 0;
}

int lima_gp_init(struct lima_gp *gp)
{
	struct lima_device *dev = gp->ip.dev;
	int err;

	err = lima_gp_reset(gp);
	if (err)
		return err;

	err = devm_request_irq(dev->dev, gp->ip.irq, lima_gp_irq_handler, 0,
			       gp->ip.name, gp);
	if (err) {
		dev_err(dev->dev, "gp %s fail to request irq\n", gp->ip.name);
		return err;
	}

	gp->pipe.start_task = lima_gp_start_task;
	gp->pipe.reset = lima_gp_reset;
	gp->pipe.data = gp;
	gp->pipe.mmu[0] = &gp->mmu;
	gp->pipe.num_mmu = 1;
	return 0;
}

void lima_gp_fini(struct lima_gp *gp)
{

}
