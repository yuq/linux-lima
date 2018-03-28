#include "lima.h"

#define LIMA_PP_FRAME                        0x0000
#define LIMA_PP_RSW			     0x0004
#define LIMA_PP_STACK			     0x0030
#define LIMA_PP_STACK_SIZE		     0x0034
#define LIMA_PP_ORIGIN_OFFSET_X	             0x0040
#define LIMA_PP_WB(i) 			     (0x0100 * (i + 1))
#define   LIMA_PP_WB_SOURCE_SELECT           0x0000
#define	  LIMA_PP_WB_SOURCE_ADDR             0x0004

#define LIMA_PP_VERSION                      0x1000
#define LIMA_PP_CURRENT_REND_LIST_ADDR       0x1004
#define	LIMA_PP_STATUS                       0x1008
#define	  LIMA_PP_STATUS_RENDERING_ACTIVE    (1 << 0)
#define	  LIMA_PP_STATUS_BUS_STOPPED	     (1 << 4)
#define	LIMA_PP_CTRL                         0x100c
#define   LIMA_PP_CTRL_STOP_BUS	             (1 << 0)
#define	  LIMA_PP_CTRL_FLUSH_CACHES          (1 << 3)
#define	  LIMA_PP_CTRL_FORCE_RESET           (1 << 5)
#define	  LIMA_PP_CTRL_START_RENDERING       (1 << 6)
#define	  LIMA_PP_CTRL_SOFT_RESET            (1 << 7)
#define	LIMA_PP_INT_RAWSTAT                  0x1020
#define	LIMA_PP_INT_CLEAR                    0x1024
#define	LIMA_PP_INT_MASK                     0x1028
#define	LIMA_PP_INT_STATUS                   0x102c
#define	  LIMA_PP_IRQ_END_OF_FRAME           (1 << 0)
#define	  LIMA_PP_IRQ_END_OF_TILE	     (1 << 1)
#define	  LIMA_PP_IRQ_HANG		     (1 << 2)
#define	  LIMA_PP_IRQ_FORCE_HANG	     (1 << 3)
#define	  LIMA_PP_IRQ_BUS_ERROR		     (1 << 4)
#define	  LIMA_PP_IRQ_BUS_STOP		     (1 << 5)
#define	  LIMA_PP_IRQ_CNT_0_LIMIT	     (1 << 6)
#define	  LIMA_PP_IRQ_CNT_1_LIMIT	     (1 << 7)
#define	  LIMA_PP_IRQ_WRITE_BOUNDARY_ERROR   (1 << 8)
#define	  LIMA_PP_IRQ_INVALID_PLIST_COMMAND  (1 << 9)
#define	  LIMA_PP_IRQ_CALL_STACK_UNDERFLOW   (1 << 10)
#define	  LIMA_PP_IRQ_CALL_STACK_OVERFLOW    (1 << 11)
#define	  LIMA_PP_IRQ_RESET_COMPLETED	     (1 << 12)
#define	LIMA_PP_WRITE_BOUNDARY_LOW           0x1044
#define	LIMA_PP_BUS_ERROR_STATUS             0x1050
#define	LIMA_PP_PERF_CNT_0_ENABLE            0x1080
#define	LIMA_PP_PERF_CNT_0_SRC               0x1084
#define	LIMA_PP_PERF_CNT_0_LIMIT             0x1088
#define	LIMA_PP_PERF_CNT_0_VALUE             0x108c
#define	LIMA_PP_PERF_CNT_1_ENABLE            0x10a0
#define	LIMA_PP_PERF_CNT_1_SRC               0x10a4
#define	LIMA_PP_PERF_CNT_1_LIMIT             0x10a8
#define	LIMA_PP_PERF_CNT_1_VALUE             0x10ac
#define LIMA_PP_PERFMON_CONTR                0x10b0
#define LIMA_PP_PERFMON_BASE                 0x10b4

#define LIMA_PP_IRQ_MASK_ALL                 \
	(                                    \
	 LIMA_PP_IRQ_END_OF_FRAME          | \
	 LIMA_PP_IRQ_END_OF_TILE           | \
	 LIMA_PP_IRQ_HANG                  | \
	 LIMA_PP_IRQ_FORCE_HANG            | \
	 LIMA_PP_IRQ_BUS_ERROR             | \
	 LIMA_PP_IRQ_BUS_STOP              | \
	 LIMA_PP_IRQ_CNT_0_LIMIT           | \
	 LIMA_PP_IRQ_CNT_1_LIMIT           | \
	 LIMA_PP_IRQ_WRITE_BOUNDARY_ERROR  | \
	 LIMA_PP_IRQ_INVALID_PLIST_COMMAND | \
	 LIMA_PP_IRQ_CALL_STACK_UNDERFLOW  | \
	 LIMA_PP_IRQ_CALL_STACK_OVERFLOW   | \
	 LIMA_PP_IRQ_RESET_COMPLETED)

#define LIMA_PP_IRQ_MASK_ERROR               \
	(                                    \
	 LIMA_PP_IRQ_FORCE_HANG            | \
	 LIMA_PP_IRQ_BUS_ERROR             | \
	 LIMA_PP_IRQ_WRITE_BOUNDARY_ERROR  | \
	 LIMA_PP_IRQ_INVALID_PLIST_COMMAND | \
	 LIMA_PP_IRQ_CALL_STACK_UNDERFLOW  | \
	 LIMA_PP_IRQ_CALL_STACK_OVERFLOW)

#define LIMA_PP_IRQ_MASK_USED                \
	(                                    \
	 LIMA_PP_IRQ_END_OF_FRAME          | \
	 LIMA_PP_IRQ_MASK_ERROR)

#define pp_write(reg, data) writel(data, core->ip.iomem + LIMA_PP_##reg)
#define pp_read(reg) readl(core->ip.iomem + LIMA_PP_##reg)

static irqreturn_t lima_pp_core_irq_handler(int irq, void *data)
{
	struct lima_pp_core *core = data;
	struct lima_device *dev = core->ip.dev;
	struct lima_pp *pp = dev->pp;
	u32 state = pp_read(INT_STATUS);
	bool task_done = false;

	/* for shared irq case */
	if (!state)
		return IRQ_NONE;

	if (state & LIMA_PP_IRQ_MASK_ERROR) {
		u32 status = pp_read(STATUS);

		dev_err(dev->dev, "pp error irq state=%x status=%x\n",
			state, status);

		task_done = true;
		pp->error = true;

		/* mask all interrupts before hard reset */
		pp_write(INT_MASK, 0);
	}
	else {
		if (state & LIMA_PP_IRQ_END_OF_FRAME)
			task_done = true;
	}

	pp_write(INT_CLEAR, state);

	if (task_done && atomic_dec_and_test(&pp->task))
		lima_sched_pipe_task_done(&pp->pipe, pp->error);

	return IRQ_HANDLED;
}

static void lima_pp_core_soft_reset_async(struct lima_pp_core *core)
{
	if (core->async_reset)
		return;

	pp_write(INT_MASK, 0);
	pp_write(INT_RAWSTAT, LIMA_PP_IRQ_MASK_ALL);
	pp_write(CTRL, LIMA_PP_CTRL_SOFT_RESET);
	core->async_reset = true;
}

static int lima_pp_core_soft_reset_async_wait(struct lima_pp_core *core)
{
	struct lima_device *dev = core->ip.dev;
	int timeout;

	if (!core->async_reset)
		return 0;

	for (timeout = 1000; timeout > 0; timeout--) {
		if (!(pp_read(STATUS) & LIMA_PP_STATUS_RENDERING_ACTIVE) &&
		    pp_read(INT_RAWSTAT) == LIMA_PP_IRQ_RESET_COMPLETED)
			break;
	}
	if (!timeout) {
		dev_err(dev->dev, "gp reset time out\n");
		return -ETIMEDOUT;
	}

	pp_write(INT_CLEAR, LIMA_PP_IRQ_MASK_ALL);
	pp_write(INT_MASK, LIMA_PP_IRQ_MASK_USED);

	core->async_reset = false;
	return 0;
}

static void lima_pp_core_start_task(struct lima_pp_core *core, int index,
				    struct lima_sched_task *task)
{
	struct drm_lima_m400_pp_frame *frame = task->frame;
	u32 *frame_reg = (void *)&frame->frame;
	const int num_frame_reg = 23, num_wb_reg = 12;
	int i, j;

	lima_pp_core_soft_reset_async_wait(core);

	frame->frame.plbu_array_address = frame->plbu_array_address[index];
	frame->frame.fragment_stack_address = frame->fragment_stack_address[index];

	for (i = 0; i < num_frame_reg; i++)
		writel(frame_reg[i], core->ip.iomem + LIMA_PP_FRAME + i * 4);

	for (i = 0; i < 3; i++) {
		u32 *wb_reg = (void *)&frame->wb[i];
		for (j = 0; j < num_wb_reg; j++)
			writel(wb_reg[j], core->ip.iomem + LIMA_PP_WB(i) + j * 4);
	}

	pp_write(CTRL, LIMA_PP_CTRL_START_RENDERING);
}

static int lima_pp_core_hard_reset(struct lima_pp_core *core)
{
	struct lima_device *dev = core->ip.dev;
	int timeout;

	pp_write(PERF_CNT_0_LIMIT, 0xC0FFE000);
	pp_write(INT_MASK, 0);
	pp_write(CTRL, LIMA_PP_CTRL_FORCE_RESET);
	for (timeout = 1000; timeout > 0; timeout--) {
		pp_write(PERF_CNT_0_LIMIT, 0xC01A0000);
		if (pp_read(PERF_CNT_0_LIMIT) == 0xC01A0000)
			break;
	}
	if (!timeout) {
		dev_err(dev->dev, "pp hard reset timeout\n");
		return -ETIMEDOUT;
	}

	pp_write(PERF_CNT_0_LIMIT, 0);
	pp_write(INT_CLEAR, LIMA_PP_IRQ_MASK_ALL);
	pp_write(INT_MASK, LIMA_PP_IRQ_MASK_USED);
	return 0;
}

static void lima_pp_print_version(struct lima_pp_core *core)
{
	u32 version, major, minor;
	char *name;

	version = pp_read(VERSION);
	major = (version >> 8) & 0xFF;
	minor = version & 0xFF;
	switch (version >> 16) {
	case 0xC807:
	    name = "mali200";
		break;
	case 0xCE07:
		name = "mali300";
		break;
	case 0xCD07:
		name = "mali400";
		break;
	case 0xCF07:
		name = "mali450";
		break;
	default:
		name = "unknow";
		break;
	}
	dev_info(core->ip.dev->dev, "%s - %s version major %d minor %d\n",
		 core->ip.name, name, major, minor);
}

int lima_pp_core_init(struct lima_pp_core *core)
{
	struct lima_device *dev = core->ip.dev;
	int err;

	lima_pp_print_version(core);

	core->async_reset = false;
	lima_pp_core_soft_reset_async(core);
	err = lima_pp_core_soft_reset_async_wait(core);
	if (err)
		return err;

	err = devm_request_irq(dev->dev, core->ip.irq, lima_pp_core_irq_handler,
			       IRQF_SHARED, core->ip.name, core);
	if (err) {
		dev_err(dev->dev, "pp %s fail to request irq\n", core->ip.name);
		return err;
	}

	return 0;
}

void lima_pp_core_fini(struct lima_pp_core *core)
{
	
}

static int lima_pp_task_validate(void *data, struct lima_sched_task *task)
{
	struct lima_pp *pp = data;
	struct drm_lima_m400_pp_frame *f = task->frame;

	if (f->num_pp > pp->num_core)
		return -EINVAL;

	return 0;
}

static void lima_pp_task_run(void *data, struct lima_sched_task *task)
{
	struct lima_pp *pp = data;
	struct drm_lima_m400_pp_frame *frame = task->frame;
	int i;

	pp->error = false;
	atomic_set(&pp->task, frame->num_pp);

	for (i = 0; i < frame->num_pp; i++)
		lima_pp_core_start_task(pp->core + i, i, task);
}

static void lima_pp_task_fini(void *data)
{
	struct lima_pp *pp = data;
	int i;

	for (i = 0; i < pp->num_core; i++)
		lima_pp_core_soft_reset_async(pp->core + i);
}

static void lima_pp_task_error(void *data)
{
	struct lima_pp *pp = data;
	int i;

	for (i = 0; i < pp->num_core; i++)
		lima_pp_core_hard_reset(pp->core + i);
}

static void lima_pp_task_mmu_error(void *data)
{
	struct lima_pp *pp = data;

	pp->error = true;
	if (atomic_dec_and_test(&pp->task))
		lima_sched_pipe_task_done(&pp->pipe, pp->error);
}

static struct kmem_cache *lima_pp_task_slab = NULL;
static int lima_pp_task_slab_refcnt = 0;

int lima_pp_init(struct lima_pp *pp)
{
	int i, frame_size;

	frame_size = sizeof(struct drm_lima_m400_pp_frame);
	if (!lima_pp_task_slab) {
		lima_pp_task_slab = kmem_cache_create(
			"lima_pp_task", sizeof(struct lima_sched_task) + frame_size,
			0, SLAB_HWCACHE_ALIGN, NULL);
		if (!lima_pp_task_slab)
			return -ENOMEM;
	}
	lima_pp_task_slab_refcnt++;

	pp->pipe.frame_size = frame_size;
	pp->pipe.task_slab = lima_pp_task_slab;

	pp->pipe.task_validate = lima_pp_task_validate;
	pp->pipe.task_run = lima_pp_task_run;
	pp->pipe.task_fini = lima_pp_task_fini;
	pp->pipe.task_error = lima_pp_task_error;
	pp->pipe.task_mmu_error = lima_pp_task_mmu_error;
	pp->pipe.data = pp;

	for (i = 0; i < pp->num_core; i++)
		pp->pipe.mmu[i] = &pp->core[i].mmu;
	pp->pipe.num_mmu = pp->num_core;
	return 0;
}

void lima_pp_fini(struct lima_pp *pp)
{
	if (!--lima_pp_task_slab_refcnt) {
		kmem_cache_destroy(lima_pp_task_slab);
		lima_pp_task_slab = NULL;
	}
}
