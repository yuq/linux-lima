/*
 * Copyright (C) 2017 Lima Project
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
#ifndef __LIMA_SCHED_H__
#define __LIMA_SCHED_H__

#include <drm/gpu_scheduler.h>

struct lima_vm;

struct lima_sched_task {
	struct drm_sched_job base;

	struct lima_vm *vm;
	void *frame;

	struct dma_fence **dep;
	int num_dep;
	int max_dep;

	/* pipe fence */
	struct dma_fence *fence;
};

struct lima_sched_context {
	struct drm_sched_entity base;
	struct mutex lock;
	struct dma_fence **fences;
	uint32_t sequence;
};

#define LIMA_SCHED_PIPE_MAX_MMU       8
#define LIMA_SCHED_PIPE_MAX_L2_CACHE  2
#define LIMA_SCHED_PIPE_MAX_PROCESSOR 8

struct lima_ip;

struct lima_sched_pipe {
	struct drm_gpu_scheduler base;

	u64 fence_context;
	u32 fence_seqno;
	spinlock_t fence_lock;

	struct lima_sched_task *current_task;
	struct lima_vm *current_vm;

	struct lima_ip *mmu[LIMA_SCHED_PIPE_MAX_MMU];
	int num_mmu;

	struct lima_ip *l2_cache[LIMA_SCHED_PIPE_MAX_L2_CACHE];
	int num_l2_cache;

	struct lima_ip *processor[LIMA_SCHED_PIPE_MAX_PROCESSOR];
	int num_processor;

	struct lima_ip *bcast_processor;
	struct lima_ip *bcast_mmu;

	u32 done;
	bool error;
	atomic_t task;

	int frame_size;
	struct kmem_cache *task_slab;

	int (*task_validate)(struct lima_sched_pipe *pipe, struct lima_sched_task *task);
	void (*task_run)(struct lima_sched_pipe *pipe, struct lima_sched_task *task);
	void (*task_fini)(struct lima_sched_pipe *pipe);
	void (*task_error)(struct lima_sched_pipe *pipe);
	void (*task_mmu_error)(struct lima_sched_pipe *pipe);

	struct work_struct error_work;
};

int lima_sched_task_init(struct lima_sched_task *task,
			 struct lima_sched_context *context,
			 struct lima_vm *vm);
void lima_sched_task_fini(struct lima_sched_task *task);
int lima_sched_task_add_dep(struct lima_sched_task *task, struct dma_fence *fence);

int lima_sched_context_init(struct lima_sched_pipe *pipe,
			    struct lima_sched_context *context,
			    atomic_t *guilty);
void lima_sched_context_fini(struct lima_sched_pipe *pipe,
			     struct lima_sched_context *context);
uint32_t lima_sched_context_queue_task(struct lima_sched_context *context,
				       struct lima_sched_task *task,
				       uint32_t *done);
struct dma_fence *lima_sched_context_get_fence(
	struct lima_sched_context *context, uint32_t seq);

int lima_sched_pipe_init(struct lima_sched_pipe *pipe, const char *name);
void lima_sched_pipe_fini(struct lima_sched_pipe *pipe);
void lima_sched_pipe_task_done(struct lima_sched_pipe *pipe);

static inline void lima_sched_pipe_mmu_error(struct lima_sched_pipe *pipe)
{
	pipe->error = true;
	pipe->task_mmu_error(pipe);
}

int lima_sched_slab_init(void);
void lima_sched_slab_fini(void);

unsigned long lima_timeout_to_jiffies(u64 timeout_ns);

#endif
