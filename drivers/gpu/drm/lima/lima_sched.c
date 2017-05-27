#include <linux/kthread.h>

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

struct lima_sched_task *lima_sched_task_create(struct lima_vm *vm, void *frame)
{
	struct lima_sched_task *task;

	task = kzalloc(sizeof(*task), GFP_KERNEL);
	if (!task)
		return ERR_PTR(-ENOMEM);

	task->vm = lima_vm_get(vm);
	task->frame = frame;

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

	if (task->frame)
		kfree(task->frame);

	if (task->vm)
		lima_vm_put(task->vm);

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
	unsigned long saved_flags;
	struct lima_fence *fence;

	fence = kzalloc(sizeof(*fence), GFP_KERNEL);
	if (!fence)
		return -ENOMEM;
	fence->pipe = pipe;

	spin_lock_irqsave(&pipe->lock, saved_flags);

	dma_fence_init(&fence->base, &lima_fence_ops, &pipe->fence_lock,
		       pipe->fence_context, ++pipe->fence_seqno);
	task->fence = &fence->base;

	/* for caller usage of the fence, otherwise pipe worker 
	 * may consumed the fence */
	dma_fence_get(task->fence);

	list_add_tail(&task->list, &pipe->queue);

	spin_unlock_irqrestore(&pipe->lock, saved_flags);

	wake_up(&pipe->worker_wait);
	return 0;
}

static struct lima_sched_task *lima_sched_pipe_get_task(struct lima_sched_pipe *pipe)
{
	unsigned long saved_flags;
	struct lima_sched_task *task;

	spin_lock_irqsave(&pipe->lock, saved_flags);
	task = list_first_entry_or_null(&pipe->queue, struct lima_sched_task, list);
	spin_unlock_irqrestore(&pipe->lock, saved_flags);
	return task;
}

#define LIMA_WORKER_WAIT_TIMEOUT_NS 1000000000

static int lima_sched_pipe_worker_wait_fence(struct dma_fence *fence)
{
	int ret;
	unsigned long timeout = nsecs_to_jiffies(LIMA_WORKER_WAIT_TIMEOUT_NS);

	while (1) {
		ret = dma_fence_wait_timeout(fence, true, timeout);

		/* interrupted by signal, may be kthread stop */
		if (ret == -ERESTARTSYS) {
			if (kthread_should_stop())
				return ret;
			else
				continue;
		}

		if (ret < 0)
			return ret;

		if (!ret)
			return -ETIMEDOUT;

		return 0;
	}

	return 0;
}

static int lima_sched_pipe_worker(void *param)
{
	struct lima_sched_pipe *pipe = param;
	struct lima_sched_task *task;

	while (!kthread_should_stop()) {
		int i, ret;
		unsigned long saved_flags;

		wait_event_interruptible(pipe->worker_wait,
					 (task = lima_sched_pipe_get_task(pipe)) ||
					 kthread_should_stop());

		if (!task)
			continue;

		/* wait all dependent fence be signaled */
		for (i = 0; i < task->num_dep; i++) {
			ret = lima_sched_pipe_worker_wait_fence(task->dep[i]);
			if (ret == -ERESTARTSYS)
				return 0;
			if (ret < 0) {
				DRM_INFO("lima worker wait dep fence error %d\n", ret);
				goto abort;
			}
		}

		for (i = 0; i < pipe->num_mmu; i++)
			lima_mmu_switch_vm(pipe->mmu[i], task->vm, false);

		if (!pipe->start_task(pipe->data, task)) {
			ret = lima_sched_pipe_worker_wait_fence(task->fence);
			if (ret == -ERESTARTSYS)
				return 0;
			if (ret < 0) {
				DRM_INFO("lima worker wait task fence error %d\n", ret);
				pipe->reset(pipe->data);
			}
		}

	abort:
		spin_lock_irqsave(&pipe->lock, saved_flags);
		list_del(&task->list);
		spin_unlock_irqrestore(&pipe->lock, saved_flags);
		lima_sched_task_delete(task);
		/* the only writer of this counter */
		pipe->fence_done_seqno++;
	}

	return 0;
}

int lima_sched_pipe_init(struct lima_sched_pipe *pipe, const char *name)
{
	struct task_struct *worker;

	pipe->name = name;
	spin_lock_init(&pipe->lock);
	INIT_LIST_HEAD(&pipe->queue);
	pipe->fence_context = dma_fence_context_alloc(1);
	spin_lock_init(&pipe->fence_lock);
	init_waitqueue_head(&pipe->worker_wait);

	worker = kthread_run(lima_sched_pipe_worker, pipe, name);
	if (IS_ERR(worker)) {
		DRM_ERROR("Fail to create pipe worker for %s\n", name);
		return PTR_ERR(worker);
	}
	pipe->worker = worker;
	return 0;
}

void lima_sched_pipe_fini(struct lima_sched_pipe *pipe)
{
	struct lima_sched_task *task, *tmp;

	kthread_stop(pipe->worker);

	list_for_each_entry_safe(task, tmp, &pipe->queue, list) {
		list_del(&task->list);
		lima_sched_task_delete(task);
	}
}

static unsigned long lima_timeout_to_jiffies(u64 timeout_ns)
{
	unsigned long timeout_jiffies;
	ktime_t timeout;

	/* clamp timeout if it's to large */
	if (((s64)timeout_ns) < 0)
		return MAX_SCHEDULE_TIMEOUT;

	timeout = ktime_sub(ns_to_ktime(timeout_ns), ktime_get());
	if (ktime_to_ns(timeout) < 0)
		return 0;

	timeout_jiffies = nsecs_to_jiffies(ktime_to_ns(timeout));
	/*  clamp timeout to avoid unsigned-> signed overflow */
	if (timeout_jiffies > MAX_SCHEDULE_TIMEOUT )
		return MAX_SCHEDULE_TIMEOUT;

	return timeout_jiffies;
}

static struct dma_fence *lima_sched_pipe_get_fence(struct lima_sched_pipe *pipe, u32 fence)
{
	unsigned long saved_flags;
	struct lima_sched_task *task;
	struct dma_fence *f = NULL;

	spin_lock_irqsave(&pipe->lock, saved_flags);
	list_for_each_entry(task, &pipe->queue, list) {
		if (task->fence->seqno < fence)
			continue;

		if (task->fence->seqno == fence) {
			f = task->fence;
			dma_fence_get(f);
		}

		break;
	}
	spin_unlock_irqrestore(&pipe->lock, saved_flags);

	return f;
}

int lima_sched_pipe_wait_fence(struct lima_sched_pipe *pipe, u32 fence, u64 timeout_ns)
{
	int ret = 0;

	if (fence > pipe->fence_seqno)
		return -EINVAL;

	if (!timeout_ns)
		return fence <= pipe->fence_done_seqno ? 0 : -EBUSY;
	else {
		unsigned long timeout = lima_timeout_to_jiffies(timeout_ns);
		struct dma_fence *f = lima_sched_pipe_get_fence(pipe, fence);

		if (f) {
			ret = dma_fence_wait_timeout(f, true, timeout);
			if (ret == 0)
				ret = -ETIMEDOUT;
			else if (ret > 0)
				ret = 0;
			dma_fence_put(f);
		}
	}

	return ret;
}

void lima_sched_pipe_task_done(struct lima_sched_pipe *pipe)
{
	struct lima_sched_task *task;

	spin_lock(&pipe->lock);
	task = list_first_entry(&pipe->queue, struct lima_sched_task, list);
	spin_unlock(&pipe->lock);

	dma_fence_signal(task->fence);
}
