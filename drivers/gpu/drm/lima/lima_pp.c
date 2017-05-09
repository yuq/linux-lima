#include "lima.h"

static int lima_pp_start_task(void *data, struct lima_sched_task *task)
{
	struct lima_pp *pp = data;

	DRM_INFO("lima start task pp %s\n", pp->ip.name);
	dma_fence_signal(task->fence);
	return 0;
}

static int lima_pp_reset(void *data)
{
	struct lima_pp *pp = data;

	DRM_INFO("lima reset pp %s\n", pp->ip.name);
	return 0;
}

int lima_pp_init(struct lima_pp *pp)
{
	pp->pipe.start_task = lima_pp_start_task;
	pp->pipe.reset = lima_pp_reset;
	pp->pipe.data = pp;
	pp->pipe.mmu = &pp->mmu;
	return 0;
}

void lima_pp_fini(struct lima_pp *pp)
{
	
}
