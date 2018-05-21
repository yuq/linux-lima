/* Copyright 2018 Qiang Yu <yuq825@gmail.com>
 * SPDX-License-Identifier: MIT
 */

#ifndef __LIMA_BCAST_H__
#define __LIMA_BCAST_H__

struct lima_ip;

int lima_bcast_init(struct lima_ip *ip);
void lima_bcast_fini(struct lima_ip *ip);

void lima_bcast_enable(struct lima_device *dev);
void lima_bcast_disable(struct lima_device *dev);

#endif
