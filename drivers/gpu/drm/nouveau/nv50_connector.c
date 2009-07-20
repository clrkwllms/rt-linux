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
#include "nv50_display.h"

static struct nouveau_encoder *
nv50_connector_to_encoder(struct nouveau_connector *connector, bool digital)
{
	struct drm_device *dev = connector->base.dev;
	struct nouveau_encoder *encoder;
	struct drm_mode_object *obj;
	int i, id;

	for (i = 0; i < DRM_CONNECTOR_MAX_ENCODER; i++) {
		id = connector->base.encoder_ids[i];
		if (!id)
			break;

		obj = drm_mode_object_find(dev, id, DRM_MODE_OBJECT_ENCODER);
		if (!obj)
			continue;
		encoder = nouveau_encoder(obj_to_encoder(obj));

		if (digital) {
			switch (encoder->dcb->type) {
			case OUTPUT_TMDS:
			case OUTPUT_LVDS:
				return encoder;
			default:
				break;
			}
		} else {
			switch (encoder->dcb->type) {
			case OUTPUT_ANALOG:
				return encoder;
			default:
				break;
			}
		}
	}

	return NULL;
}

static void nv50_connector_destroy(struct drm_connector *drm_connector)
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
nv50_connector_set_digital(struct nouveau_connector *connector, bool digital)
{
	struct drm_device *dev = connector->base.dev;

	if (connector->base.connector_type == DRM_MODE_CONNECTOR_DVII) {
		struct drm_property *prop =
			dev->mode_config.dvi_i_subconnector_property;
		uint64_t val;

		if (digital)
			val = DRM_MODE_SUBCONNECTOR_DVID;
		else
			val = DRM_MODE_SUBCONNECTOR_DVIA;

		drm_connector_property_set_value(&connector->base, prop, val);
	}

	connector->digital = digital;
}

void nv50_connector_detect_all(struct drm_device *dev)
{
	struct drm_connector *drm_connector = NULL;

	list_for_each_entry(drm_connector, &dev->mode_config.connector_list, head) {
		drm_connector->funcs->detect(drm_connector);
	}
}

static enum drm_connector_status
nv50_connector_detect(struct drm_connector *drm_connector)
{
	struct drm_device *dev = drm_connector->dev;
	struct nouveau_connector *connector = nouveau_connector(drm_connector);
	struct nouveau_encoder *encoder = NULL;
	struct drm_encoder_helper_funcs *helper = NULL;

	if (drm_connector->connector_type == DRM_MODE_CONNECTOR_LVDS) {
		if (!connector->native_mode && !nouveau_i2c_detect(connector)) {
			NV_ERROR(dev, "No native mode for LVDS.\n");
			return connector_status_disconnected;
		}

		nv50_connector_set_digital(connector, true);
		return connector_status_connected;
	}

	encoder = nv50_connector_to_encoder(connector, false);
	if (encoder)
		helper = encoder->base.helper_private;

	if (helper && helper->detect(&encoder->base, &connector->base) ==
			connector_status_connected) {
		nv50_connector_set_digital(connector, false);
		return connector_status_connected;
	}

	if (nouveau_i2c_detect(connector)) {
		nv50_connector_set_digital(connector, true);
		return connector_status_connected;
	}

	return connector_status_disconnected;
}

static int nv50_connector_set_property(struct drm_connector *drm_connector,
				       struct drm_property *property,
				       uint64_t value)
{
	struct drm_device *dev = drm_connector->dev;
	struct nouveau_connector *connector = nouveau_connector(drm_connector);
	int rval;

	/* Scaling mode */
	if (property == dev->mode_config.scaling_mode_property) {
		struct nouveau_crtc *crtc = NULL;
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
		if (connector->base.connector_type == DRM_MODE_CONNECTOR_LVDS &&
		    value == DRM_MODE_SCALE_NON_GPU)
			return -EINVAL;

		/* Changing between GPU and panel scaling requires a full
		 * modeset
		 */
		if ((connector->scaling_mode == DRM_MODE_SCALE_NON_GPU) ||
		    (value == DRM_MODE_SCALE_NON_GPU))
			modeset = true;
		connector->scaling_mode = value;

		if (drm_connector->encoder && drm_connector->encoder->crtc)
			crtc = nouveau_crtc(drm_connector->encoder->crtc);

		if (!crtc)
			return 0;

		if (modeset) {
			rval = drm_crtc_helper_set_mode(&crtc->base,
							&crtc->base.mode,
							crtc->base.x,
							crtc->base.y, NULL);
			if (rval)
				return rval;
		} else {
			rval = crtc->set_scale(crtc, value, true);
			if (rval)
				return rval;
		}

		return 0;
	}

	/* Dithering */
	if (property == dev->mode_config.dithering_mode_property) {
		struct nouveau_crtc *crtc = NULL;

		if (value == DRM_MODE_DITHERING_ON)
			connector->use_dithering = true;
		else
			connector->use_dithering = false;

		if (drm_connector->encoder && drm_connector->encoder->crtc)
			crtc = nouveau_crtc(drm_connector->encoder->crtc);

		if (!crtc)
			return 0;

		/* update hw state */
		crtc->use_dithering = connector->use_dithering;
		rval = crtc->set_dither(crtc, true);
		if (rval)
			return rval;

		return 0;
	}

	return -EINVAL;
}

static struct drm_display_mode *
nv50_connector_native_mode(struct nouveau_connector *connector)
{
	struct drm_device *dev = connector->base.dev;
	struct drm_display_mode *mode;

	if (!connector->digital)
		return NULL;

	list_for_each_entry(mode, &connector->base.probed_modes, head) {
		if (mode->type & DRM_MODE_TYPE_PREFERRED)
			return drm_mode_duplicate(dev, mode);
	}

	return NULL;
}

static int nv50_connector_get_modes(struct drm_connector *drm_connector)
{
	struct drm_device *dev = drm_connector->dev;
	struct nouveau_connector *connector = nouveau_connector(drm_connector);
	struct edid *edid = NULL;
	int ret = 0;

	/* If we're not LVDS, destroy the previous native mode, the attached
	 * monitor could have changed.
	 */
	if (drm_connector->connector_type != DRM_MODE_CONNECTOR_LVDS &&
	    connector->native_mode) {
		drm_mode_destroy(dev, connector->native_mode);
		connector->native_mode = NULL;
	}

	if (connector->i2c_chan)
		edid = drm_get_edid(drm_connector, &connector->i2c_chan->adapter);
	drm_mode_connector_update_edid_property(drm_connector, edid);

	if (edid) {
		ret = drm_add_edid_modes(drm_connector, edid);
		kfree(edid);
	}

	/* Find the native mode if this is a digital panel, if we didn't
	 * find any modes through DDC previously add the native mode to
	 * the list of modes.
	 */
	if (!connector->native_mode)
		connector->native_mode = nv50_connector_native_mode(connector);
	if (ret == 0 && connector->native_mode) {
		struct drm_display_mode *mode;

		mode = drm_mode_duplicate(dev, connector->native_mode);
		drm_mode_probed_add(drm_connector, mode);
		ret = 1;
	}

	return ret;
}

static int nv50_connector_mode_valid(struct drm_connector *drm_connector,
				     struct drm_display_mode *mode)
{
	struct drm_device *dev = drm_connector->dev;
	struct nouveau_connector *connector = nouveau_connector(drm_connector);
	struct nouveau_encoder *encoder =
		nv50_connector_to_encoder(connector, connector->digital);
	unsigned min_clock, max_clock;

	/* This really should not happen, but it appears it might do
	 * somehow, debug!
	 */
	if (!encoder) {
		int i;

		NV_ERROR(dev, "no encoder for connector: %s %d\n",
			 drm_get_connector_name(drm_connector),
			 connector->digital);
		for (i = 0; i < DRM_CONNECTOR_MAX_ENCODER; i++) {
			struct drm_mode_object *obj;
			if (!drm_connector->encoder_ids[i])
				break;

			obj = drm_mode_object_find(dev, drm_connector->encoder_ids[i], DRM_MODE_OBJECT_ENCODER);
			if (!obj)
				continue;
			encoder = nouveau_encoder(obj_to_encoder(obj));
			NV_ERROR(dev, " %d: %d\n", i, encoder->dcb->type);
		}

		return MODE_BAD;
	}

	min_clock = 25000;

	switch (encoder->base.encoder_type) {
	case DRM_MODE_ENCODER_LVDS:
		if (!connector->native_mode) {
			NV_ERROR(dev, "AIIII no native mode\n");
			return MODE_PANEL;
		}

		if (mode->hdisplay > connector->native_mode->hdisplay ||
		    mode->vdisplay > connector->native_mode->vdisplay)
			return MODE_PANEL;

		max_clock = 400000;
		break;
	case DRM_MODE_ENCODER_TMDS:
		if (!encoder->dual_link)
			max_clock = 165000;
		else
			max_clock = 330000;
		break;
	default:
		max_clock = 400000;
		break;
	}

	if (mode->clock < min_clock)
		return MODE_CLOCK_LOW;

	if (mode->clock > max_clock)
		return MODE_CLOCK_HIGH;

	return MODE_OK;
}

static struct drm_encoder *
nv50_connector_best_encoder(struct drm_connector *drm_connector)
{
	struct nouveau_connector *connector = nouveau_connector(drm_connector);

	return &nv50_connector_to_encoder(connector, connector->digital)->base;
}

static const struct drm_connector_helper_funcs nv50_connector_helper_funcs = {
	.get_modes = nv50_connector_get_modes,
	.mode_valid = nv50_connector_mode_valid,
	.best_encoder = nv50_connector_best_encoder,
};

static const struct drm_connector_funcs nv50_connector_funcs = {
	.dpms = drm_helper_connector_dpms,
	.save = NULL,
	.restore = NULL,
	.detect = nv50_connector_detect,
	.destroy = nv50_connector_destroy,
	.fill_modes = drm_helper_probe_single_connector_modes,
	.set_property = nv50_connector_set_property
};

int nv50_connector_create(struct drm_device *dev, int i2c_index, int type)
{
	struct nouveau_connector *connector = NULL;
	struct drm_encoder *drm_encoder;
	struct drm_display_mode native;
	int ret;

	NV_DEBUG(dev, "\n");

	connector = kzalloc(sizeof(*connector), GFP_KERNEL);
	if (!connector)
		return -ENOMEM;

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

		if (nouveau_bios_fp_mode(dev, &native))
			connector->native_mode = drm_mode_duplicate(dev, &native);
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
		connector->scaling_mode = DRM_MODE_SCALE_FULLSCREEN;
		break;
	default:
		connector->scaling_mode = DRM_MODE_SCALE_NON_GPU;
		break;
	}

	if (type == DRM_MODE_CONNECTOR_LVDS)
		connector->use_dithering = true;
	else
		connector->use_dithering = false;

	/* It should be allowed sometimes, but let's be safe for the moment. */
	connector->base.interlace_allowed = false;
	connector->base.doublescan_allowed = false;

	drm_connector_init(dev, &connector->base, &nv50_connector_funcs, type);
	drm_connector_helper_add(&connector->base, &nv50_connector_helper_funcs);

	if (i2c_index < 0xf) {
		ret = nouveau_i2c_new(dev,
				      drm_get_connector_name(&connector->base),
				      i2c_index, &connector->i2c_chan);
		if (ret) {
			NV_ERROR(dev, "Error initialising I2C on %s: %d\n",
				 drm_get_connector_name(&connector->base), ret);
		}
	}

	/* Init DVI-I specific properties */
	if (type == DRM_MODE_CONNECTOR_DVII) {
		drm_mode_create_dvi_i_properties(dev);
		drm_connector_attach_property(&connector->base, dev->mode_config.dvi_i_subconnector_property, 0);
		drm_connector_attach_property(&connector->base, dev->mode_config.dvi_i_select_subconnector_property, 0);
	}

	/* If supported in the future, it will have to use the scalers
	 * internally and not expose them.
	 */
	if (type != DRM_MODE_CONNECTOR_SVIDEO) {
		drm_connector_attach_property(&connector->base, dev->mode_config.scaling_mode_property, connector->scaling_mode);
	}

	drm_connector_attach_property(&connector->base, dev->mode_config.dithering_mode_property, connector->use_dithering ? DRM_MODE_DITHERING_ON : DRM_MODE_DITHERING_OFF);

	/* attach encoders */
	list_for_each_entry(drm_encoder, &dev->mode_config.encoder_list, head) {
		struct nouveau_encoder *encoder = nouveau_encoder(drm_encoder);

		if (encoder->dcb->i2c_index != i2c_index)
			continue;

		drm_mode_connector_attach_encoder(&connector->base, drm_encoder);
	}

	drm_sysfs_connector_add(&connector->base);

	return 0;
}
