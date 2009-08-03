/*
 * Copyright 2003 NVIDIA, Corporation
 * Copyright 2006 Dave Airlie
 * Copyright 2007 Maarten Maathuis
 * Copyright 2007-2009 Stuart Bennett
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
#include "nouveau_hw.h"
#include "nvreg.h"

static int
nv_output_ramdac_offset(struct nouveau_encoder *nv_encoder)
{
	int offset = 0;

	if (nv_encoder->dcb->or & (8 | OUTPUT_C))
		offset += 0x68;
	if (nv_encoder->dcb->or & (8 | OUTPUT_B))
		offset += 0x2000;

	return offset;
}

static int
nv_get_digital_bound_head(struct drm_device *dev, int or)
{
	/* special case of nv_read_tmds to find crtc associated with an output.
	 * this does not give a correct answer for off-chip dvi, but there's no
	 * use for such an answer anyway
	 */
	int ramdac = (or & OUTPUT_C) >> 2;

	NVWriteRAMDAC(dev, ramdac, NV_PRAMDAC_FP_TMDS_CONTROL,
	NV_PRAMDAC_FP_TMDS_CONTROL_WRITE_DISABLE | 0x4);
	return (((NVReadRAMDAC(dev, ramdac, NV_PRAMDAC_FP_TMDS_DATA) & 0x8) >> 3) ^ ramdac);
}

/*
 * arbitrary limit to number of sense oscillations tolerated in one sample
 * period (observed to be at least 13 in "nvidia")
 */
#define MAX_HBLANK_OSC 20

/*
 * arbitrary limit to number of conflicting sample pairs to tolerate at a
 * voltage step (observed to be at least 5 in "nvidia")
 */
#define MAX_SAMPLE_PAIRS 10

static int sample_load_twice(struct drm_device *dev, bool sense[2])
{
	int i;

	for (i = 0; i < 2; i++) {
		bool sense_a, sense_b, sense_b_prime;
		int j = 0;

		/*
		 * wait for bit 0 clear -- out of hblank -- (say reg value 0x4),
		 * then wait for transition 0x4->0x5->0x4: enter hblank, leave
		 * hblank again
		 * use a 10ms timeout (guards against crtc being inactive, in
		 * which case blank state would never change)
		 */
		if (!nouveau_wait_until(dev, 10000000, NV_PRMCIO_INP0__COLOR,
					0x00000001, 0x00000000))
			return -EBUSY;
		if (!nouveau_wait_until(dev, 10000000, NV_PRMCIO_INP0__COLOR,
					0x00000001, 0x00000001))
			return -EBUSY;
		if (!nouveau_wait_until(dev, 10000000, NV_PRMCIO_INP0__COLOR,
					0x00000001, 0x00000000))
			return -EBUSY;

		udelay(100);
		/* when level triggers, sense is _LO_ */
		sense_a = nv_rd08(dev, NV_PRMCIO_INP0) & 0x10;

		/* take another reading until it agrees with sense_a... */
		do {
			udelay(100);
			sense_b = nv_rd08(dev, NV_PRMCIO_INP0) & 0x10;
			if (sense_a != sense_b) {
				sense_b_prime =
					nv_rd08(dev, NV_PRMCIO_INP0) & 0x10;
				if (sense_b == sense_b_prime) {
					/* ... unless two consecutive subsequent
					 * samples agree; sense_a is replaced */
					sense_a = sense_b;
					/* force mis-match so we loop */
					sense_b = !sense_a;
				}
			}
		} while ((sense_a != sense_b) && ++j < MAX_HBLANK_OSC);

		if (j == MAX_HBLANK_OSC)
			/* with so much oscillation, default to sense:LO */
			sense[i] = false;
		else
			sense[i] = sense_a;
	}

	return 0;
}

static enum drm_connector_status
nv04_dac_load_detect(struct drm_encoder *encoder,
		     struct drm_connector *connector)
{
	struct drm_device *dev = encoder->dev;
	uint8_t saved_seq1, saved_pi, saved_rpc1;
	uint8_t saved_palette0[3], saved_palette_mask;
	uint32_t saved_rtest_ctrl, saved_rgen_ctrl;
	int i;
	uint8_t blue;
	bool sense = true;

	/*
	 * for this detection to work, there needs to be a mode set up on the
	 * CRTC.  this is presumed to be the case
	 */

	if (nv_two_heads(dev))
		/* only implemented for head A for now */
		NVSetOwner(dev, 0);

	saved_seq1 = NVReadVgaSeq(dev, 0, NV_VIO_SR_CLOCK_INDEX);
	NVWriteVgaSeq(dev, 0, NV_VIO_SR_CLOCK_INDEX, saved_seq1 & ~0x20);

	saved_rtest_ctrl = NVReadRAMDAC(dev, 0, NV_PRAMDAC_TEST_CONTROL);
	NVWriteRAMDAC(dev, 0, NV_PRAMDAC_TEST_CONTROL,
		      saved_rtest_ctrl & ~NV_PRAMDAC_TEST_CONTROL_PWRDWN_DAC_OFF);

	msleep(10);

	saved_pi = NVReadVgaCrtc(dev, 0, NV_CIO_CRE_PIXEL_INDEX);
	NVWriteVgaCrtc(dev, 0, NV_CIO_CRE_PIXEL_INDEX,
		       saved_pi & ~(0x80 | MASK(NV_CIO_CRE_PIXEL_FORMAT)));
	saved_rpc1 = NVReadVgaCrtc(dev, 0, NV_CIO_CRE_RPC1_INDEX);
	NVWriteVgaCrtc(dev, 0, NV_CIO_CRE_RPC1_INDEX, saved_rpc1 & ~0xc0);

	nv_wr08(dev, NV_PRMDIO_READ_MODE_ADDRESS, 0x0);
	for (i = 0; i < 3; i++)
		saved_palette0[i] = nv_rd08(dev, NV_PRMDIO_PALETTE_DATA);
	saved_palette_mask = nv_rd08(dev, NV_PRMDIO_PIXEL_MASK);
	nv_wr08(dev, NV_PRMDIO_PIXEL_MASK, 0);

	saved_rgen_ctrl = NVReadRAMDAC(dev, 0, NV_PRAMDAC_GENERAL_CONTROL);
	NVWriteRAMDAC(dev, 0, NV_PRAMDAC_GENERAL_CONTROL,
		      (saved_rgen_ctrl & ~(NV_PRAMDAC_GENERAL_CONTROL_BPC_8BITS |
					   NV_PRAMDAC_GENERAL_CONTROL_TERMINATION_75OHM)) |
		      NV_PRAMDAC_GENERAL_CONTROL_PIXMIX_ON);

	blue = 8;	/* start of test range */

	do {
		bool sense_pair[2];

		nv_wr08(dev, NV_PRMDIO_WRITE_MODE_ADDRESS, 0);
		nv_wr08(dev, NV_PRMDIO_PALETTE_DATA, 0);
		nv_wr08(dev, NV_PRMDIO_PALETTE_DATA, 0);
		/* testing blue won't find monochrome monitors.  I don't care */
		nv_wr08(dev, NV_PRMDIO_PALETTE_DATA, blue);

		i = 0;
		/* take sample pairs until both samples in the pair agree */
		do {
			if (sample_load_twice(dev, sense_pair))
				goto out;
		} while ((sense_pair[0] != sense_pair[1]) &&
							++i < MAX_SAMPLE_PAIRS);

		if (i == MAX_SAMPLE_PAIRS)
			/* too much oscillation defaults to LO */
			sense = false;
		else
			sense = sense_pair[0];

	/*
	 * if sense goes LO before blue ramps to 0x18, monitor is not connected.
	 * ergo, if blue gets to 0x18, monitor must be connected
	 */
	} while (++blue < 0x18 && sense);

out:
	nv_wr08(dev, NV_PRMDIO_PIXEL_MASK, saved_palette_mask);
	NVWriteRAMDAC(dev, 0, NV_PRAMDAC_GENERAL_CONTROL, saved_rgen_ctrl);
	nv_wr08(dev, NV_PRMDIO_WRITE_MODE_ADDRESS, 0);
	for (i = 0; i < 3; i++)
		nv_wr08(dev, NV_PRMDIO_PALETTE_DATA, saved_palette0[i]);
	NVWriteRAMDAC(dev, 0, NV_PRAMDAC_TEST_CONTROL, saved_rtest_ctrl);
	NVWriteVgaCrtc(dev, 0, NV_CIO_CRE_PIXEL_INDEX, saved_pi);
	NVWriteVgaCrtc(dev, 0, NV_CIO_CRE_RPC1_INDEX, saved_rpc1);
	NVWriteVgaSeq(dev, 0, NV_VIO_SR_CLOCK_INDEX, saved_seq1);

	if (blue == 0x18) {
		NV_TRACE(dev, "Load detected on head A\n");
		return connector_status_connected;
	}

	return connector_status_disconnected;
}

static enum drm_connector_status
nv17_dac_load_detect(struct drm_encoder *encoder,
		     struct drm_connector *connector)
{
	struct nouveau_encoder *nv_encoder = nouveau_encoder(encoder);
	struct drm_device *dev = encoder->dev;
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	uint32_t testval, regoffset = nv_output_ramdac_offset(nv_encoder);
	uint32_t saved_powerctrl_2 = 0, saved_powerctrl_4 = 0, saved_routput, saved_rtest_ctrl, temp;
	int head, present = 0;

#define RGB_TEST_DATA(r,g,b) (r << 0 | g << 10 | b << 20)
	testval = RGB_TEST_DATA(0x140, 0x140, 0x140); /* 0x94050140 */
	if (dev_priv->vbios->dactestval)
		testval = dev_priv->vbios->dactestval;

	saved_rtest_ctrl = NVReadRAMDAC(dev, 0, NV_PRAMDAC_TEST_CONTROL + regoffset);
	NVWriteRAMDAC(dev, 0, NV_PRAMDAC_TEST_CONTROL + regoffset,
		      saved_rtest_ctrl & ~NV_PRAMDAC_TEST_CONTROL_PWRDWN_DAC_OFF);

	saved_powerctrl_2 = nvReadMC(dev, NV_PBUS_POWERCTRL_2);

	nvWriteMC(dev, NV_PBUS_POWERCTRL_2, saved_powerctrl_2 & 0xd7ffffff);
	if (regoffset == 0x68) {
		saved_powerctrl_4 = nvReadMC(dev, NV_PBUS_POWERCTRL_4);
		nvWriteMC(dev, NV_PBUS_POWERCTRL_4, saved_powerctrl_4 & 0xffffffcf);
	}

	msleep(4);

	saved_routput = NVReadRAMDAC(dev, 0, NV_PRAMDAC_DACCLK + regoffset);
	head = (saved_routput & 0x100) >> 8;
#if 0
	/* if there's a spare crtc, using it will minimise flicker for the case
	 * where the in-use crtc is in use by an off-chip tmds encoder */
	if (xf86_config->crtc[head]->enabled && !xf86_config->crtc[head ^ 1]->enabled)
		head ^= 1;
#endif
	/* nv driver and nv31 use 0xfffffeee, nv34 and 6600 use 0xfffffece */
	NVWriteRAMDAC(dev, 0, NV_PRAMDAC_DACCLK + regoffset,
		      (saved_routput & 0xfffffece) | head << 8);
	msleep(1);

	temp = NVReadRAMDAC(dev, 0, NV_PRAMDAC_DACCLK + regoffset);
	NVWriteRAMDAC(dev, 0, NV_PRAMDAC_DACCLK + regoffset, temp | 1);

	NVWriteRAMDAC(dev, head, NV_PRAMDAC_TESTPOINT_DATA,
		      NV_PRAMDAC_TESTPOINT_DATA_NOTBLANK | testval);
	temp = NVReadRAMDAC(dev, head, NV_PRAMDAC_TEST_CONTROL);
	NVWriteRAMDAC(dev, head, NV_PRAMDAC_TEST_CONTROL,
		      temp | NV_PRAMDAC_TEST_CONTROL_TP_INS_EN_ASSERTED);
	msleep(1);

	present = NVReadRAMDAC(dev, 0, NV_PRAMDAC_TEST_CONTROL + regoffset) &
			NV_PRAMDAC_TEST_CONTROL_SENSEB_ALLHI;

	temp = NVReadRAMDAC(dev, head, NV_PRAMDAC_TEST_CONTROL);
	NVWriteRAMDAC(dev, head, NV_PRAMDAC_TEST_CONTROL,
		      temp & ~NV_PRAMDAC_TEST_CONTROL_TP_INS_EN_ASSERTED);
	NVWriteRAMDAC(dev, head, NV_PRAMDAC_TESTPOINT_DATA, 0);

	/* bios does something more complex for restoring, but I think this is good enough */
	NVWriteRAMDAC(dev, 0, NV_PRAMDAC_DACCLK + regoffset, saved_routput);
	NVWriteRAMDAC(dev, 0, NV_PRAMDAC_TEST_CONTROL + regoffset, saved_rtest_ctrl);
	if (regoffset == 0x68)
		nvWriteMC(dev, NV_PBUS_POWERCTRL_4, saved_powerctrl_4);
	nvWriteMC(dev, NV_PBUS_POWERCTRL_2, saved_powerctrl_2);

	if (present) {
		NV_INFO(dev, "Load detected on output %c\n",
			     '@' + ffs(nv_encoder->dcb->or));
		return connector_status_connected;
	}

	return connector_status_disconnected;
}

static bool
nv_output_mode_fixup(struct drm_encoder *encoder, struct drm_display_mode *mode,
		     struct drm_display_mode *adjusted_mode)
{
	struct nouveau_encoder *nv_encoder = nouveau_encoder(encoder);
	struct nouveau_connector *nv_connector;

	nv_connector = nouveau_encoder_connector_get(nv_encoder);
	if (!nv_connector)
		return false;

	/* For internal panels and gpu scaling on DVI we need the native mode */
	if (nv_encoder->dcb->type == OUTPUT_LVDS ||
	    (nv_encoder->dcb->type == OUTPUT_TMDS && nv_connector->scaling_mode != DRM_MODE_SCALE_NON_GPU)) {
		int id = adjusted_mode->base.id;
		*adjusted_mode = *nv_connector->native_mode;
		adjusted_mode->base.id = id;
	}

	return true;
}

static void
nv_digital_output_prepare_sel_clk(struct drm_device *dev,
				  struct nouveau_encoder *nv_encoder, int head)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nv04_mode_state *state = &dev_priv->mode_reg;
	uint32_t bits1618 = nv_encoder->dcb->or & OUTPUT_A ? 0x10000 : 0x40000;

	if (nv_encoder->dcb->location != DCB_LOC_ON_CHIP)
		return;

	/* SEL_CLK is only used on the primary ramdac
	 * It toggles spread spectrum PLL output and sets the bindings of PLLs
	 * to heads on digital outputs
	 */
	if (head)
		state->sel_clk |= bits1618;
	else
		state->sel_clk &= ~bits1618;

	/* nv30:
	 *	bit 0		NVClk spread spectrum on/off
	 *	bit 2		MemClk spread spectrum on/off
	 * 	bit 4		PixClk1 spread spectrum on/off toggle
	 * 	bit 6		PixClk2 spread spectrum on/off toggle
	 *
	 * nv40 (observations from bios behaviour and mmio traces):
	 * 	bits 4&6	as for nv30
	 * 	bits 5&7	head dependent as for bits 4&6, but do not appear with 4&6;
	 * 			maybe a different spread mode
	 * 	bits 8&10	seen on dual-link dvi outputs, purpose unknown (set by POST scripts)
	 * 	The logic behind turning spread spectrum on/off in the first place,
	 * 	and which bit-pair to use, is unclear on nv40 (for earlier cards, the fp table
	 * 	entry has the necessary info)
	 */
	if (nv_encoder->dcb->type == OUTPUT_LVDS && dev_priv->saved_reg.sel_clk & 0xf0) {
		int shift = (dev_priv->saved_reg.sel_clk & 0x50) ? 0 : 1;

		state->sel_clk &= ~0xf0;
		state->sel_clk |= (head ? 0x40 : 0x10) << shift;
	}
}

#define FP_TG_CONTROL_ON  (NV_PRAMDAC_FP_TG_CONTROL_DISPEN_POS |	\
			   NV_PRAMDAC_FP_TG_CONTROL_HSYNC_POS |		\
			   NV_PRAMDAC_FP_TG_CONTROL_VSYNC_POS)
#define FP_TG_CONTROL_OFF (NV_PRAMDAC_FP_TG_CONTROL_DISPEN_DISABLE |	\
			   NV_PRAMDAC_FP_TG_CONTROL_HSYNC_DISABLE |	\
			   NV_PRAMDAC_FP_TG_CONTROL_VSYNC_DISABLE)

static inline bool is_fpc_off(uint32_t fpc)
{
	return ((fpc & (FP_TG_CONTROL_ON | FP_TG_CONTROL_OFF)) ==
			FP_TG_CONTROL_OFF);
}

static void
nv_output_prepare(struct drm_encoder *encoder)
{
	struct nouveau_encoder *nv_encoder = nouveau_encoder(encoder);
	struct drm_encoder_helper_funcs *helper = encoder->helper_private;
	struct drm_device *dev = encoder->dev;
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	int head = nouveau_crtc(encoder->crtc)->index;
	struct nv04_crtc_reg *crtcstate = dev_priv->mode_reg.crtc_reg;
	uint8_t *cr_lcd = &crtcstate[head].CRTC[NV_CIO_CRE_LCD__INDEX];
	uint8_t *cr_lcd_oth = &crtcstate[head ^ 1].CRTC[NV_CIO_CRE_LCD__INDEX];
	bool digital_op = nv_encoder->dcb->type == OUTPUT_LVDS ||
			  nv_encoder->dcb->type == OUTPUT_TMDS;

	helper->dpms(encoder, DRM_MODE_DPMS_OFF);

	if (nv_encoder->dcb->type == OUTPUT_ANALOG) {
		if (NVReadRAMDAC(dev, head, NV_PRAMDAC_FP_TG_CONTROL) &
							FP_TG_CONTROL_ON) {
			/* digital remnants must be cleaned before new crtc
			 * values programmed.  delay is time for the vga stuff
			 * to realise it's in control again
			 */
			NVWriteRAMDAC(dev, head, NV_PRAMDAC_FP_TG_CONTROL,
				      FP_TG_CONTROL_OFF);
			msleep(50);
		}
		/* don't inadvertently turn it on when state written later */
		crtcstate[head].fp_control = FP_TG_CONTROL_OFF;
	}

	/* calculate some output specific CRTC regs now, so that they can be
	 * written in nv_crtc_set_mode
	 */

	if (digital_op)
		nv_digital_output_prepare_sel_clk(dev, nv_encoder, head);

	/* Some NV4x have unknown values (0x3f, 0x50, 0x54, 0x6b, 0x79, 0x7f)
	 * at LCD__INDEX which we don't alter
	 */
	if (!(*cr_lcd & 0x44)) {
		*cr_lcd = digital_op ? 0x3 : 0x0;
		if (digital_op && nv_two_heads(dev)) {
			if (nv_encoder->dcb->location == DCB_LOC_ON_CHIP)
				*cr_lcd |= head ? 0x0 : 0x8;
			else {
				*cr_lcd |= (nv_encoder->dcb->or << 4) & 0x30;
				if (nv_encoder->dcb->type == OUTPUT_LVDS)
					*cr_lcd |= 0x30;
				if ((*cr_lcd & 0x30) == (*cr_lcd_oth & 0x30)) {
					/* avoid being connected to both crtcs */
					*cr_lcd_oth &= ~0x30;
					NVWriteVgaCrtc(dev, head ^ 1,
						       NV_CIO_CRE_LCD__INDEX,
						       *cr_lcd_oth);
				}
			}
		}
	}
}

static void
nv_output_mode_set(struct drm_encoder *encoder, struct drm_display_mode *mode,
		   struct drm_display_mode *adjusted_mode)
{
	struct nouveau_encoder *nv_encoder = nouveau_encoder(encoder);
	struct drm_device *dev = encoder->dev;
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct dcb_entry *dcbe = nv_encoder->dcb;
	int head = nouveau_crtc(encoder->crtc)->index;

	NV_TRACE(dev, "%s called for encoder %d\n", __func__,
		      nv_encoder->dcb->index);

	if (nv_gf4_disp_arch(dev) && dcbe->type == OUTPUT_ANALOG) {
		struct drm_encoder *rebind;
		uint32_t dac_offset = nv_output_ramdac_offset(nv_encoder);
		uint32_t otherdac;

		/* bit 16-19 are bits that are set on some G70 cards,
		 * but don't seem to have much effect */
		NVWriteRAMDAC(dev, 0, NV_PRAMDAC_DACCLK + dac_offset,
			      head << 8 | NV_PRAMDAC_DACCLK_SEL_DACCLK);
		/* force any other vga encoders to bind to the other crtc */
		list_for_each_entry(rebind, &dev->mode_config.encoder_list, head) {
			struct nouveau_encoder *nv_rebind = nouveau_encoder(rebind);

			if (nv_rebind == nv_encoder || nv_rebind->dcb->type != OUTPUT_ANALOG)
				continue;

			dac_offset = nv_output_ramdac_offset(nv_rebind);
			otherdac = NVReadRAMDAC(dev, 0, NV_PRAMDAC_DACCLK + dac_offset);
			NVWriteRAMDAC(dev, 0, NV_PRAMDAC_DACCLK + dac_offset,
				      (otherdac & ~0x0100) | (head ^ 1) << 8);
		}
	}
	if (dcbe->type == OUTPUT_TMDS)
		run_tmds_table(dev, dcbe, head, adjusted_mode->clock);
	else if (dcbe->type == OUTPUT_LVDS)
		call_lvds_script(dev, dcbe, head, LVDS_RESET, adjusted_mode->clock);
	if (dcbe->type == OUTPUT_LVDS || dcbe->type == OUTPUT_TMDS)
		/* update fp_control state for any changes made by scripts,
		 * so correct value is written at DPMS on */
		dev_priv->mode_reg.crtc_reg[head].fp_control =
			NVReadRAMDAC(dev, head, NV_PRAMDAC_FP_TG_CONTROL);

	/* This could use refinement for flatpanels, but it should work this way */
	if (dev_priv->chipset < 0x44)
		NVWriteRAMDAC(dev, 0, NV_PRAMDAC_TEST_CONTROL + nv_output_ramdac_offset(nv_encoder), 0xf0000000);
	else
		NVWriteRAMDAC(dev, 0, NV_PRAMDAC_TEST_CONTROL + nv_output_ramdac_offset(nv_encoder), 0x00100000);
}

static void
nv_output_commit(struct drm_encoder *encoder)
{
	struct nouveau_encoder *nv_encoder = nouveau_encoder(encoder);
	struct drm_device *dev = encoder->dev;
	struct nouveau_crtc *nv_crtc = nouveau_crtc(encoder->crtc);
	struct drm_encoder_helper_funcs *helper = encoder->helper_private;

	helper->dpms(encoder, DRM_MODE_DPMS_ON);

	NV_INFO(dev, "Output %s is running on CRTC %d using output %c\n",
		      drm_get_connector_name(&nouveau_encoder_connector_get(nv_encoder)->base), nv_crtc->index,
		      '@' + ffs(nv_encoder->dcb->or));
}

static void
dpms_update_fp_control(struct drm_device *dev,
		       struct nouveau_encoder *nv_encoder, struct drm_crtc * crtc,
		       int mode)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nouveau_crtc *nv_crtc;
	uint32_t *fpc;

	if (mode == DRM_MODE_DPMS_ON) {
		nv_crtc = nouveau_crtc(crtc);
		fpc = &dev_priv->mode_reg.crtc_reg[nv_crtc->index].fp_control;

		if (is_fpc_off(*fpc)) {
			/* using saved value is ok, as (is_digital && dpms_on &&
			 * fp_control==OFF) is (at present) *only* true when
			 * fpc's most recent change was by below "off" code
			 */
			*fpc = nv_crtc->dpms_saved_fp_control;
		}

		nv_crtc->fp_users |= 1 << nv_encoder->dcb->index;
		NVWriteRAMDAC(dev, nv_crtc->index, NV_PRAMDAC_FP_TG_CONTROL, *fpc);
	} else {
		list_for_each_entry(crtc, &dev->mode_config.crtc_list, head) {
			nv_crtc = nouveau_crtc(crtc);
			fpc = &dev_priv->mode_reg.crtc_reg[nv_crtc->index].fp_control;

			nv_crtc->fp_users &= ~(1 << nv_encoder->dcb->index);
			if (!is_fpc_off(*fpc) && !nv_crtc->fp_users) {
				nv_crtc->dpms_saved_fp_control = *fpc;
				/* cut the FP output */
				*fpc &= ~FP_TG_CONTROL_ON;
				*fpc |= FP_TG_CONTROL_OFF;
				NVWriteRAMDAC(dev, nv_crtc->index,
					      NV_PRAMDAC_FP_TG_CONTROL, *fpc);
			}
		}
	}
}

static inline bool is_powersaving_dpms(int mode)
{
	return (mode != DRM_MODE_DPMS_ON);
}

static void
lvds_encoder_dpms(struct drm_device *dev, struct nouveau_encoder *nv_encoder,
		  struct drm_crtc * crtc, int mode)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	bool was_powersaving = is_powersaving_dpms(nv_encoder->last_dpms);

	if (nv_encoder->last_dpms == mode)
		return;
	nv_encoder->last_dpms = mode;

	NV_INFO(dev, "Setting dpms mode %d on lvds encoder (output %d)\n",
		     mode, nv_encoder->dcb->index);

	if (was_powersaving && is_powersaving_dpms(mode))
		return;

	if (nv_encoder->dcb->lvdsconf.use_power_scripts) {
		struct nouveau_connector *nv_connector = nouveau_encoder_connector_get(nv_encoder);

		/* when removing an output, crtc may not be set, but PANEL_OFF
		 * must still be run
		 */
		int head = crtc ? nouveau_crtc(crtc)->index :
			   nv_get_digital_bound_head(dev, nv_encoder->dcb->or);

		if (mode == DRM_MODE_DPMS_ON)
			call_lvds_script(dev, nv_encoder->dcb, head,
					 LVDS_PANEL_ON, nv_connector->native_mode->clock);
		else
			/* pxclk of 0 is fine for PANEL_OFF, and for a
			 * disconnected LVDS encoder there is no native_mode
			 */
			call_lvds_script(dev, nv_encoder->dcb, head,
					 LVDS_PANEL_OFF, 0);
	}

	dpms_update_fp_control(dev, nv_encoder, crtc, mode);

	if (mode == DRM_MODE_DPMS_ON)
		nv_digital_output_prepare_sel_clk(dev, nv_encoder, nouveau_crtc(crtc)->index);
	else {
		dev_priv->mode_reg.sel_clk = NVReadRAMDAC(dev, 0, NV_PRAMDAC_SEL_CLK);
		dev_priv->mode_reg.sel_clk &= ~0xf0;
	}
	NVWriteRAMDAC(dev, 0, NV_PRAMDAC_SEL_CLK, dev_priv->mode_reg.sel_clk);
}

static void
vga_encoder_dpms(struct drm_device *dev, struct nouveau_encoder *nv_encoder,
		 struct drm_crtc * crtc, int mode)
{
	if (nv_encoder->last_dpms == mode)
		return;
	nv_encoder->last_dpms = mode;

	NV_INFO(dev, "Setting dpms mode %d on vga encoder (output %d)\n",
		     mode, nv_encoder->dcb->index);

	if (nv_gf4_disp_arch(dev)) {
		uint32_t outputval = NVReadRAMDAC(dev, 0, NV_PRAMDAC_DACCLK + nv_output_ramdac_offset(nv_encoder));

		if (mode == DRM_MODE_DPMS_OFF)
			NVWriteRAMDAC(dev, 0, NV_PRAMDAC_DACCLK + nv_output_ramdac_offset(nv_encoder),
				      outputval & ~NV_PRAMDAC_DACCLK_SEL_DACCLK);
		else if (mode == DRM_MODE_DPMS_ON)
			NVWriteRAMDAC(dev, 0, NV_PRAMDAC_DACCLK + nv_output_ramdac_offset(nv_encoder),
				      outputval | NV_PRAMDAC_DACCLK_SEL_DACCLK);
	}
}

static void
tmds_encoder_dpms(struct drm_device *dev, struct nouveau_encoder *nv_encoder,
		  struct drm_crtc * crtc, int mode)
{
	if (nv_encoder->last_dpms == mode)
		return;
	nv_encoder->last_dpms = mode;

	NV_INFO(dev, "Setting dpms mode %d on tmds encoder (output %d)\n",
		     mode, nv_encoder->dcb->index);

	dpms_update_fp_control(dev, nv_encoder, crtc, mode);
}

static void
nv_output_dpms(struct drm_encoder *encoder, int mode)
{
	struct nouveau_encoder *nv_encoder = nouveau_encoder(encoder);
	struct drm_device *dev = encoder->dev;
	struct drm_crtc *crtc = encoder->crtc;
	void (* const encoder_dpms[4])(struct drm_device *, struct nouveau_encoder *, struct drm_crtc *, int) =
		/* index matches DCB type */
		{ vga_encoder_dpms, NULL, tmds_encoder_dpms, lvds_encoder_dpms };

	encoder_dpms[nv_encoder->dcb->type](dev, nv_encoder, crtc, mode);
}

static void
nv04_encoder_save(struct drm_encoder *encoder)
{
	struct nouveau_encoder *nv_encoder = nouveau_encoder(encoder);
	struct drm_device *dev = encoder->dev;

	if (!nv_encoder->dcb)	/* uninitialised encoder */
		return;

	if (nv_gf4_disp_arch(dev) && nv_encoder->dcb->type == OUTPUT_ANALOG)
		nv_encoder->restore.output =
			NVReadRAMDAC(dev, 0, NV_PRAMDAC_DACCLK +
				nv_output_ramdac_offset(nv_encoder));
	if (nv_two_heads(dev) && (nv_encoder->dcb->type == OUTPUT_LVDS ||
			      nv_encoder->dcb->type == OUTPUT_TMDS))
		nv_encoder->restore.head =
			nv_get_digital_bound_head(dev, nv_encoder->dcb->or);
}

static void
nv04_encoder_restore(struct drm_encoder *encoder)
{
	struct nouveau_encoder *nv_encoder = nouveau_encoder(encoder);
	struct drm_device *dev = encoder->dev;
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	int head = nv_encoder->restore.head;

	if (!nv_encoder->dcb)	/* uninitialised encoder */
		return;

	if (nv_gf4_disp_arch(dev) && nv_encoder->dcb->type == OUTPUT_ANALOG)
		NVWriteRAMDAC(dev, 0,
			      NV_PRAMDAC_DACCLK + nv_output_ramdac_offset(nv_encoder),
			      nv_encoder->restore.output);
	if (nv_encoder->dcb->type == OUTPUT_LVDS)
		call_lvds_script(dev, nv_encoder->dcb, head, LVDS_PANEL_ON,
				 nouveau_encoder_connector_get(nv_encoder)->native_mode->clock);
	if (nv_encoder->dcb->type == OUTPUT_TMDS) {
		int clock = nouveau_hw_pllvals_to_clk
					(&dev_priv->saved_reg.crtc_reg[head].pllvals);

		run_tmds_table(dev, nv_encoder->dcb, head, clock);
	}

	nv_encoder->last_dpms = NV_DPMS_CLEARED;
}

static const struct drm_encoder_helper_funcs nv04_dac_helper_funcs = {
	.dpms = nv_output_dpms,
	.save = nv04_encoder_save,
	.restore = nv04_encoder_restore,
	.mode_fixup = nv_output_mode_fixup,
	.prepare = nv_output_prepare,
	.commit = nv_output_commit,
	.mode_set = nv_output_mode_set,
	.detect = nv04_dac_load_detect
};

static const struct drm_encoder_helper_funcs nv17_dac_helper_funcs = {
	.dpms = nv_output_dpms,
	.save = nv04_encoder_save,
	.restore = nv04_encoder_restore,
	.mode_fixup = nv_output_mode_fixup,
	.prepare = nv_output_prepare,
	.commit = nv_output_commit,
	.mode_set = nv_output_mode_set,
	.detect = nv17_dac_load_detect
};

static const struct drm_encoder_helper_funcs nv04_encoder_helper_funcs = {
	.dpms = nv_output_dpms,
	.save = nv04_encoder_save,
	.restore = nv04_encoder_restore,
	.mode_fixup = nv_output_mode_fixup,
	.prepare = nv_output_prepare,
	.commit = nv_output_commit,
	.mode_set = nv_output_mode_set,
	.detect = NULL
};

static void
nv04_encoder_destroy(struct drm_encoder *encoder)
{
	struct nouveau_encoder *nv_encoder = nouveau_encoder(encoder);

	NV_DEBUG(encoder->dev, "\n");

	drm_encoder_cleanup(encoder);
	kfree(nv_encoder);
}

static const struct drm_encoder_funcs nv04_encoder_funcs = {
	.destroy = nv04_encoder_destroy,
};

int
nv04_encoder_create(struct drm_device *dev, struct dcb_entry *entry)
{
	const struct drm_encoder_helper_funcs *helper;
	struct nouveau_encoder *nv_encoder = NULL;
	int type;

	switch (entry->type) {
	case OUTPUT_TMDS:
		type = DRM_MODE_ENCODER_TMDS;
		break;
	case OUTPUT_LVDS:
		type = DRM_MODE_ENCODER_LVDS;
		break;
	case OUTPUT_ANALOG:
		type = DRM_MODE_ENCODER_DAC;
		break;
	default:
		return -EINVAL;
	}

	nv_encoder = kzalloc(sizeof(*nv_encoder), GFP_KERNEL);
	if (!nv_encoder)
		return -ENOMEM;

	nv_encoder->dcb = entry;
	nv_encoder->or = ffs(entry->or) - 1;

	if (entry->type == OUTPUT_ANALOG) {
		if (nv_gf4_disp_arch(dev))
			helper = &nv17_dac_helper_funcs;
		else
			helper = &nv04_dac_helper_funcs;
	} else
		helper = &nv04_encoder_helper_funcs;

	drm_encoder_init(dev, &nv_encoder->base, &nv04_encoder_funcs, type);
	drm_encoder_helper_add(&nv_encoder->base, helper);

	nv_encoder->base.possible_crtcs = entry->heads;
	nv_encoder->base.possible_clones = 0;

	return 0;
}

