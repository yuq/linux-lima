/* Copyright 2017-2018 Qiang Yu <yuq825@gmail.com>
 * SPDX-License-Identifier: MIT
 */

#ifndef __LIMA_PP_H__
#define __LIMA_PP_H__

struct lima_ip;
struct lima_device;

int lima_pp_init(struct lima_ip *ip);
void lima_pp_fini(struct lima_ip *ip);

int lima_pp_bcast_init(struct lima_ip *ip);
void lima_pp_bcast_fini(struct lima_ip *ip);

int lima_pp_pipe_init(struct lima_device *dev);
void lima_pp_pipe_fini(struct lima_device *dev);

#endif
