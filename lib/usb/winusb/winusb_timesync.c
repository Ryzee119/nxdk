/*
 * SPDX-License-Identifier: CC0-1.0
 *
 * WinUSB frame number / time synchronisation helpers and the overlapped
 * completion helper.
 */

#include <stdint.h>
#include <string.h>

#include "winusb_internal.h"

/* Provided by the nxdk OHCI host controller port (lib/usb/tinyusb_xbox) */
extern uint32_t hcd_frame_number(uint8_t rhport);

BOOL WINAPI WinUsb_GetOverlappedResult(
    _In_  WINUSB_INTERFACE_HANDLE InterfaceHandle,
    _In_  LPOVERLAPPED            lpOverlapped,
    _Out_ LPDWORD                 lpNumberOfBytesTransferred,
    _In_  BOOL                    bWait
)
{
    ULONG_PTR status;

    if (lpOverlapped == NULL) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }

    if (winusb_interface_validate(InterfaceHandle) == NULL) {
        return FALSE;
    }

    for (;;) {
        status = lpOverlapped->Internal;

        if (status != STATUS_PENDING) {
            break;
        }

        if (!bWait) {
            SetLastError(ERROR_IO_INCOMPLETE);
            return FALSE;
        }

        if (lpOverlapped->hEvent != NULL) {
            WaitForSingleObject(lpOverlapped->hEvent, INFINITE);
        } else {
            Sleep(1);
        }
    }

    if (status != 0) {
        SetLastError((DWORD)status);
        return FALSE;
    }

    if (lpNumberOfBytesTransferred != NULL) {
        *lpNumberOfBytesTransferred = (DWORD)lpOverlapped->InternalHigh;
    }

    return TRUE;
}

/*--------------------------------------------------------------------
 * FRAME NUMBER
 *--------------------------------------------------------------------*/

BOOL WINAPI WinUsb_GetCurrentFrameNumber(
    _In_  WINUSB_INTERFACE_HANDLE InterfaceHandle,
    _Out_ PULONG                  CurrentFrameNumber,
    _Out_ LARGE_INTEGER          *TimeStamp
)
{
    winusb_interface_t *itf;
    tuh_bus_info_t bus_info;

    if (CurrentFrameNumber == NULL) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }

    winusb_lock();

    itf = winusb_interface_validate(InterfaceHandle);
    if (itf == NULL) {
        winusb_unlock();
        return FALSE;
    }

    tuh_bus_info_get(itf->device->daddr, &bus_info);
    *CurrentFrameNumber = hcd_frame_number(bus_info.rhport);

    winusb_unlock();

    if (TimeStamp != NULL) {
        QueryPerformanceCounter(TimeStamp);
    }

    return TRUE;
}

BOOL WINAPI WinUsb_GetAdjustedFrameNumber(
    _Inout_ PULONG        CurrentFrameNumber,
    _In_    LARGE_INTEGER TimeStamp
)
{
    LARGE_INTEGER now;
    LARGE_INTEGER frequency;

    if (CurrentFrameNumber == NULL) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }

    if (!QueryPerformanceCounter(&now) || !QueryPerformanceFrequency(&frequency) ||
        frequency.QuadPart <= 0) {
        return FALSE;
    }

    /* Full speed USB has one frame per millisecond */
    const LONGLONG elapsed_us = (now.QuadPart - TimeStamp.QuadPart) * 1000000 / frequency.QuadPart;
    *CurrentFrameNumber += (ULONG)(elapsed_us / 1000);

    return TRUE;
}

BOOL WINAPI WinUsb_GetCurrentFrameNumberAndQpc(
    _In_  WINUSB_INTERFACE_HANDLE                             InterfaceHandle,
    _Out_ PUSB_FRAME_NUMBER_AND_QPC_FOR_TIME_SYNC_INFORMATION FrameNumberAndQpcInfo
)
{
    ULONG frame = 0;
    LARGE_INTEGER timestamp;

    if (FrameNumberAndQpcInfo == NULL) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }

    if (!WinUsb_GetCurrentFrameNumber(InterfaceHandle, &frame, &timestamp)) {
        return FALSE;
    }

    memset(FrameNumberAndQpcInfo, 0, sizeof(*FrameNumberAndQpcInfo));
    FrameNumberAndQpcInfo->CurrentQueryFrameNumber = frame;
    FrameNumberAndQpcInfo->FrameNumberFromHal = frame;
    FrameNumberAndQpcInfo->QueryPerformanceCounterValue = timestamp;
    QueryPerformanceFrequency(&FrameNumberAndQpcInfo->QueryPerformanceCounterFrequency);

    return TRUE;
}

BOOL WINAPI WinUsb_StartTrackingForTimeSync(
    _In_ WINUSB_INTERFACE_HANDLE                      InterfaceHandle,
    _In_ PUSB_START_TRACKING_FOR_TIME_SYNC_INFORMATION StartTrackingInfo
)
{
    (void)StartTrackingInfo;

    if (winusb_interface_validate(InterfaceHandle) == NULL) {
        return FALSE;
    }

    /* Hardware time sync tracking is not available on the Xbox OHCI controller */
    SetLastError(ERROR_NOT_SUPPORTED);
    return FALSE;
}

BOOL WINAPI WinUsb_StopTrackingForTimeSync(
    _In_ WINUSB_INTERFACE_HANDLE                     InterfaceHandle,
    _In_ PUSB_STOP_TRACKING_FOR_TIME_SYNC_INFORMATION StopTrackingInfo
)
{
    (void)StopTrackingInfo;

    if (winusb_interface_validate(InterfaceHandle) == NULL) {
        return FALSE;
    }

    SetLastError(ERROR_NOT_SUPPORTED);
    return FALSE;
}
