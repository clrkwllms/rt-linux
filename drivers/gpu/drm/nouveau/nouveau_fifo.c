/*
 * Copyright 2005-2006 Stephane Marchesin
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * PRECISION INSIGHT AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#include "drmP.h"
#include "drm.h"
#include "nouveau_drv.h"
#include "nouveau_drm.h"
#include "nouveau_dma.h"


/* returns the size of fifo context */
int nouveau_fifo_ctx_size(struct drm_device *dev)
{
	struct drm_nouveau_private *dev_priv=dev->dev_private;

	if (dev_priv->card_type >= NV_40)
		return 128;
	else if (dev_priv->card_type >= NV_17)
		return 64;
	else
		return 32;
}

/***********************************
 * functions doing the actual work
 ***********************************/

static int nouveau_fifo_instmem_configure(struct drm_device *dev)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;

	nv_wr32(dev, NV03_PFIFO_RAMHT,
			(0x03 << 24) /* search 128 */ |
			((dev_priv->ramht_bits - 9) << 16) |
			(dev_priv->ramht_offset >> 8)
			);

	nv_wr32(dev, NV03_PFIFO_RAMRO, dev_priv->ramro_offset>>8);

	switch(dev_priv->card_type)
	{
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
			nv_wr32(dev, NV40_PFIFO_RAMFC, 0x30002);
			break;
		case NV_44:
			nv_wr32(dev, NV40_PFIFO_RAMFC,
				((nouveau_mem_fb_amount(dev) - 512 * 1024 +
				 dev_priv->ramfc_offset) >> 16) | (2 << 16));
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
			nv_wr32(dev, NV03_PFIFO_RAMFC,
				dev_priv->ramfc_offset>>8);
			break;
	}

	return 0;
}

int nouveau_fifo_init(struct drm_device *dev)
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

static int
nouveau_fifo_pushbuf_ctxdma_init(struct nouveau_channel *chan)
{
	struct drm_device *dev = chan->dev;
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nouveau_bo *pb = chan->pushbuf_bo;
	struct nouveau_gpuobj *pushbuf = NULL;
	uint32_t start = pb->bo.mem.mm_node->start << PAGE_SHIFT;
	uint32_t size = pb->bo.mem.size;
	int ret;

	if (pb->bo.mem.mem_type == TTM_PL_TT) {
		ret = nouveau_gpuobj_gart_dma_new(chan, start +
						  dev_priv->vm_gart_base, size,
						  NV_DMA_ACCESS_RO,
						  &pushbuf,
						  &chan->pushbuf_base);
	} else
	if (dev_priv->card_type != NV_04) {
		ret = nouveau_gpuobj_dma_new(chan, NV_CLASS_DMA_IN_MEMORY,
					     start, size, NV_DMA_ACCESS_RO,
					     NV_DMA_TARGET_VIDMEM, &pushbuf);
		chan->pushbuf_base = 0;
	} else {
		/* NV04 cmdbuf hack, from original ddx.. not sure of it's
		 * exact reason for existing :)  PCI access to cmdbuf in
		 * VRAM.
		 */
		ret = nouveau_gpuobj_dma_new(chan, NV_CLASS_DMA_IN_MEMORY,
					     start +
					       drm_get_resource_start(dev, 1),
					     size, NV_DMA_ACCESS_RO,
					     NV_DMA_TARGET_PCI, &pushbuf);
		chan->pushbuf_base = 0;
	}

	if ((ret = nouveau_gpuobj_ref_add(dev, chan, 0, pushbuf,
					  &chan->pushbuf))) {
		NV_ERROR(dev, "Error referencing push buffer ctxdma: %d\n", ret);
		if (pushbuf != dev_priv->gart_info.sg_ctxdma)
			nouveau_gpuobj_del(dev, &pushbuf);
		return ret;
	}

	return 0;
}

static struct nouveau_bo *
nouveau_fifo_user_pushbuf_alloc(struct drm_device *dev)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nouveau_config *config = &dev_priv->config;
	struct nouveau_bo *pushbuf = NULL;
	int pb_min_size = max(NV03_FIFO_SIZE, PAGE_SIZE);
	int ret;

	/* Defaults for unconfigured values */
	if (!config->cmdbuf.location)
		config->cmdbuf.location = TTM_PL_FLAG_TT;
	if (!config->cmdbuf.size || config->cmdbuf.size < pb_min_size)
		config->cmdbuf.size = 65536;

	ret = nouveau_bo_new(dev, NULL, config->cmdbuf.size, 0,
			     config->cmdbuf.location, 0, 0x0000,
			     false, true, &pushbuf);
	if (ret) {
		NV_ERROR(dev, "error allocating DMA push buffer: %d\n", ret);
		return NULL;
	}

	ret = nouveau_bo_pin(pushbuf, config->cmdbuf.location);
	if (ret) {
		NV_ERROR(dev, "error pinning DMA push buffer: %d\n", ret);
		nouveau_bo_ref(NULL, &pushbuf);
		return NULL;
	}

	return pushbuf;
}

/* allocates and initializes a fifo for user space consumption */
int
nouveau_fifo_alloc(struct drm_device *dev, struct nouveau_channel **chan_ret,
		   struct drm_file *file_priv,
		   uint32_t vram_handle, uint32_t tt_handle)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nouveau_engine *engine = &dev_priv->engine;
	struct nouveau_channel *chan;
	int channel, user;
	int ret;

	/*
	 * Alright, here is the full story
	 * Nvidia cards have multiple hw fifo contexts (praise them for that,
	 * no complicated crash-prone context switches)
	 * We allocate a new context for each app and let it write to it directly
	 * (woo, full userspace command submission !)
	 * When there are no more contexts, you lost
	 */
	for (channel = 0; channel < engine->fifo.channels; channel++) {
		if (dev_priv->fifos[channel] == NULL)
			break;
	}

	/* no more fifos. you lost. */
	if (channel == engine->fifo.channels)
		return -EINVAL;

	dev_priv->fifos[channel] = kzalloc(sizeof(struct nouveau_channel),
					   GFP_KERNEL);
	if (!dev_priv->fifos[channel])
		return -ENOMEM;
	dev_priv->fifo_alloc_count++;
	chan = dev_priv->fifos[channel];
	chan->dev = dev;
	chan->id = channel;
	chan->file_priv = file_priv;
	chan->vram_handle = vram_handle;
	chan->gart_handle = tt_handle;

	NV_INFO(dev, "Allocating FIFO number %d\n", channel);

	/* Allocate DMA push buffer */
	chan->pushbuf_bo = nouveau_fifo_user_pushbuf_alloc(dev);
	if (!chan->pushbuf_bo) {
		ret = -ENOMEM;
		NV_ERROR(dev, "pushbuf %d\n", ret);
		nouveau_fifo_free(chan);
		return ret;
	}

	/* Locate channel's user control regs */
	if (dev_priv->card_type < NV_40)
		user = NV03_USER(channel);
	else
	if (dev_priv->card_type < NV_50)
		user = NV40_USER(channel);
	else
		user = NV50_USER(channel);

	ret = drm_addmap(dev, drm_get_resource_start(dev, 0) + user,
			 PAGE_SIZE, _DRM_REGISTERS, _DRM_DRIVER |
			 _DRM_READ_ONLY, &chan->user);
	if (ret) {
		NV_ERROR(dev, "regs %d\n", ret);
		nouveau_fifo_free(chan);
		return ret;
	}
	chan->user_put = 0x40;
	chan->user_get = 0x44;

	/* Allocate space for per-channel fixed notifier memory */
	ret = nouveau_notifier_init_channel(chan);
	if (ret) {
		NV_ERROR(dev, "ntfy %d\n", ret);
		nouveau_fifo_free(chan);
		return ret;
	}

	/* Setup channel's default objects */
	ret = nouveau_gpuobj_channel_init(chan, vram_handle, tt_handle);
	if (ret) {
		NV_ERROR(dev, "gpuobj %d\n", ret);
		nouveau_fifo_free(chan);
		return ret;
	}

	/* Create a dma object for the push buffer */
	ret = nouveau_fifo_pushbuf_ctxdma_init(chan);
	if (ret) {
		NV_ERROR(dev, "pbctxdma %d\n", ret);
		nouveau_fifo_free(chan);
		return ret;
	}

	engine->graph.fifo_access(dev, false);
	nouveau_wait_for_idle(dev);

	/* disable the fifo caches */
	nv_wr32(dev, NV03_PFIFO_CACHES, 0x00000000);
	nv_wr32(dev, NV04_PFIFO_CACHE1_DMA_PUSH,
			nv_rd32(dev, NV04_PFIFO_CACHE1_DMA_PUSH) & ~0x1);
	nv_wr32(dev, NV03_PFIFO_CACHE1_PUSH0, 0x00000000);
	nv_wr32(dev, NV04_PFIFO_CACHE1_PULL0, 0x00000000);

	/* Create a graphics context for new channel */
	ret = engine->graph.create_context(chan);
	if (ret) {
		nouveau_fifo_free(chan);
		return ret;
	}

	/* Construct inital RAMFC for new channel */
	ret = engine->fifo.create_context(chan);
	if (ret) {
		nouveau_fifo_free(chan);
		return ret;
	}

	/* setup channel's default get/put values
	 * XXX: quite possibly extremely pointless..
	 */
	nvchan_wr32(chan->user_get, chan->pushbuf_base);
	nvchan_wr32(chan->user_put, chan->pushbuf_base);

	/* If this is the first channel, setup PFIFO ourselves.  For any
	 * other case, the GPU will handle this when it switches contexts.
	 */
	if (dev_priv->card_type < NV_50 &&
	    dev_priv->fifo_alloc_count == 1) {
		ret = engine->fifo.load_context(chan);
		if (ret) {
			nouveau_fifo_free(chan);
			return ret;
		}

		ret = engine->graph.load_context(chan);
		if (ret) {
			nouveau_fifo_free(chan);
			return ret;
		}
	}

	nv_wr32(dev, NV04_PFIFO_CACHE1_DMA_PUSH,
			nv_rd32(dev, NV04_PFIFO_CACHE1_DMA_PUSH) | 1);
	nv_wr32(dev, NV03_PFIFO_CACHE1_PUSH0, 0x00000001);
	nv_wr32(dev, NV04_PFIFO_CACHE1_PULL0, 0x00000001);
	nv_wr32(dev, NV04_PFIFO_CACHE1_PULL1, 0x00000001);

	/* reenable the fifo caches */
	nv_wr32(dev, NV03_PFIFO_CACHES, 1);

	engine->graph.fifo_access(dev, true);

	ret = nouveau_dma_init(chan);
	if (!ret)
		ret = nouveau_fence_init(chan);
	if (ret) {
		nouveau_fifo_free(chan);
		return ret;
	}

	NV_INFO(dev, "%s: initialised FIFO %d\n", __func__, channel);
	*chan_ret = chan;
	return 0;
}

int
nouveau_channel_idle(struct nouveau_channel *chan)
{
	struct drm_device *dev = chan->dev;
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nouveau_engine *engine = &dev_priv->engine;
	uint32_t caches;
	int idle;

	if (!chan) {
		NV_ERROR(dev, "no channel...\n");
		return 1;
	}

	caches = nv_rd32(dev, NV03_PFIFO_CACHES);
	nv_wr32(dev, NV03_PFIFO_CACHES, caches & ~1);

	if (engine->fifo.channel_id(dev) != chan->id) {
		struct nouveau_gpuobj *ramfc =
			chan->ramfc ? chan->ramfc->gpuobj : NULL;

		if (!ramfc) {
			NV_ERROR(dev, "No RAMFC for channel %d\n", chan->id);
			return 1;
		}

		engine->instmem.prepare_access(dev, false);
		if (nv_ro32(dev, ramfc, 0) != nv_ro32(dev, ramfc, 1))
			idle = 0;
		else
			idle = 1;
		engine->instmem.finish_access(dev);
	} else {
		idle = (nv_rd32(dev, NV04_PFIFO_CACHE1_DMA_GET) ==
			nv_rd32(dev, NV04_PFIFO_CACHE1_DMA_PUT));
	}

	nv_wr32(dev, NV03_PFIFO_CACHES, caches);
	return idle;
}

/* stops a fifo */
void nouveau_fifo_free(struct nouveau_channel *chan)
{
	struct drm_device *dev = chan->dev;
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nouveau_engine *engine = &dev_priv->engine;
	uint64_t t_start;
	bool timeout = false;
	int ret;

	NV_INFO(dev, "%s: freeing fifo %d\n", __func__, chan->id);

	/* Give the channel a chance to idle, wait 2s (hopefully) */
	t_start = engine->timer.read(dev);
	while (!nouveau_channel_idle(chan)) {
		if (engine->timer.read(dev) - t_start > 2000000000ULL) {
			NV_ERROR(dev, "Failed to idle channel %d.  "
				      "Prepare for strangeness..\n", chan->id);
			timeout = true;
			break;
		}
	}

	/* Wait on a fence until channel goes idle, this ensures the engine
	 * has finished with the last push buffer completely before we destroy
	 * the channel.
	 */
	if (!timeout) {
		struct nouveau_fence *fence = NULL;

		ret = nouveau_fence_new(chan, &fence, true);
		if (ret == 0) {
			ret = nouveau_fence_wait(fence, NULL, false, false);
			nouveau_fence_unref((void *)&fence);
		}

		if (ret) {
			NV_ERROR(dev, "Failed to fence channel %d.  "
				      "Prepare for strangeness..\n", chan->id);
			timeout = true;
		}
	}

	/* Ensure all outstanding fences are signaled.  They should be if the
	 * above attempts at idling were OK, but if we failed this'll tell TTM
	 * we're done with the buffers.
	 */
	nouveau_fence_fini(chan);

	/* disable the fifo caches */
	nv_wr32(dev, NV03_PFIFO_CACHES, 0x00000000);
	nv_wr32(dev, NV04_PFIFO_CACHE1_DMA_PUSH,
			nv_rd32(dev, NV04_PFIFO_CACHE1_DMA_PUSH) & ~0x1);
	nv_wr32(dev, NV03_PFIFO_CACHE1_PUSH0, 0x00000000);
	nv_wr32(dev, NV04_PFIFO_CACHE1_PULL0, 0x00000000);

	// FIXME XXX needs more code

	engine->fifo.destroy_context(chan);

	/* Cleanup PGRAPH state */
	engine->graph.destroy_context(chan);

	/* reenable the fifo caches */
	nv_wr32(dev, NV04_PFIFO_CACHE1_DMA_PUSH,
			nv_rd32(dev, NV04_PFIFO_CACHE1_DMA_PUSH) | 1);
	nv_wr32(dev, NV03_PFIFO_CACHE1_PUSH0, 0x00000001);
	nv_wr32(dev, NV04_PFIFO_CACHE1_PULL0, 0x00000001);
	nv_wr32(dev, NV03_PFIFO_CACHES, 0x00000001);

	/* Deallocate push buffer */
	nouveau_gpuobj_ref_del(dev, &chan->pushbuf);
	nouveau_bo_ref(NULL, &chan->pushbuf_bo);

	/* Destroy objects belonging to the channel */
	nouveau_gpuobj_channel_takedown(chan);

	nouveau_notifier_takedown_channel(chan);

	if (chan->user)
		drm_rmmap(dev, chan->user);

	dev_priv->fifos[chan->id] = NULL;
	dev_priv->fifo_alloc_count--;
	kfree(chan);
}

/* cleanups all the fifos from file_priv */
void nouveau_fifo_cleanup(struct drm_device *dev, struct drm_file *file_priv)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nouveau_engine *engine = &dev_priv->engine;
	int i;

	NV_DEBUG(dev, "clearing FIFO enables from file_priv\n");
	for(i = 0; i < engine->fifo.channels; i++) {
		struct nouveau_channel *chan = dev_priv->fifos[i];

		if (chan && chan->file_priv == file_priv)
			nouveau_fifo_free(chan);
	}
}

int
nouveau_fifo_owner(struct drm_device *dev, struct drm_file *file_priv,
		   int channel)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nouveau_engine *engine = &dev_priv->engine;

	if (channel >= engine->fifo.channels)
		return 0;
	if (dev_priv->fifos[channel] == NULL)
		return 0;
	return (dev_priv->fifos[channel]->file_priv == file_priv);
}

/***********************************
 * ioctls wrapping the functions
 ***********************************/

static int nouveau_ioctl_fifo_alloc(struct drm_device *dev, void *data,
				    struct drm_file *file_priv)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct drm_nouveau_channel_alloc *init = data;
	struct drm_map_list *entry;
	struct nouveau_channel *chan;
	int res;

	NOUVEAU_CHECK_INITIALISED_WITH_RETURN;

	if (init->fb_ctxdma_handle == ~0 || init->tt_ctxdma_handle == ~0)
		return -EINVAL;

	res = nouveau_fifo_alloc(dev, &chan, file_priv,
				 init->fb_ctxdma_handle,
				 init->tt_ctxdma_handle);
	if (res)
		return res;
	init->channel  = chan->id;

	init->subchan[0].handle = NvM2MF;
	if (dev_priv->card_type < NV_50)
		init->subchan[0].grclass = 0x5039;
	else
		init->subchan[0].grclass = 0x0039;
	init->nr_subchan = 1;

	/* and the notifier block */
	entry = drm_find_matching_map(dev, chan->notifier_map);
	if (!entry) {
		nouveau_fifo_free(chan);
		return -EFAULT;
	}

	init->notifier = entry->user_token;
	init->notifier_size = chan->notifier_bo->bo.mem.size;

	return 0;
}

static int nouveau_ioctl_fifo_free(struct drm_device *dev, void *data,
				   struct drm_file *file_priv)
{
	struct drm_nouveau_channel_free *cfree = data;
	struct nouveau_channel *chan;

	NOUVEAU_CHECK_INITIALISED_WITH_RETURN;
	NOUVEAU_GET_USER_CHANNEL_WITH_RETURN(cfree->channel, file_priv, chan);

	nouveau_fifo_free(chan);
	return 0;
}

/***********************************
 * finally, the ioctl table
 ***********************************/

struct drm_ioctl_desc nouveau_ioctls[] = {
	DRM_IOCTL_DEF(DRM_NOUVEAU_CARD_INIT, nouveau_ioctl_card_init, DRM_AUTH),
	DRM_IOCTL_DEF(DRM_NOUVEAU_GETPARAM, nouveau_ioctl_getparam, DRM_AUTH),
	DRM_IOCTL_DEF(DRM_NOUVEAU_SETPARAM, nouveau_ioctl_setparam, DRM_AUTH|DRM_MASTER|DRM_ROOT_ONLY),
	DRM_IOCTL_DEF(DRM_NOUVEAU_CHANNEL_ALLOC, nouveau_ioctl_fifo_alloc, DRM_AUTH),
	DRM_IOCTL_DEF(DRM_NOUVEAU_CHANNEL_FREE, nouveau_ioctl_fifo_free, DRM_AUTH),
	DRM_IOCTL_DEF(DRM_NOUVEAU_GROBJ_ALLOC, nouveau_ioctl_grobj_alloc, DRM_AUTH),
	DRM_IOCTL_DEF(DRM_NOUVEAU_NOTIFIEROBJ_ALLOC, nouveau_ioctl_notifier_alloc, DRM_AUTH),
	DRM_IOCTL_DEF(DRM_NOUVEAU_GPUOBJ_FREE, nouveau_ioctl_gpuobj_free, DRM_AUTH),
	DRM_IOCTL_DEF(DRM_NOUVEAU_SUSPEND, nouveau_ioctl_suspend, DRM_AUTH),
	DRM_IOCTL_DEF(DRM_NOUVEAU_RESUME, nouveau_ioctl_resume, DRM_AUTH),
	DRM_IOCTL_DEF(DRM_NOUVEAU_GEM_NEW, nouveau_gem_ioctl_new, DRM_AUTH),
	DRM_IOCTL_DEF(DRM_NOUVEAU_GEM_PUSHBUF, nouveau_gem_ioctl_pushbuf, DRM_AUTH),
	DRM_IOCTL_DEF(DRM_NOUVEAU_GEM_PUSHBUF_CALL, nouveau_gem_ioctl_pushbuf_call, DRM_AUTH),
	DRM_IOCTL_DEF(DRM_NOUVEAU_GEM_PIN, nouveau_gem_ioctl_pin, DRM_AUTH),
	DRM_IOCTL_DEF(DRM_NOUVEAU_GEM_UNPIN, nouveau_gem_ioctl_unpin, DRM_AUTH),
	DRM_IOCTL_DEF(DRM_NOUVEAU_GEM_CPU_PREP, nouveau_gem_ioctl_cpu_prep, DRM_AUTH),
	DRM_IOCTL_DEF(DRM_NOUVEAU_GEM_CPU_FINI, nouveau_gem_ioctl_cpu_fini, DRM_AUTH),
	DRM_IOCTL_DEF(DRM_NOUVEAU_GEM_TILE, nouveau_gem_ioctl_tile, DRM_AUTH),
	DRM_IOCTL_DEF(DRM_NOUVEAU_GEM_INFO, nouveau_gem_ioctl_info, DRM_AUTH),
};

int nouveau_max_ioctl = DRM_ARRAY_SIZE(nouveau_ioctls);
