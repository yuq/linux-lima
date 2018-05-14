/*
 * Copyright (C) 2017-2018 Lima Project
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
#ifndef __LIMA_DRV_H__
#define __LIMA_DRV_H__

#include <drm/drmP.h>
#include <drm/ttm/ttm_execbuf_util.h>

#include "lima_ctx.h"

extern int lima_sched_timeout_ms;
extern int lima_sched_max_tasks;
extern int lima_max_mem;

struct lima_vm;
struct lima_bo;
struct lima_sched_task;

struct drm_lima_gem_submit_bo;

#define DRM_FILE_PAGE_OFFSET (0x100000000ULL >> PAGE_SHIFT)

struct lima_drm_priv {
	struct lima_vm *vm;
	struct lima_ctx_mgr ctx_mgr;
};

struct lima_submit {
	struct lima_ctx *ctx;
	int pipe;
	u32 flags;

	struct drm_lima_gem_submit_bo *bos;
	struct ttm_validate_buffer *vbs;
	u32 nr_bos;

	struct ttm_validate_buffer vm_pd_vb;
	struct ww_acquire_ctx ticket;
	struct list_head duplicates;
	struct list_head validated;

	union drm_lima_gem_submit_dep *deps;
	u32 nr_deps;

	struct lima_sched_task *task;

	uint32_t fence;
	uint32_t done;
	int sync_fd;
};

static inline struct lima_drm_priv *
to_lima_drm_priv(struct drm_file *file)
{
	return file->driver_priv;
}

#endif
