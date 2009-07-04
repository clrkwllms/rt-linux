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
#include "nv50_grctx.h"

#define IS_G80 ((dev_priv->chipset & 0xf0) == 0x50)

static void
nv50_graph_init_reset(struct drm_device *dev)
{
	uint32_t pmc_e = NV_PMC_ENABLE_PGRAPH | (1 << 21);

	NV_DEBUG(dev, "\n");

	nv_wr32(NV03_PMC_ENABLE, nv_rd32(NV03_PMC_ENABLE) & ~pmc_e);
	nv_wr32(NV03_PMC_ENABLE, nv_rd32(NV03_PMC_ENABLE) |  pmc_e);
}

static void
nv50_graph_init_intr(struct drm_device *dev)
{
	NV_DEBUG(dev, "\n");

	nv_wr32(NV03_PGRAPH_INTR, 0xffffffff);
	nv_wr32(0x400138, 0xffffffff);
	nv_wr32(NV40_PGRAPH_INTR_EN, 0xffffffff);
}

static void
nv50_graph_init_regs__nv(struct drm_device *dev)
{
	NV_DEBUG(dev, "\n");

	nv_wr32(0x400804, 0xc0000000);
	nv_wr32(0x406800, 0xc0000000);
	nv_wr32(0x400c04, 0xc0000000);
	nv_wr32(0x401804, 0xc0000000);
	nv_wr32(0x405018, 0xc0000000);
	nv_wr32(0x402000, 0xc0000000);

	nv_wr32(0x400108, 0xffffffff);

	nv_wr32(0x400824, 0x00004000);
	nv_wr32(0x400500, 0x00000000);
}

static void
nv50_graph_init_regs(struct drm_device *dev)
{
	NV_DEBUG(dev, "\n");

	nv_wr32(NV04_PGRAPH_DEBUG_3, (1<<2) /* HW_CONTEXT_SWITCH_ENABLED */);
	nv_wr32(0x402ca8, 0x800);
}

static int
nv50_graph_init_ctxctl(struct drm_device *dev)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	uint32_t *voodoo = NULL;

	NV_DEBUG(dev, "\n");

	switch (dev_priv->chipset) {
	case 0x50:
		voodoo = nv50_ctxprog;
		break;
	case 0x84:
		voodoo = nv84_ctxprog;
		break;
	case 0x86:
		voodoo = nv86_ctxprog;
		break;
	case 0x92:
		voodoo = nv92_ctxprog;
		break;
	case 0x94:
	case 0x96:
		voodoo = nv94_ctxprog;
		break;
	case 0x98:
		voodoo = nv98_ctxprog;
		break;
	case 0xa0:
		voodoo = nva0_ctxprog;
		break;
	case 0xaa:
		voodoo = nvaa_ctxprog;
		break;
	case 0xac:
		voodoo = nvac_ctxprog;
		break;
	default:
		NV_ERROR(dev, "no ctxprog for chipset NV%02x\n", dev_priv->chipset);
		return -EINVAL;
	}

	nv_wr32(NV40_PGRAPH_CTXCTL_UCODE_INDEX, 0);
	while (*voodoo != ~0) {
		nv_wr32(NV40_PGRAPH_CTXCTL_UCODE_DATA, *voodoo);
		voodoo++;
	}

	nv_wr32(0x400320, 4);
	nv_wr32(NV40_PGRAPH_CTXCTL_CUR, 0);
	nv_wr32(NV20_PGRAPH_CHANNEL_CTX_POINTER, 0);

	return 0;
}

int
nv50_graph_init(struct drm_device *dev)
{
	int ret;

	NV_DEBUG(dev, "\n");

	nv50_graph_init_reset(dev);
	nv50_graph_init_regs__nv(dev);
	nv50_graph_init_regs(dev);
	nv50_graph_init_intr(dev);

	ret = nv50_graph_init_ctxctl(dev);
	if (ret)
		return ret;

	return 0;
}

void
nv50_graph_takedown(struct drm_device *dev)
{
	NV_DEBUG(dev, "\n");
}

void
nv50_graph_fifo_access(struct drm_device *dev, bool enabled)
{
	const uint32_t mask = 0x00010001;

	if (enabled)
		nv_wr32(0x400500, nv_rd32(0x400500) | mask);
	else
		nv_wr32(0x400500, nv_rd32(0x400500) & ~mask);
}

int
nv50_graph_create_context(struct nouveau_channel *chan)
{
	struct drm_device *dev = chan->dev;
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nouveau_gpuobj *ramin = chan->ramin->gpuobj;
	struct nouveau_gpuobj *ctx;
	uint32_t *ctxvals = NULL;
	int grctx_size = 0x70000, hdr;
	int ret, pos;

	NV_DEBUG(dev, "ch%d\n", chan->id);

	ret = nouveau_gpuobj_new_ref(dev, chan, NULL, 0, grctx_size, 0x1000,
				     NVOBJ_FLAG_ZERO_ALLOC |
				     NVOBJ_FLAG_ZERO_FREE, &chan->ramin_grctx);
	if (ret)
		return ret;
	ctx = chan->ramin_grctx->gpuobj;

	hdr = IS_G80 ? 0x200 : 0x20;
	dev_priv->engine.instmem.prepare_access(dev, true);
	INSTANCE_WR(ramin, (hdr + 0x00)/4, 0x00190002);
	INSTANCE_WR(ramin, (hdr + 0x04)/4, chan->ramin_grctx->instance +
					   grctx_size - 1);
	INSTANCE_WR(ramin, (hdr + 0x08)/4, chan->ramin_grctx->instance);
	INSTANCE_WR(ramin, (hdr + 0x0c)/4, 0);
	INSTANCE_WR(ramin, (hdr + 0x10)/4, 0);
	INSTANCE_WR(ramin, (hdr + 0x14)/4, 0x00010000);
	dev_priv->engine.instmem.finish_access(dev);

	switch (dev_priv->chipset) {
	case 0x50:
		ctxvals = nv50_ctxvals;
		break;
	case 0x84:
		ctxvals = nv84_ctxvals;
		break;
	case 0x86:
		ctxvals = nv86_ctxvals;
		break;
	case 0x92:
		ctxvals = nv92_ctxvals;
		break;
	case 0x94:
		ctxvals = nv94_ctxvals;
		break;
	case 0x96:
		ctxvals = nv96_ctxvals;
		break;
	case 0x98:
		ctxvals = nv98_ctxvals;
		break;
	case 0xa0:
		ctxvals = nva0_ctxvals;
		break;
	case 0xaa:
		ctxvals = nvaa_ctxvals;
		break;
	case 0xac:
		ctxvals = nvac_ctxvals;
		break;
	default:
		break;
	}

	dev_priv->engine.instmem.prepare_access(dev, true);

	pos = 0;
	while (*ctxvals) {
		int cnt = *ctxvals++;

		while (cnt--)
			INSTANCE_WR(ctx, pos++, *ctxvals);
		ctxvals++;
	}

	INSTANCE_WR(ctx, 0x00000/4, chan->ramin->instance >> 12);
	if ((dev_priv->chipset & 0xf0) == 0xa0)
		INSTANCE_WR(ctx, 0x00004/4, 0x00000000);
	else
		INSTANCE_WR(ctx, 0x0011c/4, 0x00000000);
	dev_priv->engine.instmem.finish_access(dev);

	return 0;
}

void
nv50_graph_destroy_context(struct nouveau_channel *chan)
{
	struct drm_device *dev = chan->dev;
	struct drm_nouveau_private *dev_priv = dev->dev_private;

	NV_DEBUG(dev, "ch%d\n", chan->id);

	if (chan->ramin && chan->ramin->gpuobj) {
		int i, hdr;

		hdr = IS_G80 ? 0x200 : 0x20;
		dev_priv->engine.instmem.prepare_access(dev, true);
		for (i=hdr; i<hdr+24; i+=4)
			INSTANCE_WR(chan->ramin->gpuobj, i/4, 0);
		dev_priv->engine.instmem.finish_access(dev);

		nouveau_gpuobj_ref_del(dev, &chan->ramin_grctx);
	}
}

static int
nv50_graph_transfer_context(struct drm_device *dev, uint32_t inst, int save)
{
	uint32_t old_cp, tv = 20000;
	int i;

	NV_DEBUG(dev, "inst=0x%08x, save=%d\n", inst, save);

	old_cp = nv_rd32(NV20_PGRAPH_CHANNEL_CTX_POINTER);
	nv_wr32(NV20_PGRAPH_CHANNEL_CTX_POINTER, inst);
	nv_wr32(0x400824, nv_rd32(0x400824) |
		 (save ? NV40_PGRAPH_CTXCTL_0310_XFER_SAVE :
			 NV40_PGRAPH_CTXCTL_0310_XFER_LOAD));
	nv_wr32(NV40_PGRAPH_CTXCTL_0304, NV40_PGRAPH_CTXCTL_0304_XFER_CTX);

	for (i = 0; i < tv; i++) {
		if (nv_rd32(NV40_PGRAPH_CTXCTL_030C) == 0)
			break;
	}
	nv_wr32(NV20_PGRAPH_CHANNEL_CTX_POINTER, old_cp);

	if (i == tv) {
		NV_ERROR(dev, "failed: inst=0x%08x save=%d\n", inst, save);
		NV_ERROR(dev, "0x40030C = 0x%08x\n",
			  nv_rd32(NV40_PGRAPH_CTXCTL_030C));
		return -EBUSY;
	}

	return 0;
}

int
nv50_graph_load_context(struct nouveau_channel *chan)
{
	struct drm_device *dev = chan->dev;
	uint32_t inst = chan->ramin->instance >> 12;
	int ret; (void)ret;

	NV_DEBUG(dev, "ch%d\n", chan->id);

#if 0
	if ((ret = nv50_graph_transfer_context(dev, inst, 0)))
		return ret;
#endif

	nv_wr32(NV20_PGRAPH_CHANNEL_CTX_POINTER, inst);
	nv_wr32(0x400320, 4);
	nv_wr32(NV40_PGRAPH_CTXCTL_CUR, inst | (1<<31));

	return 0;
}

int
nv50_graph_save_context(struct nouveau_channel *chan)
{
	struct drm_device *dev = chan->dev;
	uint32_t inst = chan->ramin->instance >> 12;

	NV_DEBUG(dev, "ch%d\n", chan->id);

	return nv50_graph_transfer_context(dev, inst, 1);
}
