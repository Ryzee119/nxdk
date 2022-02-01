#include <hal/debug.h>
#include <hal/video.h>
#include <windows.h>
#include <stdarg.h>
#include "tusb.h"
#include "chip.h"

#ifdef CFG_TUSB_DEBUG_PRINTF
#define DEBUG_MAX_LEN 256
static char outbuf[DEBUG_MAX_LEN];
int CFG_TUSB_DEBUG_PRINTF(const char *format, ...)
{

    // This can be in IRQ context. Store message and output to user later
    va_list args;
    va_start(args, format);
    vsnprintf(outbuf, DEBUG_MAX_LEN, format, args);
    DbgPrint("%s", outbuf);
    debugPrint("%s", outbuf);
    return 1;
}
#endif

static KINTERRUPT InterruptObject;
static BOOLEAN __stdcall ISR(PKINTERRUPT Interrupt, PVOID ServiceContext)
{
    tuh_int_handler(0);
    return TRUE;
}

static void tusb_thread()
{
    tusb_init();
    while (1)
    {
        tuh_task();
    }
}

int main(void)
{
    XVideoSetMode(640, 480, 32, REFRESH_DEFAULT);
    KIRQL irql;
    ULONG vector = HalGetInterruptVector(1, &irql);
    KeInitializeInterrupt(&InterruptObject,
                          &ISR,
                          NULL,
                          vector,
                          irql,
                          LevelSensitive,
                          FALSE);
    CreateThread(NULL, 0, (void *)tusb_thread, NULL, 0, NULL);

    while (1)
    {
        Sleep(1000);
    }
    return 0;
}

void hcd_int_enable(uint8_t rhport)
{
    KeConnectInterrupt(&InterruptObject);
}

void hcd_int_disable(uint8_t rhport)
{
    KeDisconnectInterrupt(&InterruptObject);
}

void tuh_hid_report_received_cb(uint8_t dev_addr, uint8_t instance, uint8_t const *report, uint16_t len)
{
    TU_LOG1("HID Got data: ");
    TU_LOG1_MEM(report, len, 4);
    tuh_hid_receive_report(dev_addr, instance);
}

void tuh_hid_umount_cb(uint8_t dev_addr, uint8_t instance)
{
    TU_LOG1("HID UNMOUNTED %02x %d\n", dev_addr, instance);
}

void tuh_hid_mount_cb(uint8_t dev_addr, uint8_t instance, uint8_t const *desc_report, uint16_t desc_len)
{
    TU_LOG1("HID MOUNTED %02x %d\n", dev_addr, instance);
    tuh_hid_receive_report(dev_addr, instance);
}

void tuh_xinput_report_received_cb(uint8_t dev_addr, uint8_t instance, uint8_t const *report, uint16_t len)
{
    xinputh_interface_t *xid_itf = (xinputh_interface_t *)report;
    // TU_LOG1("XINPUT Got data on dev%02x, len %02x:\n", dev_addr, len);
    // TU_LOG1_MEM(report, len, 4);
    tuh_xinput_receive_report(dev_addr, instance);
}

void tuh_xinput_mount_cb(uint8_t dev_addr, uint8_t instance, const xinputh_interface_t *xinput_itf)
{
    TU_LOG1("XINPUT MOUNTED %02x %d\n", dev_addr, instance);

    // If this is a Xbox 360 Wireless controller, dont register yet. Need to wait for a connection packet
    // on the in pipe.
    if (xinput_itf->type == XBOX360_WIRELESS && xinput_itf->connected == false)
    {
        tuh_xinput_receive_report(dev_addr, instance);
        return;
    }

    tuh_xinput_set_led(dev_addr, instance, 0, true);
    tuh_xinput_set_led(dev_addr, instance, 1, true);
    tuh_xinput_set_rumble(dev_addr, instance, 0, 0, true);
    tuh_xinput_receive_report(dev_addr, instance);
}

void tuh_xinput_umount_cb(uint8_t dev_addr, uint8_t instance)
{
    TU_LOG1("XINPUT UNMOUNTED %02x %d\n", dev_addr, instance);
}
