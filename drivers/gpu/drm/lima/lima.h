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
#ifndef __LIMA_H__

#include <linux/rbtree.h>

#include <drm/drmP.h>
#include <drm/lima_drm.h>

#include "lima_vm.h"
#include "lima_sched.h"

extern int lima_sched_timeout_ms;
extern int lima_sched_max_tasks;

enum lima_gpu_type {
	GPU_MALI400 = 0,
	GPU_MALI450,
};

struct lima_device;

#define LIMA_IP_MAX_NAME_LEN 32

struct lima_ip {
	struct lima_device *dev;
	char name[LIMA_IP_MAX_NAME_LEN];
	void __iomem *iomem;
	int irq;
};

struct lima_pmu {
	struct lima_ip ip;
	uint32_t switch_delay;
};

struct lima_l2_cache {
	struct lima_ip ip;
};

struct lima_mmu {
	struct lima_ip ip;
	struct lima_sched_pipe *pipe;

	spinlock_t lock;
	struct lima_vm *vm;
	bool zap_all;
};

#define LIMA_GP_TASK_VS    0x01
#define LIMA_GP_TASK_PLBU  0x02

struct lima_gp {
	struct lima_ip ip;
	struct lima_mmu mmu;
	struct lima_sched_pipe pipe;
	struct lima_l2_cache *l2_cache;

	int task;
	bool async_reset;
};

struct lima_pp_core {
	struct lima_ip ip;
	struct lima_mmu mmu;
	bool async_reset;
};

#define LIMA_MAX_PP 4

struct lima_pp {
	struct lima_pp_core core[LIMA_MAX_PP];
	int num_core;
	struct lima_sched_pipe pipe;
	struct lima_l2_cache *l2_cache;
	atomic_t task;
	bool error;
};

#define LIMA_MAX_PIPE 2

struct lima_device {
	struct device *dev;
	struct drm_device *ddev;
	struct platform_device *pdev;

	enum lima_gpu_type gpu_type;
	void __iomem *iomem;

	struct clk *clk_bus;
	struct clk *clk_gpu;
	struct reset_control *reset;
	struct regulator *regulator;

	struct lima_pmu *pmu;

	struct lima_l2_cache *l2_cache;

	struct lima_sched_pipe *pipe[LIMA_MAX_PIPE];

	struct lima_gp *gp;
	struct lima_pp *pp;
	int num_pp;

	struct lima_vm *empty_vm;
};

struct lima_ctx {
	struct kref refcnt;
	struct lima_device *dev;
	struct lima_sched_context context[LIMA_MAX_PIPE];
	atomic_t guilty;
};

struct lima_ctx_mgr {
	spinlock_t lock;
	struct idr handles;
};

struct lima_drm_priv {
	struct lima_vm *vm;
	struct lima_ctx_mgr ctx_mgr;
};

struct lima_bo_va_mapping {
	struct list_head list;
	struct rb_node rb;
	uint32_t start;
	uint32_t last;
	uint32_t __subtree_last;
};

struct lima_submit {
	struct lima_ctx *ctx;
	int pipe;

	struct drm_lima_gem_submit_bo *bos;
	struct lima_bo **lbos;
	u32 nr_bos;

	struct lima_sched_task *task;

	uint32_t fence;
	uint32_t done;
};

static inline struct lima_drm_priv *
to_lima_drm_priv(struct drm_file *file)
{
	return file->driver_priv;
}

int lima_device_init(struct lima_device *ldev);
void lima_device_fini(struct lima_device *ldev);

int lima_pmu_init(struct lima_pmu *pmu);
void lima_pmu_fini(struct lima_pmu *pmu);

int lima_l2_cache_init(struct lima_l2_cache *l2_cache);
void lima_l2_cache_fini(struct lima_l2_cache *l2_cache);
int lima_l2_cache_flush(struct lima_l2_cache *l2_cache);

int lima_mmu_init(struct lima_mmu *mmu);
void lima_mmu_fini(struct lima_mmu *mmu);
void lima_mmu_switch_vm(struct lima_mmu *mmu, struct lima_vm *vm, bool reset);
void lima_mmu_zap_vm(struct lima_mmu *mmu, struct lima_vm *vm, u32 va, u32 size);
void lima_mmu_page_fault_resume(struct lima_mmu *mmu);

int lima_gp_init(struct lima_gp *gp);
void lima_gp_fini(struct lima_gp *gp);

int lima_pp_core_init(struct lima_pp_core *core);
void lima_pp_core_fini(struct lima_pp_core *core);
int lima_pp_init(struct lima_pp *pp);
void lima_pp_fini(struct lima_pp *pp);

unsigned long lima_timeout_to_jiffies(u64 timeout_ns);

int lima_ctx_create(struct lima_device *dev, struct lima_ctx_mgr *mgr, u32 *id);
int lima_ctx_free(struct lima_ctx_mgr *mgr, u32 id);
struct lima_ctx *lima_ctx_get(struct lima_ctx_mgr *mgr, u32 id);
void lima_ctx_put(struct lima_ctx *ctx);
void lima_ctx_mgr_init(struct lima_ctx_mgr *mgr);
void lima_ctx_mgr_fini(struct lima_ctx_mgr *mgr);

#endif
