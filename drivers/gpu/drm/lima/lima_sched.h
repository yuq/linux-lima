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

#include <linux/list.h>
#include <linux/wait.h>

struct lima_sched_task {
	struct list_head list;
	struct lima_vm *vm;
	void *frame;

	struct dma_fence **dep;
	int num_dep;
	int max_dep;

	struct dma_fence *fence;
};

#define LIMA_SCHED_PIPE_MAX_MMU 4
struct lima_sched_pipe {
	const char *name;

	u64 fence_context;
	spinlock_t fence_lock;
	struct lima_mmu *mmu[LIMA_SCHED_PIPE_MAX_MMU];
	int num_mmu;

	struct task_struct *worker;
	wait_queue_head_t worker_wait;

	spinlock_t lock;
	struct list_head queue;
	u32 fence_seqno;

	u32 fence_done_seqno;

	int (*start_task)(void *data, struct lima_sched_task *task);
	int (*reset)(void *data);
	void *data;
};

struct lima_sched_task *lima_sched_task_create(struct lima_vm *vm, void *frame);
void lima_sched_task_delete(struct lima_sched_task *task);
int lima_sched_task_add_dep(struct lima_sched_task *task, struct dma_fence *fence);
int lima_sched_task_queue(struct lima_sched_pipe *pipe, struct lima_sched_task *task);

int lima_sched_pipe_init(struct lima_sched_pipe *pipe, const char *name);
void lima_sched_pipe_fini(struct lima_sched_pipe *pipe);
int lima_sched_pipe_wait_fence(struct lima_sched_pipe *pipe, u32 fence, u64 timeout_ns);
void lima_sched_pipe_task_done(struct lima_sched_pipe *pipe);

#endif
