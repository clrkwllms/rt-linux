/*
 * Copyright 1993-2003 NVIDIA, Corporation
 * Copyright 2006 Dave Airlie
 * Copyright 2007 Maarten Maathuis
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
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#include "drmP.h"
#include "drm_crtc_helper.h"

#include "nouveau_drv.h"
#include "nouveau_encoder.h"
#include "nouveau_connector.h"
#include "nouveau_crtc.h"
#include "nouveau_fb.h"
#include "nouveau_fbcon.h"
#include "nouveau_hw.h"
#include "nvreg.h"

static int
nv04_crtc_mode_set_base(struct drm_crtc *crtc, int x, int y,
			struct drm_framebuffer *old_fb);

static void
crtc_wr_cio_state(struct drm_crtc *crtc, struct nv04_crtc_reg *crtcstate, int index)
{
	NVWriteVgaCrtc(crtc->dev, nouveau_crtc(crtc)->index, index,
		       crtcstate->CRTC[index]);
}

void nv_crtc_set_digital_vibrance(struct drm_crtc *crtc, int level)
{
	struct nouveau_crtc *nv_crtc = nouveau_crtc(crtc);
	struct drm_nouveau_private *dev_priv = crtc->dev->dev_private;
	struct nv04_crtc_reg *regp = &dev_priv->mode_reg.crtc_reg[nv_crtc->index];

	regp->CRTC[NV_CIO_CRE_CSB] = nv_crtc->saturation = level;
	if (nv_crtc->saturation && nv_gf4_disp_arch(crtc->dev)) {
		regp->CRTC[NV_CIO_CRE_CSB] = 0x80;
		regp->CRTC[NV_CIO_CRE_5B] = nv_crtc->saturation << 2;
		crtc_wr_cio_state(crtc, regp, NV_CIO_CRE_5B);
	}
	crtc_wr_cio_state(crtc, regp, NV_CIO_CRE_CSB);
}

void nv_crtc_set_image_sharpening(struct drm_crtc *crtc, int level)
{
	struct nouveau_crtc *nv_crtc = nouveau_crtc(crtc);
	struct drm_nouveau_private *dev_priv = crtc->dev->dev_private;
	struct nv04_crtc_reg *regp = &dev_priv->mode_reg.crtc_reg[nv_crtc->index];

	nv_crtc->sharpness = level;
	if (level < 0)	/* blur is in hw range 0x3f -> 0x20 */
		level += 0x40;
	regp->ramdac_634 = level;
	NVWriteRAMDAC(crtc->dev, nv_crtc->index, NV_PRAMDAC_634, regp->ramdac_634);
}

/* NV4x 0x40.. pll notes:
 * gpu pll: 0x4000 + 0x4004
 * ?gpu? pll: 0x4008 + 0x400c
 * vpll1: 0x4010 + 0x4014
 * vpll2: 0x4018 + 0x401c
 * mpll: 0x4020 + 0x4024
 * mpll: 0x4038 + 0x403c
 *
 * the first register of each pair has some unknown details:
 * bits 0-7: redirected values from elsewhere? (similar to PLL_SETUP_CONTROL?)
 * bits 20-23: (mpll) something to do with post divider?
 * bits 28-31: related to single stage mode? (bit 8/12)
 */

static void nv_crtc_calc_state_ext(struct drm_crtc *crtc, struct drm_display_mode * mode, int dot_clock)
{
	struct drm_device *dev = crtc->dev;
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nouveau_crtc *nv_crtc = nouveau_crtc(crtc);
	struct nv04_mode_state *state = &dev_priv->mode_reg;
	struct nv04_crtc_reg *regp = &state->crtc_reg[nv_crtc->index];
	struct drm_framebuffer *fb = crtc->fb;
	struct nouveau_pll_vals *pv = &regp->pllvals;
	struct pll_lims pll_lim;
	int vclk, arb_burst, arb_fifo_lwm;

	if (get_pll_limits(dev, nv_crtc->index ? VPLL2 : VPLL1, &pll_lim))
		return;

	/* NM2 == 0 is used to determine single stage mode on two stage plls */
	pv->NM2 = 0;

	/* for newer nv4x the blob uses only the first stage of the vpll below a
	 * certain clock.  for a certain nv4b this is 150MHz.  since the max
	 * output frequency of the first stage for this card is 300MHz, it is
	 * assumed the threshold is given by vco1 maxfreq/2
	 */
	/* for early nv4x, specifically nv40 and *some* nv43 (devids 0 and 6,
	 * not 8, others unknown), the blob always uses both plls.  no problem
	 * has yet been observed in allowing the use a single stage pll on all
	 * nv43 however.  the behaviour of single stage use is untested on nv40
	 */
	if (dev_priv->chipset > 0x40 && dot_clock <= (pll_lim.vco1.maxfreq / 2))
		memset(&pll_lim.vco2, 0, sizeof(pll_lim.vco2));

	if (!(vclk = nouveau_calc_pll_mnp(dev, &pll_lim, dot_clock, pv)))
		return;

	/* The blob uses this always, so let's do the same */
	if (nv_arch(dev) == NV_40)
		state->pllsel |= NV_RAMDAC_PLL_SELECT_USE_VPLL2_TRUE;
	/* again nv40 and some nv43 act more like nv3x as described above */
	if (dev_priv->chipset < 0x41)
		state->pllsel |= NV_PRAMDAC_PLL_COEFF_SELECT_SOURCE_PROG_MPLL |
				 NV_PRAMDAC_PLL_COEFF_SELECT_SOURCE_PROG_NVPLL;
	state->pllsel |= (nv_crtc->index ? NV_RAMDAC_PLL_SELECT_PLL_SOURCE_VPLL2 |
					  NV_RAMDAC_PLL_SELECT_VCLK2_RATIO_DB2 :
					  NV_PRAMDAC_PLL_COEFF_SELECT_SOURCE_PROG_VPLL |
					  NV_PRAMDAC_PLL_COEFF_SELECT_VCLK_RATIO_DB2);

	if (pv->NM2)
		NV_TRACE(dev, "vpll: n1 %d n2 %d m1 %d m2 %d log2p %d\n",
			 pv->N1, pv->N2, pv->M1, pv->M2, pv->log2P);
	else
		NV_TRACE(dev, "vpll: n %d m %d log2p %d\n",
			 pv->N1, pv->M1, pv->log2P);

	nouveau_calc_arb(dev, vclk, fb->bits_per_pixel, &arb_burst, &arb_fifo_lwm);

	regp->CRTC[NV_CIO_CRE_FF_INDEX] = arb_burst;
	regp->CRTC[NV_CIO_CRE_FFLWM__INDEX] = arb_fifo_lwm & 0xff;
	if (nv_arch(dev) >= NV_30)
		regp->CRTC[NV_CIO_CRE_47] = arb_fifo_lwm >> 8;

	nv_crtc->cursor.set_offset(nv_crtc, nv_crtc->cursor.offset);
}

static void
nv_crtc_dpms(struct drm_crtc *crtc, int mode)
{
	struct nouveau_crtc *nv_crtc = nouveau_crtc(crtc);
	struct drm_device *dev = crtc->dev;
	unsigned char seq1 = 0, crtc17 = 0;
	unsigned char crtc1A;

	NV_TRACE(dev, "Setting dpms mode %d on CRTC %d\n", mode, nv_crtc->index);

	if (nv_crtc->last_dpms == mode) /* Don't do unnecesary mode changes. */
		return;

	nv_crtc->last_dpms = mode;

	if (nv_two_heads(dev))
		NVSetOwner(dev, nv_crtc->index);

	/* nv4ref indicates these two RPC1 bits inhibit h/v sync */
	crtc1A = NVReadVgaCrtc(dev, nv_crtc->index, NV_CIO_CRE_RPC1_INDEX) & ~0xC0;
	switch(mode) {
	case DRM_MODE_DPMS_STANDBY:
		/* Screen: Off; HSync: Off, VSync: On -- Not Supported */
		seq1 = 0x20;
		crtc17 = 0x80;
		crtc1A |= 0x80;
		break;
	case DRM_MODE_DPMS_SUSPEND:
		/* Screen: Off; HSync: On, VSync: Off -- Not Supported */
		seq1 = 0x20;
		crtc17 = 0x80;
		crtc1A |= 0x40;
		break;
	case DRM_MODE_DPMS_OFF:
		/* Screen: Off; HSync: Off, VSync: Off */
		seq1 = 0x20;
		crtc17 = 0x00;
		crtc1A |= 0xC0;
		break;
	case DRM_MODE_DPMS_ON:
	default:
		/* Screen: On; HSync: On, VSync: On */
		seq1 = 0x00;
		crtc17 = 0x80;
		break;
	}

	NVVgaSeqReset(dev, nv_crtc->index, true);
	/* Each head has it's own sequencer, so we can turn it off when we want */
	seq1 |= (NVReadVgaSeq(dev, nv_crtc->index, NV_VIO_SR_CLOCK_INDEX) & ~0x20);
	NVWriteVgaSeq(dev, nv_crtc->index, NV_VIO_SR_CLOCK_INDEX, seq1);
	crtc17 |= (NVReadVgaCrtc(dev, nv_crtc->index, NV_CIO_CR_MODE_INDEX) & ~0x80);
	mdelay(10);
	NVWriteVgaCrtc(dev, nv_crtc->index, NV_CIO_CR_MODE_INDEX, crtc17);
	NVVgaSeqReset(dev, nv_crtc->index, false);

	NVWriteVgaCrtc(dev, nv_crtc->index, NV_CIO_CRE_RPC1_INDEX, crtc1A);
}

static bool
nv_crtc_mode_fixup(struct drm_crtc *crtc, struct drm_display_mode * mode,
		   struct drm_display_mode * adjusted_mode)
{
	return true;
}

static void
nv_crtc_mode_set_vga(struct drm_crtc *crtc, struct drm_display_mode *mode,
		     struct drm_display_mode *adjusted_mode)
{
	struct drm_device *dev = crtc->dev;
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nouveau_crtc *nv_crtc = nouveau_crtc(crtc);
	struct nv04_crtc_reg *regp = &dev_priv->mode_reg.crtc_reg[nv_crtc->index];
	struct drm_framebuffer *fb = crtc->fb;

	/* Calculate our timings */
	int horizDisplay	= (mode->crtc_hdisplay >> 3) 	- 1;
	int horizStart		= (mode->crtc_hsync_start >> 3) 	- 1;
	int horizEnd		= (mode->crtc_hsync_end >> 3) 	- 1;
	int horizTotal		= (mode->crtc_htotal >> 3)		- 5;
	int horizBlankStart	= (mode->crtc_hdisplay >> 3)		- 1;
	int horizBlankEnd	= (mode->crtc_htotal >> 3)		- 1;
	int vertDisplay		= mode->crtc_vdisplay			- 1;
	int vertStart		= mode->crtc_vsync_start 		- 1;
	int vertEnd		= mode->crtc_vsync_end			- 1;
	int vertTotal		= mode->crtc_vtotal 			- 2;
	int vertBlankStart	= mode->crtc_vdisplay 			- 1;
	int vertBlankEnd	= mode->crtc_vtotal			- 1;

	struct drm_encoder *encoder;
	bool fp_output = false;

	list_for_each_entry(encoder, &dev->mode_config.encoder_list, head) {
		struct nouveau_encoder *nv_encoder = nouveau_encoder(encoder);

		if (encoder->crtc == crtc &&
		    (nv_encoder->dcb->type == OUTPUT_LVDS ||
		     nv_encoder->dcb->type == OUTPUT_TMDS))
			fp_output = true;
	}

	if (fp_output) {
		vertStart = vertTotal - 3;
		vertEnd = vertTotal - 2;
		vertBlankStart = vertStart;
		horizStart = horizTotal - 5;
		horizEnd = horizTotal - 2;
		horizBlankEnd = horizTotal + 4;
#if 0
		if (dev->overlayAdaptor && nv_arch(dev) >= NV_10)
			/* This reportedly works around some video overlay bandwidth problems */
			horizTotal += 2;
#endif
	}

	if (mode->flags & DRM_MODE_FLAG_INTERLACE)
		vertTotal |= 1;

#if 0
	ErrorF("horizDisplay: 0x%X \n", horizDisplay);
	ErrorF("horizStart: 0x%X \n", horizStart);
	ErrorF("horizEnd: 0x%X \n", horizEnd);
	ErrorF("horizTotal: 0x%X \n", horizTotal);
	ErrorF("horizBlankStart: 0x%X \n", horizBlankStart);
	ErrorF("horizBlankEnd: 0x%X \n", horizBlankEnd);
	ErrorF("vertDisplay: 0x%X \n", vertDisplay);
	ErrorF("vertStart: 0x%X \n", vertStart);
	ErrorF("vertEnd: 0x%X \n", vertEnd);
	ErrorF("vertTotal: 0x%X \n", vertTotal);
	ErrorF("vertBlankStart: 0x%X \n", vertBlankStart);
	ErrorF("vertBlankEnd: 0x%X \n", vertBlankEnd);
#endif

	/*
	* compute correct Hsync & Vsync polarity
	*/
	if ((mode->flags & (DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_NHSYNC))
		&& (mode->flags & (DRM_MODE_FLAG_PVSYNC | DRM_MODE_FLAG_NVSYNC))) {

		regp->MiscOutReg = 0x23;
		if (mode->flags & DRM_MODE_FLAG_NHSYNC) regp->MiscOutReg |= 0x40;
		if (mode->flags & DRM_MODE_FLAG_NVSYNC) regp->MiscOutReg |= 0x80;
	} else {
		int vdisplay = mode->vdisplay;
		if (mode->flags & DRM_MODE_FLAG_DBLSCAN)
			vdisplay *= 2;
		if (mode->vscan > 1)
			vdisplay *= mode->vscan;
		if (vdisplay < 400)
			regp->MiscOutReg = 0xA3;		/* +hsync -vsync */
		else if (vdisplay < 480)
			regp->MiscOutReg = 0x63;		/* -hsync +vsync */
		else if (vdisplay < 768)
			regp->MiscOutReg = 0xE3;		/* -hsync -vsync */
		else
			regp->MiscOutReg = 0x23;		/* +hsync +vsync */
	}

	regp->MiscOutReg |= (mode->clock_index & 0x03) << 2;

	/*
	 * Time Sequencer
	 */
	regp->Sequencer[NV_VIO_SR_RESET_INDEX] = 0x00;
	/* 0x20 disables the sequencer */
	if (mode->flags & DRM_MODE_FLAG_CLKDIV2)
		regp->Sequencer[NV_VIO_SR_CLOCK_INDEX] = 0x29;
	else
		regp->Sequencer[NV_VIO_SR_CLOCK_INDEX] = 0x21;
	regp->Sequencer[NV_VIO_SR_PLANE_MASK_INDEX] = 0x0F;
	regp->Sequencer[NV_VIO_SR_CHAR_MAP_INDEX] = 0x00;
	regp->Sequencer[NV_VIO_SR_MEM_MODE_INDEX] = 0x0E;

	/*
	 * CRTC
	 */
	regp->CRTC[NV_CIO_CR_HDT_INDEX] = horizTotal;
	regp->CRTC[NV_CIO_CR_HDE_INDEX] = horizDisplay;
	regp->CRTC[NV_CIO_CR_HBS_INDEX] = horizBlankStart;
	regp->CRTC[NV_CIO_CR_HBE_INDEX] = (1 << 7) |
					  XLATE(horizBlankEnd, 0, NV_CIO_CR_HBE_4_0);
	regp->CRTC[NV_CIO_CR_HRS_INDEX] = horizStart;
	regp->CRTC[NV_CIO_CR_HRE_INDEX] = XLATE(horizBlankEnd, 5, NV_CIO_CR_HRE_HBE_5) |
					  XLATE(horizEnd, 0, NV_CIO_CR_HRE_4_0);
	regp->CRTC[NV_CIO_CR_VDT_INDEX] = vertTotal;
	regp->CRTC[NV_CIO_CR_OVL_INDEX] = XLATE(vertStart, 9, NV_CIO_CR_OVL_VRS_9) |
					  XLATE(vertDisplay, 9, NV_CIO_CR_OVL_VDE_9) |
					  XLATE(vertTotal, 9, NV_CIO_CR_OVL_VDT_9) |
					  (1 << 4) |
					  XLATE(vertBlankStart, 8, NV_CIO_CR_OVL_VBS_8) |
					  XLATE(vertStart, 8, NV_CIO_CR_OVL_VRS_8) |
					  XLATE(vertDisplay, 8, NV_CIO_CR_OVL_VDE_8) |
					  XLATE(vertTotal, 8, NV_CIO_CR_OVL_VDT_8);
	regp->CRTC[NV_CIO_CR_RSAL_INDEX] = 0x00;
	regp->CRTC[NV_CIO_CR_CELL_HT_INDEX] = ((mode->flags & DRM_MODE_FLAG_DBLSCAN) ? MASK(NV_CIO_CR_CELL_HT_SCANDBL) : 0) |
					      1 << 6 |
					      XLATE(vertBlankStart, 9, NV_CIO_CR_CELL_HT_VBS_9);
	regp->CRTC[NV_CIO_CR_CURS_ST_INDEX] = 0x00;
	regp->CRTC[NV_CIO_CR_CURS_END_INDEX] = 0x00;
	regp->CRTC[NV_CIO_CR_SA_HI_INDEX] = 0x00;
	regp->CRTC[NV_CIO_CR_SA_LO_INDEX] = 0x00;
	regp->CRTC[NV_CIO_CR_TCOFF_HI_INDEX] = 0x00;
	regp->CRTC[NV_CIO_CR_TCOFF_LO_INDEX] = 0x00;
	regp->CRTC[NV_CIO_CR_VRS_INDEX] = vertStart;
	regp->CRTC[NV_CIO_CR_VRE_INDEX] = 1 << 5 | XLATE(vertEnd, 0, NV_CIO_CR_VRE_3_0);
	regp->CRTC[NV_CIO_CR_VDE_INDEX] = vertDisplay;
	/* framebuffer can be larger than crtc scanout area. */
	regp->CRTC[NV_CIO_CR_OFFSET_INDEX] = fb->pitch / 8;
	regp->CRTC[NV_CIO_CR_ULINE_INDEX] = 0x00;
	regp->CRTC[NV_CIO_CR_VBS_INDEX] = vertBlankStart;
	regp->CRTC[NV_CIO_CR_VBE_INDEX] = vertBlankEnd;
	regp->CRTC[NV_CIO_CR_MODE_INDEX] = 0x43;
	regp->CRTC[NV_CIO_CR_LCOMP_INDEX] = 0xff;

	/*
	 * Some extended CRTC registers (they are not saved with the rest of the vga regs).
	 */

	/* framebuffer can be larger than crtc scanout area. */
	regp->CRTC[NV_CIO_CRE_RPC0_INDEX] = XLATE(fb->pitch / 8, 8, NV_CIO_CRE_RPC0_OFFSET_10_8);
	regp->CRTC[NV_CIO_CRE_RPC1_INDEX] = mode->crtc_hdisplay < 1280 ?
					    MASK(NV_CIO_CRE_RPC1_LARGE) : 0x00;
	regp->CRTC[NV_CIO_CRE_LSR_INDEX] = XLATE(horizBlankEnd, 6, NV_CIO_CRE_LSR_HBE_6) |
					   XLATE(vertBlankStart, 10, NV_CIO_CRE_LSR_VBS_10) |
					   XLATE(vertStart, 10, NV_CIO_CRE_LSR_VRS_10) |
					   XLATE(vertDisplay, 10, NV_CIO_CRE_LSR_VDE_10) |
					   XLATE(vertTotal, 10, NV_CIO_CRE_LSR_VDT_10);
	regp->CRTC[NV_CIO_CRE_HEB__INDEX] = XLATE(horizStart, 8, NV_CIO_CRE_HEB_HRS_8) |
					    XLATE(horizBlankStart, 8, NV_CIO_CRE_HEB_HBS_8) |
					    XLATE(horizDisplay, 8, NV_CIO_CRE_HEB_HDE_8) |
					    XLATE(horizTotal, 8, NV_CIO_CRE_HEB_HDT_8);
	regp->CRTC[NV_CIO_CRE_EBR_INDEX] = XLATE(vertBlankStart, 11, NV_CIO_CRE_EBR_VBS_11) |
					   XLATE(vertStart, 11, NV_CIO_CRE_EBR_VRS_11) |
					   XLATE(vertDisplay, 11, NV_CIO_CRE_EBR_VDE_11) |
					   XLATE(vertTotal, 11, NV_CIO_CRE_EBR_VDT_11);

	if (mode->flags & DRM_MODE_FLAG_INTERLACE) {
		horizTotal = (horizTotal >> 1) & ~1;
		regp->CRTC[NV_CIO_CRE_ILACE__INDEX] = horizTotal;
		regp->CRTC[NV_CIO_CRE_HEB__INDEX] |= XLATE(horizTotal, 8, NV_CIO_CRE_HEB_ILC_8);
	} else
		regp->CRTC[NV_CIO_CRE_ILACE__INDEX] = 0xff;  /* interlace off */

	/*
	* Graphics Display Controller
	*/
	regp->Graphics[NV_VIO_GX_SR_INDEX] = 0x00;
	regp->Graphics[NV_VIO_GX_SREN_INDEX] = 0x00;
	regp->Graphics[NV_VIO_GX_CCOMP_INDEX] = 0x00;
	regp->Graphics[NV_VIO_GX_ROP_INDEX] = 0x00;
	regp->Graphics[NV_VIO_GX_READ_MAP_INDEX] = 0x00;
	regp->Graphics[NV_VIO_GX_MODE_INDEX] = 0x40; /* 256 color mode */
	regp->Graphics[NV_VIO_GX_MISC_INDEX] = 0x05; /* map 64k mem + graphic mode */
	regp->Graphics[NV_VIO_GX_DONT_CARE_INDEX] = 0x0F;
	regp->Graphics[NV_VIO_GX_BIT_MASK_INDEX] = 0xFF;

	regp->Attribute[0]  = 0x00; /* standard colormap translation */
	regp->Attribute[1]  = 0x01;
	regp->Attribute[2]  = 0x02;
	regp->Attribute[3]  = 0x03;
	regp->Attribute[4]  = 0x04;
	regp->Attribute[5]  = 0x05;
	regp->Attribute[6]  = 0x06;
	regp->Attribute[7]  = 0x07;
	regp->Attribute[8]  = 0x08;
	regp->Attribute[9]  = 0x09;
	regp->Attribute[10] = 0x0A;
	regp->Attribute[11] = 0x0B;
	regp->Attribute[12] = 0x0C;
	regp->Attribute[13] = 0x0D;
	regp->Attribute[14] = 0x0E;
	regp->Attribute[15] = 0x0F;
	regp->Attribute[NV_CIO_AR_MODE_INDEX] = 0x01; /* Enable graphic mode */
	/* Non-vga */
	regp->Attribute[NV_CIO_AR_OSCAN_INDEX] = 0x00;
	regp->Attribute[NV_CIO_AR_PLANE_INDEX] = 0x0F; /* enable all color planes */
	regp->Attribute[NV_CIO_AR_HPP_INDEX] = 0x00;
	regp->Attribute[NV_CIO_AR_CSEL_INDEX] = 0x00;
}

/**
 * Sets up registers for the given mode/adjusted_mode pair.
 *
 * The clocks, CRTCs and outputs attached to this CRTC must be off.
 *
 * This shouldn't enable any clocks, CRTCs, or outputs, but they should
 * be easily turned on/off after this.
 */
static void
nv_crtc_mode_set_regs(struct drm_crtc *crtc, struct drm_display_mode * mode)
{
	struct drm_device *dev = crtc->dev;
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nouveau_crtc *nv_crtc = nouveau_crtc(crtc);
	struct nv04_crtc_reg *regp = &dev_priv->mode_reg.crtc_reg[nv_crtc->index];
	struct nv04_crtc_reg *savep = &dev_priv->saved_reg.crtc_reg[nv_crtc->index];
	struct drm_encoder *encoder;
	bool lvds_output = false, tmds_output = false, off_chip_digital = false;

	list_for_each_entry(encoder, &dev->mode_config.encoder_list, head) {
		struct nouveau_encoder *nv_encoder = nouveau_encoder(encoder);
		bool digital = false;

		if (encoder->crtc != crtc)
			continue;

		if (nv_encoder->dcb->type == OUTPUT_LVDS)
			digital = lvds_output = true;
		if (nv_encoder->dcb->type == OUTPUT_TMDS)
			digital = tmds_output = true;
		if (nv_encoder->dcb->location != DCB_LOC_ON_CHIP && digital)
			off_chip_digital = true;
	}

	/* Registers not directly related to the (s)vga mode */

	/* What is the meaning of this register? */
	/* A few popular values are 0x18, 0x1c, 0x38, 0x3c */
	regp->CRTC[NV_CIO_CRE_ENH_INDEX] = savep->CRTC[NV_CIO_CRE_ENH_INDEX] & ~(1<<5);

	regp->crtc_eng_ctrl = 0;
	/* Except for rare conditions I2C is enabled on the primary crtc */
	if (nv_crtc->index == 0)
		regp->crtc_eng_ctrl |= NV_CRTC_FSEL_I2C;
#if 0
	/* Set overlay to desired crtc. */
	if (dev->overlayAdaptor) {
		NVPortPrivPtr pPriv = GET_OVERLAY_PRIVATE(dev);
		if (pPriv->overlayCRTC == nv_crtc->index)
			regp->crtc_eng_ctrl |= NV_CRTC_FSEL_OVERLAY;
	}
#endif

	/* ADDRESS_SPACE_PNVM is the same as setting HCUR_ASI */
	regp->cursor_cfg = NV_PCRTC_CURSOR_CONFIG_CUR_LINES_64 |
			     NV_PCRTC_CURSOR_CONFIG_CUR_PIXELS_64 |
			     NV_PCRTC_CURSOR_CONFIG_ADDRESS_SPACE_PNVM;
	if (dev_priv->chipset >= 0x11)
		regp->cursor_cfg |= NV_PCRTC_CURSOR_CONFIG_CUR_BPP_32;
	if (mode->flags & DRM_MODE_FLAG_DBLSCAN)
		regp->cursor_cfg |= NV_PCRTC_CURSOR_CONFIG_DOUBLE_SCAN_ENABLE;

	/* Unblock some timings */
	regp->CRTC[NV_CIO_CRE_53] = 0;
	regp->CRTC[NV_CIO_CRE_54] = 0;

	/* 0x00 is disabled, 0x11 is lvds, 0x22 crt and 0x88 tmds */
	if (lvds_output)
		regp->CRTC[NV_CIO_CRE_SCRATCH3__INDEX] = 0x11;
	else if (tmds_output)
		regp->CRTC[NV_CIO_CRE_SCRATCH3__INDEX] = 0x88;
	else
		regp->CRTC[NV_CIO_CRE_SCRATCH3__INDEX] = 0x22;

	/* These values seem to vary */
	/* This register seems to be used by the bios to make certain decisions on some G70 cards? */
	regp->CRTC[NV_CIO_CRE_SCRATCH4__INDEX] = savep->CRTC[NV_CIO_CRE_SCRATCH4__INDEX];

	nv_crtc_set_digital_vibrance(crtc, nv_crtc->saturation);

	/* probably a scratch reg, but kept for cargo-cult purposes:
	 * bit0: crtc0?, head A
	 * bit6: lvds, head A
	 * bit7: (only in X), head A
	 */
	if (nv_crtc->index == 0)
		regp->CRTC[NV_CIO_CRE_4B] = savep->CRTC[NV_CIO_CRE_4B] | 0x80;

	/* The blob seems to take the current value from crtc 0, add 4 to that
	 * and reuse the old value for crtc 1 */
	regp->CRTC[NV_CIO_CRE_TVOUT_LATENCY] = dev_priv->saved_reg.crtc_reg[0].CRTC[NV_CIO_CRE_TVOUT_LATENCY];
	if (!nv_crtc->index)
		regp->CRTC[NV_CIO_CRE_TVOUT_LATENCY] += 4;

	/* the blob sometimes sets |= 0x10 (which is the same as setting |=
	 * 1 << 30 on 0x60.830), for no apparent reason */
	regp->CRTC[NV_CIO_CRE_59] = off_chip_digital;

	regp->crtc_830 = mode->crtc_vdisplay - 3;
	regp->crtc_834 = mode->crtc_vdisplay - 1;

	if (nv_arch(dev) == NV_40)
		/* This is what the blob does */
		regp->crtc_850 = NVReadCRTC(dev, 0, NV_PCRTC_850);

	if (nv_arch(dev) == NV_40)
		regp->gpio_ext = NVReadCRTC(dev, 0, NV_PCRTC_GPIO_EXT);

	regp->crtc_cfg = NV_PCRTC_CONFIG_START_ADDRESS_HSYNC;

	/* Some misc regs */
	if (nv_arch(dev) == NV_40) {
		regp->CRTC[NV_CIO_CRE_85] = 0xFF;
		regp->CRTC[NV_CIO_CRE_86] = 0x1;
	}

	regp->CRTC[NV_CIO_CRE_PIXEL_INDEX] = (crtc->fb->depth + 1) / 8;
	/* Enable slaved mode (called MODE_TV in nv4ref.h) */
	if (lvds_output || tmds_output)
		regp->CRTC[NV_CIO_CRE_PIXEL_INDEX] |= (1 << 7);

	/* Generic PRAMDAC regs */

	if (nv_arch(dev) >= NV_10)
		/* Only bit that bios and blob set. */
		regp->nv10_cursync = (1 << 25);

	regp->ramdac_gen_ctrl = NV_PRAMDAC_GENERAL_CONTROL_BPC_8BITS |
				NV_PRAMDAC_GENERAL_CONTROL_VGA_STATE_SEL |
				NV_PRAMDAC_GENERAL_CONTROL_PIXMIX_ON;
	if (crtc->fb->depth == 16)
		regp->ramdac_gen_ctrl |= NV_PRAMDAC_GENERAL_CONTROL_ALT_MODE_SEL;
	if (dev_priv->chipset >= 0x11)
		regp->ramdac_gen_ctrl |= NV_PRAMDAC_GENERAL_CONTROL_PIPE_LONG;

	regp->ramdac_630 = 0; /* turn off green mode (tv test pattern?) */

	nv_crtc_set_image_sharpening(crtc, nv_crtc->sharpness);

	/* Some values the blob sets */
	regp->ramdac_a20 = 0x0;
	regp->ramdac_a24 = 0xfffff;
	regp->ramdac_a34 = 0x1;
}

enum fp_display_regs {
	FP_DISPLAY_END,
	FP_TOTAL,
	FP_CRTC,
	FP_SYNC_START,
	FP_SYNC_END,
	FP_VALID_START,
	FP_VALID_END
};

/* this could be set in nv_output, but would require some rework of load/save */
static void
nv_crtc_mode_set_fp_regs(struct drm_crtc *crtc, struct drm_display_mode *mode,
			 struct drm_display_mode * adjusted_mode)
{
	struct drm_device *dev = crtc->dev;
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nouveau_crtc *nv_crtc = nouveau_crtc(crtc);
	struct nv04_crtc_reg *regp = &dev_priv->mode_reg.crtc_reg[nv_crtc->index];
	struct nv04_crtc_reg *savep = &dev_priv->saved_reg.crtc_reg[nv_crtc->index];
	struct nouveau_connector *nv_connector = nouveau_crtc_connector_get(nv_crtc);
	struct nouveau_encoder *nv_encoder = NULL;
	struct drm_encoder *encoder;
	uint32_t mode_ratio, panel_ratio;

	list_for_each_entry(encoder, &dev->mode_config.encoder_list, head) {
		/* assuming one fp output per crtc seems ok */
		if (encoder->crtc != crtc)
			continue;
		nv_encoder = nouveau_encoder(encoder);

		if (nv_encoder->dcb->type == OUTPUT_LVDS ||
		    nv_encoder->dcb->type == OUTPUT_TMDS)
			break;
		nv_encoder = NULL;
	}

	if (!nv_encoder)
		return;

	regp->fp_horiz_regs[FP_DISPLAY_END] = adjusted_mode->hdisplay - 1;
	regp->fp_horiz_regs[FP_TOTAL] = adjusted_mode->htotal - 1;
	if (!nv_gf4_disp_arch(dev) ||
	    (adjusted_mode->hsync_start - adjusted_mode->hdisplay) >=
					dev_priv->vbios->digital_min_front_porch)
		regp->fp_horiz_regs[FP_CRTC] = adjusted_mode->hdisplay;
	else
		regp->fp_horiz_regs[FP_CRTC] = adjusted_mode->hsync_start - dev_priv->vbios->digital_min_front_porch - 1;
	regp->fp_horiz_regs[FP_SYNC_START] = adjusted_mode->hsync_start - 1;
	regp->fp_horiz_regs[FP_SYNC_END] = adjusted_mode->hsync_end - 1;
	regp->fp_horiz_regs[FP_VALID_START] = adjusted_mode->hskew;
	regp->fp_horiz_regs[FP_VALID_END] = adjusted_mode->hdisplay - 1;

	regp->fp_vert_regs[FP_DISPLAY_END] = adjusted_mode->vdisplay - 1;
	regp->fp_vert_regs[FP_TOTAL] = adjusted_mode->vtotal - 1;
	regp->fp_vert_regs[FP_CRTC] = adjusted_mode->vtotal - 5 - 1;
	regp->fp_vert_regs[FP_SYNC_START] = adjusted_mode->vsync_start - 1;
	regp->fp_vert_regs[FP_SYNC_END] = adjusted_mode->vsync_end - 1;
	regp->fp_vert_regs[FP_VALID_START] = 0;
	regp->fp_vert_regs[FP_VALID_END] = adjusted_mode->vdisplay - 1;

	/* bit26: a bit seen on some g7x, no as yet discernable purpose */
	regp->fp_control = NV_PRAMDAC_FP_TG_CONTROL_DISPEN_POS |
			   (savep->fp_control & (1 << 26 | NV_PRAMDAC_FP_TG_CONTROL_READ_PROG));
	/* Deal with vsync/hsync polarity */
	/* LVDS screens do set this, but modes with +ve syncs are very rare */
	if (adjusted_mode->flags & DRM_MODE_FLAG_PVSYNC)
		regp->fp_control |= NV_PRAMDAC_FP_TG_CONTROL_VSYNC_POS;
	if (adjusted_mode->flags & DRM_MODE_FLAG_PHSYNC)
		regp->fp_control |= NV_PRAMDAC_FP_TG_CONTROL_HSYNC_POS;
	/* panel scaling first, as native would get set otherwise */
	if (nv_connector->scaling_mode == DRM_MODE_SCALE_NON_GPU ||
	    nv_connector->scaling_mode == DRM_MODE_SCALE_NO_SCALE)	/* panel handles it */
		regp->fp_control |= NV_PRAMDAC_FP_TG_CONTROL_MODE_CENTER;
	else if (mode->hdisplay == adjusted_mode->hdisplay &&
		 mode->vdisplay == adjusted_mode->vdisplay) /* native mode */
		regp->fp_control |= NV_PRAMDAC_FP_TG_CONTROL_MODE_NATIVE;
	else /* gpu needs to scale */
		regp->fp_control |= NV_PRAMDAC_FP_TG_CONTROL_MODE_SCALE;
	if (nvReadEXTDEV(dev, NV_PEXTDEV_BOOT_0) & NV_PEXTDEV_BOOT_0_STRAP_FP_IFACE_12BIT)
		regp->fp_control |= NV_PRAMDAC_FP_TG_CONTROL_WIDTH_12;
	if (nv_encoder->dcb->location != DCB_LOC_ON_CHIP &&
	    nv_connector->native_mode->clock > 165000)
		regp->fp_control |= (2 << 24);
	if (nv_encoder->dual_link)
		regp->fp_control |= (8 << 28);

	regp->fp_debug_0 = NV_PRAMDAC_FP_DEBUG_0_YWEIGHT_ROUND |
			   NV_PRAMDAC_FP_DEBUG_0_XWEIGHT_ROUND |
			   NV_PRAMDAC_FP_DEBUG_0_YINTERP_BILINEAR |
			   NV_PRAMDAC_FP_DEBUG_0_XINTERP_BILINEAR |
			   NV_RAMDAC_FP_DEBUG_0_TMDS_ENABLED |
			   NV_PRAMDAC_FP_DEBUG_0_YSCALE_ENABLE |
			   NV_PRAMDAC_FP_DEBUG_0_XSCALE_ENABLE;

	/* We want automatic scaling */
	regp->fp_debug_1 = 0;
	/* This can override HTOTAL and VTOTAL */
	regp->fp_debug_2 = 0;

	/* Use 20.12 fixed point format to avoid floats */
	mode_ratio = (1 << 12) * mode->hdisplay / mode->vdisplay;
	panel_ratio = (1 << 12) * adjusted_mode->hdisplay / adjusted_mode->vdisplay;
	/* if ratios are equal, SCALE_ASPECT will automatically (and correctly)
	 * get treated the same as SCALE_FULLSCREEN */
	if (nv_connector->scaling_mode == DRM_MODE_SCALE_ASPECT &&
	    mode_ratio != panel_ratio) {
		uint32_t diff, scale;
		bool divide_by_2 = nv_gf4_disp_arch(dev);

		if (mode_ratio < panel_ratio) {
			/* vertical needs to expand to glass size (automatic)
			 * horizontal needs to be scaled at vertical scale factor
			 * to maintain aspect */

			scale = (1 << 12) * mode->vdisplay / adjusted_mode->vdisplay;
			regp->fp_debug_1 = NV_PRAMDAC_FP_DEBUG_1_XSCALE_TESTMODE_ENABLE |
					   XLATE(scale, divide_by_2, NV_PRAMDAC_FP_DEBUG_1_XSCALE_VALUE);

			/* restrict area of screen used, horizontally */
			diff = adjusted_mode->hdisplay -
			       adjusted_mode->vdisplay * mode_ratio / (1 << 12);
			regp->fp_horiz_regs[FP_VALID_START] += diff / 2;
			regp->fp_horiz_regs[FP_VALID_END] -= diff / 2;
		}

		if (mode_ratio > panel_ratio) {
			/* horizontal needs to expand to glass size (automatic)
			 * vertical needs to be scaled at horizontal scale factor
			 * to maintain aspect */

			scale = (1 << 12) * mode->hdisplay / adjusted_mode->hdisplay;
			regp->fp_debug_1 = NV_PRAMDAC_FP_DEBUG_1_YSCALE_TESTMODE_ENABLE |
					   XLATE(scale, divide_by_2, NV_PRAMDAC_FP_DEBUG_1_YSCALE_VALUE);

			/* restrict area of screen used, vertically */
			diff = adjusted_mode->vdisplay -
			       (1 << 12) * adjusted_mode->hdisplay / mode_ratio;
			regp->fp_vert_regs[FP_VALID_START] += diff / 2;
			regp->fp_vert_regs[FP_VALID_END] -= diff / 2;
		}
	}

	/* Output property. */
	if (nv_connector && nv_connector->use_dithering) {
		if (dev_priv->chipset == 0x11)
			regp->dither = savep->dither | 0x00010000;
		else {
			int i;
			regp->dither = savep->dither | 0x00000001;
			for (i = 0; i < 3; i++) {
				regp->dither_regs[i] = 0xe4e4e4e4;
				regp->dither_regs[i + 3] = 0x44444444;
			}
		}
	} else {
		if (dev_priv->chipset != 0x11) {
			/* reset them */
			int i;
			for (i = 0; i < 3; i++) {
				regp->dither_regs[i] = savep->dither_regs[i];
				regp->dither_regs[i + 3] = savep->dither_regs[i + 3];
			}
		}
		regp->dither = savep->dither;
	}
}

/**
 * Sets up registers for the given mode/adjusted_mode pair.
 *
 * The clocks, CRTCs and outputs attached to this CRTC must be off.
 *
 * This shouldn't enable any clocks, CRTCs, or outputs, but they should
 * be easily turned on/off after this.
 */
static int
nv_crtc_mode_set(struct drm_crtc *crtc, struct drm_display_mode *mode,
		 struct drm_display_mode *adjusted_mode,
		 int x, int y, struct drm_framebuffer *old_fb)
{
	struct drm_device *dev = crtc->dev;
	struct nouveau_crtc *nv_crtc = nouveau_crtc(crtc);
	struct drm_nouveau_private *dev_priv = dev->dev_private;

	NV_TRACE(dev, "CTRC mode on CRTC %d:\n", nv_crtc->index);
	drm_mode_debug_printmodeline(mode);
	NV_TRACE(dev, "Output mode on CRTC %d:\n", nv_crtc->index);
	drm_mode_debug_printmodeline(mode);

	/* unlock must come after turning off FP_TG_CONTROL in output_prepare */
	nv_lock_vga_crtc_shadow(dev, nv_crtc->index, -1);

	nv_crtc_mode_set_vga(crtc, mode, adjusted_mode);
	/* calculated in output_prepare, nv40 needs it written before calculating PLLs */
	if (nv_arch(dev) == NV_40)
		NVWriteRAMDAC(dev, 0, NV_PRAMDAC_SEL_CLK, dev_priv->mode_reg.sel_clk);
	nv_crtc_mode_set_regs(crtc, mode);
	nv_crtc_mode_set_fp_regs(crtc, mode, adjusted_mode);
	nv_crtc_calc_state_ext(crtc, mode, adjusted_mode->clock);

	nouveau_hw_load_state(dev, nv_crtc->index, &dev_priv->mode_reg);

	nv04_crtc_mode_set_base(crtc, x, y, NULL);

#ifdef __BIG_ENDIAN
	/* turn on LFB swapping */
	{
		uint8_t tmp = NVReadVgaCrtc(dev, nv_crtc->index, NV_CIO_CRE_RCR);
		tmp |= MASK(NV_CIO_CRE_RCR_ENDIAN_BIG);
		NVWriteVgaCrtc(dev, nv_crtc->index, NV_CIO_CRE_RCR, tmp);
	}
#endif

	return 0;
}

static void nv_crtc_save(struct drm_crtc *crtc)
{
	struct nouveau_crtc *nv_crtc = nouveau_crtc(crtc);
	struct drm_nouveau_private *dev_priv = crtc->dev->dev_private;

	if (nv_two_heads(crtc->dev))
		NVSetOwner(crtc->dev, nv_crtc->index);

	nouveau_hw_save_state(crtc->dev, nv_crtc->index, &dev_priv->saved_reg);

	/* init some state to saved value */
	dev_priv->mode_reg.sel_clk = dev_priv->saved_reg.sel_clk & ~(0x5 << 16);
	dev_priv->mode_reg.crtc_reg[nv_crtc->index].CRTC[NV_CIO_CRE_LCD__INDEX] = dev_priv->saved_reg.crtc_reg[nv_crtc->index].CRTC[NV_CIO_CRE_LCD__INDEX];
}

static void nv_crtc_restore(struct drm_crtc *crtc)
{
	struct nouveau_crtc *nv_crtc = nouveau_crtc(crtc);
	struct drm_nouveau_private *dev_priv = crtc->dev->dev_private;
	int head = nv_crtc->index;
	uint8_t saved_cr21 = dev_priv->saved_reg.crtc_reg[head].CRTC[NV_CIO_CRE_21];

	if (nv_two_heads(crtc->dev))
		NVSetOwner(crtc->dev, head);

	nouveau_hw_load_state(crtc->dev, head, &dev_priv->saved_reg);
	nv_lock_vga_crtc_shadow(crtc->dev, head, saved_cr21);

	nv_crtc->last_dpms = NV_DPMS_CLEARED;
}

static void nv_crtc_prepare(struct drm_crtc *crtc)
{
	struct drm_device *dev = crtc->dev;
	struct nouveau_crtc *nv_crtc = nouveau_crtc(crtc);
	struct drm_crtc_helper_funcs *funcs = crtc->helper_private;

	if (nv_two_heads(dev))
		NVSetOwner(dev, nv_crtc->index);

	funcs->dpms(crtc, DRM_MODE_DPMS_OFF);

	NVBlankScreen(dev, nv_crtc->index, true);

	/* Some more preperation. */
	NVWriteCRTC(dev, nv_crtc->index, NV_PCRTC_CONFIG, NV_PCRTC_CONFIG_START_ADDRESS_NON_VGA);
	if (nv_arch(dev) == NV_40) {
		uint32_t reg900 = NVReadRAMDAC(dev, nv_crtc->index, NV_PRAMDAC_900);
		NVWriteRAMDAC(dev, nv_crtc->index, NV_PRAMDAC_900, reg900 & ~0x10000);
	}
}

static void nv_crtc_commit(struct drm_crtc *crtc)
{
	struct drm_crtc_helper_funcs *funcs = crtc->helper_private;

	funcs->dpms(crtc, DRM_MODE_DPMS_ON);
}

static void nv_crtc_destroy(struct drm_crtc *crtc)
{
}

#define DEPTH_SHIFT(val, w) ((val << (8 - w)) | (val >> ((w << 1) - 8)))
static void
nv_crtc_gamma_load(struct nouveau_crtc *nv_crtc)
{
	struct drm_device *dev = nv_crtc->base.dev;
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct rgb { uint8_t r, g, b; } __attribute__((packed)) *rgbs;
	int i;

	rgbs = (struct rgb *)dev_priv->mode_reg.crtc_reg[nv_crtc->index].DAC;

	switch (nv_crtc->base.fb->depth) {
	case 15:
		/* R5G5B5 */
		/* spread 5 bits per colour (32 colours) over 256 (per colour) registers */
		for (i = 0; i < 32; i++) {
			rgbs[DEPTH_SHIFT(i, 5)].r = nv_crtc->lut.r[i] >> 8;
			rgbs[DEPTH_SHIFT(i, 5)].g = nv_crtc->lut.g[i] >> 8;
			rgbs[DEPTH_SHIFT(i, 5)].b = nv_crtc->lut.b[i] >> 8;
		}
		break;
	case 16:
		/* R5G6B5 */
		for (i = 0; i < 64; i++) {
			/* set 64 regs for green's 6 bits of colour */
			rgbs[DEPTH_SHIFT(i, 6)].g = nv_crtc->lut.g[i] >> 8;
			if (i < 32) {
				rgbs[DEPTH_SHIFT(i, 5)].r =  nv_crtc->lut.r[i] >> 8;
				rgbs[DEPTH_SHIFT(i, 5)].b = nv_crtc->lut.b[i] >> 8;
			}
		}
		break;
	default:
		/* R8G8B8 */
		for (i = 0; i < 256; i++) {
			rgbs[i].r = nv_crtc->lut.r[i] >> 8;
			rgbs[i].g = nv_crtc->lut.g[i] >> 8;
			rgbs[i].b = nv_crtc->lut.b[i] >> 8;
		}
	}

	nouveau_hw_load_state_palette(dev, nv_crtc->index, &dev_priv->mode_reg);
}

static void
nv_crtc_gamma_set(struct drm_crtc *crtc, u16 *r, u16 *g, u16 *b, uint32_t size)
{
	struct nouveau_crtc *nv_crtc = nouveau_crtc(crtc);
	int i;

	if (size != 256)
		return;

	for (i = 0; i < 256; i++) {
		nv_crtc->lut.r[i] = r[i];
		nv_crtc->lut.g[i] = g[i];
		nv_crtc->lut.b[i] = b[i];
	}

	/* We need to know the depth before we upload, but it's possible to
	 * get called before a framebuffer is bound.  If this is the case,
	 * mark the lut values as dirty by setting depth==0, and it'll be
	 * uploaded on the first mode_set_base()
	 */
	if (!nv_crtc->base.fb) {
		nv_crtc->lut.depth = 0;
		return;
	}

	nv_crtc_gamma_load(nv_crtc);
}

static int
nv04_crtc_mode_set_base(struct drm_crtc *crtc, int x, int y,
			struct drm_framebuffer *old_fb)
{
	struct nouveau_crtc *nv_crtc = nouveau_crtc(crtc);
	struct drm_nouveau_private *dev_priv = crtc->dev->dev_private;
	struct nv04_crtc_reg *regp = &dev_priv->mode_reg.crtc_reg[nv_crtc->index];
	struct drm_framebuffer *drm_fb = nv_crtc->base.fb;
	struct nouveau_framebuffer *fb = nouveau_framebuffer(drm_fb);
	int ret;

	ret = nouveau_bo_pin(fb->nvbo, TTM_PL_FLAG_VRAM);
	if (ret)
		return ret;

	if (old_fb) {
		struct nouveau_framebuffer *fb = nouveau_framebuffer(old_fb);

		nouveau_bo_unpin(fb->nvbo);
	}

	nv_crtc->fb.offset = fb->nvbo->bo.offset;

	if (nv_crtc->lut.depth != drm_fb->depth) {
		nv_crtc->lut.depth = drm_fb->depth;
		nv_crtc_gamma_load(nv_crtc);
	}

	regp->CRTC[NV_CIO_CR_OFFSET_INDEX] = drm_fb->pitch >> 3;
	regp->CRTC[NV_CIO_CRE_RPC0_INDEX] =
		XLATE(drm_fb->pitch >> 3, 8, NV_CIO_CRE_RPC0_OFFSET_10_8);
	crtc_wr_cio_state(crtc, regp, NV_CIO_CRE_RPC0_INDEX);
	crtc_wr_cio_state(crtc, regp, NV_CIO_CR_OFFSET_INDEX);

	regp->fb_start = nv_crtc->fb.offset & ~3;
	regp->fb_start += (y * drm_fb->pitch) + (x * drm_fb->bits_per_pixel / 8);
	NVWriteCRTC(crtc->dev, nv_crtc->index, NV_PCRTC_START, regp->fb_start);
	return 0;
}

static const struct drm_crtc_funcs nv04_crtc_funcs = {
	.save = nv_crtc_save,
	.restore = nv_crtc_restore,
	.cursor_set = nv50_crtc_cursor_set,
	.cursor_move = nv50_crtc_cursor_move,
	.gamma_set = nv_crtc_gamma_set,
	.set_config = drm_crtc_helper_set_config,
	.destroy = nv_crtc_destroy,
};

static const struct drm_crtc_helper_funcs nv04_crtc_helper_funcs = {
	.dpms = nv_crtc_dpms,
	.prepare = nv_crtc_prepare,
	.commit = nv_crtc_commit,
	.mode_fixup = nv_crtc_mode_fixup,
	.mode_set = nv_crtc_mode_set,
	.mode_set_base = nv04_crtc_mode_set_base,
};

int
nv04_crtc_create(struct drm_device *dev, int crtc_num)
{
	struct nouveau_crtc *nv_crtc;
	int i;

	nv_crtc = kzalloc(sizeof(*nv_crtc) +
			  NOUVEAUFB_CONN_LIMIT * sizeof(struct drm_connector *),
			  GFP_KERNEL);
	if (!nv_crtc)
		return -ENOMEM;

	for (i = 0; i < 256; i++) {
		nv_crtc->lut.r[i] = i << 8;
		nv_crtc->lut.g[i] = i << 8;
		nv_crtc->lut.b[i] = i << 8;
	}
	nv_crtc->lut.depth = 0;

	nv_crtc->index = crtc_num;
	nv_crtc->last_dpms = NV_DPMS_CLEARED;

	nv_crtc->mode_set.crtc = &nv_crtc->base;
	nv_crtc->mode_set.connectors = (struct drm_connector **)(nv_crtc + 1);
	nv_crtc->mode_set.num_connectors = 0;

	drm_crtc_init(dev, &nv_crtc->base, &nv04_crtc_funcs);
	drm_crtc_helper_add(&nv_crtc->base, &nv04_crtc_helper_funcs);
	drm_mode_crtc_set_gamma_size(&nv_crtc->base, 256);

	nv04_cursor_init(nv_crtc);

	return 0;
}

