/*
 * Copyright (C) 2008 Ben Skeggs.
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
#include "drm.h"

#include "nouveau_drv.h"
#include "nouveau_drm.h"
#include "nouveau_dma.h"

#define nouveau_gem_pushbuf_sync(chan) 0

int
nouveau_gem_object_new(struct drm_gem_object *gem)
{
	return 0;
}

void
nouveau_gem_object_del(struct drm_gem_object *gem)
{
	struct nouveau_bo *nvbo = gem->driver_private;
	struct ttm_buffer_object *bo = &nvbo->bo;

	if (!nvbo)
		return;
	nvbo->gem = NULL;

	if (unlikely(nvbo->cpu_filp))
		ttm_bo_synccpu_write_release(bo);

	if (unlikely(nvbo->pin_refcnt)) {
		nvbo->pin_refcnt = 1;
		nouveau_bo_unpin(nvbo);
	}

	ttm_bo_unref(&bo);
}

int
nouveau_gem_new(struct drm_device *dev, struct nouveau_channel *chan,
		int size, int align, uint32_t flags, uint32_t tile_mode,
		uint32_t tile_flags, bool no_vm, bool mappable,
		struct nouveau_bo **pnvbo)
{
	struct nouveau_bo *nvbo;
	int ret;

	ret = nouveau_bo_new(dev, chan, size, align, flags, tile_mode,
			     tile_flags, no_vm, mappable, pnvbo);
	if (ret)
		return ret;
	nvbo = *pnvbo;

	nvbo->gem = drm_gem_object_alloc(dev, nvbo->bo.mem.size);
	if (!nvbo->gem) {
		nouveau_bo_ref(NULL, pnvbo);
		return -ENOMEM;
	}

	nvbo->bo.persistant_swap_storage = nvbo->gem->filp;
	nvbo->gem->driver_private = nvbo;
	return 0;
}

static int
nouveau_gem_info(struct drm_gem_object *gem, struct drm_nouveau_gem_info *rep)
{
	struct nouveau_bo *nvbo = nouveau_gem_object(gem);

	if (nvbo->bo.mem.mem_type == TTM_PL_FLAG_TT)
		rep->domain = NOUVEAU_GEM_DOMAIN_GART;
	else
		rep->domain = NOUVEAU_GEM_DOMAIN_VRAM;

	rep->size = nvbo->bo.mem.num_pages << PAGE_SHIFT;
	rep->offset = nvbo->bo.offset;
	rep->map_handle = nvbo->mappable ? nvbo->bo.addr_space_offset : 0;
	rep->tile_mode = nvbo->tile_mode;
	rep->tile_flags = nvbo->tile_flags;
	return 0;
}

static bool
nouveau_gem_tile_mode_valid(struct drm_device *dev, uint32_t tile_flags) {
	switch (tile_flags) {
	case 0x0000:
	case 0x1800:
	case 0x2800:
	case 0x4800:
	case 0x7000:
	case 0x7400:
	case 0x7a00:
	case 0xe000:
		break;
	default:
		NV_ERROR(dev, "bad page flags: 0x%08x\n", tile_flags);
		return false;
	}

	return true;
}

int
nouveau_gem_ioctl_new(struct drm_device *dev, void *data,
		      struct drm_file *file_priv)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct drm_nouveau_gem_new *req = data;
	struct nouveau_bo *nvbo = NULL;
	struct nouveau_channel *chan = NULL;
	uint32_t flags = 0;
	int ret = 0;

	NOUVEAU_CHECK_INITIALISED_WITH_RETURN;

	if (unlikely(dev_priv->ttm.bdev.dev_mapping == NULL))
		dev_priv->ttm.bdev.dev_mapping = dev_priv->dev->dev_mapping;

	if (req->channel_hint) {
		NOUVEAU_GET_USER_CHANNEL_WITH_RETURN(req->channel_hint,
						     file_priv, chan);
	}

	if (req->info.domain & NOUVEAU_GEM_DOMAIN_VRAM)
		flags |= TTM_PL_FLAG_VRAM;
	if (req->info.domain & NOUVEAU_GEM_DOMAIN_GART)
		flags |= TTM_PL_FLAG_TT;
	if (!flags || req->info.domain & NOUVEAU_GEM_DOMAIN_CPU)
		flags |= TTM_PL_FLAG_SYSTEM;

	if (req->info.tile_mode > 4) {
		NV_ERROR(dev, "bad tile mode: %d\n", req->info.tile_mode);
		return -EINVAL;
	}

	if (!nouveau_gem_tile_mode_valid(dev, req->info.tile_flags))
		return -EINVAL;

	ret = nouveau_gem_new(dev, chan, req->info.size, req->align, flags,
			      req->info.tile_mode, req->info.tile_flags, false,
			      (req->info.domain & NOUVEAU_GEM_DOMAIN_MAPPABLE),
			      &nvbo);
	if (ret)
		return ret;

	ret = nouveau_gem_info(nvbo->gem, &req->info);
	if (ret)
		goto out;

	ret = drm_gem_handle_create(file_priv, nvbo->gem, &req->info.handle);
out:
	mutex_lock(&dev->struct_mutex);
	drm_gem_object_handle_unreference(nvbo->gem);
	mutex_unlock(&dev->struct_mutex);

	if (ret)
		drm_gem_object_unreference(nvbo->gem);
	return ret;
}

static int
nouveau_gem_set_domain(struct drm_gem_object *gem, uint32_t read_domains,
		       uint32_t write_domains, uint32_t valid_domains)
{
	struct nouveau_bo *nvbo = gem->driver_private;
	struct ttm_buffer_object *bo = &nvbo->bo;
	uint64_t flags;

	if (!valid_domains || (!read_domains && !write_domains))
		return -EINVAL;

	if (write_domains) {
		if ((valid_domains & NOUVEAU_GEM_DOMAIN_VRAM) &&
		    (write_domains & NOUVEAU_GEM_DOMAIN_VRAM))
			flags = TTM_PL_FLAG_VRAM;
		else
		if ((valid_domains & NOUVEAU_GEM_DOMAIN_GART) &&
		    (write_domains & NOUVEAU_GEM_DOMAIN_GART))
			flags = TTM_PL_FLAG_TT;
		else
			return -EINVAL;
	} else {
		if ((valid_domains & NOUVEAU_GEM_DOMAIN_VRAM) &&
		    (read_domains & NOUVEAU_GEM_DOMAIN_VRAM) &&
		    (bo->mem.mem_type == TTM_PL_VRAM ||
		     bo->mem.mem_type == TTM_PL_PRIV0))
			flags = TTM_PL_FLAG_VRAM;
		else
		if ((valid_domains & NOUVEAU_GEM_DOMAIN_GART) &&
		    (read_domains & NOUVEAU_GEM_DOMAIN_GART) &&
		    bo->mem.mem_type == TTM_PL_TT)
			flags = TTM_PL_FLAG_TT;
		else
		if ((valid_domains & NOUVEAU_GEM_DOMAIN_VRAM) &&
		    (read_domains & NOUVEAU_GEM_DOMAIN_VRAM))
			flags = TTM_PL_FLAG_VRAM;
		else
			flags = TTM_PL_FLAG_TT;
	}

	if ((flags & TTM_PL_FLAG_VRAM) && !nvbo->mappable)
		flags |= TTM_PL_FLAG_PRIV0;

	bo->proposed_placement &= ~TTM_PL_MASK_MEM;
	bo->proposed_placement |= flags;
	return 0;
}

static void
nouveau_gem_pushbuf_backoff(struct list_head *list)
{
	struct list_head *entry, *tmp;
	struct nouveau_bo *nvbo;

	list_for_each_safe(entry, tmp, list) {
		nvbo = list_entry(entry, struct nouveau_bo, entry);

		drm_gem_object_unreference(nvbo->gem);
		ttm_bo_unreserve(&nvbo->bo);
		list_del(&nvbo->entry);
	}
}

static void
nouveau_gem_pushbuf_fence(struct list_head *list, struct nouveau_fence *fence)
{
	struct list_head *entry, *tmp;
	struct nouveau_fence *prev_fence;
	struct nouveau_bo *nvbo;

	list_for_each_safe(entry, tmp, list) {
		nvbo = list_entry(entry, struct nouveau_bo, entry);

		spin_lock(&nvbo->bo.lock);
		prev_fence = nvbo->bo.sync_obj;
		nvbo->bo.sync_obj = nouveau_fence_ref(fence);
		spin_unlock(&nvbo->bo.lock);

		drm_gem_object_unreference(nvbo->gem);
		ttm_bo_unreserve(&nvbo->bo);
		list_del(&nvbo->entry);

		nouveau_fence_unref((void *)&prev_fence);
	}
}

static int
nouveau_gem_pushbuf_validate(struct nouveau_channel *chan,
			     struct drm_file *file_priv,
			     struct drm_nouveau_gem_pushbuf_bo *pbbo,
			     uint64_t user_buffers, int nr_buffers,
			     struct list_head *list, int *apply_relocs)
{
	struct drm_device *dev = chan->dev;
	struct drm_nouveau_gem_pushbuf_bo *b;
	struct drm_nouveau_gem_pushbuf_bo __user *user_pbbos =
				(void __force __user *)(uintptr_t)user_buffers;
	struct nouveau_fence *prev_fence;
	struct nouveau_bo *nvbo;
	struct list_head *entry, *tmp;
	int ret = -EINVAL;
	int i;

	if (nr_buffers == 0)
		return 0;

	if (apply_relocs)
		*apply_relocs = 0;

retry:
	for (i = 0, b = pbbo; i < nr_buffers; i++, b++) {
		struct drm_gem_object *gem;

		gem = drm_gem_object_lookup(dev, file_priv, b->handle);
		if (!gem) {
			NV_ERROR(dev, "Unknown handle 0x%08x\n", b->handle);
			ret = -EINVAL;
			goto out_unref;
		}
		nvbo = gem->driver_private;

		ret = ttm_bo_reserve(&nvbo->bo, false, false, true,
				     chan->fence.sequence);
		if (ret) {
			nouveau_gem_pushbuf_backoff(list);
			if (ret == -EAGAIN) {
				ret = ttm_bo_wait_unreserved(&nvbo->bo, false);
				if (unlikely(ret))
					goto out_unref;
				goto retry;
			} else
				goto out_unref;
		}

		if (unlikely(atomic_read(&nvbo->bo.cpu_writers) > 0)) {
			nouveau_gem_pushbuf_backoff(list);
			ret = ttm_bo_wait_cpu(&nvbo->bo, false);
			if (ret)
				goto out_unref;
			goto retry;
		}

		list_add_tail(&nvbo->entry, list);
	}

	b = pbbo;
	list_for_each_safe(entry, tmp, list) {
		nvbo = list_entry(entry, struct nouveau_bo, entry);

		prev_fence = nvbo->bo.sync_obj;
		if (prev_fence && nouveau_fence_channel(prev_fence) != chan) {
			spin_lock(&nvbo->bo.lock);
			ret = ttm_bo_wait(&nvbo->bo, false, false, false);
			spin_unlock(&nvbo->bo.lock);
			if (ret)
				goto out_unref;
		}

		ret = nouveau_gem_set_domain(nvbo->gem, b->read_domains,
					     b->write_domains,
					     b->valid_domains);
		if (ret)
			goto out_unref;

		nvbo->channel = chan;
		ret = ttm_buffer_object_validate(&nvbo->bo,
						 nvbo->bo.proposed_placement,
						 false, false);
		nvbo->channel = NULL;
		if (ret)
			goto out_unref;

		if (nvbo->bo.offset == b->presumed_offset &&
		    (((nvbo->bo.mem.mem_type == TTM_PL_VRAM ||
		       nvbo->bo.mem.mem_type == TTM_PL_PRIV0) &&
		      b->presumed_domain & NOUVEAU_GEM_DOMAIN_VRAM) ||
		     (nvbo->bo.mem.mem_type == TTM_PL_TT &&
		      b->presumed_domain & NOUVEAU_GEM_DOMAIN_GART))) {
			b++;
			user_pbbos++;
			continue;
		}

		if (nvbo->bo.mem.mem_type == TTM_PL_TT)
			b->presumed_domain = NOUVEAU_GEM_DOMAIN_GART;
		else
			b->presumed_domain = NOUVEAU_GEM_DOMAIN_VRAM;
		b->presumed_offset = nvbo->bo.offset;
		b->presumed_ok = 0;
		if (apply_relocs)
			(*apply_relocs)++;

		if (DRM_COPY_TO_USER(user_pbbos, b, sizeof(*b))) {
			ret = -EFAULT;
			goto out_unref;
		}

		b++;
		user_pbbos++;
	}

out_unref:
	if (unlikely(ret))
		nouveau_gem_pushbuf_backoff(list);

	return ret;
}

static int
nouveau_gem_pushbuf_reloc_apply(struct nouveau_channel *chan,
				struct drm_nouveau_gem_pushbuf_reloc *reloc,
				struct drm_nouveau_gem_pushbuf_bo *bo,
				uint32_t *pushbuf, int nr_relocs,
				int nr_buffers, int nr_dwords)
{
	struct drm_device *dev = chan->dev;
	int i;

	for (i = 0; i < nr_relocs; i++) {
		struct drm_nouveau_gem_pushbuf_reloc *r = &reloc[i];
		struct drm_nouveau_gem_pushbuf_bo *b;
		uint32_t data;

		if (r->bo_index >= nr_buffers || r->reloc_index >= nr_dwords) {
			NV_ERROR(dev, "Bad relocation %d\n", i);
			NV_ERROR(dev, "  bo: %d max %d\n", r->bo_index, nr_buffers);
			NV_ERROR(dev, "  id: %d max %d\n", r->reloc_index, nr_dwords);
			return -EINVAL;
		}

		b = &bo[r->bo_index];
		if (b->presumed_ok)
			continue;

		if (r->flags & NOUVEAU_GEM_RELOC_LOW)
			data = b->presumed_offset + r->data;
		else
		if (r->flags & NOUVEAU_GEM_RELOC_HIGH)
			data = (b->presumed_offset + r->data) >> 32;
		else
			data = r->data;

		if (r->flags & NOUVEAU_GEM_RELOC_OR) {
			if (b->presumed_domain == NOUVEAU_GEM_DOMAIN_GART)
				data |= r->tor;
			else
				data |= r->vor;
		}

		pushbuf[r->reloc_index] = data;
	}

	return 0;
}

static inline void *
u_memcpya(uint64_t user, unsigned nmemb, unsigned size)
{
	void *mem;
	void __user *userptr = (void __force __user *)(uintptr_t)user;

	mem = kmalloc(nmemb * size, GFP_KERNEL);
	if (!mem)
		return ERR_PTR(-ENOMEM);

	if (DRM_COPY_FROM_USER(mem, userptr, nmemb * size)) {
		kfree(mem);
		return ERR_PTR(-EFAULT);
	}

	return mem;
}

int
nouveau_gem_ioctl_pushbuf(struct drm_device *dev, void *data,
			  struct drm_file *file_priv)
{
	struct drm_nouveau_gem_pushbuf *req = data;
	struct drm_nouveau_gem_pushbuf_bo *bo = NULL;
	struct drm_nouveau_gem_pushbuf_reloc *reloc = NULL;
	struct nouveau_fence *fence = NULL;
	struct nouveau_channel *chan;
	struct list_head list;
	uint32_t *pushbuf = NULL;
	int ret = 0, i;

	NOUVEAU_CHECK_INITIALISED_WITH_RETURN;
	NOUVEAU_GET_USER_CHANNEL_WITH_RETURN(req->channel, file_priv, chan);

	if (req->nr_dwords >= chan->dma.max ||
	    req->nr_buffers > NOUVEAU_GEM_MAX_BUFFERS ||
	    req->nr_relocs > NOUVEAU_GEM_MAX_RELOCS) {
		NV_ERROR(dev, "Pushbuf config exceeds limits:\n");
		NV_ERROR(dev, "  dwords : %d max %d\n", req->nr_dwords,
			 chan->dma.max - 1);
		NV_ERROR(dev, "  buffers: %d max %d\n", req->nr_buffers,
			 NOUVEAU_GEM_MAX_BUFFERS);
		NV_ERROR(dev, "  relocs : %d max %d\n", req->nr_relocs,
			 NOUVEAU_GEM_MAX_RELOCS);
		return -EINVAL;
	}

	pushbuf = u_memcpya(req->dwords, req->nr_dwords, sizeof(uint32_t));
	if (IS_ERR(pushbuf))
		return PTR_ERR(pushbuf);

	bo = u_memcpya(req->buffers, req->nr_buffers, sizeof(*bo));
	if (IS_ERR(bo)) {
		kfree(pushbuf);
		return PTR_ERR(bo);
	}

	reloc = u_memcpya(req->relocs, req->nr_relocs, sizeof(*reloc));
	if (IS_ERR(reloc)) {
		kfree(bo);
		kfree(pushbuf);
		return PTR_ERR(reloc);
	}

	mutex_lock(&dev->struct_mutex);

	INIT_LIST_HEAD(&list);

	/* Validate buffer list */
	ret = nouveau_fence_new(chan, &fence, false);
	if (ret)
		goto out;

	ret = nouveau_gem_pushbuf_validate(chan, file_priv, bo, req->buffers,
					   req->nr_buffers, &list, NULL);
	if (ret)
		goto out;

	/* Apply any relocations that are required */
	ret = nouveau_gem_pushbuf_reloc_apply(chan, reloc, bo, pushbuf,
					      req->nr_relocs, req->nr_buffers,
					      req->nr_dwords);
	if (ret)
		goto out;

	/* Emit push buffer to the hw
	 *XXX: OMG ALSO YUCK!!!
	 */
	ret = RING_SPACE(chan, req->nr_dwords);
	if (ret)
		goto out;

	for (i = 0; i < req->nr_dwords; i++)
		OUT_RING (chan, pushbuf[i]);

	ret = nouveau_fence_emit(fence);
	if (ret) {
		NV_ERROR(dev, "error fencing pushbuf: %d\n", ret);
		WIND_RING(chan);
		goto out;
	}

	nouveau_gem_pushbuf_fence(&list, fence);

	if (nouveau_gem_pushbuf_sync(chan)) {
		ret = nouveau_fence_wait(fence, NULL, false, false);
		if (ret) {
			for (i = 0; i < req->nr_dwords; i++)
				NV_ERROR(dev, "0x%08x\n", pushbuf[i]);
			NV_ERROR(dev, "^^ above push buffer is fail :(\n");
		}
	}

	FIRE_RING(chan);
out:
	if (unlikely(ret))
		nouveau_gem_pushbuf_backoff(&list);
	nouveau_fence_unref((void *)&fence);

	mutex_unlock(&dev->struct_mutex);

	kfree(pushbuf);
	kfree(bo);
	kfree(reloc);
	return ret;
}

int
nouveau_gem_ioctl_pushbuf_call(struct drm_device *dev, void *data,
			       struct drm_file *file_priv)
{
	return -ENODEV;
}

static inline uint32_t
domain_to_ttm(struct nouveau_bo *nvbo, uint32_t domain)
{
	uint32_t flags = 0;

	if (domain & NOUVEAU_GEM_DOMAIN_VRAM) {
		flags |= TTM_PL_FLAG_VRAM;
		if (!nvbo->mappable)
			flags |= TTM_PL_FLAG_PRIV0;
	}

	if (domain & NOUVEAU_GEM_DOMAIN_GART)
		flags |= TTM_PL_FLAG_TT;

	return flags;
}

int
nouveau_gem_ioctl_pin(struct drm_device *dev, void *data,
		      struct drm_file *file_priv)
{
	struct drm_nouveau_gem_pin *req = data;
	struct drm_gem_object *gem;
	struct nouveau_bo *nvbo;
	int ret = 0;

	NOUVEAU_CHECK_INITIALISED_WITH_RETURN;

	if (drm_core_check_feature(dev, DRIVER_MODESET)) {
		NV_ERROR(dev, "pin only allowed without kernel modesetting\n");
		return -EINVAL;
	}

	if (!DRM_SUSER(DRM_CURPROC))
		return -EPERM;

	gem = drm_gem_object_lookup(dev, file_priv, req->handle);
	if (!gem)
		return -EINVAL;
	nvbo = nouveau_gem_object(gem);

	ret = nouveau_bo_pin(nvbo, domain_to_ttm(nvbo, req->domain));
	if (ret)
		goto out;

	req->offset = nvbo->bo.offset;
	if (nvbo->bo.mem.mem_type == TTM_PL_TT)
		req->domain = NOUVEAU_GEM_DOMAIN_GART;
	else
		req->domain = NOUVEAU_GEM_DOMAIN_VRAM;

out:
	mutex_lock(&dev->struct_mutex);
	drm_gem_object_unreference(gem);
	mutex_unlock(&dev->struct_mutex);

	return ret;
}

int
nouveau_gem_ioctl_unpin(struct drm_device *dev, void *data,
			struct drm_file *file_priv)
{
	struct drm_nouveau_gem_pin *req = data;
	struct drm_gem_object *gem;
	int ret;

	NOUVEAU_CHECK_INITIALISED_WITH_RETURN;

	if (drm_core_check_feature(dev, DRIVER_MODESET))
		return -EINVAL;

	gem = drm_gem_object_lookup(dev, file_priv, req->handle);
	if (!gem)
		return -EINVAL;

	ret = nouveau_bo_unpin(nouveau_gem_object(gem));

	mutex_lock(&dev->struct_mutex);
	drm_gem_object_unreference(gem);
	mutex_unlock(&dev->struct_mutex);

	return ret;
}

int
nouveau_gem_ioctl_cpu_prep(struct drm_device *dev, void *data,
			   struct drm_file *file_priv)
{
	struct drm_nouveau_gem_cpu_prep *req = data;
	struct drm_gem_object *gem;
	struct nouveau_bo *nvbo;
	bool no_wait = !!(req->flags & NOUVEAU_GEM_CPU_PREP_NOWAIT);
	int ret = -EINVAL;

	NOUVEAU_CHECK_INITIALISED_WITH_RETURN;

	gem = drm_gem_object_lookup(dev, file_priv, req->handle);
	if (!gem)
		return ret;
	nvbo = nouveau_gem_object(gem);

	if (nvbo->cpu_filp) {
		if (nvbo->cpu_filp == file_priv)
			goto out;

		ret = ttm_bo_wait_cpu(&nvbo->bo, no_wait);
		if (ret == -ERESTART)
			ret = -EAGAIN;
		if (ret)
			goto out;
	}

	if (req->flags & NOUVEAU_GEM_CPU_PREP_NOBLOCK) {
		ret = ttm_bo_wait(&nvbo->bo, false, false, no_wait);
	} else {
		ret = ttm_bo_synccpu_write_grab(&nvbo->bo, no_wait);
		if (ret == -ERESTART)
			ret = -EAGAIN;
		else
		if (ret == 0)
			nvbo->cpu_filp = file_priv;
	}

out:
	mutex_lock(&dev->struct_mutex);
	drm_gem_object_unreference(gem);
	mutex_unlock(&dev->struct_mutex);
	return ret;
}

int
nouveau_gem_ioctl_cpu_fini(struct drm_device *dev, void *data,
			   struct drm_file *file_priv)
{
	struct drm_nouveau_gem_cpu_prep *req = data;
	struct drm_gem_object *gem;
	struct nouveau_bo *nvbo;
	int ret = -EINVAL;

	NOUVEAU_CHECK_INITIALISED_WITH_RETURN;

	gem = drm_gem_object_lookup(dev, file_priv, req->handle);
	if (!gem)
		return ret;
	nvbo = nouveau_gem_object(gem);

	if (nvbo->cpu_filp != file_priv)
		goto out;
	nvbo->cpu_filp = NULL;

	ttm_bo_synccpu_write_release(&nvbo->bo);
	ret = 0;

out:
	mutex_lock(&dev->struct_mutex);
	drm_gem_object_unreference(gem);
	mutex_unlock(&dev->struct_mutex);
	return ret;
}

int
nouveau_gem_ioctl_info(struct drm_device *dev, void *data,
		       struct drm_file *file_priv)
{
	struct drm_nouveau_gem_info *req = data;
	struct drm_gem_object *gem;
	int ret;

	NOUVEAU_CHECK_INITIALISED_WITH_RETURN;

	gem = drm_gem_object_lookup(dev, file_priv, req->handle);
	if (!gem)
		return -EINVAL;

	ret = nouveau_gem_info(gem, req);
	mutex_lock(&dev->struct_mutex);
	drm_gem_object_unreference(gem);
	mutex_unlock(&dev->struct_mutex);
	return ret;
}

