/*
 * Copyright (C) 2009 Red Hat <bskeggs@redhat.com>
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

/*
 * Authors:
 *  Ben Skeggs <bskeggs@redhat.com>
 */

#include "drmP.h"
#include "nouveau_drv.h"

static int
nouveau_debugfs_chipset_info(struct seq_file *m, void *data)
{
	struct drm_info_node *node = (struct drm_info_node *) m->private;
	struct drm_minor *minor = node->minor;
	struct drm_device *dev = minor->dev;
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	uint32_t ppci_0;

	ppci_0 = nv_rd32(dev, dev_priv->chipset >= 0x40 ? 0x88000 : 0x1800);

	seq_printf(m, "PMC_BOOT_0: 0x%08x\n", nv_rd32(dev, NV03_PMC_BOOT_0));
	seq_printf(m, "PCI ID    : 0x%04x:0x%04x\n",
		   ppci_0 & 0xffff, ppci_0 >> 16);
	return 0;
}

static int
nouveau_debugfs_memory_info(struct seq_file *m, void *data)
{
	struct drm_info_node *node = (struct drm_info_node *) m->private;
	struct drm_minor *minor = node->minor;
	struct drm_device *dev = minor->dev;

	seq_printf(m, "VRAM total: %dMiB\n",
		   (int)(nouveau_mem_fb_amount(dev) >> 20));
	return 0;
}

struct drm_info_list nouveau_debugfs_list[] = {
	{ "chipset", nouveau_debugfs_chipset_info, 0 },
	{ "memory", nouveau_debugfs_memory_info, 0 },
};
#define NOUVEAU_DEBUGFS_ENTRIES ARRAY_SIZE(nouveau_debugfs_list)

int
nouveau_debugfs_init(struct drm_minor *minor)
{
	drm_debugfs_create_files(nouveau_debugfs_list, NOUVEAU_DEBUGFS_ENTRIES,
				 minor->debugfs_root, minor);
	return 0;
}

void
nouveau_debugfs_takedown(struct drm_minor *minor)
{
	drm_debugfs_remove_files(nouveau_debugfs_list, NOUVEAU_DEBUGFS_ENTRIES,
				 minor);
}

