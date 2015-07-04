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
#include <drm/drmP.h>
#include "gm12u320_drv.h"

#define DATA_RCV_EPT			2
#define DATA_SND_EPT			3

#define DATA_BLOCK_HEADER_SIZE		84
#define DATA_BLOCK_CONTENT_SIZE		64512
#define DATA_BLOCK_FOOTER_SIZE		20
#define DATA_BLOCK_SIZE			DATA_BLOCK_HEADER_SIZE + \
					DATA_BLOCK_CONTENT_SIZE + \
					DATA_BLOCK_FOOTER_SIZE
#define DATA_LAST_BLOCK_CONTENT_SIZE	4032
#define DATA_LAST_BLOCK_SIZE		DATA_BLOCK_HEADER_SIZE + \
					DATA_LAST_BLOCK_CONTENT_SIZE + \
					DATA_BLOCK_FOOTER_SIZE

#define CMD_SIZE			31
#define READ_BLOCK_SIZE			13

#define CMD_TIMEOUT		msecs_to_jiffies(200)
#define DATA_TIMEOUT		msecs_to_jiffies(1000)
#define IDLE_TIMEOUT		msecs_to_jiffies(2000)
#define FIRST_FRAME_TIMEOUT	msecs_to_jiffies(2000)

static const char cmd_data[CMD_SIZE] =
	{0x55, 0x53, 0x42, 0x43, 0x00, 0x00, 0x00, 0x00, 0x68, 0xfc, 0x00, 0x00,
	0x00, 0x00, 0x10, 0xff, 0x00, 0x00, 0x00, 0x00, 0xfc, 0x00, 0x80, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };

static const char cmd_draw[CMD_SIZE] =
	{0x55, 0x53, 0x42, 0x43, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x10, 0xfe, 0x00, 0x00, 0x00, 0xc0, 0xd1, 0x05, 0x00, 0x40,
	0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00 };

static const char data_block_header[DATA_BLOCK_HEADER_SIZE] =
	{ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0xfb, 0x14, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x04, 0x15, 0x00, 0x00, 0xfc, 0x00, 0x00, 0x01, 0x00, 0x00, 0xdb };

static const char data_last_block_header[DATA_BLOCK_HEADER_SIZE] =
	{ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0xfb, 0x14, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x2a, 0x00, 0x20, 0x00, 0xc0, 0x0f, 0x00, 0x00, 0x01, 0x00, 0x00, 0xd7 };

static const char data_block_footer[DATA_BLOCK_FOOTER_SIZE] =
	{ 0xfb, 0x14, 0x02, 0x20, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x80, 0x00, 0x00, 0x4f };

static const char bl_get_set_brightness[CMD_SIZE] =
	{0x55, 0x53, 0x42, 0x43, 0x00, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00,
	0x80, 0x01, 0x10, 0xfd, 0x00, 0x00, 0x00, 0xc0, 0xff, 0x35, 0x00, 0x00,
	0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00 };

static int gm12u320_usb_alloc(struct gm12u320_device *gm12u320)
{
	int i, k;
	int block_size;
	const char *header;

	gm12u320->cmd_buf = kmalloc(CMD_SIZE, GFP_KERNEL);
	if (!gm12u320->cmd_buf)
		return -ENOMEM;

	for (k = 0; k < GM12U320_FRAME_COUNT; k++) {
		for (i = 0; i < GM12U320_BLOCK_COUNT; i++) {
			if (i == GM12U320_BLOCK_COUNT - 1) {
				block_size = DATA_LAST_BLOCK_SIZE;
				header = data_last_block_header;
			} else {
				block_size = DATA_BLOCK_SIZE;
				header = data_block_header;
			}

			gm12u320->data_buf[k][i] = kzalloc(block_size,
							   GFP_KERNEL);
			if (!gm12u320->data_buf[k][i])
				return -ENOMEM;

			memcpy(gm12u320->data_buf[k][i], header,
			       DATA_BLOCK_HEADER_SIZE);
			memcpy(gm12u320->data_buf[k][i] +
					(block_size - DATA_BLOCK_FOOTER_SIZE),
			       data_block_footer, DATA_BLOCK_FOOTER_SIZE);
		}
	}
	return 0;
}

static void gm12u320_usb_free(struct gm12u320_device *gm12u320)
{
	int i, k;

	for (k = 0; k < GM12U320_FRAME_COUNT; k++)
		for (i = 0; i < GM12U320_BLOCK_COUNT; i++)
			kfree(gm12u320->data_buf[k][i]);

	kfree(gm12u320->cmd_buf);
}

void gm12u320_32bpp_to_24bpp_packed(u8 *dst, u8 *src, int len)
{
	while (len--) {
		*dst++ = *src++;
		*dst++ = *src++;
		*dst++ = *src++;
		src++;
	}
}

void gm12u320_update_frame(struct gm12u320_framebuffer *fb)
{
	struct drm_device *dev = fb->base.dev;
	struct gm12u320_device *gm12u320 = dev->dev_private;
	int block, dst_offset, frame, len, remain, ret;
	unsigned long flags;
	u8 *src;
	int x1 = 0;
	int x2 = GM12U320_USER_WIDTH;
	int y1 = 0;
	int y2 = GM12U320_HEIGHT;

	if (!fb->obj->vmapping) {
		ret = gm12u320_gem_vmap(fb->obj);
		if (ret == -ENOMEM) {
			DRM_ERROR("failed to vmap fb\n");
			return;
		}
		if (!fb->obj->vmapping) {
			DRM_ERROR("failed to vmapping\n");
			return;
		}
	}

	spin_lock_irqsave(&gm12u320->frame_lock, flags);
	frame = !gm12u320->current_frame;
	spin_unlock_irqrestore(&gm12u320->frame_lock, flags);

	x1 += (GM12U320_REAL_WIDTH - GM12U320_USER_WIDTH) / 2;
	x2 += (GM12U320_REAL_WIDTH - GM12U320_USER_WIDTH) / 2;

	src = fb->obj->vmapping;
	for (; y1 < y2; y1++) {
		remain = 0;
		len = (x2 - x1) * 3;
		dst_offset = (y1 * GM12U320_REAL_WIDTH + x1) * 3;
		block = dst_offset / DATA_BLOCK_CONTENT_SIZE;
		dst_offset %= DATA_BLOCK_CONTENT_SIZE;

		if ((dst_offset + len) > DATA_BLOCK_CONTENT_SIZE) {
			remain = dst_offset + len - DATA_BLOCK_CONTENT_SIZE;
			len = DATA_BLOCK_CONTENT_SIZE - dst_offset;
		}

		dst_offset += DATA_BLOCK_HEADER_SIZE;
		len /= 3;

		gm12u320_32bpp_to_24bpp_packed(
			gm12u320->data_buf[frame][block] + dst_offset,
			src, len);

		if (remain) {
			block++;
			dst_offset = DATA_BLOCK_HEADER_SIZE;
			gm12u320_32bpp_to_24bpp_packed(
				gm12u320->data_buf[frame][block] + dst_offset,
				src + len * 4, remain / 3);
		}
		src += fb->base.pitches[0];
	}

	spin_lock_irqsave(&gm12u320->frame_lock, flags);
	gm12u320->next_frame = frame;
	spin_unlock_irqrestore(&gm12u320->frame_lock, flags);

	wake_up(&gm12u320->frame_waitq);
}

static int gm12u320_frame_ready(struct gm12u320_device *gm12u320)
{
	int ret;

	spin_lock(&gm12u320->frame_lock);
	ret = gm12u320->next_frame != gm12u320->current_frame;
	spin_unlock(&gm12u320->frame_lock);

	return ret;
}

static void gm12u320_frame_work(struct work_struct *work)
{
	struct gm12u320_device *gm12u320 =
		container_of(work, struct gm12u320_device, frame_work);
	int draw_status_timeout = FIRST_FRAME_TIMEOUT;
	int block, block_size, frame, len, ret;

	while (1) {
		/*
		 * We must draw a frame every 2s otherwise the projector
		 * switches back to showing its logo.
		 */
		wait_event_timeout(gm12u320->frame_waitq,
				   gm12u320_frame_ready(gm12u320),
				   IDLE_TIMEOUT);

		spin_lock(&gm12u320->frame_lock);
		frame = gm12u320->current_frame = gm12u320->next_frame;
		spin_unlock(&gm12u320->frame_lock);

		for (block = 0; block < GM12U320_BLOCK_COUNT; block++) {
			if (block == GM12U320_BLOCK_COUNT - 1)
				block_size = DATA_LAST_BLOCK_SIZE;
			else
				block_size = DATA_BLOCK_SIZE;

			/* Send data command to device */
			memcpy(gm12u320->cmd_buf, cmd_data, CMD_SIZE);
			gm12u320->cmd_buf[8] = block_size & 0xff;
			gm12u320->cmd_buf[9] = block_size >> 8;
			gm12u320->cmd_buf[20] = 0xfc - block * 4;
			gm12u320->cmd_buf[21] = block | (frame << 7);

			ret = usb_bulk_msg(gm12u320->udev,
				   usb_sndbulkpipe(gm12u320->udev, DATA_SND_EPT),
				   gm12u320->cmd_buf, CMD_SIZE, &len, CMD_TIMEOUT);
			if (ret || len != CMD_SIZE)
				goto err;

			/* Send data block to device */
			ret = usb_bulk_msg(gm12u320->udev,
				   usb_sndbulkpipe(gm12u320->udev, DATA_SND_EPT),
				   gm12u320->data_buf[frame][block], block_size,
				   &len, DATA_TIMEOUT);
			if (ret || len != block_size)
				goto err;

			/* Read status */
			ret = usb_bulk_msg(gm12u320->udev,
				   usb_rcvbulkpipe(gm12u320->udev, DATA_RCV_EPT),
				   gm12u320->cmd_buf, READ_BLOCK_SIZE, &len,
				   CMD_TIMEOUT);
			if (ret || len != READ_BLOCK_SIZE)
				goto err;
		}

		/* Send draw command to device */
		memcpy(gm12u320->cmd_buf, cmd_draw, CMD_SIZE);
		ret = usb_bulk_msg(gm12u320->udev,
				   usb_sndbulkpipe(gm12u320->udev, DATA_SND_EPT),
				   gm12u320->cmd_buf, CMD_SIZE, &len, CMD_TIMEOUT);
		if (ret || len != CMD_SIZE)
			goto err;

		/* Read status */
		ret = usb_bulk_msg(gm12u320->udev,
			   usb_rcvbulkpipe(gm12u320->udev, DATA_RCV_EPT),
			   gm12u320->cmd_buf, READ_BLOCK_SIZE, &len,
			   draw_status_timeout);
		if (ret || len != READ_BLOCK_SIZE)
			goto err;

		draw_status_timeout = CMD_TIMEOUT;
	}
err:
	/* Do not log errors caused by module unload or device unplug */
	if (ret != -ENOENT && ret != -ECONNRESET && ret != -ESHUTDOWN &&
	    ret != -ENODEV)
		dev_err(&gm12u320->udev->dev, "Frame update error: %d\n", ret);
}

int gm12u320_driver_load(struct drm_device *dev, unsigned long flags)
{
	struct usb_device *udev = (void*)flags;
	struct gm12u320_device *gm12u320;
	int ret = -ENOMEM;

	DRM_DEBUG("\n");
	gm12u320 = kzalloc(sizeof(struct gm12u320_device), GFP_KERNEL);
	if (!gm12u320)
		return -ENOMEM;

	gm12u320->udev = udev;
	gm12u320->ddev = dev;
	dev->dev_private = gm12u320;

	INIT_WORK(&gm12u320->frame_work, gm12u320_frame_work);
	spin_lock_init(&gm12u320->frame_lock);
	init_waitqueue_head(&gm12u320->frame_waitq);

	/*
	 * These are deliberately different so that we send out an empty
	 * screen to replace the projector logo immediately.
	 */
	gm12u320->current_frame = 1;
	gm12u320->next_frame = 0;

	ret = gm12u320_usb_alloc(gm12u320);
	if (ret)
		goto err;

	gm12u320->frame_workq = create_singlethread_workqueue(DRIVER_NAME);
	if (!gm12u320->frame_workq) {
		ret = -ENOMEM;
		goto err;
	}

	DRM_DEBUG("\n");
	ret = gm12u320_modeset_init(dev);
	if (ret)
		goto err;

	ret = gm12u320_fbdev_init(dev);
	if (ret)
		goto err;

	ret = drm_vblank_init(dev, 1);
	if (ret)
		goto err_fb;

	queue_work(gm12u320->frame_workq, &gm12u320->frame_work);

	return 0;
err_fb:
	gm12u320_fbdev_cleanup(dev);
err:
	gm12u320_usb_free(gm12u320);
	kfree(gm12u320);
	DRM_ERROR("%d\n", ret);
	return ret;
}

int gm12u320_driver_unload(struct drm_device *dev)
{
	struct gm12u320_device *gm12u320 = dev->dev_private;

	/* Wake up frame_work, so that it sees the disconnect and exits */
	wake_up(&gm12u320->frame_waitq);
	cancel_work_sync(&gm12u320->frame_work);
	destroy_workqueue(gm12u320->frame_workq);
	gm12u320_usb_free(gm12u320);

	drm_vblank_cleanup(dev);
	gm12u320_fbdev_cleanup(dev);
	gm12u320_modeset_cleanup(dev);
	kfree(gm12u320);
	return 0;
}
