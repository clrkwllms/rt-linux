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

#define NV40_RAMFC(c) (dev_priv->ramfc_offset + ((c) * NV40_RAMFC__SIZE))
#define NV40_RAMFC__SIZE 128
#define NV40_RAMFC_DMA_PUT                                       0x00
#define NV40_RAMFC_DMA_GET                                       0x04
#define NV40_RAMFC_REF_CNT                                       0x08
#define NV40_RAMFC_DMA_INSTANCE                                  0x0C
#define NV40_RAMFC_DMA_DCOUNT /* ? */                            0x10
#define NV40_RAMFC_DMA_STATE                                     0x14
#define NV40_RAMFC_DMA_FETCH                                     0x18
#define NV40_RAMFC_ENGINE                                        0x1C
#define NV40_RAMFC_PULL1_ENGINE                                  0x20
#define NV40_RAMFC_ACQUIRE_VALUE                                 0x24
#define NV40_RAMFC_ACQUIRE_TIMESTAMP                             0x28
#define NV40_RAMFC_ACQUIRE_TIMEOUT                               0x2C
#define NV40_RAMFC_SEMAPHORE                                     0x30
#define NV40_RAMFC_DMA_SUBROUTINE                                0x34
#define NV40_RAMFC_GRCTX_INSTANCE /* guess */                    0x38
#define NV40_RAMFC_DMA_TIMESLICE                                 0x3C
#define NV40_RAMFC_UNK_40                                        0x40
#define NV40_RAMFC_UNK_44                                        0x44
#define NV40_RAMFC_UNK_48                                        0x48
#define NV40_RAMFC_UNK_4C                                        0x4C
#define NV40_RAMFC_UNK_50                                        0x50

#define RAMFC_WR(offset, val) nv_wo32(dev, chan->ramfc->gpuobj, \
					 NV40_RAMFC_##offset/4, (val))
#define RAMFC_RD(offset)      nv_ro32(dev, chan->ramfc->gpuobj, \
					 NV40_RAMFC_##offset/4)

int
nv40_fifo_create_context(struct nouveau_channel *chan)
{
	struct drm_device *dev = chan->dev;
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	int ret;

	ret = nouveau_gpuobj_new_fake(dev, NV40_RAMFC(chan->id), ~0,
						NV40_RAMFC__SIZE,
						NVOBJ_FLAG_ZERO_ALLOC |
						NVOBJ_FLAG_ZERO_FREE,
						NULL, &chan->ramfc);
	if (ret)
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

static void
nv40_fifo_do_load_context(struct drm_device *dev, int chid)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	uint32_t fc = NV40_RAMFC(chid), tmp, tmp2;

	dev_priv->engine.instmem.prepare_access(dev, false);

	nv_wr32(dev, NV04_PFIFO_CACHE1_DMA_PUT, nv_ri32(dev, fc + 0));
	nv_wr32(dev, NV04_PFIFO_CACHE1_DMA_GET, nv_ri32(dev, fc + 4));
	nv_wr32(dev, NV10_PFIFO_CACHE1_REF_CNT, nv_ri32(dev, fc + 8));
	nv_wr32(dev, NV04_PFIFO_CACHE1_DMA_INSTANCE, nv_ri32(dev, fc + 12));
	nv_wr32(dev, NV04_PFIFO_CACHE1_DMA_DCOUNT, nv_ri32(dev, fc + 16));
	nv_wr32(dev, NV04_PFIFO_CACHE1_DMA_STATE, nv_ri32(dev, fc + 20));

	/* No idea what 0x2058 is.. */
	tmp   = nv_ri32(dev, fc + 24);
	tmp2  = nv_rd32(dev, 0x2058) & 0xFFF;
	tmp2 |= (tmp & 0x30000000);
	nv_wr32(dev, 0x2058, tmp2);
	tmp  &= ~0x30000000;
	nv_wr32(dev, NV04_PFIFO_CACHE1_DMA_FETCH, tmp);

	nv_wr32(dev, NV04_PFIFO_CACHE1_ENGINE, nv_ri32(dev, fc + 28));
	nv_wr32(dev, NV04_PFIFO_CACHE1_PULL1, nv_ri32(dev, fc + 32));
	nv_wr32(dev, NV10_PFIFO_CACHE1_ACQUIRE_VALUE, nv_ri32(dev, fc + 36));
	tmp = nv_ri32(dev, fc + 40);
	nv_wr32(dev, NV10_PFIFO_CACHE1_ACQUIRE_TIMESTAMP, tmp);
	nv_wr32(dev, NV10_PFIFO_CACHE1_ACQUIRE_TIMEOUT, nv_ri32(dev, fc + 44));
	nv_wr32(dev, NV10_PFIFO_CACHE1_SEMAPHORE, nv_ri32(dev, fc + 48));
	nv_wr32(dev, NV10_PFIFO_CACHE1_DMA_SUBROUTINE, nv_ri32(dev, fc + 52));
	nv_wr32(dev, NV40_PFIFO_GRCTX_INSTANCE, nv_ri32(dev, fc + 56));

	/* Don't clobber the TIMEOUT_ENABLED flag when restoring from RAMFC */
	tmp  = nv_rd32(dev, NV04_PFIFO_DMA_TIMESLICE) & ~0x1FFFF;
	tmp |= nv_ri32(dev, fc + 60) & 0x1FFFF;
	nv_wr32(dev, NV04_PFIFO_DMA_TIMESLICE, tmp);

	nv_wr32(dev, 0x32e4, nv_ri32(dev, fc + 64));
	/* NVIDIA does this next line twice... */
	nv_wr32(dev, 0x32e8, nv_ri32(dev, fc + 68));
	nv_wr32(dev, 0x2088, nv_ri32(dev, fc + 76));
	nv_wr32(dev, 0x3300, nv_ri32(dev, fc + 80));

	dev_priv->engine.instmem.finish_access(dev);
}

int
nv40_fifo_load_context(struct nouveau_channel *chan)
{
	struct drm_device *dev = chan->dev;
	uint32_t tmp;

	nv40_fifo_do_load_context(dev, chan->id);

	/* Set channel active, and in DMA mode */
	nv_wr32(dev, NV03_PFIFO_CACHE1_PUSH1,
		     NV40_PFIFO_CACHE1_PUSH1_DMA | chan->id);
	nv_wr32(dev, NV04_PFIFO_CACHE1_DMA_PUSH, 1);

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

static void
nv40_fifo_init_reset(struct drm_device *dev)
{
	int i;

	nv_wr32(dev, NV03_PMC_ENABLE,
		nv_rd32(dev, NV03_PMC_ENABLE) & ~NV_PMC_ENABLE_PFIFO);
	nv_wr32(dev, NV03_PMC_ENABLE,
		nv_rd32(dev, NV03_PMC_ENABLE) |  NV_PMC_ENABLE_PFIFO);

	nv_wr32(dev, 0x003224, 0x000f0078);
	nv_wr32(dev, 0x003210, 0x00000000);
	nv_wr32(dev, 0x003270, 0x00000000);
	nv_wr32(dev, 0x003240, 0x00000000);
	nv_wr32(dev, 0x003244, 0x00000000);
	nv_wr32(dev, 0x003258, 0x00000000);
	nv_wr32(dev, 0x002504, 0x00000000);
	for (i = 0; i < 16; i++)
		nv_wr32(dev, 0x002510 + (i * 4), 0x00000000);
	nv_wr32(dev, 0x00250c, 0x0000ffff);
	nv_wr32(dev, 0x002048, 0x00000000);
	nv_wr32(dev, 0x003228, 0x00000000);
	nv_wr32(dev, 0x0032e8, 0x00000000);
	nv_wr32(dev, 0x002410, 0x00000000);
	nv_wr32(dev, 0x002420, 0x00000000);
	nv_wr32(dev, 0x002058, 0x00000001);
	nv_wr32(dev, 0x00221c, 0x00000000);
	/* something with 0x2084, read/modify/write, no change */
	nv_wr32(dev, 0x002040, 0x000000ff);
	nv_wr32(dev, 0x002500, 0x00000000);
	nv_wr32(dev, 0x003200, 0x00000000);

	nv_wr32(dev, NV04_PFIFO_DMA_TIMESLICE, 0x2101ffff);
}

static void
nv40_fifo_init_ramxx(struct drm_device *dev)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;

	nv_wr32(dev, NV03_PFIFO_RAMHT, (0x03 << 24) /* search 128 */ |
				       ((dev_priv->ramht_bits - 9) << 16) |
				       (dev_priv->ramht_offset >> 8));
	nv_wr32(dev, NV03_PFIFO_RAMRO, dev_priv->ramro_offset>>8);

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
			  dev_priv->ramfc_offset) >> 16) | (3 << 16));
		break;
	}
}

static void
nv40_fifo_init_intr(struct drm_device *dev)
{
	nv_wr32(dev, 0x002100, 0xffffffff);
	nv_wr32(dev, 0x002140, 0xffffffff);
}

int
nv40_fifo_init(struct drm_device *dev)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nouveau_fifo_engine *pfifo = &dev_priv->engine.fifo;
	int i;

	nv40_fifo_init_reset(dev);
	nv40_fifo_init_ramxx(dev);

	nv40_fifo_do_load_context(dev, pfifo->channels - 1);
	nv_wr32(dev, NV03_PFIFO_CACHE1_PUSH1, pfifo->channels - 1);

	nv40_fifo_init_intr(dev);
	pfifo->enable(dev);
	pfifo->reassign(dev, true);

	for (i = 0; i < dev_priv->engine.fifo.channels; i++) {
		if (dev_priv->fifos[i]) {
			uint32_t mode = nv_rd32(dev, NV04_PFIFO_MODE);
			nv_wr32(dev, NV04_PFIFO_MODE, mode | (1 << i));
		}
	}

	return 0;
}
