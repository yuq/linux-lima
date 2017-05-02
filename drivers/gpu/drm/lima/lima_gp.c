#include "lima.h"

static int lima_gp_start_task(void *data, struct lima_sched_task *task)
{
	struct lima_gp *gp = data;

	DRM_INFO("lima start task gp %s\n", gp->ip.name);
	dma_fence_signal(task->fence);
	return 0;
}

int lima_gp_init(struct lima_gp *gp)
{
	gp->pipe.start_task = lima_gp_start_task;
	gp->pipe.data = gp;
	return 0;
}

void lima_gp_fini(struct lima_gp *gp)
{
	
}
