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

#include "nv50_display.h"
#include "nouveau_crtc.h"
#include "nouveau_encoder.h"
#include "nouveau_connector.h"
#include "nouveau_fb.h"
#include "drm_crtc_helper.h"

static int nv50_display_pre_init(struct drm_device *dev)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nv50_evo_channel *evo = &dev_priv->evo;
	uint32_t ram_amount;
	int ret, i;

	NV_DEBUG(dev, "\n");

	ret = nouveau_bo_new(dev, dev_priv->channel, 16384, 0,
			     TTM_PL_FLAG_VRAM, 0, 0x0000, false, true,
			     &evo->ramin);
	if (ret) {
		NV_ERROR(dev, "Error allocating EVO channel memory: %d\n", ret);
		return ret;
	}

	ret = nouveau_bo_map(evo->ramin);
	if (ret) {
		NV_ERROR(dev, "Error mapping EVO channel memory: %d\n", ret);
		nouveau_bo_ref(NULL, &evo->ramin);
		return ret;
	}

	evo->data = evo->ramin->kmap.virtual;
	for (i = 0; i < 16384/4; i++)
		evo->data[i] = 0;
	evo->offset  = evo->ramin->bo.mem.mm_node->start << PAGE_SHIFT;
	evo->hashtab = 0;
	evo->objects = evo->hashtab + 4096;
	evo->pushbuf = evo->objects + 4096;

	/* Setup enough of a "normal" channel to be able to use PFIFO
	 * DMA routines.
	 */
	ret = drm_addmap(dev, drm_get_resource_start(dev, 0) +
			 NV50_PDISPLAY_USER(0), PAGE_SIZE, _DRM_REGISTERS,
			 _DRM_DRIVER | _DRM_READ_ONLY, &evo->chan.user);
	if (ret) {
		NV_ERROR(dev, "Error mapping EVO control regs: %d\n", ret);
		nouveau_bo_ref(NULL, &evo->ramin);
		return ret;
	}
	evo->chan.dev = dev;
	evo->chan.id = -1;
	evo->chan.user_put = 0;
	evo->chan.user_get = 4;
	evo->chan.dma.max = (4096 /4) - 2;
	evo->chan.dma.put = 0;
	evo->chan.dma.cur = evo->chan.dma.put;
	evo->chan.dma.free = evo->chan.dma.max - evo->chan.dma.cur;
	evo->chan.dma.pushbuf = evo->ramin->kmap.virtual + evo->pushbuf;

	RING_SPACE(&evo->chan, NOUVEAU_DMA_SKIPS);
	for (i = 0; i < NOUVEAU_DMA_SKIPS; i++)
		OUT_RING  (&evo->chan, 0);

	nv_wr32(0x00610184, nv_rd32(0x00614004));
	/*
	 * I think the 0x006101XX range is some kind of main control area that enables things.
	 */
	/* CRTC? */
	nv_wr32(0x00610190 + 0 * 0x10, nv_rd32(0x00616100 + 0 * 0x800));
	nv_wr32(0x00610190 + 1 * 0x10, nv_rd32(0x00616100 + 1 * 0x800));
	nv_wr32(0x00610194 + 0 * 0x10, nv_rd32(0x00616104 + 0 * 0x800));
	nv_wr32(0x00610194 + 1 * 0x10, nv_rd32(0x00616104 + 1 * 0x800));
	nv_wr32(0x00610198 + 0 * 0x10, nv_rd32(0x00616108 + 0 * 0x800));
	nv_wr32(0x00610198 + 1 * 0x10, nv_rd32(0x00616108 + 1 * 0x800));
	nv_wr32(0x0061019c + 0 * 0x10, nv_rd32(0x0061610c + 0 * 0x800));
	nv_wr32(0x0061019c + 1 * 0x10, nv_rd32(0x0061610c + 1 * 0x800));
	/* DAC */
	nv_wr32(0x006101d0 + 0 * 0x4, nv_rd32(0x0061a000 + 0 * 0x800));
	nv_wr32(0x006101d0 + 1 * 0x4, nv_rd32(0x0061a000 + 1 * 0x800));
	nv_wr32(0x006101d0 + 2 * 0x4, nv_rd32(0x0061a000 + 2 * 0x800));
	/* SOR */
	nv_wr32(0x006101e0 + 0 * 0x4, nv_rd32(0x0061c000 + 0 * 0x800));
	nv_wr32(0x006101e0 + 1 * 0x4, nv_rd32(0x0061c000 + 1 * 0x800));
	nv_wr32(0x006101e0 + 2 * 0x4, nv_rd32(0x0061c000 + 2 * 0x800));
	nv_wr32(0x006101e0 + 3 * 0x4, nv_rd32(0x0061c000 + 3 * 0x800));
	/* Something not yet in use, tv-out maybe. */
	nv_wr32(0x006101f0 + 0 * 0x4, nv_rd32(0x0061e000 + 0 * 0x800));
	nv_wr32(0x006101f0 + 1 * 0x4, nv_rd32(0x0061e000 + 1 * 0x800));
	nv_wr32(0x006101f0 + 2 * 0x4, nv_rd32(0x0061e000 + 2 * 0x800));

	for (i = 0; i < 3; i++) {
		nv_wr32(NV50_PDISPLAY_DAC_REGS_DPMS_CTRL(i), 0x00550000 |
			NV50_PDISPLAY_DAC_REGS_DPMS_CTRL_PENDING);
		nv_wr32(NV50_PDISPLAY_DAC_REGS_CLK_CTRL1(i), 0x00000001);
	}

	/* This used to be in crtc unblank, but seems out of place there. */
	nv_wr32(NV50_PDISPLAY_UNK_380, 0);
	/* RAM is clamped to 256 MiB. */
	ram_amount = nouveau_mem_fb_amount(dev);
	NV_DEBUG(dev, "ram_amount %d\n", ram_amount);
	if (ram_amount > 256*1024*1024)
		ram_amount = 256*1024*1024;
	nv_wr32(NV50_PDISPLAY_RAM_AMOUNT, ram_amount - 1);
	nv_wr32(NV50_PDISPLAY_UNK_388, 0x150000);
	nv_wr32(NV50_PDISPLAY_UNK_38C, 0);

	return 0;
}

static int
nv50_display_init(struct drm_device *dev)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nouveau_timer_engine *ptimer = &dev_priv->engine.timer;
	struct nv50_evo_channel *evo = &dev_priv->evo;
	uint64_t start;
	uint32_t val;

	NV_DEBUG(dev, "\n");

	/* The precise purpose is unknown, i suspect it has something to do
	 * with text mode.
	 */
	if (nv_rd32(NV50_PDISPLAY_INTR) & 0x100) {
		nv_wr32(NV50_PDISPLAY_INTR, 0x100);
		nv_wr32(0x006194e8, nv_rd32(0x006194e8) & ~1);
		if (!nv_wait(0x006194e8, 2, 0)) {
			NV_ERROR(dev, "timeout: (0x6194e8 & 2) != 0\n");
			NV_ERROR(dev, "0x6194e8 = 0x%08x\n", nv_rd32(0x6194e8));
			return -EBUSY;
		}
	}

	/* taken from nv bug #12637, attempts to un-wedge the hw if it's
	 * stuck in some unspecified state
	 */
	start = ptimer->read(dev);
	nv_wr32(NV50_PDISPLAY_CHANNEL_STAT(0), 0x2b00);
	while ((val = nv_rd32(NV50_PDISPLAY_CHANNEL_STAT(0))) & 0x1e0000) {
		if ((val & 0x9f0000) == 0x20000)
			nv_wr32(NV50_PDISPLAY_CHANNEL_STAT(0), val | 0x800000);

		if ((val & 0x3f0000) == 0x30000)
			nv_wr32(NV50_PDISPLAY_CHANNEL_STAT(0), val | 0x200000);

		if (ptimer->read(dev) - start > 1000000000ULL) {
			NV_ERROR(dev, "timeout: (0x610200 & 0x1e0000) != 0\n");
			NV_ERROR(dev, "0x610200 = 0x%08x\n", val);
			return -EBUSY;
		}
	}

	nv_wr32(NV50_PDISPLAY_CTRL_STATE, NV50_PDISPLAY_CTRL_STATE_ENABLE);
	nv_wr32(NV50_PDISPLAY_CHANNEL_STAT(0), 0x1000b03);
	if (!nv_wait(NV50_PDISPLAY_CHANNEL_STAT(0), 0x40000000, 0x40000000)) {
		NV_ERROR(dev, "timeout: (0x610200 & 0x40000000) == 0x40000000\n");
		NV_ERROR(dev, "0x610200 = 0x%08x\n",
			  nv_rd32(NV50_PDISPLAY_CHANNEL_STAT(0)));
		return -EBUSY;
	}

	/* initialise display objects */
	if (dev_priv->chipset != 0x50) {
		evo->data[evo->hashtab/4 + 0] = NvEvoVM;
		evo->data[evo->hashtab/4 + 1] = (evo->objects << 10) | 2;
		evo->data[evo->objects/4 + 0] = 0x1e99003d;
		evo->data[evo->objects/4 + 1] = 0xffffffff;
		evo->data[evo->objects/4 + 2] = 0x00000000;
		evo->data[evo->objects/4 + 3] = 0x00000000;
		evo->data[evo->objects/4 + 4] = 0x00000000;
		evo->data[evo->objects/4 + 5] = 0x00010000;
	}
	evo->data[evo->hashtab/4 + 2] = NvEvoVRAM;
	evo->data[evo->hashtab/4 + 3] = ((evo->objects + 0x20) << 10) | 2;
	evo->data[(evo->objects + 0x20)/4 + 0] = 0x0019003d;
	evo->data[(evo->objects + 0x20)/4 + 1] = nouveau_mem_fb_amount(dev) - 1;
	evo->data[(evo->objects + 0x20)/4 + 2] = 0x00000000;
	evo->data[(evo->objects + 0x20)/4 + 3] = 0x00000000;
	evo->data[(evo->objects + 0x20)/4 + 4] = 0x00000000;
	evo->data[(evo->objects + 0x20)/4 + 5] = 0x00010000;

	nv_wr32(NV50_PDISPLAY_OBJECTS, ((evo->offset + evo->hashtab) >> 8) | 9);

	/* initialise fifo */
	nv_wr32(NV50_PDISPLAY_CHANNEL_DMA_CB(0),
		((evo->offset + evo->pushbuf) >> 8) |
		NV50_PDISPLAY_CHANNEL_DMA_CB_LOCATION_VRAM |
		NV50_PDISPLAY_CHANNEL_DMA_CB_VALID);
	nv_wr32(NV50_PDISPLAY_CHANNEL_UNK2(0), 0x00010000);
	nv_wr32(NV50_PDISPLAY_CHANNEL_UNK3(0), 0x00000002);
	if (!nv_wait(0x610200, 0x80000000, 0x00000000)) {
		NV_ERROR(dev, "timeout: (0x610200 & 0x80000000) == 0\n");
		NV_ERROR(dev, "0x610200 = 0x%08x\n", nv_rd32(0x610200));
		return -EBUSY;
	}
	nv_wr32(NV50_PDISPLAY_CHANNEL_STAT(0),
		(nv_rd32(NV50_PDISPLAY_CHANNEL_STAT(0)) & ~0x00000003) |
		 NV50_PDISPLAY_CHANNEL_STAT_DMA_ENABLED);
	nv_wr32(NV50_PDISPLAY_USER_PUT(0), 0);
	nv_wr32(NV50_PDISPLAY_CHANNEL_STAT(0), 0x01000003 |
		NV50_PDISPLAY_CHANNEL_STAT_DMA_ENABLED);
	nv_wr32(0x610300, nv_rd32(0x610300) & ~1);

	RING_SPACE(&evo->chan, 11);
	BEGIN_RING(&evo->chan, 0, NV50_UNK84, 2);
	OUT_RING  (&evo->chan, 0x00000000);
	OUT_RING  (&evo->chan, 0x00000000);
	BEGIN_RING(&evo->chan, 0, NV50_CRTC0_BLANK_CTRL, 1);
	OUT_RING  (&evo->chan, NV50_CRTC0_BLANK_CTRL_BLANK);
	BEGIN_RING(&evo->chan, 0, NV50_CRTC0_UNK800, 1);
	OUT_RING  (&evo->chan, 0);
	BEGIN_RING(&evo->chan, 0, NV50_CRTC0_DISPLAY_START, 1);
	OUT_RING  (&evo->chan, 0);
	BEGIN_RING(&evo->chan, 0, NV50_CRTC0_UNK82C, 1);
	OUT_RING  (&evo->chan, 0);
	FIRE_RING (&evo->chan);
	if (!nv_wait(0x640004, 0xffffffff, evo->chan.dma.put << 2))
		NV_ERROR(dev, "evo pushbuf stalled\n");

	/* enable clock change interrupts. */
	nv_wr32(NV50_PDISPLAY_INTR_EN, (NV50_PDISPLAY_INTR_EN_CLK_UNK10 |
					NV50_PDISPLAY_INTR_EN_CLK_UNK20 |
					NV50_PDISPLAY_INTR_EN_CLK_UNK40));

	/* enable hotplug interrupts */
	nv_wr32(NV50_PCONNECTOR_HOTPLUG_CTRL, 0x7FFF7FFF);
//	nv_wr32(NV50_PCONNECTOR_HOTPLUG_INTR, 0x7FFF7FFF);


	return 0;
}

static int nv50_display_disable(struct drm_device *dev)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct drm_crtc *drm_crtc;
	int i;

	NV_DEBUG(dev, "\n");

	list_for_each_entry(drm_crtc, &dev->mode_config.crtc_list, head) {
		struct nouveau_crtc *crtc = to_nouveau_crtc(drm_crtc);

		nv50_crtc_blank(crtc, true);
	}

	RING_SPACE(&dev_priv->evo.chan, 2);
	BEGIN_RING(&dev_priv->evo.chan, 0, NV50_UPDATE_DISPLAY, 1);
	OUT_RING  (&dev_priv->evo.chan, 0);
	FIRE_RING (&dev_priv->evo.chan);

	/* Almost like ack'ing a vblank interrupt, maybe in the spirit of
	 * cleaning up?
	 */
	list_for_each_entry(drm_crtc, &dev->mode_config.crtc_list, head) {
		struct nouveau_crtc *crtc = to_nouveau_crtc(drm_crtc);
		uint32_t mask = NV50_PDISPLAY_INTR_VBLANK_CRTC(crtc->index);

		if (!crtc->base.enabled)
			continue;

		nv_wr32(NV50_PDISPLAY_INTR, mask);
		if (!nv_wait(NV50_PDISPLAY_INTR, mask, mask)) {
			NV_ERROR(dev, "timeout: (0x610024 & 0x%08x) == "
				      "0x%08x\n", mask, mask);
			NV_ERROR(dev, "0x610024 = 0x%08x\n",
				 nv_rd32(NV50_PDISPLAY_INTR));
		}
	}

	nv_wr32(NV50_PDISPLAY_CHANNEL_STAT(0), 0);
	nv_wr32(NV50_PDISPLAY_CTRL_STATE, 0);
	if (!nv_wait(NV50_PDISPLAY_CHANNEL_STAT(0), 0x1e0000, 0)) {
		NV_ERROR(dev, "timeout: (0x610200 & 0x1e0000) == 0\n");
		NV_ERROR(dev, "0x610200 = 0x%08x\n",
			  nv_rd32(NV50_PDISPLAY_CHANNEL_STAT(0)));
	}

	for (i = 0; i < NV50_PDISPLAY_SOR_REGS__LEN; i++) {
		if (!nv_wait(NV50_PDISPLAY_SOR_REGS_DPMS_STATE(i),
			     NV50_PDISPLAY_SOR_REGS_DPMS_STATE_WAIT, 0)) {
			NV_ERROR(dev, "timeout: SOR_DPMS_STATE_WAIT(%d) == 0\n", i);
			NV_ERROR(dev, "SOR_DPMS_STATE(%d) = 0x%08x\n", i,
				  nv_rd32(NV50_PDISPLAY_SOR_REGS_DPMS_STATE(i)));
		}
	}

	/* disable interrupts. */
	nv_wr32(NV50_PDISPLAY_INTR_EN, 0x00000000);

	/* disable hotplug interrupts */
	nv_wr32(NV50_PCONNECTOR_HOTPLUG_INTR, 0);

	return 0;
}

int nv50_display_create(struct drm_device *dev)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct parsed_dcb *dcb = dev_priv->vbios->dcb;
	uint32_t connector[16] = {};
	int ret, i;

	NV_DEBUG(dev, "\n");

	/* init basic kernel modesetting */
	drm_mode_config_init(dev);

	/* Initialise some optional connector properties. */
	drm_mode_create_scaling_mode_property(dev);
	drm_mode_create_dithering_property(dev);

	dev->mode_config.min_width = 0;
	dev->mode_config.min_height = 0;

	dev->mode_config.funcs = (void *)&nouveau_mode_config_funcs;

	dev->mode_config.max_width = 8192;
	dev->mode_config.max_height = 8192;

	dev->mode_config.fb_base = dev_priv->fb_phys;

	ret = nv50_display_pre_init(dev);
	if (ret)
		return ret;

	/* Create CRTC objects */
	for (i = 0; i < 2; i++)
		nv50_crtc_create(dev, i);

	/* We setup the encoders from the BIOS table */
	for (i = 0 ; i < dcb->entries; i++) {
		struct dcb_entry *entry = &dcb->entry[i];

		switch (entry->type) {
		case OUTPUT_TMDS:
		case OUTPUT_LVDS:
			nv50_sor_create(dev, entry);
			break;
		case OUTPUT_ANALOG:
			nv50_dac_create(dev, entry);
			break;
		default:
			NV_WARN(dev, "DCB encoder %d unknown\n", entry->type);
			continue;
		}

		connector[entry->i2c_index] |= (1 << entry->type);
	}

	/* Look at which encoders are attached to each i2c bus to
	 * determine which connectors are present.
	 */
	for (i = 0 ; i < dcb->entries; i++) {
		struct dcb_entry *entry = &dcb->entry[i];
		uint16_t encoders;
		int type;

		encoders = connector[entry->i2c_index];
		connector[entry->i2c_index] = 0;

		/* already done? */
		if (!encoders)
			continue;

		if (encoders & (1 << OUTPUT_TMDS)) {
			if (encoders & (1 << OUTPUT_ANALOG))
				type = DRM_MODE_CONNECTOR_DVII;
			else
				type = DRM_MODE_CONNECTOR_DVID;
		} else
		if (encoders & (1 << OUTPUT_ANALOG)) {
			type = DRM_MODE_CONNECTOR_VGA;
		} else
		if (encoders & (1 << OUTPUT_LVDS)) {
			type = DRM_MODE_CONNECTOR_LVDS;
		} else
			type = DRM_MODE_CONNECTOR_Unknown;

		if (type == DRM_MODE_CONNECTOR_Unknown)
			continue;

		nv50_connector_create(dev, entry->i2c_index, type);
	}

	ret = nv50_display_init(dev);
	if (ret)
		return ret;

	return 0;
}

int nv50_display_destroy(struct drm_device *dev)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;

	NV_DEBUG(dev, "\n");

	nv50_display_disable(dev);

	if (dev_priv->evo.ramin && dev_priv->evo.ramin->kmap.virtual)
		nouveau_bo_unmap(dev_priv->evo.ramin);
	nouveau_bo_ref(NULL, &dev_priv->evo.ramin);

	if (dev_priv->evo.chan.user) {
		drm_rmmap(dev, dev_priv->evo.chan.user);
		dev_priv->evo.chan.user = NULL;
	}

	drm_mode_config_cleanup(dev);

	return 0;
}

static void nv50_display_vclk_update(struct drm_device *dev)
{
	struct drm_encoder *drm_encoder;
	struct drm_crtc *drm_crtc;
	struct nouveau_encoder *encoder = NULL;
	struct nouveau_crtc *crtc = NULL;
	int crtc_index;
	uint32_t unk30 = nv_rd32(NV50_PDISPLAY_UNK30_CTRL);

	for (crtc_index = 0; crtc_index < 2; crtc_index++) {
		bool clock_change = false;
		bool clock_ack = false;

		if (crtc_index == 0 && (unk30 & NV50_PDISPLAY_UNK30_CTRL_UPDATE_VCLK0))
			clock_change = true;

		if (crtc_index == 1 && (unk30 & NV50_PDISPLAY_UNK30_CTRL_UPDATE_VCLK1))
			clock_change = true;

		if (clock_change)
			clock_ack = true;

#if 0
		if (dev_priv->last_crtc == crtc_index)
#endif
			clock_ack = true;

		list_for_each_entry(drm_crtc, &dev->mode_config.crtc_list, head) {
			crtc = to_nouveau_crtc(drm_crtc);
			if (crtc->index == crtc_index)
				break;
		}

		if (clock_change)
			nv50_crtc_set_clock(dev, crtc->index, crtc->mode->clock);

		NV_DEBUG(dev, "index %d clock_change %d clock_ack %d\n", crtc_index, clock_change, clock_ack);

		if (!clock_ack)
			continue;

		nv_wr32(NV50_PDISPLAY_CRTC_CLK_CTRL2(crtc->index), 0);

		list_for_each_entry(drm_encoder, &dev->mode_config.encoder_list, head) {
			encoder = to_nouveau_encoder(drm_encoder);

			if (!drm_encoder->crtc)
				continue;

			if (drm_encoder->crtc == drm_crtc)
				encoder->set_clock_mode(encoder, crtc->mode);
		}
	}
}

void
nv50_display_irq_handler_old(struct drm_device *dev)
{
	uint32_t super = nv_rd32(NV50_PDISPLAY_INTR);
	uint32_t state;

	NV_DEBUG(dev, "0x610024 = 0x%08x\n", super);

	if (super & 0x0000000c)
		nv_wr32(NV50_PDISPLAY_INTR, super & 0x0000000c);

	state = (super >> 4) & 7;
	if (state) {
		if (state == 2)
			nv50_display_vclk_update(dev);

		nv_wr32(NV50_PDISPLAY_INTR, super & 0x00000070);
		nv_wr32(NV50_PDISPLAY_UNK30_CTRL,
			NV50_PDISPLAY_UNK30_CTRL_PENDING);
	}
}

static int
nv50_display_irq_head(struct drm_device *dev, int *phead,
		      struct dcb_entry **pdcbent)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	uint32_t unk30 = nv_rd32(NV50_PDISPLAY_UNK30_CTRL);
	uint32_t dac = 0, sor = 0;
	int head, i, or;

	/* We're assuming that head 0 *or* head 1 will be active here,
	 * and not both.  I'm not sure if the hw will even signal both
	 * ever, but it definitely shouldn't for us as we commit each
	 * CRTC separately, and submission will be blocked by the GPU
	 * until we handle each in turn.
	 */
	NV_DEBUG(dev, "0x610030: 0x%08x\n", unk30);
	head = ffs((unk30 >> 9) & 3) - 1;
	if (head < 0) {
		NV_ERROR(dev, "no active heads: 0x%08x\n", nv_rd32(0x610030));
		return -EINVAL;
	}

	/* This assumes CRTCs are never bound to multiple encoders, which
	 * should be the case.
	 */
	for (i = 0; i < 3; i++) {
		if (nv_rd32(NV50_PDISPLAY_DAC_MODE_CTRL_P(i)) & (1 << head))
			dac |= (1 << i);
	}

	if (dev_priv->chipset < 0x90 || dev_priv->chipset == 0x92 ||
	    dev_priv->chipset == 0xa0) {
		for (i = 0; i < 4; i++) {
			if (nv_rd32(NV50_PDISPLAY_SOR_MODE_CTRL_P(i)) & (1 << head))
				sor |= (1 << i);
		}
	} else {
		for (i = 0; i < 4; i++) {
			if (nv_rd32(NV90_PDISPLAY_SOR_MODE_CTRL_P(i)) & (1 << head))
				sor |= (1 << i);
		}
	}

	NV_DEBUG(dev, "dac: 0x%08x, sor: 0x%08x\n", dac, sor);

	if (dac && sor) {
		NV_ERROR(dev, "multiple encoders: 0x%08x 0x%08x\n", dac, sor);
		return -1;
	} else
	if (dac) {
		or = ffs(dac) - 1;
		if (dac & ~(1 << or)) {
			NV_ERROR(dev, "multiple DAC: 0x%08x\n", dac);
			return -1;
		}
	} else
	if (sor) {
		or = ffs(sor) - 1;
		if (sor & ~(1 << or)) {
			NV_ERROR(dev, "multiple SOR: 0x%08x\n", sor);
			return -1;
		}
	} else {
		NV_ERROR(dev, "no encoders!\n");
		return -1;
	}

	for (i = 0; i < dev_priv->vbios->dcb->entries; i++) {
		struct dcb_entry *dcbent = &dev_priv->vbios->dcb->entry[i];

		if (dac && (dcbent->type != OUTPUT_ANALOG &&
			    dcbent->type != OUTPUT_TV))
			continue;
		else
		if (sor && (dcbent->type != OUTPUT_TMDS &&
			    dcbent->type != OUTPUT_LVDS))
			continue;

		if (dcbent->or & (1 << or)) {
			*phead = head;
			*pdcbent = dcbent;
			return 0;
		}
	}

	NV_ERROR(dev, "no DCB entry for %d %d\n", dac != 0, or);
	return 0;
}

static void
nv50_display_vblank_handler(struct drm_device *dev, uint32_t intr)
{
	nv_wr32(NV50_PDISPLAY_INTR, intr & NV50_PDISPLAY_INTR_VBLANK_CRTCn);
}

static void
nv50_display_unk10_handler(struct drm_device *dev)
{
	struct dcb_entry *dcbent;
	int head, ret;

	ret = nv50_display_irq_head(dev, &head, &dcbent);
	if (ret)
		goto ack;

	nv_wr32(0x619494, nv_rd32(0x619494) & ~8);

	nouveau_bios_run_display_table(dev, dcbent, -1);

ack:
	nv_wr32(NV50_PDISPLAY_INTR, NV50_PDISPLAY_INTR_CLK_UNK10);
	nv_wr32(0x610030, 0x80000000);
}

static void
nv50_display_unk20_handler(struct drm_device *dev)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nvbios *bios = &dev_priv->VBIOS;
	struct dcb_entry *dcbent;
	uint32_t tmp, pclk;
	int head, or, ret;

	ret = nv50_display_irq_head(dev, &head, &dcbent);
	if (ret)
		goto ack;
	or = ffs(dcbent->or) - 1;
	pclk = nv_rd32(NV50_PDISPLAY_CRTC_P(head, CLOCK)) & 0x3fffff;

	NV_DEBUG(dev, "head %d pxclk: %dKHz\n", head, pclk);

	nouveau_bios_run_display_table(dev, dcbent, -2);

	nv50_crtc_set_clock(dev, head, pclk);

	nouveau_bios_run_display_table(dev, dcbent, pclk);

	tmp = nv_rd32(NV50_PDISPLAY_CRTC_CLK_CTRL2(head));
	tmp &= ~0x000000f;
	nv_wr32(NV50_PDISPLAY_CRTC_CLK_CTRL2(head), tmp);

	if (dcbent->type != OUTPUT_ANALOG) {
		int tclk;

		if (dcbent->type == OUTPUT_LVDS)
			tclk = bios->fp.duallink_transition_clk;
		else
			tclk = 165000;

		tmp = nv_rd32(NV50_PDISPLAY_SOR_CLK_CTRL2(or));
		tmp &= ~0x00000f0f;
		if (pclk > tclk)
			tmp |= 0x00000101;
		nv_wr32(NV50_PDISPLAY_SOR_CLK_CTRL2(or), tmp);
	} else {
		nv_wr32(NV50_PDISPLAY_DAC_CLK_CTRL2(or), 0);
	}

ack:
	nv_wr32(NV50_PDISPLAY_INTR, NV50_PDISPLAY_INTR_CLK_UNK20);
	nv_wr32(0x610030, 0x80000000);
}

static void
nv50_display_unk40_handler(struct drm_device *dev)
{
	struct dcb_entry *dcbent;
	int head, pclk, ret;

	ret = nv50_display_irq_head(dev, &head, &dcbent);
	if (ret)
		goto ack;
	pclk = nv_rd32(NV50_PDISPLAY_CRTC_P(head, CLOCK)) & 0x3fffff;

	nouveau_bios_run_display_table(dev, dcbent, -pclk);

ack:
	nv_wr32(NV50_PDISPLAY_INTR, NV50_PDISPLAY_INTR_CLK_UNK40);
	nv_wr32(0x610030, 0x80000000);
	nv_wr32(0x619494, nv_rd32(0x619494) | 8);
}

void
nv50_display_irq_handler(struct drm_device *dev)
{
	while (nv_rd32(NV50_PMC_INTR_0) & NV50_PMC_INTR_0_DISPLAY) {
		uint32_t unk20 = nv_rd32(0x610020);
		uint32_t intr = nv_rd32(NV50_PDISPLAY_INTR);
		(void)unk20;

		if (!intr)
			break;
		NV_DEBUG(dev, "PDISPLAY_INTR 0x%08x\n", intr);

		if (intr & NV50_PDISPLAY_INTR_CLK_UNK10)
			nv50_display_unk10_handler(dev);
		else
		if (intr & NV50_PDISPLAY_INTR_CLK_UNK20)
			nv50_display_unk20_handler(dev);
		else
		if (intr & NV50_PDISPLAY_INTR_CLK_UNK40)
			nv50_display_unk40_handler(dev);
		else
		if (intr & NV50_PDISPLAY_INTR_VBLANK_CRTCn)
			nv50_display_vblank_handler(dev, intr);
		else {
			NV_ERROR(dev, "unknown PDISPLAY_INTR: 0x%08x\n", intr);
			nv_wr32(NV50_PDISPLAY_INTR, intr);
		}
	}
}
