/*
 * SPDX-License-Identifier: CC0-1.0
 *
 * WinUSB pipe and power policy management.
 */

#include <stdint.h>
#include <string.h>

#include "winusb_internal.h"

/* WinUSB accepts boolean policies in 1, 2 or 4 byte form */
static BOOL winusb_read_bool(ULONG length, PVOID value, BOOL *result)
{
    if (value == NULL) {
        return FALSE;
    }

    if (length >= sizeof(BOOL)) {
        *result = (*(const BOOL *)value) != 0;
        return TRUE;
    }

    if (length >= sizeof(USHORT)) {
        *result = (*(const USHORT *)value) != 0;
        return TRUE;
    }

    if (length >= sizeof(UCHAR)) {
        *result = (*(const UCHAR *)value) != 0;
        return TRUE;
    }

    return FALSE;
}

BOOL WINAPI WinUsb_SetPipePolicy(
    _In_ WINUSB_INTERFACE_HANDLE InterfaceHandle,
    _In_ UCHAR                   PipeID,
    _In_ ULONG                   PolicyType,
    _In_ ULONG                   ValueLength,
    _In_ PVOID                   Value
)
{
    winusb_interface_t *itf;
    winusb_pipe_t *pipe;
    BOOL boolean;
    BOOL result = TRUE;

    if (Value == NULL || ValueLength == 0) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }

    winusb_lock();

    itf = winusb_interface_validate(InterfaceHandle);
    if (itf == NULL) {
        winusb_unlock();
        return FALSE;
    }

    pipe = &itf->pipes[WINUSB_PIPE_INDEX(PipeID)];
    if (!pipe->valid || pipe->info.PipeId != PipeID) {
        winusb_unlock();
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }

    switch (PolicyType) {
    case SHORT_PACKET_TERMINATE:
    case AUTO_CLEAR_STALL:
    case IGNORE_SHORT_PACKETS:
    case ALLOW_PARTIAL_READS:
    case AUTO_FLUSH:
    case RAW_IO:
    case RESET_PIPE_ON_RESUME:
        if (!winusb_read_bool(ValueLength, Value, &boolean)) {
            result = FALSE;
            SetLastError(ERROR_INVALID_PARAMETER);
            break;
        }

        switch (PolicyType) {
        case SHORT_PACKET_TERMINATE: pipe->short_packet_terminate = boolean; break;
        case AUTO_CLEAR_STALL:       pipe->auto_clear_stall = boolean;       break;
        case IGNORE_SHORT_PACKETS:   pipe->ignore_short_packets = boolean;   break;
        case ALLOW_PARTIAL_READS:    pipe->allow_partial_reads = boolean;    break;
        case AUTO_FLUSH:             pipe->auto_flush = boolean;             break;
        case RAW_IO:                 pipe->raw_io = boolean;                 break;
        default:                     pipe->reset_pipe_on_resume = boolean;   break;
        }
        break;

    case PIPE_TRANSFER_TIMEOUT:
        if (ValueLength < sizeof(ULONG)) {
            result = FALSE;
            SetLastError(ERROR_INVALID_PARAMETER);
            break;
        }
        pipe->transfer_timeout = *(const ULONG *)Value;
        break;

    case MAXIMUM_TRANSFER_SIZE:
        if (ValueLength < sizeof(ULONG)) {
            result = FALSE;
            SetLastError(ERROR_INVALID_PARAMETER);
            break;
        }
        pipe->maximum_transfer_size = *(const ULONG *)Value;
        break;

    default:
        result = FALSE;
        SetLastError(ERROR_INVALID_PARAMETER);
        break;
    }

    winusb_unlock();
    return result;
}

BOOL WINAPI WinUsb_GetPipePolicy(
    _In_    WINUSB_INTERFACE_HANDLE InterfaceHandle,
    _In_    UCHAR                   PipeID,
    _In_    ULONG                   PolicyType,
    _Inout_ PULONG                  ValueLength,
    _Out_   PVOID                   Value
)
{
    winusb_interface_t *itf;
    winusb_pipe_t *pipe;
    ULONG length;

    if (ValueLength == NULL || Value == NULL) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }

    winusb_lock();

    itf = winusb_interface_validate(InterfaceHandle);
    if (itf == NULL) {
        winusb_unlock();
        return FALSE;
    }

    pipe = &itf->pipes[WINUSB_PIPE_INDEX(PipeID)];
    if (!pipe->valid || pipe->info.PipeId != PipeID) {
        winusb_unlock();
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }

    length = *ValueLength;

    switch (PolicyType) {
    case PIPE_TRANSFER_TIMEOUT:
    case MAXIMUM_TRANSFER_SIZE: {
        const ULONG value = (PolicyType == PIPE_TRANSFER_TIMEOUT)
                                ? pipe->transfer_timeout
                                : pipe->maximum_transfer_size;

        if (length < sizeof(ULONG)) {
            winusb_unlock();
            SetLastError(ERROR_INSUFFICIENT_BUFFER);
            return FALSE;
        }

        *(ULONG *)Value = value;
        *ValueLength = sizeof(ULONG);
        break;
    }

    case SHORT_PACKET_TERMINATE:
    case AUTO_CLEAR_STALL:
    case IGNORE_SHORT_PACKETS:
    case ALLOW_PARTIAL_READS:
    case AUTO_FLUSH:
    case RAW_IO:
    case RESET_PIPE_ON_RESUME: {
        BOOL value;

        if (length < sizeof(BOOL)) {
            winusb_unlock();
            SetLastError(ERROR_INSUFFICIENT_BUFFER);
            return FALSE;
        }

        switch (PolicyType) {
        case SHORT_PACKET_TERMINATE: value = pipe->short_packet_terminate; break;
        case AUTO_CLEAR_STALL:       value = pipe->auto_clear_stall;       break;
        case IGNORE_SHORT_PACKETS:   value = pipe->ignore_short_packets;   break;
        case ALLOW_PARTIAL_READS:    value = pipe->allow_partial_reads;    break;
        case AUTO_FLUSH:             value = pipe->auto_flush;             break;
        case RAW_IO:                 value = pipe->raw_io;                 break;
        default:                     value = pipe->reset_pipe_on_resume;   break;
        }

        *(BOOL *)Value = value;
        *ValueLength = sizeof(BOOL);
        break;
    }

    default:
        winusb_unlock();
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }

    winusb_unlock();
    return TRUE;
}

/*--------------------------------------------------------------------
 * POWER POLICY
 *--------------------------------------------------------------------*/

BOOL WINAPI WinUsb_SetPowerPolicy(
    _In_ WINUSB_INTERFACE_HANDLE InterfaceHandle,
    _In_ ULONG                   PolicyType,
    _In_ ULONG                   ValueLength,
    _In_ PVOID                   Value
)
{
    winusb_interface_t *itf;
    BOOL boolean;
    BOOL result = TRUE;

    if (Value == NULL || ValueLength == 0) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }

    winusb_lock();

    itf = winusb_interface_validate(InterfaceHandle);
    if (itf == NULL) {
        winusb_unlock();
        return FALSE;
    }

    switch (PolicyType) {
    case AUTO_SUSPEND:
        if (!winusb_read_bool(ValueLength, Value, &boolean)) {
            result = FALSE;
            SetLastError(ERROR_INVALID_PARAMETER);
            break;
        }
        itf->device->auto_suspend = boolean;
        break;

    case SUSPEND_DELAY:
        if (ValueLength < sizeof(ULONG)) {
            result = FALSE;
            SetLastError(ERROR_INVALID_PARAMETER);
            break;
        }
        itf->device->suspend_delay = *(const ULONG *)Value;
        break;

    default:
        result = FALSE;
        SetLastError(ERROR_INVALID_PARAMETER);
        break;
    }

    winusb_unlock();
    return result;
}

BOOL WINAPI WinUsb_GetPowerPolicy(
    _In_    WINUSB_INTERFACE_HANDLE InterfaceHandle,
    _In_    ULONG                   PolicyType,
    _Inout_ PULONG                  ValueLength,
    _Out_   PVOID                   Value
)
{
    winusb_interface_t *itf;
    ULONG length;

    if (ValueLength == NULL || Value == NULL) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }

    winusb_lock();

    itf = winusb_interface_validate(InterfaceHandle);
    if (itf == NULL) {
        winusb_unlock();
        return FALSE;
    }

    length = *ValueLength;

    switch (PolicyType) {
    case AUTO_SUSPEND:
        if (length < sizeof(BOOL)) {
            winusb_unlock();
            SetLastError(ERROR_INSUFFICIENT_BUFFER);
            return FALSE;
        }
        *(BOOL *)Value = itf->device->auto_suspend;
        *ValueLength = sizeof(BOOL);
        break;

    case SUSPEND_DELAY:
        if (length < sizeof(ULONG)) {
            winusb_unlock();
            SetLastError(ERROR_INSUFFICIENT_BUFFER);
            return FALSE;
        }
        *(ULONG *)Value = itf->device->suspend_delay;
        *ValueLength = sizeof(ULONG);
        break;

    default:
        winusb_unlock();
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }

    winusb_unlock();
    return TRUE;
}
