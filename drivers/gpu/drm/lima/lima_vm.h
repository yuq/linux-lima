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
#ifndef __LIMA_VM_H__
#define __LIMA_VM_H__

#include <linux/mutex.h>
#include <linux/rbtree.h>


struct lima_vm_page {
	u32 *cpu;
	dma_addr_t dma;
};

struct lima_vm {
	struct mutex lock;

	struct device *dev;

	/* tree of virtual addresses mapped */
	struct rb_root va;

        struct lima_vm_page pd;
	struct lima_vm_page *pts;
};

int lima_vm_init(struct lima_vm *vm, struct device *dev, bool empty);
void lima_vm_fini(struct lima_vm *vm);

int lima_vm_map(struct lima_vm *vm, dma_addr_t dma, u32 va, u32 size);
int lima_vm_unmap(struct lima_vm *vm, u32 va, u32 size);

#endif
