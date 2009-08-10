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
#include "nouveau_drv.h"
#include "nouveau_drm.h"


#define RAMFC_WR(offset, val) nv_wo32(dev, chan->ramfc->gpuobj, \
					 NV40_RAMFC_##offset/4, (val))
#define RAMFC_RD(offset)      nv_ro32(dev, chan->ramfc->gpuobj, \
					 NV40_RAMFC_##offset/4)
#define NV40_RAMFC(c) (dev_priv->ramfc_offset + ((c)*NV40_RAMFC__SIZE))
#define NV40_RAMFC__SIZE 128

int
nv40_fifo_create_context(struct nouveau_channel *chan)
{
	struct drm_device *dev = chan->dev;
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	int ret;

	if ((ret = nouveau_gpuobj_new_fake(dev, NV40_RAMFC(chan->id), ~0,
						NV40_RAMFC__SIZE,
						NVOBJ_FLAG_ZERO_ALLOC |
						NVOBJ_FLAG_ZERO_FREE,
						NULL, &chan->ramfc)))
		return ret;

	/* Fill entries that are seen filled in dumps of nvidia driver just
	 * after channel's is put into DMA mode
	 */
	dev_priv->engine.instmem.prepare_access(dev, true);
	RAMFC_WR(DMA_PUT       , chan->pushbuf_base);
	RAMFC_WR(DMA_GET       , chan->pushbuf_base);
	RAMFC_WR(DMA_INSTANCE  , chan->pushbuf->instance >> 4);
	RAMFC_WR(DMA_FETCH     , NV_PFIFO_CACHE1_DMA_FETCH_TRIG_128_BYTES |
				 NV_PFIFO_CACHE1_DMA_FETCH_SIZE_128_BYTES |
				 NV_PFIFO_CACHE1_DMA_FETCH_MAX_REQS_8 |
#ifdef __BIG_ENDIAN
				 NV_PFIFO_CACHE1_BIG_ENDIAN |
#endif
				 0x30000000 /* no idea.. */);
	RAMFC_WR(DMA_SUBROUTINE, 0);
	RAMFC_WR(GRCTX_INSTANCE, chan->ramin_grctx->instance >> 4);
	RAMFC_WR(DMA_TIMESLICE , 0x0001FFFF);
	dev_priv->engine.instmem.finish_access(dev);

	/* enable the fifo dma operation */
	nv_wr32(dev, NV04_PFIFO_MODE,
			nv_rd32(dev, NV04_PFIFO_MODE) | (1 << chan->id));
	return 0;
}

void
nv40_fifo_destroy_context(struct nouveau_channel *chan)
{
	struct drm_device *dev = chan->dev;

	nv_wr32(dev, NV04_PFIFO_MODE,
			nv_rd32(dev, NV04_PFIFO_MODE) & ~(1 << chan->id));

	if (chan->ramfc)
		nouveau_gpuobj_ref_del(dev, &chan->ramfc);
}

int
nv40_fifo_load_context(struct nouveau_channel *chan)
{
	struct drm_device *dev = chan->dev;
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	uint32_t tmp, tmp2;

	dev_priv->engine.instmem.prepare_access(dev, false);

	nv_wr32(dev, NV04_PFIFO_CACHE1_DMA_GET,      RAMFC_RD(DMA_GET));
	nv_wr32(dev, NV04_PFIFO_CACHE1_DMA_PUT,      RAMFC_RD(DMA_PUT));
	nv_wr32(dev, NV10_PFIFO_CACHE1_REF_CNT,      RAMFC_RD(REF_CNT));
	nv_wr32(dev, NV04_PFIFO_CACHE1_DMA_INSTANCE, RAMFC_RD(DMA_INSTANCE));
	nv_wr32(dev, NV04_PFIFO_CACHE1_DMA_DCOUNT,   RAMFC_RD(DMA_DCOUNT));
	nv_wr32(dev, NV04_PFIFO_CACHE1_DMA_STATE,    RAMFC_RD(DMA_STATE));

	/* No idea what 0x2058 is.. */
	tmp   = RAMFC_RD(DMA_FETCH);
	tmp2  = nv_rd32(dev, 0x2058) & 0xFFF;
	tmp2 |= (tmp & 0x30000000);
	nv_wr32(dev, 0x2058, tmp2);
	tmp  &= ~0x30000000;
	nv_wr32(dev, NV04_PFIFO_CACHE1_DMA_FETCH, tmp);

	nv_wr32(dev, NV04_PFIFO_CACHE1_ENGINE,  RAMFC_RD(ENGINE));
	nv_wr32(dev, NV04_PFIFO_CACHE1_PULL1,   RAMFC_RD(PULL1_ENGINE));
	nv_wr32(dev, NV10_PFIFO_CACHE1_ACQUIRE_VALUE, RAMFC_RD(ACQUIRE_VALUE));
	nv_wr32(dev, NV10_PFIFO_CACHE1_ACQUIRE_TIMESTAMP,
						RAMFC_RD(ACQUIRE_TIMESTAMP));
	nv_wr32(dev, NV10_PFIFO_CACHE1_ACQUIRE_TIMEOUT,
						RAMFC_RD(ACQUIRE_TIMEOUT));
	nv_wr32(dev, NV10_PFIFO_CACHE1_SEMAPHORE, RAMFC_RD(SEMAPHORE));
	nv_wr32(dev, NV10_PFIFO_CACHE1_DMA_SUBROUTINE,
						RAMFC_RD(DMA_SUBROUTINE));
	nv_wr32(dev, NV40_PFIFO_GRCTX_INSTANCE, RAMFC_RD(GRCTX_INSTANCE));
	nv_wr32(dev, 0x32e4, RAMFC_RD(UNK_40));
	/* NVIDIA does this next line twice... */
	nv_wr32(dev, 0x32e8, RAMFC_RD(UNK_44));
	nv_wr32(dev, 0x2088, RAMFC_RD(UNK_4C));
	nv_wr32(dev, 0x3300, RAMFC_RD(UNK_50));

	/* not sure what part is PUT, and which is GET.. never seen a non-zero
	 * value appear in a mmio-trace yet..
	 */
#if 0
	tmp = nv_rd32(dev, UNK_84);
	nv_wr32(dev, NV_PFIFO_CACHE1_GET, tmp /* ??? */);
	nv_wr32(dev, NV_PFIFO_CACHE1_PUT, tmp /* ??? */);
#endif

	/* Don't clobber the TIMEOUT_ENABLED flag when restoring from RAMFC */
	tmp  = nv_rd32(dev, NV04_PFIFO_DMA_TIMESLICE) & ~0x1FFFF;
	tmp |= RAMFC_RD(DMA_TIMESLICE) & 0x1FFFF;
	nv_wr32(dev, NV04_PFIFO_DMA_TIMESLICE, tmp);

	dev_priv->engine.instmem.finish_access(dev);

	/* Set channel active, and in DMA mode */
	nv_wr32(dev, NV03_PFIFO_CACHE1_PUSH1,
		NV40_PFIFO_CACHE1_PUSH1_DMA | chan->id);

	/* Reset DMA_CTL_AT_INFO to INVALID */
	tmp = nv_rd32(dev, NV04_PFIFO_CACHE1_DMA_CTL) & ~(1 << 31);
	nv_wr32(dev, NV04_PFIFO_CACHE1_DMA_CTL, tmp);

	return 0;
}

int
nv40_fifo_save_context(struct nouveau_channel *chan)
{
	struct drm_device *dev = chan->dev;
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	uint32_t tmp;

	dev_priv->engine.instmem.prepare_access(dev, true);

	RAMFC_WR(DMA_PUT,        nv_rd32(dev, NV04_PFIFO_CACHE1_DMA_PUT));
	RAMFC_WR(DMA_GET,        nv_rd32(dev, NV04_PFIFO_CACHE1_DMA_GET));
	RAMFC_WR(REF_CNT,        nv_rd32(dev, NV10_PFIFO_CACHE1_REF_CNT));
	RAMFC_WR(DMA_INSTANCE,   nv_rd32(dev, NV04_PFIFO_CACHE1_DMA_INSTANCE));
	RAMFC_WR(DMA_DCOUNT,     nv_rd32(dev, NV04_PFIFO_CACHE1_DMA_DCOUNT));
	RAMFC_WR(DMA_STATE,      nv_rd32(dev, NV04_PFIFO_CACHE1_DMA_STATE));

	tmp  = nv_rd32(dev, NV04_PFIFO_CACHE1_DMA_FETCH);
	tmp |= nv_rd32(dev, 0x2058) & 0x30000000;
	RAMFC_WR(DMA_FETCH	  , tmp);

	RAMFC_WR(ENGINE,         nv_rd32(dev, NV04_PFIFO_CACHE1_ENGINE));
	RAMFC_WR(PULL1_ENGINE,   nv_rd32(dev, NV04_PFIFO_CACHE1_PULL1));
	RAMFC_WR(ACQUIRE_VALUE,  nv_rd32(dev, NV10_PFIFO_CACHE1_ACQUIRE_VALUE));
	tmp = nv_rd32(dev, NV10_PFIFO_CACHE1_ACQUIRE_TIMESTAMP);
	RAMFC_WR(ACQUIRE_TIMESTAMP, tmp);
	RAMFC_WR(ACQUIRE_TIMEOUT,
			nv_rd32(dev, NV10_PFIFO_CACHE1_ACQUIRE_TIMEOUT));
	RAMFC_WR(SEMAPHORE,      nv_rd32(dev, NV10_PFIFO_CACHE1_SEMAPHORE));

	/* NVIDIA read 0x3228 first, then write DMA_GET here.. maybe something
	 * more involved depending on the value of 0x3228?
	 */
	RAMFC_WR(DMA_SUBROUTINE, nv_rd32(dev, NV04_PFIFO_CACHE1_DMA_GET));

	RAMFC_WR(GRCTX_INSTANCE, nv_rd32(dev, NV40_PFIFO_GRCTX_INSTANCE));

	/* No idea what the below is for exactly, ripped from a mmio-trace */
	RAMFC_WR(UNK_40,         nv_rd32(dev, NV40_PFIFO_UNK32E4));

	/* NVIDIA do this next line twice.. bug? */
	RAMFC_WR(UNK_44,         nv_rd32(dev, 0x32e8));
	RAMFC_WR(UNK_4C,         nv_rd32(dev, 0x2088));
	RAMFC_WR(UNK_50,         nv_rd32(dev, 0x3300));

#if 0 /* no real idea which is PUT/GET in UNK_48.. */
	tmp  = nv_rd32(dev, NV04_PFIFO_CACHE1_GET);
	tmp |= (nv_rd32(dev, NV04_PFIFO_CACHE1_PUT) << 16);
	RAMFC_WR(UNK_48           , tmp);
#endif

	dev_priv->engine.instmem.finish_access(dev);
	return 0;
}

int
nv40_fifo_init(struct drm_device *dev)
{
	int ret;

	ret = nv04_fifo_init(dev);
	if (ret)
		return ret;

	nv_wr32(dev, NV04_PFIFO_DMA_TIMESLICE, 0x2101ffff);
	return 0;
}
