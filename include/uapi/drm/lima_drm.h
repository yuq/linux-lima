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
#ifndef __LIMA_DRM_H__
#define __LIMA_DRM_H__

#include "drm.h"

#if defined(__cplusplus)
extern "C" {
#endif

#define LIMA_INFO_GPU_MALI400 0x00

struct drm_lima_info {
	__u32 gpu_id;  /* out */
	__u32 num_pp;  /* out */
};

struct drm_lima_gem_create {
	__u32 size;    /* in */
	__u32 flags;   /* in */
	__u32 handle;  /* out */
	__u32 pad;
};

struct drm_lima_gem_info {
	__u32 handle;  /* in */
	__u32 pad;
	__u64 offset;  /* out */
};

#define LIMA_VA_OP_MAP    1
#define LIMA_VA_OP_UNMAP  2

struct drm_lima_gem_va {
	__u32 handle;  /* in */
	__u32 op;      /* in */
	__u32 flags;   /* in */
	__u32 va;      /* in */
};

#define LIMA_SUBMIT_BO_READ   0x01
#define LIMA_SUBMIT_BO_WRITE  0x02

struct drm_lima_gem_submit_bo {
	__u32 handle;  /* in */
	__u32 flags;   /* in */
};

struct drm_lima_m400_gp_frame {
	__u32 vs_cmd_start;
	__u32 vs_cmd_end;
	__u32 plbu_cmd_start;
	__u32 plbu_cmd_end;
	__u32 tile_heap_start;
	__u32 tile_heap_end;
};

struct drm_lima_m400_pp_frame {
	__u32 dummy;
};

struct drm_lima_gem_submit {
	__u32 fence;       /* out */
	__u32 pipe;        /* in */
	__u32 nr_bos;      /* in */
	__u64 bos;         /* in */
	__u64 frame;       /* in */
	__u64 frame_size;  /* in */
};

struct drm_lima_wait_fence {
	__u32 pipe;        /* in */
	__u32 fence;       /* in */
	__u64 timeout_ns;  /* in */
};

#define DRM_LIMA_INFO        0x00
#define DRM_LIMA_GEM_CREATE  0x01
#define DRM_LIMA_GEM_INFO    0x02
#define DRM_LIMA_GEM_VA      0x03
#define DRM_LIMA_GEM_SUBMIT  0x04
#define DRM_LIMA_WAIT_FENCE  0x05

#define DRM_IOCTL_LIMA_INFO DRM_IOR(DRM_COMMAND_BASE + DRM_LIMA_INFO, struct drm_lima_info)
#define DRM_IOCTL_LIMA_GEM_CREATE DRM_IOWR(DRM_COMMAND_BASE + DRM_LIMA_GEM_CREATE, struct drm_lima_gem_create)
#define DRM_IOCTL_LIMA_GEM_INFO DRM_IOWR(DRM_COMMAND_BASE + DRM_LIMA_GEM_INFO, struct drm_lima_gem_info)
#define DRM_IOCTL_LIMA_GEM_VA DRM_IOW(DRM_COMMAND_BASE + DRM_LIMA_GEM_VA, struct drm_lima_gem_va)
#define DRM_IOCTL_LIMA_GEM_SUBMIT DRM_IOWR(DRM_COMMAND_BASE + DRM_LIMA_GEM_SUBMIT, struct drm_lima_gem_submit)
#define DRM_IOCTL_LIMA_WAIT_FENCE DRM_IOW(DRM_COMMAND_BASE + DRM_LIMA_WAIT_FENCE, struct drm_lima_wait_fence)

#if defined(__cplusplus)
}
#endif

#endif /* __LIMA_DRM_H__ */
