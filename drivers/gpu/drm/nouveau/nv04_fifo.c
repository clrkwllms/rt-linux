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

#define RAMFC_WR(offset, val) nv_wo32(dev, chan->ramfc->gpuobj, \
					 NV04_RAMFC_##offset/4, (val))
#define RAMFC_RD(offset)      nv_ro32(dev, chan->ramfc->gpuobj, \
					 NV04_RAMFC_##offset/4)
#define NV04_RAMFC(c) (dev_priv->ramfc_offset + ((c) * NV04_RAMFC__SIZE))
#define NV04_RAMFC__SIZE 32

static int
nouveau_fifo_instmem_configure(struct drm_device *dev)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;

	nv_wr32(dev, NV03_PFIFO_RAMHT,
			(0x03 << 24) /* search 128 */ |
			((dev_priv->ramht_bits - 9) << 16) |
			(dev_priv->ramht_offset >> 8)
			);

	nv_wr32(dev, NV03_PFIFO_RAMRO, dev_priv->ramro_offset>>8);

	switch (dev_priv->card_type) {
	case NV_40:
		switch (dev_priv->chipset) {
		case 0x47:
		case 0x49:
		case 0x4b:
			nv_wr32(dev, 0x2230, 1);
			break;
		default:
			break;
		}

		switch (dev_priv->chipset) {
		case 0x40:
		case 0x41:
		case 0x42:
		case 0x43:
		case 0x45:
		case 0x47:
		case 0x48:
		case 0x49:
		case 0x4b:
			nv_wr32(dev, NV40_PFIFO_RAMFC, 0x30002);
			break;
		default:
			nv_wr32(dev, 0x2230, 0);
			nv_wr32(dev, NV40_PFIFO_RAMFC,
				((nouveau_mem_fb_amount(dev) - 512 * 1024 +
				  dev_priv->ramfc_offset) >> 16) | (2 << 16));
			break;
		}
		break;
	case NV_30:
	case NV_20:
	case NV_17:
		nv_wr32(dev, NV03_PFIFO_RAMFC,
			     (dev_priv->ramfc_offset >> 8) |
			     (1 << 16) /* 64 Bytes entry*/);
		/* XXX nvidia blob set bit 18, 21,23 for nv20 & nv30 */
		break;
	case NV_11:
	case NV_10:
	case NV_04:
	case NV_05:
		nv_wr32(dev, NV03_PFIFO_RAMFC, dev_priv->ramfc_offset >> 8);
		break;
	default:
		NV_ERROR(dev, "unknown card type %d\n", dev_priv->card_type);
		return -EINVAL;
	}

	return 0;
}

int
nv04_fifo_init(struct drm_device *dev)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	uint32_t mode = 0;
	int ret, i;

	for (i = 0; i < dev_priv->engine.fifo.channels; i++) {
		if (dev_priv->fifos[i])
			mode |= (1 << i);
	}

	nv_wr32(dev, NV03_PMC_ENABLE,
		nv_rd32(dev, NV03_PMC_ENABLE) & ~NV_PMC_ENABLE_PFIFO);
	nv_wr32(dev, NV03_PMC_ENABLE,
		nv_rd32(dev, NV03_PMC_ENABLE) | NV_PMC_ENABLE_PFIFO);

	/* Enable PFIFO error reporting */
	nv_wr32(dev, NV03_PFIFO_INTR_0, 0xFFFFFFFF);
	nv_wr32(dev, NV03_PFIFO_INTR_EN_0, 0xFFFFFFFF);

	nv_wr32(dev, NV03_PFIFO_CACHES, 0x00000000);

	ret = nouveau_fifo_instmem_configure(dev);
	if (ret) {
		NV_ERROR(dev, "Failed to configure instance memory\n");
		return ret;
	}

	/* FIXME remove all the stuff that's done in nouveau_fifo_alloc */

	NV_DEBUG(dev, "Setting defaults for remaining PFIFO regs\n");

	/* All channels into PIO mode */
	nv_wr32(dev, NV04_PFIFO_MODE, 0x00000000);

	nv_wr32(dev, NV03_PFIFO_CACHE1_PUSH0, 0x00000000);
	nv_wr32(dev, NV04_PFIFO_CACHE1_PULL0, 0x00000000);
	/* Channel 0 active, PIO mode */
	nv_wr32(dev, NV03_PFIFO_CACHE1_PUSH1, 0x00000000);
	/* PUT and GET to 0 */
	nv_wr32(dev, NV04_PFIFO_CACHE1_DMA_PUT, 0x00000000);
	nv_wr32(dev, NV04_PFIFO_CACHE1_DMA_GET, 0x00000000);
	/* No cmdbuf object */
	nv_wr32(dev, NV04_PFIFO_CACHE1_DMA_INSTANCE, 0x00000000);
	nv_wr32(dev, NV03_PFIFO_CACHE0_PUSH0, 0x00000000);
	nv_wr32(dev, NV04_PFIFO_CACHE0_PULL0, 0x00000000);
	nv_wr32(dev, NV04_PFIFO_SIZE, 0x0000FFFF);
	nv_wr32(dev, NV04_PFIFO_CACHE1_HASH, 0x0000FFFF);
	nv_wr32(dev, NV04_PFIFO_CACHE0_PULL1, 0x00000001);
	nv_wr32(dev, NV04_PFIFO_CACHE1_DMA_CTL, 0x00000000);
	nv_wr32(dev, NV04_PFIFO_CACHE1_DMA_STATE, 0x00000000);
	nv_wr32(dev, NV04_PFIFO_CACHE1_ENGINE, 0x00000000);

	nv_wr32(dev, NV04_PFIFO_CACHE1_DMA_FETCH,
			NV_PFIFO_CACHE1_DMA_FETCH_TRIG_112_BYTES |
			NV_PFIFO_CACHE1_DMA_FETCH_SIZE_128_BYTES |
			NV_PFIFO_CACHE1_DMA_FETCH_MAX_REQS_4 |
#ifdef __BIG_ENDIAN
			NV_PFIFO_CACHE1_BIG_ENDIAN |
#endif
			0x00000000);

	/* FIXME on NV04 */
	if (dev_priv->card_type >= NV_10) {
		nv_wr32(dev, NV10_PGRAPH_CTX_USER, 0x0);
		nv_wr32(dev, NV04_PFIFO_DELAY_0, 0xff /* retrycount */);
		if (dev_priv->card_type >= NV_40)
			nv_wr32(dev, NV10_PGRAPH_CTX_CONTROL, 0x00002001);
		else
			nv_wr32(dev, NV10_PGRAPH_CTX_CONTROL, 0x10110000);
	} else {
		nv_wr32(dev, NV04_PGRAPH_CTX_USER, 0x0);
		nv_wr32(dev, NV04_PFIFO_DELAY_0, 0xff /* retrycount */);
		nv_wr32(dev, NV04_PGRAPH_CTX_CONTROL, 0x10110000);
	}

	nv_wr32(dev, NV04_PFIFO_DMA_TIMESLICE, 0x001fffff);
	nv_wr32(dev, NV04_PFIFO_MODE, mode);
	return 0;
}

int
nv04_fifo_channel_id(struct drm_device *dev)
{
	return nv_rd32(dev, NV03_PFIFO_CACHE1_PUSH1) &
			NV03_PFIFO_CACHE1_PUSH1_CHID_MASK;
}

int
nv04_fifo_create_context(struct nouveau_channel *chan)
{
	struct drm_device *dev = chan->dev;
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	int ret;

	if ((ret = nouveau_gpuobj_new_fake(dev, NV04_RAMFC(chan->id), ~0,
						NV04_RAMFC__SIZE,
						NVOBJ_FLAG_ZERO_ALLOC |
						NVOBJ_FLAG_ZERO_FREE,
						NULL, &chan->ramfc)))
		return ret;

	/* Setup initial state */
	dev_priv->engine.instmem.prepare_access(dev, true);
	RAMFC_WR(DMA_PUT, chan->pushbuf_base);
	RAMFC_WR(DMA_GET, chan->pushbuf_base);
	RAMFC_WR(DMA_INSTANCE, chan->pushbuf->instance >> 4);
	RAMFC_WR(DMA_FETCH, (NV_PFIFO_CACHE1_DMA_FETCH_TRIG_128_BYTES |
			     NV_PFIFO_CACHE1_DMA_FETCH_SIZE_128_BYTES |
			     NV_PFIFO_CACHE1_DMA_FETCH_MAX_REQS_8 |
#ifdef __BIG_ENDIAN
			     NV_PFIFO_CACHE1_BIG_ENDIAN |
#endif
			     0));
	dev_priv->engine.instmem.finish_access(dev);

	/* enable the fifo dma operation */
	nv_wr32(dev, NV04_PFIFO_MODE,
		nv_rd32(dev, NV04_PFIFO_MODE) | (1 << chan->id));
	return 0;
}

void
nv04_fifo_destroy_context(struct nouveau_channel *chan)
{
	struct drm_device *dev = chan->dev;

	nv_wr32(dev, NV04_PFIFO_MODE,
		nv_rd32(dev, NV04_PFIFO_MODE) & ~(1 << chan->id));

	nouveau_gpuobj_ref_del(dev, &chan->ramfc);
}

int
nv04_fifo_load_context(struct nouveau_channel *chan)
{
	struct drm_device *dev = chan->dev;
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	uint32_t tmp;

	dev_priv->engine.instmem.prepare_access(dev, false);

	nv_wr32(dev, NV03_PFIFO_CACHE1_PUSH1,
		 NV03_PFIFO_CACHE1_PUSH1_DMA | chan->id);

	nv_wr32(dev, NV04_PFIFO_CACHE1_DMA_GET, RAMFC_RD(DMA_GET));
	nv_wr32(dev, NV04_PFIFO_CACHE1_DMA_PUT, RAMFC_RD(DMA_PUT));

	tmp = RAMFC_RD(DMA_INSTANCE);
	nv_wr32(dev, NV04_PFIFO_CACHE1_DMA_INSTANCE, tmp & 0xFFFF);
	nv_wr32(dev, NV04_PFIFO_CACHE1_DMA_DCOUNT, tmp >> 16);

	nv_wr32(dev, NV04_PFIFO_CACHE1_DMA_STATE, RAMFC_RD(DMA_STATE));
	nv_wr32(dev, NV04_PFIFO_CACHE1_DMA_FETCH, RAMFC_RD(DMA_FETCH));
	nv_wr32(dev, NV04_PFIFO_CACHE1_ENGINE, RAMFC_RD(ENGINE));
	nv_wr32(dev, NV04_PFIFO_CACHE1_PULL1, RAMFC_RD(PULL1_ENGINE));

	dev_priv->engine.instmem.finish_access(dev);

	/* Reset NV04_PFIFO_CACHE1_DMA_CTL_AT_INFO to INVALID */
	tmp = nv_rd32(dev, NV04_PFIFO_CACHE1_DMA_CTL) & ~(1 << 31);
	nv_wr32(dev, NV04_PFIFO_CACHE1_DMA_CTL, tmp);

	return 0;
}

int
nv04_fifo_save_context(struct nouveau_channel *chan)
{
	struct drm_device *dev = chan->dev;
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	uint32_t tmp;

	dev_priv->engine.instmem.prepare_access(dev, true);

	RAMFC_WR(DMA_PUT, nv_rd32(dev, NV04_PFIFO_CACHE1_DMA_PUT));
	RAMFC_WR(DMA_GET, nv_rd32(dev, NV04_PFIFO_CACHE1_DMA_GET));

	tmp  = nv_rd32(dev, NV04_PFIFO_CACHE1_DMA_DCOUNT) << 16;
	tmp |= nv_rd32(dev, NV04_PFIFO_CACHE1_DMA_INSTANCE);
	RAMFC_WR(DMA_INSTANCE, tmp);

	RAMFC_WR(DMA_STATE, nv_rd32(dev, NV04_PFIFO_CACHE1_DMA_STATE));
	RAMFC_WR(DMA_FETCH, nv_rd32(dev, NV04_PFIFO_CACHE1_DMA_FETCH));
	RAMFC_WR(ENGINE, nv_rd32(dev, NV04_PFIFO_CACHE1_ENGINE));
	RAMFC_WR(PULL1_ENGINE, nv_rd32(dev, NV04_PFIFO_CACHE1_PULL1));

	dev_priv->engine.instmem.finish_access(dev);
	return 0;
}
