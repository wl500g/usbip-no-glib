/*
 * Copyright (C) 2005-2006 Takahiro Hirofuchi
 */

#ifndef _USBIP_STUB_DRIVER_H
#define _USBIP_STUB_DRIVER_H

#include "usbip.h"


struct usbip_stub_driver {
	int ndevs;
	struct sysfs_driver *sysfs_driver;

	struct dlist *edev_list;	/* list of exported device */
};

struct usbip_exported_device {
	struct sysfs_device *sudev;

	int32_t status;
	struct usb_device    udev;
	struct usb_interface uinf[];
};



struct usbip_stub_driver *usbip_stub_driver_open(void);
void usbip_stub_driver_close(struct usbip_stub_driver *driver);
int usbip_stub_refresh_device_list(struct usbip_stub_driver *driver);
int usbip_stub_export_device(struct usbip_stub_driver *driver,
		struct usbip_exported_device *edev, int sockfd);

/* add for SWIG */
struct usbip_exported_device *usbip_stub_get_device(struct usbip_stub_driver *, int num);
#endif
