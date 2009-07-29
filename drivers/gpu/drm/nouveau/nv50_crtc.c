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

#include "drmP.h"
#include "drm_mode.h"
#include "drm_crtc_helper.h"
#include "nouveau_reg.h"
#include "nouveau_drv.h"
#include "nouveau_hw.h"
#include "nouveau_encoder.h"
#include "nouveau_crtc.h"
#include "nouveau_fb.h"
#include "nouveau_fbcon.h"
#include "nouveau_connector.h"
#include "nv50_display.h"

#define NV50_LUT_INDEX(val, w) ((val << (8 - w)) | (val >> ((w << 1) - 8)))
static int
nv50_crtc_lut_load(struct nouveau_crtc *crtc)
{
	struct drm_device *dev = crtc->base.dev;
	uint32_t index = 0, i;
	void __iomem *lut = crtc->lut.nvbo->kmap.virtual;

	NV_DEBUG(dev, "\n");

	/* 16 bits, red, green, blue, unused, total of 64 bits per index */
	/* 10 bits lut, with 14 bits values. */
	switch (crtc->lut.depth) {
	case 15:
		/* R5G5B5 */
		for (i = 0; i < 32; i++) {
			index = NV50_LUT_INDEX(i, 5);
			writew(crtc->lut.r[i] >> 2, lut + 8*index + 0);
			writew(crtc->lut.g[i] >> 2, lut + 8*index + 2);
			writew(crtc->lut.b[i] >> 2, lut + 8*index + 4);
		}
		break;
	case 16:
		/* R5G6B5 */
		for (i = 0; i < 32; i++) {
			index = NV50_LUT_INDEX(i, 5);
			writew(crtc->lut.r[i] >> 2, lut + 8*index + 0);
			writew(crtc->lut.b[i] >> 2, lut + 8*index + 4);
		}

		/* Green has an extra bit. */
		for (i = 0; i < 64; i++) {
			index = NV50_LUT_INDEX(i, 6);
			writew(crtc->lut.g[i] >> 2, lut + 8*index + 2);
		}
		break;
	default:
		/* R8G8B8 */
		for (i = 0; i < 256; i++) {
			writew(crtc->lut.r[i] >> 2, lut + 8*i + 0);
			writew(crtc->lut.g[i] >> 2, lut + 8*i + 2);
			writew(crtc->lut.b[i] >> 2, lut + 8*i + 4);
		}

		if (crtc->lut.depth == 30) {
			writew(crtc->lut.r[i-1] >> 2, lut + 8*i + 0);
			writew(crtc->lut.g[i-1] >> 2, lut + 8*i + 2);
			writew(crtc->lut.b[i-1] >> 2, lut + 8*i + 4);
		}
		break;
	}

	return 0;
}

int
nv50_crtc_blank(struct nouveau_crtc *crtc, bool blanked)
{
	struct drm_device *dev = crtc->base.dev;
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nouveau_channel *evo = dev_priv->evo;
	int index = crtc->index;

	NV_DEBUG(dev, "index %d\n", crtc->index);
	NV_DEBUG(dev, "%s\n", blanked ? "blanked" : "unblanked");

	if (blanked) {
		crtc->cursor.hide(crtc, false);

		RING_SPACE(evo, dev_priv->chipset != 0x50 ? 7 : 5);
		BEGIN_RING(evo, 0, NV50_EVO_CRTC(index, CLUT_MODE), 2);
		OUT_RING  (evo, NV50_EVO_CRTC_CLUT_MODE_BLANK);
		OUT_RING  (evo, 0);
		if (dev_priv->chipset != 0x50) {
			BEGIN_RING(evo, 0, NV84_EVO_CRTC(index, CLUT_DMA), 1);
			OUT_RING  (evo, NV84_EVO_CRTC_CLUT_DMA_HANDLE_NONE);
		}

		BEGIN_RING(evo, 0, NV50_EVO_CRTC(index, FB_DMA), 1);
		OUT_RING  (evo, NV50_EVO_CRTC_FB_DMA_HANDLE_NONE);
	} else {
		crtc->cursor.set_offset(crtc, crtc->cursor.offset);
		if (crtc->cursor.visible)
			crtc->cursor.show(crtc, false);
		else
			crtc->cursor.hide(crtc, false);

		RING_SPACE(evo, dev_priv->chipset != 0x50 ? 10 : 8);
		BEGIN_RING(evo, 0, NV50_EVO_CRTC(index, CLUT_MODE), 2);
		OUT_RING  (evo, crtc->lut.depth == 8 ?
				NV50_EVO_CRTC_CLUT_MODE_OFF :
				NV50_EVO_CRTC_CLUT_MODE_ON);
		OUT_RING  (evo, (crtc->lut.nvbo->bo.mem.mm_node->start <<
				 PAGE_SHIFT) >> 8);
		if (dev_priv->chipset != 0x50) {
			BEGIN_RING(evo, 0, NV84_EVO_CRTC(index, CLUT_DMA), 1);
			OUT_RING  (evo, NvEvoVRAM);
		}

		BEGIN_RING(evo, 0, NV50_EVO_CRTC(index, FB_OFFSET), 2);
		OUT_RING  (evo, crtc->fb.offset >> 8);
		OUT_RING  (evo, 0);
		BEGIN_RING(evo, 0, NV50_EVO_CRTC(index, FB_DMA), 1);
		if (dev_priv->chipset != 0x50 && crtc->fb.tiled)
			if (crtc->fb.cpp == 2)
				OUT_RING(evo, NvEvoFB16);
			else
				OUT_RING(evo, NvEvoFB32);
		else
			OUT_RING(evo, NvEvoVRAM);
	}

	crtc->fb.blanked = blanked;
	return 0;
}

static int nv50_crtc_set_dither(struct nouveau_crtc *crtc, bool update)
{
	struct drm_device *dev = crtc->base.dev;
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nouveau_channel *evo = dev_priv->evo;
	int ret;

	NV_DEBUG(dev, "\n");

	ret = RING_SPACE(evo, 2 + (update ? 2 : 0));
	if (ret) {
		NV_ERROR(dev, "no space while setting dither\n");
		return ret;
	}

	BEGIN_RING(evo, 0, NV50_EVO_CRTC(crtc->index, DITHER_CTRL), 1);
	if (crtc->use_dithering)
		OUT_RING(evo, NV50_EVO_CRTC_DITHER_CTRL_ON);
	else
		OUT_RING(evo, NV50_EVO_CRTC_DITHER_CTRL_OFF);

	if (update) {
		BEGIN_RING(evo, 0, NV50_EVO_UPDATE, 1);
		OUT_RING  (evo, 0);
		FIRE_RING (evo);
	}

	return 0;
}

static struct nouveau_encoder *
nouveau_crtc_encoder_get(struct nouveau_crtc *crtc)
{
	struct drm_device *dev = crtc->base.dev;
	struct drm_encoder *drm_encoder;

	list_for_each_entry(drm_encoder, &dev->mode_config.encoder_list, head) {
		if (drm_encoder->crtc == &crtc->base)
			return nouveau_encoder(drm_encoder);
	}

	return NULL;
}

struct nouveau_connector *
nouveau_crtc_connector_get(struct nouveau_crtc *crtc)
{
	struct drm_device *dev = crtc->base.dev;
	struct drm_connector *drm_connector;
	struct nouveau_encoder *encoder;

	encoder = nouveau_crtc_encoder_get(crtc);
	if (!encoder)
		return NULL;

	list_for_each_entry(drm_connector, &dev->mode_config.connector_list, head) {
		if (drm_connector->encoder == &encoder->base)
			return nouveau_connector(drm_connector);
	}

	return NULL;
}

static int
nv50_crtc_set_scale(struct nouveau_crtc *crtc, int scaling_mode, bool update)
{
	struct nouveau_connector *connector = nouveau_crtc_connector_get(crtc);
	struct drm_device *dev = crtc->base.dev;
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nouveau_channel *evo = dev_priv->evo;
	struct drm_display_mode *native_mode = NULL;
	struct drm_display_mode *mode = &crtc->base.mode;
	uint32_t outX, outY, horiz, vert;
	int ret;

	NV_DEBUG(dev, "\n");

	if (!connector->digital)
		scaling_mode = DRM_MODE_SCALE_NON_GPU;

	switch (scaling_mode) {
	case DRM_MODE_SCALE_NO_SCALE:
	case DRM_MODE_SCALE_NON_GPU:
		break;
	default:
		if (!connector || !connector->native_mode) {
			NV_ERROR(dev, "No native mode, forcing panel scaling\n");
			scaling_mode = DRM_MODE_SCALE_NON_GPU;
		} else {
			native_mode = connector->native_mode;
		}
		break;
	}

	switch (scaling_mode) {
	case DRM_MODE_SCALE_ASPECT:
		horiz = (native_mode->hdisplay << 19) / mode->hdisplay;
		vert = (native_mode->vdisplay << 19) / mode->vdisplay;

		if (vert > horiz) {
			outX = (mode->hdisplay * horiz) >> 19;
			outY = (mode->vdisplay * horiz) >> 19;
		} else {
			outX = (mode->hdisplay * vert) >> 19;
			outY = (mode->vdisplay * vert) >> 19;
		}
		break;
	case DRM_MODE_SCALE_FULLSCREEN:
		outX = native_mode->hdisplay;
		outY = native_mode->vdisplay;
		break;
	case DRM_MODE_SCALE_NO_SCALE:
	case DRM_MODE_SCALE_NON_GPU:
	default:
		outX = mode->hdisplay;
		outY = mode->vdisplay;
		break;
	}

	ret = RING_SPACE(evo, update ? 7 : 5);
	if (ret)
		return ret;

	/* Got a better name for SCALER_ACTIVE? */
	/* One day i've got to really figure out why this is needed. */
	BEGIN_RING(evo, 0, NV50_EVO_CRTC(crtc->index, SCALE_CTRL), 1);
	if ((mode->flags & DRM_MODE_FLAG_DBLSCAN) ||
	    (mode->flags & DRM_MODE_FLAG_INTERLACE) ||
	    mode->hdisplay != outX || mode->vdisplay != outY) {
		OUT_RING(evo, NV50_EVO_CRTC_SCALE_CTRL_ACTIVE);
	} else {
		OUT_RING(evo, NV50_EVO_CRTC_SCALE_CTRL_INACTIVE);
	}

	BEGIN_RING(evo, 0, NV50_EVO_CRTC(crtc->index, SCALE_RES1), 2);
	OUT_RING  (evo, outY << 16 | outX);
	OUT_RING  (evo, outY << 16 | outX);

	if (update) {
		BEGIN_RING(evo, 0, NV50_EVO_UPDATE, 1);
		OUT_RING  (evo, 0);
		FIRE_RING (evo);
	}

	return 0;
}

int
nv50_crtc_set_clock(struct drm_device *dev, int head, int pclk)
{
	uint32_t pll_reg = NV50_PDISPLAY_CRTC_CLK_CTRL1(head);
	struct nouveau_pll_vals pll;
	struct pll_lims limits;
	uint32_t reg1, reg2;
	int ret;

	ret = get_pll_limits(dev, pll_reg, &limits);
	if (ret)
		return ret;

	/*XXX: need a vbios image from one of these cards to look at
	 *     rather than just guessing.  P isn't log2P on these
	 *     cards, it's uncertain at this stage what the PLL
	 *     limits tables have to say about these chips.
	 *
	 *     getPLL_single will need some modifications to calculate
	 *     this properly too.
	 *
	 *     for the moment, hacking up the PLL limits table with
	 *     a log2 value matching nv's maximum.
	 */
	if (!limits.vco2.maxfreq) {
		NV_ERROR(dev, "single-stage PLL, please report: %d!!\n",
			 limits.max_usable_log2p);
		limits.max_usable_log2p = 6;
	}

	ret = nouveau_calc_pll_mnp(dev, &limits, pclk, &pll);
	if (ret <= 0)
		return ret;

	if (limits.vco2.maxfreq) {
		reg1 = nv_rd32(dev, pll_reg + 4) & 0xff00ff00;
		reg2 = nv_rd32(dev, pll_reg + 8) & 0x8000ff00;
		nv_wr32(dev, pll_reg, 0x10000611);
		nv_wr32(dev, pll_reg + 4, reg1 | (pll.M1 << 16) | pll.N1);
		nv_wr32(dev, pll_reg + 8,
			reg2 | (pll.log2P << 28) | (pll.M2 << 16) | pll.N2);
	} else {
		reg1 = nv_rd32(dev, pll_reg + 4) & 0xffc00000;
		nv_wr32(dev, pll_reg, 0x50000610);
		nv_wr32(dev, pll_reg + 4, reg1 |
			(((1<<pll.log2P)-1) << 16) | (pll.M1 << 8) | pll.N1);
	}

	return 0;
}

static void nv50_crtc_destroy(struct drm_crtc *drm_crtc)
{
	struct drm_device *dev = drm_crtc->dev;
	struct nouveau_crtc *crtc = nouveau_crtc(drm_crtc);

	NV_DEBUG(dev, "\n");

	if (!crtc)
		return;

	drm_crtc_cleanup(&crtc->base);

	nv50_cursor_fini(crtc);

	nouveau_bo_ref(NULL, &crtc->lut.nvbo);
	kfree(crtc->mode);
	kfree(crtc);
}

int
nv50_crtc_cursor_set(struct drm_crtc *drm_crtc, struct drm_file *file_priv,
		     uint32_t buffer_handle, uint32_t width, uint32_t height)
{
	struct drm_device *dev = drm_crtc->dev;
	struct nouveau_crtc *crtc = nouveau_crtc(drm_crtc);
	struct nouveau_bo *cursor = NULL;
	int ret = 0;

	if (width != 64 || height != 64)
		return -EINVAL;

	if (buffer_handle) {
		struct drm_nouveau_private *dev_priv = dev->dev_private;
		struct drm_gem_object *gem;
		struct nouveau_bo *nvbo;

		gem = drm_gem_object_lookup(dev, file_priv, buffer_handle);
		if (!gem)
			return -EINVAL;
		nvbo = nouveau_gem_object(gem);

		nouveau_bo_ref(nvbo, &cursor);
		mutex_lock(&dev->struct_mutex);
		drm_gem_object_unreference(gem);
		mutex_unlock(&dev->struct_mutex);

		ret = nouveau_bo_pin(nvbo, TTM_PL_FLAG_VRAM);
		if (ret)
			goto out;

		crtc->cursor.offset = nvbo->bo.offset - dev_priv->vm_vram_base;
		crtc->cursor.set_offset(crtc, crtc->cursor.offset);
		crtc->cursor.show(crtc, true);
	} else {
		crtc->cursor.hide(crtc, true);
	}

	if (crtc->cursor.nvbo)
		nouveau_bo_unpin(crtc->cursor.nvbo);
	nouveau_bo_ref(cursor, &crtc->cursor.nvbo);

out:
	nouveau_bo_ref(NULL, &cursor);
	return ret;
}

int
nv50_crtc_cursor_move(struct drm_crtc *drm_crtc, int x, int y)
{
	struct nouveau_crtc *crtc = nouveau_crtc(drm_crtc);

	crtc->cursor.set_pos(crtc, x, y);
	return 0;
}

static void
nv50_crtc_gamma_set(struct drm_crtc *drm_crtc, u16 *r, u16 *g, u16 *b,
		    uint32_t size)
{
	struct nouveau_crtc *crtc = nouveau_crtc(drm_crtc);
	int i;

	if (size != 256)
		return;

	for (i = 0; i < 256; i++) {
		crtc->lut.r[i] = r[i];
		crtc->lut.g[i] = g[i];
		crtc->lut.b[i] = b[i];
	}

	/* We need to know the depth before we upload, but it's possible to
	 * get called before a framebuffer is bound.  If this is the case,
	 * mark the lut values as dirty by setting depth==0, and it'll be
	 * uploaded on the first mode_set_base()
	 */
	if (!crtc->base.fb) {
		crtc->lut.depth = 0;
		return;
	}

	nv50_crtc_lut_load(crtc);
}

static int
nv50_crtc_helper_set_config(struct drm_mode_set *set)
{
	struct drm_nouveau_private *dev_priv = set->crtc->dev->dev_private;
	int ret;

	dev_priv->in_modeset = true;
	ret = drm_crtc_helper_set_config(set);
	dev_priv->in_modeset = false;
	return ret;
}

static void
nv50_crtc_save(struct drm_crtc *crtc)
{
	NV_ERROR(crtc->dev ,"!!\n");
}

static void
nv50_crtc_restore(struct drm_crtc *crtc)
{
	NV_ERROR(crtc->dev ,"!!\n");
}

static const struct drm_crtc_funcs nv50_crtc_funcs = {
	.save = nv50_crtc_save,
	.restore = nv50_crtc_restore,
	.cursor_set = nv50_crtc_cursor_set,
	.cursor_move = nv50_crtc_cursor_move,
	.gamma_set = nv50_crtc_gamma_set,
	.set_config = nv50_crtc_helper_set_config,
	.destroy = nv50_crtc_destroy,
};

static void nv50_crtc_dpms(struct drm_crtc *drm_crtc, int mode)
{
	struct drm_nouveau_private *dev_priv = drm_crtc->dev->dev_private;
	struct nouveau_crtc *crtc = nouveau_crtc(drm_crtc);

	if (dev_priv->in_modeset)
		nv50_crtc_blank(crtc, true);
}

static void nv50_crtc_prepare(struct drm_crtc *drm_crtc)
{
}

static void nv50_crtc_commit(struct drm_crtc *drm_crtc)
{
	struct drm_device *dev = drm_crtc->dev;
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nouveau_channel *evo = dev_priv->evo;
	struct nouveau_crtc *crtc = nouveau_crtc(drm_crtc);
	int ret;

	nv50_crtc_blank(crtc, false);

	ret = RING_SPACE(evo, 2);
	if (ret) {
		NV_ERROR(dev, "no space while committing crtc\n");
		return;
	}
	BEGIN_RING(evo, 0, NV50_EVO_UPDATE, 1);
	OUT_RING  (evo, 0);
	FIRE_RING (evo);
}

static bool nv50_crtc_mode_fixup(struct drm_crtc *drm_crtc,
				 struct drm_display_mode *mode,
				 struct drm_display_mode *adjusted_mode)
{
	return true;
}

static int
nv50_crtc_do_mode_set_base(struct drm_crtc *drm_crtc, int x, int y,
			   struct drm_framebuffer *old_fb, bool update)
{
	struct nouveau_crtc *crtc = nouveau_crtc(drm_crtc);
	struct drm_device *dev = crtc->base.dev;
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nouveau_channel *evo = dev_priv->evo;
	struct drm_framebuffer *drm_fb = crtc->base.fb;
	struct nouveau_framebuffer *fb = nouveau_framebuffer(drm_fb);
	int ret, format;

	switch (drm_fb->depth) {
	case  8: format = NV50_EVO_CRTC_FB_DEPTH_8; break;
	case 15: format = NV50_EVO_CRTC_FB_DEPTH_15; break;
	case 16: format = NV50_EVO_CRTC_FB_DEPTH_16; break;
	case 24: format = NV50_EVO_CRTC_FB_DEPTH_24; break;
	case 30: format = NV50_EVO_CRTC_FB_DEPTH_30; break;
	default:
		 NV_ERROR(dev, "unknown depth %d\n", drm_fb->depth);
		 return -EINVAL;
	}

	ret = nouveau_bo_pin(fb->nvbo, TTM_PL_FLAG_VRAM);
	if (ret)
		return ret;

	if (old_fb) {
		struct nouveau_framebuffer *ofb = nouveau_framebuffer(old_fb);
		nouveau_bo_unpin(ofb->nvbo);
	}

	crtc->fb.offset = fb->nvbo->bo.offset - dev_priv->vm_vram_base;
	crtc->fb.tiled = fb->nvbo->tile_flags ? true : false;
	crtc->fb.cpp = drm_fb->bits_per_pixel / 8;
	if (!crtc->fb.blanked && dev_priv->chipset != 0x50) {
		ret = RING_SPACE(evo, 2);
		if (ret)
			return ret;

		BEGIN_RING(evo, 0, NV50_EVO_CRTC(crtc->index, FB_DMA), 1);
		if (crtc->fb.tiled) {
			if (crtc->fb.cpp == 4)
				OUT_RING  (evo, NvEvoFB32);
			else
				OUT_RING  (evo, NvEvoFB16);
		} else
			OUT_RING  (evo, NvEvoVRAM);
	}

	ret = RING_SPACE(evo, 10);
	if (ret)
		return ret;

	BEGIN_RING(evo, 0, NV50_EVO_CRTC(crtc->index, FB_OFFSET), 5);
	OUT_RING  (evo, crtc->fb.offset >> 8);
	OUT_RING  (evo, 0);
	OUT_RING  (evo, (drm_fb->height << 16) | drm_fb->width);
	if (!crtc->fb.tiled) {
		OUT_RING  (evo, drm_fb->pitch | (1 << 20));
	} else {
		OUT_RING  (evo, ((drm_fb->pitch / 4) << 4) |
				  fb->nvbo->tile_mode);
	}
	if (dev_priv->chipset == 0x50)
		OUT_RING  (evo, (fb->nvbo->tile_flags << 8) | format);
	else
		OUT_RING  (evo, format);

	BEGIN_RING(evo, 0, NV50_EVO_CRTC(crtc->index, COLOR_CTRL), 1);
	OUT_RING  (evo, NV50_EVO_CRTC_COLOR_CTRL_COLOR);
	BEGIN_RING(evo, 0, NV50_EVO_CRTC(crtc->index, FB_POS), 1);
	OUT_RING  (evo, (y << 16) | x);

	if (crtc->lut.depth != fb->base.depth) {
		crtc->lut.depth = fb->base.depth;
		nv50_crtc_lut_load(crtc);
	}

	if (update) {
		ret = RING_SPACE(evo, 2);
		if (ret)
			return ret;
		BEGIN_RING(evo, 0, NV50_EVO_UPDATE, 1);
		OUT_RING  (evo, 0);
		FIRE_RING (evo);
	}

	return 0;
}

static int
nv50_crtc_mode_set(struct drm_crtc *drm_crtc, struct drm_display_mode *mode,
		   struct drm_display_mode *adjusted_mode, int x, int y,
		   struct drm_framebuffer *old_fb)
{
	struct drm_device *dev = drm_crtc->dev;
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nouveau_channel *evo = dev_priv->evo;
	struct nouveau_crtc *crtc = nouveau_crtc(drm_crtc);
	struct drm_encoder *drm_encoder;
	struct nouveau_encoder *encoder;
	struct nouveau_connector *connector = NULL;
	uint32_t hsync_dur,  vsync_dur, hsync_start_to_end, vsync_start_to_end;
	uint32_t hunk1, vunk1, vunk2a, vunk2b;
	int ret;

	/* Find the connector attached to this CRTC */
	list_for_each_entry(drm_encoder, &dev->mode_config.encoder_list, head) {
		struct drm_connector *drm_connector;

		encoder = nouveau_encoder(drm_encoder);
		if (drm_encoder->crtc != &crtc->base)
			continue;

		list_for_each_entry(drm_connector, &dev->mode_config.connector_list, head) {
			connector = nouveau_connector(drm_connector);
			if (drm_connector->encoder != drm_encoder)
				continue;

			break;
		}

		break; /* no use in finding more than one mode */
	}

	*crtc->mode = *adjusted_mode;
	crtc->use_dithering = connector->use_dithering;

	NV_DEBUG(dev, "index %d\n", crtc->index);

	hsync_dur = adjusted_mode->hsync_end - adjusted_mode->hsync_start;
	vsync_dur = adjusted_mode->vsync_end - adjusted_mode->vsync_start;
	hsync_start_to_end = adjusted_mode->htotal - adjusted_mode->hsync_start;
	vsync_start_to_end = adjusted_mode->vtotal - adjusted_mode->vsync_start;
	/* I can't give this a proper name, anyone else can? */
	hunk1 = adjusted_mode->htotal -
		adjusted_mode->hsync_start + adjusted_mode->hdisplay;
	vunk1 = adjusted_mode->vtotal -
		adjusted_mode->vsync_start + adjusted_mode->vdisplay;
	/* Another strange value, this time only for interlaced adjusted_modes. */
	vunk2a = 2 * adjusted_mode->vtotal -
		 adjusted_mode->vsync_start + adjusted_mode->vdisplay;
	vunk2b = adjusted_mode->vtotal -
		 adjusted_mode->vsync_start + adjusted_mode->vtotal;

	if (adjusted_mode->flags & DRM_MODE_FLAG_INTERLACE) {
		vsync_dur /= 2;
		vsync_start_to_end  /= 2;
		vunk1 /= 2;
		vunk2a /= 2;
		vunk2b /= 2;
		/* magic */
		if (adjusted_mode->flags & DRM_MODE_FLAG_DBLSCAN) {
			vsync_start_to_end -= 1;
			vunk1 -= 1;
			vunk2a -= 1;
			vunk2b -= 1;
		}
	}

	ret = RING_SPACE(evo, 17);
	if (ret)
		return ret;

	BEGIN_RING(evo, 0, NV50_EVO_CRTC(crtc->index, CLOCK), 2);
	OUT_RING  (evo, adjusted_mode->clock | 0x800000);
	OUT_RING  (evo, (adjusted_mode->flags & DRM_MODE_FLAG_INTERLACE) ? 2 : 0);

	BEGIN_RING(evo, 0, NV50_EVO_CRTC(crtc->index, DISPLAY_START), 5);
	OUT_RING  (evo, 0);
	OUT_RING  (evo, (adjusted_mode->vtotal << 16) | adjusted_mode->htotal);
	OUT_RING  (evo, (vsync_dur - 1) << 16 | (hsync_dur - 1));
	OUT_RING  (evo, (vsync_start_to_end - 1) << 16 |
			(hsync_start_to_end - 1));
	OUT_RING  (evo, (vunk1 - 1) << 16 | (hunk1 - 1));

	if (adjusted_mode->flags & DRM_MODE_FLAG_INTERLACE) {
		BEGIN_RING(evo, 0, NV50_EVO_CRTC(crtc->index, UNK0824), 1);
		OUT_RING  (evo, (vunk2b - 1) << 16 | (vunk2a - 1));
	} else {
		OUT_RING  (evo, 0);
		OUT_RING  (evo, 0);
	}

	BEGIN_RING(evo, 0, NV50_EVO_CRTC(crtc->index, UNK082C), 1);
	OUT_RING  (evo, 0);

	/* This is the actual resolution of the mode. */
	BEGIN_RING(evo, 0, NV50_EVO_CRTC(crtc->index, REAL_RES), 1);
	OUT_RING  (evo, (crtc->base.mode.vdisplay << 16) |
			 crtc->base.mode.hdisplay);
	BEGIN_RING(evo, 0, NV50_EVO_CRTC(crtc->index, SCALE_CENTER_OFFSET), 1);
	OUT_RING  (evo, NV50_EVO_CRTC_SCALE_CENTER_OFFSET_VAL(0, 0));

	crtc->set_dither(crtc, false);
	crtc->set_scale(crtc, connector->scaling_mode, false);

	FIRE_RING (evo);
	return nv50_crtc_do_mode_set_base(drm_crtc, x, y, old_fb, false);
}

static int
nv50_crtc_mode_set_base(struct drm_crtc *drm_crtc, int x, int y,
			struct drm_framebuffer *old_fb)
{
	return nv50_crtc_do_mode_set_base(drm_crtc, x, y, old_fb, true);
}

static const struct drm_crtc_helper_funcs nv50_crtc_helper_funcs = {
	.dpms = nv50_crtc_dpms,
	.prepare = nv50_crtc_prepare,
	.commit = nv50_crtc_commit,
	.mode_fixup = nv50_crtc_mode_fixup,
	.mode_set = nv50_crtc_mode_set,
	.mode_set_base = nv50_crtc_mode_set_base,
};

int
nv50_crtc_create(struct drm_device *dev, int index)
{
	struct nouveau_crtc *crtc = NULL;
	int ret, i;

	NV_DEBUG(dev, "\n");

	crtc = kzalloc(sizeof(*crtc) +
		       NOUVEAUFB_CONN_LIMIT * sizeof(struct drm_connector *),
		       GFP_KERNEL);
	if (!crtc)
		return -ENOMEM;

	crtc->mode = kzalloc(sizeof(*crtc->mode), GFP_KERNEL);
	if (!crtc->mode) {
		kfree(crtc);
		return -ENOMEM;
	}

	/* Default CLUT parameters, will be activated on the hw upon
	 * first mode set.
	 */
	for (i = 0; i < 256; i++) {
		crtc->lut.r[i] = i << 8;
		crtc->lut.g[i] = i << 8;
		crtc->lut.b[i] = i << 8;
	}
	crtc->lut.depth = 0;

	ret = nouveau_bo_new(dev, NULL, 4096, 0x100, TTM_PL_FLAG_VRAM,
			     0, 0x0000, false, true, &crtc->lut.nvbo);
	if (!ret) {
		ret = nouveau_bo_pin(crtc->lut.nvbo, TTM_PL_FLAG_VRAM);
		if (!ret)
			ret = nouveau_bo_map(crtc->lut.nvbo);
		if (ret)
			nouveau_bo_ref(NULL, &crtc->lut.nvbo);
	}

	if (ret) {
		kfree(crtc->mode);
		kfree(crtc);
		return ret;
	}

	crtc->index = index;

	/* set function pointers */
	crtc->set_dither = nv50_crtc_set_dither;
	crtc->set_scale = nv50_crtc_set_scale;

	crtc->mode_set.crtc = &crtc->base;
	crtc->mode_set.connectors = (struct drm_connector **)(crtc + 1);
	crtc->mode_set.num_connectors = 0;

	drm_crtc_init(dev, &crtc->base, &nv50_crtc_funcs);
	drm_crtc_helper_add(&crtc->base, &nv50_crtc_helper_funcs);
	drm_mode_crtc_set_gamma_size(&crtc->base, 256);

	nv50_cursor_init(crtc);
	return 0;
}
