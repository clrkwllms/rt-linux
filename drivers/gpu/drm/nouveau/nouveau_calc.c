/*
 * Copyright 1993-2003 NVIDIA, Corporation
 * Copyright 2007-2009 Stuart Bennett
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF
 * OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include "drmP.h"
#include "nouveau_drv.h"
#include "nouveau_hw.h"

/****************************************************************************\
*                                                                            *
* The video arbitration routines calculate some "magic" numbers.  Fixes      *
* the snow seen when accessing the framebuffer without it.                   *
* It just works (I hope).                                                    *
*                                                                            *
\****************************************************************************/

struct nv_fifo_info {
	int graphics_lwm;
	int video_lwm;
	int graphics_burst_size;
	int video_burst_size;
	bool valid;
};

struct nv_sim_state {
	int pclk_khz;
	int mclk_khz;
	int nvclk_khz;
	int pix_bpp;
	bool enable_mp;
	bool enable_video;
	int mem_page_miss;
	int mem_latency;
	int memory_type;
	int memory_width;
};

static void
nv4CalcArbitration(struct nv_fifo_info *fifo, struct nv_sim_state *arb)
{
	int pagemiss, cas, width, video_enable, bpp;
	int nvclks, mclks, pclks, vpagemiss, crtpagemiss, vbs;
	int found, mclk_extra, mclk_loop, cbs, m1, p1;
	int mclk_freq, pclk_freq, nvclk_freq, mp_enable;
	int us_m, us_n, us_p, video_drain_rate, crtc_drain_rate;
	int vpm_us, us_video, vlwm, video_fill_us, cpm_us, us_crt, clwm;

	pclk_freq = arb->pclk_khz;
	mclk_freq = arb->mclk_khz;
	nvclk_freq = arb->nvclk_khz;
	pagemiss = arb->mem_page_miss;
	cas = arb->mem_latency;
	width = arb->memory_width >> 6;
	video_enable = arb->enable_video;
	bpp = arb->pix_bpp;
	mp_enable = arb->enable_mp;
	clwm = 0;
	vlwm = 0;
	cbs = 128;
	pclks = 2;
	nvclks = 2;
	nvclks += 2;
	nvclks += 1;
	mclks = 5;
	mclks += 3;
	mclks += 1;
	mclks += cas;
	mclks += 1;
	mclks += 1;
	mclks += 1;
	mclks += 1;
	mclk_extra = 3;
	nvclks += 2;
	nvclks += 1;
	nvclks += 1;
	nvclks += 1;
	if (mp_enable)
		mclks += 4;
	nvclks += 0;
	pclks += 0;
	found = 0;
	vbs = 0;
	while (found != 1) {
		fifo->valid = true;
		found = 1;
		mclk_loop = mclks + mclk_extra;
		us_m = mclk_loop * 1000 * 1000 / mclk_freq;
		us_n = nvclks * 1000 * 1000 / nvclk_freq;
		us_p = nvclks * 1000 * 1000 / pclk_freq;
		if (video_enable) {
			video_drain_rate = pclk_freq * 2;
			crtc_drain_rate = pclk_freq * bpp / 8;
			vpagemiss = 2;
			vpagemiss += 1;
			crtpagemiss = 2;
			vpm_us = vpagemiss * pagemiss * 1000 * 1000 / mclk_freq;
			if (nvclk_freq * 2 > mclk_freq * width)
				video_fill_us = cbs * 1000 * 1000 / 16 / nvclk_freq;
			else
				video_fill_us = cbs * 1000 * 1000 / (8 * width) / mclk_freq;
			us_video = vpm_us + us_m + us_n + us_p + video_fill_us;
			vlwm = us_video * video_drain_rate / (1000 * 1000);
			vlwm++;
			vbs = 128;
			if (vlwm > 128)
				vbs = 64;
			if (vlwm > (256 - 64))
				vbs = 32;
			if (nvclk_freq * 2 > mclk_freq * width)
				video_fill_us = vbs * 1000 * 1000 / 16 / nvclk_freq;
			else
				video_fill_us = vbs * 1000 * 1000 / (8 * width) / mclk_freq;
			cpm_us = crtpagemiss * pagemiss * 1000 * 1000 / mclk_freq;
			us_crt = us_video + video_fill_us + cpm_us + us_m + us_n + us_p;
			clwm = us_crt * crtc_drain_rate / (1000 * 1000);
			clwm++;
		} else {
			crtc_drain_rate = pclk_freq * bpp / 8;
			crtpagemiss = 2;
			crtpagemiss += 1;
			cpm_us = crtpagemiss * pagemiss * 1000 * 1000 / mclk_freq;
			us_crt = cpm_us + us_m + us_n + us_p;
			clwm = us_crt * crtc_drain_rate / (1000 * 1000);
			clwm++;
		}
		m1 = clwm + cbs - 512;
		p1 = m1 * pclk_freq / mclk_freq;
		p1 = p1 * bpp / 8;
		if ((p1 < m1 && m1 > 0) ||
		    (video_enable && (clwm > 511 || vlwm > 255)) ||
		    (!video_enable && clwm > 519)) {
			fifo->valid = false;
			found = !mclk_extra;
			mclk_extra--;
		}
		if (clwm < 384)
			clwm = 384;
		if (vlwm < 128)
			vlwm = 128;
		fifo->graphics_lwm = clwm;
		fifo->graphics_burst_size = 128;
		fifo->video_lwm = vlwm + 15;
		fifo->video_burst_size = vbs;
	}
}

static void
nv10CalcArbitration(struct nv_fifo_info *fifo, struct nv_sim_state *arb)
{
	int pagemiss, width, video_enable, bpp;
	int nvclks, mclks, pclks, vpagemiss, crtpagemiss;
	int nvclk_fill;
	int found, mclk_extra, mclk_loop, cbs, m1;
	int mclk_freq, pclk_freq, nvclk_freq, mp_enable;
	int us_m, us_m_min, us_n, us_p, crtc_drain_rate;
	int vus_m;
	int vpm_us, us_video, cpm_us, us_crt, clwm;
	int clwm_rnd_down;
	int m2us, us_pipe_min, p1clk, p2;
	int min_mclk_extra;
	int us_min_mclk_extra;

	pclk_freq = arb->pclk_khz;	/* freq in KHz */
	mclk_freq = arb->mclk_khz;
	nvclk_freq = arb->nvclk_khz;
	pagemiss = arb->mem_page_miss;
	width = arb->memory_width / 64;
	video_enable = arb->enable_video;
	bpp = arb->pix_bpp;
	mp_enable = arb->enable_mp;
	clwm = 0;
	cbs = 512;
	pclks = 4;	/* lwm detect. */
	nvclks = 3;	/* lwm -> sync. */
	nvclks += 2;	/* fbi bus cycles (1 req + 1 busy) */
	mclks = 1;	/* 2 edge sync.  may be very close to edge so just put one. */
	mclks += 1;	/* arb_hp_req */
	mclks += 5;	/* ap_hp_req   tiling pipeline */
	mclks += 2;	/* tc_req     latency fifo */
	mclks += 2;	/* fb_cas_n_  memory request to fbio block */
	mclks += 7;	/* sm_d_rdv   data returned from fbio block */

	/* fb.rd.d.Put_gc   need to accumulate 256 bits for read */
	if (arb->memory_type == 0) {
		if (arb->memory_width == 64)	/* 64 bit bus */
			mclks += 4;
		else
			mclks += 2;
	} else if (arb->memory_width == 64)	/* 64 bit bus */
		mclks += 2;
	else
		mclks += 1;

	if (!video_enable && arb->memory_width == 128) {
		mclk_extra = (bpp == 32) ? 31 : 42;	/* Margin of error */
		min_mclk_extra = 17;
	} else {
		mclk_extra = (bpp == 32) ? 8 : 4;	/* Margin of error */
		/* mclk_extra = 4; *//* Margin of error */
		min_mclk_extra = 18;
	}

	nvclks += 1;	/* 2 edge sync.  may be very close to edge so just put one. */
	nvclks += 1;	/* fbi_d_rdv_n */
	nvclks += 1;	/* Fbi_d_rdata */
	nvclks += 1;	/* crtfifo load */

	if (mp_enable)
		mclks += 4;	/* Mp can get in with a burst of 8. */
	/* Extra clocks determined by heuristics */

	nvclks += 0;
	pclks += 0;
	found = 0;
	while (found != 1) {
		fifo->valid = true;
		found = 1;
		mclk_loop = mclks + mclk_extra;
		us_m = mclk_loop * 1000 * 1000 / mclk_freq;	/* Mclk latency in us */
		us_m_min = mclks * 1000 * 1000 / mclk_freq;	/* Minimum Mclk latency in us */
		us_min_mclk_extra = min_mclk_extra * 1000 * 1000 / mclk_freq;
		us_n = nvclks * 1000 * 1000 / nvclk_freq;	/* nvclk latency in us */
		us_p = pclks * 1000 * 1000 / pclk_freq;	/* nvclk latency in us */
		us_pipe_min = us_m_min + us_n + us_p;

		vus_m = mclk_loop * 1000 * 1000 / mclk_freq;	/* Mclk latency in us */

		if (video_enable) {
			crtc_drain_rate = pclk_freq * bpp / 8;	/* MB/s */

			vpagemiss = 1;	/* self generating page miss */
			vpagemiss += 1;	/* One higher priority before */

			crtpagemiss = 2;	/* self generating page miss */
			if (mp_enable)
				crtpagemiss += 1;	/* if MA0 conflict */

			vpm_us = vpagemiss * pagemiss * 1000 * 1000 / mclk_freq;

			us_video = vpm_us + vus_m;	/* Video has separate read return path */

			cpm_us = crtpagemiss * pagemiss * 1000 * 1000 / mclk_freq;
			us_crt = us_video	/* Wait for video */
				 + cpm_us	/* CRT Page miss */
				 + us_m + us_n + us_p;	/* other latency */

			clwm = us_crt * crtc_drain_rate / (1000 * 1000);
			clwm++;	/* fixed point <= float_point - 1.  Fixes that */
		} else {
			crtc_drain_rate = pclk_freq * bpp / 8;	/* bpp * pclk/8 */

			crtpagemiss = 1;	/* self generating page miss */
			crtpagemiss += 1;	/* MA0 page miss */
			if (mp_enable)
				crtpagemiss += 1;	/* if MA0 conflict */
			cpm_us = crtpagemiss * pagemiss * 1000 * 1000 / mclk_freq;
			us_crt = cpm_us + us_m + us_n + us_p;
			clwm = us_crt * crtc_drain_rate / (1000 * 1000);
			clwm++;	/* fixed point <= float_point - 1.  Fixes that */

			/* Finally, a heuristic check when width == 64 bits */
			if (width == 1) {
				nvclk_fill = nvclk_freq * 8;
				if (crtc_drain_rate * 100 >= nvclk_fill * 102)
					clwm = 0xfff;	/* Large number to fail */
				else if (crtc_drain_rate * 100 >= nvclk_fill * 98) {
					clwm = 1024;
					cbs = 512;
				}
			}
		}

		/*
		 * Overfill check:
		 */

		clwm_rnd_down = (clwm / 8) * 8;
		if (clwm_rnd_down < clwm)
			clwm += 8;

		m1 = clwm + cbs - 1024;	/* Amount of overfill */
		m2us = us_pipe_min + us_min_mclk_extra;

		/* pclk cycles to drain */
		p1clk = m2us * pclk_freq / (1000 * 1000);
		p2 = p1clk * bpp / 8;	/* bytes drained. */

		if (p2 < m1 && m1 > 0) {
			fifo->valid = false;
			found = 0;
			if (min_mclk_extra == 0) {
				if (cbs <= 32)
					found = 1;	/* Can't adjust anymore! */
				else
					cbs = cbs / 2;	/* reduce the burst size */
			} else
				min_mclk_extra--;
		} else if (clwm > 1023) {	/* Have some margin */
			fifo->valid = false;
			found = 0;
			if (min_mclk_extra == 0)
				found = 1;	/* Can't adjust anymore! */
			else
				min_mclk_extra--;
		}

		if (clwm < (1024 - cbs + 8))
			clwm = 1024 - cbs + 8;
		/*  printf("CRT LWM: prog: 0x%x, bs: 256\n", clwm); */
		fifo->graphics_lwm = clwm;
		fifo->graphics_burst_size = cbs;

		fifo->video_lwm = 1024;
		fifo->video_burst_size = 512;
	}
}

static void
nv4_10UpdateArbitrationSettings(struct drm_device *dev, int VClk, int bpp,
				int *burst, int *lwm)
{
	struct nv_fifo_info fifo_data;
	struct nv_sim_state sim_data;
	int MClk = nouveau_hw_get_clock(dev, MPLL);
	int NVClk = nouveau_hw_get_clock(dev, NVPLL);
	uint32_t cfg1 = nvReadFB(dev, NV_PFB_CFG1);

	sim_data.pclk_khz = VClk;
	sim_data.mclk_khz = MClk;
	sim_data.nvclk_khz = NVClk;
	sim_data.pix_bpp = bpp;
	sim_data.enable_mp = false;
	if ((dev->pci_device & 0xffff) == 0x01a0 /*CHIPSET_NFORCE*/ ||
	    (dev->pci_device & 0xffff) == 0x01f0 /*CHIPSET_NFORCE2*/) {
		uint32_t type;

		pci_read_config_dword(pci_get_bus_and_slot(0, 1), 0x7c, &type);

		sim_data.enable_video = false;
		sim_data.memory_type = (type >> 12) & 1;
		sim_data.memory_width = 64;
		sim_data.mem_latency = 3;
		sim_data.mem_page_miss = 10;
	} else {
		sim_data.enable_video = (nv_arch(dev) != NV_04);
		sim_data.memory_type = nvReadFB(dev, NV_PFB_CFG0) & 0x1;
		sim_data.memory_width = (nvReadEXTDEV(dev, NV_PEXTDEV_BOOT_0) & 0x10) ? 128 : 64;
		sim_data.mem_latency = cfg1 & 0xf;
		sim_data.mem_page_miss = ((cfg1 >> 4) & 0xf) + ((cfg1 >> 31) & 0x1);
	}

	if (nv_arch(dev) == NV_04)
		nv4CalcArbitration(&fifo_data, &sim_data);
	else
		nv10CalcArbitration(&fifo_data, &sim_data);

	if (fifo_data.valid) {
		int b = fifo_data.graphics_burst_size >> 4;
		*burst = 0;
		while (b >>= 1)
			(*burst)++;
		*lwm = fifo_data.graphics_lwm >> 3;
	}
}

static void
nv30UpdateArbitrationSettings(int *burst, int *lwm)
{
	unsigned int fifo_size, burst_size, graphics_lwm;

	fifo_size = 2048;
	burst_size = 512;
	graphics_lwm = fifo_size - burst_size;

	*burst = 0;
	burst_size >>= 5;
	while (burst_size >>= 1)
		(*burst)++;
	*lwm = graphics_lwm >> 3;
}

void
nouveau_calc_arb(struct drm_device *dev, int vclk, int bpp, int *burst, int *lwm)
{
	if (nv_arch(dev) < NV_30)
		nv4_10UpdateArbitrationSettings(dev, vclk, bpp, burst, lwm);
	else if ((dev->pci_device & 0xfff0) == 0x0240 /*CHIPSET_C51*/ ||
		 (dev->pci_device & 0xfff0) == 0x03d0 /*CHIPSET_C512*/) {
		*burst = 128;
		*lwm = 0x0480;
	} else
		nv30UpdateArbitrationSettings(burst, lwm);
}

static int
getMNP_single(struct drm_device *dev, struct pll_lims *pll_lim, int clk,
	      struct nouveau_pll_vals *bestpv)
{
	/* Find M, N and P for a single stage PLL
	 *
	 * Note that some bioses (NV3x) have lookup tables of precomputed MNP
	 * values, but we're too lazy to use those atm
	 *
	 * "clk" parameter in kHz
	 * returns calculated clock
	 */
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	int cv = dev_priv->vbios->chip_version;
	int minvco = pll_lim->vco1.minfreq, maxvco = pll_lim->vco1.maxfreq;
	int minM = pll_lim->vco1.min_m, maxM = pll_lim->vco1.max_m;
	int minN = pll_lim->vco1.min_n, maxN = pll_lim->vco1.max_n;
	int minU = pll_lim->vco1.min_inputfreq, maxU = pll_lim->vco1.max_inputfreq;
	int maxlog2P = pll_lim->max_usable_log2p;
	int crystal = pll_lim->refclk;
	int M, N, log2P, P;
	int clkP, calcclk;
	int delta, bestdelta = INT_MAX;
	int bestclk = 0;

	/* this division verified for nv20, nv18, nv28 (Haiku), and nv34 */
	/* possibly correlated with introduction of 27MHz crystal */
	if (cv < 0x17 || cv == 0x1a || cv == 0x20) {
		if (clk > 250000)
			maxM = 6;
		if (clk > 340000)
			maxM = 2;
	} else if (cv < 0x40) {
		if (clk > 150000)
			maxM = 6;
		if (clk > 200000)
			maxM = 4;
		if (clk > 340000)
			maxM = 2;
	}

	if ((clk << maxlog2P) < minvco) {
		minvco = clk << maxlog2P;
		maxvco = minvco * 2;
	}
	if (clk + clk/200 > maxvco)	/* +0.5% */
		maxvco = clk + clk/200;

	/* NV34 goes maxlog2P->0, NV20 goes 0->maxlog2P */
	for (log2P = 0; log2P <= maxlog2P; log2P++) {
		P = 1 << log2P;
		clkP = clk * P;

		if (clkP < minvco)
			continue;
		if (clkP > maxvco)
			return bestclk;

		for (M = minM; M <= maxM; M++) {
			if (crystal/M < minU)
				return bestclk;
			if (crystal/M > maxU)
				continue;

			/* add crystal/2 to round better */
			N = (clkP * M + crystal/2) / crystal;

			if (N < minN)
				continue;
			if (N > maxN)
				break;

			/* more rounding additions */
			calcclk = ((N * crystal + P/2) / P + M/2) / M;
			delta = abs(calcclk - clk);
			/* we do an exhaustive search rather than terminating
			 * on an optimality condition...
			 */
			if (delta < bestdelta) {
				bestdelta = delta;
				bestclk = calcclk;
				bestpv->N1 = N;
				bestpv->M1 = M;
				bestpv->log2P = log2P;
				if (delta == 0)	/* except this one */
					return bestclk;
			}
		}
	}

	return bestclk;
}

static int
getMNP_double(struct drm_device *dev, struct pll_lims *pll_lim, int clk,
	      struct nouveau_pll_vals *bestpv)
{
	/* Find M, N and P for a two stage PLL
	 *
	 * Note that some bioses (NV30+) have lookup tables of precomputed MNP
	 * values, but we're too lazy to use those atm
	 *
	 * "clk" parameter in kHz
	 * returns calculated clock
	 */
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	int chip_version = dev_priv->vbios->chip_version;
	int minvco1 = pll_lim->vco1.minfreq, maxvco1 = pll_lim->vco1.maxfreq;
	int minvco2 = pll_lim->vco2.minfreq, maxvco2 = pll_lim->vco2.maxfreq;
	int minU1 = pll_lim->vco1.min_inputfreq, minU2 = pll_lim->vco2.min_inputfreq;
	int maxU1 = pll_lim->vco1.max_inputfreq, maxU2 = pll_lim->vco2.max_inputfreq;
	int minM1 = pll_lim->vco1.min_m, maxM1 = pll_lim->vco1.max_m;
	int minN1 = pll_lim->vco1.min_n, maxN1 = pll_lim->vco1.max_n;
	int minM2 = pll_lim->vco2.min_m, maxM2 = pll_lim->vco2.max_m;
	int minN2 = pll_lim->vco2.min_n, maxN2 = pll_lim->vco2.max_n;
	int maxlog2P = pll_lim->max_usable_log2p;
	int crystal = pll_lim->refclk;
	bool fixedgain2 = (minM2 == maxM2 && minN2 == maxN2);
	int M1, N1, M2, N2, log2P;
	int clkP, calcclk1, calcclk2, calcclkout;
	int delta, bestdelta = INT_MAX;
	int bestclk = 0;

	int vco2 = (maxvco2 - maxvco2/200) / 2;
	for (log2P = 0; clk && log2P < maxlog2P && clk <= (vco2 >> log2P); log2P++)
		;
	clkP = clk << log2P;

	if (maxvco2 < clk + clk/200)	/* +0.5% */
		maxvco2 = clk + clk/200;

	for (M1 = minM1; M1 <= maxM1; M1++) {
		if (crystal/M1 < minU1)
			return bestclk;
		if (crystal/M1 > maxU1)
			continue;

		for (N1 = minN1; N1 <= maxN1; N1++) {
			calcclk1 = crystal * N1 / M1;
			if (calcclk1 < minvco1)
				continue;
			if (calcclk1 > maxvco1)
				break;

			for (M2 = minM2; M2 <= maxM2; M2++) {
				if (calcclk1/M2 < minU2)
					break;
				if (calcclk1/M2 > maxU2)
					continue;

				/* add calcclk1/2 to round better */
				N2 = (clkP * M2 + calcclk1/2) / calcclk1;
				if (N2 < minN2)
					continue;
				if (N2 > maxN2)
					break;

				if (!fixedgain2) {
					if (chip_version < 0x60)
						if (N2/M2 < 4 || N2/M2 > 10)
							continue;

					calcclk2 = calcclk1 * N2 / M2;
					if (calcclk2 < minvco2)
						break;
					if (calcclk2 > maxvco2)
						continue;
				} else
					calcclk2 = calcclk1;

				calcclkout = calcclk2 >> log2P;
				delta = abs(calcclkout - clk);
				/* we do an exhaustive search rather than terminating
				 * on an optimality condition...
				 */
				if (delta < bestdelta) {
					bestdelta = delta;
					bestclk = calcclkout;
					bestpv->N1 = N1;
					bestpv->M1 = M1;
					bestpv->N2 = N2;
					bestpv->M2 = M2;
					bestpv->log2P = log2P;
					if (delta == 0)	/* except this one */
						return bestclk;
				}
			}
		}
	}

	return bestclk;
}

int
nouveau_calc_pll_mnp(struct drm_device *dev, struct pll_lims *pll_lim, int clk,
		     struct nouveau_pll_vals *pv)
{
	int outclk;

	if (!pll_lim->vco2.maxfreq)
		outclk = getMNP_single(dev, pll_lim, clk, pv);
	else
		outclk = getMNP_double(dev, pll_lim, clk, pv);

	if (!outclk)
		NV_ERROR(dev, "Could not find a compatible set of PLL values\n");

	return outclk;
}
