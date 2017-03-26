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

#define DRM_LIMA_INFO 0x00

#define DRM_IOCTL_LIMA_INFO DRM_IOR(DRM_COMMAND_BASE + DRM_LIMA_INFO, struct drm_lima_info)

#if defined(__cplusplus)
}
#endif

#endif /* __LIMA_DRM_H__ */
