// Host USB-Serial/JTAG VFS shim. Moving the secondary console onto the driver
// is a no-op here, stdout already reaches the test.
#ifndef FURBLE_HOST_CONSOLE_DRIVER_USB_SERIAL_JTAG_VFS_H
#define FURBLE_HOST_CONSOLE_DRIVER_USB_SERIAL_JTAG_VFS_H

void usb_serial_jtag_vfs_use_driver(void);

#endif
