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

#ifndef __NOUVEAU_DMA_H__
#define __NOUVEAU_DMA_H__

/* This is needed to avoid a race condition.
 * Otherwise you may be writing in the fetch area.
 * Is this large enough, as it's only 32 bytes, and the maximum
 * fetch size is 256 bytes?
 */
#define NOUVEAU_DMA_SKIPS 8

typedef enum {
	NvSubM2MF	= 0,
	NvSub2D		= 1,
	NvSubCtxSurf2D  = 1,
	NvSubGdiRect    = 2,
	NvSubImageBlit  = 3
} nouveau_subchannel_id_t;

typedef enum {
	NvM2MF		= 0x80000001,
	NvDmaFB		= 0x80000002,
	NvDmaTT		= 0x80000003,
	NvDmaVRAM	= 0x80000004,
	NvDmaGART	= 0x80000005,
	NvNotify0       = 0x80000006,
	Nv2D		= 0x80000007,
	NvCtxSurf2D	= 0x80000008,
	NvRop		= 0x80000009,
	NvImagePatt	= 0x8000000a,
	NvClipRect	= 0x8000000b,
	NvGdiRect	= 0x8000000c,
	NvImageBlit	= 0x8000000d,

	/* G80+ display objects */
	NvEvoVRAM	= 0x01000000,
	NvEvoFB16	= 0x01000001,
	NvEvoFB32	= 0x01000002
} nouveau_object_handle_t;

#define NV_MEMORY_TO_MEMORY_FORMAT                                    0x00000039
#define NV_MEMORY_TO_MEMORY_FORMAT_NAME                               0x00000000
#define NV_MEMORY_TO_MEMORY_FORMAT_SET_REF                            0x00000050
#define NV_MEMORY_TO_MEMORY_FORMAT_NOP                                0x00000100
#define NV_MEMORY_TO_MEMORY_FORMAT_NOTIFY                             0x00000104
#define NV_MEMORY_TO_MEMORY_FORMAT_NOTIFY_STYLE_WRITE                 0x00000000
#define NV_MEMORY_TO_MEMORY_FORMAT_NOTIFY_STYLE_WRITE_LE_AWAKEN       0x00000001
#define NV_MEMORY_TO_MEMORY_FORMAT_DMA_NOTIFY                         0x00000180
#define NV_MEMORY_TO_MEMORY_FORMAT_DMA_SOURCE                         0x00000184
#define NV_MEMORY_TO_MEMORY_FORMAT_OFFSET_IN                          0x0000030c

#define NV50_MEMORY_TO_MEMORY_FORMAT                                  0x00005039
#define NV50_MEMORY_TO_MEMORY_FORMAT_UNK200                           0x00000200
#define NV50_MEMORY_TO_MEMORY_FORMAT_UNK21C                           0x0000021c
#define NV50_MEMORY_TO_MEMORY_FORMAT_OFFSET_IN_HIGH                   0x00000238
#define NV50_MEMORY_TO_MEMORY_FORMAT_OFFSET_OUT_HIGH                  0x0000023c

static inline int
RING_SPACE(struct nouveau_channel *chan, int size)
{
	if (chan->dma.free < size) {
		int ret;

		ret = nouveau_dma_wait(chan, size);
		if (ret)
			return ret;
	}

	chan->dma.free -= size;
	return 0;
}

static inline void
OUT_RING(struct nouveau_channel *chan, int data)
{
#ifdef NOUVEAU_DMA_DEBUG
	NV_INFO(chan->dev, "Ch%d/0x%08x: 0x%08x\n",
		chan->id, chan->dma.cur << 2, data);
#endif
	chan->dma.pushbuf[chan->dma.cur++] = data;
}

static inline void
BEGIN_RING(struct nouveau_channel *chan, int subc, int mthd, int size)
{
	OUT_RING(chan, (subc << 13) | (size << 18) | mthd);
}

#define READ_GET() ((nvchan_rd32(chan->user_get) - chan->pushbuf_base) >> 2)

#define WRITE_PUT(val) do {                                                    \
	volatile uint32_t tmp;                                                 \
	DRM_MEMORYBARRIER();                                                   \
	tmp = chan->dma.pushbuf[0];                                            \
	nvchan_wr32(chan->user_put, ((val) << 2) + chan->pushbuf_base);        \
	chan->dma.put = (val);                                                 \
} while (0)

static inline void
FIRE_RING(struct nouveau_channel *chan)
{
#ifdef NOUVEAU_DMA_DEBUG
	NV_INFO(chan->dev, "Ch%d/0x%08x: PUSH!\n",
		chan->id, chan->dma.cur << 2);
#endif
	if (chan->dma.cur == chan->dma.put)
		return;
	chan->accel_done = true;

	WRITE_PUT(chan->dma.cur);
}

static inline void
WIND_RING(struct nouveau_channel *chan)
{
	chan->dma.cur = chan->dma.put;
}

#endif
