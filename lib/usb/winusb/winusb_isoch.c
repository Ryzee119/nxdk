/*
 * SPDX-License-Identifier: CC0-1.0
 *
 * WinUSB isochronous API.
 *
 * TinyUSB's OHCI host controller driver does not implement isochronous
 * transfers, so registering a buffer is supported (it is a pure bookkeeping
 * operation) but the transfer functions report ERROR_NOT_SUPPORTED.
 */

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "winusb_internal.h"

#define WINUSB_ISOCH_MAGIC 0x5749534Fu /* 'WISO' */

typedef struct {
    ULONG magic;
    winusb_interface_t *itf;
    PUCHAR buffer;
    ULONG length;
} winusb_isoch_buffer_t;

BOOL WINAPI WinUsb_RegisterIsochBuffer(
    _In_  WINUSB_INTERFACE_HANDLE      InterfaceHandle,
    _In_  UCHAR                        PipeID,
    _Inout_ PUCHAR                     Buffer,
    _In_  ULONG                        BufferLength,
    _Out_ PWINUSB_ISOCH_BUFFER_HANDLE  IsochBufferHandle
)
{
    winusb_interface_t *itf;
    winusb_isoch_buffer_t *isoch;

    if (IsochBufferHandle == NULL) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    *IsochBufferHandle = NULL;

    winusb_lock();

    itf = winusb_interface_validate(InterfaceHandle);
    if (itf == NULL) {
        winusb_unlock();
        return FALSE;
    }

    if (Buffer == NULL || BufferLength == 0) {
        winusb_unlock();
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }

    isoch = (winusb_isoch_buffer_t *)calloc(1, sizeof(*isoch));
    if (isoch == NULL) {
        winusb_unlock();
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return FALSE;
    }

    isoch->magic = WINUSB_ISOCH_MAGIC;
    isoch->itf = itf;
    isoch->buffer = Buffer;
    isoch->length = BufferLength;

    (void)PipeID;

    *IsochBufferHandle = (WINUSB_ISOCH_BUFFER_HANDLE)isoch;
    winusb_unlock();

    return TRUE;
}

BOOL WINAPI WinUsb_UnregisterIsochBuffer(
    _In_ WINUSB_ISOCH_BUFFER_HANDLE IsochBufferHandle
)
{
    winusb_isoch_buffer_t *isoch = (winusb_isoch_buffer_t *)IsochBufferHandle;

    if (isoch == NULL) {
        SetLastError(ERROR_INVALID_HANDLE);
        return FALSE;
    }

    winusb_lock();

    if (isoch->magic != WINUSB_ISOCH_MAGIC) {
        winusb_unlock();
        SetLastError(ERROR_INVALID_HANDLE);
        return FALSE;
    }

    isoch->magic = 0;
    free(isoch);
    winusb_unlock();

    return TRUE;
}

static BOOL winusb_isoch_not_supported(WINUSB_ISOCH_BUFFER_HANDLE buffer, LPOVERLAPPED overlapped)
{
    (void)buffer;

    SetLastError(ERROR_NOT_SUPPORTED);

    if (overlapped != NULL) {
        overlapped->Internal = (ULONG_PTR)ERROR_NOT_SUPPORTED;
        overlapped->InternalHigh = 0;
        if (overlapped->hEvent != NULL) {
            SetEvent(overlapped->hEvent);
        }
    }

    return FALSE;
}

BOOL WINAPI WinUsb_WriteIsochPipe(
    _In_      WINUSB_ISOCH_BUFFER_HANDLE BufferHandle,
    _In_      ULONG                      Offset,
    _In_      ULONG                      Length,
    _Inout_   PULONG                     FrameNumber,
    _Inout_opt_ LPOVERLAPPED             Overlapped
)
{
    (void)Offset;
    (void)Length;
    (void)FrameNumber;
    return winusb_isoch_not_supported(BufferHandle, Overlapped);
}

BOOL WINAPI WinUsb_ReadIsochPipe(
    _In_      WINUSB_ISOCH_BUFFER_HANDLE BufferHandle,
    _In_      ULONG                      Offset,
    _In_      ULONG                      Length,
    _Inout_   PULONG                     FrameNumber,
    _In_      ULONG                      NumberOfPackets,
    _Out_writes_(NumberOfPackets) PUSBD_ISO_PACKET_DESCRIPTOR IsoPacketDescriptors,
    _Inout_opt_ LPOVERLAPPED             Overlapped
)
{
    (void)Offset;
    (void)Length;
    (void)FrameNumber;
    (void)NumberOfPackets;
    (void)IsoPacketDescriptors;
    return winusb_isoch_not_supported(BufferHandle, Overlapped);
}

BOOL WINAPI WinUsb_WriteIsochPipeAsap(
    _In_      WINUSB_ISOCH_BUFFER_HANDLE BufferHandle,
    _In_      ULONG                      Offset,
    _In_      ULONG                      Length,
    _In_      BOOL                       ContinueStream,
    _Inout_opt_ LPOVERLAPPED             Overlapped
)
{
    (void)Offset;
    (void)Length;
    (void)ContinueStream;
    return winusb_isoch_not_supported(BufferHandle, Overlapped);
}

BOOL WINAPI WinUsb_ReadIsochPipeAsap(
    _In_      WINUSB_ISOCH_BUFFER_HANDLE BufferHandle,
    _In_      ULONG                      Offset,
    _In_      ULONG                      Length,
    _In_      BOOL                       ContinueStream,
    _In_      ULONG                      NumberOfPackets,
    _Out_writes_(NumberOfPackets) PUSBD_ISO_PACKET_DESCRIPTOR IsoPacketDescriptors,
    _Inout_opt_ LPOVERLAPPED             Overlapped
)
{
    (void)Offset;
    (void)Length;
    (void)ContinueStream;
    (void)NumberOfPackets;
    (void)IsoPacketDescriptors;
    return winusb_isoch_not_supported(BufferHandle, Overlapped);
}
