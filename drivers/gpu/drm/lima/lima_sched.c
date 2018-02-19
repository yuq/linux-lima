#include <linux/kthread.h>

#include "lima.h"

struct lima_fence {
	struct dma_fence base;
	struct lima_sched_pipe *pipe;
};

static struct kmem_cache *lima_fence_slab = NULL;

int lima_sched_slab_init(void)
{
	lima_fence_slab = kmem_cache_create(
		"lima_fence", sizeof(struct lima_fence), 0,
		SLAB_HWCACHE_ALIGN, NULL);
	if (!lima_fence_slab)
		return -ENOMEM;

	return 0;
}

void lima_sched_slab_fini(void)
{
	if (lima_fence_slab)
		kmem_cache_destroy(lima_fence_slab);
}

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

	return f->pipe->base.name;
}

static bool lima_fence_enable_signaling(struct dma_fence *fence)
{
	return true;
}

static void lima_fence_release_rcu(struct rcu_head *rcu)
{
	struct dma_fence *f = container_of(rcu, struct dma_fence, rcu);
	struct lima_fence *fence = to_lima_fence(f);

	kmem_cache_free(lima_fence_slab, fence);
}

static void lima_fence_release(struct dma_fence *fence)
{
	struct lima_fence *f = to_lima_fence(fence);

	call_rcu(&f->base.rcu, lima_fence_release_rcu);
}

static const struct dma_fence_ops lima_fence_ops = {
	.get_driver_name = lima_fence_get_driver_name,
	.get_timeline_name = lima_fence_get_timeline_name,
	.enable_signaling = lima_fence_enable_signaling,
	.wait = dma_fence_default_wait,
	.release = lima_fence_release,
};

static inline struct lima_sched_task *to_lima_task(struct drm_sched_job *job)
{
	return container_of(job, struct lima_sched_task, base);
}

static inline struct lima_sched_pipe *to_lima_pipe(struct drm_gpu_scheduler *sched)
{
	return container_of(sched, struct lima_sched_pipe, base);
}

int lima_sched_task_init(struct lima_sched_task *task,
			 struct lima_sched_context *context,
			 struct lima_vm *vm)
{
	struct lima_fence *fence;
	int err;

	fence = kmem_cache_zalloc(lima_fence_slab, GFP_KERNEL);
	if (!fence)
	       return -ENOMEM;

	err = drm_sched_job_init(&task->base, context->base.sched,
				 &context->base, context);
	if (err)
		goto err_out0;

	task->vm = lima_vm_get(vm);
	task->fence = &fence->base;
	return 0;

err_out0:
	kmem_cache_free(lima_fence_slab, fence);
	return err;
}

void lima_sched_task_fini(struct lima_sched_task *task)
{
	struct lima_fence *fence = to_lima_fence(task->fence);
	kmem_cache_free(lima_fence_slab, fence);

	dma_fence_put(&task->base.s_fence->finished);

	lima_vm_put(task->vm);
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

int lima_sched_context_init(struct lima_sched_pipe *pipe,
			    struct lima_sched_context *context,
			    atomic_t *guilty)
{
	struct drm_sched_rq *rq = pipe->base.sched_rq + DRM_SCHED_PRIORITY_NORMAL;
	int err;

	context->fences =
		kzalloc(sizeof(*context->fences) * lima_sched_max_tasks, GFP_KERNEL);
	if (!context->fences)
		return -ENOMEM;

	spin_lock_init(&context->lock);
	err = drm_sched_entity_init(&pipe->base, &context->base, rq,
				    lima_sched_max_tasks, guilty);
	if (err) {
		kfree(context->fences);
		context->fences = NULL;
		return err;
	}

	return 0;
}

void lima_sched_context_fini(struct lima_sched_pipe *pipe,
			     struct lima_sched_context *context)
{
	drm_sched_entity_fini(&pipe->base, &context->base);

	if (context->fences)
		kfree(context->fences);
}

static uint32_t lima_sched_context_add_fence(struct lima_sched_context *context,
					     struct dma_fence *fence,
					     uint32_t *done)
{
	uint32_t seq, idx, i, n;
	struct dma_fence *other;

	spin_lock(&context->lock);

	seq = context->sequence;
	idx = seq & (lima_sched_max_tasks - 1);
	other = context->fences[idx];

	if (other) {
		int err = dma_fence_wait(other, false);
		if (err)
			DRM_ERROR("Error %d waiting context fence\n", err);
	}

	context->fences[idx] = dma_fence_get(fence);
	context->sequence++;

	/* get finished fence offset from seq */
	n = min(seq + 1, (uint32_t)lima_sched_max_tasks);
	for (i = 1; i < n; i++) {
		idx = (seq - i) & (lima_sched_max_tasks - 1);
		if (dma_fence_is_signaled(context->fences[idx]))
			break;
	}

	spin_unlock(&context->lock);

	dma_fence_put(other);

	*done = i;
	return seq;
}

static struct dma_fence *lima_sched_context_get_fence(struct lima_sched_context *context,
						      uint32_t seq)
{
	struct dma_fence *fence;
	int idx;

	spin_lock(&context->lock);

	/* assume no overflow */
	if (seq >= context->sequence) {
		fence = ERR_PTR(-EINVAL);
		goto out;
	}

	if (seq + lima_sched_max_tasks < context->sequence) {
		fence = NULL;
		goto out;
	}

	idx = seq & (lima_sched_max_tasks - 1);
	fence = dma_fence_get(context->fences[idx]);

out:
	spin_unlock(&context->lock);

	return fence;
}

uint32_t lima_sched_context_queue_task(struct lima_sched_context *context,
				       struct lima_sched_task *task,
				       uint32_t *done)
{
	uint32_t seq = lima_sched_context_add_fence(
		context, &task->base.s_fence->finished, done);
	drm_sched_entity_push_job(&task->base, &context->base);
	return seq;
}

int lima_sched_context_wait_fence(struct lima_sched_context *context,
				  u32 fence, u64 timeout_ns)
{
	int ret;
	struct dma_fence *f = lima_sched_context_get_fence(context, fence);

	if (IS_ERR(f))
		return PTR_ERR(f);
	else if (!f)
		return 0;

	if (!timeout_ns)
		ret = dma_fence_is_signaled(f) ? 0 : -EBUSY;
	else {
		unsigned long timeout = lima_timeout_to_jiffies(timeout_ns);

		ret = dma_fence_wait_timeout(f, true, timeout);
		if (ret == 0)
			ret = -ETIMEDOUT;
		else if (ret > 0)
			ret = 0;
	}

	dma_fence_put(f);
	return ret;
}

static struct dma_fence *lima_sched_dependency(struct drm_sched_job *job,
					       struct drm_sched_entity *entity)
{
	struct lima_sched_task *task = to_lima_task(job);
	int i;

	for (i = 0; i < task->num_dep; i++) {
		struct dma_fence *fence = task->dep[i];

		if (!task->dep[i])
			continue;

		task->dep[i] = NULL;

		if (!dma_fence_is_signaled(fence))
			return fence;

		dma_fence_put(fence);
	}

	return NULL;
}

static struct dma_fence *lima_sched_run_job(struct drm_sched_job *job)
{
	struct lima_sched_task *task = to_lima_task(job);
	struct lima_sched_pipe *pipe = to_lima_pipe(job->sched);
	struct lima_fence *fence = to_lima_fence(task->fence);
	struct dma_fence *ret;
	int i;

	/* after GPU reset */
	if (job->s_fence->finished.error < 0)
		return NULL;

	fence->pipe = pipe;
	dma_fence_init(task->fence, &lima_fence_ops, &pipe->fence_lock,
		       pipe->fence_context, ++pipe->fence_seqno);

	/* for caller usage of the fence, otherwise irq handler 
	 * may consume the fence before caller use it */
	ret = dma_fence_get(task->fence);

	pipe->current_task = task;

	/* this is needed for MMU to work correctly, otherwise GP/PP
	 * will hang or page fault for unknown reason after running for
	 * a while.
	 *
	 * Need to investigate:
	 * 1. is it related to TLB
	 * 2. how much performance will be affected by L2 cache flush
	 * 3. can we reduce the calling of this function because all
	 *    GP/PP use the same L2 cache
	 */
	if (pipe->mmu[0]->ip.dev->gpu_type == GPU_MALI450) {
		lima_l2_cache_flush(pipe->mmu[0]->ip.dev->gp->l2_cache);
		lima_l2_cache_flush(pipe->mmu[0]->ip.dev->pp->l2_cache);
	} else {
		lima_l2_cache_flush(pipe->mmu[0]->ip.dev->l2_cache);
	}

	for (i = 0; i < pipe->num_mmu; i++)
		lima_mmu_switch_vm(pipe->mmu[i], task->vm, false);

	pipe->task_run(pipe->data, task);

	return task->fence;
}

static void lima_sched_handle_error_task(struct lima_sched_pipe *pipe,
					 struct lima_sched_task *task)
{
	int i;

	kthread_park(pipe->base.thread);
	drm_sched_hw_job_reset(&pipe->base, &task->base);

	pipe->task_error(pipe->data);

	for (i = 0; i < pipe->num_mmu; i++)
		lima_mmu_page_fault_resume(pipe->mmu[i]);

	drm_sched_job_recovery(&pipe->base);
	kthread_unpark(pipe->base.thread);
}

static void lima_sched_timedout_job(struct drm_sched_job *job)
{
	struct lima_sched_pipe *pipe = to_lima_pipe(job->sched);
	struct lima_sched_task *task = to_lima_task(job);

	lima_sched_handle_error_task(pipe, task);
}

static void lima_sched_free_job(struct drm_sched_job *job)
{
	struct lima_sched_task *task = to_lima_task(job);
	struct lima_sched_pipe *pipe = to_lima_pipe(job->sched);
	int i;

	dma_fence_put(task->fence);

	for (i = 0; i < task->num_dep; i++) {
		if (task->dep[i])
			dma_fence_put(task->dep[i]);
	}

	if (task->dep)
		kfree(task->dep);

	lima_vm_put(task->vm);

	kmem_cache_free(pipe->task_slab, task);
}

const struct drm_sched_backend_ops lima_sched_ops = {
	.dependency = lima_sched_dependency,
	.run_job = lima_sched_run_job,
	.timedout_job = lima_sched_timedout_job,
	.free_job = lima_sched_free_job,
};

static void lima_sched_error_work(struct work_struct *work)
{
	struct lima_sched_pipe *pipe =
		container_of(work, struct lima_sched_pipe, error_work);
	struct lima_sched_task *task = pipe->current_task;

	lima_sched_handle_error_task(pipe, task);
}

int lima_sched_pipe_init(struct lima_sched_pipe *pipe, const char *name)
{
	long timeout;

	if (lima_sched_timeout_ms <= 0)
		timeout = MAX_SCHEDULE_TIMEOUT;
	else
		timeout = msecs_to_jiffies(lima_sched_timeout_ms);

	pipe->fence_context = dma_fence_context_alloc(1);
	spin_lock_init(&pipe->fence_lock);

	INIT_WORK(&pipe->error_work, lima_sched_error_work);

	return drm_sched_init(&pipe->base, &lima_sched_ops, 1, 0, timeout, name);
}

void lima_sched_pipe_fini(struct lima_sched_pipe *pipe)
{
	drm_sched_fini(&pipe->base);
}

unsigned long lima_timeout_to_jiffies(u64 timeout_ns)
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

void lima_sched_pipe_task_done(struct lima_sched_pipe *pipe, bool error)
{
	if (error)
	        schedule_work(&pipe->error_work);
	else {
		struct lima_sched_task *task = pipe->current_task;

		pipe->task_fini(pipe->data);
		dma_fence_signal(task->fence);
	}
}
