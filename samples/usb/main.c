#include <hal/debug.h>
#include <hal/video.h>
#include <windows.h>
#include <usbh_lib.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdbool.h>

/*
 * This demo will initialise the usb stack, detect events on the usb ports
 * and print the device PID and VID.
*/
void dump_device_descriptor(DESC_DEV_T *desc);
void dump_config_descriptor(DESC_CONF_T *desc);
bool write_to_file = false;
char buff[256];
FILE *d;
void USB_debug(const char *format, ...)
{
    if (write_to_file == false)
    {
        return;
    }
    va_list args;
    va_start(args, format);
    vsnprintf(buff, sizeof(buff), format, args);
    va_end(args);
    fwrite(buff, strlen(buff) , 1, d);
}

void device_connection_callback(UDEV_T *udev, int status)
{
    debugPrint("Device connected on USB hub port %u (VID: %04x PID: %04x)\n", udev->port_num,
                                                                              udev->descriptor.idVendor,
                                                                              udev->descriptor.idProduct);
    char filename[MAX_PATH];
    sprintf(filename, "D:\\USB_DESCRIPTORS_%d_%04x_%04X.txt", udev->port_num, udev->descriptor.idVendor, udev->descriptor.idProduct);
    d = fopen(filename, "w");
    if (d)
    {
        write_to_file = true;
        dump_device_descriptor(&udev->descriptor);
        dump_config_descriptor((DESC_CONF_T *)udev->cfd_buff);
        write_to_file = false;
        fclose(d);
        debugPrint("Wrote descriptors to %s\n", filename);
    }
    else
    {
        debugPrint("Could not open %s for writing\n", filename);
    }
}

void device_disconnect_callback(UDEV_T *udev, int status)
{
     debugPrint("Device disconnected on USB hub port %u (VID: %04x PID: %04x)\n", udev->port_num,
                                                                                  udev->descriptor.idVendor,
                                                                                  udev->descriptor.idProduct);
}

int main(void)
{
    XVideoSetMode(640, 480, 32, REFRESH_DEFAULT);

    usbh_core_init();
    usbh_install_conn_callback(&device_connection_callback, &device_disconnect_callback);
    debugPrint("Plug and unplug USB devices to print the USB device VID and PID\n");

    while (1) {
        usbh_pooling_hubs();
    }

    //Never reached, but shown for clarity
    usbh_core_deinit();
    return 0;
}
