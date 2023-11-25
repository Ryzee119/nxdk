#include <string.h>
#include <assert.h>
#include <stdlib.h>
#include <stdatomic.h>
#include <stdarg.h>

#include <nxdk/nxusb.h>
#include "tusb.h"
#include "chip.h"

typedef struct nx_usb_data {
    atomic_int count;
    KINTERRUPT irq;
    KDPC dpc;
    KEVENT evt;
    HANDLE irq_thread;
    HANDLE task_thread;
} nx_usb_data_t;

static nx_usb_data_t usb = {0};

static BOOLEAN NTAPI isr(PKINTERRUPT Interrupt, PVOID ServiceContext)
{
    nx_usb_data_t *nx_usb_data = ServiceContext;
    KeInsertQueueDpc(&nx_usb_data->dpc, NULL, NULL);
    return TRUE;
}

static void NTAPI dpc(PKDPC Dpc, PVOID DeferredContext, PVOID arg1, PVOID arg2)
{
    nx_usb_data_t *nx_usb_data = DeferredContext;
    KeSetEvent(&nx_usb_data->evt, IO_KEYBOARD_INCREMENT, FALSE);
    return;
}

static DWORD WINAPI irq_process(LPVOID lpThreadParameter)
{
    nx_usb_data_t *nx_usb_data = lpThreadParameter;
    while (1) {
        KeWaitForSingleObject(&nx_usb_data->evt, Executive, KernelMode, FALSE, NULL);
        tuh_int_handler(0, false);
    }
    return 0;
}

static DWORD WINAPI tusb_thread(LPVOID lpThreadParameter)
{
    DbgPrint("INIT\n");
    tusb_init();
    while (1)
    {
        tuh_task();
    }
}

int tusb_print_hook(const char *format, ...)
{
    char outbuf[256];
    va_list args;
    va_start(args, format);
    vsnprintf(outbuf, sizeof(outbuf), format, args);
    DbgPrint("%s", outbuf);
    return 1;
}

void* tusb_app_virt_to_phys(void *virt_addr) {
    if (virt_addr == NULL) {
        return NULL;
    }
    DbgPrint("%p %p\n", virt_addr, MmGetPhysicalAddress(virt_addr));
    return (void *)MmGetPhysicalAddress(virt_addr);
}

void* tusb_app_phys_to_virt(void *phys_addr) {

}

void hcd_int_enable(uint8_t rhport)
{
    //KeConnectInterrupt(&usb.irq);
}

void hcd_int_disable(uint8_t rhport)
{
    //KeDisconnectInterrupt(&usb.irq);
}

int nxUsbInit()
{
    ULONG irqc, vector;
    KIRQL irql;

    int ref_count = atomic_fetch_add(&usb.count, 1);
    if (ref_count > 0) {
        return 1;
    }

    // Prepare USB interrupts
    irqc = (LPC_USB_BASE == XBOX_USB_OHCI_BASE0) ? 1 : 9;
    vector = HalGetInterruptVector(irqc, &irql);
    KeInitializeInterrupt(&usb.irq, &isr, &usb, vector, irql, LevelSensitive, FALSE);
    KeInitializeDpc(&usb.dpc, dpc, &usb);
    KeInitializeEvent(&usb.evt, SynchronizationEvent, FALSE);
    usb.irq_thread = CreateThread(NULL, 0, irq_process, &usb, 0, NULL);
    usb.task_thread = CreateThread(NULL, 0, (void *)tusb_thread, NULL, 0, NULL);
    if (usb.irq_thread == NULL || usb.task_thread == NULL) {
        if (usb.irq_thread)  CloseHandle(usb.irq_thread);
        if (usb.task_thread) CloseHandle(usb.task_thread);
        return -1;
    }

    KeConnectInterrupt(&usb.irq);

    return 1;
}

void nxUsbShutdown()
{
    int ref_count = atomic_fetch_sub(&usb.count, 1);
    if (ref_count < 0) { 
        ref_count= 0;
    }

    if (ref_count - 1 == 0) {
        KeDisconnectInterrupt(&usb.irq);
    }

    // Close mutex
    // Close sem
    // Close thread
}
