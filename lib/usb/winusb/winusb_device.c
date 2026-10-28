/*
 * SPDX-License-Identifier: CC0-1.0
 *
 * WinUSB device & interface lifecycle for nxdk.
 *
 * This file also owns the glue to the TinyUSB host stack: the stack is
 * initialised on first use and - unless the application already initialised it
 * itself - the TinyUSB host task is run from an internal thread so that the
 * blocking WinUSB APIs behave like their Windows counterparts.
 */

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "winusb_internal.h"

/* TinyUSB speed values differ from the WinUSB DEVICE_SPEED values */
static UCHAR winusb_speed_from_tinyusb(tusb_speed_t speed)
{
    switch (speed) {
    case TUSB_SPEED_LOW:  return LowSpeed;
    case TUSB_SPEED_HIGH: return HighSpeed;
    default:              return FullSpeed;
    }
}

/*--------------------------------------------------------------------
 * GLOBAL STATE
 *--------------------------------------------------------------------*/

static winusb_device_t *winusb_device_list;
static CRITICAL_SECTION winusb_cs;
static volatile LONG winusb_cs_state;

static HANDLE winusb_task_thread;
static volatile LONG winusb_stack_state; /* 0 = not started, 1 = starting, 2 = owned */

void winusb_lock(void)
{
    if (winusb_cs_state != 2) {
        if (__sync_bool_compare_and_swap(&winusb_cs_state, 0, 1)) {
            InitializeCriticalSection(&winusb_cs);
            winusb_cs_state = 2;
        } else {
            while (winusb_cs_state != 2) {
                Sleep(0);
            }
        }
    }

    EnterCriticalSection(&winusb_cs);
}

void winusb_unlock(void)
{
    LeaveCriticalSection(&winusb_cs);
}

static DWORD WINAPI winusb_task_thread_proc(LPVOID parameter)
{
    (void)parameter;

    for (;;) {
        tuh_task();
    }

    return 0;
}

static void winusb_stack_init(void)
{
    if (tusb_inited()) {
        /* The application initialised TinyUSB, it has to pump tuh_task() */
        return;
    }

    if (__sync_bool_compare_and_swap(&winusb_stack_state, 0, 1)) {
        if (!tusb_init()) {
            winusb_stack_state = 3;

            return;
        }

        DWORD thread_id = 0;
        winusb_task_thread = CreateThread(NULL, 0, winusb_task_thread_proc, NULL, 0, &thread_id);
        winusb_stack_state = 2;
        return;
    }

    while (winusb_stack_state == 1) {
        Sleep(1);
    }
}

/*--------------------------------------------------------------------
 * DEVICE PATH PARSING
 *--------------------------------------------------------------------*/

static const char *winusb_find_ci(const char *haystack, const char *needle)
{
    const size_t needle_len = strlen(needle);

    for (; *haystack != '\0'; haystack++) {
        size_t i = 0;
        while (i < needle_len) {
            char a = haystack[i];
            char b = needle[i];
            if (a >= 'A' && a <= 'Z') {
                a = (char)(a - 'A' + 'a');
            }
            if (b >= 'A' && b <= 'Z') {
                b = (char)(b - 'A' + 'a');
            }
            if (a != b) {
                break;
            }
            i++;
        }
        if (i == needle_len) {
            return haystack;
        }
    }

    return NULL;
}

static int winusb_hex_digit(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static BOOL winusb_parse_hex(const char *text, int digits, int *value)
{
    int result = 0;

    for (int i = 0; i < digits; i++) {
        const int digit = winusb_hex_digit(text[i]);
        if (digit < 0) {
            return FALSE;
        }
        result = (result << 4) | digit;
    }

    *value = result;
    return TRUE;
}

typedef struct {
    int vid;
    int pid;
    int interface_number;
    int index;
} winusb_device_path_t;

static BOOL winusb_parse_device_path(const char *path, winusb_device_path_t *parsed)
{
    const char *p;

    parsed->vid = -1;
    parsed->pid = -1;
    parsed->interface_number = -1;
    parsed->index = 0;

    if (path == NULL) {
        return FALSE;
    }

    p = winusb_find_ci(path, "vid_");
    if (p == NULL) {
        p = winusb_find_ci(path, "vid=");
    }
    if (p != NULL) {
        winusb_parse_hex(p + 4, 4, &parsed->vid);
    }

    p = winusb_find_ci(path, "pid_");
    if (p == NULL) {
        p = winusb_find_ci(path, "pid=");
    }
    if (p != NULL) {
        winusb_parse_hex(p + 4, 4, &parsed->pid);
    }

    p = winusb_find_ci(path, "mi_");
    if (p != NULL) {
        winusb_parse_hex(p + 3, 2, &parsed->interface_number);
    }

    p = winusb_find_ci(path, "index=");
    if (p != NULL) {
        parsed->index = atoi(p + 6);
    }

    return (parsed->vid >= 0 && parsed->pid >= 0);
}

/*--------------------------------------------------------------------
 * HANDLE LOOKUP
 *--------------------------------------------------------------------*/

winusb_device_t *winusb_device_from_handle(HANDLE handle)
{
    if (handle == NULL || handle == INVALID_HANDLE_VALUE) {
        return NULL;
    }

    for (winusb_device_t *dev = winusb_device_list; dev != NULL; dev = dev->next) {
        if (dev->user_handle == handle && dev->open) {
            return dev;
        }
    }

    return NULL;
}

winusb_interface_t *winusb_interface_from_handle(WINUSB_INTERFACE_HANDLE handle)
{
    if (handle == NULL) {
        return NULL;
    }

    for (winusb_device_t *dev = winusb_device_list; dev != NULL; dev = dev->next) {
        for (UCHAR i = 0; i < dev->interface_count; i++) {
            if ((WINUSB_INTERFACE_HANDLE)dev->interfaces[i] == handle) {
                return dev->interfaces[i];
            }
        }
    }

    return NULL;
}

winusb_interface_t *winusb_interface_validate(WINUSB_INTERFACE_HANDLE handle)
{
    winusb_interface_t *itf = winusb_interface_from_handle(handle);

    if (itf == NULL || itf->device == NULL || !itf->device->open) {
        SetLastError(ERROR_INVALID_HANDLE);
        return NULL;
    }

    return itf;
}

BOOL winusb_device_is_mounted(winusb_device_t *dev)
{
    return (dev != NULL) && tuh_mounted(dev->daddr);
}

/*--------------------------------------------------------------------
 * DESCRIPTOR LOADING
 *--------------------------------------------------------------------*/

BOOL winusb_device_load_descriptors(winusb_device_t *dev)
{
    WINUSB_SETUP_PACKET setup;
    UCHAR header[9];
    ULONG transferred = 0;
    USHORT total_length;
    PUCHAR config;
    tusb_desc_device_t device_desc;

    if (dev->descriptors_loaded) {
        return TRUE;
    }

    if (!tuh_descriptor_get_device_local(dev->daddr, &device_desc)) {
        SetLastError(ERROR_GEN_FAILURE);
        return FALSE;
    }
    memcpy(&dev->device_descriptor, &device_desc, sizeof(dev->device_descriptor));

    memset(&setup, 0, sizeof(setup));
    setup.RequestType = 0x80;                 /* device to host, standard, device */
    setup.Request     = 0x06;                 /* GET_DESCRIPTOR */
    setup.Value       = 0x0200;               /* configuration descriptor, index 0 */
    setup.Index       = 0;
    setup.Length      = sizeof(header);

    if (!winusb_control_transfer_sync(dev, &setup, header, sizeof(header), &transferred) ||
        transferred < sizeof(header)) {
        SetLastError(ERROR_GEN_FAILURE);
        return FALSE;
    }

    total_length = (USHORT)(header[2] | (header[3] << 8));
    if (total_length < sizeof(USB_CONFIGURATION_DESCRIPTOR)) {
        SetLastError(ERROR_GEN_FAILURE);
        return FALSE;
    }

    config = (PUCHAR)malloc(total_length);
    if (config == NULL) {
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return FALSE;
    }

    setup.Length = total_length;
    if (!winusb_control_transfer_sync(dev, &setup, config, total_length, &transferred) ||
        transferred < sizeof(USB_CONFIGURATION_DESCRIPTOR)) {
        free(config);
        SetLastError(ERROR_GEN_FAILURE);
        return FALSE;
    }

    dev->config_descriptor = config;
    dev->config_descriptor_len = (USHORT)transferred;
    dev->descriptors_loaded = TRUE;
    return TRUE;
}

/*--------------------------------------------------------------------
 * INTERFACE TABLE
 *--------------------------------------------------------------------*/

static BOOL winusb_device_build_interfaces(winusb_device_t *dev)
{
    PUSB_CONFIGURATION_DESCRIPTOR config = (PUSB_CONFIGURATION_DESCRIPTOR)dev->config_descriptor;
    PUCHAR position = (PUCHAR)config + config->bLength;
    PUCHAR end = (PUCHAR)config + dev->config_descriptor_len;

    dev->interface_count = 0;

    while ((position + 2) <= end) {
        PUSB_COMMON_DESCRIPTOR descriptor = (PUSB_COMMON_DESCRIPTOR)position;

        if (descriptor->bLength < 2) {
            break;
        }

        if (descriptor->bDescriptorType == 4 /* INTERFACE */ &&
            descriptor->bLength >= sizeof(USB_INTERFACE_DESCRIPTOR)) {
            PUSB_INTERFACE_DESCRIPTOR id = (PUSB_INTERFACE_DESCRIPTOR)descriptor;
            BOOL exists = FALSE;

            for (UCHAR i = 0; i < dev->interface_count; i++) {
                if (dev->interfaces[i]->itf_num == id->bInterfaceNumber) {
                    exists = TRUE;
                    break;
                }
            }

            if (!exists && dev->interface_count < WINUSB_MAX_INTERFACES) {
                PUSB_INTERFACE_DESCRIPTOR active =
                    winusb_find_interface_descriptor(dev, id->bInterfaceNumber, 0);
                winusb_interface_t *itf = (winusb_interface_t *)calloc(1, sizeof(*itf));

                if (itf == NULL) {
                    SetLastError(ERROR_NOT_ENOUGH_MEMORY);
                    return FALSE;
                }

                if (active == NULL) {
                    active = id;
                }

                itf->magic = WINUSB_INTERFACE_MAGIC;
                itf->device = dev;
                itf->itf_num = id->bInterfaceNumber;
                itf->alt_setting = active->bAlternateSetting;
                itf->descriptor = active;
                winusb_interface_fill_pipes(itf);

                dev->interfaces[dev->interface_count++] = itf;
            }
        }

        position += descriptor->bLength;
    }

    if (dev->interface_count == 0) {
        SetLastError(ERROR_NOT_FOUND);
        return FALSE;
    }

    return TRUE;
}

void winusb_device_close_endpoints(winusb_device_t *dev)
{
    for (UCHAR i = 0; i < dev->interface_count; i++) {
        winusb_interface_close_endpoints(dev->interfaces[i]);
    }
}

static void winusb_device_free_interfaces(winusb_device_t *dev)
{
    for (UCHAR i = 0; i < dev->interface_count; i++) {
        if (dev->interfaces[i] != NULL) {
            winusb_interface_close_endpoints(dev->interfaces[i]);
            free(dev->interfaces[i]);
            dev->interfaces[i] = NULL;
        }
    }

    dev->interface_count = 0;
}

/*--------------------------------------------------------------------
 * ENDPOINTS
 *--------------------------------------------------------------------*/

BOOL winusb_interface_open_endpoints(winusb_interface_t *itf)
{
    if (itf->endpoints_open) {
        return TRUE;
    }

    for (UCHAR i = 0; i < itf->descriptor->bNumEndpoints; i++) {
        PUSB_ENDPOINT_DESCRIPTOR endpoint =
            winusb_find_endpoint_descriptor(itf->device, itf->descriptor, i);
        tusb_desc_endpoint_t desc;

        if (endpoint == NULL) {
            continue;
        }

        memset(&desc, 0, sizeof(desc));
        desc.bLength          = sizeof(tusb_desc_endpoint_t);
        desc.bDescriptorType  = TUSB_DESC_ENDPOINT;
        desc.bEndpointAddress = endpoint->bEndpointAddress;
        desc.bmAttributes.xfer = (tusb_xfer_type_t)(endpoint->bmAttributes & 0x03u);
        desc.wMaxPacketSize   = endpoint->wMaxPacketSize;
        desc.bInterval        = endpoint->bInterval;

        /* A class driver may have opened the endpoint already */
        (void)tuh_edpt_open(itf->device->daddr, &desc);
    }

    itf->endpoints_open = TRUE;
    return TRUE;
}

void winusb_interface_close_endpoints(winusb_interface_t *itf)
{
    if (!itf->endpoints_open) {
        return;
    }

    for (UCHAR i = 0; i < itf->descriptor->bNumEndpoints; i++) {
        PUSB_ENDPOINT_DESCRIPTOR endpoint =
            winusb_find_endpoint_descriptor(itf->device, itf->descriptor, i);

        if (endpoint != NULL) {
            (void)tuh_edpt_close(itf->device->daddr, endpoint->bEndpointAddress);
        }
    }

    itf->endpoints_open = FALSE;
}

/*--------------------------------------------------------------------
 * INTERFACE ALTERNATE SETTINGS
 *--------------------------------------------------------------------*/

typedef struct {
    volatile xfer_result_t result;
    HANDLE event;
} winusb_itf_request_t;

static void winusb_set_interface_cb(tuh_xfer_t *transfer)
{
    winusb_itf_request_t *ctx = (winusb_itf_request_t *)(uintptr_t)transfer->user_data;

    ctx->result = transfer->result;
    SetEvent(ctx->event);
}

static BOOL winusb_set_interface_request(winusb_device_t *dev, UCHAR itf_num, UCHAR alt)
{
    winusb_itf_request_t ctx;
    BOOL started;

    ctx.result = XFER_RESULT_FAILED;
    ctx.event = CreateEventA(NULL, TRUE, FALSE, NULL);
    if (ctx.event == NULL) {
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return FALSE;
    }

    started = tuh_interface_set(dev->daddr, itf_num, alt, winusb_set_interface_cb,
                                (uintptr_t)&ctx);
    if (started) {
        WaitForSingleObject(ctx.event, 5000);
    }

    CloseHandle(ctx.event);

    if (!started || ctx.result != XFER_RESULT_SUCCESS) {
        SetLastError(ERROR_GEN_FAILURE);
        return FALSE;
    }

    return TRUE;
}

BOOL winusb_interface_set_alt(winusb_interface_t *itf, UCHAR alt_setting, BOOL with_request)
{
    PUSB_INTERFACE_DESCRIPTOR descriptor;

    descriptor = winusb_find_interface_descriptor(itf->device, itf->itf_num, alt_setting);
    if (descriptor == NULL) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }

    if (with_request && !winusb_set_interface_request(itf->device, itf->itf_num, alt_setting)) {
        return FALSE;
    }

    winusb_interface_close_endpoints(itf);

    itf->alt_setting = alt_setting;
    itf->descriptor = descriptor;
    winusb_interface_fill_pipes(itf);
    winusb_interface_open_endpoints(itf);

    return TRUE;
}

/*--------------------------------------------------------------------
 * DEVICE HANDLES (nxdk platform layer)
 *--------------------------------------------------------------------*/

HANDLE WINAPI WinUsbOpenDevice(LPCSTR DevicePath)
{
    winusb_device_path_t path;
    const DWORD start = GetTickCount();

    if (!winusb_parse_device_path(DevicePath, &path)) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return NULL;
    }

    winusb_stack_init();

    for (;;) {
        int found = 0;
        UCHAR daddr = 0;

        for (UCHAR addr = 1; addr <= (CFG_TUH_DEVICE_MAX + CFG_TUH_HUB); addr++) {
            uint16_t vid = 0;
            uint16_t pid = 0;

            if (!tuh_mounted(addr)) {
                continue;
            }

            if (!tuh_vid_pid_get(addr, &vid, &pid)) {
                continue;
            }

            if (vid != (uint16_t)path.vid || pid != (uint16_t)path.pid) {
                continue;
            }

            if (found == path.index) {
                daddr = addr;
                break;
            }

            found++;
        }

        if (daddr != 0) {
            winusb_device_t *dev = (winusb_device_t *)calloc(1, sizeof(*dev));
            if (dev == NULL) {
                SetLastError(ERROR_NOT_ENOUGH_MEMORY);
                return NULL;
            }

            dev->magic = WINUSB_DEVICE_MAGIC;
            dev->open = TRUE;
            dev->daddr = daddr;
            dev->user_handle = dev;
            dev->selected_interface = (path.interface_number >= 0) ? (UCHAR)path.interface_number : 0;
            dev->speed = winusb_speed_from_tinyusb(tuh_speed_get(daddr));

            winusb_lock();
            dev->next = winusb_device_list;
            winusb_device_list = dev;
            winusb_unlock();

            return (HANDLE)dev;
        }

        if ((GetTickCount() - start) >= 5000) {
            SetLastError(ERROR_FILE_NOT_FOUND);
            return NULL;
        }

        Sleep(10);
    }
}

BOOL WINAPI WinUsbCloseDevice(HANDLE DeviceHandle)
{
    winusb_device_t *dev;
    winusb_device_t **link;

    winusb_lock();

    dev = winusb_device_from_handle(DeviceHandle);
    if (dev == NULL) {
        winusb_unlock();
        SetLastError(ERROR_INVALID_HANDLE);
        return FALSE;
    }

    link = &winusb_device_list;
    while (*link != NULL && *link != dev) {
        link = &(*link)->next;
    }
    if (*link == dev) {
        *link = dev->next;
    }

    dev->open = FALSE;
    winusb_unlock();

    winusb_device_free_interfaces(dev);
    free(dev->config_descriptor);
    free(dev);

    return TRUE;
}

/*--------------------------------------------------------------------
 * WinUsb_Initialize / WinUsb_Free / WinUsb_GetAssociatedInterface
 *--------------------------------------------------------------------*/

BOOL WINAPI WinUsb_Initialize(
    _In_  HANDLE                   DeviceHandle,
    _Out_ PWINUSB_INTERFACE_HANDLE InterfaceHandle
)
{
    winusb_device_t *dev;
    winusb_interface_t *itf = NULL;

    if (InterfaceHandle == NULL) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    *InterfaceHandle = NULL;

    winusb_lock();

    dev = winusb_device_from_handle(DeviceHandle);
    if (dev == NULL) {
        winusb_unlock();
        SetLastError(ERROR_INVALID_HANDLE);
        return FALSE;
    }

    if (!dev->initialized) {

        if (!winusb_device_is_mounted(dev)) {
            winusb_unlock();
            SetLastError(ERROR_DEVICE_NOT_CONNECTED);
            return FALSE;
        }

        if (!winusb_device_load_descriptors(dev) || !winusb_device_build_interfaces(dev)) {
            winusb_device_free_interfaces(dev);
            free(dev->config_descriptor);
            dev->config_descriptor = NULL;
            dev->config_descriptor_len = 0;
            dev->descriptors_loaded = FALSE;
            winusb_unlock();
            return FALSE;
        }

        dev->initialized = TRUE;
    }

    for (UCHAR i = 0; i < dev->interface_count; i++) {
        if (dev->interfaces[i]->itf_num == dev->selected_interface) {
            itf = dev->interfaces[i];
            break;
        }
    }

    if (itf == NULL && dev->interface_count > 0) {
        itf = dev->interfaces[0];
    }

    if (itf == NULL) {
        winusb_unlock();
        SetLastError(ERROR_NOT_FOUND);
        return FALSE;
    }

    winusb_interface_open_endpoints(itf);
    dev->ref_count++;

    *InterfaceHandle = (WINUSB_INTERFACE_HANDLE)itf;
    winusb_unlock();

    return TRUE;
}

BOOL WINAPI WinUsb_Free(
    _In_ WINUSB_INTERFACE_HANDLE InterfaceHandle
)
{
    winusb_interface_t *itf;
    winusb_device_t *dev;

    winusb_lock();

    itf = winusb_interface_validate(InterfaceHandle);
    if (itf == NULL) {
        winusb_unlock();
        return FALSE;
    }

    dev = itf->device;
    winusb_interface_close_endpoints(itf);

    if (dev->ref_count > 0) {
        dev->ref_count--;
    }

    if (dev->ref_count == 0) {
        winusb_device_free_interfaces(dev);
        free(dev->config_descriptor);
        dev->config_descriptor = NULL;
        dev->config_descriptor_len = 0;
        dev->descriptors_loaded = FALSE;
        dev->initialized = FALSE;
    }

    winusb_unlock();
    return TRUE;
}

BOOL WINAPI WinUsb_GetAssociatedInterface(
    _In_  WINUSB_INTERFACE_HANDLE  InterfaceHandle,
    _In_  UCHAR                    AssociatedInterfaceIndex,
    _Out_ PWINUSB_INTERFACE_HANDLE AssociatedInterfaceHandle
)
{
    winusb_interface_t *itf;
    winusb_interface_t *associated;
    winusb_device_t *dev;

    if (AssociatedInterfaceHandle == NULL) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    *AssociatedInterfaceHandle = NULL;

    winusb_lock();

    itf = winusb_interface_validate(InterfaceHandle);
    if (itf == NULL) {
        winusb_unlock();
        return FALSE;
    }

    dev = itf->device;
    if (AssociatedInterfaceIndex >= dev->interface_count) {
        winusb_unlock();
        SetLastError(ERROR_NO_MORE_ITEMS);
        return FALSE;
    }

    associated = dev->interfaces[AssociatedInterfaceIndex];
    winusb_interface_open_endpoints(associated);
    dev->ref_count++;

    *AssociatedInterfaceHandle = (WINUSB_INTERFACE_HANDLE)associated;
    winusb_unlock();

    return TRUE;
}

/*--------------------------------------------------------------------
 * DEVICE / INTERFACE INFORMATION
 *--------------------------------------------------------------------*/

BOOL WINAPI WinUsb_QueryDeviceInformation(
    _In_    WINUSB_INTERFACE_HANDLE InterfaceHandle,
    _In_    ULONG                   InformationType,
    _Inout_ PULONG                  BufferLength,
    _Out_   PVOID                   Buffer
)
{
    winusb_interface_t *itf;
    UCHAR speed;

    if (BufferLength == NULL || Buffer == NULL) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }

    winusb_lock();

    itf = winusb_interface_validate(InterfaceHandle);
    if (itf == NULL) {
        winusb_unlock();
        return FALSE;
    }

    switch (InformationType) {
    case DEVICE_SPEED:
        if (*BufferLength < sizeof(UCHAR)) {
            winusb_unlock();
            SetLastError(ERROR_INSUFFICIENT_BUFFER);
            return FALSE;
        }

        speed = itf->device->speed;
        *(UCHAR *)Buffer = speed;
        *BufferLength = sizeof(UCHAR);
        break;

    default:
        winusb_unlock();
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }

    winusb_unlock();
    return TRUE;
}

BOOL WINAPI WinUsb_QueryInterfaceSettings(
    _In_  WINUSB_INTERFACE_HANDLE   InterfaceHandle,
    _In_  UCHAR                    AlternateInterfaceNumber,
    _Out_ PUSB_INTERFACE_DESCRIPTOR UsbAltInterfaceDescriptor
)
{
    winusb_interface_t *itf;
    PUSB_INTERFACE_DESCRIPTOR descriptor;

    if (UsbAltInterfaceDescriptor == NULL) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }

    winusb_lock();

    itf = winusb_interface_validate(InterfaceHandle);
    if (itf == NULL) {
        winusb_unlock();
        return FALSE;
    }

    descriptor = winusb_find_interface_descriptor(itf->device, itf->itf_num,
                                                  AlternateInterfaceNumber);
    if (descriptor == NULL) {
        winusb_unlock();
        SetLastError(ERROR_NO_MORE_ITEMS);
        return FALSE;
    }

    memcpy(UsbAltInterfaceDescriptor, descriptor, sizeof(USB_INTERFACE_DESCRIPTOR));
    winusb_unlock();

    return TRUE;
}

BOOL WINAPI WinUsb_GetCurrentAlternateSetting(
    _In_  WINUSB_INTERFACE_HANDLE InterfaceHandle,
    _Out_ PUCHAR                  SettingNumber
)
{
    winusb_interface_t *itf;

    if (SettingNumber == NULL) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }

    winusb_lock();

    itf = winusb_interface_validate(InterfaceHandle);
    if (itf == NULL) {
        winusb_unlock();
        return FALSE;
    }

    *SettingNumber = itf->alt_setting;
    winusb_unlock();

    return TRUE;
}

BOOL WINAPI WinUsb_SetCurrentAlternateSetting(
    _In_ WINUSB_INTERFACE_HANDLE InterfaceHandle,
    _In_ UCHAR                   SettingNumber
)
{
    winusb_interface_t *itf;
    BOOL result;

    winusb_lock();

    itf = winusb_interface_validate(InterfaceHandle);
    if (itf == NULL) {
        winusb_unlock();
        return FALSE;
    }

    result = winusb_interface_set_alt(itf, SettingNumber, TRUE);
    winusb_unlock();

    return result;
}

BOOL WINAPI WinUsb_SetCurrentAlternateSettingAsync(
    _In_        WINUSB_INTERFACE_HANDLE InterfaceHandle,
    _In_        UCHAR                   SettingNumber,
    _Inout_opt_ LPOVERLAPPED            Overlapped
)
{
    winusb_interface_t *itf;
    BOOL result;

    winusb_lock();

    itf = winusb_interface_validate(InterfaceHandle);
    if (itf == NULL) {
        winusb_unlock();

        if (Overlapped != NULL) {
            Overlapped->Internal = (ULONG_PTR)GetLastError();
            Overlapped->InternalHigh = 0;
            if (Overlapped->hEvent != NULL) {
                SetEvent(Overlapped->hEvent);
            }
        }

        return FALSE;
    }

    /* The request is short, it is executed synchronously and reported as such */
    result = winusb_interface_set_alt(itf, SettingNumber, TRUE);
    winusb_unlock();

    if (Overlapped != NULL) {
        Overlapped->Internal = result ? 0 : (ULONG_PTR)GetLastError();
        Overlapped->InternalHigh = 0;
        if (Overlapped->hEvent != NULL) {
            SetEvent(Overlapped->hEvent);
        }
    }

    return result;
}
