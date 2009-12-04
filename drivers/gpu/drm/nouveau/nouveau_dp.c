/*
 * Copyright 2009 Red Hat Inc.
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
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 * Authors: Ben Skeggs
 */

#include "drmP.h"
#include "nouveau_drv.h"
#include "nouveau_i2c.h"
#include "nouveau_encoder.h"

int
nouveau_dp_auxch(struct nouveau_i2c_chan *auxch, int cmd, int addr,
		 uint8_t *data, int data_nr)
{
	struct drm_device *dev = auxch->dev;
	uint32_t tmp, ctrl, stat = 0, data32[4] = {};
	int ret = 0, i, index = auxch->rd;

	tmp = nv_rd32(dev, NV50_AUXCH_CTRL(auxch->rd));
	nv_wr32(dev, NV50_AUXCH_CTRL(auxch->rd), tmp | 0x00100000);
	tmp = nv_rd32(dev, NV50_AUXCH_CTRL(auxch->rd));
	if (!(tmp & 0x01000000)) {
		NV_ERROR(dev, "expected bit 24 == 1, got 0x%08x\n", tmp);
		ret = -EIO;
		goto out;
	}

	for (i = 0; i < 3; i++) {
		tmp = nv_rd32(dev, NV50_AUXCH_STAT(auxch->rd));
		if (tmp & NV50_AUXCH_STAT_STATE_READY)
			break;
		udelay(100);
	}

	if (i == 3) {
		ret = -EBUSY;
		goto out;
	}

	if (!(cmd & 1)) {
		memcpy(data32, data, data_nr);
		for (i = 0; i < 4; i++)
			nv_wr32(dev, NV50_AUXCH_DATA_OUT(index, i), data32[i]);
	}

	nv_wr32(dev, NV50_AUXCH_ADDR(index), addr);
	ctrl  = nv_rd32(dev, NV50_AUXCH_CTRL(index));
	ctrl &= ~(NV50_AUXCH_CTRL_CMD | NV50_AUXCH_CTRL_LEN);
	ctrl |= (cmd << NV50_AUXCH_CTRL_CMD_SHIFT);
	ctrl |= ((data_nr - 1) << NV50_AUXCH_CTRL_LEN_SHIFT);

	for (;;) {
		nv_wr32(dev, NV50_AUXCH_CTRL(index), ctrl | 0x80000000);
		nv_wr32(dev, NV50_AUXCH_CTRL(index), ctrl);
		nv_wr32(dev, NV50_AUXCH_CTRL(index), ctrl | 0x00010000);
		if (!nv_wait(NV50_AUXCH_CTRL(index), 0x00010000, 0x00000000)) {
			NV_ERROR(dev, "expected bit 16 == 0, got 0x%08x\n",
				 nv_rd32(dev, NV50_AUXCH_CTRL(index)));
			return -EBUSY;
		}

		udelay(400);

		stat = nv_rd32(dev, NV50_AUXCH_STAT(index));
		if ((stat & NV50_AUXCH_STAT_REPLY_AUX) !=
			    NV50_AUXCH_STAT_REPLY_AUX_DEFER)
			break;
	}

	if (cmd & 1) {
		for (i = 0; i < 4; i++)
			data32[i] = nv_rd32(dev, NV50_AUXCH_DATA_IN(index, i));
		memcpy(data, data32, data_nr);
	}

out:
	tmp = nv_rd32(dev, NV50_AUXCH_CTRL(auxch->rd));
	nv_wr32(dev, NV50_AUXCH_CTRL(auxch->rd), tmp & ~0x00100000);
	tmp = nv_rd32(dev, NV50_AUXCH_CTRL(auxch->rd));
	if (tmp & 0x01000000) {
		NV_ERROR(dev, "expected bit 24 == 0, got 0x%08x\n", tmp);
		ret = -EIO;
	}

	udelay(400);

	return ret ? ret : (stat & NV50_AUXCH_STAT_REPLY);
}

int
nouveau_dp_i2c_aux_ch(struct i2c_adapter *adapter,
		      uint8_t *send, int send_bytes,
		      uint8_t *recv, int recv_bytes)
{
	struct nouveau_i2c_chan *auxch = (struct nouveau_i2c_chan *)adapter;
	int ret = 0, cmd, addr;
	uint8_t *status;

	/* adjust receive info, we don't return status in buffer */
	status = &recv[0];
	recv++;
	recv_bytes--;

	/* adjust send info, we don't put cmd/addr etc into buffer */
	cmd = send[0] >> 4;
	addr = (send[1] << 8) | send[2];
	send += 3;
	send_bytes -= 3;

	/* write command, adjust, we don't put length byte into buffer */
	if (!recv_bytes && send_bytes) {
		int len = send[0] + 1;

		ret = nouveau_dp_auxch(auxch, cmd, addr, send + 1, len);
		if (ret < 0)
			return ret;
	} else
	if (recv_bytes) {
		ret = nouveau_dp_auxch(auxch, cmd, addr, recv, recv_bytes);
		if (ret < 0)
			return ret;
	}

	*status = ((ret & NV50_AUXCH_STAT_REPLY) >> 16) << 4;
	return 0;
}

