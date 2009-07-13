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

#include "drmP.h"
#include "drm.h"
#include "drm_crtc_helper.h"
#include "nouveau_drv.h"
#include "nouveau_hw.h"

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

MODULE_PARM_DESC(uscript, "Execute output scripts (>=GeForce 8)");
int nouveau_uscript = 1;
module_param_named(uscript, nouveau_uscript, int, 0400);

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
nouveau_pci_suspend(struct pci_dev *pdev,  pm_message_t state)
{
	struct drm_device *dev = pci_get_drvdata(pdev);
	struct drm_nouveau_private *dev_priv = dev->dev_private;

	if (dev_priv->card_type >= NV_50)
		return -ENODEV;

	if (state.event == PM_EVENT_PRETHAW)
		return 0;

	pci_save_state(pdev);
	if (state.event == PM_EVENT_SUSPEND) {
		pci_disable_device(pdev);
		pci_set_power_state(pdev, PCI_D3hot);
	}
	return 0;
}

static int
nouveau_pci_resume(struct pci_dev *pdev)
{
	struct drm_device *dev = pci_get_drvdata(pdev);
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct drm_encoder *encoder;
	struct drm_crtc *crtc;
	int ret;

	if (dev_priv->card_type >= NV_50)
		return -ENODEV;

	pci_set_power_state(pdev, PCI_D0);
	pci_restore_state(pdev);
	if (pci_enable_device(pdev))
		return -1;
	pci_set_master(dev->pdev);

	ret = nouveau_run_vbios_init(dev);
	if (ret)
		return ret;

	NVLockVgaCrtcs(dev, false);

	/* meh.. modeset apparently doesn't setup all the regs and depends
	 * on pre-existing state, for now load the state of the card *before*
	 * nouveau was loaded, and then do a modeset.
	 *
	 * best thing to do probably is to make save/restore routines not
	 * save/restore "pre-load" state, but more general so we can save
	 * on suspend too.
	 */
	list_for_each_entry(encoder, &dev->mode_config.encoder_list, head) {
		struct drm_encoder_helper_funcs *func = encoder->helper_private;

		func->restore(encoder);
	}

	list_for_each_entry(crtc, &dev->mode_config.crtc_list, head)
		crtc->funcs->restore(crtc);

	drm_helper_resume_force_mode(dev);
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
