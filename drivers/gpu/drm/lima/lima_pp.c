#include "lima.h"

static int lima_pp_start_task(void *data, struct lima_sched_task *task)
{
	struct lima_pp *pp = data;

	DRM_INFO("lima start task pp %s\n", pp->ip.name);
	dma_fence_signal(task->fence);
	return 0;
}

int lima_pp_init(struct lima_pp *pp)
{
	pp->pipe.start_task = lima_pp_start_task;
	pp->pipe.data = pp;
	return 0;
}

void lima_pp_fini(struct lima_pp *pp)
{
	
}
