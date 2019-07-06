/*
 * Copyright (C) 2012-2016 Red Hat Inc.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License v2. See the file COPYING in the main directory of this archive for
 * more details.
 */

#include <linux/module.h>
#include <drm/drmP.h>
#include <drm/drm_crtc_helper.h>
#include <drm/drm_probe_helper.h>
#include "gm12u320_drv.h"

DEFINE_DRM_GEM_SHMEM_FOPS(gm12u320_driver_fops);

static struct drm_driver driver = {
	.driver_features = DRIVER_MODESET | DRIVER_GEM | DRIVER_PRIME,
	.load = gm12u320_driver_load,
	.unload = gm12u320_driver_unload,
	.release = gm12u320_driver_release,

	.name = DRIVER_NAME,
	.desc = DRIVER_DESC,
	.date = DRIVER_DATE,
	.major = DRIVER_MAJOR,
	.minor = DRIVER_MINOR,

	/* gem hooks */
	.fops = &gm12u320_driver_fops,
	DRM_GEM_SHMEM_DRIVER_OPS,
};

static int gm12u320_usb_probe(struct usb_interface *interface,
			      const struct usb_device_id *id)
{
	struct usb_device *udev = interface_to_usbdev(interface);
	struct drm_device *dev;
	int r;

	/*
	 * The gm12u320 presents itself to the system as 2 usb mass-storage
	 * interfaces, for the second one we proceed successully with binding,
	 * but otherwise ignore it.
	 */
	if (interface->cur_altsetting->desc.bInterfaceNumber != 0)
		return 0;

	dev = drm_dev_alloc(&driver, &interface->dev);
	if (IS_ERR(dev))
		return PTR_ERR(dev);

	r = drm_dev_register(dev, (unsigned long)udev);
	if (r)
		goto err_free;

	usb_set_intfdata(interface, dev);
	DRM_INFO("Initialized gm12u320 on minor %d\n", dev->primary->index);

	return 0;

err_free:
	drm_dev_put(dev);
	return r;
}

static void gm12u320_usb_disconnect(struct usb_interface *interface)
{
	struct drm_device *dev = usb_get_intfdata(interface);

	if (!dev)
		return;

	drm_kms_helper_poll_disable(dev);
	gm12u320_fbdev_unplug(dev);
	gm12u320_stop_fb_update(dev);
	drm_dev_unplug(dev);
	drm_dev_put(dev);
}

#ifdef CONFIG_PM

int gm12u320_suspend(struct usb_interface *interface, pm_message_t message)
{
	struct drm_device *dev = usb_get_intfdata(interface);

	if (!dev)
		return 0;

	gm12u320_stop_fb_update(dev);
	return 0;
}

int gm12u320_resume(struct usb_interface *interface)
{
	struct drm_device *dev = usb_get_intfdata(interface);

	if (!dev)
		return 0;

	gm12u320_set_ecomode(dev);
	gm12u320_start_fb_update(dev);
	return 0;
}
#endif

static const struct usb_device_id id_table[] = {
	{ USB_DEVICE(0x1de1, 0xc102) },
	{},
};
MODULE_DEVICE_TABLE(usb, id_table);

static struct usb_driver gm12u320_driver = {
	.name = "gm12u320",
	.probe = gm12u320_usb_probe,
	.disconnect = gm12u320_usb_disconnect,
	.id_table = id_table,
#ifdef CONFIG_PM
	.suspend = gm12u320_suspend,
	.resume = gm12u320_resume,
	.reset_resume = gm12u320_resume,
#endif
};

module_usb_driver(gm12u320_driver);
MODULE_AUTHOR("Hans de Goede <hdegoede@redhat.com>");
MODULE_LICENSE("GPL");
