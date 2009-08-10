/*
 * Copyright (C) 2007 Ben Skeggs.
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

#include "drmP.h"
#include "drm.h"
#include "nouveau_drv.h"
#include "nouveau_dma.h"

int
nouveau_dma_init(struct nouveau_channel *chan)
{
	struct drm_device *dev = chan->dev;
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nouveau_gpuobj *m2mf = NULL;
	int ret, i;

	/* Create NV_MEMORY_TO_MEMORY_FORMAT for buffer moves */
	ret = nouveau_gpuobj_gr_new(chan, dev_priv->card_type < NV_50 ?
				    0x0039 : 0x5039, &m2mf);
	if (ret)
		return ret;

	ret = nouveau_gpuobj_ref_add(dev, chan, NvM2MF, m2mf, NULL);
	if (ret)
		return ret;

	/* NV_MEMORY_TO_MEMORY_FORMAT requires a notifier object */
	ret = nouveau_notifier_alloc(chan, NvNotify0, 32, &chan->m2mf_ntfy);
	if (ret)
		return ret;

	/* Map push buffer */
	ret = nouveau_bo_map(chan->pushbuf_bo);
	if (ret)
		return ret;
	chan->dma.pushbuf = chan->pushbuf_bo->kmap.virtual;

	/* Map M2MF notifier object - fbcon. */
	if (drm_core_check_feature(dev, DRIVER_MODESET)) {
		ret = nouveau_bo_map(chan->notifier_bo);
		if (ret)
			return ret;
		chan->m2mf_ntfy_map  = chan->notifier_bo->kmap.virtual;
		chan->m2mf_ntfy_map += chan->m2mf_ntfy;
	}

	/* Initialise DMA vars */
	chan->dma.max  = (chan->pushbuf_bo->bo.mem.size >> 2) - 2;
	chan->dma.put  = 0;
	chan->dma.cur  = chan->dma.put;
	chan->dma.free = chan->dma.max - chan->dma.cur;

	/* Insert NOPS for NOUVEAU_DMA_SKIPS */
	RING_SPACE(chan, NOUVEAU_DMA_SKIPS);
	for (i = 0; i < NOUVEAU_DMA_SKIPS; i++)
		OUT_RING (chan, 0);

	/* Initialise NV_MEMORY_TO_MEMORY_FORMAT */
	RING_SPACE(chan, 4);
	BEGIN_RING(chan, NvSubM2MF, NV_MEMORY_TO_MEMORY_FORMAT_NAME, 1);
	OUT_RING  (chan, NvM2MF);
	BEGIN_RING(chan, NvSubM2MF, NV_MEMORY_TO_MEMORY_FORMAT_DMA_NOTIFY, 1);
	OUT_RING  (chan, NvNotify0);

	/* Sit back and pray the channel works.. */
	FIRE_RING (chan);

	return 0;
}

static inline bool
READ_GET(struct nouveau_channel *chan, uint32_t *get)
{
	uint32_t val;

	val = nvchan_rd32(chan->user_get);
	if (val < chan->pushbuf_base ||
	    val >= chan->pushbuf_base + chan->pushbuf_bo->bo.mem.size)
		return false;

	*get = (val - chan->pushbuf_base) >> 2;
	return true;
}

int
nouveau_dma_wait(struct nouveau_channel *chan, int size)
{
	const int us_timeout = 100000;
	uint32_t get;
	int ret = -EBUSY, i;

	for (i = 0; i < us_timeout; i++) {
		if (!READ_GET(chan, &get)) {
			DRM_UDELAY(1);
			continue;
		}

		if (chan->dma.put >= get) {
			chan->dma.free = chan->dma.max - chan->dma.cur;

			if (chan->dma.free < size) {
				OUT_RING(chan, 0x20000000|chan->pushbuf_base);
				if (get <= NOUVEAU_DMA_SKIPS) {
					/*corner case - will be idle*/
					if (chan->dma.put <= NOUVEAU_DMA_SKIPS)
						WRITE_PUT(NOUVEAU_DMA_SKIPS + 1);

					for (; i < us_timeout; i++) {
						if (READ_GET(chan, &get) &&
						    get > NOUVEAU_DMA_SKIPS)
							break;

						DRM_UDELAY(1);
					}

					if (i >= us_timeout)
						break;
				}

				WRITE_PUT(NOUVEAU_DMA_SKIPS);
				chan->dma.cur  =
				chan->dma.put  = NOUVEAU_DMA_SKIPS;
				chan->dma.free = get - (NOUVEAU_DMA_SKIPS + 1);
			}
		} else {
			chan->dma.free = get - chan->dma.cur - 1;
		}

		if (chan->dma.free >= size) {
			ret = 0;
			break;
		}

		DRM_UDELAY(1);
	}

	return ret;
}
