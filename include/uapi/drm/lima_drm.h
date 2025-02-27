/* SPDX-License-Identifier: (GPL-2.0 WITH Linux-syscall-note) OR MIT */
/* Copyright 2017-2018 Qiang Yu <yuq825@gmail.com> */

#ifndef __LIMA_DRM_H__
#define __LIMA_DRM_H__

#include "drm.h"

#if defined(__cplusplus)
extern "C" {
#endif

#define LIMA_INFO_GPU_MALI400 0x00
#define LIMA_INFO_GPU_MALI450 0x01

struct drm_lima_info {
	__u32 gpu_id;   /* out */
	__u32 num_pp;   /* out */
	__u64 va_start; /* out */
	__u64 va_end;   /* out */
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

#define LIMA_SUBMIT_DEP_FENCE   0x00
#define LIMA_SUBMIT_DEP_SYNC_FD 0x01

struct drm_lima_gem_submit_dep_fence {
	__u32 type;
	__u32 ctx;
	__u32 pipe;
	__u32 seq;
};

struct drm_lima_gem_submit_dep_sync_fd {
	__u32 type;
	__u32 fd;
};

union drm_lima_gem_submit_dep {
	__u32 type;
	struct drm_lima_gem_submit_dep_fence fence;
	struct drm_lima_gem_submit_dep_sync_fd sync_fd;
};

#define LIMA_GP_FRAME_REG_NUM 6

struct drm_lima_gp_frame {
	__u32 frame[LIMA_GP_FRAME_REG_NUM];
};

#define LIMA_PP_FRAME_REG_NUM 23
#define LIMA_PP_WB_REG_NUM 12

struct drm_lima_m400_pp_frame {
	__u32 frame[LIMA_PP_FRAME_REG_NUM];
	__u32 num_pp;
	__u32 wb[3 * LIMA_PP_WB_REG_NUM];
	__u32 plbu_array_address[4];
	__u32 fragment_stack_address[4];
};

struct drm_lima_m450_pp_frame {
	__u32 frame[LIMA_PP_FRAME_REG_NUM];
	__u32 _pad;
	__u32 wb[3 * LIMA_PP_WB_REG_NUM];
	__u32 dlbu_regs[4];
	__u32 fragment_stack_address[8];
};

#define LIMA_PIPE_GP  0x00
#define LIMA_PIPE_PP  0x01

#define LIMA_SUBMIT_FLAG_EXPLICIT_FENCE (1 << 0)
#define LIMA_SUBMIT_FLAG_SYNC_FD_OUT    (1 << 1)

struct drm_lima_gem_submit_in {
	__u32 ctx;
	__u32 pipe;
	__u32 nr_bos;
	__u32 frame_size;
	__u64 bos;
	__u64 frame;
	__u64 deps;
	__u32 nr_deps;
	__u32 flags;
};

struct drm_lima_gem_submit_out {
	__u32 fence;
	__u32 done;
	__u32 sync_fd;
	__u32 _pad;
};

union drm_lima_gem_submit {
	struct drm_lima_gem_submit_in in;
	struct drm_lima_gem_submit_out out;
};

struct drm_lima_wait_fence {
	__u32 ctx;         /* in */
	__u32 pipe;        /* in */
	__u64 timeout_ns;  /* in */
	__u32 seq;         /* in */
	__u32 _pad;
};

#define LIMA_GEM_WAIT_READ   0x01
#define LIMA_GEM_WAIT_WRITE  0x02

struct drm_lima_gem_wait {
	__u32 handle;      /* in */
	__u32 op;          /* in */
	__u64 timeout_ns;  /* in */
};

#define LIMA_CTX_OP_CREATE 1
#define LIMA_CTX_OP_FREE   2

struct drm_lima_ctx {
	__u32 op;          /* in */
	__u32 id;          /* in/out */
};

#define DRM_LIMA_INFO        0x00
#define DRM_LIMA_GEM_CREATE  0x01
#define DRM_LIMA_GEM_INFO    0x02
#define DRM_LIMA_GEM_VA      0x03
#define DRM_LIMA_GEM_SUBMIT  0x04
#define DRM_LIMA_WAIT_FENCE  0x05
#define DRM_LIMA_GEM_WAIT    0x06
#define DRM_LIMA_CTX         0x07

#define DRM_IOCTL_LIMA_INFO DRM_IOR(DRM_COMMAND_BASE + DRM_LIMA_INFO, struct drm_lima_info)
#define DRM_IOCTL_LIMA_GEM_CREATE DRM_IOWR(DRM_COMMAND_BASE + DRM_LIMA_GEM_CREATE, struct drm_lima_gem_create)
#define DRM_IOCTL_LIMA_GEM_INFO DRM_IOWR(DRM_COMMAND_BASE + DRM_LIMA_GEM_INFO, struct drm_lima_gem_info)
#define DRM_IOCTL_LIMA_GEM_VA DRM_IOW(DRM_COMMAND_BASE + DRM_LIMA_GEM_VA, struct drm_lima_gem_va)
#define DRM_IOCTL_LIMA_GEM_SUBMIT DRM_IOWR(DRM_COMMAND_BASE + DRM_LIMA_GEM_SUBMIT, union drm_lima_gem_submit)
#define DRM_IOCTL_LIMA_WAIT_FENCE DRM_IOW(DRM_COMMAND_BASE + DRM_LIMA_WAIT_FENCE, struct drm_lima_wait_fence)
#define DRM_IOCTL_LIMA_GEM_WAIT DRM_IOW(DRM_COMMAND_BASE + DRM_LIMA_GEM_WAIT, struct drm_lima_gem_wait)
#define DRM_IOCTL_LIMA_CTX DRM_IOWR(DRM_COMMAND_BASE + DRM_LIMA_CTX, struct drm_lima_ctx)

#if defined(__cplusplus)
}
#endif

#endif /* __LIMA_DRM_H__ */
