# $Id$

sbin_PROGRAMS = usbip usbipd usbip_bind_driver

usbip_SOURCES = vhci_attach.c usbip_network.c usbip_network.h
usbipd_SOURCES = stub_server.c usbip_network.c usbip_network.h
usbip_bind_driver_SOURCES = bind-driver.c \
		utils.c utils.h \
		usbip_network.h usbip_network.c

dist_man_MANS = usbip.8 usbipd.8 usbip_bind_driver.8

#usbip_bind_driver_CFLAGS = -Wall -W -Wstrict-prototypes @PACKAGE_CFLAGS@
#usbip_bind_driver_LDADD = @PACKAGE_LIBS@

#AM_CPPFLAGS = -I../include -Wall -std=gnu99
INCLUDES = -I$(top_srcdir)/lib
LDADD = ../lib/libusbip.la @PACKAGE_LIBS@
EXTRA_CFLAGS = @EXTRA_CFLAGS@
AM_CFLAGS = -Wall -W -Wstrict-prototypes -std=gnu99 $(EXTRA_CFLAGS) @PACKAGE_CFLAGS@ -DUSBIDS_FILE='"@USBIDS_DIR@/usb.ids"'

if !HAVE_GLIB
usbip_SOURCES += glib-stub.c glib-stub.h
usbipd_SOURCES += glib-stub.c glib-stub.h
usbip_bind_driver_SOURCES += glib-stub.c glib-stub.h
endif
