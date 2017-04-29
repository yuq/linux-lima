#include "lima.h"

struct lima_fence {
	struct dma_fence base;
	struct lima_sched_pipe *pipe;
};

static inline struct lima_fence *to_lima_fence(struct dma_fence *fence)
{
	return container_of(fence, struct lima_fence, base);
}

static const char *lima_fence_get_driver_name(struct dma_fence *fence)
{
	return "lima";
}

static const char *lima_fence_get_timeline_name(struct dma_fence *fence)
{
	struct lima_fence *f = to_lima_fence(fence);

	return f->pipe->name;
}

static bool lima_fence_enable_signaling(struct dma_fence *fence)
{
	return true;
}

static void lima_fence_release(struct dma_fence *fence)
{
	struct lima_fence *f = to_lima_fence(fence);

	kfree_rcu(f, base.rcu);
}

static const struct dma_fence_ops lima_fence_ops = {
	.get_driver_name = lima_fence_get_driver_name,
	.get_timeline_name = lima_fence_get_timeline_name,
	.enable_signaling = lima_fence_enable_signaling,
	.wait = dma_fence_default_wait,
	.release = lima_fence_release,
};

struct lima_sched_task *lima_sched_task_create(void)
{
	struct lima_sched_task *task;

	task = kzalloc(sizeof(*task), GFP_KERNEL);
	if (!task)
		return ERR_PTR(-ENOMEM);

	return task;
}

void lima_sched_task_delete(struct lima_sched_task *task)
{
	int i;

	if (task->fence)
		dma_fence_put(task->fence);

	for (i = 0; i < task->num_dep; i++)
		dma_fence_put(task->dep[i]);

	if (task->dep)
		kfree(task->dep);

	kfree(task);
}

int lima_sched_task_add_dep(struct lima_sched_task *task, struct dma_fence *fence)
{
	int i, new_dep = 4;

	if (task->dep && task->num_dep == task->max_dep)
		new_dep = task->max_dep * 2;

	if (task->max_dep < new_dep) {
		void *dep = krealloc(task->dep, sizeof(*task->dep) * new_dep, GFP_KERNEL);
		if (!dep)
			return -ENOMEM;
		task->max_dep = new_dep;
		task->dep = dep;
	}

	dma_fence_get(fence);
	for (i = 0; i < task->num_dep; i++) {
		if (task->dep[i]->context == fence->context &&
		    dma_fence_is_later(fence, task->dep[i])) {
			dma_fence_put(task->dep[i]);
			task->dep[i] = fence;
			return 0;
		}
	}

	task->dep[task->num_dep++] = fence;
	return 0;
}

int lima_sched_task_queue(struct lima_sched_pipe *pipe, struct lima_sched_task *task)
{
	struct lima_fence *fence;

	fence = kzalloc(sizeof(*fence), GFP_KERNEL);
	if (!fence)
		return -ENOMEM;
	fence->pipe = pipe;

	mutex_lock(&pipe->lock);

	dma_fence_init(&fence->base, &lima_fence_ops, &pipe->fence_lock,
		       pipe->fence_context, ++pipe->fence_seqno);
	task->fence = &fence->base;

	list_add_tail(&task->list, &pipe->queue);

	mutex_unlock(&pipe->lock);
	return 0;
}

void lima_sched_pipe_init(struct lima_sched_pipe *pipe, const char *name)
{
	pipe->name = name;
	mutex_init(&pipe->lock);
	INIT_LIST_HEAD(&pipe->queue);
	pipe->fence_context = dma_fence_context_alloc(1);
	spin_lock_init(&pipe->fence_lock);
}

void lima_sched_pipe_fini(struct lima_sched_pipe *pipe)
{
	struct lima_sched_task *task, *tmp;

	list_for_each_entry_safe(task, tmp, &pipe->queue, list) {
		list_del(&task->list);
		lima_sched_task_delete(task);
	}
}
