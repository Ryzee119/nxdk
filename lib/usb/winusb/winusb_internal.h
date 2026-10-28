/*
 * SPDX-License-Identifier: CC0-1.0
 *
 * Internal definitions for the nxdk WinUSB implementation.
 */

#ifndef WINUSB_INTERNAL_H_
#define WINUSB_INTERNAL_H_

#include <windows.h>
#include <winusb.h>

#include "tusb.h"

#define WINUSB_DEVICE_MAGIC    0x57424456u /* 'WBDV' */
#define WINUSB_INTERFACE_MAGIC 0x57424956u /* 'WBIV' */

#define WINUSB_MAX_DEVICES     8
#define WINUSB_MAX_INTERFACES  16
#define WINUSB_MAX_PIPES       32

/* tuh_xfer_t / usbh transfer length is a 16-bit value */
#define WINUSB_XFER_CHUNK      0xFFFFu

/* Endpoint address <-> pipe table index (0-15 OUT, 16-31 IN) */
#define WINUSB_PIPE_INDEX(pipe_id) (((pipe_id) & 0x0Fu) + (((pipe_id) & 0x80u) ? 16u : 0u))

typedef struct _WINUSB_PIPE {
    BOOL  valid;
    WINUSB_PIPE_INFORMATION_EX info;

    /* Pipe policies (WINUSB_PIPE_INFORMATION / SHORT_PACKET_TERMINATE etc.) */
    BOOL  short_packet_terminate;
    BOOL  auto_clear_stall;
    ULONG transfer_timeout;      /* PIPE_TRANSFER_TIMEOUT, milliseconds, 0 = wait forever */
    BOOL  ignore_short_packets;
    BOOL  allow_partial_reads;
    BOOL  auto_flush;
    BOOL  raw_io;
    ULONG maximum_transfer_size;
    BOOL  reset_pipe_on_resume;
} winusb_pipe_t;

typedef struct _WINUSB_DEVICE winusb_device_t;

typedef struct _WINUSB_INTERFACE {
    ULONG magic;
    winusb_device_t *device;
    UCHAR itf_num;
    UCHAR alt_setting;
    PUSB_INTERFACE_DESCRIPTOR descriptor; /* points into device->config_descriptor */
    winusb_pipe_t pipes[WINUSB_MAX_PIPES];
    BOOL endpoints_open;
} winusb_interface_t;

struct _WINUSB_DEVICE {
    ULONG magic;
    HANDLE user_handle;              /* handle returned by WinUsbOpenDevice() */
    BOOL  open;
    BOOL  initialized;
    BOOL  descriptors_loaded;

    UCHAR daddr;                     /* TinyUSB device address */
    UCHAR speed;                     /* LowSpeed / FullSpeed / HighSpeed */
    UCHAR selected_interface;        /* interface requested through the device path */
    UCHAR reserved;

    USB_DEVICE_DESCRIPTOR device_descriptor;
    UCHAR *config_descriptor;
    USHORT config_descriptor_len;

    winusb_interface_t *interfaces[WINUSB_MAX_INTERFACES];
    UCHAR interface_count;

    /* Power policy */
    BOOL  auto_suspend;
    ULONG suspend_delay;

    winusb_device_t *next;
    uint32_t ref_count;
};

/*--------------------------------------------------------------------
 * winusb_device.c
 *--------------------------------------------------------------------*/
void winusb_lock(void);
void winusb_unlock(void);

BOOL winusb_device_is_mounted(winusb_device_t *dev);
BOOL winusb_device_load_descriptors(winusb_device_t *dev);
void winusb_device_close_endpoints(winusb_device_t *dev);

winusb_device_t *winusb_device_from_handle(HANDLE handle);
winusb_interface_t *winusb_interface_from_handle(WINUSB_INTERFACE_HANDLE handle);
winusb_interface_t *winusb_interface_validate(WINUSB_INTERFACE_HANDLE handle);

BOOL winusb_interface_set_alt(winusb_interface_t *itf, UCHAR alt_setting, BOOL with_request);
void winusb_interface_fill_pipes(winusb_interface_t *itf);
BOOL winusb_interface_open_endpoints(winusb_interface_t *itf);
void winusb_interface_close_endpoints(winusb_interface_t *itf);

/*--------------------------------------------------------------------
 * winusb_descriptors.c
 *--------------------------------------------------------------------*/
PUSB_INTERFACE_DESCRIPTOR winusb_find_interface_descriptor(winusb_device_t *dev,
                                                           LONG itf_num, LONG alt_setting);
PUSB_ENDPOINT_DESCRIPTOR winusb_find_endpoint_descriptor(winusb_device_t *dev,
                                                         PUSB_INTERFACE_DESCRIPTOR itf,
                                                         UCHAR index);

/*--------------------------------------------------------------------
 * winusb_xfer.c
 *--------------------------------------------------------------------*/
/*
 * Performs (or starts, when Overlapped is non NULL) a transfer on an endpoint.
 * For control transfers pass setup != NULL and ep_addr = 0.
 * length must be the number of data bytes (setup->Length for control).
 */
BOOL winusb_xfer(winusb_interface_t *itf, UCHAR ep_addr, PUCHAR buffer, ULONG length,
                 WINUSB_SETUP_PACKET const *setup, LPOVERLAPPED overlapped,
                 ULONG timeout_ms, PULONG transferred);

/* Synchronous convenience wrapper used internally for control requests. */
BOOL winusb_control_transfer_sync(winusb_device_t *dev, WINUSB_SETUP_PACKET const *setup,
                                  PUCHAR buffer, ULONG length, PULONG transferred);

/* Completes an overlapped transfer stored in the OVERLAPPED structure. */
BOOL winusb_overlapped_complete(LPOVERLAPPED overlapped, DWORD error, DWORD transferred);

#endif /* WINUSB_INTERNAL_H_ */
