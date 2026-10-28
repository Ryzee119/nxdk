/*
 * SPDX-License-Identifier: CC0-1.0
 *
 * WinUSB pipe API: querying pipes, bulk/interrupt transfers, control
 * transfers and pipe management.
 */

#include <stdint.h>
#include <string.h>

#include "winusb_internal.h"

/*--------------------------------------------------------------------
 * PIPE TABLES
 *--------------------------------------------------------------------*/

static void winusb_pipe_init(winusb_pipe_t *pipe, PUSB_ENDPOINT_DESCRIPTOR endpoint)
{
    memset(pipe, 0, sizeof(*pipe));

    pipe->valid = TRUE;
    pipe->info.PipeId = endpoint->bEndpointAddress;
    pipe->info.MaximumPacketSize = endpoint->wMaxPacketSize & 0x07FF;
    pipe->info.Interval = endpoint->bInterval;
    pipe->info.MaximumBytesPerInterval = pipe->info.MaximumPacketSize;

    switch (endpoint->bmAttributes & 0x03u) {
    case 0: pipe->info.PipeType = UsbdPipeTypeControl;     break;
    case 1: pipe->info.PipeType = UsbdPipeTypeIsochronous; break;
    case 2: pipe->info.PipeType = UsbdPipeTypeBulk;        break;
    default: pipe->info.PipeType = UsbdPipeTypeInterrupt;  break;
    }

    /* Default pipe policies (same defaults as winusb.sys) */
    pipe->transfer_timeout = 0;         /* wait forever */
    pipe->allow_partial_reads = TRUE;
}

void winusb_interface_fill_pipes(winusb_interface_t *itf)
{
    memset(itf->pipes, 0, sizeof(itf->pipes));

    for (UCHAR i = 0; i < itf->descriptor->bNumEndpoints; i++) {
        PUSB_ENDPOINT_DESCRIPTOR endpoint =
            winusb_find_endpoint_descriptor(itf->device, itf->descriptor, i);

        if (endpoint != NULL) {
            winusb_pipe_init(&itf->pipes[WINUSB_PIPE_INDEX(endpoint->bEndpointAddress)], endpoint);
        }
    }
}

static winusb_pipe_t *winusb_pipe_get(winusb_interface_t *itf, UCHAR pipe_id)
{
    winusb_pipe_t *pipe = &itf->pipes[WINUSB_PIPE_INDEX(pipe_id)];

    if (!pipe->valid || pipe->info.PipeId != pipe_id) {
        return NULL;
    }

    return pipe;
}

/*--------------------------------------------------------------------
 * QUERY PIPE
 *--------------------------------------------------------------------*/

BOOL WINAPI WinUsb_QueryPipeEx(
    _In_  WINUSB_INTERFACE_HANDLE     InterfaceHandle,
    _In_  UCHAR                       AlternateSettingNumber,
    _In_  UCHAR                       PipeIndex,
    _Out_ PWINUSB_PIPE_INFORMATION_EX PipeInformationEx
)
{
    winusb_interface_t *itf;
    PUSB_INTERFACE_DESCRIPTOR descriptor;
    PUSB_ENDPOINT_DESCRIPTOR endpoint;
    winusb_pipe_t pipe;

    if (PipeInformationEx == NULL) {
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
                                                  AlternateSettingNumber);
    if (descriptor == NULL) {
        winusb_unlock();
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }

    endpoint = winusb_find_endpoint_descriptor(itf->device, descriptor, PipeIndex);
    if (endpoint == NULL) {
        winusb_unlock();
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }

    winusb_pipe_init(&pipe, endpoint);
    *PipeInformationEx = pipe.info;

    winusb_unlock();
    return TRUE;
}

BOOL WINAPI WinUsb_QueryPipe(
    _In_  WINUSB_INTERFACE_HANDLE  InterfaceHandle,
    _In_  UCHAR                    AlternateInterfaceNumber,
    _In_  UCHAR                    PipeIndex,
    _Out_ PWINUSB_PIPE_INFORMATION PipeInformation
)
{
    WINUSB_PIPE_INFORMATION_EX info;

    if (PipeInformation == NULL) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }

    if (!WinUsb_QueryPipeEx(InterfaceHandle, AlternateInterfaceNumber, PipeIndex, &info)) {
        return FALSE;
    }

    PipeInformation->PipeType = info.PipeType;
    PipeInformation->PipeId = info.PipeId;
    PipeInformation->MaximumPacketSize = info.MaximumPacketSize;
    PipeInformation->Interval = info.Interval;

    return TRUE;
}


/*--------------------------------------------------------------------
 * READ / WRITE PIPE
 *--------------------------------------------------------------------*/

static BOOL winusb_pipe_transfer(winusb_interface_t *itf, UCHAR pipe_id, PUCHAR buffer,
                                 ULONG length, PULONG transferred, LPOVERLAPPED overlapped,
                                 BOOL read)
{
    winusb_pipe_t *pipe;
    ULONG actual = 0;
    ULONG timeout;
    BOOL result;

    if (transferred != NULL) {
        *transferred = 0;
    }

    if (buffer == NULL) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }

    winusb_lock();

    pipe = winusb_pipe_get(itf, pipe_id);
    if (pipe == NULL || ((pipe_id & 0x80u) != 0) != read) {
        winusb_unlock();
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }

    timeout = pipe->transfer_timeout;
    winusb_unlock();

    if (length == 0) {
        return TRUE;
    }

    /* Do not hold the global lock while the transfer is in flight */
    result = winusb_xfer(itf, pipe_id, buffer, length, NULL, overlapped, timeout, &actual);

    if (result && transferred != NULL) {
        *transferred = actual;
    }

    return result;
}

BOOL WINAPI WinUsb_ReadPipe(
    _In_      WINUSB_INTERFACE_HANDLE InterfaceHandle,
    _In_      UCHAR                   PipeID,
    _Out_     PUCHAR                  Buffer,
    _In_      ULONG                   BufferLength,
    _Out_opt_ PULONG                  LengthTransferred,
    _Inout_opt_ LPOVERLAPPED          Overlapped
)
{
    winusb_interface_t *itf = winusb_interface_validate(InterfaceHandle);

    if (itf == NULL) {
        return FALSE;
    }

    return winusb_pipe_transfer(itf, PipeID, Buffer, BufferLength, LengthTransferred,
                                Overlapped, TRUE);
}

BOOL WINAPI WinUsb_WritePipe(
    _In_      WINUSB_INTERFACE_HANDLE InterfaceHandle,
    _In_      UCHAR                   PipeID,
    _In_      PUCHAR                  Buffer,
    _In_      ULONG                   BufferLength,
    _Out_opt_ PULONG                  LengthTransferred,
    _Inout_opt_ LPOVERLAPPED          Overlapped
)
{
    winusb_interface_t *itf = winusb_interface_validate(InterfaceHandle);

    if (itf == NULL) {
        return FALSE;
    }

    return winusb_pipe_transfer(itf, PipeID, Buffer, BufferLength, LengthTransferred,
                                Overlapped, FALSE);
}

/*--------------------------------------------------------------------
 * CONTROL TRANSFER
 *--------------------------------------------------------------------*/

BOOL WINAPI WinUsb_ControlTransfer(
    _In_      WINUSB_INTERFACE_HANDLE InterfaceHandle,
    _In_      WINUSB_SETUP_PACKET     SetupPacket,
    _Inout_   PUCHAR                  Buffer,
    _In_      ULONG                   BufferLength,
    _Out_opt_ PULONG                  LengthTransferred,
    _Inout_opt_ LPOVERLAPPED          Overlapped
)
{
    winusb_interface_t *itf;
    WINUSB_SETUP_PACKET setup = SetupPacket;
    ULONG actual = 0;
    BOOL result;

    if (LengthTransferred != NULL) {
        *LengthTransferred = 0;
    }

    itf = winusb_interface_validate(InterfaceHandle);
    if (itf == NULL) {
        return FALSE;
    }

    if (setup.Length > BufferLength) {
        setup.Length = (USHORT)BufferLength;
    }

    if (setup.Length > 0 && Buffer == NULL) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }

    /* usbh enforces CFG_TUH_CONTROL_TIMEOUT_MS internally */
    result = winusb_xfer(itf, 0, Buffer, setup.Length, &setup, Overlapped, 0, &actual);

    if (result && LengthTransferred != NULL) {
        *LengthTransferred = actual;
    }

    return result;
}

/*--------------------------------------------------------------------
 * PIPE MANAGEMENT
 *--------------------------------------------------------------------*/

static BOOL winusb_complete_overlapped(BOOL result, LPOVERLAPPED overlapped)
{
    if (overlapped != NULL) {
        overlapped->Internal = result ? 0 : (ULONG_PTR)GetLastError();
        overlapped->InternalHigh = 0;
        if (overlapped->hEvent != NULL) {
            SetEvent(overlapped->hEvent);
        }
    }

    return result;
}

static PUSB_ENDPOINT_DESCRIPTOR winusb_active_endpoint(winusb_interface_t *itf, UCHAR pipe_id)
{
    for (UCHAR i = 0; i < itf->descriptor->bNumEndpoints; i++) {
        PUSB_ENDPOINT_DESCRIPTOR endpoint =
            winusb_find_endpoint_descriptor(itf->device, itf->descriptor, i);

        if (endpoint != NULL && endpoint->bEndpointAddress == pipe_id) {
            return endpoint;
        }
    }

    return NULL;
}

static void winusb_endpoint_open(winusb_device_t *dev, PUSB_ENDPOINT_DESCRIPTOR endpoint)
{
    tusb_desc_endpoint_t desc;

    memset(&desc, 0, sizeof(desc));
    desc.bLength           = sizeof(tusb_desc_endpoint_t);
    desc.bDescriptorType   = TUSB_DESC_ENDPOINT;
    desc.bEndpointAddress  = endpoint->bEndpointAddress;
    desc.bmAttributes.xfer = (tusb_xfer_type_t)(endpoint->bmAttributes & 0x03u);
    desc.wMaxPacketSize    = endpoint->wMaxPacketSize;
    desc.bInterval         = endpoint->bInterval;

    (void)tuh_edpt_open(dev->daddr, &desc);
}

static BOOL winusb_reset_pipe(winusb_interface_t *itf, UCHAR pipe_id)
{
    winusb_device_t *dev = itf->device;
    PUSB_ENDPOINT_DESCRIPTOR endpoint = winusb_active_endpoint(itf, pipe_id);
    WINUSB_SETUP_PACKET setup;
    ULONG transferred = 0;

    if (endpoint == NULL) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }

    memset(&setup, 0, sizeof(setup));
    setup.RequestType = 0x02;   /* host to device, standard, endpoint */
    setup.Request     = 0x01;   /* CLEAR_FEATURE */
    setup.Value       = 0x0000; /* ENDPOINT_HALT */
    setup.Index       = pipe_id;
    setup.Length      = 0;

    if (!winusb_control_transfer_sync(dev, &setup, NULL, 0, &transferred)) {
        return FALSE;
    }

    /* Reset the host side data toggle by re-opening the endpoint */
    if (itf->endpoints_open) {
        (void)tuh_edpt_close(dev->daddr, pipe_id);
        winusb_endpoint_open(dev, endpoint);
    }

    return TRUE;
}

BOOL WINAPI WinUsb_ResetPipe(
    _In_ WINUSB_INTERFACE_HANDLE InterfaceHandle,
    _In_ UCHAR                   PipeID
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

    result = winusb_reset_pipe(itf, PipeID);
    winusb_unlock();

    return result;
}

BOOL WINAPI WinUsb_ResetPipeAsync(
    _In_        WINUSB_INTERFACE_HANDLE InterfaceHandle,
    _In_        UCHAR                   PipeID,
    _Inout_opt_ LPOVERLAPPED            Overlapped
)
{
    return winusb_complete_overlapped(WinUsb_ResetPipe(InterfaceHandle, PipeID), Overlapped);
}

static BOOL winusb_abort_pipe(winusb_interface_t *itf, UCHAR pipe_id)
{
    if (winusb_active_endpoint(itf, pipe_id) == NULL) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }

    /* The OHCI host driver does not implement transfer abort, best effort */
    (void)tuh_edpt_abort_xfer(itf->device->daddr, pipe_id);
    return TRUE;
}

BOOL WINAPI WinUsb_AbortPipe(
    _In_ WINUSB_INTERFACE_HANDLE InterfaceHandle,
    _In_ UCHAR                   PipeID
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

    result = winusb_abort_pipe(itf, PipeID);
    winusb_unlock();

    return result;
}

BOOL WINAPI WinUsb_AbortPipeAsync(
    _In_        WINUSB_INTERFACE_HANDLE InterfaceHandle,
    _In_        UCHAR                   PipeID,
    _Inout_opt_ LPOVERLAPPED            Overlapped
)
{
    return winusb_complete_overlapped(WinUsb_AbortPipe(InterfaceHandle, PipeID), Overlapped);
}

BOOL WINAPI WinUsb_FlushPipe(
    _In_ WINUSB_INTERFACE_HANDLE InterfaceHandle,
    _In_ UCHAR                   PipeID
)
{
    winusb_interface_t *itf;

    winusb_lock();

    itf = winusb_interface_validate(InterfaceHandle);
    if (itf == NULL) {
        winusb_unlock();
        return FALSE;
    }

    if (winusb_active_endpoint(itf, PipeID) == NULL || winusb_pipe_get(itf, PipeID) == NULL) {
        winusb_unlock();
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }

    /* Nothing is buffered on the host side; the device FIFO can not be flushed */
    winusb_unlock();
    return TRUE;
}
