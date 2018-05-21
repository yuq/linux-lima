/* Copyright 2017-2018 Qiang Yu <yuq825@gmail.com>
 * SPDX-License-Identifier: MIT
 */

#ifndef __LIMA_L2_CACHE_H__
#define __LIMA_L2_CACHE_H__

struct lima_ip;

int lima_l2_cache_init(struct lima_ip *ip);
void lima_l2_cache_fini(struct lima_ip *ip);

int lima_l2_cache_flush(struct lima_ip *ip);

#endif
