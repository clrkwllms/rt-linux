/*
 * Copyright (C) 2008 Maarten Maathuis.
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial
 * portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE COPYRIGHT OWNER(S) AND/OR ITS SUPPLIERS BE
 * LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
 * OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
 * WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 */

#ifndef __NOUVEAU_OUTPUT_H__
#define __NOUVEAU_OUTPUT_H__

#include "nouveau_drv.h"

#define NV_DPMS_CLEARED 0x80

struct nouveau_encoder {
	struct drm_encoder base;

	struct dcb_entry *dcb;
	int or;

	bool dual_link;

	int last_dpms;

	struct nv04_output_reg restore;
};
#define nouveau_encoder(x) container_of((x), struct nouveau_encoder, base)

int nv50_sor_create(struct drm_device *dev, struct dcb_entry *entry);
int nv50_dac_create(struct drm_device *dev, struct dcb_entry *entry);

#endif /* __NOUVEAU_OUTPUT_H__ */
