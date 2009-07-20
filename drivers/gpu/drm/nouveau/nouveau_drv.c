/*
 * Copyright 2005 Stephane Marchesin.
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
 * VA LINUX SYSTEMS AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */

#include <linux/console.h>

#include "drmP.h"
#include "drm.h"
#include "drm_crtc_helper.h"
#include "nouveau_drv.h"
#include "nouveau_hw.h"
#include "nv50_display.h"

#include "drm_pciids.h"

MODULE_PARM_DESC(noagp, "Disable AGP");
int nouveau_noagp = 0;
module_param_named(noagp, nouveau_noagp, int, 0400);

MODULE_PARM_DESC(modeset, "Enable kernel modesetting");
int nouveau_modeset = -1; /* kms */
module_param_named(modeset, nouveau_modeset, int, 0400);

MODULE_PARM_DESC(duallink, "Allow dual-link TMDS (>=GeForce 8)");
int nouveau_duallink = 1;
module_param_named(duallink, nouveau_duallink, int, 0400);

MODULE_PARM_DESC(uscript_lvds, "LVDS output script table ID (>=GeForce 8)");
int nouveau_uscript_lvds = -1;
module_param_named(uscript_lvds, nouveau_uscript_lvds, int, 0400);

MODULE_PARM_DESC(uscript_tmds, "TMDS output script table ID (>=GeForce 8)");
int nouveau_uscript_tmds = -1;
module_param_named(uscript_tmds, nouveau_uscript_tmds, int, 0400);

int nouveau_fbpercrtc = 0;
#if 0
module_param_named(fbpercrtc, nouveau_fbpercrtc, int, 0400);
#endif

static struct pci_device_id pciidlist[] = {
	{
		PCI_DEVICE(PCI_VENDOR_ID_NVIDIA, PCI_ANY_ID),
		.class = PCI_BASE_CLASS_DISPLAY << 16,
		.class_mask  = 0xff << 16,
	},
	{
		PCI_DEVICE(PCI_VENDOR_ID_NVIDIA_SGS, PCI_ANY_ID),
		.class = PCI_BASE_CLASS_DISPLAY << 16,
		.class_mask  = 0xff << 16,
	},
	{}
};

MODULE_DEVICE_TABLE(pci, pciidlist);

static struct drm_driver driver;

static int __devinit
nouveau_pci_probe(struct pci_dev *pdev, const struct pci_device_id *ent)
{
	return drm_get_dev(pdev, ent, &driver);
}

static void
nouveau_pci_remove(struct pci_dev *pdev)
{
	struct drm_device *dev = pci_get_drvdata(pdev);

	drm_put_dev(dev);
}

static int
nouveau_pci_suspend(struct pci_dev *pdev, pm_message_t pm_state)
{
	struct drm_device *dev = pci_get_drvdata(pdev);
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nouveau_engine *engine = &dev_priv->engine;
	uint32_t fbdev_flags;
	int ret, i;

	if (!drm_core_check_feature(dev, DRIVER_MODESET))
		return -ENODEV;

	if (pm_state.event == PM_EVENT_PRETHAW)
		return 0;

	fbdev_flags = dev_priv->fbdev_info->flags;
	dev_priv->fbdev_info->flags |= FBINFO_HWACCEL_DISABLED;

	NV_INFO(dev, "Evicting buffers...\n");
	ttm_bo_evict_mm(&dev_priv->ttm.bdev, TTM_PL_VRAM);

	NV_INFO(dev, "Idling channels...\n");
	for (i = 0; i < engine->fifo.channels; i++) {
		struct nouveau_channel *chan = dev_priv->fifos[i];
		struct nouveau_fence *fence = NULL;

		if (!chan || (dev_priv->card_type >= NV_50 &&
			      chan == dev_priv->fifos[0]))
			continue;

		ret = nouveau_fence_new(chan, &fence, true);
		if (ret == 0) {
			ret = nouveau_fence_wait(fence, NULL, false, false);
			nouveau_fence_unref((void *)&fence);
		}

		if (ret) {
			NV_ERROR(dev, "Failed to idle channel %d for suspend\n",
				 chan->id);
		}
	}

	engine->graph.fifo_access(dev, false);
	nouveau_wait_for_idle(dev);

	nv_wr32(NV03_PFIFO_CACHES, 0x00000000);
	nv_wr32(NV04_PFIFO_CACHE1_DMA_PUSH, nv_rd32(
		NV04_PFIFO_CACHE1_DMA_PUSH) & ~1);
	nv_wr32(NV03_PFIFO_CACHE1_PUSH0, 0x00000000);
	nv_wr32(NV04_PFIFO_CACHE1_PULL0, 0x00000000);

	i = engine->fifo.channel_id(dev);
	NV_INFO(dev, "Last active channel was %d\n", i);
	if (i >= 0 && i < engine->fifo.channels && dev_priv->fifos[i]) {
		struct nouveau_channel *chan = dev_priv->fifos[i];

		NV_INFO(dev, "Saving state of channel %d...\n", chan->id);
		engine->fifo.save_context(chan);
		engine->graph.save_context(chan);
	}

	NV_INFO(dev, "Suspending GPU objects...\n");
	ret = nouveau_gpuobj_suspend(dev);
	if (ret) {
		NV_ERROR(dev, "... failed: %d\n", ret);
		return ret;
	}

	if (engine->instmem.suspend) {
		ret = engine->instmem.suspend(dev);
		if (ret) {
			NV_ERROR(dev, "... failed: %d\n", ret);
			return ret;
		}
	}

	NV_INFO(dev, "And we're gone!\n");
	pci_save_state(pdev);
	if (pm_state.event == PM_EVENT_SUSPEND) {
		pci_disable_device(pdev);
		pci_set_power_state(pdev, PCI_D3hot);
	}

	acquire_console_sem();
	fb_set_suspend(dev_priv->fbdev_info, 1);
	release_console_sem();
	dev_priv->fbdev_info->flags = fbdev_flags;
	return 0;
}

static int
nouveau_pci_resume(struct pci_dev *pdev)
{
	struct drm_device *dev = pci_get_drvdata(pdev);
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nouveau_engine *engine = &dev_priv->engine;
	struct drm_crtc *crtc;
	uint32_t fbdev_flags;
	int ret;

	if (!drm_core_check_feature(dev, DRIVER_MODESET))
		return -ENODEV;

	fbdev_flags = dev_priv->fbdev_info->flags;
	dev_priv->fbdev_info->flags |= FBINFO_HWACCEL_DISABLED;

	NV_INFO(dev, "We're back, enabling device...\n");
	pci_set_power_state(pdev, PCI_D0);
	pci_restore_state(pdev);
	if (pci_enable_device(pdev))
		return -1;
	pci_set_master(dev->pdev);

	NV_INFO(dev, "POSTing device...\n");
	ret = nouveau_run_vbios_init(dev);
	if (ret)
		return ret;

	if (dev_priv->gart_info.type == NOUVEAU_GART_AGP) {
		ret = nouveau_mem_init_agp(dev);
		if (ret) {
			NV_ERROR(dev, "error reinitialising AGP: %d\n", ret);
			return ret;
		}
	}

	NV_INFO(dev, "Reinitialising engines...\n");
	if (engine->instmem.resume)
		engine->instmem.resume(dev);
	engine->mc.init(dev);
	engine->timer.init(dev);
	engine->fb.init(dev);
	engine->graph.init(dev);
	engine->fifo.init(dev);

	NV_INFO(dev, "Restoring GPU objects...\n");
	nouveau_gpuobj_resume(dev);

	nouveau_irq_postinstall(dev);

	if (dev_priv->card_type < NV_50) {
		engine->fifo.load_context(dev_priv->channel);
		engine->graph.load_context(dev_priv->channel);
	}

	NV_INFO(dev, "Re-enabling acceleration..\n");
	nv_wr32(NV04_PFIFO_CACHE1_DMA_PUSH,
		 nv_rd32(NV04_PFIFO_CACHE1_DMA_PUSH) | 1);
	nv_wr32(NV03_PFIFO_CACHE1_PUSH0, 0x00000001);
	nv_wr32(NV04_PFIFO_CACHE1_PULL0, 0x00000001);
	nv_wr32(NV04_PFIFO_CACHE1_PULL1, 0x00000001);
	nv_wr32(NV03_PFIFO_CACHES, 1);

	engine->graph.fifo_access(dev, true);

	NV_INFO(dev, "Restoring mode...\n");
	if (dev_priv->card_type < NV_50)
		nv04_display_restore(dev);
	else
		nv50_display_init(dev);

	/* Force CLUT to get re-loaded during modeset */
	list_for_each_entry(crtc, &dev->mode_config.crtc_list, head) {
		struct nouveau_crtc *nv_crtc = nouveau_crtc(crtc);

		nv_crtc->lut.depth = 0;
	}

	acquire_console_sem();
	fb_set_suspend(dev_priv->fbdev_info, 0);
	release_console_sem();

	drm_helper_resume_force_mode(dev);
	dev_priv->fbdev_info->flags = fbdev_flags;
	return 0;
}

extern struct drm_ioctl_desc nouveau_ioctls[];
extern int nouveau_max_ioctl;

static struct drm_driver driver = {
	.driver_features =
		DRIVER_USE_AGP | DRIVER_PCI_DMA | DRIVER_SG |
		DRIVER_HAVE_IRQ | DRIVER_IRQ_SHARED | DRIVER_GEM,
	.load = nouveau_load,
	.firstopen = nouveau_firstopen,
	.lastclose = nouveau_lastclose,
	.unload = nouveau_unload,
	.preclose = nouveau_preclose,
	.irq_preinstall = nouveau_irq_preinstall,
	.irq_postinstall = nouveau_irq_postinstall,
	.irq_uninstall = nouveau_irq_uninstall,
	.irq_handler = nouveau_irq_handler,
	.reclaim_buffers = drm_core_reclaim_buffers,
	.get_map_ofs = drm_core_get_map_ofs,
	.get_reg_ofs = drm_core_get_reg_ofs,
	.ioctls = nouveau_ioctls,
	.fops = {
		.owner = THIS_MODULE,
		.open = drm_open,
		.release = drm_release,
		.ioctl = drm_ioctl,
		.mmap = nouveau_ttm_mmap,
		.poll = drm_poll,
		.fasync = drm_fasync,
#if defined(CONFIG_COMPAT)
		.compat_ioctl = nouveau_compat_ioctl,
#endif
	},
	.pci_driver = {
		.name = DRIVER_NAME,
		.id_table = pciidlist,
		.probe = nouveau_pci_probe,
		.remove = nouveau_pci_remove,
		.suspend = nouveau_pci_suspend,
		.resume = nouveau_pci_resume
	},

	.gem_init_object = nouveau_gem_object_new,
	.gem_free_object = nouveau_gem_object_del,

	.name = DRIVER_NAME,
	.desc = DRIVER_DESC,
#ifdef GIT_REVISION
	.date = GIT_REVISION,
#else
	.date = DRIVER_DATE,
#endif
	.major = DRIVER_MAJOR,
	.minor = DRIVER_MINOR,
	.patchlevel = DRIVER_PATCHLEVEL,
};

static int __init nouveau_init(void)
{
	driver.num_ioctls = nouveau_max_ioctl;

	if (nouveau_modeset == -1)
#if defined(CONFIG_DRM_NOUVEAU_KMS)
		nouveau_modeset = 1;
#else
		nouveau_modeset = 0;
#endif

	if (nouveau_modeset == 1)
		driver.driver_features |= DRIVER_MODESET;

	return drm_init(&driver);
}

static void __exit nouveau_exit(void)
{
	drm_exit(&driver);
}

module_init(nouveau_init);
module_exit(nouveau_exit);

MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_LICENSE("GPL and additional rights");
