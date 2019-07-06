/*
 * Copyright (C) 2012-2016 Red Hat Inc.
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

#ifndef GM12U320_DRV_H
#define GM12U320_DRV_H

#include <linux/usb.h>
#include <linux/spinlock.h>
#include <linux/mm_types.h>
#include <drm/drm_gem.h>
#include <drm/drm_gem_shmem_helper.h>

#define DRIVER_NAME		"gm12u320"
#define DRIVER_DESC		"Grain Media GM12U320 USB projector display"
#define DRIVER_DATE		"20150107"

#define DRIVER_MAJOR		0
#define DRIVER_MINOR		0
#define DRIVER_PATCHLEVEL	1

#define GM12U320_BO_CACHEABLE	(1 << 0)
#define GM12U320_BO_WC		(1 << 1)

/*
 * The DLP has an actual width of 854 pixels, but that is not a multiple
 * of 8, breaking things left and right, so we export a width of 848.
 */
#define GM12U320_USER_WIDTH	848
#define GM12U320_REAL_WIDTH	854
#define GM12U320_HEIGHT		480

#define GM12U320_BLOCK_COUNT	20

struct gm12u320_device;

struct gm12u320_fbdev;

struct gm12u320_device {
	struct device *dev;
	struct usb_device *udev;
	struct drm_device *ddev;
	struct gm12u320_fbdev *fbdev;
	struct mutex gem_lock;
	unsigned char *cmd_buf;
	unsigned char *data_buf[GM12U320_BLOCK_COUNT];
	struct {
		bool run;
		struct workqueue_struct *workq;
		struct work_struct work;
		wait_queue_head_t waitq;
		struct mutex lock;
		struct gm12u320_framebuffer *fb;
		int x1;
		int x2;
		int y1;
		int y2;
	} fb_update;
};

struct gm12u320_framebuffer {
	struct drm_framebuffer base;
	struct drm_gem_shmem_object *obj;
};

#define to_gm12u320_fb(x) container_of(x, struct gm12u320_framebuffer, base)

/* modeset */
int gm12u320_modeset_init(struct drm_device *dev);
void gm12u320_modeset_cleanup(struct drm_device *dev);
int gm12u320_connector_init(struct drm_device *dev,
			    struct drm_encoder *encoder);

struct drm_encoder *gm12u320_encoder_init(struct drm_device *dev);

int gm12u320_driver_load(struct drm_device *dev, unsigned long flags);
void gm12u320_driver_unload(struct drm_device *dev);
void gm12u320_driver_release(struct drm_device *dev);

int gm12u320_fbdev_init(struct drm_device *dev);
void gm12u320_fbdev_cleanup(struct drm_device *dev);
void gm12u320_fbdev_unplug(struct drm_device *dev);
struct drm_framebuffer *
gm12u320_fb_user_fb_create(struct drm_device *dev, struct drm_file *file,
			   const struct drm_mode_fb_cmd2 *mode_cmd);
void gm12u320_fb_mark_dirty(struct gm12u320_framebuffer *fb,
			    int x1, int x2, int y1, int y2);
void gm12u320_start_fb_update(struct drm_device *dev);
void gm12u320_stop_fb_update(struct drm_device *dev);
int gm12u320_set_ecomode(struct drm_device *dev);

#endif
