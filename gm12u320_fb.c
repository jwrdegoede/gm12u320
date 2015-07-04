/*
 * Copyright (C) 2012-2015 Red Hat Inc.
 *
 * Based in parts on the udl code. Based in parts on the gm12u320 fb driver:
 * Copyright (C) 2013 Viacheslav Nurmekhamitov <slavrn@yandex.ru>
 * Copyright (C) 2009 Roberto De Ioris <roberto@unbit.it>
 * Copyright (C) 2009 Jaya Kumar <jayakumar.lkml@gmail.com>
 * Copyright (C) 2009 Bernie Thompson <bernie@plugable.com>
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License v2. See the file COPYING in the main directory of this archive for
 * more details.
 */
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/fb.h>
#include <linux/dma-buf.h>

#include <drm/drmP.h>
#include <drm/drm_crtc.h>
#include <drm/drm_crtc_helper.h>
#include "gm12u320_drv.h"

#include <drm/drm_fb_helper.h>

struct gm12u320_fbdev {
	struct drm_fb_helper helper;
	struct gm12u320_framebuffer fb;
	struct list_head fbdev_list;
};

static int gm12u320_fb_open(struct fb_info *info, int user)
{
	struct gm12u320_fbdev *fbdev = info->par;
	struct drm_device *ddev = fbdev->fb.base.dev;

	/* If the USB device is gone, we don't accept new opens */
	if (drm_device_is_unplugged(ddev))
		return -ENODEV;

	return 0;
}

static struct fb_ops gm12u320_fb_ops = {
	.owner = THIS_MODULE,
	.fb_check_var = drm_fb_helper_check_var,
	.fb_set_par = drm_fb_helper_set_par,
	.fb_fillrect = sys_fillrect,
	.fb_copyarea = sys_copyarea,
	.fb_imageblit = sys_imageblit,
	.fb_pan_display = drm_fb_helper_pan_display,
	.fb_blank = drm_fb_helper_blank,
	.fb_setcmap = drm_fb_helper_setcmap,
	.fb_debug_enter = drm_fb_helper_debug_enter,
	.fb_debug_leave = drm_fb_helper_debug_leave,
	.fb_open = gm12u320_fb_open,
};

static void gm12u320_fb_defio_cb(struct fb_info *info, struct list_head *pl)
{
	struct gm12u320_fbdev *fbdev = info->par;

	gm12u320_update_frame(&fbdev->fb);
}

static struct fb_deferred_io gm12u320_fb_defio = {
	.delay = HZ / 50,
	.deferred_io = gm12u320_fb_defio_cb,
};

static int gm12u320_user_framebuffer_dirty(struct drm_framebuffer *drm_fb,
				      struct drm_file *file,
				      unsigned flags, unsigned color,
				      struct drm_clip_rect *clips,
				      unsigned num_clips)
{
	struct gm12u320_framebuffer *fb = to_gm12u320_fb(drm_fb);
	int ret = 0;

	drm_modeset_lock_all(drm_fb->dev);

	if (fb->obj->base.import_attach) {
		ret = dma_buf_begin_cpu_access(
			fb->obj->base.import_attach->dmabuf, 0,
			fb->obj->base.size, DMA_FROM_DEVICE);
		if (ret)
			goto unlock;
	}

	gm12u320_update_frame(fb);

	if (fb->obj->base.import_attach) {
		dma_buf_end_cpu_access(fb->obj->base.import_attach->dmabuf, 0,
				       fb->obj->base.size, DMA_FROM_DEVICE);
	}

 unlock:
	drm_modeset_unlock_all(drm_fb->dev);

	return ret;
}

static void gm12u320_user_framebuffer_destroy(struct drm_framebuffer *drm_fb)
{
	struct gm12u320_framebuffer *fb = to_gm12u320_fb(drm_fb);

	if (fb->obj)
		drm_gem_object_unreference_unlocked(&fb->obj->base);

	drm_framebuffer_cleanup(drm_fb);
	kfree(fb);
}

static const struct drm_framebuffer_funcs gm12u320fb_funcs = {
	.destroy = gm12u320_user_framebuffer_destroy,
	.dirty = gm12u320_user_framebuffer_dirty,
};


static int
gm12u320_framebuffer_init(struct drm_device *dev,
		     struct gm12u320_framebuffer *fb,
		     struct drm_mode_fb_cmd2 *mode_cmd,
		     struct gm12u320_gem_object *obj)
{
	int ret;

	fb->obj = obj;
	drm_helper_mode_fill_fb_struct(&fb->base, mode_cmd);
	ret = drm_framebuffer_init(dev, &fb->base, &gm12u320fb_funcs);
	return ret;
}


static int gm12u320fb_create(struct drm_fb_helper *helper,
			struct drm_fb_helper_surface_size *sizes)
{
	struct gm12u320_fbdev *fbdev =
		container_of(helper, struct gm12u320_fbdev, helper);
	struct drm_device *dev = fbdev->helper.dev;
	struct fb_info *info;
	struct device *device = dev->dev;
	struct drm_framebuffer *drm_fb;
	struct drm_mode_fb_cmd2 mode_cmd;
	struct gm12u320_gem_object *obj;
	uint32_t size;
	int ret = 0;

	if (sizes->surface_bpp == 24)
		sizes->surface_bpp = 32;

	mode_cmd.width = sizes->surface_width;
	mode_cmd.height = sizes->surface_height;
	mode_cmd.pitches[0] = mode_cmd.width * ((sizes->surface_bpp + 7) / 8);

	mode_cmd.pixel_format = drm_mode_legacy_fb_format(sizes->surface_bpp,
							  sizes->surface_depth);

	size = mode_cmd.pitches[0] * mode_cmd.height;
	size = ALIGN(size, PAGE_SIZE);

	obj = gm12u320_gem_alloc_object(dev, size);
	if (!obj)
		goto out;

	ret = gm12u320_gem_vmap(obj);
	if (ret) {
		DRM_ERROR("failed to vmap fb\n");
		goto out_gfree;
	}

	info = framebuffer_alloc(0, device);
	if (!info) {
		ret = -ENOMEM;
		goto out_gfree;
	}
	info->par = fbdev;

	ret = gm12u320_framebuffer_init(dev, &fbdev->fb, &mode_cmd, obj);
	if (ret)
		goto out_gfree;

	drm_fb = &fbdev->fb.base;

	fbdev->helper.fb = drm_fb;
	fbdev->helper.fbdev = info;

	strcpy(info->fix.id, "gm12u320drmfb");

	info->screen_base = fbdev->fb.obj->vmapping;
	info->fix.smem_len = size;
	info->fix.smem_start = (unsigned long)fbdev->fb.obj->vmapping;

	info->flags = FBINFO_DEFAULT | FBINFO_CAN_FORCE_OUTPUT;
	info->fbops = &gm12u320_fb_ops;
	info->fbdefio = &gm12u320_fb_defio;
	drm_fb_helper_fill_fix(info, drm_fb->pitches[0], drm_fb->depth);
	drm_fb_helper_fill_var(info, &fbdev->helper, sizes->fb_width, sizes->fb_height);
	fb_deferred_io_init(info);

	ret = fb_alloc_cmap(&info->cmap, 256, 0);
	if (ret) {
		ret = -ENOMEM;
		goto out_gfree;
	}

	DRM_DEBUG_KMS("allocated %dx%d vmal %p\n",
		      drm_fb->width, drm_fb->height,
		      fbdev->fb.obj->vmapping);

	return ret;
out_gfree:
	drm_gem_object_unreference(&fbdev->fb.obj->base);
out:
	return ret;
}

static const struct drm_fb_helper_funcs gm12u320_fb_helper_funcs = {
	.fb_probe = gm12u320fb_create,
};

static void gm12u320_fbdev_destroy(struct drm_device *dev,
			      struct gm12u320_fbdev *fbdev)
{
	struct fb_info *info;
	if (fbdev->helper.fbdev) {
		info = fbdev->helper.fbdev;
		fb_deferred_io_cleanup(info);
		unregister_framebuffer(info);
		if (info->cmap.len)
			fb_dealloc_cmap(&info->cmap);
		framebuffer_release(info);
	}
	drm_fb_helper_fini(&fbdev->helper);
	drm_framebuffer_unregister_private(&fbdev->fb.base);
	drm_framebuffer_cleanup(&fbdev->fb.base);
	drm_gem_object_unreference_unlocked(&fbdev->fb.obj->base);
}

int gm12u320_fbdev_init(struct drm_device *dev)
{
	struct gm12u320_device *gm12u320 = dev->dev_private;
	struct gm12u320_fbdev *fbdev;
	int ret;

	fbdev = kzalloc(sizeof(struct gm12u320_fbdev), GFP_KERNEL);
	if (!fbdev)
		return -ENOMEM;

	gm12u320->fbdev = fbdev;

	drm_fb_helper_prepare(dev, &fbdev->helper, &gm12u320_fb_helper_funcs);

	ret = drm_fb_helper_init(dev, &fbdev->helper, 1, 1);
	if (ret)
		goto free;

	ret = drm_fb_helper_single_add_all_connectors(&fbdev->helper);
	if (ret)
		goto fini;

	/* disable all the possible outputs/crtcs before entering KMS mode */
	drm_helper_disable_unused_functions(dev);

	ret = drm_fb_helper_initial_config(&fbdev->helper, 32);
	if (ret)
		goto fini;

	return 0;

fini:
	drm_fb_helper_fini(&fbdev->helper);
free:
	kfree(fbdev);
	return ret;
}

void gm12u320_fbdev_cleanup(struct drm_device *dev)
{
	struct gm12u320_device *gm12u320 = dev->dev_private;
	if (!gm12u320->fbdev)
		return;

	gm12u320_fbdev_destroy(dev, gm12u320->fbdev);
	kfree(gm12u320->fbdev);
	gm12u320->fbdev = NULL;
}

void gm12u320_fbdev_unplug(struct drm_device *dev)
{
	struct gm12u320_device *gm12u320 = dev->dev_private;
	struct gm12u320_fbdev *fbdev;

	if (!gm12u320->fbdev)
		return;

	fbdev = gm12u320->fbdev;
	if (fbdev->helper.fbdev) {
		struct fb_info *info;
		info = fbdev->helper.fbdev;
		unlink_framebuffer(info);
	}
}

struct drm_framebuffer *
gm12u320_fb_user_fb_create(struct drm_device *dev,
		   struct drm_file *file,
		   struct drm_mode_fb_cmd2 *mode_cmd)
{
	struct drm_gem_object *obj;
	struct gm12u320_framebuffer *fb;
	int ret;
	uint32_t size;

	obj = drm_gem_object_lookup(dev, file, mode_cmd->handles[0]);
	if (obj == NULL)
		return ERR_PTR(-ENOENT);

	size = mode_cmd->pitches[0] * mode_cmd->height;
	size = ALIGN(size, PAGE_SIZE);

	if (size > obj->size) {
		DRM_ERROR("object size not sufficient for fb %d %zu %d %d\n", size, obj->size, mode_cmd->pitches[0], mode_cmd->height);
		return ERR_PTR(-ENOMEM);
	}

	fb = kzalloc(sizeof(*fb), GFP_KERNEL);
	if (fb == NULL)
		return ERR_PTR(-ENOMEM);

	ret = gm12u320_framebuffer_init(dev, fb, mode_cmd, to_gm12u320_bo(obj));
	if (ret) {
		kfree(fb);
		return ERR_PTR(-EINVAL);
	}
	return &fb->base;
}
