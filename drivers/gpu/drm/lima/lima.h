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

#include <drm/drmP.h>
#include <drm/lima_drm.h>

#include "lima_vm.h"
#include "lima_sched.h"

enum lima_gpu_type {
	GPU_MALI400 = 0,
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

	int task;
};

struct lima_pp_core {
	struct lima_ip ip;
	struct lima_mmu mmu;
};

#define LIMA_MAX_PP 4

struct lima_pp {
	struct lima_pp_core core[LIMA_MAX_PP];
	int num_core;
	struct lima_sched_pipe pipe;
	atomic_t task;
};

struct lima_device {
	struct device *dev;
	struct drm_device *ddev;
	struct platform_device *pdev;

	enum lima_gpu_type gpu_type;

	struct clk *clk_bus;
	struct clk *clk_gpu;
	struct reset_control *reset;

	struct lima_pmu *pmu;

	struct lima_l2_cache *l2_cache;

	struct lima_sched_pipe *pipe[2];

	struct lima_gp *gp;
	struct lima_pp *pp;
	int num_pp;

	struct lima_vm *empty_vm;
};

struct lima_drm_priv {
	struct lima_vm *vm;
};

int lima_device_init(struct lima_device *ldev, struct drm_device *dev);
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
void lima_pp_init(struct lima_pp *pp);

int lima_gem_create_handle(struct drm_device *dev, struct drm_file *file,
			   u32 size, u32 flags, u32 *handle);
void lima_gem_free_object(struct drm_gem_object *obj);
int lima_gem_mmap_offset(struct drm_file *file, u32 handle, u64 *offset);
int lima_gem_mmap(struct file *filp, struct vm_area_struct *vma);
int lima_gem_va_map(struct drm_file *file, u32 handle, u32 flags, u32 va);
int lima_gem_va_unmap(struct drm_file *file, u32 handle, u32 va);
int lima_gem_submit(struct drm_file *file, struct lima_sched_pipe *pipe,
		    struct drm_lima_gem_submit_bo *bos, u32 nr_bos,
		    void *frame, u32 *fence);
int lima_gem_wait(struct drm_file *file, u32 handle, u32 op, u64 timeout_ns);

unsigned long lima_timeout_to_jiffies(u64 timeout_ns);

#endif
