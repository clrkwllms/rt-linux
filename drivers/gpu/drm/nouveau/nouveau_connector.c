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
#include "drm_edid.h"
#include "drm_crtc_helper.h"
#include "nouveau_reg.h"
#include "nouveau_drv.h"
#include "nouveau_encoder.h"
#include "nouveau_crtc.h"
#include "nouveau_connector.h"
#include "nouveau_hw.h"

extern int nouveau_duallink;

static struct nouveau_encoder *
nouveau_connector_encoder_get(struct drm_connector *connector, int type)
{
	struct drm_device *dev = connector->dev;
	struct nouveau_encoder *nv_encoder;
	struct drm_mode_object *obj;
	int i, id;

	for (i = 0; i < DRM_CONNECTOR_MAX_ENCODER; i++) {
		id = connector->encoder_ids[i];
		if (!id)
			break;

		obj = drm_mode_object_find(dev, id, DRM_MODE_OBJECT_ENCODER);
		if (!obj)
			continue;
		nv_encoder = nouveau_encoder(obj_to_encoder(obj));

		if (nv_encoder->dcb->type == type)
			return nv_encoder;
	}

	return NULL;
}

static void
nouveau_connector_destroy(struct drm_connector *drm_connector)
{
	struct nouveau_connector *connector = nouveau_connector(drm_connector);
	struct drm_device *dev = connector->base.dev;

	NV_DEBUG(dev, "\n");

	if (!connector)
		return;

	nouveau_i2c_del(&connector->i2c_chan);

	drm_sysfs_connector_remove(drm_connector);
	drm_connector_cleanup(drm_connector);
	kfree(drm_connector);
}

static void
nouveau_connector_ddc_prepare(struct drm_connector *connector, int *flags)
{
	struct drm_nouveau_private *dev_priv = connector->dev->dev_private;

	if (dev_priv->card_type >= NV_50)
		return;

	*flags = 0;
	if (NVLockVgaCrtcs(dev_priv->dev, false))
		*flags |= 1;
	if (nv_heads_tied(dev_priv->dev))
		*flags |= 2;

	if (*flags & 2)
		NVSetOwner(dev_priv->dev, 0); /* necessary? */
}

static void
nouveau_connector_ddc_finish(struct drm_connector *connector, int flags)
{
	struct drm_nouveau_private *dev_priv = connector->dev->dev_private;

	if (dev_priv->card_type >= NV_50)
		return;

	if (flags & 2)
		NVSetOwner(dev_priv->dev, 4);
	if (flags & 1)
		NVLockVgaCrtcs(dev_priv->dev, true);
}

static bool
nouveau_connector_ddc_detect(struct drm_connector *connector)
{
	struct nouveau_connector *nv_connector = nouveau_connector(connector);
	/* kindly borrrowed from the intel driver, hope it works. */
	uint8_t out_buf[] = { 0x0, 0x0};
	uint8_t buf[2];
	int ret, flags;
	struct i2c_msg msgs[] = {
		{
			.addr = 0x50,
			.flags = 0,
			.len = 1,
			.buf = out_buf,
		},
		{
			.addr = 0x50,
			.flags = I2C_M_RD,
			.len = 1,
			.buf = buf,
		}
	};

	if (!nv_connector->i2c_chan)
		return false;

	nouveau_connector_ddc_prepare(connector, &flags);
	ret = i2c_transfer(&nv_connector->i2c_chan->adapter, msgs, 2);
	nouveau_connector_ddc_finish(connector, flags);
	if (ret == 2)
		return true;
	return false;
}

static void
nouveau_connector_set_encoder(struct drm_connector *connector,
			      struct nouveau_encoder *nv_encoder)
{
	struct nouveau_connector *nv_connector = nouveau_connector(connector);
	struct drm_nouveau_private *dev_priv = connector->dev->dev_private;
	struct drm_device *dev = connector->dev;

	if (nv_connector->detected_encoder == nv_encoder)
		return;
	nv_connector->detected_encoder = nv_encoder;

	if (nv_encoder->dcb->type == OUTPUT_LVDS ||
	    nv_encoder->dcb->type == OUTPUT_TMDS) {
		connector->doublescan_allowed = false;
		connector->interlace_allowed = false;
	} else {
		connector->doublescan_allowed = true;
		if (dev_priv->card_type == NV_20 ||
		   (dev_priv->card_type == NV_10 &&
		    (dev->pci_device & 0x0ff0) != 0x0100 &&
		    (dev->pci_device & 0x0ff0) != 0x0150))
			/* HW is broken */
			connector->interlace_allowed = false;
		else
			connector->interlace_allowed = true;
	}
}

static enum drm_connector_status
nouveau_connector_lvds_detect(struct drm_connector *connector)
{
	struct nouveau_connector *nv_connector = nouveau_connector(connector);
	struct nouveau_encoder *nv_encoder;
	struct drm_device *dev = connector->dev;
	int flags;

	nv_encoder = nouveau_connector_encoder_get(connector, OUTPUT_LVDS);
	if (!nv_encoder) {
		NV_ERROR(dev, "LVDS but no encoder!\n");
		return connector_status_disconnected;
	}

	if (nv_connector->native_mode) {
		nouveau_connector_set_encoder(connector, nv_encoder);
		return connector_status_connected;
	}

	nouveau_connector_ddc_prepare(connector, &flags);
	nv_connector->edid =
		drm_get_edid(connector, &nv_connector->i2c_chan->adapter);
	nouveau_connector_ddc_finish(connector, flags);
	drm_mode_connector_update_edid_property(connector, nv_connector->edid);
	if (!nv_connector->edid) {
		NV_ERROR(dev, "LVDS but no modes!\n");
		return connector_status_disconnected;
	}

	nouveau_connector_set_encoder(connector, nv_encoder);
	return connector_status_connected;
}

static enum drm_connector_status
nouveau_connector_detect(struct drm_connector *connector)
{
	struct drm_device *dev = connector->dev;
	struct nouveau_connector *nv_connector = nouveau_connector(connector);
	struct drm_encoder_helper_funcs *helper = NULL;
	struct nouveau_encoder *nv_encoder = NULL;
	int type, flags;

	nv_connector->detected_encoder = NULL;

	if (connector->connector_type == DRM_MODE_CONNECTOR_LVDS)
		return nouveau_connector_lvds_detect(connector);

	nv_encoder = nouveau_connector_encoder_get(connector, OUTPUT_ANALOG);
	if (!nouveau_connector_ddc_detect(connector)) {
		if (!nv_encoder || !nv_encoder->base.helper_private)
			return connector_status_disconnected;

		helper = nv_encoder->base.helper_private;
		if (!helper || !helper->detect)
			return connector_status_disconnected;

		if (helper->detect(&nv_encoder->base, connector) ==
				connector_status_connected) {
			nouveau_connector_set_encoder(connector, nv_encoder);
			return connector_status_connected;
		}

		return connector_status_disconnected;
	}

	nouveau_connector_ddc_prepare(connector, &flags);
	nv_connector->edid =
		drm_get_edid(connector, &nv_connector->i2c_chan->adapter);
	nouveau_connector_ddc_finish(connector, flags);
	drm_mode_connector_update_edid_property(connector, nv_connector->edid);
	if (!nv_connector->edid) {
		NV_ERROR(dev, "DDC responded, but no EDID for %s\n",
			      drm_get_connector_name(connector));
		return connector_status_disconnected;
	}

	if (nv_connector->edid->input & DRM_EDID_INPUT_DIGITAL)
		type = OUTPUT_TMDS;
	else
		type = OUTPUT_ANALOG;

	nv_encoder = nouveau_connector_encoder_get(connector, type);
	if (!nv_encoder) {
		NV_ERROR(dev, "Detected %d encoder on %s, but no object!\n",
			      type, drm_get_connector_name(connector));
		return connector_status_disconnected;
	}

	nouveau_connector_set_encoder(connector, nv_encoder);
	return connector_status_connected;
}

static int
nouveau_connector_set_property(struct drm_connector *connector,
			       struct drm_property *property, uint64_t value)
{
	struct nouveau_connector *nv_connector = nouveau_connector(connector);
	struct drm_device *dev = connector->dev;
	int ret;

	/* Scaling mode */
	if (property == dev->mode_config.scaling_mode_property) {
		struct nouveau_crtc *nv_crtc = NULL;
		bool modeset = false;

		switch (value) {
		case DRM_MODE_SCALE_NON_GPU:
		case DRM_MODE_SCALE_FULLSCREEN:
		case DRM_MODE_SCALE_NO_SCALE:
		case DRM_MODE_SCALE_ASPECT:
			break;
		default:
			return -EINVAL;
		}

		/* LVDS always needs gpu scaling */
		if (connector->connector_type == DRM_MODE_CONNECTOR_LVDS &&
		    value == DRM_MODE_SCALE_NON_GPU)
			return -EINVAL;

		/* Changing between GPU and panel scaling requires a full
		 * modeset
		 */
		if ((nv_connector->scaling_mode == DRM_MODE_SCALE_NON_GPU) ||
		    (value == DRM_MODE_SCALE_NON_GPU))
			modeset = true;
		nv_connector->scaling_mode = value;

		if (connector->encoder && connector->encoder->crtc)
			nv_crtc = nouveau_crtc(connector->encoder->crtc);
		if (!nv_crtc)
			return 0;

		if (modeset || !nv_crtc->set_scale) {
			ret = drm_crtc_helper_set_mode(&nv_crtc->base,
							&nv_crtc->base.mode,
							nv_crtc->base.x,
							nv_crtc->base.y, NULL);
			if (ret)
				return ret;
		} else {
			ret = nv_crtc->set_scale(nv_crtc, value, true);
			if (ret)
				return ret;
		}

		return 0;
	}

	/* Dithering */
	if (property == dev->mode_config.dithering_mode_property) {
		struct nouveau_crtc *nv_crtc = NULL;

		if (value == DRM_MODE_DITHERING_ON)
			nv_connector->use_dithering = true;
		else
			nv_connector->use_dithering = false;

		if (connector->encoder && connector->encoder->crtc)
			nv_crtc = nouveau_crtc(connector->encoder->crtc);

		if (!nv_crtc || !nv_crtc->set_dither)
			return 0;

		return nv_crtc->set_dither(nv_crtc, nv_connector->use_dithering,
					   true);
	}

	return -EINVAL;
}

static struct drm_display_mode *
nouveau_connector_native_mode(struct nouveau_connector *connector)
{
	struct drm_device *dev = connector->base.dev;
	struct drm_display_mode *mode;

	if (connector->detected_encoder->dcb->type == OUTPUT_ANALOG)
		return NULL;

	list_for_each_entry(mode, &connector->base.probed_modes, head) {
		if (mode->type & DRM_MODE_TYPE_PREFERRED)
			return drm_mode_duplicate(dev, mode);
	}

	return NULL;
}

static int
nouveau_connector_get_modes(struct drm_connector *connector)
{
	struct nouveau_connector *nv_connector = nouveau_connector(connector);
	struct drm_device *dev = connector->dev;
	int ret = 0;

	/* If we're not LVDS, destroy the previous native mode, the attached
	 * monitor could have changed.
	 */
	if (connector->connector_type != DRM_MODE_CONNECTOR_LVDS &&
	    nv_connector->native_mode) {
		drm_mode_destroy(dev, nv_connector->native_mode);
		nv_connector->native_mode = NULL;
	}

	if (nv_connector->edid)
		ret = drm_add_edid_modes(connector, nv_connector->edid);

	/* Find the native mode if this is a digital panel, if we didn't
	 * find any modes through DDC previously add the native mode to
	 * the list of modes.
	 */
	if (!nv_connector->native_mode)
		nv_connector->native_mode =
			nouveau_connector_native_mode(nv_connector);
	if (ret == 0 && nv_connector->native_mode) {
		struct drm_display_mode *mode;

		mode = drm_mode_duplicate(dev, nv_connector->native_mode);
		drm_mode_probed_add(connector, mode);
		ret = 1;
	}

	return ret;
}

static int
nouveau_connector_mode_valid(struct drm_connector *connector,
			     struct drm_display_mode *mode)
{
	struct drm_nouveau_private *dev_priv = connector->dev->dev_private;
	struct nouveau_connector *nv_connector = nouveau_connector(connector);
	struct nouveau_encoder *nv_encoder = nv_connector->detected_encoder;
	unsigned min_clock, max_clock;

	min_clock = 25000;

	switch (nv_encoder->dcb->type) {
	case OUTPUT_LVDS:
		BUG_ON(!nv_connector->native_mode);
		if (mode->hdisplay > nv_connector->native_mode->hdisplay ||
		    mode->vdisplay > nv_connector->native_mode->vdisplay)
			return MODE_PANEL;

		max_clock = 400000;
		break;
	case OUTPUT_TMDS:
		if ((dev_priv->card_type >= NV_50 && !nouveau_duallink) ||
		    (dev_priv->card_type < NV_50 &&
		     !nv_encoder->dcb->duallink_possible))
			max_clock = 165000;
		else
			max_clock = 330000;
		break;
	default:
		max_clock = nv_encoder->dcb->crtconf.maxfreq;
		if (!max_clock)
			max_clock = 350000;
		break;
	}

	if (mode->clock < min_clock)
		return MODE_CLOCK_LOW;

	if (mode->clock > max_clock)
		return MODE_CLOCK_HIGH;

	return MODE_OK;
}

static struct drm_encoder *
nouveau_connector_best_encoder(struct drm_connector *connector)
{
	struct nouveau_connector *nv_connector = nouveau_connector(connector);

	if (nv_connector->detected_encoder)
		return &nv_connector->detected_encoder->base;

	return NULL;
}

static const struct drm_connector_helper_funcs
nouveau_connector_helper_funcs = {
	.get_modes = nouveau_connector_get_modes,
	.mode_valid = nouveau_connector_mode_valid,
	.best_encoder = nouveau_connector_best_encoder,
};

static const struct drm_connector_funcs
nouveau_connector_funcs = {
	.dpms = drm_helper_connector_dpms,
	.save = NULL,
	.restore = NULL,
	.detect = nouveau_connector_detect,
	.destroy = nouveau_connector_destroy,
	.fill_modes = drm_helper_probe_single_connector_modes,
	.set_property = nouveau_connector_set_property
};

static int
nouveau_connector_create_lvds(struct drm_device *dev,
			      struct nouveau_connector *nv_connector)
{
	struct drm_display_mode native;
	bool dummy, if_is_24bit = false;
	int ret;

	ret = nouveau_bios_parse_lvds_table(dev, 0, &dummy, &if_is_24bit);
	if (ret)
		return ret;

	if (nouveau_bios_fp_mode(dev, &native))
		nv_connector->native_mode = drm_mode_duplicate(dev, &native);

	nv_connector->use_dithering = !if_is_24bit;
	return 0;
}

int
nouveau_connector_create(struct drm_device *dev, int i2c_index, int type)
{
	struct nouveau_connector *nv_connector = NULL;
	struct drm_connector *connector;
	struct drm_encoder *encoder;
	int ret;

	NV_DEBUG(dev, "\n");

	nv_connector = kzalloc(sizeof(*nv_connector), GFP_KERNEL);
	if (!nv_connector)
		return -ENOMEM;
	connector = &nv_connector->base;

	switch (type) {
	case DRM_MODE_CONNECTOR_VGA:
		NV_INFO(dev, "Detected a VGA connector\n");
		break;
	case DRM_MODE_CONNECTOR_DVID:
		NV_INFO(dev, "Detected a DVI-D connector\n");
		break;
	case DRM_MODE_CONNECTOR_DVII:
		NV_INFO(dev, "Detected a DVI-I connector\n");
		break;
	case DRM_MODE_CONNECTOR_LVDS:
		NV_INFO(dev, "Detected a LVDS connector\n");

		ret = nouveau_connector_create_lvds(dev, nv_connector);
		if (ret) {
			kfree(nv_connector);
			return ret;
		}
		break;
	case DRM_MODE_CONNECTOR_SVIDEO:
		NV_INFO(dev, "Detected a TV connector\n");
		break;
	default:
		NV_ERROR(dev, "Unknown connector, this is not good.\n");
		break;
	}

	/* some reasonable defaults */
	switch (type) {
	case DRM_MODE_CONNECTOR_DVII:
	case DRM_MODE_CONNECTOR_DVID:
	case DRM_MODE_CONNECTOR_LVDS:
		nv_connector->scaling_mode = DRM_MODE_SCALE_FULLSCREEN;
		break;
	default:
		nv_connector->scaling_mode = DRM_MODE_SCALE_NON_GPU;
		break;
	}

	if (type != DRM_MODE_CONNECTOR_LVDS)
		nv_connector->use_dithering = false;

	/* defaults, will get overridden in detect() */
	connector->interlace_allowed = false;
	connector->doublescan_allowed = false;

	drm_connector_init(dev, connector, &nouveau_connector_funcs, type);
	drm_connector_helper_add(connector, &nouveau_connector_helper_funcs);

	if (i2c_index < 0xf) {
		ret = nouveau_i2c_new(dev, drm_get_connector_name(connector),
				      i2c_index, &nv_connector->i2c_chan);
		if (ret) {
			NV_ERROR(dev, "Error initialising I2C on %s: %d\n",
				 drm_get_connector_name(connector), ret);
		}
	}

	/* Init DVI-I specific properties */
	if (type == DRM_MODE_CONNECTOR_DVII) {
		drm_mode_create_dvi_i_properties(dev);
		drm_connector_attach_property(connector, dev->mode_config.dvi_i_subconnector_property, 0);
		drm_connector_attach_property(connector, dev->mode_config.dvi_i_select_subconnector_property, 0);
	}

	/* If supported in the future, it will have to use the scalers
	 * internally and not expose them.
	 */
	if (type != DRM_MODE_CONNECTOR_SVIDEO) {
		drm_connector_attach_property(connector, dev->mode_config.scaling_mode_property, nv_connector->scaling_mode);
	}

	drm_connector_attach_property(connector, dev->mode_config.dithering_mode_property, nv_connector->use_dithering ? DRM_MODE_DITHERING_ON : DRM_MODE_DITHERING_OFF);

	/* attach encoders */
	list_for_each_entry(encoder, &dev->mode_config.encoder_list, head) {
		struct nouveau_encoder *nv_encoder = nouveau_encoder(encoder);

		if (nv_encoder->dcb->i2c_index != i2c_index)
			continue;

		drm_mode_connector_attach_encoder(connector, encoder);
	}

	drm_sysfs_connector_add(connector);
	return 0;
}
